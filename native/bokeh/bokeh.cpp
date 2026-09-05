// Bokeh -- flou de mise au point optique pour Clarisse.
//
// Ce que Defocus Blur ne fait pas :
//
//   - le noyau reproduit un vrai diaphragme -- lames droites ou bombees par
//     arc de cercle exact, rotation, anamorphisme ;
//   - les defauts d'optique qui font le realisme sont exposes : vignettage
//     optique en oeil-de-chat, aberration spherique, aberration chromatique ;
//   - l'alpha est convolue avec le meme noyau que les couleurs, faute de quoi
//     la silhouette reste nette pendant que l'image floute.
//
// -- Sur la lumiere -----------------------------------------------------------
//
// Tout se calcule en lineaire, et rien n'est ecrete. C'est ce qui fait les
// boules : une speculaire a 50 sur un fond a 0.1, etalee sur le disque, donne
// une boule plusieurs fois plus lumineuse que son voisinage. Aucun artifice
// n'est necessaire -- la moyenne normalisee suffit, et c'est le comportement
// physique. Le couple threshold/gain n'est la que pour rattraper une source
// deja ecretee, ou pour forcer le trait : c'est un reglage artistique, pas le
// mecanisme.
//
// -- Le contrat de CtxKernelFilter, mesure ------------------------------------
//
// Rien de ceci n'est documente : les pages Doxygen listent les champs et
// laissent toutes les descriptions vides. Releve en instrumentant le filtre,
// puis recoupe en desassemblant ix_module.dll.
//
//   ctx.image     proxy de la SOURCE, deja elargi de kernel_radius de chaque
//                 cote (88x88 pour une tuile de 64 et un rayon de 12).
//                 Clarisse fournit donc la marge : rien a demander.
//   ctx.region    la tuile a ecrire, en coordonnees DU PROXY : {r, r, w, h}.
//   ctx.channel_* la DESTINATION, region.width x region.height, indexee
//                 y * region.width + x, sans marge. Pre-remplie avec la
//                 source : ne rien ecrire ne casse rien.
//   ctx.x0, y0    la tuile en coordonnees image.
//   bords         la marge hors canvas est remplie en CLAMP par Clarisse :
//                 aucun traitement de bord a ecrire.
//   pre_filter    seul source_image y est valide ; dest_image et x0/y0 sont
//                 du bruit, les lire plante.
//   retour        `true` valide la tuile, `false` la jette.

#include <dso_export.h>
#include <of_app.h>
#include <of_object_factory.h>

#include <module_kernel_filter.h>
#include <ctx_eval.h>
#include <ctx_filter.h>
#include <image_canvas.h>
#include <image_proxy.h>
#include <core_log.h>

#include <math.h>
#include <vector>

#include <bokeh.cma>

class BokehModule : public ModuleKernelFilter {
public:
    BokehModule() : ModuleKernelFilter() {}
};

// La doc du SDK ecrit ces callbacks avec ModuleObject * ; le vrai typedef dit
// OfModule * (of_class.h:35-36).
IX_BEGIN_DECLARE_MODULE_CALLBACKS(ImageFilterBokeh, ModuleKernelFilterCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
    static void pre_filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx,
                           unsigned int& kernel_radius, unsigned int& total_pass_count);
    static bool filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx);
    static void post_filter(OfObject& object);
IX_END_DECLARE_MODULE_CALLBACKS(ImageFilterBokeh)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(ImageFilterBokeh);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
    // ModuleKernelFilter::filter() appelle cb_filter sans tester sa nullite
    // (module_kernel_filter.h:33-35). L'oublier planterait.
    module_callbacks->cb_pre_filter = IX_MODULE_CLBK::pre_filter;
    module_callbacks->cb_filter = IX_MODULE_CLBK::filter;
    module_callbacks->cb_post_filter = IX_MODULE_CLBK::post_filter;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    // set_object est indispensable : OfModule::is_protected() et
    // get_object_name() dereferencent m_object sans le tester.
    BokehModule *module = new BokehModule();
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

const double PI = 3.14159265358979323846;

// Finesse de l'anneau d'aberration spherique. p grand concentre l'energie
// tres pres du bord ; 4 donne une bulle de savon credible.
const double SPHERICAL_POWER = 4.0;

// Quantification des poids interieurs pour les sommes prefixees. Sans
// aberration spherique l'interieur est uniforme et un seul niveau suffit.
const int SPHERICAL_LEVELS = 16;

// Ecart de rayon entre le rouge et le bleu, a pleine aberration chromatique.
// A 5 % il fallait chercher la frange a la loupe ; les optiques reelles en
// montrent bien plus sur les boules de bokeh.
const double CHROMA_SPREAD = 0.18;

struct Settings {
    double radius;          // en pixels de l'image evaluee
    int    blades;
    double rotation;        // radians
    double curvature;       // 0 lames droites .. 1 disque ; negatif = concave
    double anamorphism;
    double softness;
    double threshold;
    double gain;
    double vignetting;
    double spherical;
    double chromatic;
    int    image_width;
    int    image_height;
};

Settings g_settings;

// Inventaire des canaux : une seule fois par evaluation.
bool g_channels_reported = false;

// -- geometrie de l'ouverture -------------------------------------------------
//
// Formulation par demi-plans plutot que par atan2 : les normales sortantes des
// N aretes sont precalculees, et max_k (u . n_k) donne rho * cos(delta) ou
// delta est l'ecart angulaire a l'arete la plus proche. Aucune fonction
// transcendante par echantillon, et le resultat est directement une distance
// signee -- ce dont l'antialiasing a besoin.
struct Aperture {
    bool   circular;
    int    blades;
    double nx[64];
    double ny[64];
    double apothem;         // cos(pi/N)
    double arc_centre;      // c, abscisse du centre de l'arc de lame
    double arc_radius;      // R_b
    bool   concave;
};

void
aperture_init(Aperture& a, const int& blades, const double& rotation,
              const double& curvature)
{
    a.circular = (blades < 3);
    a.blades = blades > 64 ? 64 : blades;
    a.concave = (curvature < 0.0);
    if (a.circular) {
        a.apothem = 1.0;
        a.arc_centre = 0.0;
        a.arc_radius = 1.0;
        return;
    }

    const double half = PI / a.blades;
    a.apothem = cos(half);
    const double chord = sin(half);

    for (int k = 0; k < a.blades; ++k) {
        const double angle = rotation + (2.0 * k + 1.0) * half;
        a.nx[k] = cos(angle);
        a.ny[k] = sin(angle);
    }

    // Lames bombees : chaque arete droite est remplacee par un arc de cercle
    // passant par les deux sommets. Le bombement au milieu de l'arete vaut
    // kappa * (1 - cos(pi/N)) ; le rayon de l'arc s'en deduit. A kappa = 1 on
    // retrouve exactement le cercle, a kappa = 0 exactement le polygone.
    const double bulge = fabs(curvature) * (1.0 - a.apothem);
    if (bulge < 1e-9) {
        a.arc_radius = 0.0;     // polygone droit : pas d'arc
        a.arc_centre = 0.0;
        return;
    }
    a.arc_radius = (bulge * bulge + chord * chord) / (2.0 * bulge);
    const double offset = sqrt(a.arc_radius * a.arc_radius - chord * chord);
    a.arc_centre = a.concave ? (a.apothem + offset) : (a.apothem - offset);
}

// Rayon de la frontiere dans la direction de l'echantillon, en unites de rayon
// circonscrit. Rend 1 pour le disque.
inline double
aperture_edge(const Aperture& a, const double& ux, const double& uy,
              const double& rho)
{
    if (a.circular) return 1.0;

    double m = ux * a.nx[0] + uy * a.ny[0];
    for (int k = 1; k < a.blades; ++k) {
        const double d = ux * a.nx[k] + uy * a.ny[k];
        if (d > m) m = d;
    }
    if (rho < 1e-9) return a.apothem;

    // m = rho * cos(delta) : on tient cos et sin de l'ecart a l'arete sans
    // jamais appeler atan2.
    double cos_d = m / rho;
    if (cos_d > 1.0) cos_d = 1.0;
    if (cos_d < -1.0) cos_d = -1.0;

    if (a.arc_radius <= 0.0) {
        // Polygone droit : rho <= apotheme / cos(delta).
        if (cos_d <= 1e-6) return 1.0;
        return a.apothem / cos_d;
    }

    const double sin2 = 1.0 - cos_d * cos_d;
    const double inside = a.arc_radius * a.arc_radius
                          - a.arc_centre * a.arc_centre * sin2;
    if (inside <= 0.0) return a.apothem;
    const double root = sqrt(inside);
    return a.concave ? (a.arc_centre * cos_d - root)
                     : (a.arc_centre * cos_d + root);
}

// -- le noyau -----------------------------------------------------------------
//
// Un tap explicite : utilise pour les echantillons de bord, dont la couverture
// est partielle, et pour le chemin lent.
struct Tap {
    int   dx;
    int   dy;
    float weight;
};

// Un segment horizontal de l'interieur, a poids constant sur son niveau.
struct Run {
    int dy;
    int x0;
    int x1;   // inclus
};

struct Kernel {
    int    reach;
    double total;           // somme de tous les poids, avant normalisation
    double level_weight;    // poids d'un niveau de l'interieur
    int    levels;
    std::vector<Run> runs;  // interieur, quantifie par niveaux
    std::vector<Tap> edge;  // bord, couverture partielle, poids exact
    std::vector<Tap> all;   // tous les taps, pour le chemin lent
    bool   uniform_interior;
};

// Couverture d'un echantillon, suréchantillonnee 8x8 quand il est a cheval sur
// la frontiere. Le suréchantillonnage n'est fait que la : sur un rayon de 100
// il concerne quelques centaines de taps sur trente mille.
inline double
coverage(const Aperture& a, const double& cx, const double& cy,
         const double& step, const double& feather)
{
    const double rho = sqrt(cx * cx + cy * cy);
    const double edge = aperture_edge(a, cx, cy, rho);
    const double d = rho - edge;          // <0 dedans, >0 dehors
    if (d < -feather) return 1.0;
    if (d > feather) return 0.0;

    const int N = 8;
    int inside = 0;
    for (int j = 0; j < N; ++j) {
        const double sy = cy + step * ((j + 0.5) / N - 0.5);
        for (int i = 0; i < N; ++i) {
            const double sx = cx + step * ((i + 0.5) / N - 0.5);
            const double r = sqrt(sx * sx + sy * sy);
            if (r <= aperture_edge(a, sx, sy, r)) ++inside;
        }
    }
    return double(inside) / (N * N);
}

// Construit le noyau pour une position donnee dans le cadre.
//
// Le noyau depend de l'endroit du cadre a cause du vignettage optique, nul au
// centre et maximal dans les coins. On le rebatit donc par tuile, evalue en
// son centre. Le faire par pixel serait plus juste et cent fois plus lent,
// pour une difference invisible sur 64 pixels -- le vignettage varie a
// l'echelle du cadre.
void
build_kernel(Kernel& k, const Settings& s, const double& channel_scale,
             const double& frame_x, const double& frame_y,
             const bool& keep_taps)
{
    k.runs.clear();
    k.edge.clear();
    k.all.clear();
    k.total = 0.0;
    k.levels = 1;
    k.level_weight = 0.0;
    k.uniform_interior = (s.spherical == 0.0);

    const double radius = s.radius * channel_scale;
    if (radius < 0.5) { k.reach = 0; return; }

    Aperture aperture;
    aperture_init(aperture, s.blades, s.rotation, s.curvature);

    // Anamorphisme : on comprime les coordonnees d'echantillonnage sur un axe,
    // ce qui etire la forme obtenue sur l'autre.
    double scale_x = 1.0, scale_y = 1.0;
    if (s.anamorphism > 0.0) scale_x = 1.0 + s.anamorphism;
    else if (s.anamorphism < 0.0) scale_y = 1.0 - s.anamorphism;

    const int reach_x = (int)(radius / scale_x + 1.5);
    const int reach_y = (int)(radius / scale_y + 1.5);
    k.reach = reach_x > reach_y ? reach_x : reach_y;

    // Vignettage optique : le barillet tronque le faisceau hors axe. La pupille
    // apparente devient l'intersection de deux disques decales -- l'amande dite
    // oeil-de-chat. Le decalage est dirige vers le centre du cadre, de sorte
    // que le grand axe de l'ovale soit tangentiel : les amandes tournent autour
    // du centre, elles ne pointent pas vers lui.
    const double frame_r = sqrt(frame_x * frame_x + frame_y * frame_y);
    const double offset = s.vignetting * frame_r;
    double shift_x = 0.0, shift_y = 0.0;
    if (offset > 1e-6 && frame_r > 1e-9) {
        shift_x = -frame_x / frame_r * offset;
        shift_y = -frame_y / frame_r * offset;
    }

    // Largeur du fondu de bord. Un pixel suffit a supprimer le crenelage ;
    // au-dela, c'est le reglage de douceur qui l'elargit volontairement, pour
    // le rendu des optiques a lames usees ou des filtres diffuseurs.
    double feather = 1.0 / radius;
    if (s.softness > 0.0) {
        const double wanted = s.softness;
        if (wanted > feather) feather = wanted;
    }
    const double step = 1.0 / radius;

    // Poids brut par echantillon, garde pour extraire les segments ensuite.
    const int side = 2 * k.reach + 1;
    std::vector<float> field((size_t)side * side, 0.0f);
    std::vector<float> partial((size_t)side * side, 0.0f);
    double max_interior = 0.0;

    for (int dy = -k.reach; dy <= k.reach; ++dy) {
        for (int dx = -k.reach; dx <= k.reach; ++dx) {
            const double ux = dx * scale_x / radius;
            const double uy = dy * scale_y / radius;

            double cover = coverage(aperture, ux, uy, step, feather);
            if (cover <= 0.0) continue;

            if (offset > 1e-6) {
                // Disque de troncature : intersection de deux convexes.
                const double vx = ux - shift_x;
                const double vy = uy - shift_y;
                const double vr = sqrt(vx * vx + vy * vy);
                double cut = (1.0 - vr) / feather + 0.5;
                if (cut <= 0.0) continue;
                if (cut > 1.0) cut = 1.0;
                cover *= cut;
            }

            // Aberration spherique : redistribution radiale a moyenne
            // preservee. La moyenne de rho^p ponderee par l'aire sur le disque
            // unite vaut 2/(p+2), donc le terme entre parentheses est de
            // moyenne nulle : le curseur deplace l'energie sans en ajouter.
            double weight = cover;
            if (s.spherical != 0.0) {
                // rho^4 par deux multiplications : ni sqrt ni pow.
                const double rho2 = ux * ux + uy * uy;
                const double p = SPHERICAL_POWER;   // 4
                double bias = 1.0 + s.spherical * (rho2 * rho2 - 2.0 / (p + 2.0));
                if (bias < 0.0) bias = 0.0;
                weight *= bias;
            }
            if (weight <= 0.0) continue;

            const size_t index = (size_t)(dy + k.reach) * side + (dx + k.reach);
            if (cover >= 0.999) {
                field[index] = (float) weight;
                if (weight > max_interior) max_interior = weight;
            } else {
                partial[index] = (float) weight;
            }
            k.total += weight;

            if (keep_taps) {
                Tap tap;
                tap.dx = dx;
                tap.dy = dy;
                tap.weight = (float) weight;
                k.all.push_back(tap);
            }
        }
    }

    if (k.total <= 0.0) { k.reach = 0; return; }

    // Les echantillons de bord gardent leur poids exact : les quantifier ferait
    // apparaitre des marches sur le pourtour des boules, et c'est precisement
    // la que ca se voit.
    for (int dy = -k.reach; dy <= k.reach; ++dy) {
        for (int dx = -k.reach; dx <= k.reach; ++dx) {
            const size_t index = (size_t)(dy + k.reach) * side + (dx + k.reach);
            if (partial[index] > 0.0f) {
                Tap tap;
                tap.dx = dx;
                tap.dy = dy;
                tap.weight = partial[index];
                k.edge.push_back(tap);
            }
        }
    }

    // L'interieur, lui, se decompose en niveaux : chaque niveau est un ensemble
    // de segments horizontaux, et un segment se somme en deux lectures dans une
    // somme prefixee. Sans aberration spherique tous les poids interieurs sont
    // egaux et un seul niveau suffit.
    k.levels = k.uniform_interior ? 1 : SPHERICAL_LEVELS;
    k.level_weight = max_interior / k.levels;
    if (k.level_weight <= 0.0) { k.levels = 0; return; }

    for (int level = 1; level <= k.levels; ++level) {
        const float threshold = (float)((level - 0.5) * k.level_weight);
        for (int dy = -k.reach; dy <= k.reach; ++dy) {
            // `open` plutot qu'une sentinelle a -1 : dx est negatif sur la
            // moitie gauche du noyau, et un segment qui y commence serait
            // indistinguable de l'absence de segment. Le bug se voit tres
            // bien -- chaque boule perd sa moitie gauche.
            bool open = false;
            int start = 0;
            for (int dx = -k.reach; dx <= k.reach + 1; ++dx) {
                const bool in = (dx <= k.reach) &&
                    field[(size_t)(dy + k.reach) * side + (dx + k.reach)] >= threshold;
                if (in && !open) { open = true; start = dx; }
                else if (!in && open) {
                    Run run;
                    run.dy = dy;
                    run.x0 = start;
                    run.x1 = dx - 1;
                    k.runs.push_back(run);
                    open = false;
                }
            }
        }
    }

    // La quantification de l'interieur decale legerement la somme des poids.
    // On recalcule le total sur ce qui sera reellement somme, pour que la
    // normalisation soit exacte et l'energie conservee.
    double quantised = 0.0;
    for (size_t i = 0; i < k.runs.size(); ++i)
        quantised += (k.runs[i].x1 - k.runs[i].x0 + 1) * k.level_weight;
    for (size_t i = 0; i < k.edge.size(); ++i)
        quantised += k.edge[i].weight;
    if (quantised > 0.0) k.total = quantised;
}

// -- sommes prefixees ---------------------------------------------------------
//
// Toutes nos formes sont convexes, ou intersection de convexes : chaque coupe
// horizontale du noyau est un segment. Un segment se somme alors en deux
// lectures au lieu de sa longueur.
//
// En double : une somme prefixee sur une ligne d'image HDR en float32 perd de
// la precision par soustraction de grands nombres.
struct Prefix {
    int width;
    int height;
    std::vector<double> data;   // (width + 1) par ligne, data[0] = 0

    void build(const float *source, const int& w, const int& h) {
        width = w;
        height = h;
        data.assign((size_t)(w + 1) * h, 0.0);
        for (int y = 0; y < h; ++y) {
            double running = 0.0;
            const float *row = source + (size_t)y * w;
            double *out = &data[(size_t)y * (w + 1)];
            out[0] = 0.0;
            for (int x = 0; x < w; ++x) {
                running += row[x];
                out[x + 1] = running;
            }
        }
    }

    // Somme de source[y][x0..x1] inclus, bornes supposees valides.
    inline double span(const int& y, const int& x0, const int& x1) const {
        const double *row = &data[(size_t)y * (width + 1)];
        return row[x1 + 1] - row[x0];
    }
};

} // namespace

// -- callbacks ----------------------------------------------------------------

void
IX_MODULE_CLBK::pre_filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx,
                           unsigned int& kernel_radius, unsigned int& total_pass_count)
{
    const CmaImageFilterBokeh& cma = (const CmaImageFilterBokeh&) eval.get_cma();

    // Le rayon est exprime en pixels de l'image finale, mais Clarisse peut
    // evaluer a une fraction de sa taille. La doc des layers le dit : un flou
    // de 5 pixels fait 5 pixels a 100 %, 2,5 a 50 %, 10 a 200 %, pour que le
    // reglage soit valable quel que soit le multiplicateur. C'est au filtre de
    // faire la mise a l'echelle.
    const double scale = ctx.resolution_multiplier > 0.0 ? ctx.resolution_multiplier : 1.0;

    g_settings.radius      = cma.get_radius() * scale;
    g_settings.blades      = (int) cma.get_blades();
    g_settings.rotation    = cma.get_rotation();
    g_settings.curvature   = cma.get_roundness();
    g_settings.anamorphism = cma.get_anamorphism();
    g_settings.softness    = cma.get_softness();
    g_settings.threshold   = cma.get_threshold();
    g_settings.gain        = cma.get_gain();
    g_settings.vignetting  = cma.get_optical_vignetting();
    g_settings.spherical   = cma.get_spherical_aberration();
    g_settings.chromatic   = cma.get_chromatic_aberration();
    g_settings.image_width  = ctx.source_image ? ctx.source_image->get_width() : 1;
    g_settings.image_height = ctx.source_image ? ctx.source_image->get_height() : 1;

    // La marge demandee doit contenir la forme la plus large : l'anamorphisme
    // l'etire sur un axe, l'aberration chromatique decale le rayon d'un canal.
    double reach = g_settings.radius;
    if (g_settings.anamorphism != 0.0) reach *= (1.0 + fabs(g_settings.anamorphism));
    reach *= (1.0 + CHROMA_SPREAD * fabs(g_settings.chromatic));

    kernel_radius = (unsigned int)(reach < 0.0 ? 0.0 : reach + 1.5);
    total_pass_count = 1;
    g_channels_reported = false;
}

bool
IX_MODULE_CLBK::filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx)
{
    const ImageProxy *src = ctx.image;
    if (src == 0) return true;

    const Settings& s = g_settings;

    // Inventaire des canaux, une fois par evaluation. C'est ce qui dira si un
    // canal de profondeur parvient jusqu'au filtre -- condition d'un rayon
    // variable, donc d'une vraie mise au point.
    if (!g_channels_reported) {
        g_channels_reported = true;
        CoreString names;
        for (unsigned int i = 0; i < src->get_channel_count(); ++i) {
            if (i) names += ", ";
            names += src->get_channel_name(i);
        }
        LOG_INFO("[Bokeh] " << src->get_channel_count() << " canaux : "
                 << names << "\n");
    }

    if (s.radius < 0.5) return true;   // la destination porte deja la source

    const float *channels[4];
    channels[0] = src->get_red_channel();
    channels[1] = src->get_green_channel();
    channels[2] = src->get_blue_channel();
    channels[3] = src->get_alpha_channel();

    float *out[4];
    out[0] = ctx.channel_r.data;
    out[1] = ctx.channel_g.data;
    out[2] = ctx.channel_b.data;
    out[3] = ctx.channel_a.data;

    if (channels[0] == 0 || out[0] == 0) return true;

    const int stride = (int) src->get_width();
    const int rows   = (int) src->get_height();
    const int width  = ctx.region.width;
    const int height = ctx.region.height;

    // Position de la tuile dans le cadre, ramenee a [-1, 1] depuis le centre :
    // c'est ce qui pilote le vignettage optique.
    const double half_w = s.image_width * 0.5;
    const double half_h = s.image_height * 0.5;
    const double frame_x = (ctx.x0 + width * 0.5 - half_w) / (half_w > 0.0 ? half_w : 1.0);
    const double frame_y = (ctx.y0 + height * 0.5 - half_h) / (half_h > 0.0 ? half_h : 1.0);

    // Un noyau par canal quand l'aberration chromatique est active : c'est le
    // decalage de rayon entre canaux qui colore le bord des boules. L'alpha
    // suit le rayon nominal -- il n'a pas de longueur d'onde.
    // Le couple threshold/gain pondere chaque echantillon par sa propre
    // luminance : les poids deviennent dependants des donnees, et les sommes
    // prefixees ne s'appliquent plus. A gain 1 -- le defaut, et le comportement
    // physique -- on prend le chemin rapide.
    const bool boosting = (s.gain != 1.0);

    Kernel kernel_rgb[3];
    const bool split = (s.chromatic != 0.0);
    build_kernel(kernel_rgb[1], s, 1.0, frame_x, frame_y, boosting);
    if (split) {
        build_kernel(kernel_rgb[0], s, 1.0 + CHROMA_SPREAD * s.chromatic, frame_x, frame_y, boosting);
        build_kernel(kernel_rgb[2], s, 1.0 - CHROMA_SPREAD * s.chromatic, frame_x, frame_y, boosting);
    }
    if (kernel_rgb[1].total <= 0.0) return true;

    if (!boosting) {
        // Chemin rapide. On convolue les quatre canaux avec le meme noyau
        // normalise, alpha compris : une convolution est une moyenne ponderee
        // par la couverture, et l'alpha porte la couverture. Ne pas le flouter
        // laisserait une silhouette nette sur une image floue.
        Prefix prefix;
        for (int c = 0; c < 4; ++c) {
            if (channels[c] == 0 || out[c] == 0) continue;
            const Kernel& k = (split && c < 3) ? kernel_rgb[c] : kernel_rgb[1];
            if (k.total <= 0.0) continue;

            prefix.build(channels[c], stride, rows);
            const double inverse = 1.0 / k.total;

            for (int y = 0; y < height; ++y) {
                const int cy = y + ctx.region.y;
                for (int x = 0; x < width; ++x) {
                    const int cx = x + ctx.region.x;
                    double sum = 0.0;

                    for (size_t i = 0; i < k.runs.size(); ++i) {
                        const Run& run = k.runs[i];
                        const int ry = cy + run.dy;
                        if (ry < 0 || ry >= rows) continue;
                        int x0 = cx + run.x0;
                        int x1 = cx + run.x1;
                        if (x1 < 0 || x0 >= stride) continue;
                        if (x0 < 0) x0 = 0;
                        if (x1 >= stride) x1 = stride - 1;
                        sum += prefix.span(ry, x0, x1);
                    }
                    sum *= k.level_weight;

                    for (size_t i = 0; i < k.edge.size(); ++i) {
                        const Tap& tap = k.edge[i];
                        const int ty = cy + tap.dy;
                        const int tx = cx + tap.dx;
                        if (ty < 0 || ty >= rows || tx < 0 || tx >= stride) continue;
                        sum += channels[c][(size_t)ty * stride + tx] * tap.weight;
                    }

                    out[c][y * width + x] = (float)(sum * inverse);
                }
            }
        }
        return true;
    }

    // Chemin lent : reprise des hautes lumieres. Chaque echantillon compte pour
    // son poids de noyau multiplie par un facteur qui ne depasse 1 que s'il
    // passe le seuil, et on divise par la somme reelle de ces poids. Dans un
    // disque ou un seul pixel est tres clair, il domine la moyenne et le disque
    // entier prend sa couleur. C'est un forcage artistique : en lineaire non
    // ecrete, la moyenne simple donne deja des boules lumineuses.
    const float threshold = (float) s.threshold;
    const float gain = (float) s.gain;
    const float *sr = channels[0], *sg = channels[1], *sb = channels[2];

    for (int y = 0; y < height; ++y) {
        const int cy = y + ctx.region.y;
        for (int x = 0; x < width; ++x) {
            const int cx = x + ctx.region.x;
            double sum[4] = {0.0, 0.0, 0.0, 0.0};
            double norm[4] = {0.0, 0.0, 0.0, 0.0};

            for (int c = 0; c < 4; ++c) {
                if (channels[c] == 0 || out[c] == 0) continue;
                const Kernel& k = (split && c < 3) ? kernel_rgb[c] : kernel_rgb[1];
                for (size_t i = 0; i < k.all.size(); ++i) {
                    const Tap& tap = k.all[i];
                    const int ty = cy + tap.dy;
                    const int tx = cx + tap.dx;
                    if (ty < 0 || ty >= rows || tx < 0 || tx >= stride) continue;
                    const size_t o = (size_t)ty * stride + tx;
                    const float lum = 0.2126f * sr[o] + 0.7152f * sg[o] + 0.0722f * sb[o];
                    double w = tap.weight;
                    if (lum > threshold) w *= 1.0 + gain * (lum - threshold);
                    sum[c] += channels[c][o] * w;
                    norm[c] += w;
                }
            }

            for (int c = 0; c < 4; ++c) {
                if (out[c] == 0 || channels[c] == 0) continue;
                out[c][y * width + x] = (float)(norm[c] > 0.0 ? sum[c] / norm[c] : 0.0);
            }
        }
    }

    return true;
}

void
IX_MODULE_CLBK::post_filter(OfObject& object)
{
}
