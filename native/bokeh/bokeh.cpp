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
#include <image_map.h>
#include <image_map_channel.h>
#include <image_context.h>
#include <of_object.h>
#include <of_attr.h>
#include <module_scene_item.h>
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

// -- tranches correctives -----------------------------------------------------
//
// Une tranche est un intervalle de CoC signee et le rayon CONSTANT qu'on lui
// applique. Elles sont ordonnees de l'ARRIERE vers l'AVANT, ordre dans lequel
// elles se recomposent.
//
// Pourquoi decouper. Le flou en une passe prend son rayon dans la profondeur
// du pixel d'ARRIVEE. Or ramasser autour du pixel d'arrivee et epandre depuis
// le pixel de depart ne donnent le meme resultat que si le rayon est constant.
// Des qu'il varie, un premier plan flou ne peut plus deborder sur un
// arriere-plan net : les pixels de l'arriere-plan sont nets, donc recopies
// tels quels, et ne recoivent rien. La silhouette du premier plan reste dure
// la ou une vraie optique la dissout, et on ne voit pas au travers de son bord.
//
// A rayon constant, ramassage et epandage coincident de nouveau. D'ou le
// decoupage : chaque tranche est floutee SEULE, a rayon constant, en
// premultiplie -- couleur et couverture ensemble -- puis recomposee sur les
// precedentes. Une tranche floue etale aussi sa couverture, donc elle voile
// progressivement ce qui est derriere elle.
struct Slice {
    double lo, hi;   // bornes en CoC signee, hi exclu sauf pour la derniere
    double coc;      // rayon representatif, en fraction du rayon maximal
};

class BokehModule : public ModuleKernelFilter {
public:
    BokehModule() : ModuleKernelFilter(), focus_override(-1.0) {}
    DepthSnapshot depth;

    // Distance calculee depuis l'objet vise, ou -1 si aucun n'est renseigne.
    // Elle est resolue une fois dans pre_filter : remonter au layer, y lire la
    // camera et interroger deux transformations monde a chaque tuile serait du
    // gaspillage, et filter est multi-thread.
    double focus_override;

    // Decoupage en tranches, bati une fois par evaluation sur l'image ENTIERE.
    // Des bornes calculees par tuile donneraient des rayons differents de part
    // et d'autre d'une frontiere de tuile, donc une couture.
    std::vector<Slice> slices;
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
    CoreString depth_aov;       // nom du canal, vide = rayon constant
    int    depth_mode;          // 0 distance reelle, 1 inverse
    double focus_distance;
    double focus_override;      // depuis l'objet vise, ou -1
    double focus_range;
    double blur_falloff;
    int    focus_side;          // 0 les deux, 1 arriere, 2 avant
    double front_multiplier;    // flou du cote proche seulement
    double back_multiplier;     // flou du cote lointain seulement
    int    slices;              // tranches correctives ; 1 = passe unique
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
    s.depth_aov       = cma.get_depth_aov();
    s.depth_mode      = (int) cma.get_depth_mode();
    s.focus_distance  = cma.get_focus_distance();
    s.focus_override  = -1.0;   // renseigne par l'appelant, pas par le Cma
    s.focus_range     = cma.get_focus_range();
    s.blur_falloff    = cma.get_blur_falloff();
    s.focus_side      = (int) cma.get_focus_side();
    s.front_multiplier = cma.get_front_multiplier();
    s.back_multiplier  = cma.get_back_multiplier();
    s.slices          = (int) cma.get_corrective_slices();
    if (s.slices < 1)  s.slices = 1;
    if (s.slices > 64) s.slices = 64;

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

// Repere monde d'un objet de scene : sa position, et l'axe qu'il regarde.
//
// Une camera Clarisse vise son -Z local ; on rend donc l'oppose du troisieme
// axe pour obtenir une direction de visee franche. extract_frame decompose la
// matrice, les axes rendus sont donc orthonormes meme si l'objet porte une
// echelle ou un cisaillement.
bool
world_frame(OfObject *item, GMathVec3d& position, GMathVec3d& forward)
{
    if (item == 0) return false;
    ModuleSceneItem *module = item->get_module<ModuleSceneItem>();
    if (module == 0) return false;
    const GMathMatrix4x4d& matrix = module->get_global_matrix();
    matrix.extract_translation(position);
    GMathVec3d ax, ay, az;
    matrix.extract_frame(ax, ay, az);
    forward = -az;
    return true;
}

// Profondeur de l'objet vise, dans l'unite meme de l'AOV.
//
// Le filtre est embarque dans un layer ; get_parent() rend ce layer, dont
// l'attribut active_camera designe la camera. Sans elle on ne peut rien
// calculer -- une mise au point n'a de sens que depuis un point de vue.
//
// Le point important est l'unite. L'AOV `depth.Z` de Clarisse porte la
// profondeur PROJETEE sur l'axe de visee, pas la distance au point de vue :
// mesure sur une sphere posee a 14 unites devant la camera mais decalee de 5
// sur le cote, l'AOV rend 12.7 la ou la distance vaut 13.6. Comparer une
// distance euclidienne a cette profondeur ferait donc deriver la mise au point
// des que l'objet quitte le centre du cadre -- et d'autant plus que le champ
// est large. On projette.
//
// C'est aussi le calcul juste optiquement : une lentille mince fait le point
// sur un PLAN parallele au capteur, pas sur une sphere centree sur l'oeil.
//
// Rend -1 quand la chaine ne se resout pas, ou quand l'objet est derriere la
// camera : on retombe alors sur la distance saisie a la main.
double
focus_distance_from_object(OfObject& filter_object)
{
    OfAttr *target_attr = filter_object.get_attribute("focus_object");
    if (target_attr == 0) return -1.0;
    OfObject *target = target_attr->get_object();
    if (target == 0) return -1.0;

    OfObject *layer = filter_object.get_parent();
    if (layer == 0) return -1.0;
    OfAttr *camera_attr = layer->get_attribute("active_camera");
    if (camera_attr == 0) return -1.0;
    OfObject *camera = camera_attr->get_object();
    if (camera == 0) return -1.0;

    GMathVec3d eye, gaze, aim, unused;
    if (!world_frame(camera, eye, gaze)) return -1.0;
    if (!world_frame(target, aim, unused)) return -1.0;

    const double depth = (aim - eye).dot(gaze);
    return (depth > 0.0) ? depth : -1.0;
}

// Retrouve le canal de profondeur a partir du nom de groupe rendu par le tag.
//
// Le nom exact d'abord -- un AOV custom peut porter un nom simple -- puis les
// composantes usuelles, puis n'importe quel canal du groupe. Le repli final
// est le canal Z standard, pour les images qui en portent un sans passer par
// les AOV.
ImageMapChannel *
find_depth_channel(const ImageMap& map, const CoreString& group)
{
    if (group.get_count() != 0) {
        ImageMapChannel *exact = map.get_channel_by_name(group);
        if (exact != 0) return exact;

        const char *suffixes[] = {".z", ".Z", ".depth", ".r", ".R"};
        for (int i = 0; i < 5; ++i) {
            CoreString candidate = group;
            candidate += suffixes[i];
            ImageMapChannel *found = map.get_channel_by_name(candidate);
            if (found != 0) return found;
        }

        // N'importe quelle composante du groupe : un AOV peut nommer la sienne
        // autrement que par les suffixes attendus.
        CoreString prefix = group;
        prefix += ".";
        const CoreVector<ImageMapChannel *>& all = map.get_channels();
        for (unsigned int i = 0; i < all.get_count(); ++i) {
            if (all[i] == 0) continue;
            const CoreString& name = all[i]->get_name();
            if (name.get_count() > prefix.get_count()
                && name.sub_string(0, prefix.get_count()) == prefix)
                return all[i];
        }
    }
    return map.get_channel(ImageMap::CHANNEL_Z);
}

// Fraction SIGNEE du rayon maximal a appliquer pour une valeur de profondeur.
// Negative devant le plan de nettete, positive derriere, nulle dans la zone
// nette, bornee a l'unite.
//
// Ce n'est pas la formule optique du cercle de confusion : celle-ci demande
// une focale, une ouverture et une taille de capteur, dont un filtre 2D ne
// dispose pas. C'est le modele artistique, celui de tous les outils de
// compositing -- un plan de mise au point, une profondeur de zone nette, et
// une courbe de montee.
//
// Le SIGNE n'est pas decoratif : c'est lui qui rend la fonction MONOTONE en
// profondeur. La grandeur non signee est en V -- elle redescend a zero au
// point de nettete puis remonte -- si bien qu'un premier plan et un
// arriere-plan y prennent la meme valeur. Trancher la profondeur sur cette
// grandeur melangerait les deux dans la meme tranche, exactement ce que les
// tranches doivent separer. Signee, l'ordre des valeurs est l'ordre optique.
double
signed_coc(const Settings& s, const double& raw)
{
    // Zero ne veut pas dire "a distance nulle" mais "aucune geometrie
    // touchee" : le fond d'un rendu. Le traiter comme un objet colle a la
    // camera lui donnerait le flou maximal, ce qui est faux des que la mise
    // au point est lointaine. On le place a l'infini, ou il doit etre.
    //
    // En mode inverse, c'est deja la convention : 1/z tend vers zero quand z
    // tend vers l'infini.
    double z = raw;
    if (s.depth_mode == 1) z = (raw > 1e-9) ? (1.0 / raw) : 1e9;
    else if (raw <= 1e-9) z = 1e9;

    const double focus = (s.focus_override > 0.0) ? s.focus_override
                                                  : s.focus_distance;
    const double delta = z - focus;
    const bool   front = (delta < 0.0);

    if (s.focus_side == 1 && front)  return 0.0;   // arriere seulement
    if (s.focus_side == 2 && !front) return 0.0;   // avant seulement

    double distance = fabs(delta) - s.focus_range;
    if (distance <= 0.0) return 0.0;               // dans la zone nette

    // La montee se fait sur une seconde fois la profondeur de zone nette :
    // c'est ce qui donne une transition lisible plutot qu'un saut.
    //
    // Le repli quand la zone nette est nulle se prend sur la mise au point
    // EFFECTIVE, objet vise compris : le prendre sur la distance saisie a la
    // main donnait une portee calculee autour de 10 unites alors que le point
    // etait fait a 40.
    const double span = (s.focus_range > 1e-6) ? s.focus_range * 2.0
                                               : (focus * 0.25 + 1.0);
    double t = distance / span;
    if (t > 1.0) t = 1.0;

    // Les multiplicateurs par cote. Regler separement l'avant et l'arriere est
    // le geste le plus courant : un premier plan flou se remarque bien plus
    // qu'un arriere-plan flou, et on veut souvent le retenir sans toucher au
    // decor.
    double coc = pow(t, s.blur_falloff) *
                 (front ? s.front_multiplier : s.back_multiplier);
    if (coc > 1.0) coc = 1.0;
    if (coc < 0.0) coc = 0.0;

    return front ? -coc : coc;
}

// Fraction non signee, pour la passe unique qui n'a pas besoin de l'ordre.
inline double
circle_of_confusion(const Settings& s, const double& raw)
{
    const double c = signed_coc(s, raw);
    return c < 0.0 ? -c : c;
}

// Construit le decoupage a partir de l'etendue REELLE de la CoC signee dans
// l'image.
//
// Deux exigences se rejoignent ici.
//
// D'abord le decoupage doit etre GLOBAL, identique pour toutes les tuiles :
// des bornes calculees par tuile donneraient des rayons differents de part et
// d'autre d'une frontiere de tuile, donc une couture visible. D'ou le calcul
// dans pre_filter, qui voit l'image entiere, et pas dans filter.
//
// Ensuite la zone nette a sa propre tranche, de rayon exactement nul. Sans
// elle, la tranche qui contient le zero prend le rayon de son milieu et floute
// ce qui doit rester parfaitement net -- sur un sujet net devant un fond
// lointain, c'est le sujet qui perdrait son piqué.
void
build_slices(std::vector<Slice>& slices, const double& smin, const double& smax,
             const int& requested)
{
    slices.clear();
    const double EPS = 1e-6;

    const bool has_front = (smin < -EPS);
    const bool has_back  = (smax >  EPS);

    // Rien de flou : une seule tranche nette suffit.
    if (!has_front && !has_back) {
        Slice sharp = {-1.0, 1.0, 0.0};
        slices.push_back(sharp);
        return;
    }

    // Une tranche est reservee au net ; les autres se partagent les deux cotes
    // au prorata de l'etendue que chacun couvre, pour que le pas de rayon soit
    // le meme partout.
    int budget = requested - 1;
    if (budget < 1) budget = 1;

    const double front_span = has_front ? -smin : 0.0;
    const double back_span  = has_back  ?  smax : 0.0;
    const double total_span = front_span + back_span;

    int n_front = 0, n_back = 0;
    if (has_front && has_back) {
        n_front = (int)(budget * front_span / total_span + 0.5);
        if (n_front < 1) n_front = 1;
        if (n_front > budget - 1) n_front = budget - 1;
        n_back = budget - n_front;
    } else if (has_front) {
        n_front = budget;
    } else {
        n_back = budget;
    }

    // De l'arriere vers l'avant : les CoC signees decroissantes.
    for (int i = n_back - 1; i >= 0; --i) {
        Slice sl;
        sl.lo = back_span * i / n_back;
        sl.hi = (i == n_back - 1) ? (smax + 1.0) : back_span * (i + 1) / n_back;
        if (i == 0) sl.lo = EPS;   // le net a sa tranche, pas celle-ci
        sl.coc = back_span * (i + 0.5) / n_back;
        slices.push_back(sl);
    }

    Slice sharp = {-EPS, EPS, 0.0};
    slices.push_back(sharp);

    for (int i = 0; i < n_front; ++i) {
        Slice sl;
        sl.hi = -front_span * i / n_front;
        sl.lo = (i == n_front - 1) ? (smin - 1.0) : -front_span * (i + 1) / n_front;
        if (i == 0) sl.hi = -EPS;
        sl.coc = front_span * (i + 0.5) / n_front;
        slices.push_back(sl);
    }
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

// -- flou d'un plan a rayon constant ------------------------------------------

// Recopie la tuile depuis le plan source, sans flou. C'est le cas de la tranche
// nette, qui doit rester exactement telle quelle.
void
copy_plane(const float *plane, const int& stride, const CtxKernelFilter& ctx,
           float *dest)
{
    const int width  = ctx.region.width;
    const int height = ctx.region.height;
    for (int y = 0; y < height; ++y) {
        const float *row = plane + (size_t)(y + ctx.region.y) * stride + ctx.region.x;
        float *d = dest + (size_t) y * width;
        for (int x = 0; x < width; ++x) d[x] = row[x];
    }
}

// Convolue un plan de la taille du proxy vers un plan de la taille de la tuile.
// Le rayon est CONSTANT, ce qui est toute la raison d'etre des tranches :
// ramassage et epandage ne coincident qu'a rayon constant.
void
blur_plane(const float *plane, const int& stride, const int& rows,
           const CtxKernelFilter& ctx, const Kernel& k,
           const bool& preserve_exposure, float *dest)
{
    const int width  = ctx.region.width;
    const int height = ctx.region.height;

    // Le vignettage optique DOIT assombrir les coins. Diviser par la somme
    // locale des poids l'annulerait exactement et ne laisserait que la forme
    // d'amande : on divise par la somme du noyau NON tronque.
    const double reference = (preserve_exposure || k.total_unvignetted <= 0.0)
                             ? k.total : k.total_unvignetted;
    const double inverse = (reference > 0.0) ? (1.0 / reference) : 0.0;

    Prefix prefix;
    prefix.build(plane, stride, rows);

    for (int y = 0; y < height; ++y) {
        const int cy = y + ctx.region.y;
        for (int x = 0; x < width; ++x) {
            const int cx = x + ctx.region.x;
            double sum = 0.0;

            // Convolution, pas correlation : le noyau est retourne.
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
                sum += plane[(size_t)ty * stride + tx] * tap.weight;
            }

            dest[(size_t) y * width + x] = (float)(sum * inverse);
        }
    }
}

// -- la passe a tranches ------------------------------------------------------
//
// Chaque tranche est extraite en premultiplie -- couleur ET couverture --
// floutee seule a rayon constant, puis reportee sur l'accumulation par un
// simple "over". La couverture floutee est ce qui rend le bord juste : elle
// s'etale avec la couleur, donc une tranche floue voile progressivement ce qui
// est derriere elle au lieu de s'arreter sur sa silhouette d'origine.
//
// L'ordre est celui du tableau : de l'arriere vers l'avant.
bool
filter_sliced(const Settings& s, const CtxKernelFilter& ctx, const ImageProxy& src,
              const DepthSnapshot& depth, const std::vector<Slice>& slices,
              const float *const *channels, float *const *out,
              const double& frame_x, const double& frame_y,
              const bool& split, const double *channel_scale)
{
    const int stride = (int) src.get_width();
    const int rows   = (int) src.get_height();
    const int width  = ctx.region.width;
    const int height = ctx.region.height;
    if (stride <= 0 || rows <= 0 || width <= 0 || height <= 0) return true;

    const size_t source_count = (size_t) stride * rows;
    const size_t dest_count   = (size_t) width * height;

    // La CoC signee de chaque pixel source, calculee une fois. Chaque tranche
    // n'a plus qu'a comparer deux bornes au lieu de refaire la conversion de
    // profondeur autant de fois qu'il y a de tranches.
    std::vector<float> sc(source_count);
    for (int py = 0; py < rows; ++py) {
        const int ay = ctx.y0 + py - ctx.region.y;
        for (int px = 0; px < stride; ++px) {
            const int ax = ctx.x0 + px - ctx.region.x;
            sc[(size_t) py * stride + px] = (float) signed_coc(s, depth.at(ax, ay));
        }
    }

    // Reprise des hautes lumieres : un facteur par pixel source, applique a la
    // couleur avant le flou et PAS a la couverture. Une lumiere vive s'etale
    // donc plus fort sans devenir plus opaque -- c'est ce qui donne des boules
    // franches. Le critere se prend sur max(r, g, b) et non sur la luminance
    // ponderee : une lumiere bleue pure a une luma faible et passerait sous le
    // seuil alors qu'elle est eclatante.
    const bool boosting = (s.gain != 0.0);
    std::vector<float> boost;
    if (boosting) {
        boost.assign(source_count, 1.0f);
        const float threshold = (float) s.threshold;
        const float gain = (float) s.gain;
        for (size_t i = 0; i < source_count; ++i) {
            float peak = 0.0f;
            for (int c = 0; c < 3; ++c)
                if (channels[c] != 0 && channels[c][i] > peak) peak = channels[c][i];
            if (peak > threshold) boost[i] = 1.0f + gain * (peak - threshold);
        }
    }

    std::vector<float> acc(dest_count * 4, 0.0f);
    std::vector<float> plane(source_count);
    std::vector<float> blurred(dest_count);
    std::vector<float> cover(dest_count);

    for (size_t si = 0; si < slices.size(); ++si) {
        const Slice& sl = slices[si];

        // Cette tranche touche-t-elle seulement la tuile elargie ? La plupart
        // n'y sont pas, et le test coute une passe lineaire la ou le flou
        // couterait un noyau entier par pixel.
        bool present = false;
        for (size_t i = 0; i < source_count; ++i) {
            if (sc[i] >= sl.lo && sc[i] < sl.hi) { present = true; break; }
        }
        if (!present) continue;

        Settings scaled = s;
        scaled.radius = s.radius * sl.coc;

        Kernel kern[3];
        bool sharp = (scaled.radius < 0.5);
        if (!sharp) {
            for (int c = 0; c < 3; ++c) {
                if (c != 1 && !split) continue;
                build_kernel(kern[c], scaled, channel_scale[c],
                             frame_x, frame_y, false);
            }
            if (kern[1].total <= 0.0) sharp = true;
        }

        // La couverture d'abord : c'est elle qui pilote le report, donc elle
        // doit etre connue avant d'ecrire le moindre canal.
        for (size_t i = 0; i < source_count; ++i) {
            if (sc[i] < sl.lo || sc[i] >= sl.hi) { plane[i] = 0.0f; continue; }
            plane[i] = (channels[3] != 0) ? channels[3][i] : 1.0f;
        }
        // La couverture se normalise TOUJOURS sur la somme reelle du noyau,
        // jamais sur la somme non vignettee. Le vignettage assombrit la
        // lumiere, il ne perce pas la matiere : une couverture reduite par le
        // vignettage rendrait chaque tranche partiellement transparente, les
        // tranches cesseraient de se couvrir et l'image se creuserait.
        if (sharp) copy_plane(&plane[0], stride, ctx, &cover[0]);
        else       blur_plane(&plane[0], stride, rows, ctx, kern[1],
                              true, &cover[0]);

        // Une couverture hors de [0, 1] inverserait le report : le facteur
        // (1 - couverture) deviendrait negatif.
        for (size_t i = 0; i < dest_count; ++i) {
            if (cover[i] < 0.0f) cover[i] = 0.0f;
            else if (cover[i] > 1.0f) cover[i] = 1.0f;
        }

        for (int c = 0; c < 4; ++c) {
            if (channels[c] == 0) continue;

            for (size_t i = 0; i < source_count; ++i) {
                if (sc[i] < sl.lo || sc[i] >= sl.hi) { plane[i] = 0.0f; continue; }
                plane[i] = (boosting && c < 3) ? channels[c][i] * boost[i]
                                               : channels[c][i];
            }
            if (sharp) copy_plane(&plane[0], stride, ctx, &blurred[0]);
            else       blur_plane(&plane[0], stride, rows, ctx,
                                  kern[(split && c < 3) ? c : 1],
                                  s.preserve_exposure, &blurred[0]);

            float *dest = &acc[(size_t) c * dest_count];
            for (size_t i = 0; i < dest_count; ++i)
                dest[i] = blurred[i] + dest[i] * (1.0f - cover[i]);
        }
    }

    for (int c = 0; c < 4; ++c) {
        if (out[c] == 0 || channels[c] == 0) continue;
        const float *a = &acc[(size_t) c * dest_count];
        for (size_t i = 0; i < dest_count; ++i) out[c][i] = a[i];
    }
    return true;
}

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

    // La passe de profondeur est un AOV de l'image filtree elle-meme.
    //
    // C'est ainsi que procede le denoiser OptiX de Clarisse : il declare un
    // `tag` filtre sur `aov_groups` et lit le canal par son nom. Le canal
    // vient donc du meme ImageMap que les couleurs -- il est deja aligne, a la
    // meme resolution, sans rien a rechantillonner.
    //
    // ImageProxy, lui, ne donne acces qu'a r, g, b, a et l : il n'alloue un
    // tampon que pour ces cinq noms. Un AOV s'atteint par l'ImageMap du
    // canvas, et se lit en tampon flottant.
    //
    // Ici, et pas dans filter : pre_filter tourne une seule fois, sur le
    // thread appelant. filter tourne sur les threads du pool, une fois par
    // tuile -- y refaire ce travail le referait des centaines de fois.
    BokehModule *module = (BokehModule *) object.get_module();
    if (module == 0) return;
    module->depth = DepthSnapshot();

    // La mise au point sur un objet, resolue une fois par evaluation.
    module->focus_override = focus_distance_from_object(object);

    // Et dans la copie locale, sans quoi tout ce qui suit -- le decoupage en
    // tranches en particulier -- se calculerait sur la distance saisie a la
    // main pendant que le rendu, lui, ferait le point sur l'objet. Les bornes
    // des tranches ne correspondraient plus aux rayons appliques.
    s.focus_override = module->focus_override;

    if (module->focus_override > 0.0)
        LOG_INFO("[Bokeh] mise au point sur l'objet : "
                 << module->focus_override << " unites\n");

    // Viser un objet sans avoir branche d'AOV ne produit RIEN de visible : le
    // filtre ignore alors la profondeur et floute tout uniformement. L'echec
    // est silencieux et se cherche longtemps, alors on le dit.
    if (module->focus_override > 0.0 && s.depth_aov.get_count() == 0)
        LOG_WARNING("[Bokeh] un objet de mise au point est vise, mais aucun AOV "
                    "de profondeur n'est choisi : le flou restera uniforme. "
                    "Activez l'AOV depth sur le Layer 3D, puis choisissez-le "
                    "dans Focus > Depth AOV.\n");

    if (s.radius < 0.5 || s.depth_aov.get_count() == 0) return;
    if (ctx.source_image == 0) return;

    ImageMap *map = ctx.source_image->get_image();
    if (map == 0) return;

    // L'attribut `tag` filtre sur `aov_groups` rend un nom de GROUPE -- par
    // exemple "depth". Le canal, lui, porte le nom du groupe suivi de sa
    // composante : "depth.z". Chercher le nom tel quel echoue donc toujours,
    // et le filtre retombe en silence sur un rayon constant.
    ImageMapChannel *channel = find_depth_channel(*map, s.depth_aov);
    if (channel == 0) {
        // Dire ce qui EST disponible : c'est la seule information utile quand
        // l'AOV demande n'est pas la, et elle evite d'aller la chercher a
        // taton dans l'interface.
        CoreString available;
        const CoreVector<ImageMapChannel *>& all = map->get_channels();
        for (unsigned int i = 0; i < all.get_count(); ++i) {
            if (all[i] == 0) continue;
            if (available.get_count()) available += ", ";
            available += all[i]->get_name();
        }
        LOG_INFO("[Bokeh] AOV '" << s.depth_aov << "' introuvable. Disponibles : "
                 << available << "\n");
        return;
    }

    DepthSnapshot& depth = module->depth;
    depth.x = ctx.source_image->get_x();
    depth.y = ctx.source_image->get_y();
    depth.w = ctx.source_image->get_width();
    depth.h = ctx.source_image->get_height();
    if (depth.w <= 0 || depth.h <= 0) return;

    depth.data.resize((size_t) depth.w * depth.h);
    ImageEvalContext eval_context(*ctx.source_image, 0);
    channel->create_float_buffer(&eval_context, depth.x, depth.y,
                                 (unsigned int) depth.w, (unsigned int) depth.h,
                                 &depth.data[0]);

    // Redresser la profondeur des pixels de silhouette, en divisant par l'alpha.
    //
    // Un pixel de bord anticrenele porte une profondeur MOYENNEE sur ses
    // echantillons, et les echantillons de fond y comptent pour zero. Le filtre
    // de pixel ecrit donc `profondeur = couverture x z_geometrie`, et non
    // z_geometrie. Sur une sphere a 12,7 unites devant un fond vide, un pixel
    // couvert a moitie rend 6,3 : une profondeur deux fois plus proche que tout
    // ce que contient la scene. Ces valeurs fantomes se classent parmi les plus
    // floues, donc s'etalent au maximum et se recomposent en dernier, par-dessus
    // tout le reste -- un liseré autour de chaque silhouette.
    //
    // Mesure sur le banc d'essai : la CoC signee descendait a -0,61 dans une
    // scene ou rien n'est devant le point de nettete.
    //
    // La couverture, c'est l'alpha, ecrit par le meme filtre sur les memes
    // echantillons. Diviser par lui rend donc z_geometrie EXACTEMENT, et pas
    // approximativement. Un median 3x3 a ete essaye d'abord et ne suffit pas :
    // sur une silhouette large de deux pixels, la mediane du voisinage est
    // encore un melange.
    //
    // Un pixel a cheval sur DEUX geometries opaques garde son alpha a 1 et n'est
    // pas touche : sa profondeur est alors une moyenne de deux valeurs reelles,
    // donc comprise entre elles, ce qui est inoffensif.
    ImageMapChannel *alpha_channel = map->get_channel(ImageMap::CHANNEL_A);
    if (alpha_channel != 0) {
        std::vector<float> alpha((size_t) depth.w * depth.h);
        alpha_channel->create_float_buffer(&eval_context, depth.x, depth.y,
                                           (unsigned int) depth.w,
                                           (unsigned int) depth.h, &alpha[0]);
        for (size_t i = 0; i < depth.data.size(); ++i) {
            const float a = alpha[i];
            if (a > 1e-4f && a < 0.999f) depth.data[i] /= a;
        }
    }

    double low = depth.data[0], high = depth.data[0];
    for (size_t i = 1; i < depth.data.size(); ++i) {
        const float v = depth.data[i];
        if (v < low) low = v;
        if (v > high) high = v;
    }
    depth.near_value = low;
    depth.far_value = high;
    depth.ready = true;

    LOG_INFO("[Bokeh] AOV '" << s.depth_aov << "' : " << depth.w << "x" << depth.h
             << ", etendue " << low << " a " << high << "\n");

    // Le decoupage en tranches, ici et pas dans filter : il doit etre le meme
    // pour toutes les tuiles, sans quoi deux tuiles voisines appliqueraient des
    // rayons differents a la meme profondeur et la couture se verrait.
    //
    // On mesure l'etendue de la CoC signee sur l'image entiere plutot que de
    // prendre [-1, 1] par principe : une scene sans premier plan flou
    // n'occupe que la moitie de cet intervalle, et la moitie des tranches
    // seraient vides. A etendue mesuree, toutes servent.
    module->slices.clear();
    if (s.slices >= 2) {
        double smin = 0.0, smax = 0.0;
        for (size_t i = 0; i < depth.data.size(); ++i) {
            const double c = signed_coc(s, depth.data[i]);
            if (c < smin) smin = c;
            if (c > smax) smax = c;
        }
        build_slices(module->slices, smin, smax, s.slices);
        LOG_INFO("[Bokeh] " << (int) module->slices.size()
                 << " tranche(s) correctives, CoC signee de " << smin
                 << " a " << smax << "\n");
    }
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

    // La distance calculee depuis l'objet vise a ete resolue dans pre_filter :
    // remonter au layer et interroger deux transformations monde a chaque
    // tuile serait du gaspillage.
    const BokehModule *owner = (const BokehModule *) object.get_module();
    if (owner != 0) s.focus_override = owner->focus_override;
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

    // Le chemin a tranches, quand il y en a plus d'une. Il est plus cher --
    // grossierement proportionnel au nombre de tranches -- mais c'est le seul
    // qui rende les bords justes la ou deux profondeurs se cotoient. La passe
    // unique ci-dessous reste pour qui prefere la vitesse, et c'est aussi le
    // repli quand aucune profondeur n'est branchee.
    if (depth != 0 && module->slices.size() >= 2)
        return filter_sliced(s, ctx, *src, *depth, module->slices,
                             channels, out, frame_x, frame_y,
                             split, channel_scale);

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

    // Aucune conversion de coordonnees : l'AOV vient du meme ImageMap que les
    // couleurs, donc a la meme resolution et dans le meme repere. ctx.x0 et
    // ctx.y0 sont deja en coordonnees image absolues.

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
                        const float z = depth->at(ctx.x0 + x, ctx.y0 + y);
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
                const float z = depth->at(ctx.x0 + x, ctx.y0 + y);
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
