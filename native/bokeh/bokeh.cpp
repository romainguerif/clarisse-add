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
#include <image_handle.h>
#include <module_image.h>
#include <module_image_quality.h>
#include <of_object.h>
#include <of_attr.h>
#include <core_log.h>

#include <math.h>
#include <vector>

#include "aperture.h"
#include <bokeh.cma>

// Instantane de la passe de profondeur, pris une fois par evaluation dans
// pre_filter. On recopie les pixels plutot que garder un pointeur du moteur :
// filter() est multi-thread, et toute question de duree de vie ou de
// concurrence disparait avec la copie. Cout : un flottant par pixel.
//
// Il vit dans le module, pas dans un global : un filtre par objet, un
// instantane par objet. Deux filtres dans la meme scene ne se marchent pas
// dessus.
struct DepthSnapshot {
    std::vector<float> data;
    int  x, y, w, h;        // fenetre visible du canvas de profondeur
    bool ready;
    double near_value;      // etendue reelle, mesuree sur l'instantane
    double far_value;

    DepthSnapshot() : x(0), y(0), w(0), h(0), ready(false),
                      near_value(0.0), far_value(0.0) {}

    // Plus proche voisin, en coordonnees absolues, borne aux bords. Une
    // profondeur interpolee sur un bord d'objet est de toute facon un
    // mensonge -- la moyenne entre un premier plan et un arriere-plan.
    inline float at(int px, int py) const {
        px -= x; py -= y;
        if (px < 0) px = 0; else if (px >= w) px = w - 1;
        if (py < 0) py = 0; else if (py >= h) py = h - 1;
        return data[(size_t) py * w + px];
    }
};

class BokehModule : public ModuleKernelFilter {
public:
    BokehModule() : ModuleKernelFilter() {}
    DepthSnapshot depth;
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

using clarisse_add::Aperture;
using clarisse_add::aperture_init;
using clarisse_add::aperture_edge;

const double PI = 3.14159265358979323846;

// Finesse de l'anneau d'aberration spherique. p grand concentre l'energie
// tres pres du bord ; 4 donne une bulle de savon credible.
const double SPHERICAL_POWER = 4.0;

// Quantification des poids interieurs pour les sommes prefixees. Sans
// aberration spherique l'interieur est uniforme et un seul niveau suffit.
//
// A 16 niveaux, chaque marche vaut 6,25 % du pic : sur une bulle de savon
// eclairee a 50, cela fait des anneaux concentriques de 3 unites contre un
// fond a 0,1 -- parfaitement visibles. Le cout est lineaire en nombre de
// segments, pas en echantillons : monter a 64 divise la marche par quatre
// pour un surcout modeste.
const int SPHERICAL_LEVELS = 64;

// Ecart de rayon entre le rouge et le bleu, a pleine aberration chromatique.
// A 5 % il fallait chercher la frange a la loupe ; les optiques reelles en
// montrent bien plus sur les boules de bokeh.
const double CHROMA_SPREAD = 0.18;

// Nombre de paliers de rayon quand une passe de profondeur pilote le flou.
//
// pre_filter ne rend qu'un seul kernel_radius, valable pour toute l'image :
// c'est lui qui dimensionne la marge du proxy, et rien ne peut la changer par
// pixel. Un rayon variable se rend donc par une echelle de noyaux batis a
// l'avance, tous bornes par le rayon maximal declare, et on choisit le palier
// pixel par pixel.
//
// Douze paliers : au-dela on ne distingue plus les marches sur un degrade de
// profondeur, en deca elles se voient sur les surfaces inclinees.
const int DEPTH_STEPS = 12;

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
    bool   preserve_exposure;
    double spherical;
    double chromatic;
    int    depth_mode;          // 0 distance reelle, 1 inverse
    double focus_distance;
    double focus_range;
    double blur_falloff;
    int    focus_side;          // 0 les deux, 1 arriere, 2 avant
    int    image_width;
    int    image_height;
};

// Lit les reglages depuis le Cma synchronise par Clarisse. Appele aussi bien
// par pre_filter que par chaque tuile de filter : le Cma est en lecture seule
// et propre a l'evaluation, donc sans partage entre instances.
void
read_settings(const CtxEval& eval, const CtxKernelFilter& ctx,
              const ImageProxy *proxy, Settings& s)
{
    const CmaImageFilterBokeh& cma = (const CmaImageFilterBokeh&) eval.get_cma();

    // Le rayon est exprime en pixels de l'image finale, mais Clarisse peut
    // evaluer a une fraction de sa taille. La doc des layers le dit : un flou
    // de 5 pixels fait 5 pixels a 100 %, 2,5 a 50 %, 10 a 200 %.
    const double scale = ctx.resolution_multiplier > 0.0
                         ? ctx.resolution_multiplier : 1.0;

    s.radius      = cma.get_radius() * scale;
    s.blades      = (int) cma.get_blades();
    // Les attributs de type `angle` sont stockes en DEGRES et le .cma ne
    // convertit rien : lus tels quels comme des radians, 5 degres demandes
    // donnent 286 degres reels.
    s.rotation    = cma.get_rotation() * (PI / 180.0);
    s.curvature   = cma.get_roundness();
    s.anamorphism = cma.get_anamorphism();
    s.softness    = cma.get_softness();
    s.threshold   = cma.get_threshold();
    s.gain        = cma.get_gain();
    s.vignetting  = cma.get_optical_vignetting();
    s.preserve_exposure = cma.get_preserve_exposure();
    s.spherical   = cma.get_spherical_aberration();
    s.chromatic   = cma.get_chromatic_aberration();
    s.depth_mode      = (int) cma.get_depth_mode();
    s.focus_distance  = cma.get_focus_distance();
    s.focus_range     = cma.get_focus_range();
    s.blur_falloff    = cma.get_blur_falloff();
    s.focus_side      = (int) cma.get_focus_side();

    // Les dimensions viennent du proxy, qui les connait : ImageProxy expose
    // get_image_width/height. Passer par source_image obligerait a tenir
    // compte de get_x/get_y, non nuls des qu'une region de rendu est active.
    s.image_width  = proxy ? proxy->get_image_width() : 1;
    s.image_height = proxy ? proxy->get_image_height() : 1;
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
    double total_unvignetted;  // la meme, sans le disque de troncature
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
         const double& step, const double& softness)
{
    const double rho = sqrt(cx * cx + cy * cy);
    const double edge = aperture_edge(a, cx, cy, rho);
    const double d = rho - edge;          // <0 dedans, >0 dehors

    // La douceur est un fondu de la frontiere sur une bande de largeur
    // `softness`, en fraction du rayon. Elle doit ponderer, pas seulement
    // elargir la zone testee : le test 8x8 porte sur une cellule d'un pixel,
    // donc au-dela d'un demi-pixel de la frontiere les 64 sous-echantillons
    // tombent tous du meme cote et le resultat ne change pas d'un iota.
    // C'est ce qu'on a mesure : ecart 0,000 entre douceur 0 et douceur 1.
    if (softness > 1e-6) {
        if (d <= -softness) return 1.0;
        if (d >= softness) return 0.0;
        const double t = 0.5 - d / (2.0 * softness);
        return t * t * (3.0 - 2.0 * t);   // smoothstep
    }

    // Bord franc : un pixel de couverture partielle suffit a supprimer le
    // crenelage, et il se calcule par sous-echantillonnage de la cellule.
    const double half_cell = step * 0.7072;
    if (d < -half_cell) return 1.0;
    if (d > half_cell) return 0.0;

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
    k.total_unvignetted = 0.0;
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

    // La douceur du bord, en fraction du rayon. A zero, `coverage` retombe sur
    // un sous-echantillonnage d'un pixel, qui suffit a supprimer le crenelage.
    const double softness = s.softness;
    const double step = 1.0 / radius;

    // Largeur de la rampe du disque de troncature. Elle suit la douceur quand
    // elle est demandee, un pixel sinon.
    const double cut_feather = (softness > step) ? softness : step;

    // Poids brut par echantillon, garde pour extraire les segments ensuite.
    const int side = 2 * k.reach + 1;
    std::vector<float> field((size_t)side * side, 0.0f);
    std::vector<float> partial((size_t)side * side, 0.0f);
    double max_interior = 0.0;

    for (int dy = -k.reach; dy <= k.reach; ++dy) {
        for (int dx = -k.reach; dx <= k.reach; ++dx) {
            const double ux = dx * scale_x / radius;
            const double uy = dy * scale_y / radius;

            double cover = coverage(aperture, ux, uy, step, softness);
            if (cover <= 0.0) continue;

            // Le poids avant troncature sert de reference d'exposition : le
            // vignettage optique DOIT assombrir les coins, c'est ce qu'il est.
            // Normaliser par la somme locale annulerait exactement
            // l'assombrissement et ne garderait que la forme d'amande.
            const double cover_full = cover;

            if (offset > 1e-6) {
                // Disque de troncature : intersection de deux convexes.
                const double vx = ux - shift_x;
                const double vy = uy - shift_y;
                const double vr = sqrt(vx * vx + vy * vy);
                double cut = (1.0 - vr) / cut_feather + 0.5;
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
            {
                double full = cover_full;
                if (s.spherical != 0.0) {
                    const double rho2f = ux * ux + uy * uy;
                    double bias = 1.0 + s.spherical
                                  * (rho2f * rho2f - 2.0 / (SPHERICAL_POWER + 2.0));
                    if (bias < 0.0) bias = 0.0;
                    full *= bias;
                }
                k.total_unvignetted += full;
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

// Fraction du rayon maximal a appliquer pour une valeur de profondeur donnee.
// Rend 0 dans la zone nette, 1 au-dela de la portee utile.
//
// Ce n'est pas la formule optique du cercle de confusion : celle-ci demande
// une focale, une ouverture et une taille de capteur, dont un filtre 2D ne
// dispose pas. C'est le modele artistique, celui de tous les outils de
// compositing -- un plan de mise au point, une profondeur de zone nette, et
// une courbe de montee.
inline double
circle_of_confusion(const Settings& s, const double& raw)
{
    // Une profondeur en 1/z se lit a l'envers, et le zero y designe l'infini.
    double z = raw;
    if (s.depth_mode == 1) z = (raw > 1e-9) ? (1.0 / raw) : 1e9;

    const double delta = z - s.focus_distance;
    if (s.focus_side == 1 && delta <= 0.0) return 0.0;   // arriere seulement
    if (s.focus_side == 2 && delta >= 0.0) return 0.0;   // avant seulement

    double distance = fabs(delta) - s.focus_range;
    if (distance <= 0.0) return 0.0;                     // dans la zone nette

    // La montee se fait sur une seconde fois la profondeur de zone nette :
    // c'est ce qui donne une transition lisible plutot qu'un saut.
    const double span = (s.focus_range > 1e-6) ? s.focus_range * 2.0
                                               : (s.focus_distance * 0.25 + 1.0);
    double t = distance / span;
    if (t > 1.0) t = 1.0;
    return pow(t, s.blur_falloff);
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
    // A ce stade, seul source_image est valide dans le contexte : dest_image
    // porte un pointeur invalide et x0/y0 du bruit. On ne lit donc que ce qui
    // sert a dimensionner le noyau.
    Settings s;
    read_settings(eval, ctx, 0, s);

    // L'anamorphisme ne fait que COMPRIMER un axe : la portee reste le rayon.
    // L'aberration chromatique, elle, agrandit le noyau d'un canal.
    double reach = s.radius * (1.0 + CHROMA_SPREAD * fabs(s.chromatic));

    kernel_radius = (unsigned int)(reach < 0.0 ? 0.0 : reach + 1.5);
    total_pass_count = 1;

    // La passe de profondeur se lit ICI, et nulle part ailleurs.
    //
    // pre_filter tourne une seule fois, sur le thread appelant -- exactement
    // la ou LayerImage demande l'image d'un autre objet depuis toujours.
    // filter(), lui, tourne sur les threads du pool, une fois par tuile : y
    // demander l'evaluation d'une autre Image ferait se bousculer tous les
    // workers sur son verrou m_image_lock, qui n'est pas recursif.
    BokehModule *module = (BokehModule *) object.get_module();
    if (module == 0) return;
    module->depth = DepthSnapshot();
    if (s.radius < 0.5) return;

    OfAttr *attr = object.get_attribute("depth_image");
    if (attr == 0) return;
    OfObject *image_object = attr->get_object();
    if (image_object == 0) return;   // rien de branche : rayon constant

    // Cast VERIFIE. Isotropix caste sans verifier dans layer_builtin.dll ;
    // ici l'attribut pourrait un jour accepter ImageNode, dont le module
    // n'est pas un ModuleImage.
    ModuleImage *image = image_object->get_module<ModuleImage>();
    if (image == 0) return;

    // D'abord la voie qui ne declenche RIEN : get_highest_quality_image
    // balaie la pyramide et rend le meilleur niveau deja propre, sans verrou
    // ni evaluation. Elle ecrase la valeur d'entree de `quality` des sa
    // premiere instruction -- la documentation qui la presente comme une
    // qualite minimale requise est fausse.
    ImageHandle handle;
    ModuleImageQuality::Level got = ModuleImageQuality::QUALITY_UNKNOWN;

    if (!image->get_highest_quality_image(handle, got)) {
        // Rien n'est calcule. Sortir si l'evaluation est deja interrompue :
        // get_image rendrait une image incomplete, sans le dire.
        if (object.get_application().must_stop_evaluation()) return;

        ModuleImageQuality::Level want = ModuleImageQuality::QUALITY_FULL;
        if (ModuleImageQuality::is_valid_quality((unsigned int) ctx.image_quality))
            want = ModuleImageQuality::get_quality((unsigned int) ctx.image_quality);
        handle = image->get_image(want, false, 0);
    }

    ImageCanvas *canvas = *handle;
    // is_empty() ne suffit pas : le constructeur par defaut d'ImageHandle
    // alloue une Data, donc il rend faux sur un handle vide. La taille fait foi.
    if (canvas == 0 || canvas->get_width() <= 0) return;

    DepthSnapshot& depth = module->depth;
    depth.x = canvas->get_x();
    depth.y = canvas->get_y();
    depth.w = canvas->get_width();
    depth.h = canvas->get_height();
    if (depth.w <= 0 || depth.h <= 0) return;

    // Reference const sur la valeur de retour : la duree de vie du temporaire
    // est etendue, et aucune copie n'est faite -- ImageProxy n'a qu'un
    // constructeur de copie superficiel, qui provoquerait une double
    // liberation de ses cinq tampons.
    const ImageProxy& proxy = canvas->get_proxy(depth.x, depth.y, depth.w, depth.h);

    // Une passe de profondeur est monochrome : le rouge suffit. Les tampons
    // peuvent etre nuls -- le proxy n'alloue un canal que si la map le porte.
    const float *values = proxy.get_red_channel();
    if (values == 0) values = proxy.get_green_channel();
    if (values == 0) values = proxy.get_blue_channel();
    if (values == 0) return;

    depth.data.assign(values, values + (size_t) depth.w * depth.h);

    double low = depth.data[0], high = depth.data[0];
    for (size_t i = 1; i < depth.data.size(); ++i) {
        const float v = depth.data[i];
        if (v < low) low = v;
        if (v > high) high = v;
    }
    depth.near_value = low;
    depth.far_value = high;
    depth.ready = true;

    LOG_INFO("[Bokeh] profondeur " << depth.w << "x" << depth.h
             << " en (" << depth.x << "," << depth.y << ")"
             << "  etendue " << low << " a " << high << "\n");
}

bool
IX_MODULE_CLBK::filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx)
{
    const ImageProxy *src = ctx.image;
    if (src == 0) return true;

    // Les reglages sont relus ici, dans une copie locale, plutot que pris dans
    // un global. Un global est partage par toutes les instances : deux filtres
    // Bokeh dans la meme scene, ou la vue image et un instantane evalues en
    // parallele, s'ecraseraient mutuellement leurs valeurs. Le symptome serait
    // le pire qui soit -- des reglages qui ne font "pas tout a fait" ce qu'on
    // demande, par intermittence.
    Settings s;
    read_settings(eval, ctx, src, s);
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
    // Le neutre de la formule de reprise est gain = 0, pas 1 : le facteur
    // vaut 1 + gain * (luminance - seuil). Le declarer neutre a 1 faisait
    // sauter le facteur de x1 a x5 entre 1,000 et 1,001, et faisait prendre
    // le chemin lent -- dix fois plus couteux -- pour un resultat identique
    // au chemin rapide quand gain valait 0.
    const bool boosting = (s.gain != 0.0);

    const bool split = (s.chromatic != 0.0);
    const double channel_scale[3] = {
        1.0 + CHROMA_SPREAD * s.chromatic, 1.0, 1.0 - CHROMA_SPREAD * s.chromatic
    };

    // Une passe de profondeur branchee ? Alors le rayon varie par pixel, et on
    // bat une echelle de noyaux plutot qu'un seul. Le palier 0 est le pixel
    // net : aucun noyau, on recopie la source.
    const BokehModule *module = (const BokehModule *) object.get_module();
    const DepthSnapshot *depth = (module && module->depth.ready) ? &module->depth : 0;
    const int steps = depth ? DEPTH_STEPS : 1;

    std::vector<Kernel> ladder((size_t) steps * 3);
    for (int level = 0; level < steps; ++level) {
        Settings scaled = s;
        if (depth) scaled.radius = s.radius * (level + 1) / (double) steps;
        for (int c = 0; c < 3; ++c) {
            if (c != 1 && !split) continue;   // un seul noyau sans aberration
            build_kernel(ladder[(size_t) level * 3 + c], scaled,
                         channel_scale[c], frame_x, frame_y, boosting);
        }
    }
    if (ladder[1].total <= 0.0) return true;

    // Le canvas source, pour convertir les coordonnees de la tuile vers celles
    // de l'image de profondeur : les deux peuvent differer de resolution.
    double depth_u = 0.0, depth_v = 0.0;
    if (depth && ctx.source_image != 0) {
        depth_u = 1.0 / (double) ctx.source_image->get_width();
        depth_v = 1.0 / (double) ctx.source_image->get_height();
    }

    if (!boosting) {
        // Chemin rapide. On convolue les quatre canaux avec le meme noyau
        // normalise, alpha compris : une convolution est une moyenne ponderee
        // par la couverture, et l'alpha porte la couverture. Ne pas le flouter
        // laisserait une silhouette nette sur une image floue.
        Prefix prefix;
        for (int c = 0; c < 4; ++c) {
            if (channels[c] == 0 || out[c] == 0) continue;
            const int channel = (split && c < 3) ? c : 1;

            prefix.build(channels[c], stride, rows);
            // Le vignettage optique DOIT assombrir les coins -- c'est ce qu'il
            // est. Diviser par la somme locale des poids annulerait exactement
            // l'assombrissement et ne garderait que la forme d'amande : mesure
            // a 55,9 % d'energie dans le coin, et un gain de 1,000 quand meme.
            // On divise donc par la somme du noyau NON tronque, sauf si
            // l'utilisateur demande de preserver l'exposition.
            for (int y = 0; y < height; ++y) {
                const int cy = y + ctx.region.y;
                for (int x = 0; x < width; ++x) {
                    const int cx = x + ctx.region.x;

                    // Le palier de rayon, choisi par la profondeur au pixel.
                    int level = steps - 1;
                    if (depth != 0) {
                        const int abs_x = ctx.x0 + x;
                        const int abs_y = ctx.y0 + y;
                        const double u = (abs_x + 0.5 - ctx.source_image->get_x()) * depth_u;
                        const double v = (abs_y + 0.5 - ctx.source_image->get_y()) * depth_v;
                        const float z = depth->at(depth->x + (int)(u * depth->w),
                                                  depth->y + (int)(v * depth->h));
                        const double coc = circle_of_confusion(s, z);
                        level = (int)(coc * steps + 0.5) - 1;
                        if (level < 0) {
                            // Pixel net : rien a flouter, on recopie.
                            out[c][y * width + x] = channels[c][(size_t)cy * stride + cx];
                            continue;
                        }
                        if (level >= steps) level = steps - 1;
                    }

                    const Kernel& k = ladder[(size_t) level * 3 + channel];
                    if (k.total <= 0.0) {
                        out[c][y * width + x] = channels[c][(size_t)cy * stride + cx];
                        continue;
                    }
                    const double reference =
                        (s.preserve_exposure || k.total_unvignetted <= 0.0)
                        ? k.total : k.total_unvignetted;
                    const double inverse = 1.0 / reference;

                    double sum = 0.0;

                    // Convolution, pas correlation : le noyau est retourne.
                    // Sans ce retournement la PSF affichee est K(-d), donc un
                    // polygone impair sort tourne de 180 degres, et l'amande
                    // du vignettage se place du cote oppose au centre du
                    // cadre -- l'inverse de ce qu'une optique produit.
                    for (size_t i = 0; i < k.runs.size(); ++i) {
                        const Run& run = k.runs[i];
                        const int ry = cy - run.dy;
                        if (ry < 0 || ry >= rows) continue;
                        int x0 = cx - run.x1;
                        int x1 = cx - run.x0;
                        if (x1 < 0 || x0 >= stride) continue;
                        if (x0 < 0) x0 = 0;
                        if (x1 >= stride) x1 = stride - 1;
                        sum += prefix.span(ry, x0, x1);
                    }
                    sum *= k.level_weight;

                    for (size_t i = 0; i < k.edge.size(); ++i) {
                        const Tap& tap = k.edge[i];
                        const int ty = cy - tap.dy;
                        const int tx = cx - tap.dx;
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

    // Le critere de reprise se prend sur max(r, g, b), pas sur la luminance
    // ponderee : une lumiere bleue pure a une luma faible et passerait sous
    // le seuil alors qu'elle est eclatante.
    //
    // Et les canaux peuvent etre nuls : le moteur n'en alloue un que si la
    // map porte le nom correspondant. Sur un canvas en luminance seule, les
    // quatre le sont. Les lire sans garde plantait.
    const float *sr = channels[0], *sg = channels[1], *sb = channels[2];

    for (int y = 0; y < height; ++y) {
        const int cy = y + ctx.region.y;
        for (int x = 0; x < width; ++x) {
            const int cx = x + ctx.region.x;
            double sum[4] = {0.0, 0.0, 0.0, 0.0};
            double norm[4] = {0.0, 0.0, 0.0, 0.0};

            int level = steps - 1;
            if (depth != 0) {
                const double u = (ctx.x0 + x + 0.5 - ctx.source_image->get_x()) * depth_u;
                const double v = (ctx.y0 + y + 0.5 - ctx.source_image->get_y()) * depth_v;
                const float z = depth->at(depth->x + (int)(u * depth->w),
                                          depth->y + (int)(v * depth->h));
                level = (int)(circle_of_confusion(s, z) * steps + 0.5) - 1;
            }

            if (level < 0) {
                for (int c = 0; c < 4; ++c)
                    if (channels[c] != 0 && out[c] != 0)
                        out[c][y * width + x] =
                            channels[c][(size_t)(y + ctx.region.y) * stride + (x + ctx.region.x)];
                continue;
            }
            if (level >= steps) level = steps - 1;

            for (int c = 0; c < 4; ++c) {
                if (channels[c] == 0 || out[c] == 0) continue;
                const Kernel& k = ladder[(size_t) level * 3 + ((split && c < 3) ? c : 1)];
                for (size_t i = 0; i < k.all.size(); ++i) {
                    const Tap& tap = k.all[i];
                    const int ty = cy - tap.dy;
                    const int tx = cx - tap.dx;
                    if (ty < 0 || ty >= rows || tx < 0 || tx >= stride) continue;
                    const size_t o = (size_t)ty * stride + tx;

                    float peak = 0.0f;
                    if (sr != 0 && sr[o] > peak) peak = sr[o];
                    if (sg != 0 && sg[o] > peak) peak = sg[o];
                    if (sb != 0 && sb[o] > peak) peak = sb[o];

                    double w = tap.weight;
                    if (peak > threshold) w *= 1.0 + gain * (peak - threshold);
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
