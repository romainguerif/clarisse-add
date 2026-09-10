// Node QwenTest -- node de test minimale
//
// Produit un cube de cote "size" centre sur l'origine. Sert a valider la
// chaine de build (cmagen, cl, link) et le chargement du module dans
// Clarisse. API verifiees : pattern create_resource / footer de
// cloth_panel.cpp, signature PolyMesh::set de poly_mesh.h:162.

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
#include <gmath_matrix4x4.h>

#include <qwen_test.cma>

class QwenTestModule : public ModulePolymesh {
public:
    QwenTestModule() : ModulePolymesh() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {"size"};
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
            PolyMesh *mesh = build_cube();
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

    PolyMesh *
    build_cube() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        const double size = read_double(object, "size", 1.0);
        if (size <= 0.0) return 0;
        const double h = size * 0.5;

        // 8 sommets dans l'ordre : bas (y = -h) puis haut (y = +h).
        CoreArray<GMathVec3f> vertices(8);
        vertices[0] = GMathVec3f(float(-h), float(-h), float(-h));
        vertices[1] = GMathVec3f(float( h), float(-h), float(-h));
        vertices[2] = GMathVec3f(float( h), float(-h), float( h));
        vertices[3] = GMathVec3f(float(-h), float(-h), float( h));
        vertices[4] = GMathVec3f(float(-h), float( h), float(-h));
        vertices[5] = GMathVec3f(float( h), float( h), float(-h));
        vertices[6] = GMathVec3f(float( h), float( h), float( h));
        vertices[7] = GMathVec3f(float(-h), float( h), float( h));

        // 6 quads, sommets en sens anti-horaire vu de l'exterieur
        // (normale sortante).
        CoreArray<unsigned int> polygon_indices(24);
        CoreArray<unsigned int> polygon_vertex_count(6);
        for (unsigned int f = 0; f < 6; f++) polygon_vertex_count[f] = 4u;

        // nord (+Z), sud (-Z), est (+X), ouest (-X), haut (+Y), bas (-Y).
        static const unsigned int sides[] = {
            2, 6, 7, 3,
            0, 4, 5, 1,
            1, 5, 6, 2,
            0, 3, 7, 4,
            4, 7, 6, 5,
            0, 2, 3, 1
        };
        for (unsigned int i = 0; i < 24; i++) polygon_indices[i] = sides[i];

        CoreArray<unsigned int> polygon_shading_groups(6);
        for (unsigned int f = 0; f < 6; f++) polygon_shading_groups[f] = 0u;
        CoreArray<CoreString> shading_group_names(1);
        shading_group_names[0] = "default";

        CoreArray<GeometryUvMap> uv_maps;
        CoreArray<GeometryNormalMap> normal_maps;
        CoreArray<GeometryColorMap> color_maps;
        CoreArray<GMathVec3f> velocities;

        PolyMesh *result = new PolyMesh;
        result->set(vertices, velocities,
                    polygon_indices, polygon_vertex_count,
                    polygon_shading_groups, shading_group_names,
                    uv_maps, normal_maps, color_maps,
                    true, 0);
        return result;
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(QwenTest, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(QwenTest)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(QwenTest);
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
    QwenTestModule *module = new QwenTestModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
