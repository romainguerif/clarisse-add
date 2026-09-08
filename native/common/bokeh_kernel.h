// Construction du noyau de bokeh -- la PSF discretisee du diaphragme.
//
// Sorti de bokeh.cpp pour une raison precise : ce code est la seule chose du
// filtre qui se mesure sans rendre. Tout ce qui fait une couture -- l'energie
// du noyau, sa portee reelle, sa dependance a la position dans le cadre -- se
// lit dans les champs de Kernel, en microsecondes et pour zero yen. Le tenir
// dans un en-tete permet a `tests/bokeh_kernel_probe.cpp` de mesurer le VRAI
// noyau et pas une copie qui derivera.
//
// Aucune dependance au SDK Clarisse ici : math.h et vector, rien d'autre.

#ifndef CLARISSE_ADD_BOKEH_KERNEL_H
#define CLARISSE_ADD_BOKEH_KERNEL_H

#include <math.h>
#include <vector>

#include "aperture.h"

namespace clarisse_add {

// Finesse de l'anneau d'aberration spherique. p grand concentre l'energie
// tres pres du bord ; 4 donne une bulle de savon credible.
const double BOKEH_SPHERICAL_POWER = 4.0;

// Quantification des poids interieurs pour les sommes prefixees. Sans
// aberration spherique l'interieur est uniforme et un seul niveau suffit.
//
// A 16 niveaux, chaque marche vaut 6,25 % du pic : sur une bulle de savon
// eclairee a 50, cela fait des anneaux concentriques de 3 unites contre un
// fond a 0,1 -- parfaitement visibles. Le cout est lineaire en nombre de
// segments, pas en echantillons : monter a 64 divise la marche par quatre
// pour un surcout modeste.
const int BOKEH_SPHERICAL_LEVELS = 64;

// Ce dont la construction a besoin, et rien de plus. Un struct a part plutot
// que les reglages complets du filtre : c'est ce qui permet de batir un noyau
// depuis une sonde, sans OfObject, sans CtxEval et sans Clarisse.
// Le vignettage optique n'est PAS ici, et c'est le point de la correction.
//
// Il depend de l'endroit du cadre ; le noyau, lui, n'en depend plus. Un noyau
// qui dependait du cadre devait etre rebati par tuile -- Clarisse ne donne pas
// d'autre granularite -- et devenait donc une fonction en ESCALIER de la
// position, constante sur chaque tuile et sautant a la frontiere. C'est la
// definition d'une couture. Mesure sur une image de 1920 : le centre de
// gravite du noyau se deplacait de 4,0 pixels d'une tuile a la suivante a
// vignettage 1 et rayon 40.
//
// Le vignettage se decrit maintenant a part (`Vignette`) et s'applique par
// PIXEL, ce qui coute le meme prix parce que la troncature est un disque : son
// intersection avec un segment horizontal du noyau se calcule en une racine.
struct KernelSpec {
    double radius;        // rayon en pixels, ECHELLE DE CANAL DEJA APPLIQUEE
    int    blades;
    double rotation;      // radians
    double curvature;     // 0 lames droites .. 1 disque ; negatif = concave
    double anamorphism;
    double softness;      // fraction du rayon
    double spherical;
    bool   keep_taps;     // garder la liste complete (mode "forme du noyau")

    KernelSpec()
        : radius(0.0), blades(0), rotation(0.0), curvature(0.0),
          anamorphism(0.0), softness(0.0), spherical(0.0), keep_taps(false) {}
};

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
    double total;              // somme de tous les poids, avant normalisation
    double level_weight;       // poids d'un niveau de l'interieur
    int    levels;
    double scale_x, scale_y;   // anamorphisme, repris par la troncature
    std::vector<Run> runs;     // interieur, quantifie par niveaux
    std::vector<Tap> edge;     // bord, couverture partielle, poids exact
    std::vector<Tap> all;      // tous les taps, pour le chemin lent
    bool   uniform_interior;

    Kernel() : reach(0), total(0.0), level_weight(0.0), levels(0),
               scale_x(1.0), scale_y(1.0), uniform_interior(true) {}
};

// -- le vignettage optique, applique par pixel --------------------------------
//
// Le barillet de l'objectif tronque le faisceau hors axe : la pupille apparente
// devient l'intersection de l'ouverture et d'un disque de meme rayon, decale
// vers le centre du cadre. C'est l'amande dite oeil-de-chat, dont le grand axe
// est tangentiel -- les amandes tournent autour du centre, elles ne pointent
// pas vers lui.
//
// En coordonnees d'ouverture le disque de troncature a le rayon 1 ; multiplie
// par le rayon du noyau, il devient, en PIXELS, un disque de meme rayon que le
// bokeh, dont le centre s'ecarte de `strength * frame_r * radius`. Ecrit comme
// cela, le test se fait directement sur le decalage entier (dx, dy) du noyau,
// donc sans repasser par les coordonnees normalisees a chaque tap.
struct Vignette {
    bool   active;
    double strength;
    double radius;        // rayon de l'ouverture, en pixels : l'echelle du decalage
    double cut;           // rayon du disque de troncature, en pixels
    double scale_x, scale_y;
    double half_w, half_h;
    double feather;       // largeur du fondu, en pixels

    Vignette() : active(false), strength(0.0), radius(0.0), cut(0.0),
                 scale_x(1.0), scale_y(1.0), half_w(1.0), half_h(1.0),
                 feather(1.0) {}

    // Le disque de troncature est un demi-pixel plus grand que l'ouverture.
    //
    // Ce n'est pas un ajustement : c'est ce qui evite de compter deux fois le
    // MEME bord. Au centre du cadre le decalage est nul, les deux frontieres
    // se confondent, et les pixels du pourtour recevaient alors leur
    // couverture partielle une fois pour l'ouverture et une fois pour la
    // troncature -- 0,5 x 0,5 au lieu de 0,5. Mesure : l'image s'assombrissait
    // de 0,86 % au centre du cadre des qu'on activait le vignettage, la ou
    // celui-ci ne doit rien faire du tout.
    static double cut_radius(const double& aperture_radius,
                             const double& feather_px) {
        return aperture_radius + 0.5 * feather_px;
    }

    // Centre du disque de troncature, dans le repere des decalages du noyau,
    // pour un pixel donne en coordonnees image absolues.
    inline void centre(const double& px, const double& py,
                       double& ox, double& oy) const {
        const double fx = (px - half_w) / (half_w > 0.0 ? half_w : 1.0);
        const double fy = (py - half_h) / (half_h > 0.0 ? half_h : 1.0);
        const double fr = sqrt(fx * fx + fy * fy);
        if (fr < 1e-9) { ox = 0.0; oy = 0.0; return; }
        const double amount = strength * fr * radius;
        ox = -fx / fr * amount;
        oy = -fy / fr * amount;
    }

    // Couverture d'un tap isole. Le fondu est une rampe d'un pixel : le bord
    // du barillet est une piece de metal, pas un degrade.
    inline double cover(const double& dx, const double& dy,
                        const double& ox, const double& oy) const {
        const double ux = dx * scale_x - ox;
        const double uy = dy * scale_y - oy;
        const double q = ux * ux + uy * uy;
        const double outer = cut + feather * 0.5;
        if (q >= outer * outer) return 0.0;
        const double inner = cut - feather * 0.5;
        if (inner > 0.0 && q <= inner * inner) return 1.0;
        const double d = sqrt(q);
        double t = (outer - d) / feather;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        return t;
    }
};

// Portee reelle du noyau, en pixels, avant arrondi.
//
// C'est LE nombre qui doit etre partage entre `pre_filter` -- qui l'utilise
// pour dimensionner la marge du proxy -- et `build_kernel`, qui l'utilise
// pour borner ses boucles. Les deux le calculaient separement et
// differemment ; c'etait une source de coutures a soi seule, parce qu'un
// noyau plus large que la marge lit du vide au bord de chaque tuile et
// s'assombrit la, exactement sur la grille des tuiles.
//
// Trois facteurs, tous multiplicatifs :
//   - l'echelle de canal de l'aberration chromatique, deja dans radius ;
//   - la douceur, qui etale la frontiere jusqu'a rho = 1 + softness ;
//   - l'anamorphisme, qui COMPRIME un axe : la portee sur l'autre reste
//     entiere, donc le max des deux vaut toujours le rayon non comprime.
inline double
bokeh_kernel_reach(const double& radius, const double& softness)
{
    const double skirt = (softness > 0.0) ? (1.0 + softness) : 1.0;
    return radius * skirt;
}

// Couverture d'un echantillon, surechantillonnee 8x8 quand il est a cheval sur
// la frontiere. Le surechantillonnage n'est fait que la : sur un rayon de 100
// il concerne quelques centaines de taps sur trente mille.
inline double
bokeh_coverage(const Aperture& a, const double& cx, const double& cy,
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
// centre et maximal dans les coins.
inline void
bokeh_build_kernel(Kernel& k, const KernelSpec& s)
{
    k.runs.clear();
    k.edge.clear();
    k.all.clear();
    k.total = 0.0;
    k.levels = 1;
    k.level_weight = 0.0;
    k.uniform_interior = (s.spherical == 0.0);

    const double radius = s.radius;
    if (radius < 0.5) { k.reach = 0; return; }

    Aperture aperture;
    aperture_init(aperture, s.blades, s.rotation, s.curvature);

    // Anamorphisme : on comprime les coordonnees d'echantillonnage sur un axe,
    // ce qui etire la forme obtenue sur l'autre.
    double scale_x = 1.0, scale_y = 1.0;
    if (s.anamorphism > 0.0) scale_x = 1.0 + s.anamorphism;
    else if (s.anamorphism < 0.0) scale_y = 1.0 - s.anamorphism;
    k.scale_x = scale_x;
    k.scale_y = scale_y;

    // La portee tient compte de la jupe de douceur. Sans elle les taps de
    // rho > 1 tombaient hors des boucles : le fondu etait tranche net, et pas
    // au meme endroit selon la direction, puisque la boucle balaye un CARRE.
    // Mesure a rayon 40 et douceur 1 : portee obtenue 58 px au lieu de 80,
    // avec un poids qui tombait de 25 % a zero d'un pixel au suivant, et une
    // forme rendue carree sur ses diagonales.
    const double reach_pixels = bokeh_kernel_reach(radius, s.softness);
    const int reach_x = (int)(reach_pixels / scale_x + 1.5);
    const int reach_y = (int)(reach_pixels / scale_y + 1.5);
    k.reach = reach_x > reach_y ? reach_x : reach_y;

    // La douceur du bord, en fraction du rayon. A zero, `bokeh_coverage`
    // retombe sur un surechantillonnage d'un pixel, qui suffit a supprimer le
    // crenelage.
    const double softness = s.softness;
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

            const double cover = bokeh_coverage(aperture, ux, uy, step, softness);
            if (cover <= 0.0) continue;

            // Aberration spherique : redistribution radiale a moyenne
            // preservee. La moyenne de rho^p ponderee par l'aire sur le disque
            // unite vaut 2/(p+2), donc le terme entre parentheses est de
            // moyenne nulle : le curseur deplace l'energie sans en ajouter.
            double weight = cover;
            if (s.spherical != 0.0) {
                // rho^4 par deux multiplications : ni sqrt ni pow.
                const double rho2 = ux * ux + uy * uy;
                const double p = BOKEH_SPHERICAL_POWER;   // 4
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

            if (s.keep_taps) {
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
    k.levels = k.uniform_interior ? 1 : BOKEH_SPHERICAL_LEVELS;
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

} // namespace clarisse_add

#endif
