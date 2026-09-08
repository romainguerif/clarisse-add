// Un tube, ou une corde, balaye le long d'une courbe passant par des points de
// controle.
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
// Le mode corde est le meme balayage, applique N fois : chaque toron suit une
// helice autour de l'axe. Sa section reste prise dans le repere de la courbe
// plutot que dans celui, incline, du toron -- l'ellipse que ca produit est
// invisible aux torsions usuelles, et ca evite de recalculer un repere par
// toron et par anneau.
//
// Les UV sont face-varying : la couture du tube demande qu'un meme sommet porte
// u=0 d'un cote et u=1 de l'autre, ce qui est impossible en vertex-varying. La
// grille UV a donc une colonne de plus que de cotes.
//
// Le reste du calcul de courbe -- Catmull-Rom, repere a torsion minimale,
// longueurs d'arc -- vit dans common/curve_core.h, partage avec les autres
// nodes de courbe.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_geometry.h>
#include <module_polymesh.h>

#include <poly_mesh.h>
#include <geometry_object.h>

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>
#include <gmath_vec3.h>

#include <curve_core.h>

#include <tube.cma>

using namespace curve_core;

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
            "control_points", "closed", "steps", "radius", "radius_profile",
            "sides", "cap", "strands", "twist"
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

private:
    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        CoreVector<GMathVec3d> cv;
        if (!gather_control_points(*object, "control_points", cv)) return 0;

        const bool closed = object->get_attribute("closed")->get_bool();
        const bool cap = object->get_attribute("cap")->get_bool() && !closed;
        const OfAttr *profile = object->get_attribute("radius_profile");
        const double radius = object->get_attribute("radius")->get_double();
        const double twist = object->get_attribute("twist")->get_double();

        unsigned int steps = (unsigned int) object->get_attribute("steps")->get_long();
        unsigned int sides = (unsigned int) object->get_attribute("sides")->get_long();
        unsigned int strands = (unsigned int) object->get_attribute("strands")->get_long();
        if (sides < 3u) sides = 3u;
        if (strands < 1u) strands = 1u;

        Path path;
        if (!path.build(cv, closed, steps)) return 0;

        const unsigned int rings = path.get_sample_count();
        if (rings < 2u) return 0;

        // Un toron unique occupe tout le rayon ; a partir de deux, ils se
        // rangent sur un cercle et leur rayon propre est celui qui les fait
        // s'effleurer sans se penetrer.
        double strand_radius = radius;
        double strand_offset = 0.0;
        if (strands > 1u) {
            const double s = sin(M_PI / double(strands));
            strand_radius = radius * s / (1.0 + s);
            strand_offset = radius - strand_radius;
        }

        const unsigned int quad_rings = closed ? rings : rings - 1u;
        const unsigned int vertex_count = strands * rings * sides;
        const unsigned int quad_count = strands * quad_rings * sides;
        const unsigned int cap_count = cap ? 2u * strands : 0u;
        const unsigned int polygon_count = quad_count + cap_count;
        const unsigned int index_count = quad_count * 4u + cap_count * sides;

        CoreArray<GMathVec3f> vertices(vertex_count);
        CoreArray<GMathVec3f> velocities;          // pas de motion blur geometrique
        CoreArray<unsigned int> polygon_indices(index_count);
        CoreArray<unsigned int> polygon_vertex_count(polygon_count);
        CoreArray<unsigned int> polygon_shading_groups(polygon_count);
        CoreArray<CoreString> shading_group_names(1);
        shading_group_names[0] = "tube";

        const unsigned int uv_rows = quad_rings + 1u;
        const unsigned int uv_cols = sides + 1u;
        const unsigned int uv_grid = strands * uv_rows * uv_cols;
        CoreArray<GMathVec3f> uvs(uv_grid + (cap ? 2u * strands * sides : 0u));
        CoreArray<unsigned int> uv_indices(index_count);

        const double total = path.get_length();

        // --- sommets -----------------------------------------------------
        for (unsigned int r = 0; r < rings; r++) {
            const double t = (rings > 1u) ? double(r) / double(rings - 1u) : 0.0;
            double scale = 1.0;
            if (profile != 0) scale = profile->get_curve_double(t);

            const GMathVec3d& centre = path.get_position(r);
            const GMathVec3d& n = path.get_normal(r);
            const GMathVec3d b = path.get_binormal(r);

            // La torsion se compte en tours par unite de longueur reelle, pas
            // par parametre : sinon le pas de la corde se resserrerait dans les
            // virages, ou la courbe est echantillonnee plus dense au metre.
            const double helix = 2.0 * M_PI * twist * path.get_arc_length(r);

            for (unsigned int strand = 0; strand < strands; strand++) {
                const double phase = helix
                                   + 2.0 * M_PI * double(strand) / double(strands);
                const GMathVec3d axis =
                    (strands > 1u)
                    ? vadd(centre, vadd(vscale(n, cos(phase) * strand_offset * scale),
                                        vscale(b, sin(phase) * strand_offset * scale)))
                    : centre;

                for (unsigned int s = 0; s < sides; s++) {
                    const double a = 2.0 * M_PI * double(s) / double(sides);
                    const double rr = strand_radius * scale;
                    const GMathVec3d p = vadd(axis,
                                              vadd(vscale(n, cos(a) * rr),
                                                   vscale(b, sin(a) * rr)));
                    vertices[(strand * rings + r) * sides + s] =
                        GMathVec3f(float(p[0]), float(p[1]), float(p[2]));
                }
            }
        }

        // --- UV ------------------------------------------------------------
        for (unsigned int strand = 0; strand < strands; strand++) {
            for (unsigned int r = 0; r < uv_rows; r++) {
                const float v = float(r) / float(uv_rows - 1u);
                for (unsigned int s = 0; s < uv_cols; s++) {
                    uvs[(strand * uv_rows + r) * uv_cols + s] =
                        GMathVec3f(float(s) / float(sides), v, 0.0f);
                }
            }
        }

        // --- topologie ------------------------------------------------------
        unsigned int k = 0;
        unsigned int face = 0;
        for (unsigned int strand = 0; strand < strands; strand++) {
            const unsigned int base = strand * rings * sides;
            const unsigned int uv_base = strand * uv_rows * uv_cols;

            for (unsigned int r = 0; r < quad_rings; r++) {
                const unsigned int r1 = (r + 1u) % rings;
                for (unsigned int s = 0; s < sides; s++) {
                    const unsigned int s1 = (s + 1u) % sides;

                    // Ordre choisi pour que la normale sorte du tube : on tourne
                    // d'abord autour de l'axe, on avance ensuite.
                    polygon_indices[k + 0u] = base + r * sides + s;
                    polygon_indices[k + 1u] = base + r * sides + s1;
                    polygon_indices[k + 2u] = base + r1 * sides + s1;
                    polygon_indices[k + 3u] = base + r1 * sides + s;

                    uv_indices[k + 0u] = uv_base + r * uv_cols + s;
                    uv_indices[k + 1u] = uv_base + r * uv_cols + s + 1u;
                    uv_indices[k + 2u] = uv_base + (r + 1u) * uv_cols + s + 1u;
                    uv_indices[k + 3u] = uv_base + (r + 1u) * uv_cols + s;

                    polygon_vertex_count[face] = 4u;
                    polygon_shading_groups[face] = 0u;
                    face++;
                    k += 4u;
                }
            }
        }

        if (cap) {
            for (unsigned int strand = 0; strand < strands; strand++) {
                const unsigned int base = strand * rings * sides;
                const unsigned int uv_cap = uv_grid + 2u * strand * sides;

                for (unsigned int s = 0; s < sides; s++) {
                    const double a = 2.0 * M_PI * double(s) / double(sides);
                    uvs[uv_cap + s] = GMathVec3f(float(0.5 + 0.5 * cos(a)),
                                                 float(0.5 + 0.5 * sin(a)), 0.0f);
                    uvs[uv_cap + sides + s] = uvs[uv_cap + s];
                }

                // Le disque du depart tourne a l'envers de celui de la fin :
                // les deux normales doivent pointer vers l'exterieur.
                for (unsigned int s = 0; s < sides; s++) {
                    polygon_indices[k + s] = base + (sides - 1u - s);
                    uv_indices[k + s] = uv_cap + (sides - 1u - s);
                }
                polygon_vertex_count[face] = sides;
                polygon_shading_groups[face] = 0u;
                face++;
                k += sides;

                const unsigned int last = base + (rings - 1u) * sides;
                for (unsigned int s = 0; s < sides; s++) {
                    polygon_indices[k + s] = last + s;
                    uv_indices[k + s] = uv_cap + sides + s;
                }
                polygon_vertex_count[face] = sides;
                polygon_shading_groups[face] = 0u;
                face++;
                k += sides;
            }
        }

        // --- assemblage -----------------------------------------------------
        CoreArray<GeometryUvMap> uv_maps(1);
        uv_maps[0].name = "uv";
        uv_maps[0].vertices = uvs;
        uv_maps[0].polygon_indices = uv_indices;

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
