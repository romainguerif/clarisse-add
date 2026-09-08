// Des points repartis le long d'une courbe, a espacement reel constant.
//
// C'est la brique qui manque pour distribuer : le Scatterer natif de Clarisse
// prend n'importe quelle Geometry comme support et instancie un objet par
// sommet. Il ne sait pas parcourir une courbe, mais il sait consommer un nuage
// de points -- alors on lui en fabrique un.
//
// Le point qui fait tout l'interet du node est la reparametrisation par
// longueur d'arc. Poser les points a intervalle regulier de parametre les
// tasserait dans les virages, la ou la courbe avance moins vite : vingt
// lampadaires ne seraient pas a vingt intervalles egaux. curve_core::Path tient
// la table des longueurs cumulees et repond par eval_at_distance, ce qui rend
// l'espacement exact quelle que soit la forme.
//
// L'orientation passe par la normale de chaque point, seul canal que le
// Scatterer sache lire (use_support_normals). Un nuage de points Clarisse ne
// porte ni matrice ni quaternion, donc c'est un axe et pas un repere complet ;
// le reste se regle avec les attributs scatter_rotation du Scatterer, qui sont
// texturables.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_geometry.h>
#include <module_particle.h>

#include <particle_cloud.h>
#include <geometry_point_cloud.h>

#include <core_array.h>
#include <core_vector.h>
#include <gmath_vec3.h>

#include <curve_core.h>

#include <curve_points.cma>

using namespace curve_core;

namespace {

enum { MODE_COUNT = 0, MODE_SPACING = 1, MODE_ENDS = 2 };
enum { NORMAL_TANGENT = 0, NORMAL_NORMAL = 1, NORMAL_BINORMAL = 2, NORMAL_UP = 3 };

} // namespace

class CurvePointsModule : public ModuleParticle {
public:
    CurvePointsModule() : ModuleParticle() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModuleParticle::module_constructor(object);

        static const char *const names[] = {
            "control_points", "closed", "steps", "mode", "count", "spacing",
            "offset", "include_last", "normal_source", "lateral_offset",
            "interpolation", "bend_radius", "slack", "gravity", "end_offset"
        };
        const unsigned int name_count = sizeof(names) / sizeof(names[0]);

        // Compter d'abord, allouer a la taille exacte ensuite : CoreArray a
        // bien un resize, mais il fait delete[] puis new[] sans rien preserver,
        // et OfAttrDirtiness ne s'initialise pas au constructeur par defaut.
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
                // DIRTINESS_ALL : un locator qui bouge remonte DIRTINESS_MOTION,
                // pas DIRTINESS_GEOMETRY.
                attrs[k++] = OfAttrDirtiness(attr, OfAttr::DIRTINESS_ALL);
            }
        }

        set_resource_attrs(ModuleGeometry::RESOURCE_ID_GEOMETRY, attrs);
    }

    ResourceData *
    create_resource(const int& id, void *data) const override
    {
        if (id == ModuleGeometry::RESOURCE_ID_GEOMETRY) {
            ParticleCloud *cloud = build_cloud();
            if (cloud != 0) return cloud;
        }
        return ModuleParticle::create_resource(id, data);
    }

private:
    ParticleCloud *
    build_cloud() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        const bool closed = object->get_attribute("closed")->get_bool();

        Path path;
        if (!build_from_object(*object, path)) return 0;

        const double length = path.get_length();
        if (length <= 1e-9) return 0;

        const long mode = object->get_attribute("mode")->get_long();
        const double offset = object->get_attribute("offset")->get_double();
        const bool include_last = object->get_attribute("include_last")->get_bool();
        const long normal_source = object->get_attribute("normal_source")->get_long();
        const double lateral = object->get_attribute("lateral_offset")->get_double();

        // Le mode Extremites est a part : deux points, et surtout deux
        // tangentes opposees. Un connecteur pose au depart doit regarder vers
        // l'exterieur du cable, pas dans le sens de parcours -- sans quoi les
        // deux embouts d'un meme cable pointent du meme cote.
        if (mode == MODE_ENDS) {
            const double end_offset =
                object->get_attribute("end_offset")->get_double();

            CoreArray<GMathVec3f> ends(2);
            CoreArray<GMathVec3f> end_normals(2);

            for (unsigned int i = 0; i < 2u; i++) {
                const double distance = (i == 0u) ? -end_offset
                                                  : length + end_offset;
                GMathVec3d position, tangent, normal;
                path.eval_at_distance(distance, position, tangent, normal);

                // eval_at_distance borne aux extremites ; on prolonge droit au
                // dela, sinon un offset positif ne sortirait jamais du cable.
                if (distance < 0.0) {
                    position = vadd(position, vscale(tangent, distance));
                } else if (distance > length) {
                    position = vadd(position, vscale(tangent, distance - length));
                }

                GMathVec3d axis = (i == 0u) ? vscale(tangent, -1.0) : tangent;
                switch (normal_source) {
                    case NORMAL_NORMAL:   axis = normal; break;
                    case NORMAL_BINORMAL: axis = vnorm(vcross(tangent, normal)); break;
                    case NORMAL_UP:       axis = GMathVec3d(0.0, 1.0, 0.0); break;
                    default: break;
                }

                ends[i] = GMathVec3f(float(position[0]), float(position[1]),
                                     float(position[2]));
                end_normals[i] = GMathVec3f(float(axis[0]), float(axis[1]),
                                            float(axis[2]));
            }

            GeometryPointCloud caps;
            if (!caps.init(2u, ends.get_data(), end_normals.get_data(), 0)) {
                return 0;
            }
            return new ParticleCloud(caps);
        }

        // Combien de points, et a quel pas.
        unsigned int count = 0;
        double step = 0.0;
        if (mode == MODE_SPACING) {
            const double spacing = object->get_attribute("spacing")->get_double();
            if (spacing <= 1e-9) return 0;
            step = spacing;
            count = (unsigned int) (length / spacing) + 1u;
        } else {
            count = (unsigned int) object->get_attribute("count")->get_long();
            if (count < 1u) count = 1u;
            // Une courbe fermee n'a pas de bout : le dernier point retomberait
            // sur le premier. Un point de plus dans le denominateur l'evite.
            const bool span_ends = include_last && !closed && count > 1u;
            step = span_ends ? length / double(count - 1u) : length / double(count);
        }
        if (count < 1u) return 0;

        CoreArray<GMathVec3f> positions(count);
        CoreArray<GMathVec3f> normals(count);

        for (unsigned int i = 0; i < count; i++) {
            // Le repli cyclique des courbes fermees est fait par
            // eval_at_distance, couture comprise.
            const double distance = (double(i) + offset) * step;

            GMathVec3d position, tangent, normal;
            path.eval_at_distance(distance, position, tangent, normal);

            if (lateral != 0.0) {
                position = vadd(position, vscale(normal, lateral));
            }

            GMathVec3d axis;
            switch (normal_source) {
                case NORMAL_NORMAL:   axis = normal; break;
                case NORMAL_BINORMAL: axis = vnorm(vcross(tangent, normal)); break;
                case NORMAL_UP:       axis = GMathVec3d(0.0, 1.0, 0.0); break;
                default:              axis = tangent; break;
            }

            positions[i] = GMathVec3f(float(position[0]), float(position[1]),
                                      float(position[2]));
            normals[i] = GMathVec3f(float(axis[0]), float(axis[1]), float(axis[2]));
        }

        GeometryPointCloud points;
        if (!points.init(count, positions.get_data(), normals.get_data(), 0)) {
            return 0;
        }
        return new ParticleCloud(points);
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryCurvePoints, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryCurvePoints)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryCurvePoints);
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
    CurvePointsModule *module = new CurvePointsModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
