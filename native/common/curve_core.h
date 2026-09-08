// Le coeur commun a tous les nodes de courbe de ClarisseAdd.
//
// Les quatre usages qu'on vise -- balayer un tube, distribuer des objets le
// long, faire courir un item sur un chemin, plier une geometrie -- ne partagent
// qu'une seule chose : pour un parametre, rendre une position, une tangente et
// un repere. Tout le reste en decoule. D'ou ce fichier, plutot que trois copies
// qui divergeraient.
//
// Deux points qui font la difference entre un resultat utilisable et un
// resultat rate, et qui sont la raison d'etre de ce fichier :
//
//   - le repere. Frenet-Serret se retourne aux points d'inflexion et n'est pas
//     defini quand la courbure s'annule, donc sur toute portion droite -- le
//     cas ordinaire d'un cable. On transporte un repere a torsion minimale par
//     la methode du double reflet (Wang et al. 2008) : deux reflexions, aucune
//     trigonometrie, stable partout.
//
//   - la longueur d'arc. Une abscisse uniforme sur une Catmull-Rom ne donne PAS
//     un espacement uniforme : les objets distribues se tassent dans les
//     virages. La table de longueurs cumulees construite ici sert a
//     reparametrer, et elle est indispensable des qu'on distribue ou qu'on fait
//     avancer quelque chose a vitesse constante.

#ifndef CLARISSE_ADD_CURVE_CORE_H
#define CLARISSE_ADD_CURVE_CORE_H

#define _USE_MATH_DEFINES
#include <cmath>

#include <of_object.h>
#include <of_attr.h>
#include <module_scene_item.h>

#include <core_array.h>
#include <core_vector.h>
#include <gmath_vec3.h>

// _USE_MATH_DEFINES ne sert a rien si un autre en-tete a deja tire <cmath>
// avant nous, ce qui arrive des qu'on inclut le SDK. On garantit la constante
// plutot que de dependre de l'ordre d'inclusion.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace curve_core {

// Les operateurs binaires de GMathVec3 ne sont pas tous garantis par les
// en-tetes reconstruits. On reste sur operator[], qui l'est.
inline GMathVec3d vsub(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

inline GMathVec3d vadd(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}

inline GMathVec3d vscale(const GMathVec3d& a, const double& s)
{
    return GMathVec3d(a[0] * s, a[1] * s, a[2] * s);
}

inline double vdot(const GMathVec3d& a, const GMathVec3d& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline GMathVec3d vcross(const GMathVec3d& a, const GMathVec3d& b)
{
    return GMathVec3d(a[1] * b[2] - a[2] * b[1],
                      a[2] * b[0] - a[0] * b[2],
                      a[0] * b[1] - a[1] * b[0]);
}

inline GMathVec3d vnorm(const GMathVec3d& a)
{
    const double l = a.get_length();
    return (l > 1e-12) ? vscale(a, 1.0 / l) : GMathVec3d(0.0, 0.0, 1.0);
}

inline GMathVec3d vlerp(const GMathVec3d& a, const GMathVec3d& b, const double& t)
{
    return GMathVec3d(a[0] + (b[0] - a[0]) * t,
                      a[1] + (b[1] - a[1]) * t,
                      a[2] + (b[2] - a[2]) * t);
}

// Catmull-Rom : la courbe passe par ses points de controle, ce qui est la seule
// chose que l'utilisateur attend quand il pose un locator quelque part.
inline GMathVec3d catmull_rom(const GMathVec3d& p0, const GMathVec3d& p1,
                              const GMathVec3d& p2, const GMathVec3d& p3,
                              const double& u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    GMathVec3d r;
    for (unsigned int k = 0; k < 3; k++) {
        r[k] = 0.5 * (2.0 * p1[k]
                      + (-p0[k] + p2[k]) * u
                      + (2.0 * p0[k] - 5.0 * p1[k] + 4.0 * p2[k] - p3[k]) * u2
                      + (-p0[k] + 3.0 * p1[k] - 3.0 * p2[k] + p3[k]) * u3);
    }
    return r;
}

inline GMathVec3d catmull_rom_tangent(const GMathVec3d& p0, const GMathVec3d& p1,
                                      const GMathVec3d& p2, const GMathVec3d& p3,
                                      const double& u)
{
    const double u2 = u * u;
    GMathVec3d r;
    for (unsigned int k = 0; k < 3; k++) {
        r[k] = 0.5 * ((-p0[k] + p2[k])
                      + 2.0 * (2.0 * p0[k] - 5.0 * p1[k] + 4.0 * p2[k] - p3[k]) * u
                      + 3.0 * (-p0[k] + 3.0 * p1[k] - 3.0 * p2[k] + p3[k]) * u2);
    }
    return r;
}

// Indice d'un point de controle voisin : on replie aux extremites pour une
// courbe ouverte, on boucle pour une courbe fermee.
inline unsigned int
control_index(const unsigned int& span, const int& offset,
              const unsigned int& count, const bool& closed)
{
    int i = int(span) + offset;
    if (closed) {
        const int n = int(count);
        return (unsigned int) (((i % n) + n) % n);
    }
    if (i < 0) i = 0;
    if (i > int(count) - 1) i = int(count) - 1;
    return (unsigned int) i;
}

// Transport du repere par double reflet. La premiere reflexion amene le repere
// du plan de l'echantillon courant vers celui du suivant, la seconde corrige
// l'ecart de tangente restant.
inline GMathVec3d
transport(const GMathVec3d& x0, const GMathVec3d& x1,
          const GMathVec3d& t0, const GMathVec3d& t1, const GMathVec3d& r0)
{
    const GMathVec3d v1 = vsub(x1, x0);
    const double c1 = vdot(v1, v1);
    if (c1 < 1e-24) return r0;

    const GMathVec3d rL = vsub(r0, vscale(v1, 2.0 * vdot(v1, r0) / c1));
    const GMathVec3d tL = vsub(t0, vscale(v1, 2.0 * vdot(v1, t0) / c1));

    const GMathVec3d v2 = vsub(t1, tL);
    const double c2 = vdot(v2, v2);
    if (c2 < 1e-24) return vnorm(rL);

    return vnorm(vsub(rL, vscale(v2, 2.0 * vdot(v2, rL) / c2)));
}

// Les positions monde d'une liste de points de controle, dans l'ordre.
// L'attribut vise est un `list<reference>` filtre sur SceneItem.
inline bool
gather_control_points(OfObject& object, const char *attr_name,
                      CoreVector<GMathVec3d>& points)
{
    OfAttr *attr = object.get_attribute(attr_name);
    if (attr == 0) return false;

    const unsigned int count = attr->get_value_count();
    for (unsigned int i = 0; i < count; i++) {
        OfObject *item = attr->get_object(i);
        if (item == 0) continue;
        ModuleSceneItem *module = item->get_module<ModuleSceneItem>();
        if (module == 0) continue;
        GMathVec3d position;
        module->get_global_position(position);
        points.add(position);
    }
    return points.get_count() >= 2;
}


// Une courbe echantillonnee, avec son repere et sa table de longueurs.
//
// L'echantillonnage est uniforme en parametre ; c'est la table `arc` qui permet
// ensuite d'interroger la courbe a une distance donnee. Les deux acces
// coexistent parce qu'ils servent a des choses differentes : le balayage d'un
// tube veut des anneaux reguliers en parametre, une distribution veut un
// espacement regulier en longueur.
class Path {
public:
    Path() : m_length(0.0), m_closed(false) {}

    // `steps` subdivisions par portion. Rend false si la courbe est degeneree.
    bool
    build(const CoreVector<GMathVec3d>& cv, const bool& closed,
          unsigned int steps)
    {
        m_closed = closed;
        if (steps < 1u) steps = 1u;

        const unsigned int cp_count = cv.get_count();
        if (cp_count < 2u) return false;

        const unsigned int span_count = closed ? cp_count : cp_count - 1u;
        const unsigned int count = closed ? span_count * steps
                                          : span_count * steps + 1u;
        if (count < 2u) return false;

        m_position.resize(count);
        m_tangent.resize(count);
        m_normal.resize(count);
        m_arc.resize(count);

        for (unsigned int r = 0; r < count; r++) {
            const bool last = (!closed && r == count - 1u);
            const unsigned int span = last ? span_count - 1u
                                           : (r / steps) % span_count;
            const double u = last ? 1.0 : double(r % steps) / double(steps);

            const GMathVec3d& p0 = cv[control_index(span, -1, cp_count, closed)];
            const GMathVec3d& p1 = cv[control_index(span,  0, cp_count, closed)];
            const GMathVec3d& p2 = cv[control_index(span,  1, cp_count, closed)];
            const GMathVec3d& p3 = cv[control_index(span,  2, cp_count, closed)];

            m_position[r] = catmull_rom(p0, p1, p2, p3, u);
            m_tangent[r] = vnorm(catmull_rom_tangent(p0, p1, p2, p3, u));
        }

        // Amorce du repere : n'importe quelle direction non colineaire a la
        // tangente fera l'affaire, le transport se charge du reste.
        GMathVec3d seed(0.0, 0.0, 1.0);
        if (fabs(vdot(m_tangent[0], seed)) > 0.9) seed = GMathVec3d(1.0, 0.0, 0.0);
        m_normal[0] = vnorm(vcross(vcross(m_tangent[0], seed), m_tangent[0]));
        for (unsigned int r = 0; r + 1u < count; r++) {
            m_normal[r + 1u] = transport(m_position[r], m_position[r + 1u],
                                         m_tangent[r], m_tangent[r + 1u],
                                         m_normal[r]);
        }

        m_arc[0] = 0.0;
        for (unsigned int r = 1; r < count; r++) {
            m_arc[r] = m_arc[r - 1u]
                     + vsub(m_position[r], m_position[r - 1u]).get_length();
        }
        m_length = m_arc[count - 1u];
        return true;
    }

    inline unsigned int get_sample_count() const { return m_position.get_count(); }
    inline const GMathVec3d& get_position(const unsigned int& i) const { return m_position[i]; }
    inline const GMathVec3d& get_tangent(const unsigned int& i) const { return m_tangent[i]; }
    inline const GMathVec3d& get_normal(const unsigned int& i) const { return m_normal[i]; }
    inline const GMathVec3d get_binormal(const unsigned int& i) const {
        return vnorm(vcross(m_tangent[i], m_normal[i]));
    }
    inline const double& get_length() const { return m_length; }
    inline const double& get_arc_length(const unsigned int& i) const { return m_arc[i]; }
    inline const bool& is_closed() const { return m_closed; }

    // Interrogation a une distance curviligne, en metres depuis le depart.
    // C'est cet acces-la qu'il faut pour distribuer ou faire avancer : il
    // garantit un espacement reel constant, ce qu'un parametre uniforme ne fait
    // pas.
    void
    eval_at_distance(double distance, GMathVec3d& position, GMathVec3d& tangent,
                     GMathVec3d& normal) const
    {
        const unsigned int count = m_position.get_count();
        if (count == 0u) return;
        if (count == 1u || m_length <= 1e-12) {
            position = m_position[0];
            tangent = m_tangent[0];
            normal = m_normal[0];
            return;
        }

        if (distance <= 0.0) distance = 0.0;
        if (distance >= m_length) distance = m_length;

        // Recherche dichotomique du segment qui contient cette distance.
        unsigned int lo = 0u, hi = count - 1u;
        while (hi - lo > 1u) {
            const unsigned int mid = (lo + hi) / 2u;
            if (m_arc[mid] <= distance) lo = mid; else hi = mid;
        }

        const double span = m_arc[hi] - m_arc[lo];
        const double t = (span > 1e-12) ? (distance - m_arc[lo]) / span : 0.0;

        position = vlerp(m_position[lo], m_position[hi], t);
        tangent = vnorm(vlerp(m_tangent[lo], m_tangent[hi], t));
        normal = vnorm(vlerp(m_normal[lo], m_normal[hi], t));
        // Reorthogonaliser : l'interpolation lineaire de deux reperes n'en
        // rend pas un.
        normal = vnorm(vcross(vcross(tangent, normal), tangent));
    }

    // Le point de la courbe le plus proche d'une position, rendu par sa
    // distance curviligne. Recherche sur les segments echantillonnes, ce qui
    // suffit tant que l'echantillonnage est assez fin -- c'est le compromis
    // habituel pour un deformeur.
    double
    project(const GMathVec3d& point) const
    {
        const unsigned int count = m_position.get_count();
        if (count < 2u) return 0.0;

        double best_distance = 0.0;
        double best_squared = -1.0;

        for (unsigned int i = 0; i + 1u < count; i++) {
            const GMathVec3d seg = vsub(m_position[i + 1u], m_position[i]);
            const double len2 = vdot(seg, seg);
            double t = 0.0;
            if (len2 > 1e-24) {
                t = vdot(vsub(point, m_position[i]), seg) / len2;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
            }
            const GMathVec3d projected = vadd(m_position[i], vscale(seg, t));
            const GMathVec3d delta = vsub(point, projected);
            const double squared = vdot(delta, delta);
            if (best_squared < 0.0 || squared < best_squared) {
                best_squared = squared;
                best_distance = m_arc[i] + (m_arc[i + 1u] - m_arc[i]) * t;
            }
        }
        return best_distance;
    }

private:
    CoreArray<GMathVec3d> m_position;
    CoreArray<GMathVec3d> m_tangent;
    CoreArray<GMathVec3d> m_normal;
    CoreArray<double> m_arc;
    double m_length;
    bool m_closed;
};

} // namespace curve_core

#endif
