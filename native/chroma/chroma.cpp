// Aberration chromatique -- trois phenomenes, trois blocs.
//
// Le mot recouvre trois choses que la plupart des outils fondent en un seul
// curseur, ce qui les rend impossibles a regler :
//
//   - **laterale (transverse)** : le grandissement differe selon la couleur.
//     C'est un decalage GEOMETRIQUE, nul au centre optique et croissant vers
//     les bords. C'est elle qu'on mesure sur un banc : 0,04 a 0,15 % de la
//     demi-diagonale sur des optiques reelles, soit un demi-pixel en HD.
//   - **longitudinale (axiale)** : les couleurs ne convergent pas a la meme
//     distance. C'est un FLOU differentiel, uniforme sur tout le cadre, y
//     compris au centre. Sans profondeur on ne peut que l'approcher.
//   - **la frange violette** : ce n'est pas de l'aberration chromatique. C'est
//     du blooming et de la diffusion dans le verre, autour des hautes
//     lumieres. D'ou un bloc additif et seuille, separe des deux autres.
//
// -- Pourquoi ce filtre existe -----------------------------------------------
//
// La camera Bokeh de ClarisseAdd calcule une vraie profondeur de champ en
// echantillonnant son diaphragme, mais elle ne peut pas faire d'aberration
// chromatique : il faudrait que le rouge, le vert et le bleu partent de points
// de lentille differents, donc trois rayons dont chacun ne contribuerait qu'a
// son canal. Un rayon de Clarisse revient avec un triplet RVB complet et rien
// ne permet de le restreindre -- RayGeneratorData ne porte aucune longueur
// d'onde, le moteur est RVB de bout en bout. Ce filtre est la reponse.
//
// -- Echantillonnage ---------------------------------------------------------
//
// Le decalage lateral n'est pas trois copies decalees mais une integration le
// long du segment de dispersion, ponderee par un profil par canal. Trois
// echantillons donnent trois images fantomes visibles comme telles ; sept
// donnent un degrade continu.
//
// Le nombre est force impair. Cela garantit un echantillon central de
// deplacement nul, donc un poids toujours valide meme quand tous les autres
// tombent hors de l'image -- ce qui rend la renormalisation de bordure sure
// sans condition.
//
// Interpolation bilineaire, et pas Catmull-Rom : le depassement de cette
// derniere sur des valeurs lineaires non ecretees fabrique un lisere noir sur
// un seul canal au bord des hautes lumieres, exactement l'artefact qu'on
// cherche a eviter.

#include <dso_export.h>
#include <of_app.h>
#include <of_object_factory.h>

#include <module_kernel_filter.h>
#include <ctx_eval.h>
#include <ctx_filter.h>
#include <image_canvas.h>
#include <image_proxy.h>

#include <math.h>
#include <vector>

#include <chroma.cma>

class ChromaModule : public ModuleKernelFilter {
public:
    ChromaModule() : ModuleKernelFilter() {}
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(ImageFilterChromaticAberration, ModuleKernelFilterCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
    static void pre_filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx,
                           unsigned int& kernel_radius, unsigned int& total_pass_count);
    static bool filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx);
    static void post_filter(OfObject& object);
IX_END_DECLARE_MODULE_CALLBACKS(ImageFilterChromaticAberration)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(ImageFilterChromaticAberration);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
    module_callbacks->cb_pre_filter = IX_MODULE_CLBK::pre_filter;
    module_callbacks->cb_filter = IX_MODULE_CLBK::filter;
    module_callbacks->cb_post_filter = IX_MODULE_CLBK::post_filter;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    // set_object est indispensable : OfModule::is_protected() dereference
    // m_object sans le tester, et l'application interroge le module des que
    // l'objet rejoint son contexte.
    ChromaModule *module = new ChromaModule();
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

struct Settings {
    double lateral;             // fraction de la demi-diagonale
    double spectrum[3];         // position de chaque canal sur le segment
    double falloff;
    double longitudinal;        // pixels
    double defocus[3];
    double fringe;
    double fringe_threshold;
    double fringe_knee;
    double fringe_radius;       // pixels
    double fringe_color[3];
    double center[2];           // fraction, depuis le centre
    int    samples;             // impair
    int    image_width;
    int    image_height;
};

void
read_settings(const CtxEval& eval, const CtxKernelFilter& ctx,
              const ImageProxy *proxy, Settings& s)
{
    const CmaImageFilterChromaticAberration& cma =
        (const CmaImageFilterChromaticAberration&) eval.get_cma();

    // Les rayons sont exprimes en pixels de l'image finale, mais Clarisse peut
    // evaluer a une fraction de sa taille.
    const double scale = ctx.resolution_multiplier > 0.0
                         ? ctx.resolution_multiplier : 1.0;

    s.lateral = cma.get_lateral_amount();
    const GMathVec3d spectrum = cma.get_lateral_spectrum();
    for (int i = 0; i < 3; ++i) s.spectrum[i] = spectrum[i];
    s.falloff = cma.get_lateral_falloff();

    s.longitudinal = cma.get_longitudinal_amount() * scale;
    const GMathVec3d defocus = cma.get_longitudinal_defocus();
    for (int i = 0; i < 3; ++i) s.defocus[i] = defocus[i];

    s.fringe = cma.get_fringe_amount();
    s.fringe_threshold = cma.get_fringe_threshold();
    s.fringe_knee = cma.get_fringe_knee();
    s.fringe_radius = cma.get_fringe_radius() * scale;
    const GMathVec3d tint = cma.get_fringe_color();
    for (int i = 0; i < 3; ++i) s.fringe_color[i] = tint[i];

    // Un attribut a plusieurs composantes qui n'est ni une couleur ni un
    // vecteur nomme sort en CoreArray, pas en GMathVec.
    const CoreArray<double>& centre = cma.get_center();
    s.center[0] = centre.get_count() > 0 ? centre[0] : 0.0;
    s.center[1] = centre.get_count() > 1 ? centre[1] : 0.0;

    // Impair : c'est ce qui garantit un echantillon central de deplacement nul.
    s.samples = (int) cma.get_samples();
    if (s.samples < 3) s.samples = 3;
    if ((s.samples & 1) == 0) s.samples += 1;

    s.image_width  = proxy ? proxy->get_image_width() : 1;
    s.image_height = proxy ? proxy->get_image_height() : 1;
}

// Demi-diagonale de l'image, en pixels : l'unite dans laquelle le decalage
// lateral est exprime, pour qu'un reglage garde le meme sens d'une resolution
// a l'autre.
inline double
half_diagonal(const Settings& s)
{
    const double w = s.image_width * 0.5;
    const double h = s.image_height * 0.5;
    return sqrt(w * w + h * h);
}

// Deplacement maximal, en pixels : la marge que le filtre doit demander.
inline double
max_reach(const Settings& s)
{
    double reach = s.lateral * half_diagonal(s);
    const double blur = s.longitudinal * 0.5;
    if (blur > reach) reach = blur;
    if (s.fringe > 0.0 && s.fringe_radius > reach) reach = s.fringe_radius;
    return reach;
}

// Lecture bilineaire dans le proxy, bornes comprises. Clarisse remplit deja la
// marge en CLAMP, donc les bornes ne servent que de garde-fou.
inline float
sample_bilinear(const float *data, const int& stride, const int& rows,
                const double& x, const double& y)
{
    double fx = x, fy = y;
    if (fx < 0.0) fx = 0.0;
    if (fy < 0.0) fy = 0.0;
    if (fx > stride - 1.0) fx = stride - 1.0;
    if (fy > rows - 1.0) fy = rows - 1.0;

    const int x0 = (int) fx;
    const int y0 = (int) fy;
    const int x1 = (x0 + 1 < stride) ? x0 + 1 : x0;
    const int y1 = (y0 + 1 < rows) ? y0 + 1 : y0;
    const double tx = fx - x0;
    const double ty = fy - y0;

    const double a = data[(size_t)y0 * stride + x0] * (1.0 - tx)
                   + data[(size_t)y0 * stride + x1] * tx;
    const double b = data[(size_t)y1 * stride + x0] * (1.0 - tx)
                   + data[(size_t)y1 * stride + x1] * tx;
    return (float)(a * (1.0 - ty) + b * ty);
}

} // namespace

void
IX_MODULE_CLBK::pre_filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx,
                           unsigned int& kernel_radius, unsigned int& total_pass_count)
{
    // A ce stade seul source_image est valide : dest_image porte un pointeur
    // invalide et x0/y0 du bruit.
    Settings s;
    read_settings(eval, ctx, 0, s);
    s.image_width  = ctx.source_image ? ctx.source_image->get_width() : 1;
    s.image_height = ctx.source_image ? ctx.source_image->get_height() : 1;

    const double reach = max_reach(s);
    kernel_radius = (unsigned int)(reach < 0.0 ? 0.0 : reach + 1.5);
    total_pass_count = 1;
}

bool
IX_MODULE_CLBK::filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx)
{
    const ImageProxy *src = ctx.image;
    if (src == 0) return true;

    Settings s;
    read_settings(eval, ctx, src, s);

    const bool do_lateral = (s.lateral > 0.0);
    const bool do_longitudinal = (s.longitudinal > 0.5);
    const bool do_fringe = (s.fringe > 0.0 && s.fringe_radius > 0.5);
    if (!do_lateral && !do_longitudinal && !do_fringe) return true;

    const float *channels[3];
    channels[0] = src->get_red_channel();
    channels[1] = src->get_green_channel();
    channels[2] = src->get_blue_channel();

    float *out[3];
    out[0] = ctx.channel_r.data;
    out[1] = ctx.channel_g.data;
    out[2] = ctx.channel_b.data;

    // Les canaux peuvent etre nuls : le moteur n'en alloue un que si la map
    // porte le nom correspondant.
    for (int c = 0; c < 3; ++c)
        if (channels[c] == 0 || out[c] == 0) return true;

    const int stride = (int) src->get_width();
    const int rows   = (int) src->get_height();
    const int width  = ctx.region.width;
    const int height = ctx.region.height;

    const double half_w = s.image_width * 0.5;
    const double half_h = s.image_height * 0.5;
    const double cx_img = half_w * (1.0 + s.center[0]);
    const double cy_img = half_h * (1.0 + s.center[1]);
    const double diag = half_diagonal(s);

    // Le profil par canal sur le segment de dispersion : une tente centree sur
    // la position spectrale du canal. Normalise, pour que le filtre soit
    // exactement neutre a decalage nul.
    const int taps = s.samples;
    std::vector<double> weight((size_t)taps * 3, 0.0);
    for (int c = 0; c < 3; ++c) {
        double total = 0.0;
        for (int i = 0; i < taps; ++i) {
            const double t = (taps > 1) ? (double)i / (taps - 1) : 0.5;
            double w = 1.0 - fabs(t - s.spectrum[c]) / 0.5;
            if (w < 0.0) w = 0.0;
            weight[(size_t)c * taps + i] = w;
            total += w;
        }
        if (total <= 0.0) {
            // Position spectrale hors du segment : on retombe sur l'echantillon
            // central, de deplacement nul, plutot que sur une division par zero.
            weight[(size_t)c * taps + taps / 2] = 1.0;
            total = 1.0;
        }
        const double inverse = 1.0 / total;
        for (int i = 0; i < taps; ++i) weight[(size_t)c * taps + i] *= inverse;
    }

    // Rayon de flou longitudinal par canal, en pixels.
    double blur[3] = {0.0, 0.0, 0.0};
    if (do_longitudinal)
        for (int c = 0; c < 3; ++c) blur[c] = s.longitudinal * s.defocus[c];

    for (int y = 0; y < height; ++y) {
        const int py = y + ctx.region.y;
        const double img_y = ctx.y0 + y;
        for (int x = 0; x < width; ++x) {
            const int px = x + ctx.region.x;
            const double img_x = ctx.x0 + x;

            // Direction radiale depuis le centre optique, et amplitude du
            // decalage. Elle croit en rho^n et s'annule exactement au centre --
            // c'est ce qui distingue l'aberration laterale d'un simple flou.
            double dir_x = 0.0, dir_y = 0.0, shift = 0.0;
            if (do_lateral) {
                const double vx = img_x - cx_img;
                const double vy = img_y - cy_img;
                const double len = sqrt(vx * vx + vy * vy);
                if (len > 1e-9) {
                    dir_x = vx / len;
                    dir_y = vy / len;
                    const double rho = len / diag;
                    shift = s.lateral * diag * pow(rho, s.falloff);
                }
            }

            for (int c = 0; c < 3; ++c) {
                double value = 0.0;
                for (int i = 0; i < taps; ++i) {
                    const double w = weight[(size_t)c * taps + i];
                    if (w <= 0.0) continue;
                    const double t = (taps > 1) ? (double)i / (taps - 1) : 0.5;
                    const double offset = (2.0 * t - 1.0) * shift;
                    value += w * sample_bilinear(channels[c], stride, rows,
                                                 px - dir_x * offset,
                                                 py - dir_y * offset);
                }

                // Flou longitudinal : une petite moyenne circulaire, propre a
                // chaque canal. Sans profondeur c'est une approximation
                // uniforme -- a laisser a zero quand le bokeh s'en charge.
                if (blur[c] > 0.5) {
                    const int reach = (int)(blur[c] + 0.5);
                    double sum = 0.0, norm = 0.0;
                    for (int dy = -reach; dy <= reach; ++dy) {
                        for (int dx = -reach; dx <= reach; ++dx) {
                            if (dx * dx + dy * dy > reach * reach) continue;
                            const int ty = py - dy;
                            const int tx = px - dx;
                            if (ty < 0 || ty >= rows || tx < 0 || tx >= stride) continue;
                            sum += channels[c][(size_t)ty * stride + tx];
                            norm += 1.0;
                        }
                    }
                    if (norm > 0.0) value = sum / norm;
                }

                out[c][y * width + x] = (float) value;
            }

            // La frange violette : additive, seuillee sur max(r, g, b) avec un
            // genou. Le seuil dur ferait "popper" les pixels qui le traversent,
            // ce qui scintille en animation.
            if (do_fringe) {
                const int reach = (int)(s.fringe_radius + 0.5);
                double excess = 0.0, norm = 0.0;
                for (int dy = -reach; dy <= reach; ++dy) {
                    for (int dx = -reach; dx <= reach; ++dx) {
                        const int d2 = dx * dx + dy * dy;
                        if (d2 > reach * reach) continue;
                        const int ty = py - dy;
                        const int tx = px - dx;
                        if (ty < 0 || ty >= rows || tx < 0 || tx >= stride) continue;
                        const size_t o = (size_t)ty * stride + tx;
                        float peak = channels[0][o];
                        if (channels[1][o] > peak) peak = channels[1][o];
                        if (channels[2][o] > peak) peak = channels[2][o];

                        double over = peak - s.fringe_threshold;
                        if (over > 0.0) {
                            // Genou quadratique : la reprise demarre en
                            // douceur au lieu de sauter au franchissement.
                            const double knee = s.fringe_knee * s.fringe_threshold;
                            if (knee > 1e-9 && over < knee)
                                over = over * over / (2.0 * knee);
                            excess += over;
                        }
                        norm += 1.0;
                    }
                }
                if (norm > 0.0 && excess > 0.0) {
                    const double amount = s.fringe * excess / norm;
                    for (int c = 0; c < 3; ++c)
                        out[c][y * width + x] += (float)(amount * s.fringe_color[c]);
                }
            }
        }
    }

    return true;
}

void
IX_MODULE_CLBK::post_filter(OfObject& object)
{
}
