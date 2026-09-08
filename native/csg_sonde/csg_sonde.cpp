// Sonde : Clarisse trace-t-il une geometrie qui n'existe pas ?
//
// Tout le document sur le CSG tient a cette question. Un booleen sur maillages
// est un probleme d'arithmetique exacte, cher et long a rendre robuste. Un CSG
// implicite ne calcule jamais de maillage : il repond seulement, pour un rayon
// donne, ou il touche la surface. Cette seconde voie n'est possible que si le
// moteur accepte une geometrie definie par sa fonction d'intersection.
//
// Ce qui la rend plausible : GeometryObject est une interface purement
// virtuelle. Elle ne parle ni de points ni de polygones. Elle demande combien
// de primitives, la boite de chacune, et ce qu'un rayon y touche. PolyMesh en
// est une implementation parmi d'autres -- et VolumeSparse, et GeometrySphere.
// Rien dans la signature n'exige un maillage.
//
// La sonde fait donc le minimum qui prouve quelque chose : la difference de
// deux spheres, resolue en intervalles le long du rayon. Aucun sommet n'est
// alloue nulle part.
//
// REPONSE : oui. Les trois booleens se rendent, avec la paroi concave de la
// morsure correctement eclairee et l'arete de coupe nette. Clarisse construit
// son BVH sur nos primitives, appelle intersect_primitive, et shade le
// fragment avec le materiau du shading group. Voir docs/csg-clarisse.md.
//
// Deux details qui ne sont pas de la decoration :
//
//   - Le CSG se fait sur des INTERVALLES, pas sur des premiers points touches.
//     Prendre la premiere intersection de chaque operande et comparer les
//     distances donne une image qui semble juste et qui est fausse : le trou
//     de la difference disparait des que le rayon entre par l'arriere de B.
//     Il faut les deux racines de chaque sphere.
//
//   - La face interieure du creux est la surface de B, avec sa normale
//     RETOURNEE. Sans ce retournement elle s'eclaire comme un dos, et le creux
//     ressort noir. C'est le meme piege que le back-face dans un moteur temps
//     reel, sauf qu'ici il n'y a personne pour le corriger apres coup.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_geometry.h>
#include <module_scene_item.h>

#include <geometry_object.h>
#include <geometry_sphere.h>
#include <geometry_raytrace_ctx.h>
#include <geometry_fragment.h>
#include <geometry_sample.h>

#include <core_array.h>
#include <core_string.h>
#include <gmath_vec3.h>
#include <gmath_ray.h>

#include <csg_sonde.cma>

// push_intersection prend un GeometryMediumDescriptor par defaut, et le corps
// de son constructeur vit dans geometry_fragment.icc -- un des fichiers
// d'implementation perdus du SDK. La structure ne porte pas GEOMETRY_EXPORT,
// donc le symbole n'est pas non plus dans la DLL : il ressort en externe non
// resolu des qu'on appelle push_intersection.
//
// On le reconstruit ici, et la valeur d'opacite n'est PAS zero. C'est le seul
// chiffre qu'il a fallu deviner dans toute la chaine, et il a coute six rendus :
// avec opacity = 0, le moteur lit "milieu totalement transparent", jette
// l'intersection dans son filtre, et rien ne s'affiche -- sans un message. Avec
// opacity = 1, les trois booleens apparaissent du premier coup.
//
// Le symptome est trompeur parce qu'il ressemble a une geometrie absente : ni
// silhouette, ni alpha, ni avertissement. La seule facon de le distinguer d'un
// module mal monte a ete de renvoyer une GeometrySphere native depuis le meme
// create_resource -- elle, elle s'affichait.
inline GeometryMediumDescriptor::GeometryMediumDescriptor() { clear(); }

inline void
GeometryMediumDescriptor::clear()
{
    opacity = GMathVec3f(1.0f);
    thickness = 0.0f;
    density = 0.0f;
    density_diff = GMathVec3f(0.0f);
}

namespace {

const double PI = 3.14159265358979323846;

enum { MODE_UNION = 0, MODE_INTERSECTION = 1, MODE_DIFFERENCE = 2 };

struct Sphere {
    GMathVec3d center;
    double radius;
};

// Les deux racines de l'equation du second degre, ou rien. On rend les racines
// non bornees : c'est l'operateur booleen, plus haut, qui decide de ce qui est
// dans le champ du rayon. Couper trop tot ici ferait disparaitre l'intervalle
// dont on a besoin pour soustraire.
bool
ray_sphere(const GMathVec3d& origin, const GMathVec3d& direction,
           const Sphere& s, double& t0, double& t1)
{
    const GMathVec3d oc = origin - s.center;
    const double a = direction.dot(direction);
    const double b = 2.0 * oc.dot(direction);
    const double c = oc.dot(oc) - s.radius * s.radius;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0 || a == 0.0) return false;
    const double root = sqrt(disc);
    t0 = (-b - root) / (2.0 * a);
    t1 = (-b + root) / (2.0 * a);
    return true;
}

// Les coordonnees spheriques du point touche, pour que compute_fragment_sample
// puisse reconstruire position et normale sans conserver le rayon. Le moteur
// ne redonne que (u, v, w) : c'est tout ce dont on dispose pour dire ou on est.
void
encode(const Sphere& s, const GMathVec3d& hit, double& u, double& v)
{
    const GMathVec3d n = (hit - s.center) / s.radius;
    const double y = n[1] < -1.0 ? -1.0 : (n[1] > 1.0 ? 1.0 : n[1]);
    const double phi = acos(y);
    double theta = atan2(n[2], n[0]);
    if (theta < 0.0) theta += 2.0 * PI;
    u = theta / (2.0 * PI);
    v = phi / PI;
}

class CsgObject : public GeometryObject {
public:
    CsgObject(const long& mode, const double& ra, const double& rb, const double& offset)
        : m_mode(mode)
    {
        m_a.center = GMathVec3d(0.0, 0.0, 0.0);
        m_a.radius = ra;
        m_b.center = GMathVec3d(offset, 0.0, 0.0);
        m_b.radius = rb;
        m_shading_groups.resize(1);
        m_shading_groups[0] = CoreString("csg");
    }

    GeometryObject *get_copy() const override
    {
        return new CsgObject(m_mode, m_a.radius, m_b.radius, m_b.center[0]);
    }

    GMathBbox3d get_bbox() const override
    {
        GMathVec3d lo = m_a.center - GMathVec3d(m_a.radius);
        GMathVec3d hi = m_a.center + GMathVec3d(m_a.radius);
        // L'intersection et la difference tiennent dans la boite de A seule,
        // mais une boite trop large ne coute qu'un peu de traverse : la sonde
        // ne cherche pas la performance, elle cherche une reponse.
        if (m_mode == MODE_UNION) {
            const GMathVec3d blo = m_b.center - GMathVec3d(m_b.radius);
            const GMathVec3d bhi = m_b.center + GMathVec3d(m_b.radius);
            for (unsigned int k = 0; k < 3; k++) {
                if (blo[k] < lo[k]) lo[k] = blo[k];
                if (bhi[k] > hi[k]) hi[k] = bhi[k];
            }
        }
        return GMathBbox3d(lo, hi);
    }

    const CoreBasicArray<CoreString>& get_shading_group_names() const override
    {
        return m_shading_groups;
    }

    unsigned int get_primitive_count() const override { return 1; }

    unsigned int get_primitive_edge_count(const unsigned int& id) const override { return 4; }

    void compute_primitive_bbox(const CtxEval& eval_ctx, const unsigned int& id,
                                GMathBbox3d& bbox) const override
    {
        bbox = get_bbox();
    }

    unsigned int get_primitive_shading_group_index(const unsigned int& id) const override
    {
        return 0;
    }

    void intersect_primitive(const CtxEval& eval_ctx, const unsigned int& id,
                             GeometryRaytraceCtx& raytrace_ctx) const override
    {
        const int first = raytrace_ctx.get_first_index();
        const int last = raytrace_ctx.get_last_index();

        for (int i = first; i <= last; i++) {
            const unsigned int index = static_cast<unsigned int>(i);
            const GMathBasicRay<>& ray = raytrace_ctx.get_local_ray(index);
            const GMathVec3d& origin = ray.get_position();
            const GMathVec3d& direction = ray.get_direction();

            double ta0, ta1, tb0, tb1;
            const bool hit_a = ray_sphere(origin, direction, m_a, ta0, ta1);
            const bool hit_b = ray_sphere(origin, direction, m_b, tb0, tb1);

            // Jusqu'a deux intervalles en sortie : la difference peut couper
            // A en deux morceaux quand B le traverse de part en part.
            double starts[2], ends[2];
            int which[2];      // 0 = surface de A, 1 = surface de B retournee
            int flip[2];
            int count = 0;

            if (m_mode == MODE_UNION) {
                if (hit_a && hit_b && ta1 >= tb0 && tb1 >= ta0) {
                    starts[0] = ta0 < tb0 ? ta0 : tb0;
                    ends[0]   = ta1 > tb1 ? ta1 : tb1;
                    which[0]  = (ta0 < tb0) ? 0 : 1;
                    flip[0]   = 0;
                    count = 1;
                } else {
                    if (hit_a) { starts[count] = ta0; ends[count] = ta1; which[count] = 0; flip[count] = 0; count++; }
                    if (hit_b) { starts[count] = tb0; ends[count] = tb1; which[count] = 1; flip[count] = 0; count++; }
                }
            } else if (m_mode == MODE_INTERSECTION) {
                if (hit_a && hit_b) {
                    const double lo = ta0 > tb0 ? ta0 : tb0;
                    const double hi = ta1 < tb1 ? ta1 : tb1;
                    if (lo < hi) {
                        starts[0] = lo; ends[0] = hi;
                        which[0]  = (ta0 > tb0) ? 0 : 1;
                        flip[0]   = 0;
                        count = 1;
                    }
                }
            } else {
                if (hit_a) {
                    if (!hit_b || tb1 <= ta0 || tb0 >= ta1) {
                        starts[0] = ta0; ends[0] = ta1; which[0] = 0; flip[0] = 0;
                        count = 1;
                    } else {
                        // B mord dans A : ce qui reste est [ta0, tb0] puis
                        // [tb1, ta1], et les faces nees de la coupe sont la
                        // surface de B vue de l'interieur.
                        if (tb0 > ta0) {
                            starts[count] = ta0; ends[count] = tb0;
                            which[count] = 0; flip[count] = 0; count++;
                        }
                        if (tb1 < ta1) {
                            starts[count] = tb1; ends[count] = ta1;
                            which[count] = 1; flip[count] = 1; count++;
                        }
                    }
                }
            }

            // On ne pousse que la premiere entree devant le rayon. Un moteur a
            // rayons d'ombre ou de refraction reappellera avec un tnear plus
            // grand, et retombera sur l'intervalle suivant.
            double best_t = 0.0;
            int best = -1;
            for (int k = 0; k < count; k++) {
                const double t = starts[k];
                if (raytrace_ctx.is_distance_clipped(index, t)) continue;
                if (best < 0 || t < best_t) { best_t = t; best = k; }
            }
            if (best < 0) continue;

            const Sphere& surface = which[best] == 0 ? m_a : m_b;
            const GMathVec3d hit = origin + direction * best_t;
            double u = 0.0, v = 0.0;
            encode(surface, hit, u, v);

            // Le code de surface passe par sub_primitive_id, pas par w. (u, v, w)
            // sont des coordonnees parametriques : y glisser un entier hors de
            // [0, 1] fait refuser l'intersection sans un mot.
            const unsigned int code =
                static_cast<unsigned int>(which[best]) + (flip[best] ? 2u : 0u);
            raytrace_ctx.push_intersection(eval_ctx, index, id, u, v, 0.0,
                                           code, 0.0, 0.0, 0.0, best_t);
        }
    }

    void compute_fragment_sample(const CtxEval& eval_ctx, const GeometryFragment& fragment,
                                 GeometrySample& sample) const override
    {
        const GMathVec3d uvw = fragment.get_uvw();
        const unsigned int code = fragment.get_sub_primitive_id();
        const Sphere& s = (code & 1u) ? m_b : m_a;
        const bool flipped = (code & 2u) != 0;

        const double theta = uvw[0] * 2.0 * PI;
        const double phi = uvw[1] * PI;
        const double sp = sin(phi), cp = cos(phi);
        const double st = sin(theta), ct = cos(theta);

        const GMathVec3d n(sp * ct, cp, sp * st);
        const GMathVec3d position = s.center + n * s.radius;

        // dP/dtheta et dP/dphi. init_surface en tire la normale par produit
        // vectoriel : echanger les deux la retourne, ce qui est exactement ce
        // qu'il faut pour la paroi du creux.
        const GMathVec3d dpdu(-sp * st * s.radius, 0.0, sp * ct * s.radius);
        const GMathVec3d dpdv(cp * ct * s.radius, -sp * s.radius, cp * st * s.radius);

        if (flipped) sample.init_surface(position, dpdv, dpdu);
        else         sample.init_surface(position, dpdu, dpdv);
    }

private:
    Sphere m_a, m_b;
    long m_mode;
    CoreArray<CoreString> m_shading_groups;
};

} // namespace

class CsgSondeModule : public ModuleGeometry {
public:
    CsgSondeModule() : ModuleGeometry() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModuleGeometry::module_constructor(object);

        static const char *const names[] = { "mode", "radius_a", "radius_b", "offset" };
        const unsigned int name_count = sizeof(names) / sizeof(names[0]);

        unsigned int present = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            if (object.get_attribute(names[i]) != 0) present++;
        }
        if (present == 0) return;

        CoreArray<OfAttrDirtiness> attrs(present);
        unsigned int k = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            OfAttr *attr = object.get_attribute(names[i]);
            if (attr != 0) attrs[k++] = OfAttrDirtiness(attr, OfAttr::DIRTINESS_ALL);
        }
        set_resource_attrs(ModuleGeometry::RESOURCE_ID_GEOMETRY, attrs);
    }

    ResourceData *
    create_resource(const int& id, void *data) const override
    {
        if (id == ModuleGeometry::RESOURCE_ID_GEOMETRY) {
            const OfObject *object = get_object();
            // Bissection : avec radius_a negatif, on rend une GeometrySphere
            // livree par Clarisse au lieu de la notre. Si elle s'affiche, le
            // montage du module est bon et le defaut est dans notre
            // GeometryObject ; si elle ne s'affiche pas non plus, il est en
            // amont, dans la classe ou son enregistrement.
            if (object != 0) {
                const OfAttr *ra_probe = object->get_attribute("radius_a");
                if (ra_probe != 0 && ra_probe->get_double() < 0.0) {
                    return new GeometrySphere(1.0f);
                }
            }
            if (object != 0) {
                const OfAttr *mode = object->get_attribute("mode");
                const OfAttr *ra = object->get_attribute("radius_a");
                const OfAttr *rb = object->get_attribute("radius_b");
                const OfAttr *off = object->get_attribute("offset");
                return new CsgObject(mode != 0 ? mode->get_long() : 2,
                                     ra != 0 ? ra->get_double() : 1.0,
                                     rb != 0 ? rb->get_double() : 0.75,
                                     off != 0 ? off->get_double() : 0.6);
            }
        }
        return ModuleGeometry::create_resource(id, data);
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryCsgSonde, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryCsgSonde)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryCsgSonde);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    CsgSondeModule *module = new CsgSondeModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
