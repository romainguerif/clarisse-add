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
    const double inside = a.arc_radius * a.arc_radius
                          - a.arc_centre * a.arc_centre * sin2;
    if (inside <= 0.0) return a.apothem;
    const double root = sqrt(inside);

    // Laquelle des deux intersections est la bonne ? Choisir sur le signe de
    // la courbure ne marche pas : pour une lame tres creusee, le centre de
    // l'arc repasse du cote proche et la racine "moins" donne un rayon qui
    // s'effondre. Mesure a trois lames : a -0.5 le sommet tombe a 0.875, a
    // -0.8 a 0.238, a -1.0 exactement a zero -- l'ouverture disparait, donc
    // plus aucun flou du tout, et cote camera plus aucune profondeur de champ.
    //
    // Le critere geometrique juste est la position du centre de l'arc par
    // rapport au sommet : la racine "plus" convient tant que le centre reste
    // en deca, ce qui se lit sur c * apotheme.
    if (a.arc_centre * a.apothem < 1.0) return a.arc_centre * cos_d + root;
    return a.arc_centre * cos_d - root;
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
