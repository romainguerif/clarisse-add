// Bokeh -- flou de mise au point optique pour Clarisse.
//
// Ce que Defocus Blur ne fait pas, et qui fait toute la difference :
//
//   - le noyau reproduit un vrai diaphragme (lames, courbure, rotation,
//     anamorphisme) au lieu d'un disque approximatif ;
//   - les hautes lumieres se rassemblent en boules nettes au lieu d'etre
//     etalees, par une moyenne ponderee par l'energie ;
//   - les defauts d'optique qui font le realisme sont exposes : vignettage
//     optique, aberration spherique, aberration chromatique.
//
// -- Le contrat de CtxKernelFilter, mesure -----------------------------------
//
// Aucune de ces informations n'est documentee : les pages Doxygen de
// CtxKernelFilter listent les champs et laissent toutes les descriptions
// vides. Ce qui suit a ete releve en instrumentant le filtre.
//
//   ctx.image     proxy de la SOURCE, de taille tuile + 2 x kernel_radius
//                 (88x88 pour une tuile de 64 et un rayon de 12). Clarisse
//                 fournit donc lui-meme la marge du noyau : inutile d'aller
//                 chercher une region elargie avec ImageCanvas::get_proxy.
//   ctx.region    la tuile a ecrire, en coordonnees DU PROXY : {12,12,64,64}.
//                 region.x et region.y valent exactement kernel_radius.
//   ctx.channel_* la DESTINATION, region.width x region.height, indexee
//                 y * region.width + x, sans marge. Elle arrive pre-remplie
//                 avec la source : un filtre qui n'ecrit rien ne casse rien.
//   ctx.x0, y0    la tuile en coordonnees image = position(proxy) + region.
//   pre_filter    n'a que source_image de valide ; dest_image et x0/y0 y sont
//                 du bruit, les lire plante.
//   threads       les tuiles arrivent dans le desordre, sur autant de threads
//                 que de coeurs. Chaque appel ne touche que sa tuile.
//
// -- Sur la lumiere ----------------------------------------------------------
//
// Tout le calcul se fait en lineaire. Clarisse travaille en flottant lineaire,
// et c'est indispensable : c'est parce que la source contient des valeurs bien
// au-dessus de 1 qu'un point brillant, etale sur le disque, reste plus clair
// que son voisinage et forme une boule. En gamma, la meme operation ecrase
// tout vers le gris.

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
    // (module_kernel_filter.h:33-35). L'oublier ne degraderait pas le
    // resultat, ca planterait.
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

// -- reglages, lus une fois et partages entre les threads ---------------------

namespace {

struct Settings {
    double radius;          // en pixels de l'image evaluee
    int    blades;
    double rotation;        // radians
    double roundness;       // -1 concave .. 0 polygone .. 1 disque
    double anamorphism;     // -1 etirement horizontal .. 1 vertical
    double threshold;
    double gain;
    double vignetting;
    double spherical;
    double chromatic;
    int    image_width;     // pour situer le pixel dans le cadre
    int    image_height;
};

Settings g_settings;

// Une entree du noyau : un decalage et son poids.
struct Tap {
    int   dx;
    int   dy;
    float weight;
};

const double PI = 3.14159265358979323846;

// Rayon de l'ouverture dans la direction `angle`, en fraction du rayon maximal.
//
// A zero lame, l'ouverture est un disque : la fonction rend 1 partout. C'est
// ce que donne un objectif a pleine ouverture, diaphragme escamote.
//
// A N lames, l'ouverture est un polygone regulier. Le rayon d'un polygone
// inscrit vaut cos(pi/N) / cos(theta' - pi/N), ou theta' est l'angle ramene
// dans un secteur. Il vaut 1 aux sommets et cos(pi/N) au milieu des cotes.
//
// La courbure des lames se lit ainsi : vers le disque, on interpole vers 1 ;
// vers l'etoile, on eleve le rayon au carre. Comme il est inferieur a 1, le
// carre creuse davantage le milieu des cotes que les sommets -- exactement le
// creusement des lames concaves des optiques anciennes.
inline double
aperture_radius(const double& angle, const int& blades, const double& rotation,
                const double& roundness)
{
    if (blades < 3) return 1.0;

    const double sector = 2.0 * PI / blades;
    double local = fmod(angle - rotation, sector);
    if (local < 0.0) local += sector;

    const double half = PI / blades;
    const double poly = cos(half) / cos(local - half);

    if (roundness > 0.0) return poly + (1.0 - poly) * roundness;
    if (roundness < 0.0) return poly + (poly * poly - poly) * (-roundness);
    return poly;
}

// Construit le noyau pour une position donnee dans le cadre.
//
// Le noyau depend de l'endroit du cadre a cause du vignettage optique, qui
// n'est pas uniforme : il est nul au centre et maximal dans les coins. On le
// reconstruit donc par tuile, evalue en son centre. Le faire par pixel serait
// plus juste et cent fois plus lent, pour une difference invisible sur une
// tuile de 64 pixels -- le vignettage varie a l'echelle du cadre entier.
//
// `channel_scale` porte l'aberration chromatique : un rayon legerement
// different par canal, ce qui colore le bord des boules.
void
build_kernel(std::vector<Tap>& taps, const Settings& s, const double& channel_scale,
             const double& frame_x, const double& frame_y)
{
    taps.clear();
    const double radius = s.radius * channel_scale;
    if (radius < 0.5) return;

    const int reach = (int)(radius + 0.5);

    // Anamorphisme : on comprime les coordonnees d'echantillonnage sur un axe,
    // ce qui etire la forme obtenue sur l'autre.
    double scale_x = 1.0, scale_y = 1.0;
    if (s.anamorphism > 0.0) scale_x = 1.0 + s.anamorphism;      // boules verticales
    else if (s.anamorphism < 0.0) scale_y = 1.0 - s.anamorphism; // boules horizontales

    // Vignettage optique : vers les bords du cadre, le barillet rogne le
    // faisceau. On modelise l'ouverture comme l'intersection du disque avec un
    // second disque decale vers le centre de l'image -- ce qui donne
    // l'amande, ou oeil-de-chat, orientee vers le centre.
    const double offset = s.vignetting * sqrt(frame_x * frame_x + frame_y * frame_y);
    double shift_x = 0.0, shift_y = 0.0;
    if (offset > 1e-6) {
        const double norm = sqrt(frame_x * frame_x + frame_y * frame_y);
        shift_x = -frame_x / norm * offset;
        shift_y = -frame_y / norm * offset;
    }

    // Largeur du fondu de bord, en fraction du rayon : un pixel. Sans lui, le
    // test d'appartenance est binaire et les boules ressortent crenelees --
    // tres visible, justement parce qu'elles ont des bords nets.
    const double feather = 1.0 / radius;

    double total = 0.0;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const double ux = dx * scale_x / radius;
            const double uy = dy * scale_y / radius;
            const double rho = sqrt(ux * ux + uy * uy);
            if (rho > 1.0 + feather) continue;

            const double angle = atan2(uy, ux);
            const double edge = aperture_radius(angle, s.blades, s.rotation, s.roundness);

            // Couverture partielle sur le bord, au lieu d'un oui/non.
            double coverage = (edge - rho) / feather + 0.5;
            if (coverage <= 0.0) continue;
            if (coverage > 1.0) coverage = 1.0;

            // Vignettage : le second disque, decale.
            if (offset > 1e-6) {
                const double vx = ux - shift_x;
                const double vy = uy - shift_y;
                double cut = (1.0 - sqrt(vx * vx + vy * vy)) / feather + 0.5;
                if (cut <= 0.0) continue;
                if (cut > 1.0) cut = 1.0;
                coverage *= cut;
            }

            // Aberration spherique : l'energie migre vers le bord (anneau
            // lumineux, dit bulle de savon) ou vers le centre (bokeh doux des
            // optiques a portrait). Le facteur reste positif.
            double weight = coverage;
            if (s.spherical != 0.0) {
                const double bias = 1.0 + s.spherical * (2.0 * (rho / (edge > 1e-6 ? edge : 1.0)) - 1.0);
                weight *= (bias < 0.0 ? 0.0 : bias);
            }

            if (weight <= 0.0) continue;
            Tap tap;
            tap.dx = dx;
            tap.dy = dy;
            tap.weight = (float) weight;
            taps.push_back(tap);
            total += weight;
        }
    }

    // Normalisation : la somme des poids vaut 1, donc un aplat garde sa
    // valeur. Sans cela, le filtre change la luminosite de l'image en meme
    // temps qu'il la floute.
    if (total > 0.0) {
        const float inverse = (float)(1.0 / total);
        for (size_t i = 0; i < taps.size(); ++i) taps[i].weight *= inverse;
    }
}

} // namespace

// -- callbacks ----------------------------------------------------------------

void
IX_MODULE_CLBK::pre_filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx,
                           unsigned int& kernel_radius, unsigned int& total_pass_count)
{
    const CmaImageFilterBokeh& cma = (const CmaImageFilterBokeh&) eval.get_cma();

    // Le rayon est exprime en pixels de l'image finale, mais Clarisse peut
    // evaluer a une fraction de sa taille (resolution_multiplier, 50 % par
    // defaut). Sans cette mise a l'echelle, le flou changerait de taille selon
    // le multiplicateur -- et la doc de l'attribut promet l'inverse :
    // regler le filtre une fois, quel que soit le multiplicateur.
    const double scale = ctx.resolution_multiplier > 0.0 ? ctx.resolution_multiplier : 1.0;

    g_settings.radius      = cma.get_radius() * scale;
    g_settings.blades      = (int) cma.get_blades();
    g_settings.rotation    = cma.get_rotation();
    g_settings.roundness   = cma.get_roundness();
    g_settings.anamorphism = cma.get_anamorphism();
    g_settings.threshold   = cma.get_threshold();
    g_settings.gain        = cma.get_gain();
    g_settings.vignetting  = cma.get_optical_vignetting();
    g_settings.spherical   = cma.get_spherical_aberration();
    g_settings.chromatic   = cma.get_chromatic_aberration();
    g_settings.image_width  = ctx.source_image ? ctx.source_image->get_width() : 1;
    g_settings.image_height = ctx.source_image ? ctx.source_image->get_height() : 1;

    // L'anamorphisme etire la forme : le noyau doit etre demande assez large
    // pour la contenir, sinon elle est tronquee au bord de la tuile.
    double reach = g_settings.radius;
    if (g_settings.anamorphism != 0.0) reach *= (1.0 + fabs(g_settings.anamorphism));
    // L'aberration chromatique decale le rayon d'un canal vers le haut.
    reach *= (1.0 + 0.05 * fabs(g_settings.chromatic));

    kernel_radius = (unsigned int)(reach < 0.0 ? 0.0 : reach + 0.5);
    total_pass_count = 1;
}

bool
IX_MODULE_CLBK::filter(OfObject& object, const CtxEval& eval, const CtxKernelFilter& ctx)
{
    const ImageProxy *src = ctx.image;
    if (src == 0) return true;

    const float *sr = src->get_red_channel();
    const float *sg = src->get_green_channel();
    const float *sb = src->get_blue_channel();
    float *dr = ctx.channel_r.data;
    float *dg = ctx.channel_g.data;
    float *db = ctx.channel_b.data;
    if (sr == 0 || sg == 0 || sb == 0 || dr == 0 || dg == 0 || db == 0) return true;

    const Settings& s = g_settings;
    if (s.radius < 0.5) return true;   // rien a flouter : la destination porte deja la source

    const int stride = (int) src->get_width();
    const int width  = ctx.region.width;
    const int height = ctx.region.height;

    // Position de la tuile dans le cadre, ramenee a [-1, 1] depuis le centre.
    // C'est ce qui pilote le vignettage optique.
    const double half_w = s.image_width * 0.5;
    const double half_h = s.image_height * 0.5;
    const double frame_x = (ctx.x0 + width * 0.5 - half_w) / (half_w > 0.0 ? half_w : 1.0);
    const double frame_y = (ctx.y0 + height * 0.5 - half_h) / (half_h > 0.0 ? half_h : 1.0);

    // Un noyau par canal quand l'aberration chromatique est active : c'est le
    // decalage de rayon entre canaux qui colore le bord des boules.
    std::vector<Tap> kernel_r, kernel_g, kernel_b;
    build_kernel(kernel_g, s, 1.0, frame_x, frame_y);
    const bool split = (s.chromatic != 0.0);
    if (split) {
        build_kernel(kernel_r, s, 1.0 + 0.05 * s.chromatic, frame_x, frame_y);
        build_kernel(kernel_b, s, 1.0 - 0.05 * s.chromatic, frame_x, frame_y);
    }
    if (kernel_g.empty()) return true;

    const Tap *taps_r = split ? &kernel_r[0] : &kernel_g[0];
    const Tap *taps_g = &kernel_g[0];
    const Tap *taps_b = split ? &kernel_b[0] : &kernel_g[0];
    const size_t count_r = split ? kernel_r.size() : kernel_g.size();
    const size_t count_g = kernel_g.size();
    const size_t count_b = split ? kernel_b.size() : kernel_g.size();

    const float threshold = (float) s.threshold;
    const float gain = (float) s.gain;
    const bool boosting = (gain != 1.0f);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int cx = x + ctx.region.x;
            const int cy = y + ctx.region.y;

            // Moyenne ponderee par l'energie.
            //
            // Chaque echantillon compte pour son poids de noyau multiplie par
            // un facteur de reprise qui ne depasse 1 que s'il est plus clair
            // que le seuil. On accumule aussi la somme de ces poids et on
            // divise par elle a la fin.
            //
            // C'est ce qui fait les boules : dans un disque ou un seul pixel
            // est tres clair, il domine la moyenne et le disque entier prend
            // sa couleur, net, au lieu de disparaitre dans le gris. Et comme
            // on divise par la somme reelle des poids, l'energie est
            // conservee : a gain 1 le filtre est exactement neutre sur un
            // aplat, quel que soit le seuil.
            double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
            double norm_r = 0.0, norm_g = 0.0, norm_b = 0.0;

            for (size_t i = 0; i < count_g; ++i) {
                const Tap& t = taps_g[i];
                const int o = (cy + t.dy) * stride + (cx + t.dx);
                const float lum = 0.2126f * sr[o] + 0.7152f * sg[o] + 0.0722f * sb[o];
                float boost = 1.0f;
                if (boosting && lum > threshold) boost = 1.0f + gain * (lum - threshold);
                const double w = t.weight * boost;
                sum_g += sg[o] * w;
                norm_g += w;
                if (!split) {
                    sum_r += sr[o] * w;
                    sum_b += sb[o] * w;
                    norm_r += w;
                    norm_b += w;
                }
            }

            if (split) {
                for (size_t i = 0; i < count_r; ++i) {
                    const Tap& t = taps_r[i];
                    const int o = (cy + t.dy) * stride + (cx + t.dx);
                    const float lum = 0.2126f * sr[o] + 0.7152f * sg[o] + 0.0722f * sb[o];
                    float boost = 1.0f;
                    if (boosting && lum > threshold) boost = 1.0f + gain * (lum - threshold);
                    const double w = t.weight * boost;
                    sum_r += sr[o] * w;
                    norm_r += w;
                }
                for (size_t i = 0; i < count_b; ++i) {
                    const Tap& t = taps_b[i];
                    const int o = (cy + t.dy) * stride + (cx + t.dx);
                    const float lum = 0.2126f * sr[o] + 0.7152f * sg[o] + 0.0722f * sb[o];
                    float boost = 1.0f;
                    if (boosting && lum > threshold) boost = 1.0f + gain * (lum - threshold);
                    const double w = t.weight * boost;
                    sum_b += sb[o] * w;
                    norm_b += w;
                }
            }

            const int d = y * width + x;
            dr[d] = (float)(norm_r > 0.0 ? sum_r / norm_r : 0.0);
            dg[d] = (float)(norm_g > 0.0 ? sum_g / norm_g : 0.0);
            db[d] = (float)(norm_b > 0.0 ? sum_b / norm_b : 0.0);
        }
    }

    return true;
}

void
IX_MODULE_CLBK::post_filter(OfObject& object)
{
}
