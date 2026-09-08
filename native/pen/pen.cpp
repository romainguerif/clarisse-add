// La plume : poser des points de courbe directement dans la vue.
//
// Sans ca, construire une courbe demande de creer les locators un par un, de
// creer le node, puis de brancher la liste a la main. C'est ce qui separe un
// node d'un outil.
//
// Le choix qui structure tout : les points de controle restent des locators
// natifs. L'outil n'invente pas sa propre representation, il pose des objets de
// scene ordinaires. Consequence, tout ce que Clarisse sait deja faire sur un
// locator marche sur nos points -- manipulateurs, snap, animation, contraintes,
// copier-coller, references de contexte -- et l'outil n'a a s'occuper que du
// geste.
//
// La deprojection est ecrite ici plutot que prise dans ix_gizmo : les helpers
// statiques de GizmoObject conviendraient, mais leur en-tete manque de
// l'arborescence reconstruite et il faudrait redeclarer des signatures a
// l'aveugle. GMathViewPoint, lui, est autonome et entierement inline ; son
// project_point donne la projection, et on l'inverse a la main pour le rayon.

// windows.h avant GL/gl.h : l'en-tete OpenGL de Microsoft ne declare ni
// WINGDIAPI ni APIENTRY, il les attend. Les deux gardes evitent au passage que
// windows.h ne redefinisse min et max sous le nez du SDK.
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

#include <ctx_tool.h>
#include <ctx_draw.h>
#include <gui_widget.h>
#include <gui_global.h>

// Le contexte GL est celui de l'hote ; pour du trace immediat dans draw_3d,
// l'en-tete du systeme suffit -- glew.h n'est requis que par gui_gl_widget.h,
// qui manque de l'arborescence reconstruite et dont on n'a pas besoin.
#include <GL/gl.h>

#include <gmath_vec3.h>
#include <gmath_matrix4x4.h>
#include <gmath_view_point.h>

#include <core_array.h>
#include <core_vector.h>
#include <core_string.h>

#include <cstdio>

#include <pen.cma>

namespace {

enum { PLANE_GROUND = 0, PLANE_FRONT = 1, PLANE_SIDE = 2, PLANE_VIEW = 3 };

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

// OfContext::ensure_unique_name est prive, et le contexte n'expose pas de
// recherche par nom. On laisse donc add_object arbitrer : il refuse un nom deja
// pris, et on incremente jusqu'a ce qu'il accepte.
OfObject *
add_unique(OfContext& context, const char *base, const char *class_name)
{
    CoreString name(base);
    for (unsigned int i = 1; i < 100000u; i++) {
        OfObject *created = context.add_object(name, class_name);
        if (created != 0) return created;
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "%s%u", base, i);
        name = CoreString(buffer);
    }
    return 0;
}

} // namespace

class PenModule : public ModuleTool {
public:
    PenModule()
        : ModuleTool(), m_hovered(-1), m_dragging(-1), m_hover_x(0), m_hover_y(0) {}

    // --- geometrie de la vue ------------------------------------------------

    // Le rayon qui part de l'oeil et passe par un pixel. On inverse
    // project_point : celui-ci divise les coordonnees locales par les facteurs
    // de zoom, donc on multiplie.
    static bool
    build_ray(const CtxTool& ctx, const int& px, const int& py,
              GMathVec3d& origin, GMathVec3d& direction)
    {
        const GMathViewPoint *view = ctx.gl.view_point;
        if (view == 0 || ctx.gl.width <= 0 || ctx.gl.height <= 0) return false;

        // Pixel -> coordonnees camera normalisees. L'axe vertical de l'ecran
        // descend, celui de la camera monte.
        const double nx = 2.0 * (double(px) / double(ctx.gl.width)) - 1.0;
        const double ny = 1.0 - 2.0 * (double(py) / double(ctx.gl.height));

        // A une unite devant, les facteurs de zoom donnent la demi-largeur et
        // la demi-hauteur du plan image.
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
            origin = vadd(origin, vadd(vscale(x_axis, lx), vscale(y_axis, ly)));
            direction = vscale(z_axis, -1.0);
        } else {
            // La camera regarde vers -Z.
            direction = vadd(vadd(vscale(x_axis, lx), vscale(y_axis, ly)),
                             vscale(z_axis, -1.0));
        }

        const double length = direction.get_length();
        if (length < 1e-12) return false;
        direction = vscale(direction, 1.0 / length);
        return true;
    }

    // Un point 3D vers un pixel, pour savoir quelle poignee est sous la souris.
    static bool
    project(const CtxTool& ctx, const GMathVec3d& world, double& px, double& py)
    {
        const GMathViewPoint *view = ctx.gl.view_point;
        if (view == 0 || ctx.gl.width <= 0 || ctx.gl.height <= 0) return false;

        GMathMatrix4x4d inverse;
        view->get_matrix().get_inverse(inverse);

        GMathVec3d camera;
        view->project_point(inverse, world, camera);

        // Derriere l'oeil : pas de poignee a afficher.
        if (camera[2] > -1e-9) return false;

        px = (camera[0] * 0.5 + 0.5) * double(ctx.gl.width);
        py = (0.5 - camera[1] * 0.5) * double(ctx.gl.height);
        return true;
    }

    // L'intersection du rayon avec le plan de construction choisi.
    bool
    pick_on_plane(const CtxTool& ctx, const int& px, const int& py,
                  GMathVec3d& hit) const
    {
        GMathVec3d origin, direction;
        if (!build_ray(ctx, px, py, origin, direction)) return false;

        OfObject *object = get_object();
        if (object == 0) return false;

        const long which = object->get_attribute("plane")->get_long();
        const double offset = object->get_attribute("plane_offset")->get_double();

        GMathVec3d normal;
        switch (which) {
            case PLANE_FRONT: normal = GMathVec3d(0.0, 0.0, 1.0); break;
            case PLANE_SIDE:  normal = GMathVec3d(1.0, 0.0, 0.0); break;
            case PLANE_VIEW:
                if (ctx.gl.view_point == 0) return false;
                normal = ctx.gl.view_point->get_z_axis();
                break;
            default:          normal = GMathVec3d(0.0, 1.0, 0.0); break;
        }

        const double denominator = vdot(direction, normal);
        if (denominator > -1e-9 && denominator < 1e-9) return false;

        const double t = (offset - vdot(origin, normal)) / denominator;
        if (t <= 0.0) return false;

        hit = vadd(origin, vscale(direction, t));
        return true;
    }

    // --- la courbe visee ----------------------------------------------------

    OfObject *
    get_target() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;
        OfAttr *attr = object->get_attribute("target");
        if (attr == 0) return 0;
        OfObject *target = attr->get_object();
        if (target == 0) return 0;
        // Un node sans points de controle n'est pas une courbe editable.
        return (target->get_attribute("control_points") != 0) ? target : 0;
    }

    // Les positions monde des points de controle actuels.
    void
    read_points(CoreVector<GMathVec3d>& points, CoreVector<OfObject *>& items) const
    {
        OfObject *target = get_target();
        if (target == 0) return;
        OfAttr *attr = target->get_attribute("control_points");
        if (attr == 0) return;

        const unsigned int count = attr->get_value_count();
        for (unsigned int i = 0; i < count; i++) {
            OfObject *item = attr->get_object(i);
            if (item == 0) continue;
            ModuleSceneItem *module = item->get_module<ModuleSceneItem>();
            if (module == 0) continue;
            GMathVec3d position;
            module->get_global_position(position);
            points.add(position);
            items.add(item);
        }
    }

    // Cree une courbe quand il n'y en a pas encore : un tube, et l'outil le
    // vise aussitot. C'est ce qui fait qu'un premier clic sur une scene vide
    // produit quelque chose.
    OfObject *
    create_target()
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        OfContext& context = object->get_context();
        const bool with_tube = object->get_attribute("create_tube")->get_bool();
        OfObject *target = add_unique(
            context, with_tube ? "curve" : "curve_points",
            with_tube ? "GeometryTube" : "GeometryCurvePoints");
        if (target == 0) return 0;

        object->get_attribute("target")->set_object(target);
        return target;
    }

    // Pose un locator a une position et l'accroche a la courbe.
    void
    append_point(OfObject& target, const GMathVec3d& position)
    {
        OfContext& context = target.get_context();
        OfObject *locator = add_unique(context, "point", "Locator");
        if (locator == 0) return;

        OfAttr *translate = locator->get_attribute("translate");
        if (translate != 0) {
            translate->set_double(position[0], 0);
            translate->set_double(position[1], 1);
            translate->set_double(position[2], 2);
        }

        OfAttr *attr = target.get_attribute("control_points");
        if (attr == 0) return;
        const unsigned int count = attr->get_value_count();
        attr->set_value_count(count + 1u);
        attr->set_object(locator, count);
    }

    void
    move_point(const int& index, const GMathVec3d& position)
    {
        CoreVector<GMathVec3d> points;
        CoreVector<OfObject *> items;
        read_points(points, items);
        if (index < 0 || (unsigned int) index >= items.get_count()) return;

        OfAttr *translate = items[index]->get_attribute("translate");
        if (translate == 0) return;
        translate->set_double(position[0], 0);
        translate->set_double(position[1], 1);
        translate->set_double(position[2], 2);
    }

    // --- etat ---------------------------------------------------------------

    int m_hovered;    // poignee sous la souris, -1 si aucune
    int m_dragging;   // poignee en cours de deplacement, -1 si aucune
    int m_hover_x;
    int m_hover_y;
};

// ---------------------------------------------------------------------------

IX_BEGIN_DECLARE_MODULE_CALLBACKS(ToolCurvePen, ModuleToolCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
    static bool enter_tool(OfObject& object, CtxTool& ctx);
    static void leave_tool(OfObject& object, CtxTool& ctx);
    static int process_event(OfObject& object, CtxTool& ctx, const CtxToolEvent& evt);
    static void draw_tool(OfObject& object, CtxTool& ctx, CtxDraw& draw);
    static void draw_tool_3d(OfObject& object, CtxTool& ctx);
    static void on_vizroot_change(OfObject& object, const OfContext *vizroot);
IX_END_DECLARE_MODULE_CALLBACKS(ToolCurvePen)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(ToolCurvePen);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
    module_callbacks->cb_enter_tool = IX_MODULE_CLBK::enter_tool;
    module_callbacks->cb_leave_tool = IX_MODULE_CLBK::leave_tool;
    module_callbacks->cb_process_event = IX_MODULE_CLBK::process_event;
    module_callbacks->cb_draw_tool = IX_MODULE_CLBK::draw_tool;
    module_callbacks->cb_draw_tool_3d = IX_MODULE_CLBK::draw_tool_3d;
    // init_callbacks() propage les douze autres mais oublie celui-la : sans un
    // pointeur valide ici, on part sur un appel indirect nul.
    module_callbacks->cb_on_vizroot_change = IX_MODULE_CLBK::on_vizroot_change;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    PenModule *module = new PenModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}

void
IX_MODULE_CLBK::on_vizroot_change(OfObject& object, const OfContext *vizroot)
{
}

bool
IX_MODULE_CLBK::enter_tool(OfObject& object, CtxTool& ctx)
{
    // Le retour arme m_is_active. Renvoyer false ici, et l'outil ne recoit
    // plus jamais un seul evenement.
    if (ctx.widget.object != 0) {
        ctx.widget.object->set_mouse_cursor(Gui::MOUSE_CURSOR_CROSS);
    }
    return true;
}

void
IX_MODULE_CLBK::leave_tool(OfObject& object, CtxTool& ctx)
{
    PenModule *module = object.get_module<PenModule>();
    if (module != 0) {
        module->m_hovered = -1;
        module->m_dragging = -1;
    }
    ctx.feedback.is_view_point_locked = false;
}

int
IX_MODULE_CLBK::process_event(OfObject& object, CtxTool& ctx, const CtxToolEvent& evt)
{
    PenModule *module = object.get_module<PenModule>();
    if (module == 0) return 0;

    const int px = ctx.gl.x;
    const int py = ctx.gl.y;

    // Quelle poignee est sous la souris ? Le test se fait en pixels, pas en
    // unites monde : une poignee doit rester attrapable quel que soit le zoom.
    const long size = object.get_attribute("handle_size")->get_long();
    const double reach = double(size) + 3.0;

    CoreVector<GMathVec3d> points;
    CoreVector<OfObject *> items;
    module->read_points(points, items);

    if (evt.id == EVT_ID_MOUSE_MOVE || evt.id == EVT_ID_MOUSE_DOWN) {
        int found = -1;
        double best = reach * reach;
        for (unsigned int i = 0; i < points.get_count(); i++) {
            double sx, sy;
            if (!PenModule::project(ctx, points[i], sx, sy)) continue;
            const double dx = sx - double(px);
            const double dy = sy - double(py);
            const double squared = dx * dx + dy * dy;
            if (squared <= best) { best = squared; found = int(i); }
        }
        if (found != module->m_hovered) {
            module->m_hovered = found;
            ctx.feedback.redraw = true;
        }
        module->m_hover_x = px;
        module->m_hover_y = py;
    }

    if (evt.id == EVT_ID_MOUSE_DOWN && evt.mouse.is_left_button()) {
        if (module->m_hovered >= 0) {
            // Attraper une poignee existante. Verrouiller le point de vue,
            // sinon le drag ferait tourner la camera au lieu de bouger le point.
            module->m_dragging = module->m_hovered;
            ctx.feedback.is_view_point_locked = true;
            return 1;
        }

        GMathVec3d hit;
        if (!module->pick_on_plane(ctx, px, py, hit)) return 0;

        OfObject *target = module->get_target();
        if (target == 0) target = module->create_target();
        if (target == 0) return 0;

        module->append_point(*target, hit);
        ctx.feedback.redraw = true;
        return 1;
    }

    if (evt.id == EVT_ID_MOUSE_DRAG && module->m_dragging >= 0) {
        GMathVec3d hit;
        if (module->pick_on_plane(ctx, px, py, hit)) {
            module->move_point(module->m_dragging, hit);
            ctx.feedback.redraw = true;
        }
        return 1;
    }

    if (evt.id == EVT_ID_MOUSE_UP && module->m_dragging >= 0) {
        module->m_dragging = -1;
        ctx.feedback.is_view_point_locked = false;
        return 1;
    }

    if (evt.id == EVT_ID_KEY_DOWN) {
        if (evt.keyboard.key == Gui::KEY_ID_DELETE
            || evt.keyboard.key == Gui::KEY_ID_BACKSPACE) {
            OfObject *target = module->get_target();
            if (target == 0) return 0;
            OfAttr *attr = target->get_attribute("control_points");
            if (attr == 0) return 0;

            const unsigned int count = attr->get_value_count();
            if (count == 0u) return 0;

            // Sans poignee survolee, on retire le dernier point : c'est le
            // geste attendu quand on vient d'en poser un de trop.
            const unsigned int victim =
                (module->m_hovered >= 0 && (unsigned int) module->m_hovered < count)
                ? (unsigned int) module->m_hovered : count - 1u;

            for (unsigned int i = victim; i + 1u < count; i++) {
                attr->set_object(attr->get_object(i + 1u), i);
            }
            attr->set_value_count(count - 1u);
            module->m_hovered = -1;
            ctx.feedback.redraw = true;
            return 1;
        }
    }

    return 0;
}

void
IX_MODULE_CLBK::draw_tool(OfObject& object, CtxTool& ctx, CtxDraw& draw)
{
    PenModule *module = object.get_module<PenModule>();
    if (module == 0) return;

    CoreVector<GMathVec3d> points;
    CoreVector<OfObject *> items;
    module->read_points(points, items);

    const long size = object.get_attribute("handle_size")->get_long();

    for (unsigned int i = 0; i < points.get_count(); i++) {
        double sx, sy;
        if (!PenModule::project(ctx, points[i], sx, sy)) continue;

        const bool active = (int(i) == module->m_hovered
                             || int(i) == module->m_dragging);
        if (active) {
            draw.fill_rect(int(sx) - size, int(sy) - size,
                            size * 2, size * 2, 1.0f, 0.8f, 0.1f, 0.9f);
        } else {
            draw.rect(int(sx) - size, int(sy) - size, size * 2, size * 2,
                       1.0f, 1.0f, 1.0f);
        }
    }
}

void
IX_MODULE_CLBK::draw_tool_3d(OfObject& object, CtxTool& ctx)
{
    PenModule *module = object.get_module<PenModule>();
    if (module == 0) return;

    CoreVector<GMathVec3d> points;
    CoreVector<OfObject *> items;
    module->read_points(points, items);
    if (points.get_count() < 2u) return;

    // La polyligne des points de controle, en coordonnees monde. Elle n'est pas
    // la courbe finale -- celle-la, c'est la geometrie qui la montre -- mais
    // elle dit dans quel ordre les points s'enchainent, ce qui est justement ce
    // qu'on ne voit pas autrement.
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.8f, 0.1f);
    glBegin(GL_LINE_STRIP);
    for (unsigned int i = 0; i < points.get_count(); i++) {
        glVertex3d(points[i][0], points[i][1], points[i][2]);
    }
    glEnd();
}
