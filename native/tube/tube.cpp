// Un tube balaye le long d'une courbe passant par des points de controle.
//
// Clarisse sait deja tout faire avec les courbes -- les tesseler, les rendre,
// les shader -- sous le nom de "fur". Ce qui lui manque, c'est de pouvoir en
// creer une. Et le chemin natif ne convient pas ici : GeometryFur n'expose
// aucun attribut de displacement, et un CurveMesh renvoie une bbox nulle et une
// aire de primitive nulle, donc le scatterer n'a rien a quoi s'accrocher.
//
// D'ou ce module : il produit un vrai maillage polygonal. A partir de la, le
// displacement, la subdivision, le scatter, les UV et l'export marchent sans
// qu'on ait une ligne a ecrire pour eux.
//
// Deux details qui font la difference entre un tube utilisable et un tube rate,
// et qu'on paie cher si on ne les connait pas d'avance :
//
//   - le repere. Frenet-Serret se retourne aux points d'inflexion et n'est pas
//     defini quand la courbure s'annule -- donc sur toute portion droite, ce
//     qui est frequent pour un cable. Le tube y vrille brutalement. On utilise
//     un repere a torsion minimale, transporte d'anneau en anneau par la
//     methode du double reflet (Wang et al. 2008) : deux reflexions, aucune
//     trigonometrie, stable partout.
//
//   - les UV. La couture du tube demande qu'un meme sommet porte u=0 d'un cote
//     et u=1 de l'autre. C'est impossible en vertex-varying ; les UV sont donc
//     face-varying, avec une colonne de plus que de cotes.

#define _USE_MATH_DEFINES
#include <cmath>

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_geometry.h>
#include <module_polymesh.h>
#include <module_scene_item.h>

#include <poly_mesh.h>
#include <geometry_object.h>

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>
#include <gmath_vec3.h>

#include <tube.cma>

namespace {

// Les operateurs binaires de GMathVec3 ne sont pas tous garantis par les
// en-tetes reconstruits. On reste sur operator[], qui l'est.
inline GMathVec3d vsub(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

inline GMathVec3d vadd(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}

inline GMathVec3d vscale(const GMathVec3d& a, const double& s)
{
    return GMathVec3d(a[0] * s, a[1] * s, a[2] * s);
}

inline double vdot(const GMathVec3d& a, const GMathVec3d& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline GMathVec3d vcross(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[1] * b[2] - a[2] * b[1],
                      a[2] * b[0] - a[0] * b[2],
                      a[0] * b[1] - a[1] * b[0]);
}

inline GMathVec3d vnorm(const GMathVec3d& a)
{
    const double l = a.get_length();
    return (l > 1e-12) ? vscale(a, 1.0 / l) : GMathVec3d(0.0, 0.0, 1.0);
}

// Catmull-Rom : la courbe passe par ses points de controle, ce qui est la
// seule chose que l'utilisateur attend quand il pose un locator quelque part.
inline GMathVec3d catmull_rom(const GMathVec3d& p0, const GMathVec3d& p1,
                              const GMathVec3d& p2, const GMathVec3d& p3,
                              const double& u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    GMathVec3d r;
    for (unsigned int k = 0; k < 3; k++) {
        r[k] = 0.5 * (2.0 * p1[k]
                      + (-p0[k] + p2[k]) * u
                      + (2.0 * p0[k] - 5.0 * p1[k] + 4.0 * p2[k] - p3[k]) * u2
                      + (-p0[k] + 3.0 * p1[k] - 3.0 * p2[k] + p3[k]) * u3);
    }
    return r;
}

inline GMathVec3d catmull_rom_tangent(const GMathVec3d& p0, const GMathVec3d& p1,
                                      const GMathVec3d& p2, const GMathVec3d& p3,
                                      const double& u)
{
    const double u2 = u * u;
    GMathVec3d r;
    for (unsigned int k = 0; k < 3; k++) {
        r[k] = 0.5 * ((-p0[k] + p2[k])
                      + 2.0 * (2.0 * p0[k] - 5.0 * p1[k] + 4.0 * p2[k] - p3[k]) * u
                      + 3.0 * (-p0[k] + 3.0 * p1[k] - 3.0 * p2[k] + p3[k]) * u2);
    }
    return r;
}

} // namespace

class TubeModule : public ModulePolymesh {
public:
    TubeModule() : ModulePolymesh() {}

protected:
    // Declarer la ressource et ce dont elle depend. Ni DIRTINESS_GEOMETRY dans
    // on_attribute_change, ni output "geometry" dans le CID ne suffisent a
    // faire rappeler create_resource : il faut dire explicitement quels
    // attributs salissent quelle ressource.
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "control_points", "closed", "steps",
            "radius", "radius_profile", "sides", "cap"
        };
        const unsigned int name_count = sizeof(names) / sizeof(names[0]);

        // Deux passes, et surtout pas de resize : CoreArray::resize(n) fait un
        // delete[] suivi d'un new[], il ne preserve rien. Et le constructeur
        // par defaut de OfAttrDirtiness laisse son pointeur non initialise --
        // donc un tableau redimensionne apres coup part en violation d'acces
        // dans set_resource_attrs, loin de sa cause.
        unsigned int count = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            if (object.get_attribute(names[i]) != 0) count++;
        }
        if (count == 0) return;

        CoreArray<OfAttrDirtiness> attrs(count);
        unsigned int k = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            OfAttr *attr = object.get_attribute(names[i]);
            if (attr != 0) {
                // DIRTINESS_ALL, et pas seulement DIRTINESS_GEOMETRY : quand un
                // locator reference bouge, ce qui remonte est DIRTINESS_MOTION.
                // Filtrer sur la geometrie seule laissait le tube en retard d'un
                // changement -- il ne se reconstruisait qu'a la modification
                // suivante, en relisant au passage la nouvelle position.
                attrs[k++] = OfAttrDirtiness(attr, OfAttr::DIRTINESS_ALL);
            }
        }

        set_resource_attrs(ModuleGeometry::RESOURCE_ID_GEOMETRY, attrs);
    }

    ResourceData *
    create_resource(const int& id, void *data) const override
    {
        if (id == ModuleGeometry::RESOURCE_ID_GEOMETRY) {
            PolyMesh *mesh = build_mesh();
            if (mesh != 0) return mesh;
        }
        return ModulePolymesh::create_resource(id, data);
    }

    void
    on_attribute_change(const OfAttr& attr, int& dirtiness,
                        const int& dirtiness_flags) override
    {
        ModulePolymesh::on_attribute_change(attr, dirtiness, dirtiness_flags);

        const CoreString& name = attr.get_name();
        if (name == "control_points" || name == "closed" || name == "steps"
            || name == "radius" || name == "radius_profile" || name == "sides"
            || name == "cap") {
            dirtiness |= OfAttr::DIRTINESS_GEOMETRY;
        }
    }

private:
    // Les positions monde des points de controle, dans l'ordre.
    bool
    gather_control_points(CoreVector<GMathVec3d>& points) const
    {
        OfObject *object = get_object();
        if (object == 0) return false;

        OfAttr *attr = object->get_attribute("control_points");
        if (attr == 0) return false;

        const unsigned int count = attr->get_value_count();
        for (unsigned int i = 0; i < count; i++) {
            OfObject *item = attr->get_object(i);
            if (item == 0) continue;
            ModuleSceneItem *module = item->get_module<ModuleSceneItem>();
            if (module == 0) continue;
            GMathVec3d position;
            module->get_global_position(position);
            points.add(position);
        }
        return points.get_count() >= 2;
    }

    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        CoreVector<GMathVec3d> cv;
        if (!gather_control_points(cv)) return 0;

        const bool closed = object->get_attribute("closed")->get_bool();
        const bool cap = object->get_attribute("cap")->get_bool() && !closed;
        const OfAttr *profile = object->get_attribute("radius_profile");
        const double radius = object->get_attribute("radius")->get_double();

        unsigned int steps = (unsigned int) object->get_attribute("steps")->get_long();
        unsigned int sides = (unsigned int) object->get_attribute("sides")->get_long();
        if (steps < 1u) steps = 1u;
        if (sides < 3u) sides = 3u;

        const unsigned int cp_count = cv.get_count();
        // Une courbe fermee a autant de portions que de points ; une courbe
        // ouverte en a un de moins.
        const unsigned int span_count = closed ? cp_count : cp_count - 1u;
        const unsigned int ring_count = closed ? span_count * steps
                                              : span_count * steps + 1u;
        if (ring_count < 2u) return 0;

        // --- echantillonnage de la courbe -------------------------------
        CoreArray<GMathVec3d> centers(ring_count);
        CoreArray<GMathVec3d> tangents(ring_count);

        for (unsigned int r = 0; r < ring_count; r++) {
            const unsigned int span = (r / steps) % (span_count == 0u ? 1u : span_count);
            const double u = double(r % steps) / double(steps);
            const double uu = (!closed && r == ring_count - 1u) ? 1.0 : u;
            const unsigned int s = (!closed && r == ring_count - 1u)
                                 ? span_count - 1u : span;

            const GMathVec3d& p0 = cv[control_index(s, -1, cp_count, closed)];
            const GMathVec3d& p1 = cv[control_index(s,  0, cp_count, closed)];
            const GMathVec3d& p2 = cv[control_index(s,  1, cp_count, closed)];
            const GMathVec3d& p3 = cv[control_index(s,  2, cp_count, closed)];

            centers[r] = catmull_rom(p0, p1, p2, p3, uu);
            tangents[r] = vnorm(catmull_rom_tangent(p0, p1, p2, p3, uu));
        }

        // --- repere a torsion minimale ----------------------------------
        // Amorce : n'importe quelle direction non colineaire a la tangente.
        CoreArray<GMathVec3d> normals(ring_count);
        {
            const GMathVec3d& t0 = tangents[0];
            GMathVec3d seed(0.0, 0.0, 1.0);
            if (fabs(vdot(t0, seed)) > 0.9) seed = GMathVec3d(1.0, 0.0, 0.0);
            normals[0] = vnorm(vcross(vcross(t0, seed), t0));
        }
        for (unsigned int r = 0; r + 1u < ring_count; r++) {
            normals[r + 1u] = transport(centers[r], centers[r + 1u],
                                        tangents[r], tangents[r + 1u], normals[r]);
        }

        // --- sommets -----------------------------------------------------
        const unsigned int vertex_count = ring_count * sides;
        CoreArray<GMathVec3f> vertices(vertex_count);

        for (unsigned int r = 0; r < ring_count; r++) {
            const double t = (ring_count > 1u) ? double(r) / double(ring_count - 1u) : 0.0;
            double scale = 1.0;
            if (profile != 0) scale = profile->get_curve_double(t);
            const double rr = radius * scale;

            const GMathVec3d& n = normals[r];
            const GMathVec3d b = vnorm(vcross(tangents[r], n));

            for (unsigned int s = 0; s < sides; s++) {
                const double a = 2.0 * M_PI * double(s) / double(sides);
                const GMathVec3d p = vadd(centers[r],
                                          vadd(vscale(n, cos(a) * rr),
                                               vscale(b, sin(a) * rr)));
                vertices[r * sides + s] = GMathVec3f(float(p[0]), float(p[1]), float(p[2]));
            }
        }

        // --- topologie ---------------------------------------------------
        const unsigned int quad_rings = closed ? ring_count : ring_count - 1u;
        const unsigned int quad_count = quad_rings * sides;
        const unsigned int cap_count = cap ? 2u : 0u;
        const unsigned int polygon_count = quad_count + cap_count;
        const unsigned int index_count = quad_count * 4u + cap_count * sides;

        CoreArray<unsigned int> polygon_indices(index_count);
        CoreArray<unsigned int> polygon_vertex_count(polygon_count);
        CoreArray<unsigned int> polygon_shading_groups(polygon_count);
        CoreArray<CoreString> shading_group_names(1);
        shading_group_names[0] = "tube";

        // Les UV ont une colonne de plus que de cotes : c'est la couture.
        const unsigned int uv_rows = quad_rings + 1u;
        const unsigned int uv_cols = sides + 1u;
        const unsigned int uv_grid = uv_rows * uv_cols;
        CoreArray<GMathVec3f> uvs(uv_grid + (cap ? 2u * sides : 0u));
        CoreArray<unsigned int> uv_indices(index_count);

        for (unsigned int r = 0; r < uv_rows; r++) {
            const float v = float(r) / float(uv_rows - 1u);
            for (unsigned int s = 0; s < uv_cols; s++) {
                uvs[r * uv_cols + s] = GMathVec3f(float(s) / float(sides), v, 0.0f);
            }
        }

        unsigned int k = 0;
        for (unsigned int r = 0; r < quad_rings; r++) {
            const unsigned int r1 = (r + 1u) % ring_count;
            for (unsigned int s = 0; s < sides; s++) {
                const unsigned int s1 = (s + 1u) % sides;
                const unsigned int q = r * sides + s;

                // Ordre choisi pour que la normale sorte du tube : on tourne
                // d'abord autour de l'axe, on avance ensuite.
                polygon_indices[k + 0u] = r * sides + s;
                polygon_indices[k + 1u] = r * sides + s1;
                polygon_indices[k + 2u] = r1 * sides + s1;
                polygon_indices[k + 3u] = r1 * sides + s;

                uv_indices[k + 0u] = r * uv_cols + s;
                uv_indices[k + 1u] = r * uv_cols + s + 1u;
                uv_indices[k + 2u] = (r + 1u) * uv_cols + s + 1u;
                uv_indices[k + 3u] = (r + 1u) * uv_cols + s;

                polygon_vertex_count[q] = 4u;
                polygon_shading_groups[q] = 0u;
                k += 4u;
            }
        }

        if (cap) {
            // Le disque du depart tourne a l'envers de celui de la fin : les
            // deux normales doivent pointer vers l'exterieur du tube.
            for (unsigned int s = 0; s < sides; s++) {
                const double a = 2.0 * M_PI * double(s) / double(sides);
                uvs[uv_grid + s] = GMathVec3f(float(0.5 + 0.5 * cos(a)),
                                              float(0.5 + 0.5 * sin(a)), 0.0f);
                uvs[uv_grid + sides + s] = uvs[uv_grid + s];
            }
            for (unsigned int s = 0; s < sides; s++) {
                polygon_indices[k + s] = (sides - 1u - s);
                uv_indices[k + s] = uv_grid + (sides - 1u - s);
            }
            polygon_vertex_count[quad_count] = sides;
            polygon_shading_groups[quad_count] = 0u;
            k += sides;

            const unsigned int last = (ring_count - 1u) * sides;
            for (unsigned int s = 0; s < sides; s++) {
                polygon_indices[k + s] = last + s;
                uv_indices[k + s] = uv_grid + sides + s;
            }
            polygon_vertex_count[quad_count + 1u] = sides;
            polygon_shading_groups[quad_count + 1u] = 0u;
            k += sides;
        }

        // --- assemblage ---------------------------------------------------
        CoreArray<GeometryUvMap> uv_maps(1);
        uv_maps[0].name = "uv";
        uv_maps[0].vertices = uvs;
        uv_maps[0].polygon_indices = uv_indices;

        CoreArray<GMathVec3f> velocities;          // pas de motion blur geometrique
        CoreArray<GeometryNormalMap> normal_maps;  // vide : PolyMesh les calcule
        CoreArray<GeometryColorMap> color_maps;

        PolyMesh *mesh = new PolyMesh;
        mesh->set(vertices, velocities,
                  polygon_indices, polygon_vertex_count, polygon_shading_groups,
                  shading_group_names,
                  uv_maps, normal_maps, color_maps,
                  true, 0);
        return mesh;
    }

    // Indice d'un point de controle voisin, en repliant aux extremites pour une
    // courbe ouverte et en bouclant pour une courbe fermee.
    static unsigned int
    control_index(const unsigned int& span, const int& offset,
                  const unsigned int& count, const bool& closed)
    {
        int i = int(span) + offset;
        if (closed) {
            const int n = int(count);
            return (unsigned int) (((i % n) + n) % n);
        }
        if (i < 0) i = 0;
        if (i > int(count) - 1) i = int(count) - 1;
        return (unsigned int) i;
    }

    // Transport du repere par double reflet (Wang et al. 2008). La premiere
    // reflexion amene le repere du plan de l'anneau courant vers celui du
    // suivant, la seconde corrige l'ecart de tangente restant. Aucune
    // trigonometrie, et rien ne degenere sur une portion droite.
    static GMathVec3d
    transport(const GMathVec3d& x0, const GMathVec3d& x1,
              const GMathVec3d& t0, const GMathVec3d& t1, const GMathVec3d& r0)
    {
        const GMathVec3d v1 = vsub(x1, x0);
        const double c1 = vdot(v1, v1);
        if (c1 < 1e-24) return r0;

        const GMathVec3d rL = vsub(r0, vscale(v1, 2.0 * vdot(v1, r0) / c1));
        const GMathVec3d tL = vsub(t0, vscale(v1, 2.0 * vdot(v1, t0) / c1));

        const GMathVec3d v2 = vsub(t1, tL);
        const double c2 = vdot(v2, v2);
        if (c2 < 1e-24) return vnorm(rL);

        return vnorm(vsub(rL, vscale(v2, 2.0 * vdot(v2, rL) / c2)));
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryTube, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryTube)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryTube);
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
    // set_object avant tout : Clarisse interroge le module des que l'objet
    // entre dans le contexte, et dereference m_object sans le tester.
    TubeModule *module = new TubeModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
