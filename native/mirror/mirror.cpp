// Node GeometryMirror -- V1
//
// Etape 1 (plan docs/mirror-clarisse.md) : echafaudage. La classe existe,
// la factory renvoie le module, le node compile. Pas encore de geometrie :
// create_resource delegue a ModulePolymesh, comme cloth_panel.cpp:576-584.

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

#include <mirror.cma>

class MirrorModule : public ModulePolymesh {
public:
    MirrorModule() : ModulePolymesh() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "geometry", "use_locator", "locator", "center", "normal",
            "offset", "clamp", "weld", "weld_threshold"
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
            // Etape 2 : lecture de la source (face_select.cpp:245-290).
            // Si la geometrie d'entree est vide, on retourne 0 : le parent
            // gere le cas, pattern cloth_panel.cpp:576-584.
            PolyMesh *mesh = build_mesh();
            if (mesh != 0) return mesh;
        }
        return ModulePolymesh::create_resource(id, data);
    }

private:
    // Lire la polymesh d'entree : positions en espace monde (matrice
    // global de la source appliquee, face_select.cpp:260-274), topologie
    // (poly_mesh.h:200-210), en espace monde.
    struct Source {
        CoreArray<GMathVec3d> vertices;
        CoreArray<unsigned int> indices;
        CoreArray<unsigned int> sizes;
    };

    bool
    read_source(const OfObject *object, Source& out) const
    {
        const OfAttr *geometry_attr = object->get_attribute("geometry");
        if (geometry_attr == 0) return false;
        OfObject *source = geometry_attr->get_object();
        if (source == 0) return false;

        ModuleGeometry *module = const_cast<OfObject&>(*source).get_module<ModuleGeometry>();
        if (module == 0) return false;
        const GeometryObject *geometry = module->get_geometry(false);
        if (geometry == 0) return false;
        const PolyMesh *mesh = CoreBaseObject::cast<PolyMesh>(geometry);
        if (mesh == 0) return false;
        const GeometryPointCloud *cloud = mesh->get_point_cloud();
        if (cloud == 0) return false;

        mesh->get_polygon_vertex_indices(out.indices);
        mesh->get_polygon_vertex_count(out.sizes);
        if (out.sizes.get_count() == 0) return false;

        ModuleSceneItem *item = const_cast<OfObject&>(*source).get_module<ModuleSceneItem>();
        const GMathMatrix4x4d *matrix =
            (item != 0) ? &item->get_global_matrix() : 0;

        const unsigned int count = cloud->get_point_count();
        if (count < 3) return false;
        out.vertices.resize(count);
        for (unsigned int i = 0; i < count; i++) {
            const GMathVec3f local = cloud->get_position(i);
            GMathVec3d world(static_cast<double>(local[0]),
                             static_cast<double>(local[1]),
                             static_cast<double>(local[2]));
            if (matrix != 0) {
                GMathVec3d transformed;
                GMathMatrix4x4d::multiply(transformed, world, *matrix);
                world = transformed;
            }
            out.vertices[i] = world;
        }
        return true;
    }

    // Etape 2 : retourner la source telle quelle (pas encore de miroir).
    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        Source src;
        if (!read_source(object, src)) return 0;

        // Conversion double -> float par composante (cloth_panel.cpp:819-824).
        CoreArray<GMathVec3f> vertices(src.vertices.get_count());
        for (unsigned int i = 0; i < src.vertices.get_count(); i++) {
            const GMathVec3d& v = src.vertices[i];
            vertices[i] = GMathVec3f(float(v[0]), float(v[1]), float(v[2]));
        }

        CoreArray<unsigned int> polygon_shading_groups(src.sizes.get_count());
        for (unsigned int i = 0; i < polygon_shading_groups.get_count(); i++) {
            polygon_shading_groups[i] = 0u;
        }

        CoreArray<CoreString> shading_group_names(1);
        shading_group_names[0] = "default";

        CoreArray<GeometryUvMap> uv_maps;
        CoreArray<GeometryNormalMap> normal_maps;
        CoreArray<GeometryColorMap> color_maps;
        CoreArray<GMathVec3f> velocities;

        PolyMesh *result = new PolyMesh;
        result->set(vertices, velocities,
                    src.indices, src.sizes,
                    polygon_shading_groups, shading_group_names,
                    uv_maps, normal_maps, color_maps,
                    true, 0);
        return result;
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryMirror, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryMirror)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryMirror);
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
    MirrorModule *module = new MirrorModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
