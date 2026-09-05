// Camera Bokeh -- la profondeur de champ calculee par l'optique, pas apres coup.
//
// C'est la difference de fond avec un filtre d'image. Un flou de mise au point
// applique sur une image finie ne peut pas savoir ce qu'il y a derriere un
// objet net : il en etale les bords sur le fond, ou laisse une aureole. Une
// camera qui echantillonne son diaphragme, elle, lance de vrais rayons depuis
// de vrais points de la lentille. L'occlusion est juste par construction, il
// n'y a aucune carte de profondeur a fournir, et le flou se compose
// correctement avec le flou de bouge, les volumes et les transparences.
//
// -- Le point d'entree -------------------------------------------------------
//
// Isotropix a laisse une porte ouverte. RayGeneratorCameraPerspective expose
// une methode publique :
//
//     typedef void (*GetLensSampleCallback)(const RayGeneratorCameraPerspective&,
//                                           const ImageSample<double>&, GMathVec2d&);
//     void set_lens_sample_callback(GetLensSampleCallback);
//
// Ce callback rend deux nombres dans [0,1]. L'aval les transforme en point sur
// la lentille par la formule classique du disque uniforme (lu dans
// ray_generator_camera_perspective_impl.h:571-573) :
//
//     lens_radius = focal_length / (2 * f_stop)
//     r = lens_radius * sqrt(u0)
//     t = u1 * 2 pi
//
// Cette transformation est inversible. Pour obtenir n'importe quelle forme
// d'ouverture, il suffit donc de tirer un point (x, y) dans la forme voulue et
// de rendre (rho^2, theta / 2pi). C'est tout le principe de ce fichier.
//
// -- Echantillonnage ---------------------------------------------------------
//
// Une forme non circulaire ne s'echantillonne pas en tirant theta uniformement
// et en mettant a l'echelle le rayon : les secteurs larges recevraient autant
// d'echantillons que les etroits, sur plus d'aire. On construit donc une table
// de repartition en angle, ponderee par l'aire du secteur (proportionnelle au
// carre du rayon de la frontiere), et on l'inverse. Exact, et calcule une
// seule fois par rendu.
//
// L'aberration spherique passe par une seconde table, radiale celle-la : elle
// deplace l'energie vers le bord ou vers le centre sans en ajouter, la
// ponderation w(rho) = 1 + s (rho^p - 2/(p+2)) etant de moyenne nulle par
// construction sur le disque unite.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>

#include <module_camera.h>
#include <module_scene_item.h>
#include <ctx_eval.h>
#include <ray_generator_camera_perspective.h>
#include <ray_generator_camera_perspective_enums.h>
#include <image_sample.h>
#include <core_log.h>

#include <math.h>

#include "aperture.h"
#include <bokeh_camera.cma>

using clarisse_add::Aperture;
using clarisse_add::aperture_init;
using clarisse_add::aperture_edge;
using clarisse_add::aperture_edge_at;

namespace {

const double TWO_PI = 6.28318530717958647692;
const int ANGLE_TABLE = 512;     // repartition en angle
const int RADIAL_TABLE = 256;    // repartition radiale (aberration spherique)
const double SPHERICAL_POWER = 4.0;

// Suite de Halton par inversion radicale. On la recalcule plutot que d'appeler
// celle de Clarisse : deux lignes, aucun symbole a lier, et le meme resultat.
inline double
radical_inverse(unsigned int index, const unsigned int& base)
{
    double result = 0.0;
    double factor = 1.0 / base;
    while (index > 0) {
        result += (index % base) * factor;
        index /= base;
        factor /= base;
    }
    return result;
}

// -- le generateur de rayons --------------------------------------------------
//
// On derive plutot que de detourner un generateur standard : les reglages du
// bokeh vivent ainsi dans l'instance, et deux cameras bokeh dans la meme scene
// gardent chacune les siens. Le callback de lentille etant une fonction libre,
// c'est le seul moyen d'atteindre les reglages -- il ne recoit que le
// generateur.
class BokehRayGenerator : public RayGeneratorCameraPerspective {
public:
    BokehRayGenerator(CameraPerspectiveEnums::FitMode fit)
        : RayGeneratorCameraPerspective(fit)
        , vignetting(0.0), swirl(0.0), anamorphism(0.0), shaped(false)
    {
    }

    // Prepare les tables pour une forme donnee. Appelee une fois par rendu.
    void prepare(const int& blades, const double& rotation, const double& curvature,
                 const double& spherical, const double& optical_vignetting,
                 const double& aperture_swirl, const double& squeeze,
                 const double& softness)
    {
        vignetting = optical_vignetting;
        swirl = aperture_swirl;
        anamorphism = squeeze;
        aperture_init(shape, blades, rotation, curvature);
        shaped = (blades >= 3) || (spherical != 0.0) || (vignetting > 0.0)
                 || (anamorphism != 0.0) || (swirl != 0.0) || (softness > 0.0);

        build_angle_table();
        build_radial_table(spherical, softness);
    }

    Aperture shape;
    double   vignetting;
    double   swirl;
    double   anamorphism;
    bool     shaped;

    // angle[i] : l'angle dont la fraction cumulee d'aire vaut i / ANGLE_TABLE.
    double angle[ANGLE_TABLE + 1];
    // radial[i] : le rayon normalise dont la fraction cumulee d'energie vaut
    // i / RADIAL_TABLE.
    double radial[RADIAL_TABLE + 1];

private:
    void build_angle_table()
    {
        if (shape.circular) {
            for (int i = 0; i <= ANGLE_TABLE; ++i)
                angle[i] = TWO_PI * i / ANGLE_TABLE;
            return;
        }

        // Aire cumulee : dA = r(theta)^2 / 2 dtheta.
        const int fine = ANGLE_TABLE * 4;
        static double cumulative[ANGLE_TABLE * 4 + 1];
        cumulative[0] = 0.0;
        for (int i = 1; i <= fine; ++i) {
            const double t = TWO_PI * (i - 0.5) / fine;
            const double r = aperture_edge_at(shape, t);
            cumulative[i] = cumulative[i - 1] + r * r;
        }
        const double total = cumulative[fine];
        if (total <= 0.0) {
            for (int i = 0; i <= ANGLE_TABLE; ++i)
                angle[i] = TWO_PI * i / ANGLE_TABLE;
            return;
        }

        int cursor = 0;
        for (int i = 0; i <= ANGLE_TABLE; ++i) {
            const double target = total * i / ANGLE_TABLE;
            while (cursor < fine && cumulative[cursor + 1] < target) ++cursor;
            const double lo = cumulative[cursor];
            const double hi = cumulative[cursor < fine ? cursor + 1 : fine];
            const double f = (hi > lo) ? (target - lo) / (hi - lo) : 0.0;
            angle[i] = TWO_PI * (cursor + f) / fine;
        }
    }

    // La douceur du bord se replie ici plutot que de s'appliquer apres coup.
    // Une ouverture a bord franc a une densite radiale constante jusqu'au
    // bord ; l'adoucir, c'est faire decroitre cette densite sur la derniere
    // fraction du rayon. En passant par la table de repartition, la
    // distribution reste exacte et le tirage reste uniforme en aire -- ce qui
    // ne serait pas le cas si on se contentait de brouiller le rayon apres.
    void build_radial_table(const double& spherical, const double& softness)
    {
        if (spherical == 0.0 && softness <= 0.0) {
            // Disque uniforme : rho = sqrt(u), la formule classique.
            for (int i = 0; i <= RADIAL_TABLE; ++i)
                radial[i] = sqrt((double)i / RADIAL_TABLE);
            return;
        }

        const int fine = RADIAL_TABLE * 8;
        static double cumulative[RADIAL_TABLE * 8 + 1];
        cumulative[0] = 0.0;
        for (int i = 1; i <= fine; ++i) {
            const double rho = (i - 0.5) / fine;
            const double rho_p = rho * rho * rho * rho;   // p = 4
            double w = 1.0 + spherical * (rho_p - 2.0 / (SPHERICAL_POWER + 2.0));
            if (w < 0.0) w = 0.0;
            if (softness > 0.0) {
                // Transition lisse sur la derniere fraction `softness` du
                // rayon : 1 jusqu'a (1 - softness), 0 au bord.
                const double start = 1.0 - softness;
                if (rho > start) {
                    double t = (1.0 - rho) / softness;
                    if (t < 0.0) t = 0.0;
                    w *= t * t * (3.0 - 2.0 * t);   // smoothstep
                }
            }
            cumulative[i] = cumulative[i - 1] + w * rho;  // dA = w * 2 rho drho
        }
        const double total = cumulative[fine];
        if (total <= 0.0) {
            for (int i = 0; i <= RADIAL_TABLE; ++i)
                radial[i] = sqrt((double)i / RADIAL_TABLE);
            return;
        }

        int cursor = 0;
        for (int i = 0; i <= RADIAL_TABLE; ++i) {
            const double target = total * i / RADIAL_TABLE;
            while (cursor < fine && cumulative[cursor + 1] < target) ++cursor;
            const double lo = cumulative[cursor];
            const double hi = cumulative[cursor < fine ? cursor + 1 : fine];
            const double f = (hi > lo) ? (target - lo) / (hi - lo) : 0.0;
            radial[i] = (cursor + f) / fine;
        }
    }
};

inline double
lookup(const double *table, const int& size, const double& u)
{
    double x = u * size;
    if (x < 0.0) x = 0.0;
    if (x > size) x = size;
    const int i = (int) x;
    if (i >= size) return table[size];
    const double f = x - i;
    return table[i] * (1.0 - f) + table[i + 1] * f;
}

// Le callback de lentille : c'est ici que se decide la forme du bokeh.
void
bokeh_lens_sample(const RayGeneratorCameraPerspective& generator,
                  const ImageSample<double>& sample, GMathVec2d& out)
{
    const BokehRayGenerator& gen = (const BokehRayGenerator&) generator;

    const unsigned int seed = sample.seed + sample.index;
    double u0 = radical_inverse(seed + 1, 2);
    double u1 = radical_inverse(seed + 1, 3);

    if (!gen.shaped) { out[0] = u0; out[1] = u1; return; }

    // Position dans le cadre, ramenee a [-1, 1] depuis le centre. C'est elle
    // qui pilote le vignettage optique et le tourbillon.
    const double gx = (sample.img_uv[0] - 0.5) * 2.0;
    const double gy = (sample.img_uv[1] - 0.5) * 2.0;
    const double frame_r = sqrt(gx * gx + gy * gy);

    // Angle tire selon l'aire des secteurs, puis rayon selon la repartition
    // radiale. Le tourbillon fait pivoter l'ouverture avec l'eloignement au
    // centre : c'est le bokeh des Petzval, ou les boules tournent autour du
    // cadre.
    double theta = lookup(gen.angle, ANGLE_TABLE, u1);
    if (gen.swirl != 0.0) theta += gen.swirl * frame_r * TWO_PI * 0.25;

    const double edge = aperture_edge_at(gen.shape, theta);
    double rho = lookup(gen.radial, RADIAL_TABLE, u0) * edge;

    double x = rho * cos(theta);
    double y = rho * sin(theta);

    // Anamorphisme : on comprime un axe. La forme reste dans le disque unite,
    // donc l'encodage de retour reste valide.
    if (gen.anamorphism > 0.0) x /= (1.0 + gen.anamorphism);
    else if (gen.anamorphism < 0.0) y /= (1.0 - gen.anamorphism);

    // Vignettage optique : le barillet rogne le faisceau hors axe. La pupille
    // devient l'intersection de deux disques decales -- l'amande dite
    // oeil-de-chat, dont le grand axe est tangentiel. On rejette les points
    // hors du disque de troncature, en changeant de dimension de Halton a
    // chaque essai.
    //
    // Reserve assumee : rejeter puis retirer conserve la FORME de l'amande
    // mais pas l'assombrissement des coins, puisqu'on rend toujours un
    // echantillon. Le callback ne peut pas supprimer un rayon -- seul le
    // generateur le pourrait. Le vignettage d'exposition se traite donc
    // separement, ce que fait de toute facon la plupart des compositeurs.
    if (gen.vignetting > 0.0 && frame_r > 1e-9) {
        const double offset = gen.vignetting * frame_r;
        const double sx = -gx / frame_r * offset;
        const double sy = -gy / frame_r * offset;
        for (int attempt = 0; attempt < 6; ++attempt) {
            const double dx = x - sx, dy = y - sy;
            if (dx * dx + dy * dy <= 1.0) break;
            const unsigned int base_a = (attempt * 2 + 5);
            const unsigned int base_b = (attempt * 2 + 7);
            u0 = radical_inverse(seed + attempt + 2, base_a % 23 + 2);
            u1 = radical_inverse(seed + attempt + 2, base_b % 29 + 2);
            theta = lookup(gen.angle, ANGLE_TABLE, u1);
            if (gen.swirl != 0.0) theta += gen.swirl * frame_r * TWO_PI * 0.25;
            rho = lookup(gen.radial, RADIAL_TABLE, u0) * aperture_edge_at(gen.shape, theta);
            x = rho * cos(theta);
            y = rho * sin(theta);
            if (gen.anamorphism > 0.0) x /= (1.0 + gen.anamorphism);
            else if (gen.anamorphism < 0.0) y /= (1.0 - gen.anamorphism);
        }
    }

    // Retour dans l'espace attendu par l'aval : r = R sqrt(u0), t = 2 pi u1.
    const double r = sqrt(x * x + y * y);
    double t = atan2(y, x);
    if (t < 0.0) t += TWO_PI;
    out[0] = r * r;
    out[1] = t / TWO_PI;
}

} // namespace

// -- declaration du module ----------------------------------------------------

IX_BEGIN_DECLARE_MODULE_CALLBACKS(CameraBokeh, ModuleCameraCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
    static RayGeneratorCamera *create_ray_generator(OfObject& object, const CtxMotionBlur *motion_blur);
    static void get_fovs(OfObject& object, const double& aspect_ratio, double& h_fov, double& v_fov);
    static void get_offsets(OfObject& object, double& h_offset, double& v_offset);
    static void get_config(OfObject& object, CameraConfig& config);
    static void module_constructor(OfObject& object, OfModule *module);
    static void on_attribute_change(OfObject& object, const OfAttr& attr,
                                    int& dirtiness, const int& dirtiness_flags);
IX_END_DECLARE_MODULE_CALLBACKS(CameraBokeh)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(CameraBokeh);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
    module_callbacks->cb_create_ray_generator = IX_MODULE_CLBK::create_ray_generator;
    module_callbacks->cb_get_fovs = IX_MODULE_CLBK::get_fovs;
    module_callbacks->cb_get_offsets = IX_MODULE_CLBK::get_offsets;
    module_callbacks->cb_get_config = IX_MODULE_CLBK::get_config;
    module_callbacks->cb_module_constructor = IX_MODULE_CLBK::module_constructor;
    module_callbacks->cb_on_attribute_change = IX_MODULE_CLBK::on_attribute_change;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    // set_object est indispensable : OfModule::is_protected() et
    // get_object_name() dereferencent m_object sans le tester.
    ModuleCamera *module = new ModuleCamera();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}

namespace {

// Lecture directe des attributs : create_ray_generator ne recoit pas de
// CtxEval, donc pas de Cma synchronise. On passe par l'OfObject.
inline double attr_double(OfObject& o, const char *name, const double& fallback = 0.0)
{
    const OfAttr *a = o.get_attribute(name);
    return a ? a->get_double() : fallback;
}

inline long attr_long(OfObject& o, const char *name, const long& fallback = 0)
{
    const OfAttr *a = o.get_attribute(name);
    return a ? a->get_long() : fallback;
}

inline bool attr_bool(OfObject& o, const char *name, const bool& fallback = false)
{
    const OfAttr *a = o.get_attribute(name);
    return a ? a->get_bool() : fallback;
}

inline double attr_double_at(OfObject& o, const char *name, const unsigned int& index,
                             const double& fallback = 0.0)
{
    const OfAttr *a = o.get_attribute(name);
    return a ? a->get_double(index) : fallback;
}

} // namespace

RayGeneratorCamera *
IX_MODULE_CLBK::create_ray_generator(OfObject& object, const CtxMotionBlur *motion_blur)
{
    const CameraPerspectiveEnums::FitMode fit =
        (CameraPerspectiveEnums::FitMode) attr_long(object, "fit_mode", 0);

    BokehRayGenerator *generator = new BokehRayGenerator(fit);

    const double focal = attr_double(object, "focal_length", 0.05);
    const double film_w = attr_double(object, "horizontal_aperture", 0.036);
    const double film_h = attr_double(object, "vertical_aperture", 0.024);

    // La profondeur de champ n'existe que si elle est demandee. A f_stop nul,
    // l'aval retombe sur une camera stenope et ne consulte jamais la lentille.
    double f_stop = 0.0;
    double focus = 0.0;
    if (attr_bool(object, "enable_dof")) {
        f_stop = attr_double(object, "f_stop", 5.6);
        focus = attr_double(object, "focus_distance", 5.0);
    }

    generator->set_shape(film_w, film_h, focal, f_stop, focus);
    generator->set_offset(attr_double_at(object, "film_offset", 0),
                          attr_double_at(object, "film_offset", 1));

    if (attr_bool(object, "enable_bokeh", true)) {
        generator->prepare((int) attr_long(object, "blades", 0),
                           attr_double(object, "blade_rotation"),
                           attr_double(object, "blade_curvature"),
                           attr_double(object, "spherical_aberration"),
                           attr_double(object, "optical_vignetting"),
                           attr_double(object, "aperture_swirl"),
                           attr_double(object, "anamorphism"),
                           attr_double(object, "aperture_softness"));
        generator->set_lens_sample_callback(bokeh_lens_sample);
    }

    return generator;
}

void
IX_MODULE_CLBK::get_fovs(OfObject& object, const double& aspect_ratio,
                         double& h_fov, double& v_fov)
{
    h_fov = attr_double(object, "horizontal_field_of_view", 39.6);
    v_fov = attr_double(object, "vertical_field_of_view", 27.0);
}

void
IX_MODULE_CLBK::get_offsets(OfObject& object, double& h_offset, double& v_offset)
{
    h_offset = attr_double_at(object, "film_offset", 0);
    v_offset = attr_double_at(object, "film_offset", 1);
}

void
IX_MODULE_CLBK::get_config(OfObject& object, CameraConfig& config)
{
    config.focal_length = attr_double(object, "focal_length", 0.05);
    config.h_aperture = attr_double(object, "horizontal_aperture", 0.036);
    config.v_aperture = attr_double(object, "vertical_aperture", 0.024);
    config.film_offset_x = attr_double_at(object, "film_offset", 0);
    config.film_offset_y = attr_double_at(object, "film_offset", 1);
    config.lens_ratio = attr_double(object, "lens_ratio", 1.0);
    config.f_stop = attr_bool(object, "enable_dof") ? attr_double(object, "f_stop", 5.6) : 0.0;
    config.focus_distance = attr_double(object, "focus_distance", 5.0);
    for (int i = 0; i < 4; ++i)
        config.overscan[i] = attr_double_at(object, "overscan", i, i == 0 ? 1.0 : 0.0);
}

// -- verrouillage des reglages de mise au point -------------------------------
//
// Le CID d'origine declare f_stop et focus_distance en read_only. Ce n'est pas
// une interdiction definitive : Clarisse leve le verrou quand la profondeur de
// champ est activee -- "Read-only state isn't necessarily permanent", dit la
// documentation des attributs, qui donne le meme exemple sur la subdivision
// des Polymesh.
//
// Cette levee est le travail du module, pas du CID. Sans elle les deux
// reglages restent grises, gardent leur valeur par defaut, et la mise au point
// ne repond a rien -- exactement le symptome qu'on a observe.
namespace {

void update_dof_lock(OfObject& object)
{
    const bool enabled = attr_bool(object, "enable_dof");
    static const char *locked[] = {"f_stop", "focus_distance", "focus_object"};
    for (int i = 0; i < 3; ++i) {
        OfAttr *attr = object.get_attribute(locked[i]);
        if (attr != 0) attr->set_read_only(!enabled);
    }
}

} // namespace

void
IX_MODULE_CLBK::module_constructor(OfObject& object, OfModule *module)
{
    update_dof_lock(object);
}

void
IX_MODULE_CLBK::on_attribute_change(OfObject& object, const OfAttr& attr,
                                    int& dirtiness, const int& dirtiness_flags)
{
    if (attr.get_name() == "enable_dof") update_dof_lock(object);
}
