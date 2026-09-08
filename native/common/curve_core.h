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

        finalize();
        return true;
    }

    // Variante en segments droits raccordes par des arcs, comme la tuyauterie
    // industrielle : un tuyau n'est pas une spline, c'est une suite de droites
    // et de coudes normalises. `radius` est le rayon du coude, `arc_steps` sa
    // finesse ; les portions droites ne portent que leurs deux extremites,
    // puisqu'il n'y a rien a y decrire.
    bool
    build_beveled(const CoreVector<GMathVec3d>& cv, const bool& closed,
                  const double& radius, unsigned int arc_steps)
    {
        m_closed = closed;
        if (arc_steps < 1u) arc_steps = 1u;

        const unsigned int n = cv.get_count();
        if (n < 2u) return false;

        // Le retrait d'un coin depend de son angle : d = r / tan(alpha/2), ou
        // alpha est l'angle entre les deux branches. On le borne ensuite a un
        // peu moins de la moitie de chaque segment voisin, sinon deux coudes
        // proches se mangent l'un l'autre et la courbe se replie.
        CoreVector<GMathVec3d> pts;

        const unsigned int first = closed ? 0u : 1u;
        const unsigned int last = closed ? n : n - 1u;

        if (!closed) pts.add(cv[0]);

        for (unsigned int i = first; i < last; i++) {
            const GMathVec3d& corner = cv[i % n];
            const GMathVec3d& before = cv[(i + n - 1u) % n];
            const GMathVec3d& after = cv[(i + 1u) % n];

            const GMathVec3d in = vsub(before, corner);
            const GMathVec3d out = vsub(after, corner);
            const double in_len = in.get_length();
            const double out_len = out.get_length();
            if (in_len < 1e-9 || out_len < 1e-9) { pts.add(corner); continue; }

            const GMathVec3d u = vscale(in, 1.0 / in_len);
            const GMathVec3d w = vscale(out, 1.0 / out_len);

            double cosine = vdot(u, w);
            if (cosine > 1.0) cosine = 1.0;
            if (cosine < -1.0) cosine = -1.0;
            const double alpha = acos(cosine);

            // Coin plat ou replie sur lui-meme : aucun arc a construire.
            if (alpha < 1e-4 || alpha > M_PI - 1e-4 || radius <= 1e-9) {
                pts.add(corner);
                continue;
            }

            double d = radius / tan(alpha * 0.5);
            const double limit = 0.49 * ((in_len < out_len) ? in_len : out_len);
            if (d > limit) d = limit;
            const double r = d * tan(alpha * 0.5);

            const GMathVec3d a = vadd(corner, vscale(u, d));
            const GMathVec3d b = vadd(corner, vscale(w, d));

            // Centre sur la bissectrice, a la distance qui rend l'arc tangent
            // aux deux branches.
            const GMathVec3d bisector = vnorm(vadd(u, w));
            const double half = alpha * 0.5;
            const double sine = sin(half);
            if (sine < 1e-9) { pts.add(corner); continue; }
            const GMathVec3d centre = vadd(corner, vscale(bisector, r / sine));

            const GMathVec3d ra = vsub(a, centre);
            const GMathVec3d rb = vsub(b, centre);
            const double arc_radius = ra.get_length();
            const GMathVec3d axis = vnorm(vcross(ra, rb));

            double span = vdot(vnorm(ra), vnorm(rb));
            if (span > 1.0) span = 1.0;
            if (span < -1.0) span = -1.0;
            const double sweep = acos(span);

            const GMathVec3d e0 = vnorm(ra);
            const GMathVec3d e1 = vnorm(vcross(axis, e0));

            for (unsigned int s = 0; s <= arc_steps; s++) {
                const double angle = sweep * double(s) / double(arc_steps);
                pts.add(vadd(centre, vscale(vadd(vscale(e0, cos(angle)),
                                                 vscale(e1, sin(angle))),
                                            arc_radius)));
            }
        }

        if (!closed) pts.add(cv[n - 1u]);

        const unsigned int count = pts.get_count();
        if (count < 2u) return false;

        m_position.resize(count);
        for (unsigned int i = 0; i < count; i++) m_position[i] = pts[i];

        // Tangentes par differences centrees : sur une polyligne, c'est la
        // seule definition qui ne saute pas aux jonctions droite/arc.
        m_tangent.resize(count);
        for (unsigned int i = 0; i < count; i++) {
            const unsigned int prev = (i == 0u) ? (closed ? count - 1u : 0u) : i - 1u;
            const unsigned int next = (i + 1u >= count) ? (closed ? 0u : count - 1u)
                                                       : i + 1u;
            m_tangent[i] = vnorm(vsub(m_position[next], m_position[prev]));
        }

        finalize();
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
    // Le repere et les longueurs cumulees, une fois les positions et les
    // tangentes en place. Commun aux deux facons de construire la courbe.
    void
    finalize()
    {
        const unsigned int count = m_position.get_count();
        m_normal.resize(count);
        m_arc.resize(count);
        if (count == 0u) { m_length = 0.0; return; }

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
    }

    CoreArray<GMathVec3d> m_position;
    CoreArray<GMathVec3d> m_tangent;
    CoreArray<GMathVec3d> m_normal;
    CoreArray<double> m_arc;
    double m_length;
    bool m_closed;
};

// Construit la courbe depuis les attributs que tous les nodes de courbe
// partagent : control_points, closed, steps, et le couple interpolation /
// bend_radius quand il est present. Passer par ici garantit qu'un tube, un
// nuage de points et un deformeur poses sur les memes locators suivent
// exactement le meme trace -- ce qui n'irait pas de soi si chacun relisait les
// attributs a sa facon.
inline bool
build_from_object(OfObject& object, Path& path)
{
    CoreVector<GMathVec3d> cv;
    if (!gather_control_points(object, "control_points", cv)) return false;

    const OfAttr *closed_attr = object.get_attribute("closed");
    const OfAttr *steps_attr = object.get_attribute("steps");
    const bool closed = (closed_attr != 0) ? closed_attr->get_bool() : false;
    unsigned int steps = (steps_attr != 0)
                       ? (unsigned int) steps_attr->get_long() : 8u;

    const OfAttr *interpolation = object.get_attribute("interpolation");
    if (interpolation != 0 && interpolation->get_long() == 1) {
        const OfAttr *bend = object.get_attribute("bend_radius");
        return path.build_beveled(cv, closed,
                                  (bend != 0) ? bend->get_double() : 0.0, steps);
    }
    return path.build(cv, closed, steps);
}

} // namespace curve_core

#endif
