// Un deformeur qui plie une geometrie le long d'une courbe.
//
// C'est l'envers du tube. Le tube fabrique une geometrie a partir d'un repere
// qui avance le long d'un chemin ; le deformeur prend une geometrie existante
// et la reecrit dans ce meme repere. Les deux tirent donc du meme coeur,
// `native/common/curve_core.h`, et c'est la seule raison pour laquelle ce
// fichier est court : la longueur d'arc et le repere a torsion minimale, qui
// sont le difficile, y sont deja resolus.
//
// Trois choses qui ne se devinent pas et qui ont dicte la forme du code :
//
//   - `cb_deform` est appele EN PARALLELE sur des plages de sommets disjointes.
//     Rien ne peut donc y etre construit ni memorise : tout l'etat partage --
//     la courbe echantillonnee, l'etendue de la geometrie, le repere de
//     depart -- est calcule dans `cb_pre_deform`, qui passe une seule fois, et
//     n'est plus qu'en lecture ensuite.
//
//   - le repere a torsion minimale part d'une amorce arbitraire. Laissee
//     telle quelle, une courbe parfaitement droite le long de l'axe rendrait
//     la section tournee d'un quart de tour, sans que rien dans la scene ne
//     l'explique. On mesure une fois l'ecart entre ce repere de depart et
//     l'axe de reference de la geometrie, et on applique cette rotation
//     constante partout -- constante, donc elle ne reintroduit pas de torsion.
//
//   - `Deformer` est `embedded_only` : la node ne se cree pas dans un contexte,
//     elle se cree depuis l'attribut `deformers` d'une geometrie
//     (`ix.cmds.AddValues([geo + ".deformers"], ["DeformerCurve"])`).

#include <cmath>

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_deformer.h>
#include <module_geometry.h>
#include <module_scene_item.h>

#include <geometry_object.h>

#include <core_vector.h>
#include <gmath_vec3.h>
#include <gmath_bbox3.h>
#include <gmath_matrix4x4.h>

#include <curve_core.h>

#include <curve_deform.cma>

using curve_core::vadd;
using curve_core::vsub;
using curve_core::vscale;
using curve_core::vdot;
using curve_core::vcross;
using curve_core::vnorm;
using curve_core::vlerp;

class CurveDeformModule : public ModuleDeformer {
public:
    CurveDeformModule()
        : ModuleDeformer()
        , m_ready(false)
        , m_axis(0u), m_axis_v(1u), m_axis_w(2u)
        , m_origin(0.0), m_scale(1.0), m_weight(1.0)
        , m_roll_cos(1.0), m_roll_sin(0.0)
        , m_global(false)
    {
    }

    // Tout l'etat partage se calcule ici, et nulle part ailleurs.
    bool
    prepare(const ModuleGeometry& geometry)
    {
        m_ready = false;

        OfObject *object = get_object();
        if (object == 0) return false;

        CoreVector<GMathVec3d> cv;
        if (!curve_core::gather_control_points(*object, "control_points", cv)) {
            return false;
        }

        const bool closed = object->get_attribute("closed")->get_bool();
        const unsigned int steps =
            (unsigned int) object->get_attribute("steps")->get_long();
        if (!m_path.build(cv, closed, steps)) return false;
        if (m_path.get_length() <= 1e-9) return false;

        long axis = object->get_attribute("axis")->get_long();
        if (axis < 0) axis = 0;
        if (axis > 2) axis = 2;
        m_axis = (unsigned int) axis;
        // Permutation circulaire -- (X,Y,Z), (Y,Z,X), (Z,X,Y). Elle garde le
        // triedre direct : avec un repere (tangente, normale, binormale) lui
        // aussi direct, une courbe droite le long de l'axe laisse la geometrie
        // intacte au lieu de la retourner.
        m_axis_v = (m_axis + 1u) % 3u;
        m_axis_w = (m_axis + 2u) % 3u;

        m_weight = object->get_attribute("weight")->get_double();

        // `local_deformation` vient de la classe Deformer. Coche, la courbe est
        // lue dans l'espace de l'objet : deplacer la geometrie emmene la
        // deformation avec elle. Decoche, les sommets passent en monde, se
        // plient contre la courbe la ou les locators se trouvent reellement,
        // puis reviennent -- la geometrie se pose sur la courbe quelle que soit
        // sa propre transformation.
        m_global = !object->get_attribute("local_deformation")->get_bool();
        if (m_global) {
            m_to_world = geometry.get_global_matrix();
            m_from_world = geometry.get_inv_global_matrix();
        }

        // L'etendue de la geometrie le long de l'axe, lue sur la geometrie NON
        // deformee. A cet instant Clarisse l'a forcement deja construite --
        // c'est la source de la deformation en cours -- donc l'accesseur rend
        // son pointeur sans reprendre le verrou de construction.
        bool measured = false;
        double lo = 0.0, hi = 0.0;
        const GeometryObject *source = geometry.get_geometry(false);
        if (source != 0) {
            GMathBbox3d bbox = source->get_bbox();
            if (m_global) {
                GMathBbox3d world = bbox;
                bbox.transform_bbox_and_get_bbox(m_to_world, world);
                bbox = world;
            }
            if (!bbox.is_empty()) {
                lo = bbox.get_min()[m_axis];
                hi = bbox.get_max()[m_axis];
                measured = true;
            }
        }

        // Sans etendue mesurable, on ne peut pas etirer ; l'origine de l'objet
        // sert alors de base, ce qui reste previsible.
        m_origin = measured ? lo : 0.0;
        m_scale = 1.0;
        if (measured && object->get_attribute("stretch")->get_bool()) {
            const double extent = hi - lo;
            if (extent > 1e-9) m_scale = m_path.get_length() / extent;
        }

        compute_roll();

        m_ready = true;
        return true;
    }

    void
    release()
    {
        m_ready = false;
    }

    // Ne touche que [begin, end), et ne lit que des membres figes par prepare().
    void
    deform_range(ModuleDeformerGeometry& geometry,
                 const unsigned int& begin, const unsigned int& end) const
    {
        if (!m_ready) return;

        const bool normals = geometry.has_normals() && geometry.get_normals() != 0;
        const bool velocities =
            geometry.has_velocities() && geometry.get_velocities() != 0;

        for (unsigned int i = begin; i < end; i++) {
            const GMathVec3f& read = geometry.get_position(i);
            const GMathVec3d source(read[0], read[1], read[2]);

            GMathVec3d p = source;
            if (m_global) GMathMatrix4x4d::multiply(p, source, m_to_world);

            GMathVec3d origin, tangent, normal;
            frame_at((p[m_axis] - m_origin) * m_scale, origin, tangent, normal);

            // Le repere redresse : u remplace l'axe v de la geometrie, v son
            // axe w, la tangente son axe principal.
            const GMathVec3d binormal = vnorm(vcross(tangent, normal));
            const GMathVec3d u = vadd(vscale(normal, m_roll_cos),
                                      vscale(binormal, m_roll_sin));
            const GMathVec3d v = vsub(vscale(binormal, m_roll_cos),
                                      vscale(normal, m_roll_sin));

            GMathVec3d q = vadd(origin, vadd(vscale(u, p[m_axis_v]),
                                             vscale(v, p[m_axis_w])));
            if (m_global) {
                GMathVec3d back;
                GMathMatrix4x4d::multiply(back, q, m_from_world);
                q = back;
            }
            if (m_weight < 1.0) q = vlerp(source, q, m_weight);
            geometry.set_position(i, GMathVec3f(float(q[0]), float(q[1]),
                                                float(q[2])));

            // Une normale et une velocite sont des directions : seule la
            // rotation du repere les concerne. L'etirement de l'axe les
            // deformerait aussi, au second ordre -- neglige, et de toute facon
            // sans objet des que Clarisse recalcule les normales du maillage
            // deforme, ce qu'il fait quand la geometrie n'en porte pas.
            if (normals) {
                const GMathVec3f& n = geometry.get_normal(i);
                geometry.set_normal(i, rotate(n, tangent, u, v));
            }
            if (velocities) {
                const GMathVec3f& velocity = geometry.get_velocity(i);
                geometry.set_velocity(i, rotate(velocity, tangent, u, v));
            }
        }
    }

private:
    // Pas de module_constructor ni de on_attribute_change ici, contrairement au
    // tube : le deformeur ne possede aucune ressource, c'est la geometrie qui
    // possede la sienne. Le seul `output "geometry"` pose sur chaque attribut
    // du CID suffit a faire remonter la salissure jusqu'a elle -- y compris
    // quand c'est un locator reference qui bouge. Verifie en retirant tout ce
    // qui suivrait : les mesures ne bougent pas d'un chiffre.

    // L'angle entre le repere de la courbe a son depart et l'axe de reference
    // de la geometrie, garde sous forme de cosinus et sinus.
    void
    compute_roll()
    {
        m_roll_cos = 1.0;
        m_roll_sin = 0.0;

        GMathVec3d origin, tangent, normal;
        m_path.eval_at_distance(0.0, origin, tangent, normal);
        const GMathVec3d binormal = vnorm(vcross(tangent, normal));

        GMathVec3d reference(0.0, 0.0, 0.0);
        reference[m_axis_v] = 1.0;
        reference = vsub(reference, vscale(tangent, vdot(tangent, reference)));

        const double length = reference.get_length();
        // Une courbe qui demarre perpendiculairement a l'axe choisi ne fournit
        // aucune orientation de reference : on garde le repere brut plutot que
        // de diviser par un residu numerique.
        if (length <= 1e-6) return;

        reference = vscale(reference, 1.0 / length);
        m_roll_cos = vdot(reference, normal);
        m_roll_sin = vdot(reference, binormal);
    }

    // Le repere a une distance donnee, avec ce qui deborde de la courbe.
    void
    frame_at(double distance, GMathVec3d& origin, GMathVec3d& tangent,
             GMathVec3d& normal) const
    {
        const double length = m_path.get_length();
        double overflow = 0.0;

        if (m_path.is_closed()) {
            // Sur une courbe fermee, ce qui depasse revient par l'autre bout.
            distance = fmod(distance, length);
            if (distance < 0.0) distance += length;
        } else if (distance < 0.0) {
            overflow = distance;
            distance = 0.0;
        } else if (distance > length) {
            overflow = distance - length;
            distance = length;
        }

        m_path.eval_at_distance(distance, origin, tangent, normal);
        // Prolonger tout droit plutot que d'entasser les sommets au bout : une
        // geometrie plus longue que sa courbe garde ainsi une forme lisible.
        if (overflow != 0.0) origin = vadd(origin, vscale(tangent, overflow));
    }

    GMathVec3f
    rotate(const GMathVec3f& direction, const GMathVec3d& tangent,
           const GMathVec3d& u, const GMathVec3d& v) const
    {
        GMathVec3d d(direction[0], direction[1], direction[2]);
        if (m_global) {
            GMathVec3d world;
            GMathMatrix4x4d::multiply_direction(world, d, m_to_world);
            d = world;
        }

        GMathVec3d r = vadd(vscale(tangent, d[m_axis]),
                            vadd(vscale(u, d[m_axis_v]), vscale(v, d[m_axis_w])));
        if (m_global) {
            GMathVec3d local;
            GMathMatrix4x4d::multiply_direction(local, r, m_from_world);
            r = local;
        }
        if (m_weight < 1.0) {
            r = vlerp(GMathVec3d(direction[0], direction[1], direction[2]), r,
                      m_weight);
        }
        return GMathVec3f(float(r[0]), float(r[1]), float(r[2]));
    }

    curve_core::Path m_path;
    bool m_ready;
    unsigned int m_axis;
    unsigned int m_axis_v;
    unsigned int m_axis_w;
    double m_origin;
    double m_scale;
    double m_weight;
    double m_roll_cos;
    double m_roll_sin;
    bool m_global;
    GMathMatrix4x4d m_to_world;
    GMathMatrix4x4d m_from_world;
};

// `deform`, `pre_deform` et `post_deform` de ModuleDeformer ne sont pas
// virtuelles : le point d'extension est cette structure de trois pointeurs de
// fonction. Les signatures doivent coincider exactement, un pointeur de
// fonction ne tolerant aucune covariance.
IX_BEGIN_DECLARE_MODULE_CALLBACKS(DeformerCurve, ModuleDeformerCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
    static bool pre_deform(OfObject& object, const CtxEval& eval_ctx,
                           const ModuleGeometry& geometry,
                           ModuleDeformerTopology& topology);
    static bool deform(OfObject& object, const CtxEval& eval_ctx,
                       const ModuleGeometry& geometry,
                       ModuleDeformerGeometry& deformed,
                       const unsigned int& begin, const unsigned int& end);
    static void post_deform(OfObject& object, const CtxEval& eval_ctx,
                            const ModuleGeometry& geometry);
IX_END_DECLARE_MODULE_CALLBACKS(DeformerCurve)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(DeformerCurve);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
    module_callbacks->cb_pre_deform = IX_MODULE_CLBK::pre_deform;
    module_callbacks->cb_deform = IX_MODULE_CLBK::deform;
    module_callbacks->cb_post_deform = IX_MODULE_CLBK::post_deform;
}

IX_END_EXTERN_C

// Le module derriere l'objet. `get_module<T>()` passerait par CoreBaseObject
// et ne testerait que l'info de classe de ModuleDeformer, qui est la seule
// declaree dans la hierarchie : un static_cast dit la meme chose sans le
// detour. Les callbacks n'etant poses que sur notre classe, l'objet recu est
// necessairement le notre.
static CurveDeformModule *
get_curve_module(OfObject& object)
{
    return static_cast<CurveDeformModule *>(object.get_module());
}

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    // set_object avant tout : Clarisse interroge le module des que l'objet
    // entre dans le contexte, et dereference m_object sans le tester.
    CurveDeformModule *module = new CurveDeformModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}

bool
IX_MODULE_CLBK::pre_deform(OfObject& object, const CtxEval& eval_ctx,
                           const ModuleGeometry& geometry,
                           ModuleDeformerTopology& topology)
{
    CurveDeformModule *module = get_curve_module(object);
    if (module == 0) return false;
    // Rendre false quand il n'y a pas de courbe : la geometrie sort intacte,
    // ce qui vaut mieux qu'un tas de sommets a l'origine.
    return module->prepare(geometry);
}

bool
IX_MODULE_CLBK::deform(OfObject& object, const CtxEval& eval_ctx,
                       const ModuleGeometry& geometry,
                       ModuleDeformerGeometry& deformed,
                       const unsigned int& begin, const unsigned int& end)
{
    CurveDeformModule *module = get_curve_module(object);
    if (module == 0) return false;
    module->deform_range(deformed, begin, end);
    return true;
}

void
IX_MODULE_CLBK::post_deform(OfObject& object, const CtxEval& eval_ctx,
                            const ModuleGeometry& geometry)
{
    CurveDeformModule *module = get_curve_module(object);
    if (module != 0) module->release();
}
