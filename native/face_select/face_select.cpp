// L'outil de selection de faces : designer a la souris ce qu'aucune regle ne
// decrira.
//
// Le node GeometrySelect sait fabriquer une selection par regle -- cette
// texture au-dessus de ce seuil, ces faces qui regardent en haut. C'est ce qui
// tient dans un graphe procedural. Mais il y a des cas ou l'artiste sait
// exactement quelles faces il veut, et ou aucune regle ne les decrira : c'est
// meme le cas courant. Cet outil-la remplit sa table d'indices.
//
// Quatre choses expliquent sa forme, et la premiere est celle qui manquait a la
// premiere version.
//
// **Le pre-surlignage.** La face sous le curseur s'allume avant qu'on clique.
// C'est le retour visuel qui rend une selection utilisable : sans lui on
// designe a l'aveugle, et on ne sait meme pas si l'outil est vivant. C'est la
// convention de Modo, et elle est la bonne -- on voit ce qu'on va prendre avant
// de le prendre.
//
// **Les faces choisies sont remplies, pas seulement cerclees**, et poussees
// vers l'oeil par un decalage de profondeur. Un contour seul se perd dans le
// maillage et clignote contre la surface qu'il epouse.
//
// **Les gestes sont ceux de tout le monde** : clic pour remplacer, Maj pour
// ajouter, Ctrl pour retirer, glisser pour peindre.
//
// **Il lance ses rayons lui-meme**, en force brute sur le maillage vise, plutot
// que d'interroger la structure d'acceleration de Clarisse : un clic est un
// rayon, et on ne depend d'aucune API dont on n'aurait pas verifie le
// comportement hors du rendu. Le maillage reste en cache entre deux evenements,
// sans quoi le survol relirait toute la geometrie a chaque pixel parcouru.

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
#include <core_string.h>
#include <core_vector.h>

#include <stdio.h>

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

// Le nom demande s'il est libre, sinon le meme suivi d'un nombre. `add_object`
// rend zero quand le nom est pris, et on incremente jusqu'a ce qu'il accepte.
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
    // Le determinant vaut moins le produit scalaire de la direction par la
    // normale : une face qui regarde la camera le rend positif. L'inverse,
    // ecrit d'abord, ne retenait que les faces tournant le dos -- donc la paroi
    // du fond sur un objet ferme, qui s'affiche ailleurs a l'ecran.
    front = det > 0.0;
    return true;
}

} // namespace

class FaceSelectModule : public ModuleTool {
public:
    FaceSelectModule()
        : ModuleTool(), m_source(0), m_hovered(-1),
          m_painting(false), m_removing(false) {}

    // Le maillage en cache. Le survol interroge la geometrie a chaque pixel
    // parcouru : la relire a chaque fois rendrait l'outil inutilisable des le
    // premier maillage serieux.
    OfObject *m_source;
    CoreVector<GMathVec3d> m_points;
    CoreArray<unsigned int> m_face_indices;
    CoreArray<unsigned int> m_face_sizes;
    CoreArray<unsigned int> m_face_starts;

    int m_hovered;
    bool m_painting;
    bool m_removing;

    // Le node de selection a remplir. Peut ne pas exister encore : l'outil sait
    // en creer un au premier clic, comme la plume cree sa courbe.
    OfObject *
    get_select_node() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;
        OfAttr *attr = object->get_attribute("target");
        if (attr == 0) return 0;
        OfObject *named = attr->get_object();
        if (named != 0 && named->get_attribute("index") != 0) return named;
        return 0;
    }

    // La geometrie contre laquelle on lance les rayons : soit l'entree du node
    // de selection, soit -- si l'artiste a pointe directement un maillage -- ce
    // maillage lui-meme.
    OfObject *
    get_mesh_source() const
    {
        OfObject *select = get_select_node();
        if (select != 0) {
            OfAttr *input = select->get_attribute("input_geometry");
            return (input != 0) ? input->get_object() : 0;
        }

        OfObject *object = get_object();
        if (object == 0) return 0;
        OfAttr *attr = object->get_attribute("target");
        if (attr == 0) return 0;
        OfObject *named = attr->get_object();
        if (named == 0) return 0;
        return (named->get_module<ModuleGeometry>() != 0) ? named : 0;
    }

    // Cree le node de selection manquant et l'insere derriere le maillage.
    // C'est ce qui permet de pointer une geometrie nue et de cliquer : demander
    // a l'artiste de monter le graphe d'abord serait une friction pour rien.
    OfObject *
    create_select_node(OfObject& mesh)
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        OfContext& context = mesh.get_context();
        OfObject *created = add_unique(context, "selection", "GeometrySelect");
        if (created == 0) return 0;

        OfAttr *input = created->get_attribute("input_geometry");
        if (input != 0) input->set_object(&mesh);

        // Sans ce mode, la table se remplirait sans que rien n'en sorte : le
        // node ignore sa liste par defaut, et l'artiste ne verrait aucun effet.
        OfAttr *mode = created->get_attribute("list_mode");
        if (mode != 0) mode->set_long(1);

        OfAttr *target = object->get_attribute("target");
        if (target != 0) target->set_object(created);
        return created;
    }

    // Relit le maillage si la source a change, ou si on le demande. Les debuts
    // de face sont calcules ici une bonne fois : sans eux, surligner la n-ieme
    // face demanderait de reparcourir la table d'indices depuis le debut.
    bool
    refresh(const bool& force)
    {
        OfObject *source = get_mesh_source();
        if (source == 0) {
            m_source = 0;
            m_points.remove_all();
            return false;
        }
        if (!force && source == m_source && m_points.get_count() > 0u) {
            return true;
        }

        m_source = source;
        m_points.remove_all();

        ModuleGeometry *module = source->get_module<ModuleGeometry>();
        if (module == 0) return false;
        const GeometryObject *geometry = module->get_geometry(false);
        if (geometry == 0) return false;
        const PolyMesh *mesh = CoreBaseObject::cast<PolyMesh>(geometry);
        if (mesh == 0) return false;
        const GeometryPointCloud *cloud = mesh->get_point_cloud();
        if (cloud == 0) return false;

        mesh->get_polygon_vertex_indices(m_face_indices);
        mesh->get_polygon_vertex_count(m_face_sizes);

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
            m_points.add(world);
        }

        m_face_starts.resize(m_face_sizes.get_count());
        unsigned int cursor = 0u;
        for (unsigned int f = 0; f < m_face_sizes.get_count(); f++) {
            m_face_starts[f] = cursor;
            cursor += m_face_sizes[f];
        }
        return m_points.get_count() > 0u;
    }

    static bool
    build_ray(const CtxTool& ctx, const int& px, const int& py,
              GMathVec3d& origin, GMathVec3d& direction)
    {
        const GMathViewPoint *view = ctx.gl.view_point;
        if (view == 0 || ctx.gl.width <= 0 || ctx.gl.height <= 0) return false;

        const double nx = 2.0 * (double(px) / double(ctx.gl.width)) - 1.0;
        const double ny = 1.0 - 2.0 * (double(py) / double(ctx.gl.height));

        // En perspective l'etendue se mesure a une unite devant l'oeil et le
        // rayon diverge ; en orthographique il ne diverge pas, et l'etendue
        // visible est celle prise a la distance de pivot. Mesurer a une unite
        // dans ce cas rapetissait la vue d'autant, et tout ce qui n'etait pas
        // au centre exact tombait bien trop pres du milieu.
        const double reference =
            view->is_orthographic() ? view->get_eye_distance() : 1.0;
        const double h = view->get_horizontal_zoom_factor(reference);
        const double v = view->get_vertical_zoom_factor(reference);
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

    // La face sous le curseur, ou -1.
    int
    pick_face(const CtxTool& ctx, const int& px, const int& py,
              const bool& backfaces) const
    {
        GMathVec3d origin, direction;
        if (!build_ray(ctx, px, py, origin, direction)) return -1;

        int best = -1;
        double nearest = 1e30;

        for (unsigned int f = 0; f < m_face_sizes.get_count(); f++) {
            const unsigned int sides = m_face_sizes[f];
            const unsigned int base = m_face_starts[f];
            if (sides < 3u) continue;

            // Un eventail depuis le premier sommet : ca suffit pour un test
            // d'intersection, meme sur une face gauche -- au pire on manque une
            // sliver, jamais une face franche.
            for (unsigned int s = 1; s + 1u < sides; s++) {
                double distance;
                bool front;
                if (!ray_triangle(origin, direction,
                                  m_points[m_face_indices[base]],
                                  m_points[m_face_indices[base + s]],
                                  m_points[m_face_indices[base + s + 1u]],
                                  distance, front)) {
                    continue;
                }
                if (!backfaces && !front) continue;
                if (distance < nearest) { nearest = distance; best = int(f); }
            }
        }
        return best;
    }

    static OfAttr *
    list_attr(OfObject& target)
    {
        OfAttr *attr = target.get_attribute("index");
        if (attr == 0) attr = target.get_attribute("faces");
        return attr;
    }

    static bool
    is_selected(OfObject& target, const unsigned int& face)
    {
        OfAttr *attr = list_attr(target);
        if (attr == 0) return false;
        const unsigned int count = attr->get_value_count();
        for (unsigned int i = 0; i < count; i++) {
            if (attr->get_long(i) == long(face)) return true;
        }
        return false;
    }

    static void
    clear_list(OfObject& target)
    {
        OfAttr *attr = list_attr(target);
        if (attr != 0) attr->set_value_count(0u);
    }

    static void
    set_face(OfObject& target, const unsigned int& face, const bool& remove)
    {
        OfAttr *attr = list_attr(target);
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

    // Le contour d'une face, tel quel. Sert au remplissage comme au trace : une
    // face de capitonnage peut avoir n'importe quel nombre de cotes.
    void
    emit_face(const unsigned int& face, const unsigned int& primitive) const
    {
        const unsigned int sides = m_face_sizes[face];
        const unsigned int base = m_face_starts[face];
        glBegin(primitive);
        for (unsigned int s = 0; s < sides; s++) {
            const GMathVec3d& p = m_points[m_face_indices[base + s]];
            glVertex3d(p[0], p[1], p[2]);
        }
        glEnd();
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

    const OfAttr *back = object.get_attribute("backfaces");
    const bool backfaces = (back != 0) && back->get_bool();

    // --- le survol ---------------------------------------------------------
    //
    // C'est ce qui manquait, et c'est lui qui rend l'outil utilisable : on voit
    // ce qu'on va prendre avant de le prendre. Il ne consomme pas l'evenement,
    // pour laisser la vue se comporter normalement.
    if (evt.id == EVT_ID_MOUSE_MOVE) {
        int face = -1;
        if (module->refresh(false)) {
            face = module->pick_face(ctx, ctx.gl.x, ctx.gl.y, backfaces);
        }
        if (face != module->m_hovered) {
            module->m_hovered = face;
            ctx.feedback.redraw = true;
        }
        return 0;
    }

    if (evt.id == EVT_ID_MOUSE_UP) {
        const bool was = module->m_painting;
        module->m_painting = false;
        ctx.feedback.is_view_point_locked = false;
        return was ? 1 : 0;
    }

    const bool is_down = (evt.id == EVT_ID_MOUSE_DOWN);
    const bool is_drag = (evt.id == EVT_ID_MOUSE_DRAG);
    if (!is_down && !is_drag) return 0;
    if (is_drag && !module->m_painting) return 0;
    if (is_down && !evt.mouse.is_left_button()) return 0;

    const OfAttr *paint_attr = object.get_attribute("paint");
    const bool paint = (paint_attr == 0) || paint_attr->get_bool();
    if (is_drag && !paint) return 0;

    OfObject *target = module->get_select_node();
    if (target == 0) {
        OfObject *mesh = module->get_mesh_source();
        if (mesh == 0) return 0;
        target = module->create_select_node(*mesh);
        if (target == 0) return 0;
    }

    if (is_down) {
        // Les gestes de tout le monde : Maj ajoute, Ctrl retire, un clic seul
        // remplace. Le reglage du node ne sert que de defaut.
        const OfAttr *mode = object.get_attribute("mode");
        module->m_removing = (mode != 0 && mode->get_long() == 1);

        if (evt.keyboard.is_modifier_ctrl()) {
            module->m_removing = true;
        } else if (evt.keyboard.is_modifier_shift()) {
            module->m_removing = false;
        } else if (!module->m_removing) {
            // Clic simple : on repart de zero, comme partout ailleurs.
            FaceSelectModule::clear_list(*target);
        }

        module->m_painting = true;
        ctx.feedback.is_view_point_locked = true;
        // La geometrie a pu bouger ou etre reconstruite depuis le survol : on
        // la relit avant d'agir, jamais avant de simplement survoler.
        module->refresh(true);
    }

    const int face = module->pick_face(ctx, ctx.gl.x, ctx.gl.y, backfaces);
    if (face < 0) {
        ctx.feedback.redraw = true;
        return 1;
    }

    FaceSelectModule::set_face(*target, (unsigned int) face,
                               module->m_removing);
    module->m_hovered = face;
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
    if (!module->refresh(false)) return;

    double color[3] = { 1.0, 0.55, 0.1 };
    const OfAttr *color_attr = object.get_attribute("highlight_color");
    if (color_attr != 0) {
        for (unsigned int i = 0; i < 3; i++) color[i] = color_attr->get_double(i);
    }

    // On sauve tout ce qu'on touche : laisser l'etat GL sale abimerait le reste
    // du dessin du viewport, et le symptome serait incomprehensible.
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_POLYGON_BIT
                 | GL_LINE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Le decalage de profondeur pousse le surlignage vers l'oeil. Sans lui il
    // se battrait avec la surface qu'il recouvre et clignoterait.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);

    const unsigned int faces = module->m_face_sizes.get_count();
    OfObject *target = module->get_select_node();
    OfAttr *list = (target != 0) ? FaceSelectModule::list_attr(*target) : 0;

    if (list != 0) {
        const unsigned int rows = list->get_value_count();

        glColor4d(color[0], color[1], color[2], 0.45);
        for (unsigned int r = 0; r < rows; r++) {
            const long face = list->get_long(r);
            if (face < 0 || (unsigned long) face >= faces) continue;
            module->emit_face((unsigned int) face, GL_POLYGON);
        }

        glLineWidth(2.0f);
        glColor4d(color[0], color[1], color[2], 1.0);
        for (unsigned int r = 0; r < rows; r++) {
            const long face = list->get_long(r);
            if (face < 0 || (unsigned long) face >= faces) continue;
            module->emit_face((unsigned int) face, GL_LINE_LOOP);
        }
    }

    // La face survolee, plus claire et plus transparente : on doit la
    // distinguer d'une face deja prise sans qu'elle lui vole la vedette.
    if (module->m_hovered >= 0 && (unsigned int) module->m_hovered < faces) {
        const bool already = (target != 0)
            && FaceSelectModule::is_selected(*target,
                                             (unsigned int) module->m_hovered);
        glColor4d(1.0, already ? 1.0 : 0.95, already ? 1.0 : 0.7, 0.30);
        module->emit_face((unsigned int) module->m_hovered, GL_POLYGON);

        glLineWidth(1.5f);
        glColor4d(1.0, 1.0, 1.0, 0.9);
        module->emit_face((unsigned int) module->m_hovered, GL_LINE_LOOP);
    }

    glPopAttrib();
}
