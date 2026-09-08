// L'outil de selection de faces : designer a la souris ce qu'aucune regle ne
// decrira.
//
// Le node GeometrySelect sait fabriquer une selection par regle -- cette
// texture au-dessus de ce seuil, ces faces qui regardent en haut. C'est ce qui
// tient dans un graphe procedural. Mais il y a des cas ou l'artiste sait
// exactement quelles faces il veut, et ou aucune regle ne les decrira : c'est
// meme le cas courant. Cet outil-la remplit sa table d'indices.
//
// Deux choix expliquent sa forme.
//
// Il vit dans la barre d'outils, pas dans un node. Un outil enferme dans un
// node ne sert que ce node ; celui-ci ecrit dans celui qu'on lui designe, et il
// servira demain a tout ce qui aura besoin de faces -- une extrusion, un
// materiau par face, un decoupage.
//
// Il lance ses rayons lui-meme, en force brute sur le maillage vise, plutot que
// d'interroger la structure d'acceleration de Clarisse. Un clic, c'est un
// rayon : meme sur un million de faces, la boucle tient en quelques
// millisecondes, et on ne depend d'aucune API dont on n'aurait pas verifie le
// comportement en dehors du rendu.

// windows.h avant GL/gl.h : l'en-tete OpenGL de Microsoft ne declare ni
// WINGDIAPI ni APIENTRY, il les attend. Les deux gardes evitent au passage que
// windows.h ne redefinisse min et max sous le nez du SDK -- ce qui casse
// gmath_vec2.h avec une erreur de syntaxe incomprehensible.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_context.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_tool.h>
#include <module_scene_item.h>
#include <module_geometry.h>

#include <ctx_tool.h>
#include <ctx_draw.h>
#include <gui_widget.h>
#include <gui_global.h>

#include <poly_mesh.h>
#include <geometry_object.h>
#include <geometry_point_cloud.h>

#include <GL/gl.h>

#include <gmath_vec3.h>
#include <gmath_matrix4x4.h>
#include <gmath_view_point.h>

#include <core_array.h>
#include <core_vector.h>

#include <face_select.cma>

namespace {

inline GMathVec3d
vsub(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

inline GMathVec3d
vcross(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[1] * b[2] - a[2] * b[1],
                      a[2] * b[0] - a[0] * b[2],
                      a[0] * b[1] - a[1] * b[0]);
}

inline double
vdot(const GMathVec3d& a, const GMathVec3d& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Moller-Trumbore. On garde le determinant signe pour savoir de quel cote on
// touche : c'est ce qui permet d'ignorer les faces qui tournent le dos.
inline bool
ray_triangle(const GMathVec3d& origin, const GMathVec3d& direction,
             const GMathVec3d& a, const GMathVec3d& b, const GMathVec3d& c,
             double& distance, bool& front)
{
    const GMathVec3d ab = vsub(b, a);
    const GMathVec3d ac = vsub(c, a);
    const GMathVec3d p = vcross(direction, ac);
    const double det = vdot(ab, p);
    if (det > -1e-12 && det < 1e-12) return false;

    const double inverse = 1.0 / det;
    const GMathVec3d t = vsub(origin, a);
    const double u = vdot(t, p) * inverse;
    if (u < 0.0 || u > 1.0) return false;

    const GMathVec3d q = vcross(t, ab);
    const double v = vdot(direction, q) * inverse;
    if (v < 0.0 || u + v > 1.0) return false;

    const double hit = vdot(ac, q) * inverse;
    if (hit <= 1e-9) return false;

    distance = hit;
    front = det < 0.0;
    return true;
}

} // namespace

class FaceSelectModule : public ModuleTool {
public:
    FaceSelectModule() : ModuleTool(), m_painting(false), m_removing(false) {}

    bool m_painting;
    bool m_removing;

    // Le maillage vise, ramene en coordonnees du monde. On le relit a chaque
    // geste : l'objet peut avoir bouge, et une selection posee sur une position
    // perimee tomberait a cote sans rien dire.
    bool
    read_mesh(CoreVector<GMathVec3d>& points,
              CoreArray<unsigned int>& face_indices,
              CoreArray<unsigned int>& face_sizes) const
    {
        OfObject *select = get_target();
        if (select == 0) return false;

        OfAttr *input = select->get_attribute("input_geometry");
        if (input == 0) return false;
        OfObject *source = input->get_object();
        if (source == 0) return false;

        ModuleGeometry *module = source->get_module<ModuleGeometry>();
        if (module == 0) return false;
        const GeometryObject *geometry = module->get_geometry(false);
        if (geometry == 0) return false;
        const PolyMesh *mesh = CoreBaseObject::cast<PolyMesh>(geometry);
        if (mesh == 0) return false;

        const GeometryPointCloud *cloud = mesh->get_point_cloud();
        if (cloud == 0) return false;

        mesh->get_polygon_vertex_indices(face_indices);
        mesh->get_polygon_vertex_count(face_sizes);

        ModuleSceneItem *item = source->get_module<ModuleSceneItem>();
        const GMathMatrix4x4d *matrix =
            (item != 0) ? &item->get_global_matrix() : 0;

        const unsigned int count = cloud->get_point_count();
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
            points.add(world);
        }
        return points.get_count() > 0u;
    }

    OfObject *
    get_target() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        OfAttr *attr = object->get_attribute("target");
        if (attr != 0) {
            OfObject *named = attr->get_object();
            // On n'accepte qu'un node qui sait recevoir une liste de faces :
            // pointer autre chose serait une erreur silencieuse.
            if (named != 0 && named->get_attribute("index") != 0) return named;
        }
        return 0;
    }

    static bool
    build_ray(const CtxTool& ctx, const int& px, const int& py,
              GMathVec3d& origin, GMathVec3d& direction)
    {
        const GMathViewPoint *view = ctx.gl.view_point;
        if (view == 0 || ctx.gl.width <= 0 || ctx.gl.height <= 0) return false;

        const double nx = 2.0 * (double(px) / double(ctx.gl.width)) - 1.0;
        const double ny = 1.0 - 2.0 * (double(py) / double(ctx.gl.height));

        const double h = view->get_horizontal_zoom_factor(1.0);
        const double v = view->get_vertical_zoom_factor(1.0);
        const double aspect = view->get_aspect_ratio();
        const double lx = (aspect != 0.0) ? (nx * h / aspect) : (nx * h);
        const double ly = ny * v;

        const GMathVec3d x_axis = view->get_x_axis();
        const GMathVec3d y_axis = view->get_y_axis();
        const GMathVec3d z_axis = view->get_z_axis();

        origin = view->get_eye_position();

        if (view->is_orthographic()) {
            // En orthographique le rayon ne diverge pas : c'est l'origine qui
            // se deplace dans le plan image.
            for (unsigned int i = 0; i < 3; i++) {
                origin[i] += x_axis[i] * lx + y_axis[i] * ly;
                direction[i] = -z_axis[i];
            }
        } else {
            // La camera regarde vers -Z.
            for (unsigned int i = 0; i < 3; i++) {
                direction[i] = x_axis[i] * lx + y_axis[i] * ly - z_axis[i];
            }
        }

        const double length = direction.get_length();
        if (length < 1e-12) return false;
        for (unsigned int i = 0; i < 3; i++) direction[i] /= length;
        return true;
    }

    // La face sous le curseur, ou -1. Force brute : un clic est un rayon, et
    // meme un million de faces se balaye en quelques millisecondes.
    int
    pick_face(const CtxTool& ctx, const int& px, const int& py,
              const CoreVector<GMathVec3d>& points,
              const CoreArray<unsigned int>& face_indices,
              const CoreArray<unsigned int>& face_sizes,
              const bool& backfaces) const
    {
        GMathVec3d origin, direction;
        if (!build_ray(ctx, px, py, origin, direction)) return -1;

        int best = -1;
        double nearest = 1e30;
        unsigned int cursor = 0u;

        for (unsigned int f = 0; f < face_sizes.get_count(); f++) {
            const unsigned int sides = face_sizes[f];
            const unsigned int base = cursor;
            cursor += sides;
            if (sides < 3u) continue;

            // Un eventail depuis le premier sommet : ca suffit pour un test
            // d'intersection, meme sur une face gauche ou concave -- au pire on
            // manque une sliver, jamais une face franche.
            for (unsigned int s = 1; s + 1u < sides; s++) {
                double distance;
                bool front;
                if (!ray_triangle(origin, direction,
                                  points[face_indices[base]],
                                  points[face_indices[base + s]],
                                  points[face_indices[base + s + 1u]],
                                  distance, front)) {
                    continue;
                }
                if (!backfaces && !front) continue;
                if (distance < nearest) { nearest = distance; best = int(f); }
            }
        }
        return best;
    }

    // Ajoute ou retire un indice dans la table du node vise.
    void
    toggle_face(OfObject& target, const unsigned int& face, const bool& remove)
    {
        OfAttr *attr = target.get_attribute("index");
        if (attr == 0) attr = target.get_attribute("faces");
        if (attr == 0) return;

        const unsigned int count = attr->get_value_count();
        int found = -1;
        for (unsigned int i = 0; i < count; i++) {
            if (attr->get_long(i) == long(face)) { found = int(i); break; }
        }

        if (remove) {
            if (found < 0) return;
            // Tasser puis raccourcir : il n'y a pas de retrait par indice sur
            // un attribut multiple.
            for (unsigned int i = (unsigned int) found; i + 1u < count; i++) {
                attr->set_long(attr->get_long(i + 1u), i);
            }
            attr->set_value_count(count - 1u);
            return;
        }

        if (found >= 0) return;
        attr->set_value_count(count + 1u);
        attr->set_long(long(face), count);
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(ToolFaceSelect, ModuleToolCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
    static void draw_tool_3d(OfObject& object, CtxTool& ctx);
    static int process_event(OfObject& object, CtxTool& ctx, const CtxToolEvent& evt);
IX_END_DECLARE_MODULE_CALLBACKS(ToolFaceSelect)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(ToolFaceSelect);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
    module_callbacks->cb_draw_tool_3d = IX_MODULE_CLBK::draw_tool_3d;
    module_callbacks->cb_process_event = IX_MODULE_CLBK::process_event;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    FaceSelectModule *module = new FaceSelectModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}

int
IX_MODULE_CLBK::process_event(OfObject& object, CtxTool& ctx, const CtxToolEvent& evt)
{
    FaceSelectModule *module = object.get_module<FaceSelectModule>();
    if (module == 0) return 0;

    OfObject *target = module->get_target();
    if (target == 0) return 0;

    const bool is_down = (evt.id == EVT_ID_MOUSE_DOWN);
    const bool is_drag = (evt.id == EVT_ID_MOUSE_DRAG);

    if (evt.id == EVT_ID_MOUSE_UP) {
        module->m_painting = false;
        return module->m_painting ? 1 : 0;
    }

    if (!is_down && !is_drag) return 0;
    if (is_drag && !module->m_painting) return 0;
    if (is_down && !evt.mouse.is_left_button()) return 0;

    const OfAttr *paint_attr = object.get_attribute("paint");
    const bool paint = (paint_attr == 0) || paint_attr->get_bool();
    if (is_drag && !paint) return 0;

    if (is_down) {
        // Ctrl retire, toujours. C'est le raccourci qu'on a dans les doigts, et
        // il doit primer sur le reglage du node.
        const OfAttr *mode = object.get_attribute("mode");
        module->m_removing = (mode != 0 && mode->get_long() == 1);
        if (evt.keyboard.is_modifier_ctrl()) module->m_removing = true;
        module->m_painting = true;
        ctx.feedback.is_view_point_locked = true;
    }

    CoreVector<GMathVec3d> points;
    CoreArray<unsigned int> face_indices;
    CoreArray<unsigned int> face_sizes;
    if (!module->read_mesh(points, face_indices, face_sizes)) return 0;

    const OfAttr *back = object.get_attribute("backfaces");
    const int face = module->pick_face(ctx, ctx.gl.x, ctx.gl.y, points,
                                       face_indices, face_sizes,
                                       (back != 0) && back->get_bool());
    if (face < 0) return 1;

    module->toggle_face(*target, (unsigned int) face, module->m_removing);
    ctx.feedback.redraw = true;
    return 1;
}

void
IX_MODULE_CLBK::draw_tool_3d(OfObject& object, CtxTool& ctx)
{
    FaceSelectModule *module = object.get_module<FaceSelectModule>();
    if (module == 0) return;

    const OfAttr *show = object.get_attribute("show_selection");
    if (show != 0 && !show->get_bool()) return;

    OfObject *target = module->get_target();
    if (target == 0) return;

    OfAttr *list = target->get_attribute("index");
    if (list == 0) list = target->get_attribute("faces");
    if (list == 0 || list->get_value_count() == 0u) return;

    CoreVector<GMathVec3d> points;
    CoreArray<unsigned int> face_indices;
    CoreArray<unsigned int> face_sizes;
    if (!module->read_mesh(points, face_indices, face_sizes)) return;

    // Les debuts de chaque face dans la table d'indices. On les calcule une
    // fois : sans ca, surligner la n-ieme face demanderait de reparcourir tout
    // le maillage depuis le debut.
    CoreArray<unsigned int> starts(face_sizes.get_count());
    unsigned int cursor = 0u;
    for (unsigned int f = 0; f < face_sizes.get_count(); f++) {
        starts[f] = cursor;
        cursor += face_sizes[f];
    }

    double color[3] = { 1.0, 0.55, 0.1 };
    const OfAttr *color_attr = object.get_attribute("highlight_color");
    if (color_attr != 0) {
        for (unsigned int i = 0; i < 3; i++) color[i] = color_attr->get_double(i);
    }

    glDisable(GL_LIGHTING);
    glColor3d(color[0], color[1], color[2]);
    glLineWidth(2.0f);

    const unsigned int rows = list->get_value_count();
    for (unsigned int r = 0; r < rows; r++) {
        const long face = list->get_long(r);
        if (face < 0 || (unsigned long) face >= face_sizes.get_count()) continue;
        const unsigned int sides = face_sizes[face];
        const unsigned int base = starts[face];
        glBegin(GL_LINE_LOOP);
        for (unsigned int s = 0; s < sides; s++) {
            const GMathVec3d& p = points[face_indices[base + s]];
            glVertex3d(p[0], p[1], p[2]);
        }
        glEnd();
    }
    glLineWidth(1.0f);
}
