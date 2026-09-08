// Geometrie d'un diaphragme reel.
//
// Partage par le filtre d'image et par la camera : les deux doivent produire
// exactement la meme forme, sans quoi passer de l'un a l'autre changerait le
// bokeh. C'est aussi la seule facon de n'avoir qu'un endroit ou corriger une
// erreur de forme.
//
// Formulation par demi-plans plutot que par atan2 : les normales sortantes des
// N aretes sont precalculees, et max_k (u . n_k) donne rho * cos(delta), ou
// delta est l'ecart angulaire a l'arete la plus proche. Aucune fonction
// transcendante par echantillon, et le resultat est directement une distance
// signee -- ce dont l'antialiasing a besoin.
//
// Les lames bombees suivent un arc de cercle exact passant par les deux
// sommets, pas une interpolation. Le bombement au milieu de l'arete vaut
// kappa * (1 - cos(pi/N)), et le rayon de l'arc s'en deduit :
//
//     R_b = (b^2 + s^2) / (2 b)     avec s = sin(pi/N), b le bombement
//     c   = a -+ sqrt(R_b^2 - s^2)  avec a = cos(pi/N)
//     r(delta) = c cos(delta) +- sqrt(R_b^2 - c^2 sin^2(delta))
//
// A kappa = 1 on retrouve exactement le cercle, a kappa = 0 exactement le
// polygone -- verifie numeriquement aux deux extremites.

#ifndef CLARISSE_ADD_APERTURE_H
#define CLARISSE_ADD_APERTURE_H

#include <math.h>

namespace clarisse_add {

const double APERTURE_PI = 3.14159265358979323846;
const int APERTURE_MAX_BLADES = 64;

struct Aperture {
    bool   circular;
    int    blades;
    double nx[APERTURE_MAX_BLADES];
    double ny[APERTURE_MAX_BLADES];
    double apothem;         // cos(pi/N)
    double arc_centre;      // c
    double arc_radius;      // R_b, ou 0 pour des lames droites
    bool   concave;
};

inline void
aperture_init(Aperture& a, int blades, const double& rotation,
              const double& curvature)
{
    if (blades > APERTURE_MAX_BLADES) blades = APERTURE_MAX_BLADES;
    a.circular = (blades < 3);
    a.blades = blades;
    a.concave = (curvature < 0.0);
    if (a.circular) {
        a.apothem = 1.0;
        a.arc_centre = 0.0;
        a.arc_radius = 1.0;
        return;
    }

    const double half = APERTURE_PI / a.blades;
    a.apothem = cos(half);
    const double chord = sin(half);

    for (int k = 0; k < a.blades; ++k) {
        const double angle = rotation + (2.0 * k + 1.0) * half;
        a.nx[k] = cos(angle);
        a.ny[k] = sin(angle);
    }

    double bulge = fabs(curvature) * (1.0 - a.apothem);

    // Une lame creusee ne peut pas se creuser jusqu'au centre. Le rayon au
    // milieu de la lame vaut apotheme - bombement ; a trois lames l'apotheme
    // vaut 1/2 et le bombement maximal vaut 1/2 aussi, si bien qu'a -100 %
    // l'ouverture se pince exactement a zero et disparait. Elle ne rendrait
    // plus aucun flou, et cote camera plus aucune profondeur de champ -- un
    // reglage qui eteint la fonction n'est pas un reglage.
    //
    // On garde donc un dixieme de l'apotheme. C'est une etoile tres marquee,
    // et elle existe encore.
    if (a.concave && bulge > 0.9 * a.apothem) bulge = 0.9 * a.apothem;

    if (bulge < 1e-9) {
        a.arc_radius = 0.0;     // polygone droit : pas d'arc
        a.arc_centre = 0.0;
        return;
    }
    a.arc_radius = (bulge * bulge + chord * chord) / (2.0 * bulge);
    const double offset = sqrt(a.arc_radius * a.arc_radius - chord * chord);
    a.arc_centre = a.concave ? (a.apothem + offset) : (a.apothem - offset);
}

// Rayon de la frontiere dans la direction de l'echantillon, en fraction du
// rayon circonscrit. Rend 1 pour le disque.
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

    double cos_d = m / rho;
    if (cos_d > 1.0) cos_d = 1.0;
    if (cos_d < -1.0) cos_d = -1.0;

    if (a.arc_radius <= 0.0) {
        if (cos_d <= 1e-6) return 1.0;
        return a.apothem / cos_d;
    }

    const double sin2 = 1.0 - cos_d * cos_d;
    double inside = a.arc_radius * a.arc_radius
                    - a.arc_centre * a.arc_centre * sin2;

    // Le rayon tangente l'arc : les deux intersections se confondent. Rendre
    // l'apotheme la -- ce que faisait la version precedente -- coupe les
    // sommets de l'etoile. A quatre lames creusees a fond, le sommet tombait
    // ainsi a 0,707 au lieu de 1, et le cas se produit par simple arrondi
    // puisque la tangence y est exacte. La racine vaut zero, elle ne vaut pas
    // autre chose.
    if (inside < 0.0) inside = 0.0;
    const double root = sqrt(inside);

    // Laquelle des deux intersections est la bonne ?
    //
    // Le rayon partant du centre coupe le cercle de l'arc deux fois, une fois
    // en deca du centre de l'arc et une fois au-dela. Une lame BOMBEE gonfle
    // vers l'exterieur : c'est l'intersection lointaine. Une lame CREUSEE
    // rentre vers le centre : c'est la proche. Il n'y a pas d'autre cas, et
    // le signe de la courbure suffit a trancher.
    //
    // Un critere sur la position du centre de l'arc a ete essaye et il est
    // faux. Il repond juste au SOMMET de la lame -- ou l'arc peut
    // effectivement passer par la branche lointaine -- et faux partout
    // ailleurs, en particulier au milieu de la lame, ou il rendait le
    // gonflement au lieu du creux. Mesure a trois lames et -50 % : rayon de
    // 3,50 au milieu de la lame la ou la geometrie donne 0,25, soit une
    // ouverture quatorze fois trop grande, largement hors du disque
    // circonscrit et donc hors de la marge reservee autour de chaque tuile.
    //
    // Reserve assumee : avec la branche proche, le sommet d'une etoile a trois
    // lames est legerement rogne (0,875 au lieu de 1 a -50 %). C'est le prix
    // de la description par demi-plans, qui suppose la forme etoilee vue du
    // centre ; l'arc d'une lame tres creusee deborde de son secteur. L'ecart
    // decroit vite avec le nombre de lames et il est nul des cinq lames.
    if (!a.concave) return a.arc_centre * cos_d + root;
    const double edge = a.arc_centre * cos_d - root;
    return (edge > 0.0) ? edge : 0.0;
}

// Rayon de la frontiere a un angle donne. Pratique quand on echantillonne en
// polaire plutot qu'en cartesien.
inline double
aperture_edge_at(const Aperture& a, const double& angle)
{
    if (a.circular) return 1.0;
    return aperture_edge(a, cos(angle), sin(angle), 1.0);
}

} // namespace clarisse_add

#endif
