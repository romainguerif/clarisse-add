// Des cables en masse entre deux nuages de points.
//
// Le tube pose un cable a la fois, avec des points de controle qu'on choisit.
// Ici c'est l'inverse : on donne deux nuages et le node tend un cable par
// paire. C'est le seul moyen d'atteindre l'echelle des faisceaux de plusieurs
// centaines de brins, ou poser les points a la main n'a plus de sens.
//
// Chaque cable resout sa propre chainette et porte son propre bruit, avec une
// graine derivee de son indice. Deux consequences : ils ne sont jamais
// identiques, et ils sont toujours les memes d'une evaluation a l'autre. La
// seconde compte autant que la premiere -- un node qui change d'aspect a chaque
// reevaluation ne tient pas dans un graphe.
//
// Le balayage est volontairement plus simple que celui du tube : section ronde,
// pas de torons, pas de bouchons. A cette echelle, chaque cable fait quelques
// dizaines de pixels ; ce qui compte est leur nombre et leur variete, pas le
// detail de chacun.

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
#include <geometry_point_cloud.h>

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>
#include <gmath_vec3.h>
#include <gmath_matrix4x4.h>

#include <curve_core.h>

#include <cable_field.cma>

using namespace curve_core;

namespace {

enum { PAIR_INDEX = 0, PAIR_NEAREST = 1, PAIR_RANDOM = 2 };

// Les points monde d'une geometrie referencee.
bool
read_cloud(OfObject& object, const char *attr_name, CoreVector<GMathVec3d>& out)
{
    OfAttr *attr = object.get_attribute(attr_name);
    if (attr == 0) return false;

    OfObject *source = attr->get_object();
    if (source == 0) return false;

    ModuleGeometry *module = source->get_module<ModuleGeometry>();
    if (module == 0) return false;

    const GeometryObject *geometry = module->get_geometry(false);
    if (geometry == 0) return false;

    const GeometryPointCloud *cloud = geometry->get_point_cloud();
    if (cloud == 0) return false;

    ModuleSceneItem *item = source->get_module<ModuleSceneItem>();
    const GMathMatrix4x4d *matrix = (item != 0) ? &item->get_global_matrix() : 0;

    const unsigned int count = cloud->get_point_count();
    for (unsigned int i = 0; i < count; i++) {
        const GMathVec3f sample = cloud->get_position(i);
        GMathVec3d world(static_cast<double>(sample[0]),
                         static_cast<double>(sample[1]),
                         static_cast<double>(sample[2]));
        if (matrix != 0) {
            GMathVec3d transformed;
            GMathMatrix4x4d::multiply(transformed, world, *matrix);
            world = transformed;
        }
        out.add(world);
    }
    return out.get_count() > 0u;
}

// Un aleatoire deterministe dans [0, 1[, derive d'un indice et d'une graine.
inline double
jitter(const unsigned int& index, const unsigned int& seed)
{
    unsigned int h = index * 2654435761u + seed * 40503u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return double(h & 0xFFFFFFu) / double(0x1000000u);
}

} // namespace

class CableFieldModule : public ModulePolymesh {
public:
    CableFieldModule() : ModulePolymesh() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "points_start", "points_end", "pairing", "count", "seed",
            "radius", "radius_variation", "sides", "steps",
            "slack", "slack_variation", "gravity",
            "noise_amplitude", "noise_frequency", "noise_octaves",
            "noise_roughness", "noise_fade"
        };
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
            PolyMesh *mesh = build_mesh();
            if (mesh != 0) return mesh;
        }
        return ModulePolymesh::create_resource(id, data);
    }

private:
    // Les paires de points a relier, selon la strategie choisie.
    void
    build_pairs(OfObject& object, const CoreVector<GMathVec3d>& starts,
                const CoreVector<GMathVec3d>& ends,
                CoreVector<unsigned int>& from,
                CoreVector<unsigned int>& to) const
    {
        const long pairing = object.get_attribute("pairing")->get_long();
        const unsigned int seed =
            (unsigned int) object.get_attribute("seed")->get_long();
        unsigned int wanted =
            (unsigned int) object.get_attribute("count")->get_long();

        const unsigned int a = starts.get_count();
        const unsigned int b = ends.get_count();
        if (a == 0u || b == 0u) return;

        unsigned int total = (a < b) ? a : b;
        if (wanted > 0u && wanted < total) total = wanted;

        for (unsigned int i = 0; i < total; i++) {
            unsigned int j = i % b;

            if (pairing == PAIR_NEAREST) {
                // La ou le plus proche donne des cables courts, donc lisibles :
                // c'est ce qu'on veut pour relier deux surfaces face a face.
                double best = -1.0;
                for (unsigned int c = 0; c < b; c++) {
                    const GMathVec3d delta = vsub(ends[c], starts[i]);
                    const double squared = vdot(delta, delta);
                    if (best < 0.0 || squared < best) { best = squared; j = c; }
                }
            } else if (pairing == PAIR_RANDOM) {
                j = (unsigned int) (jitter(i, seed + 7717u) * double(b));
                if (j >= b) j = b - 1u;
            }

            // Un cable qui relierait un point a lui-meme n'a pas de longueur.
            const GMathVec3d delta = vsub(ends[j], starts[i]);
            if (vdot(delta, delta) < 1e-12) continue;

            from.add(i);
            to.add(j);
        }
    }

    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        CoreVector<GMathVec3d> starts;
        if (!read_cloud(*object, "points_start", starts)) return 0;

        CoreVector<GMathVec3d> ends;
        if (!read_cloud(*object, "points_end", ends)) {
            // Sans second nuage, les cables relient le premier a lui-meme --
            // decale d'un cran, sinon chaque point se relierait a lui-meme.
            for (unsigned int i = 0; i < starts.get_count(); i++) {
                ends.add(starts[(i + 1u) % starts.get_count()]);
            }
        }

        CoreVector<unsigned int> from, to;
        build_pairs(*object, starts, ends, from, to);
        const unsigned int cables = from.get_count();
        if (cables == 0u) return 0;

        const unsigned int seed =
            (unsigned int) object->get_attribute("seed")->get_long();
        const double radius = object->get_attribute("radius")->get_double();
        const double radius_variation =
            object->get_attribute("radius_variation")->get_double();
        const double slack = object->get_attribute("slack")->get_double();
        const double slack_variation =
            object->get_attribute("slack_variation")->get_double();

        unsigned int sides = (unsigned int) object->get_attribute("sides")->get_long();
        unsigned int steps = (unsigned int) object->get_attribute("steps")->get_long();
        if (sides < 3u) sides = 3u;
        if (steps < 2u) steps = 2u;

        const OfAttr *gravity_attr = object->get_attribute("gravity");
        GMathVec3d gravity(0.0, -1.0, 0.0);
        if (gravity_attr != 0) {
            gravity = GMathVec3d(gravity_attr->get_double(0),
                                 gravity_attr->get_double(1),
                                 gravity_attr->get_double(2));
        }

        const double noise_amplitude =
            object->get_attribute("noise_amplitude")->get_double();
        const double noise_frequency =
            object->get_attribute("noise_frequency")->get_double();
        const unsigned int noise_octaves =
            (unsigned int) object->get_attribute("noise_octaves")->get_long();
        const double noise_roughness =
            object->get_attribute("noise_roughness")->get_double();
        const double noise_fade =
            object->get_attribute("noise_fade")->get_double();

        // Chaque cable a le meme nombre d'anneaux : ca rend les comptes exacts
        // et evite une passe de mesure avant de dimensionner les tableaux.
        const unsigned int rings = steps + 1u;
        const unsigned int per_cable_vertices = rings * sides;
        const unsigned int per_cable_quads = (rings - 1u) * sides;

        CoreArray<GMathVec3f> vertices(cables * per_cable_vertices);
        CoreArray<GMathVec3f> velocities;
        CoreArray<unsigned int> polygon_indices(cables * per_cable_quads * 4u);
        CoreArray<unsigned int> polygon_vertex_count(cables * per_cable_quads);
        CoreArray<unsigned int> polygon_shading_groups(cables * per_cable_quads);
        CoreArray<CoreString> shading_group_names(1);
        shading_group_names[0] = "cable";

        const unsigned int uv_cols = sides + 1u;
        CoreArray<GMathVec3f> uvs(cables * rings * uv_cols);
        CoreArray<unsigned int> uv_indices(cables * per_cable_quads * 4u);

        unsigned int written = 0u;

        for (unsigned int c = 0; c < cables; c++) {
            CoreVector<GMathVec3d> ends_pair;
            ends_pair.add(starts[from[c]]);
            ends_pair.add(ends[to[c]]);

            const double my_slack =
                slack * (1.0 - slack_variation * jitter(c, seed + 131u));
            const double my_radius =
                radius * (1.0 - radius_variation * jitter(c, seed + 977u));

            Path path;
            if (!path.build_catenary(ends_pair, false, my_slack, gravity, steps)) {
                continue;
            }
            if (noise_amplitude > 1e-9) {
                path.apply_noise(noise_amplitude, noise_frequency, noise_octaves,
                                 noise_roughness, seed + c * 2287u, noise_fade,
                                 0.35, gravity);
            }
            if (path.get_sample_count() != rings) continue;

            const unsigned int base = written * per_cable_vertices;
            const unsigned int uv_base = written * rings * uv_cols;

            for (unsigned int r = 0; r < rings; r++) {
                const GMathVec3d& centre = path.get_position(r);
                const GMathVec3d& n = path.get_normal(r);
                const GMathVec3d b = path.get_binormal(r);

                for (unsigned int s = 0; s < sides; s++) {
                    const double angle = 2.0 * M_PI * double(s) / double(sides);
                    const GMathVec3d p =
                        vadd(centre, vadd(vscale(n, cos(angle) * my_radius),
                                          vscale(b, sin(angle) * my_radius)));
                    vertices[base + r * sides + s] =
                        GMathVec3f(float(p[0]), float(p[1]), float(p[2]));
                }

                const float v = float(r) / float(rings - 1u);
                for (unsigned int s = 0; s < uv_cols; s++) {
                    uvs[uv_base + r * uv_cols + s] =
                        GMathVec3f(float(s) / float(sides), v, 0.0f);
                }
            }

            unsigned int k = written * per_cable_quads * 4u;
            unsigned int face = written * per_cable_quads;
            for (unsigned int r = 0; r + 1u < rings; r++) {
                for (unsigned int s = 0; s < sides; s++) {
                    const unsigned int s1 = (s + 1u) % sides;
                    polygon_indices[k + 0u] = base + r * sides + s;
                    polygon_indices[k + 1u] = base + r * sides + s1;
                    polygon_indices[k + 2u] = base + (r + 1u) * sides + s1;
                    polygon_indices[k + 3u] = base + (r + 1u) * sides + s;

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
            written++;
        }

        if (written == 0u) return 0;

        // Les cables degeneres ont ete sautes : on rend les tableaux a la
        // taille reellement ecrite plutot que de laisser des sommets a zero,
        // qui feraient un amas de triangles a l'origine.
        if (written < cables) {
            trim(vertices, written * per_cable_vertices);
            trim(polygon_indices, written * per_cable_quads * 4u);
            trim(polygon_vertex_count, written * per_cable_quads);
            trim(polygon_shading_groups, written * per_cable_quads);
            trim(uvs, written * rings * uv_cols);
            trim(uv_indices, written * per_cable_quads * 4u);
        }

        CoreArray<GeometryUvMap> uv_maps(1);
        uv_maps[0].name = "uv";
        uv_maps[0].vertices = uvs;
        uv_maps[0].polygon_indices = uv_indices;

        CoreArray<GeometryNormalMap> normal_maps;
        CoreArray<GeometryColorMap> color_maps;

        PolyMesh *mesh = new PolyMesh;
        mesh->set(vertices, velocities,
                  polygon_indices, polygon_vertex_count, polygon_shading_groups,
                  shading_group_names,
                  uv_maps, normal_maps, color_maps,
                  true, 0);
        return mesh;
    }

    // CoreArray::resize detruit son contenu -- il fait delete[] puis new[]. Pour
    // raccourcir un tableau en gardant ce qu'il porte, il faut recopier.
    template<class T>
    static void
    trim(CoreArray<T>& array, const unsigned int& size)
    {
        if (size >= array.get_count()) return;
        CoreArray<T> kept(size);
        for (unsigned int i = 0; i < size; i++) kept[i] = array[i];
        array.resize(size);
        for (unsigned int i = 0; i < size; i++) array[i] = kept[i];
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryCableField, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryCableField)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryCableField);
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
    CableFieldModule *module = new CableFieldModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
