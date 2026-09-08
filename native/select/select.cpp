// La selection de faces : designer une partie d'un maillage, et le dire au
// reste de Clarisse.
//
// C'est la brique qui manquait. Tout ce qui ne doit s'appliquer qu'a une partie
// d'un objet -- capitonner, extruder, texturer autrement -- a besoin de nommer
// cette partie, et Clarisse n'a aucune notion de selection de composants : pas
// de mode edition, pas de sous-objet, rien qui survive a une reevaluation.
//
// Deux choix de conception portent tout le node.
//
// Le premier : **une selection est une regle, pas une liste d'indices.** Une
// liste meurt a la premiere modification de topologie en amont, et elle ne
// s'anime pas. Une regle -- cette texture au-dessus de ce seuil, ces faces qui
// regardent en haut -- se reevalue toute seule, suit le maillage, et se pilote
// par tout ce que Clarisse sait fabriquer. Dans un graphe procedural, c'est la
// seule forme qui tienne.
//
// Le second : **le resultat sort en shading group.** C'est la seule notion de
// groupe de faces que Clarisse connaisse deja, donc la selection devient
// immediatement visible -- on lui assigne un materiau vif pour la regarder --,
// utilisable dans un shading layer, et surtout enchainable : un node de
// selection lit le groupe produit par le precedent, ce qui donne les unions,
// les intersections et les differences sans inventer de type de donnee.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_geometry.h>
#include <module_polymesh.h>
#include <module_scene_item.h>
#include <module_texture.h>

#include <shader_helpers.h>

#include <poly_mesh.h>
#include <geometry_object.h>
#include <geometry_point_cloud.h>

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>
#include <gmath_vec3.h>
#include <gmath_vec4.h>
#include <gmath_matrix4x4.h>

#include <cmath>

#include <select.cma>

namespace {

// Un tirage pseudo-aleatoire stable : la meme face donne toujours le meme
// nombre, quelle que soit la machine et l'ordre d'evaluation. Une selection qui
// changerait d'une image a l'autre ne serait pas utilisable.
inline double
hashed(const unsigned int& index, const unsigned int& seed)
{
    // Le melangeur final de Murmur3. Le precedent, ecrit de memoire, rendait
    // un quart de faces de trop -- deux tirages independants biaises dans le
    // meme sens, ce n'est plus du hasard.
    unsigned int h = index ^ (seed * 0x9e3779b9u);
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return double(h) / 4294967296.0;
}

inline GMathVec3d
face_normal(const CoreArray<GMathVec3f>& points,
            const CoreArray<unsigned int>& indices,
            const unsigned int& base, const unsigned int& sides)
{
    // Newell : elle tient sur les faces gauches, ce qui est le cas courant des
    // qu'on sort d'un maillage bien range.
    GMathVec3d n(0.0, 0.0, 0.0);
    for (unsigned int s = 0; s < sides; s++) {
        const GMathVec3f& a = points[indices[base + s]];
        const GMathVec3f& b = points[indices[base + (s + 1u) % sides]];
        n[0] += double(a[1] - b[1]) * double(a[2] + b[2]);
        n[1] += double(a[2] - b[2]) * double(a[0] + b[0]);
        n[2] += double(a[0] - b[0]) * double(a[1] + b[1]);
    }
    const double length = n.get_length();
    return (length > 1e-12) ? GMathVec3d(n[0] / length, n[1] / length,
                                         n[2] / length)
                            : GMathVec3d(0.0, 1.0, 0.0);
}

} // namespace

class SelectModule : public ModulePolymesh {
public:
    SelectModule() : ModulePolymesh() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "input_geometry", "group_name",
            "texture", "texture_channel", "threshold",
            "use_normal", "direction", "angle",
            "random_ratio", "seed",
            "source_group", "mode", "grow"
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
    static double
    read_double(const OfObject *object, const char *name, const double& fallback)
    {
        const OfAttr *attr = object->get_attribute(name);
        return (attr != 0) ? attr->get_double() : fallback;
    }

    static long
    read_long(const OfObject *object, const char *name, const long& fallback)
    {
        const OfAttr *attr = object->get_attribute(name);
        return (attr != 0) ? attr->get_long() : fallback;
    }

    // Evalue la texture aux sommets du maillage. C'est le critere qui ouvre
    // tout : n'importe quelle texture de Clarisse devient un pinceau de
    // selection, y compris une projection ou une carte peinte ailleurs.
    static bool
    sample_texture(OfObject *object, ModuleGeometry *source,
                   const unsigned int& point_count,
                   CoreArray<GMathVec4f>& colors)
    {
        const OfAttr *attr = object->get_attribute("texture");
        if (attr == 0) return false;
        OfObject *texture_object = attr->get_object();
        if (texture_object == 0) return false;
        ModuleTexture *texture = texture_object->get_module<ModuleTexture>();
        if (texture == 0 || source == 0) return false;

        CoreArray<unsigned int> vertices(point_count);
        for (unsigned int i = 0; i < point_count; i++) vertices[i] = i;

        colors.resize(point_count);
        for (unsigned int i = 0; i < point_count; i++) {
            colors[i] = GMathVec4f(0.0f, 0.0f, 0.0f, 0.0f);
        }
        return ShaderHelpers::evaluate_vertices_texture(*source, vertices,
                                                        *texture, colors);
    }

    static double
    channel_of(const GMathVec4f& color, const long& channel)
    {
        switch (channel) {
            case 1: return double(color[0]);
            case 2: return double(color[1]);
            case 3: return double(color[2]);
            case 4: return double(color[3]);
            default: break;
        }
        return 0.2126 * double(color[0]) + 0.7152 * double(color[1])
             + 0.0722 * double(color[2]);
    }

    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        OfAttr *input = object->get_attribute("input_geometry");
        if (input == 0) return 0;
        OfObject *source_object = input->get_object();
        if (source_object == 0) return 0;

        ModuleGeometry *module = source_object->get_module<ModuleGeometry>();
        if (module == 0) return 0;
        const GeometryObject *geometry = module->get_geometry(false);
        if (geometry == 0) return 0;
        const PolyMesh *mesh = CoreBaseObject::cast<PolyMesh>(geometry);
        if (mesh == 0) return 0;

        const GeometryPointCloud *cloud = mesh->get_point_cloud();
        if (cloud == 0) return 0;

        CoreArray<unsigned int> face_indices;
        CoreArray<unsigned int> face_sizes;
        CoreArray<unsigned int> face_groups;
        mesh->get_polygon_vertex_indices(face_indices);
        mesh->get_polygon_vertex_count(face_sizes);
        mesh->get_polygon_shading_groups(face_groups);

        const unsigned int face_count = face_sizes.get_count();
        const unsigned int point_count = cloud->get_point_count();
        if (face_count == 0u || point_count == 0u) return 0;

        CoreArray<GMathVec3f> points(point_count);
        for (unsigned int i = 0; i < point_count; i++) {
            points[i] = cloud->get_position(i);
        }

        // --- les criteres ------------------------------------------------
        const long channel = read_long(object, "texture_channel", 0);
        const double threshold = read_double(object, "threshold", 0.5);
        const bool use_normal =
            read_long(object, "use_normal", 0) != 0;
        const double angle = read_double(object, "angle", 45.0);
        const double ratio = read_double(object, "random_ratio", 1.0);
        const unsigned int seed = (unsigned int) read_long(object, "seed", 0);

        GMathVec3d direction(0.0, 1.0, 0.0);
        const OfAttr *direction_attr = object->get_attribute("direction");
        if (direction_attr != 0) {
            direction = GMathVec3d(direction_attr->get_double(0),
                                   direction_attr->get_double(1),
                                   direction_attr->get_double(2));
        }
        const double direction_length = direction.get_length();
        if (direction_length > 1e-12) {
            direction = GMathVec3d(direction[0] / direction_length,
                                   direction[1] / direction_length,
                                   direction[2] / direction_length);
        }
        // La normale du maillage est dans son espace local, la direction que
        // l'artiste donne est dans celui du monde. C'est la matrice qui les
        // reconcilie, et l'oublier ferait mentir le critere des qu'on tourne
        // l'objet.
        ModuleSceneItem *item = source_object->get_module<ModuleSceneItem>();
        if (item != 0) {
            GMathMatrix4x4d inverse;
            item->get_global_matrix().get_inverse(inverse);
            GMathVec3d local;
            GMathMatrix4x4d::multiply_direction(local, direction, inverse);
            const double length = local.get_length();
            if (length > 1e-12) {
                direction = GMathVec3d(local[0] / length, local[1] / length,
                                       local[2] / length);
            }
        }
        const double cosine = cos(angle * 3.14159265358979323846 / 180.0);

        CoreArray<GMathVec4f> colors;
        const bool has_texture =
            sample_texture(object, module, point_count, colors);

        // --- l'ensemble de depart ----------------------------------------
        const CoreBasicArray<CoreString>& group_names =
            mesh->get_shading_group_names();

        int source_index = -1;
        const OfAttr *source_attr = object->get_attribute("source_group");
        if (source_attr != 0) {
            const CoreString wanted = source_attr->get_string();
            if (wanted.get_count() > 0u) {
                for (unsigned int i = 0; i < group_names.get_count(); i++) {
                    if (group_names[i] == wanted) {
                        source_index = int(i);
                        break;
                    }
                }
            }
        }

        CoreArray<unsigned char> base(face_count);
        for (unsigned int f = 0; f < face_count; f++) {
            base[f] = (source_index >= 0 && f < face_groups.get_count()
                       && int(face_groups[f]) == source_index) ? 1u : 0u;
        }

        // --- l'ensemble des criteres --------------------------------------
        CoreArray<unsigned char> wanted(face_count);
        unsigned int cursor = 0u;
        for (unsigned int f = 0; f < face_count; f++) {
            const unsigned int sides = face_sizes[f];
            const unsigned int start = cursor;
            cursor += sides;

            bool keep = true;

            if (has_texture && sides > 0u) {
                double total = 0.0;
                for (unsigned int s = 0; s < sides; s++) {
                    total += channel_of(colors[face_indices[start + s]],
                                        channel);
                }
                if (total / double(sides) < threshold) keep = false;
            }

            if (keep && use_normal && sides >= 3u) {
                const GMathVec3d n =
                    face_normal(points, face_indices, start, sides);
                if (n[0] * direction[0] + n[1] * direction[1]
                    + n[2] * direction[2] < cosine) {
                    keep = false;
                }
            }

            if (keep && ratio < 1.0 && hashed(f, seed + 7919u) > ratio) {
                keep = false;
            }

            wanted[f] = keep ? 1u : 0u;
        }

        // --- la combinaison ------------------------------------------------
        const long mode = read_long(object, "mode", 0);
        CoreArray<unsigned char> selected(face_count);
        for (unsigned int f = 0; f < face_count; f++) {
            switch (mode) {
                case 1:  selected[f] = (base[f] || wanted[f]) ? 1u : 0u; break;
                case 2:  selected[f] = (base[f] && wanted[f]) ? 1u : 0u; break;
                case 3:  selected[f] = (base[f] && !wanted[f]) ? 1u : 0u; break;
                default: selected[f] = wanted[f]; break;
            }
        }

        grow_selection(selected, face_indices, face_sizes, point_count,
                       int(read_long(object, "grow", 0)));

        // --- le maillage de sortie ------------------------------------------
        CoreString name = "selection";
        const OfAttr *name_attr = object->get_attribute("group_name");
        if (name_attr != 0 && name_attr->get_string().get_count() > 0u) {
            name = name_attr->get_string();
        }

        // Si le groupe existe deja, on le reecrit plutot que d'en empiler un
        // second du meme nom : c'est ce qui permet de reprendre une selection
        // et de l'affiner sans polluer le maillage.
        int target = -1;
        for (unsigned int i = 0; i < group_names.get_count(); i++) {
            if (group_names[i] == name) { target = int(i); break; }
        }

        const unsigned int name_count =
            (target >= 0) ? group_names.get_count()
                          : group_names.get_count() + 1u;
        CoreArray<CoreString> names(name_count);
        for (unsigned int i = 0; i < group_names.get_count(); i++) {
            names[i] = group_names[i];
        }
        if (target < 0) {
            target = int(group_names.get_count());
            names[group_names.get_count()] = name;
        }

        // Les faces retirees du groupe doivent atterrir quelque part. On les
        // laisse dans le groupe qu'elles avaient, sauf si c'etait celui-ci --
        // auquel cas elles retombent sur le premier, qui existe toujours.
        CoreArray<unsigned int> groups(face_count);
        for (unsigned int f = 0; f < face_count; f++) {
            unsigned int current =
                (f < face_groups.get_count()) ? face_groups[f] : 0u;
            if (selected[f]) {
                current = (unsigned int) target;
            } else if (int(current) == target) {
                current = 0u;
            }
            groups[f] = current;
        }

        CoreArray<GMathVec3f> velocities;
        CoreArray<GeometryUvMap> uv_maps;
        CoreArray<GeometryNormalMap> normal_maps;
        CoreArray<GeometryColorMap> color_maps;
        copy_maps(mesh, uv_maps, normal_maps, color_maps);

        PolyMesh *result = new PolyMesh;
        result->set(points, velocities, face_indices, face_sizes,
                    groups, names, uv_maps, normal_maps, color_maps,
                    true, 0);
        return result;
    }

    // Elargit ou retrecit la selection. Deux faces sont voisines des qu'elles
    // partagent un sommet -- c'est un peu plus large qu'un voisinage par arete,
    // mais ca ne demande aucune table d'aretes et ca se comporte mieux sur les
    // maillages a n-gones, ou les aretes ne sont pas toujours partagees
    // proprement.
    static void
    grow_selection(CoreArray<unsigned char>& selected,
                   const CoreArray<unsigned int>& face_indices,
                   const CoreArray<unsigned int>& face_sizes,
                   const unsigned int& point_count, const int& steps)
    {
        if (steps == 0) return;
        const unsigned int face_count = face_sizes.get_count();
        const unsigned int passes =
            (unsigned int) ((steps > 0) ? steps : -steps);
        const bool expand = steps > 0;

        CoreArray<unsigned char> touched(point_count);
        CoreArray<unsigned char> next(face_count);

        for (unsigned int pass = 0; pass < passes; pass++) {
            for (unsigned int i = 0; i < point_count; i++) touched[i] = 0u;

            // Un sommet est marque s'il appartient a une face selectionnee --
            // ou, en erosion, a une face qui ne l'est pas.
            unsigned int cursor = 0u;
            for (unsigned int f = 0; f < face_count; f++) {
                const unsigned int sides = face_sizes[f];
                const bool mark = expand ? (selected[f] != 0u)
                                         : (selected[f] == 0u);
                if (mark) {
                    for (unsigned int s = 0; s < sides; s++) {
                        touched[face_indices[cursor + s]] = 1u;
                    }
                }
                cursor += sides;
            }

            cursor = 0u;
            for (unsigned int f = 0; f < face_count; f++) {
                const unsigned int sides = face_sizes[f];
                bool any = false;
                for (unsigned int s = 0; s < sides; s++) {
                    if (touched[face_indices[cursor + s]]) { any = true; break; }
                }
                next[f] = expand ? ((selected[f] || any) ? 1u : 0u)
                                 : ((selected[f] && !any) ? 1u : 0u);
                cursor += sides;
            }
            for (unsigned int f = 0; f < face_count; f++) selected[f] = next[f];
        }
    }

    // Le maillage ressort tel quel : il ne faut donc perdre ni les UV, ni les
    // normales, ni les couleurs en passant par ce node.
    static void
    copy_maps(const PolyMesh *mesh, CoreArray<GeometryUvMap>& uv_maps,
              CoreArray<GeometryNormalMap>& normal_maps,
              CoreArray<GeometryColorMap>& color_maps)
    {
        const unsigned int uv_count = mesh->get_uv_map_count();
        if (uv_count > 0u) {
            uv_maps.resize(uv_count);
            for (unsigned int i = 0; i < uv_count; i++) {
                CoreArray<GMathVec3f> uvs;
                CoreArray<unsigned int> indices;
                if (!mesh->get_uv_map_data(i, uvs, indices)) continue;
                uv_maps[i].name = mesh->get_uv_map_name(i);
                uv_maps[i].vertices.resize(uvs.get_count());
                for (unsigned int k = 0; k < uvs.get_count(); k++) {
                    uv_maps[i].vertices[k] = uvs[k];
                }
                uv_maps[i].polygon_indices.resize(indices.get_count());
                for (unsigned int k = 0; k < indices.get_count(); k++) {
                    uv_maps[i].polygon_indices[k] = indices[k];
                }
            }
        }

        const unsigned int normal_count = mesh->get_normal_map_count();
        if (normal_count > 0u) {
            normal_maps.resize(normal_count);
            for (unsigned int i = 0; i < normal_count; i++) {
                CoreArray<GMathVec3f> normals;
                CoreArray<unsigned int> indices;
                if (!mesh->get_normal_map_data(i, normals, indices)) continue;
                normal_maps[i].name = mesh->get_normal_map_name(i);
                normal_maps[i].normals.resize(normals.get_count());
                for (unsigned int k = 0; k < normals.get_count(); k++) {
                    normal_maps[i].normals[k] = normals[k];
                }
                normal_maps[i].polygon_indices.resize(indices.get_count());
                for (unsigned int k = 0; k < indices.get_count(); k++) {
                    normal_maps[i].polygon_indices[k] = indices[k];
                }
            }
        }

        const unsigned int color_count = mesh->get_color_map_count();
        if (color_count > 0u) {
            color_maps.resize(color_count);
            for (unsigned int i = 0; i < color_count; i++) {
                CoreArray<GMathVec4uc> colors;
                CoreArray<unsigned int> indices;
                if (!mesh->get_color_map_data(i, colors, indices)) continue;
                color_maps[i].name = mesh->get_color_map_name(i);
                color_maps[i].colors.resize(colors.get_count());
                for (unsigned int k = 0; k < colors.get_count(); k++) {
                    color_maps[i].colors[k] = colors[k];
                }
                color_maps[i].polygon_indices.resize(indices.get_count());
                for (unsigned int k = 0; k < indices.get_count(); k++) {
                    color_maps[i].polygon_indices[k] = indices[k];
                }
            }
        }
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometrySelect, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometrySelect)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometrySelect);
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
    SelectModule *module = new SelectModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
