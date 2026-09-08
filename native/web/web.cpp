// Une toile d'araignee orbitele.
//
// Le node ne dessine pas une rosace : il rejoue la construction. Ancrages,
// cadre, rayons, moyeu, spirale -- dans cet ordre, parce que c'est l'ordre qui
// produit les irregularites justes. Un rayon devie deforme la maille autour de
// lui parce que la spirale s'accroche dessus ; si on ajoutait le desordre a la
// fin, par un bruit sur les positions, on n'aurait pas ca.
//
// Toutes les valeurs d'usine viennent de mesures publiees, rassemblees dans
// docs/toile-orbitele.md. Trois d'entre elles ont change ce que j'allais
// ecrire, et meritent d'etre rappelees ici :
//
//   - **Les fils sont droits.** Le parametre de chainette d'une fibre de trois
//     microns tendue a cent micronewtons vaut onze cents metres ; sur une
//     portee de dix centimetres, la chainette est une droite au millieme de
//     pixel pres. Un rayon d'un metre entier flechit de onze centiemes de
//     millimetre. Ce qui courbe les fils d'une toile, ce sont les charges
//     ponctuelles -- une goutte de rosee pese trois mille fois le poids propre
//     du fil -- et elles les courbent en polygone funiculaire, pas en courbe
//     lisse. C'est l'inverse exact de notre node de cable, et c'est voulu.
//
//   - **Deux asymetries independantes.** Celle des longueurs (le bas est une
//     fois et demie plus grand) et celle des angles (treize degres en haut
//     contre huit en bas). La seconde survit a une extension nulle : meme dans
//     une toile symetrique en hauteur, les angles restent plus serres en bas,
//     chez quatre-vingt-douze araignees sur quatre-vingt-treize. Les lier
//     serait une erreur de modele, pas une simplification.
//
//   - **Le pas de spirale est sectoriel.** Il croit vers le haut et reste
//     constant, voire decroit, vers le bas. L'erreur naturelle serait de le
//     faire croitre avec le rayon partout ; la regression publiee est nette au
//     nord et non significative au sud.
//
// Le node produit un seul maillage, avec cinq groupes de shading -- amarrage,
// cadre, rayon, moyeu, spirale -- pour que chaque soie recoive sa matiere.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_geometry.h>
#include <module_polymesh.h>
#include <module_scene_item.h>

#include <poly_mesh.h>

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>
#include <gmath_vec3.h>

#include <cmath>

#include <curve_core.h>

#include <web.cma>

using namespace curve_core;

namespace {

enum {
    GROUP_ANCHOR = 0,
    GROUP_FRAME  = 1,
    GROUP_RADIUS = 2,
    GROUP_HUB    = 3,
    GROUP_SPIRAL = 4,
    GROUP_COUNT  = 5
};

// Un fil : une polyligne, sa soie, et son epaisseur propre.
struct Thread {
    CoreVector<GMathVec3d> points;
    unsigned int group;
    double radius;
};

// Un rayon, garde en polyligne plutot qu'en direction : c'est ce qui permet a
// la spirale de suivre les coudes au lieu de les ignorer.
struct Radius {
    CoreVector<GMathVec3d> points;
    CoreVector<double> arc;   // longueur cumulee, arc[0] = 0
    double angle;             // depuis le haut, sens trigonometrique
    double length;
};

// Un aleatoire deterministe dans [0, 1[. Deux evaluations du meme node doivent
// rendre la meme toile, sinon le node ne tient pas dans un graphe.
inline double
hash01(const unsigned int& index, const unsigned int& seed)
{
    unsigned int h = index * 2654435761u + seed * 40503u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return double(h & 0xFFFFFFu) / double(0x1000000u);
}

// Le meme, centre sur zero.
inline double
hash11(const unsigned int& index, const unsigned int& seed)
{
    return hash01(index, seed) * 2.0 - 1.0;
}

// Rediscretise une polyligne en `n` points egalement espaces en longueur.
// Les extremites sont conservees exactement.
void
resample(const CoreVector<GMathVec3d>& source, const unsigned int& n,
         CoreVector<GMathVec3d>& out)
{
    out.remove_all();
    const unsigned int count = source.get_count();
    if (count == 0u) return;
    if (count == 1u || n < 2u) { out.add(source[0]); return; }

    CoreVector<double> arc;
    arc.add(0.0);
    for (unsigned int i = 1; i < count; i++) {
        arc.add(arc[i - 1u] + vsub(source[i], source[i - 1u]).get_length());
    }
    const double total = arc[count - 1u];
    if (total < 1e-12) { out.add(source[0]); out.add(source[count - 1u]); return; }

    unsigned int seg = 0u;
    for (unsigned int k = 0; k < n; k++) {
        const double target = total * double(k) / double(n - 1u);
        while (seg + 2u < count && arc[seg + 1u] < target) seg++;
        const double span = arc[seg + 1u] - arc[seg];
        const double u = (span > 1e-12) ? ((target - arc[seg]) / span) : 0.0;
        out.add(vlerp(source[seg], source[seg + 1u], u));
    }
}

} // namespace

// La construction proprement dite. Tout est en coordonnees monde local : la
// toile est dans le plan XY, le haut est +Y, et l'epaisseur se prend en Z.
class WebBuilder {
public:
    // Reglages, lus une fois depuis l'objet.
    double radius;
    double aspect;
    unsigned int radii_count;
    double asymmetry;
    double angle_asymmetry;
    double depth;
    unsigned int seed;

    unsigned int anchors;
    double anchor_spread;
    double frame_margin;
    double anchor_length;

    double hub;
    unsigned int hub_turns;
    double free_zone;

    double spiral_pitch;
    double pitch_growth_top;
    double pitch_growth_bottom;
    double pitch_variation;
    unsigned int spiral_uturns;
    double free_sector;
    bool auxiliary;
    unsigned int auxiliary_turns;

    unsigned int deviated_radii;
    unsigned int y_radii;
    unsigned int extra_radii;
    double deviation;

    double thread_radius;
    double radius_variation;
    double frame_scale;
    double anchor_scale;
    double spiral_scale;

    CoreVector<Thread> threads;

    void
    build()
    {
        threads.remove_all();
        m_hub_centre = GMathVec3d(0.0, offset() * radius, 0.0);

        build_frame();
        build_radii();
        build_hub();
        if (auxiliary) build_auxiliary();
        build_spiral();
        if (free_sector > 1e-6) build_signal();
    }

private:
    GMathVec3d m_hub_centre;
    CoreVector<GMathVec3d> m_anchors;   // sommets du cadre
    CoreVector<Radius> m_radii;

    // Le decentrage du moyeu qui produit le rapport haut/bas demande. Si le bas
    // vaut r fois le haut, le moyeu est a (r-1)/(r+1) du rayon au-dessus du
    // centre geometrique -- soit un sixieme pour le rapport 1,4 mesure.
    double
    offset() const
    {
        const double r = (asymmetry < 0.01) ? 0.01 : asymmetry;
        return (r - 1.0) / (r + 1.0);
    }

    // Le rayon de l'ellipse enveloppe dans une direction donnee, mesure depuis
    // le centre geometrique et non depuis le moyeu.
    double
    outline(const double& angle) const
    {
        const double sx = sin(angle) / ((radius * aspect > 1e-9)
                                        ? (radius * aspect) : 1e-9);
        const double sy = cos(angle) / ((radius > 1e-9) ? radius : 1e-9);
        const double d = sqrt(sx * sx + sy * sy);
        return (d > 1e-12) ? (1.0 / d) : radius;
    }

    // Une direction dans le plan de la toile, prise depuis le haut.
    static GMathVec3d
    direction(const double& angle)
    {
        return GMathVec3d(sin(angle), cos(angle), 0.0);
    }

    // Le relief hors plan. Une toile reelle est tendue entre des points qui ne
    // sont pas coplanaires ; sans ca elle a l'air imprimee sur une vitre.
    double
    elevation(const GMathVec3d& p) const
    {
        if (depth <= 1e-9 || radius <= 1e-9) return 0.0;
        const GMathVec3d q(p[0] / radius, p[1] / radius, 0.0);
        return depth * radius * fbm(q, 0.7, 3u, 0.5, seed + 5077u);
    }

    void
    lift(CoreVector<GMathVec3d>& points) const
    {
        for (unsigned int i = 0; i < points.get_count(); i++) {
            points[i] = GMathVec3d(points[i][0], points[i][1],
                                   points[i][2] + elevation(points[i]));
        }
    }

    // L'epaisseur d'un fil, avec sa variation propre. Le diametre mesure varie
    // du simple au double dans une seule toile : c'est beaucoup, et c'est vrai.
    double
    thickness(const double& scale, const unsigned int& index) const
    {
        const double base = thread_radius * radius * scale;
        const double v = 1.0 + radius_variation * hash11(index, seed + 8171u);
        return base * ((v < 0.05) ? 0.05 : v);
    }

    void
    emit(const CoreVector<GMathVec3d>& points, const unsigned int& group,
         const double& scale, const unsigned int& index)
    {
        if (points.get_count() < 2u) return;
        Thread thread;
        thread.points = points;
        thread.group = group;
        thread.radius = thickness(scale, index);
        threads.add(thread);
    }

    // --- le cadre -----------------------------------------------------------

    void
    build_frame()
    {
        const unsigned int n = (anchors < 3u) ? 3u : anchors;
        const double step = 2.0 * M_PI / double(n);

        // Les ancrages sont pousses vers l'exterieur d'un facteur 1/cos(pi/n),
        // de sorte que ce soit le MILIEU des cordes, et non les sommets, qui
        // touche l'enveloppe voulue. Sans cette correction le polygone rogne
        // les rayons de dix-neuf pour cent au milieu de chaque cote et pas du
        // tout aux sommets : sa propre anisotropie noie alors l'asymetrie
        // qu'on cherche a produire -- mesure, avec cinq ancrages, un rapport
        // haut/bas de 1,06 la ou on demandait 1,40.
        //
        // C'est aussi ce qu'on voit sur les photos : les coins du cadre
        // depassent nettement de la partie ronde de la toile.
        const double circumscribe = 1.0 / cos(M_PI / double(n));

        for (unsigned int j = 0; j < n; j++) {
            const double jitter_angle =
                anchor_spread * hash11(j, seed + 313u) * step * 0.5;
            const double a = step * double(j) + jitter_angle;
            const double stretch =
                1.0 + frame_margin
                    + 0.15 * frame_margin * hash11(j, seed + 727u);
            const double d = outline(a) * stretch * circumscribe;
            m_anchors.add(vscale(direction(a), d));
        }

        // Le cadre : les cordes entre ancrages. Ce sont elles qui donnent a la
        // toile sa silhouette anguleuse -- les rayons ne s'arretent pas sur un
        // cercle, ils s'arretent sur des segments droits.
        for (unsigned int j = 0; j < n; j++) {
            CoreVector<GMathVec3d> ends;
            ends.add(m_anchors[j]);
            ends.add(m_anchors[(j + 1u) % n]);

            CoreVector<GMathVec3d> line;
            resample(ends, 10u, line);
            lift(line);
            emit(line, GROUP_FRAME, frame_scale, 4001u + j);
        }

        if (anchor_length > 1e-6) {
            for (unsigned int j = 0; j < n; j++) {
                const GMathVec3d& p = m_anchors[j];
                const double len = anchor_length * radius
                                 * (1.0 + 0.3 * hash11(j, seed + 1201u));
                GMathVec3d away = vnorm(p);
                // Un fil d'amarrage ne part pas dans l'axe : il va chercher ce
                // qui tient, un peu de cote.
                const double tilt = 0.5 * hash11(j, seed + 1607u);
                away = vnorm(vadd(away, GMathVec3d(-away[1] * tilt,
                                                   away[0] * tilt,
                                                   0.35 * hash11(j, seed + 1741u))));
                CoreVector<GMathVec3d> ends;
                ends.add(p);
                ends.add(vadd(p, vscale(away, len)));

                CoreVector<GMathVec3d> line;
                resample(ends, 6u, line);
                lift(line);
                emit(line, GROUP_ANCHOR, anchor_scale, 5001u + j);
            }
        }
    }

    // Ou un rayon partant du moyeu rencontre le cadre.
    bool
    hit_frame(const double& angle, GMathVec3d& out) const
    {
        const GMathVec3d u = direction(angle);
        const unsigned int n = m_anchors.get_count();
        double best = -1.0;

        for (unsigned int j = 0; j < n; j++) {
            const GMathVec3d& a = m_anchors[j];
            const GMathVec3d d = vsub(m_anchors[(j + 1u) % n], a);
            const double det = u[0] * d[1] - u[1] * d[0];
            if (fabs(det) < 1e-12) continue;

            const GMathVec3d q = vsub(a, m_hub_centre);
            const double s = (q[0] * d[1] - q[1] * d[0]) / det;
            const double v = (q[0] * u[1] - q[1] * u[0]) / det;
            if (s <= 1e-9 || v < 0.0 || v > 1.0) continue;
            if (best < 0.0 || s < best) best = s;
        }
        if (best < 0.0) return false;
        out = vadd(m_hub_centre, vscale(u, best));
        return true;
    }

    // --- les angles ---------------------------------------------------------

    // Les rayons ne sont pas equirepartis : l'ecart angulaire est plus grand en
    // haut. On se donne une densite w(theta), maximale en haut, et on place les
    // rayons de sorte que l'ecart local lui soit proportionnel -- ce qui revient
    // a inverser la primitive de 1/w.
    void
    build_angles(CoreVector<double>& out) const
    {
        const unsigned int n = radii_count;
        const double k = (angle_asymmetry < 0.05) ? 0.05 : angle_asymmetry;

        const unsigned int samples = 2048u;
        CoreVector<double> cumulative;
        cumulative.add(0.0);
        for (unsigned int i = 0; i < samples; i++) {
            const double mid = 2.0 * M_PI * (double(i) + 0.5) / double(samples);
            const double w = 1.0 + (k - 1.0) * (1.0 + cos(mid)) * 0.5;
            cumulative.add(cumulative[i]
                           + (2.0 * M_PI / double(samples)) / ((w > 1e-6) ? w : 1e-6));
        }
        const double total = cumulative[samples];

        unsigned int cursor = 0u;
        for (unsigned int j = 0; j < n; j++) {
            const double target = total * double(j) / double(n);
            while (cursor + 1u < samples && cumulative[cursor + 1u] < target) cursor++;
            const double span = cumulative[cursor + 1u] - cumulative[cursor];
            const double u = (span > 1e-15)
                           ? ((target - cumulative[cursor]) / span) : 0.0;
            double angle = 2.0 * M_PI * (double(cursor) + u) / double(samples);

            // Meme reglee, une araignee ne pose pas deux fois le meme angle.
            angle += 0.12 * (2.0 * M_PI / double(n)) * hash11(j, seed + 2311u);
            out.add(angle);
        }
    }

    // --- les rayons ---------------------------------------------------------

    void
    build_radii()
    {
        CoreVector<double> angles;
        build_angles(angles);
        const unsigned int n = angles.get_count();
        if (n == 0u) return;

        for (unsigned int j = 0; j < n; j++) {
            GMathVec3d tip;
            if (!hit_frame(angles[j], tip)) {
                tip = vadd(m_hub_centre,
                           vscale(direction(angles[j]), outline(angles[j])));
            }

            CoreVector<GMathVec3d> control;
            control.add(m_hub_centre);

            // Un rayon devie coude en chemin. Ce n'est pas un ornement : la
            // spirale s'accroche dessus, donc le coude deforme toute la maille
            // autour de lui -- et c'est la que ca se lit.
            // Le coude est SUR le trajet, pas avant : l'araignee part du moyeu
            // dans l'axe et ne s'ecarte qu'en cherchant ou s'accrocher. Un
            // rayon dont la portion interieure serait deja de biais serait plus
            // qu'une deviation -- ce serait un autre rayon, et la mesure le
            // compterait comme tel.
            const bool bent = is_marked(j, n, deviated_radii, 6101u);
            if (bent) {
                const double where = 0.45 + 0.25 * hash01(j, seed + 6151u);
                const double turn = atan(deviation)
                                  * ((hash01(j, seed + 6197u) < 0.5) ? -1.0 : 1.0);
                GMathVec3d elsewhere;
                if (hit_frame(angles[j] + turn, elsewhere)) {
                    control.add(vlerp(m_hub_centre, tip, where));
                    tip = elsewhere;
                }
            }
            control.add(tip);

            Radius r;
            // Vingt-quatre points : assez pour que le relief hors plan soit une
            // courbe et non une ligne brisee, et assez pour qu'une mesure prise
            // dans une bande etroite de la zone libre trouve toujours le rayon.
            resample(control, 24u, r.points);
            lift(r.points);
            r.angle = angles[j];
            measure(r);
            m_radii.add(r);

            emit(r.points, GROUP_RADIUS, 1.0, 100u + j);

            // Un rayon en Y se scinde avant d'atteindre le cadre. Le tronc et
            // la premiere branche restent le rayon que suit la spirale ; la
            // seconde branche est un fil de plus.
            if (is_marked(j, n, y_radii, 6301u)) {
                const double fork = 0.72 + 0.12 * hash01(j, seed + 6337u);
                const double spread = (2.0 * M_PI / double(n))
                                    * (0.35 + 0.3 * hash01(j, seed + 6373u));
                GMathVec3d other;
                if (hit_frame(angles[j] + spread, other)) {
                    CoreVector<GMathVec3d> branch;
                    branch.add(point_at(r, fork));
                    branch.add(other);
                    CoreVector<GMathVec3d> line;
                    resample(branch, 8u, line);
                    lift(line);
                    emit(line, GROUP_RADIUS, 0.9, 200u + j);
                }
            }
        }

        // Les rayons surnumeraires ne partent pas du moyeu : ils s'accrochent
        // en cours de route sur un voisin. C'est la meme regle que dans le mode
        // enchevetre -- un fil peut se fixer sur un fil.
        for (unsigned int e = 0; e < extra_radii; e++) {
            const unsigned int j =
                (unsigned int) (hash01(e, seed + 6421u) * double(n)) % n;
            const Radius& base = m_radii[j];
            const double where = 0.35 + 0.3 * hash01(e, seed + 6449u);
            const double spread = (2.0 * M_PI / double(n))
                                * (0.4 + 0.4 * hash01(e, seed + 6473u))
                                * ((hash01(e, seed + 6491u) < 0.5) ? -1.0 : 1.0);
            GMathVec3d tip;
            if (!hit_frame(base.angle + spread, tip)) continue;

            CoreVector<GMathVec3d> control;
            control.add(point_at(base, where));
            control.add(tip);
            CoreVector<GMathVec3d> line;
            resample(control, 8u, line);
            lift(line);
            emit(line, GROUP_RADIUS, 0.85, 300u + e);
        }
    }

    // Les `count` indices tires parmi `n`, sans avoir a tirer une liste : on
    // demande a chaque indice s'il fait partie du lot.
    bool
    is_marked(const unsigned int& index, const unsigned int& n,
              const unsigned int& count, const unsigned int& salt) const
    {
        if (count == 0u || n == 0u) return false;
        for (unsigned int c = 0; c < count; c++) {
            const unsigned int picked =
                (unsigned int) (hash01(c, seed + salt) * double(n)) % n;
            if (picked == index) return true;
        }
        return false;
    }

    static void
    measure(Radius& r)
    {
        r.arc.remove_all();
        r.arc.add(0.0);
        for (unsigned int i = 1; i < r.points.get_count(); i++) {
            r.arc.add(r.arc[i - 1u]
                      + vsub(r.points[i], r.points[i - 1u]).get_length());
        }
        r.length = r.arc[r.arc.get_count() - 1u];
    }

    // Le point situe a la fraction `t` de la longueur d'un rayon. C'est par la
    // que la spirale s'accroche, et c'est pour ca que les coudes se propagent.
    static GMathVec3d
    point_at(const Radius& r, double t)
    {
        const unsigned int count = r.points.get_count();
        if (count == 0u) return GMathVec3d(0.0, 0.0, 0.0);
        if (count == 1u || r.length < 1e-12) return r.points[0];
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;

        const double target = t * r.length;
        unsigned int seg = 0u;
        while (seg + 2u < count && r.arc[seg + 1u] < target) seg++;
        const double span = r.arc[seg + 1u] - r.arc[seg];
        const double u = (span > 1e-12) ? ((target - r.arc[seg]) / span) : 0.0;
        return vlerp(r.points[seg], r.points[seg + 1u], u);
    }

    // --- le moyeu -----------------------------------------------------------

    void
    build_hub()
    {
        const unsigned int n = m_radii.get_count();
        if (n == 0u || hub_turns == 0u) return;

        const double outer = hub;
        const double inner = hub * 0.15;
        const unsigned int steps = hub_turns * n;

        CoreVector<GMathVec3d> line;
        for (unsigned int s = 0; s <= steps; s++) {
            const double f = double(s) / double(steps);
            const double t = outer + (inner - outer) * f;
            line.add(point_at(m_radii[s % n], t));
        }
        emit(line, GROUP_HUB, 0.55, 7001u);
    }

    // La spirale auxiliaire est logarithmique, la spirale de capture est
    // arithmetique. Deux lois differentes pour deux spirales, et c'est mesure --
    // ce n'est pas une commodite de calcul.
    void
    build_auxiliary()
    {
        const unsigned int n = m_radii.get_count();
        if (n == 0u || auxiliary_turns == 0u) return;

        const unsigned int steps = auxiliary_turns * n;
        const double start = hub;
        const double end = 0.98;

        CoreVector<GMathVec3d> line;
        for (unsigned int s = 0; s <= steps; s++) {
            const double f = double(s) / double(steps);
            const double t = start * pow(end / start, f);
            line.add(point_at(m_radii[s % n], t));
        }
        emit(line, GROUP_SPIRAL, spiral_scale * 0.6, 7101u);
    }

    // --- la spirale de capture ---------------------------------------------

    // La forme du pas dans un secteur donne : un simple facteur, dont seule la
    // variation avec la position radiale compte. L'echelle absolue est fixee
    // apres coup, par la normalisation de `build_levels`.
    double
    pitch_shape(const double& angle, const double& t) const
    {
        const double up = (1.0 + cos(angle)) * 0.5;
        const double growth = pitch_growth_bottom
                            + (pitch_growth_top - pitch_growth_bottom) * up;
        const double middle = (free_zone + 1.0) * 0.5;
        const double factor = 1.0 + growth * (t - middle);
        return (factor < 0.05) ? 0.05 : factor;
    }

    // Les positions successives de la spirale sur UN rayon, du bord vers le
    // moyeu. C'est la piece qui rend le pas reellement sectoriel.
    //
    // La premiere version avancait d'un pas local a chaque croisement, en
    // partageant une position fractionnaire entre tous les rayons. Erreur :
    // l'ecart entre deux tours mesure sur un rayon donne valait alors la
    // MOYENNE des pas rencontres pendant le tour, pas le pas local. Ouvrir la
    // maille du haut ouvrait celle du bas d'autant -- la sonde l'a mesure,
    // 1,21 en haut contre 1,26 en bas la ou on demandait 2,03 contre 1,00.
    //
    // Chaque rayon porte donc sa propre suite, et la spirale les visite. Le
    // nombre de tours est le meme partout -- c'est un seul fil continu, il ne
    // peut pas en faire plus d'un cote que de l'autre -- d'ou la normalisation
    // par point fixe : on fabrique la suite avec la loi voulue, puis on la
    // dilate pour qu'elle atterrisse exactement sur la zone libre.
    void
    build_levels(const Radius& r, const unsigned int& index,
                 const unsigned int& turns, double *out) const
    {
        const double top = 0.97;
        const double wanted = top - free_zone;

        // Le point de depart de l'iteration compte : `pitch_shape` ne rend
        // qu'un facteur sans dimension, autour de un. Partir d'un pas de un
        // ferait descendre la suite sous zero des le premier cran, ou elle se
        // fait ecreter, et le point fixe ne se rattrape jamais -- on obtenait
        // une spirale de neuf tours qui plongeait jusqu'au centre du moyeu.
        // Avec le pas moyen exact, la premiere passe est deja juste quand la
        // croissance est nulle.
        double lambda = wanted / double(turns);

        for (unsigned int pass = 0; pass < 8u; pass++) {
            double t = top;
            out[0] = t;
            for (unsigned int k = 0; k < turns; k++) {
                // La variation d'un tour a l'autre est surtout une propriete du
                // TOUR : une araignee qui s'ecarte de sa consigne s'en ecarte
                // sur toute la boucle. Une part la suit quand meme par rayon,
                // sinon les spires seraient des cercles parfaits ondules.
                const double vary =
                    1.0 + pitch_variation
                        * (0.8 * hash11(k, seed + 9151u)
                           + 0.2 * hash11(index * 31u + k, seed + 9187u));
                double step = lambda * pitch_shape(r.angle, t)
                            * ((vary < 0.15) ? 0.15 : vary);
                t -= step;
                if (t < 0.0) t = 0.0;
                out[k + 1u] = t;
            }
            const double reached = top - out[turns];
            if (reached < 1e-12) break;
            lambda *= wanted / reached;
        }
        out[turns] = free_zone;
    }

    // La position de la spirale sur un rayon, a une phase continue. Interpoler
    // entre deux tours evite la marche d'escalier qu'on aurait en changeant de
    // niveau d'un coup au passage du premier rayon.
    static double
    sample_level(const double *levels, const unsigned int& turns,
                 const double& phase)
    {
        double p = phase;
        if (p < 0.0) p = 0.0;
        if (p > double(turns)) p = double(turns);
        unsigned int k = (unsigned int) p;
        if (k >= turns) k = (turns > 0u) ? (turns - 1u) : 0u;
        const double u = p - double(k);
        return levels[k] + (levels[k + 1u] - levels[k]) * u;
    }

    // Le secteur laisse libre, centre sur le haut.
    bool
    in_free_sector(const double& angle) const
    {
        if (free_sector <= 1e-6) return false;
        const double half = free_sector * M_PI / 360.0;
        double a = fmod(angle, 2.0 * M_PI);
        if (a < 0.0) a += 2.0 * M_PI;
        if (a > M_PI) a = 2.0 * M_PI - a;
        return a < half;
    }

    void
    build_spiral()
    {
        const unsigned int n = m_radii.get_count();
        if (n == 0u) return;

        // Une zone libre qui avale la zone de capture ne laisse pas une
        // spirale minuscule : elle n'en laisse aucune. Le cas a l'air
        // theorique, mais c'est exactement celui dont la sonde se sert pour
        // isoler la spirale du reste du maillage.
        if (free_zone >= 0.95) return;

        // Le nombre de tours se deduit du pas demande sur un rayon nominal. Il
        // est global : un seul fil ne peut pas faire plus de tours d'un cote
        // que de l'autre.
        const double pitch = (spiral_pitch > 1e-6) ? spiral_pitch : 1e-6;
        long wanted = (long) floor((0.97 - free_zone) / pitch + 0.5);
        if (wanted < 2) wanted = 2;
        if (wanted > 400) wanted = 400;
        const unsigned int turns = (unsigned int) wanted;

        CoreVector<double> levels;
        for (unsigned int i = 0; i < n * (turns + 1u); i++) levels.add(0.0);
        for (unsigned int i = 0; i < n; i++) {
            build_levels(m_radii[i], i, turns, &levels[i * (turns + 1u)]);
        }

        // On part du rayon le plus eloigne du secteur libre -- c'est-a-dire du
        // bas quand il y en a un, puisque le secteur est en haut. Partir du
        // premier rayon venu suffisait a tout casser : chez Zygiella le rayon
        // zero tombe dans le secteur, les deux voisins aussi, et la spirale
        // s'arretait a son premier point. La toile sortait sans une seule
        // spire, ce que la mesure ne disait pas et que le rendu a montre d'un
        // coup d'oeil.
        unsigned int index = 0u;
        if (free_sector > 1e-6) {
            double farthest = -1.0;
            for (unsigned int j = 0; j < n; j++) {
                double a = fmod(m_radii[j].angle, 2.0 * M_PI);
                if (a < 0.0) a += 2.0 * M_PI;
                if (a > M_PI) a = 2.0 * M_PI - a;
                if (a > farthest) { farthest = a; index = j; }
            }
        }

        int direction_sign = 1;
        double phase = 0.0;
        const double advance = 1.0 / double(n);
        const unsigned int limit = (turns + 2u) * n;

        CoreVector<GMathVec3d> line;

        for (unsigned int step = 0; step < limit; step++) {
            if (phase > double(turns)) break;
            line.add(point_at(m_radii[index],
                              sample_level(&levels[index * (turns + 1u)],
                                           turns, phase)));

            // Un demi-tour en epingle : le fil rebrousse chemin au lieu de
            // continuer son tour. On les tire vers le bord exterieur, ou ils
            // sont mesures comme etant concentres.
            if (spiral_uturns > 0u) {
                const double left = 1.0 - phase / double(turns);
                const double rate = double(spiral_uturns) / double(limit);
                if (hash01(step, seed + 9001u) < rate * (0.4 + 1.6 * left)) {
                    direction_sign = -direction_sign;
                }
            }

            unsigned int next =
                (unsigned int) ((int(index) + direction_sign + int(n)) % int(n));

            if (in_free_sector(m_radii[next].angle)) {
                direction_sign = -direction_sign;
                next = (unsigned int) ((int(index) + direction_sign + int(n)) % int(n));
                if (in_free_sector(m_radii[next].angle)) break;
            }

            phase += advance;
            index = next;
        }

        emit(line, GROUP_SPIRAL, spiral_scale, 8001u);
    }

    // Le fil avertisseur qui traverse le secteur libre : c'est par lui que
    // Zygiella sent sa toile depuis sa retraite.
    void
    build_signal()
    {
        const unsigned int n = m_radii.get_count();
        if (n == 0u) return;

        unsigned int best = 0u;
        double closest = -1.0;
        for (unsigned int j = 0; j < n; j++) {
            double a = fmod(m_radii[j].angle, 2.0 * M_PI);
            if (a < 0.0) a += 2.0 * M_PI;
            if (a > M_PI) a = 2.0 * M_PI - a;
            if (closest < 0.0 || a < closest) { closest = a; best = j; }
        }

        CoreVector<GMathVec3d> control;
        control.add(point_at(m_radii[best], 0.0));
        control.add(point_at(m_radii[best], 1.0));
        CoreVector<GMathVec3d> line;
        resample(control, 10u, line);
        lift(line);
        emit(line, GROUP_ANCHOR, 1.2, 9501u);
    }
};

class WebModule : public ModulePolymesh {
public:
    WebModule() : ModulePolymesh() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "radius", "aspect", "radii", "asymmetry", "angle_asymmetry",
            "depth", "seed",
            "anchors", "anchor_spread", "frame_margin", "anchor_length",
            "hub", "hub_turns", "free_zone",
            "spiral_pitch", "pitch_growth_top", "pitch_growth_bottom",
            "pitch_variation", "spiral_uturns", "free_sector",
            "auxiliary", "auxiliary_turns",
            "deviated_radii", "y_radii", "extra_radii", "deviation",
            "thread_radius", "radius_variation", "frame_scale", "anchor_scale",
            "spiral_scale", "sides"
        };
        const unsigned int name_count = sizeof(names) / sizeof(names[0]);

        unsigned int present = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            if (object.get_attribute(names[i]) != 0) present++;
        }
        if (present == 0) return;

        CoreArray<OfAttrDirtiness> attrs(present);
        unsigned int k = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            OfAttr *attr = object.get_attribute(names[i]);
            if (attr != 0) attrs[k++] = OfAttrDirtiness(attr, OfAttr::DIRTINESS_ALL);
        }
        set_resource_attrs(ModuleGeometry::RESOURCE_ID_GEOMETRY, attrs);
    }

    ResourceData *
    create_resource(const int& id, void *data) const override
    {
        if (id == ModuleGeometry::RESOURCE_ID_GEOMETRY) {
            PolyMesh *mesh = build_mesh();
            if (mesh != 0) return mesh;
        }
        return ModulePolymesh::create_resource(id, data);
    }

private:
    static double
    read_double(OfObject& object, const char *name, const double& fallback)
    {
        const OfAttr *attr = object.get_attribute(name);
        return (attr != 0) ? attr->get_double() : fallback;
    }

    static long
    read_long(OfObject& object, const char *name, const long& fallback)
    {
        const OfAttr *attr = object.get_attribute(name);
        return (attr != 0) ? attr->get_long() : fallback;
    }

    static bool
    read_bool(OfObject& object, const char *name, const bool& fallback)
    {
        const OfAttr *attr = object.get_attribute(name);
        return (attr != 0) ? attr->get_bool() : fallback;
    }

    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        WebBuilder web;
        web.radius = read_double(*object, "radius", 0.07);
        web.aspect = read_double(*object, "aspect", 0.88);
        web.radii_count = (unsigned int) read_long(*object, "radii", 32);
        web.asymmetry = read_double(*object, "asymmetry", 1.4);
        web.angle_asymmetry = read_double(*object, "angle_asymmetry", 1.57);
        web.depth = read_double(*object, "depth", 0.015);
        web.seed = (unsigned int) read_long(*object, "seed", 0);

        web.anchors = (unsigned int) read_long(*object, "anchors", 5);
        web.anchor_spread = read_double(*object, "anchor_spread", 0.35);
        web.frame_margin = read_double(*object, "frame_margin", 0.12);
        web.anchor_length = read_double(*object, "anchor_length", 0.4);

        web.hub = read_double(*object, "hub", 0.1);
        web.hub_turns = (unsigned int) read_long(*object, "hub_turns", 5);
        web.free_zone = read_double(*object, "free_zone", 0.3);

        web.spiral_pitch = read_double(*object, "spiral_pitch", 0.04);
        web.pitch_growth_top = read_double(*object, "pitch_growth_top", 0.9);
        web.pitch_growth_bottom = read_double(*object, "pitch_growth_bottom", 0.1);
        web.pitch_variation = read_double(*object, "pitch_variation", 0.3);
        web.spiral_uturns = (unsigned int) read_long(*object, "spiral_uturns", 11);
        web.free_sector = read_double(*object, "free_sector", 0.0);
        web.auxiliary = read_bool(*object, "auxiliary", false);
        web.auxiliary_turns = (unsigned int) read_long(*object, "auxiliary_turns", 7);

        web.deviated_radii = (unsigned int) read_long(*object, "deviated_radii", 3);
        web.y_radii = (unsigned int) read_long(*object, "y_radii", 2);
        web.extra_radii = (unsigned int) read_long(*object, "extra_radii", 1);
        web.deviation = read_double(*object, "deviation", 0.06);

        web.thread_radius = read_double(*object, "thread_radius", 0.0015);
        web.radius_variation = read_double(*object, "radius_variation", 0.4);
        web.frame_scale = read_double(*object, "frame_scale", 1.8);
        web.anchor_scale = read_double(*object, "anchor_scale", 2.0);
        web.spiral_scale = read_double(*object, "spiral_scale", 0.7);

        if (web.radii_count < 3u) web.radii_count = 3u;
        if (web.free_zone >= 0.95) web.free_zone = 0.95;
        if (web.hub > web.free_zone) web.hub = web.free_zone * 0.6;

        web.build();
        if (web.threads.get_count() == 0u) return 0;

        unsigned int sides = (unsigned int) read_long(*object, "sides", 5);
        if (sides < 3u) sides = 3u;

        return sweep(web.threads, sides);
    }

    // Le balayage. Les fils n'ont pas tous le meme nombre de points -- la
    // spirale en a des centaines la ou un fil d'amarrage en a six -- donc on
    // compte d'abord, puis on alloue une fois.
    static PolyMesh *
    sweep(const CoreVector<Thread>& threads, const unsigned int& sides)
    {
        CoreVector<Path> paths;
        CoreVector<unsigned int> rings;

        unsigned int total_vertices = 0u;
        unsigned int total_quads = 0u;

        for (unsigned int i = 0; i < threads.get_count(); i++) {
            Path path;
            if (!path.build(threads[i].points, false, 1u)) continue;
            const unsigned int count = path.get_sample_count();
            if (count < 2u) continue;
            paths.add(path);
            rings.add(count);
            total_vertices += count * sides;
            total_quads += (count - 1u) * sides;
        }
        if (total_quads == 0u) return 0;

        const unsigned int uv_cols = sides + 1u;
        unsigned int total_uvs = 0u;
        for (unsigned int i = 0; i < rings.get_count(); i++) {
            total_uvs += rings[i] * uv_cols;
        }

        CoreArray<GMathVec3f> vertices(total_vertices);
        CoreArray<GMathVec3f> velocities;
        CoreArray<unsigned int> polygon_indices(total_quads * 4u);
        CoreArray<unsigned int> polygon_vertex_count(total_quads);
        CoreArray<unsigned int> polygon_shading_groups(total_quads);
        CoreArray<GMathVec3f> uvs(total_uvs);
        CoreArray<unsigned int> uv_indices(total_quads * 4u);

        CoreArray<CoreString> shading_group_names(GROUP_COUNT);
        shading_group_names[GROUP_ANCHOR] = "amarrage";
        shading_group_names[GROUP_FRAME]  = "cadre";
        shading_group_names[GROUP_RADIUS] = "rayon";
        shading_group_names[GROUP_HUB]    = "moyeu";
        shading_group_names[GROUP_SPIRAL] = "spirale";

        unsigned int base = 0u;
        unsigned int uv_base = 0u;
        unsigned int corner = 0u;
        unsigned int face = 0u;
        unsigned int used = 0u;

        for (unsigned int i = 0; i < threads.get_count(); i++) {
            if (used >= paths.get_count()) break;
            if (threads[i].points.get_count() < 2u) continue;

            const Path& path = paths[used];
            const unsigned int count = rings[used];
            const double thread_radius = threads[i].radius;
            const unsigned int group = threads[i].group;
            used++;

            for (unsigned int r = 0; r < count; r++) {
                const GMathVec3d& centre = path.get_position(r);
                const GMathVec3d& normal = path.get_normal(r);
                const GMathVec3d binormal = path.get_binormal(r);

                for (unsigned int s = 0; s < sides; s++) {
                    const double angle = 2.0 * M_PI * double(s) / double(sides);
                    const GMathVec3d p =
                        vadd(centre,
                             vadd(vscale(normal, cos(angle) * thread_radius),
                                  vscale(binormal, sin(angle) * thread_radius)));
                    vertices[base + r * sides + s] =
                        GMathVec3f(float(p[0]), float(p[1]), float(p[2]));
                }

                const float v = float(r) / float(count - 1u);
                for (unsigned int s = 0; s < uv_cols; s++) {
                    uvs[uv_base + r * uv_cols + s] =
                        GMathVec3f(float(s) / float(sides), v, 0.0f);
                }
            }

            for (unsigned int r = 0; r + 1u < count; r++) {
                for (unsigned int s = 0; s < sides; s++) {
                    const unsigned int s1 = (s + 1u) % sides;
                    polygon_indices[corner + 0u] = base + r * sides + s;
                    polygon_indices[corner + 1u] = base + r * sides + s1;
                    polygon_indices[corner + 2u] = base + (r + 1u) * sides + s1;
                    polygon_indices[corner + 3u] = base + (r + 1u) * sides + s;

                    uv_indices[corner + 0u] = uv_base + r * uv_cols + s;
                    uv_indices[corner + 1u] = uv_base + r * uv_cols + s + 1u;
                    uv_indices[corner + 2u] = uv_base + (r + 1u) * uv_cols + s + 1u;
                    uv_indices[corner + 3u] = uv_base + (r + 1u) * uv_cols + s;

                    polygon_vertex_count[face] = 4u;
                    polygon_shading_groups[face] = group;
                    face++;
                    corner += 4u;
                }
            }

            base += count * sides;
            uv_base += count * uv_cols;
        }

        CoreArray<GeometryUvMap> uv_maps(1);
        uv_maps[0].name = "uv";
        uv_maps[0].vertices = uvs;
        uv_maps[0].polygon_indices = uv_indices;

        CoreArray<GeometryNormalMap> normal_maps;
        CoreArray<GeometryColorMap> color_maps;

        PolyMesh *mesh = new PolyMesh;
        mesh->set(vertices, velocities,
                  polygon_indices, polygon_vertex_count, polygon_shading_groups,
                  shading_group_names,
                  uv_maps, normal_maps, color_maps,
                  true, 0);
        return mesh;
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryWeb, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryWeb)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryWeb);
    new_classes.add(new_class);

    IX_MODULE_CLBK *module_callbacks;
    IX_CREATE_MODULE_CLBK(new_class, module_callbacks)
    module_callbacks->cb_create_module = IX_MODULE_CLBK::declare_module;
    module_callbacks->cb_destroy_module = IX_MODULE_CLBK::destroy_module;
}

IX_END_EXTERN_C

OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    WebModule *module = new WebModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
