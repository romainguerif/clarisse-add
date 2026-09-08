// Un solveur de tissu en positions, pour les nodes qui ont besoin de vraies
// formes de tissu plutot que d'un bombement peint.
//
// C'est du XPBD -- Macklin, Muller, Chentanez 2016 -- et non du PBD de 2007.
// La difference tient en un flottant par contrainte, et elle est decisive pour
// un outil d'artiste : en PBD, la raideur effective depend du nombre
// d'iterations, donc bouger le curseur de qualite change la forme. En XPBD le
// point fixe de l'iteration est l'equilibre statique exact, et la qualite ne
// fait plus que converger vers lui. C'est la difference entre un outil qu'on
// regle et un outil qu'on subit.
//
// Trois choix qui meritent d'etre expliques :
//
//   - Le pas de temps est fixe a un et n'est jamais expose. On ne simule pas
//     une trajectoire, on cherche un equilibre : a l'etat stationnaire, dt
//     disparait de l'equation et il ne reste que « forces elastiques plus
//     forces exterieures egalent zero ». Ce qui fait la matiere, ce sont les
//     rapports entre les compliances, pas leur valeur absolue.
//
//   - La flexion est celle de Bergou et al. 2006, dite isometrique : quatre
//     scalaires precalcules sur le patron a plat, un gradient lineaire, aucune
//     fonction transcendante. La formulation en angle diedre de Muller est un
//     zero sur zero exactement quand le tissu est plat -- c'est-a-dire dans
//     notre etat de depart, pas dans un cas rare.
//
//   - Rien n'est aleatoire ici. Le solveur est une fonction pure de son entree :
//     memes positions, memes contraintes, meme nombre d'iterations, meme
//     resultat au bit pres. Ce qui casse la symetrie du plan est fourni par
//     l'appelant, sous forme d'un bruit a graine explicite.

#ifndef CLARISSE_ADD_CLOTH_SOLVER_H
#define CLARISSE_ADD_CLOTH_SOLVER_H

#include <core_array.h>
#include <core_vector.h>
#include <gmath_vec3.h>

#include <cmath>

namespace cloth {

inline GMathVec3d csub(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

inline GMathVec3d cadd(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}

inline GMathVec3d cscale(const GMathVec3d& a, const double& s)
{
    return GMathVec3d(a[0] * s, a[1] * s, a[2] * s);
}

inline double cdot(const GMathVec3d& a, const GMathVec3d& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline GMathVec3d ccross(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[1] * b[2] - a[2] * b[1],
                      a[2] * b[0] - a[0] * b[2],
                      a[0] * b[1] - a[1] * b[0]);
}

// Une arete a longueur imposee. Sert au tissage comme au cisaillement : seules
// la longueur au repos et la compliance changent.
struct Distance {
    unsigned int a;
    unsigned int b;
    double rest;
    double compliance;
    double lambda;
};

// La flexion isometrique. Les quatre coefficients sont calcules une fois sur la
// pose de repos ; leur somme vaut zero, ce qui rend la contrainte insensible
// aux translations. La contrainte est |S| avec S la combinaison ponderee des
// quatre sommets -- nulle quand le tissu est dans sa pose de repos.
struct Bend {
    unsigned int index[4];
    double weight[4];
    double compliance;
    double lambda;
};

// Un rappel elastique vers une position de reference : le tissu pose sur
// quelque chose de mou, qui le repousse quand il s'en ecarte.
//
// C'est ce qui distingue un coussin rembourre d'un ballon, et ce n'est pas un
// detail : une enveloppe close remplie de gaz a qui on donne du tissu en trop
// ne se plisse pas, elle se deforme en une seule grande bosse -- le volume est
// conserve, mais rien n'interdit a la forme de changer. Une ouate, elle,
// resiste au changement de forme et pas seulement de volume. Elle donne au
// flambage la fondation elastique sans laquelle il n'a aucune longueur d'onde
// preferee : c'est le K de la loi de Cerda-Mahadevan, et sans K la longueur
// d'onde part a l'infini -- autrement dit, un seul grand pli.
struct Anchor {
    unsigned int index;
    GMathVec3d target;
    double compliance;
    double lambda;
};

// La pression interne d'un volume ferme. C'est elle qui gonfle un coussin, et
// c'est d'elle que naissent les plis : la membrane a plus de surface que le
// volume n'en demande, et le surplus doit aller quelque part.
struct Pressure {
    CoreVector<unsigned int> triangles;   // trois indices par triangle
    double rest_volume;                   // six fois le volume, non normalise
    double target;                        // multiplicateur : au-dela de 1, ca gonfle
    double compliance;
    // La meme chose, mais exprimee relativement a la raideur propre de la
    // contrainte plutot qu'en unites absolues. Le gradient du volume est une
    // somme d'aires : sa norme depend de la taille du coussin et du pas de la
    // grille, si bien qu'une compliance absolue ne veut pas dire la meme chose
    // d'un maillage a l'autre. Multipliee par le denominateur, elle devient un
    // pur facteur de relachement -- et surtout, elle laisse la pression ceder
    // quand le tissu est deja tendu. Sans cela, un volume vise inatteignable et
    // des aretes inextensibles se battent indefiniment, et le solveur ne
    // converge pas : il broie.
    double softness;
    double lambda;
};

class Solver {
public:
    Solver() : m_has_pressure(false) {}

    CoreVector<GMathVec3d> positions;
    CoreVector<double> inverse_mass;      // zero pour un point epingle
    CoreVector<Distance> distances;
    CoreVector<Bend> bends;
    CoreVector<Anchor> anchors;
    Pressure pressure;

    inline void enable_pressure(const bool& on) { m_has_pressure = on; }

    // Precalcule les quatre coefficients de flexion pour une arete interieure.
    // `a` et `b` portent l'arete, `c` et `d` sont les sommets opposes des deux
    // triangles qui la partagent.
    //
    // Les cotangentes viennent de la pose de repos et n'en bougent plus : c'est
    // ce qui rend la contrainte lineaire, et donc bien conditionnee meme quand
    // le tissu est parfaitement plat.
    void
    add_bend(const unsigned int& a, const unsigned int& b,
             const unsigned int& c, const unsigned int& d,
             const double& compliance)
    {
        const GMathVec3d& p0 = positions[a];
        const GMathVec3d& p1 = positions[b];
        const GMathVec3d& p2 = positions[c];
        const GMathVec3d& p3 = positions[d];

        const GMathVec3d e0 = csub(p1, p0);
        const GMathVec3d e1 = csub(p2, p0);
        const GMathVec3d e2 = csub(p3, p0);
        const GMathVec3d e3 = csub(p2, p1);
        const GMathVec3d e4 = csub(p3, p1);

        const double c01 = cotangent(e0, e1);
        const double c02 = cotangent(e0, e2);
        const double c03 = cotangent(cscale(e0, -1.0), e3);
        const double c04 = cotangent(cscale(e0, -1.0), e4);

        Bend bend;
        bend.index[0] = a;
        bend.index[1] = b;
        bend.index[2] = c;
        bend.index[3] = d;
        bend.weight[0] = c03 + c04;
        bend.weight[1] = c01 + c02;
        bend.weight[2] = -c01 - c03;
        bend.weight[3] = -c02 - c04;
        bend.compliance = compliance;
        bend.lambda = 0.0;
        bends.add(bend);
    }

    // Six fois le volume signe de l'enveloppe, par la formule du produit mixte.
    // Non normalise a dessein : le facteur six se simplifie tant qu'on compare
    // toujours des volumes calcules de la meme facon.
    double
    compute_volume() const
    {
        double total = 0.0;
        const unsigned int count = pressure.triangles.get_count() / 3u;
        for (unsigned int t = 0; t < count; t++) {
            const GMathVec3d& a = positions[pressure.triangles[t * 3u]];
            const GMathVec3d& b = positions[pressure.triangles[t * 3u + 1u]];
            const GMathVec3d& c = positions[pressure.triangles[t * 3u + 2u]];
            total += cdot(ccross(a, b), c);
        }
        return total;
    }

    // La relaxation. Un seul pas, `iterations` projections, les multiplicateurs
    // accumules d'un bout a l'autre : c'est ce qui fait converger vers
    // l'equilibre statique plutot que vers un compromis dependant du budget.
    void
    solve(const unsigned int& iterations, const GMathVec3d& gravity)
    {
        const unsigned int count = positions.get_count();
        if (count == 0u) return;

        // La gravite est appliquee une fois, comme un deplacement initial. On
        // ne l'integre pas dans le temps : il n'y a pas de trajectoire, juste
        // une position de depart deja penchee dans le bon sens.
        for (unsigned int i = 0; i < count; i++) {
            if (inverse_mass[i] > 0.0) {
                positions[i] = cadd(positions[i], gravity);
            }
        }

        for (unsigned int i = 0; i < distances.get_count(); i++) {
            distances[i].lambda = 0.0;
        }
        for (unsigned int i = 0; i < bends.get_count(); i++) {
            bends[i].lambda = 0.0;
        }
        for (unsigned int i = 0; i < anchors.get_count(); i++) {
            anchors[i].lambda = 0.0;
        }
        pressure.lambda = 0.0;

        CoreArray<GMathVec3d> gradient;
        if (m_has_pressure) gradient.resize(count);

        for (unsigned int it = 0; it < iterations; it++) {
            // Balayage alterne. L'ordre de Gauss-Seidel biaise la propagation
            // dans le sens du parcours ; l'inverser une fois sur deux symetrise
            // le resultat sans rien couter.
            const bool forward = (it % 2u) == 0u;
            project_distances(forward);
            project_bends(forward);
            project_anchors(forward);
            if (m_has_pressure) project_pressure(gradient);
        }
    }

private:
    bool m_has_pressure;

    static double
    cotangent(const GMathVec3d& u, const GMathVec3d& v)
    {
        const GMathVec3d c = ccross(u, v);
        const double norm = c.get_length();
        return cdot(u, v) / ((norm > 1e-12) ? norm : 1e-12);
    }

    void
    project_distances(const bool& forward)
    {
        const unsigned int total = distances.get_count();
        for (unsigned int k = 0; k < total; k++) {
            Distance& c = distances[forward ? k : (total - 1u - k)];

            const double wa = inverse_mass[c.a];
            const double wb = inverse_mass[c.b];
            const double w = wa + wb;
            if (w <= 0.0) continue;

            const GMathVec3d delta = csub(positions[c.a], positions[c.b]);
            const double length = delta.get_length();
            if (length < 1e-12) continue;

            const GMathVec3d n = cscale(delta, 1.0 / length);
            const double violation = length - c.rest;

            const double denominator = w + c.compliance;
            const double d_lambda =
                (-violation - c.compliance * c.lambda) / denominator;
            c.lambda += d_lambda;

            positions[c.a] = cadd(positions[c.a], cscale(n, wa * d_lambda));
            positions[c.b] = csub(positions[c.b], cscale(n, wb * d_lambda));
        }
    }

    void
    project_anchors(const bool& forward)
    {
        const unsigned int total = anchors.get_count();
        for (unsigned int k = 0; k < total; k++) {
            Anchor& c = anchors[forward ? k : (total - 1u - k)];

            const double w = inverse_mass[c.index];
            if (w <= 0.0) continue;

            const GMathVec3d delta = csub(positions[c.index], c.target);
            const double length = delta.get_length();
            if (length < 1e-12) continue;

            const GMathVec3d n = cscale(delta, 1.0 / length);
            const double d_lambda =
                (-length - c.compliance * c.lambda) / (w + c.compliance);
            c.lambda += d_lambda;

            positions[c.index] =
                cadd(positions[c.index], cscale(n, w * d_lambda));
        }
    }

    void
    project_bends(const bool& forward)
    {
        const unsigned int total = bends.get_count();
        for (unsigned int k = 0; k < total; k++) {
            Bend& c = bends[forward ? k : (total - 1u - k)];

            GMathVec3d s(0.0, 0.0, 0.0);
            double denominator = 0.0;
            for (unsigned int j = 0; j < 4u; j++) {
                s = cadd(s, cscale(positions[c.index[j]], c.weight[j]));
                denominator += inverse_mass[c.index[j]]
                             * c.weight[j] * c.weight[j];
            }
            if (denominator <= 0.0) continue;

            const double violation = s.get_length();
            if (violation < 1e-12) continue;

            const GMathVec3d direction = cscale(s, 1.0 / violation);
            const double d_lambda =
                (-violation - c.compliance * c.lambda)
                / (denominator + c.compliance);
            c.lambda += d_lambda;

            for (unsigned int j = 0; j < 4u; j++) {
                const double w = inverse_mass[c.index[j]];
                if (w <= 0.0) continue;
                positions[c.index[j]] =
                    cadd(positions[c.index[j]],
                         cscale(direction, w * c.weight[j] * d_lambda));
            }
        }
    }

    // La pression est une contrainte globale : elle touche tous les sommets a
    // la fois, donc elle se projette en deux passes -- accumuler les gradients,
    // puis appliquer.
    void
    project_pressure(CoreArray<GMathVec3d>& gradient)
    {
        const unsigned int count = positions.get_count();
        const unsigned int triangles = pressure.triangles.get_count() / 3u;
        if (triangles == 0u) return;

        for (unsigned int i = 0; i < count; i++) {
            gradient[i] = GMathVec3d(0.0, 0.0, 0.0);
        }

        double volume = 0.0;
        for (unsigned int t = 0; t < triangles; t++) {
            const unsigned int ia = pressure.triangles[t * 3u];
            const unsigned int ib = pressure.triangles[t * 3u + 1u];
            const unsigned int ic = pressure.triangles[t * 3u + 2u];

            const GMathVec3d& a = positions[ia];
            const GMathVec3d& b = positions[ib];
            const GMathVec3d& c = positions[ic];

            volume += cdot(ccross(a, b), c);

            gradient[ia] = cadd(gradient[ia], ccross(b, c));
            gradient[ib] = cadd(gradient[ib], ccross(c, a));
            gradient[ic] = cadd(gradient[ic], ccross(a, b));
        }

        double denominator = 0.0;
        for (unsigned int i = 0; i < count; i++) {
            const double w = inverse_mass[i];
            if (w <= 0.0) continue;
            denominator += w * cdot(gradient[i], gradient[i]);
        }
        if (denominator <= 1e-18) return;

        const double alpha =
            pressure.compliance + pressure.softness * denominator;

        const double violation = volume - pressure.target * pressure.rest_volume;
        const double d_lambda =
            (-violation - alpha * pressure.lambda) / (denominator + alpha);
        pressure.lambda += d_lambda;

        for (unsigned int i = 0; i < count; i++) {
            const double w = inverse_mass[i];
            if (w <= 0.0) continue;
            positions[i] = cadd(positions[i],
                                cscale(gradient[i], w * d_lambda));
        }
    }
};

} // namespace cloth

#endif
