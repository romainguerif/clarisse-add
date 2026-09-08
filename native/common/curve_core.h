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
#include <module_geometry.h>

#include <geometry_object.h>
#include <geometry_point_cloud.h>
#include <gmath_matrix4x4.h>

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

// --- bruit -----------------------------------------------------------------
//
// Un cable pose a la main n'est jamais lisse. Le bruit qui suit est ce qui
// separe une courbe mathematique d'un cable, et c'est le meilleur rapport
// resultat / lignes de code de tout le fichier.
//
// Trois precautions le rendent utilisable plutot que joli en apparence :
//
//   - il est projete dans le plan normal a la tangente. Une composante le long
//     de la courbe allongerait et raccourcirait le cable, ce qui ferait deriver
//     les UV et le pavage des modules poses dessus.
//   - sa frequence se compte en unites monde et non en parametre de courbe :
//     sinon un cable deux fois plus long aurait des ondulations deux fois plus
//     larges, alors que c'est la meme matiere.
//   - il s'eteint aux extremites, sur une largeur reglable. Un cable branche ne
//     bouge pas la ou il est accroche.

inline double
noise_fade(const double& t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

inline unsigned int
noise_hash(const int& x, const int& y, const int& z, const unsigned int& seed)
{
    unsigned int h = seed + 374761393u;
    h += (unsigned int) x * 3266489917u;
    h = ((h << 17) | (h >> 15)) * 668265263u;
    h += (unsigned int) y * 2246822519u;
    h = ((h << 13) | (h >> 19)) * 374761393u;
    h += (unsigned int) z * 3266489917u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

inline double
noise_gradient(const int& ix, const int& iy, const int& iz,
               const double& dx, const double& dy, const double& dz,
               const unsigned int& seed)
{
    const unsigned int h = noise_hash(ix, iy, iz, seed) & 15u;
    const double u = (h < 8u) ? dx : dy;
    const double v = (h < 4u) ? dy : ((h == 12u || h == 14u) ? dx : dz);
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}

// Perlin 3D classique. Rend a peu pres [-1, 1].
inline double
perlin(const double& x, const double& y, const double& z,
       const unsigned int& seed)
{
    const int ix = (int) floor(x);
    const int iy = (int) floor(y);
    const int iz = (int) floor(z);
    const double fx = x - double(ix);
    const double fy = y - double(iy);
    const double fz = z - double(iz);
    const double u = noise_fade(fx);
    const double v = noise_fade(fy);
    const double w = noise_fade(fz);

    const double n000 = noise_gradient(ix,      iy,      iz,      fx,       fy,       fz,       seed);
    const double n100 = noise_gradient(ix + 1,  iy,      iz,      fx - 1.0, fy,       fz,       seed);
    const double n010 = noise_gradient(ix,      iy + 1,  iz,      fx,       fy - 1.0, fz,       seed);
    const double n110 = noise_gradient(ix + 1,  iy + 1,  iz,      fx - 1.0, fy - 1.0, fz,       seed);
    const double n001 = noise_gradient(ix,      iy,      iz + 1,  fx,       fy,       fz - 1.0, seed);
    const double n101 = noise_gradient(ix + 1,  iy,      iz + 1,  fx - 1.0, fy,       fz - 1.0, seed);
    const double n011 = noise_gradient(ix,      iy + 1,  iz + 1,  fx,       fy - 1.0, fz - 1.0, seed);
    const double n111 = noise_gradient(ix + 1,  iy + 1,  iz + 1,  fx - 1.0, fy - 1.0, fz - 1.0, seed);

    const double x00 = n000 + u * (n100 - n000);
    const double x10 = n010 + u * (n110 - n010);
    const double x01 = n001 + u * (n101 - n001);
    const double x11 = n011 + u * (n111 - n011);
    const double y0 = x00 + v * (x10 - x00);
    const double y1 = x01 + v * (x11 - x01);
    return y0 + w * (y1 - y0);
}

// Somme d'octaves, chacune deux fois plus fine et `roughness` fois moins forte.
inline double
fbm(const GMathVec3d& p, const double& frequency, const unsigned int& octaves,
    const double& roughness, const unsigned int& seed)
{
    double total = 0.0;
    double amplitude = 1.0;
    double normalizer = 0.0;
    double f = frequency;
    for (unsigned int i = 0; i < octaves; i++) {
        total += amplitude * perlin(p[0] * f, p[1] * f, p[2] * f, seed + i * 7919u);
        normalizer += amplitude;
        amplitude *= roughness;
        f *= 2.0;
    }
    return (normalizer > 1e-9) ? total / normalizer : 0.0;
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

// L'attribut qui porte les points de controle, sous l'une ou l'autre de ses
// deux formes. Une table CID aplatit ses colonnes en attributs a part entiere :
// une table nommee control_points avec une colonne point donne un attribut
// `point`, pas `control_points`. Les nodes qui n'ont besoin que d'une suite de
// positions gardent la liste simple ; ceux qui portent des reglages par point
// passent a la table. Chercher les deux evite d'avoir a les convertir tous en
// meme temps.
inline OfAttr *
get_points_attr(OfObject& object)
{
    OfAttr *attr = object.get_attribute("point");
    if (attr != 0) return attr;
    return object.get_attribute("control_points");
}

// Les points d'une geometrie de la scene, quand le node en reference une. Ca
// ouvre la porte aux nuages de points : un Scatterer ou un GeometryPointCloud
// natif sur une surface fournit alors les points de controle, et il devient
// possible de generer des cables en masse au lieu de les poser un par un.
//
// L'ordre du nuage n'est pas celui d'un cable : un nuage disperse donne une
// courbe qui zigzague d'un bout a l'autre du volume. D'ou le chainage par plus
// proche voisin, qui traverse le nuage sans revenir sur ses pas. Il est en n
// carre, ce qui reste sans consequence a l'echelle ou on pose des points de
// controle -- quelques centaines au plus.
inline bool
gather_from_geometry(OfObject& object, CoreVector<GMathVec3d>& points)
{
    OfAttr *attr = object.get_attribute("points_geometry");
    if (attr == 0) return false;

    OfObject *source = attr->get_object();
    if (source == 0) return false;

    ModuleGeometry *module = source->get_module<ModuleGeometry>();
    if (module == 0) return false;

    const GeometryObject *geometry = module->get_geometry(false);
    if (geometry == 0) return false;

    const GeometryPointCloud *cloud = geometry->get_point_cloud();
    if (cloud == 0) return false;

    const unsigned int count = cloud->get_point_count();
    if (count < 2u) return false;

    // Les positions du nuage sont dans l'espace de l'objet ; il faut la matrice
    // de l'item pour les remettre dans le monde, ou la courbe ignorerait le
    // deplacement du nuage.
    ModuleSceneItem *item = source->get_module<ModuleSceneItem>();
    const GMathMatrix4x4d *matrix =
        (item != 0) ? &item->get_global_matrix() : 0;

    CoreVector<GMathVec3d> raw;
    for (unsigned int i = 0; i < count; i++) {
        // static_cast et non double(...) : `double(p[0])` se lit aussi comme
        // la declaration d'un tableau, et le compilateur choisit cette
        // lecture-la.
        const GMathVec3f sample = cloud->get_position(i);
        GMathVec3d world(static_cast<double>(sample[0]),
                         static_cast<double>(sample[1]),
                         static_cast<double>(sample[2]));
        if (matrix != 0) {
            GMathVec3d transformed;
            GMathMatrix4x4d::multiply(transformed, world, *matrix);
            world = transformed;
        }
        raw.add(world);
    }

    const OfAttr *order = object.get_attribute("points_order");
    if (order == 0 || order->get_long() == 0) {
        for (unsigned int i = 0; i < raw.get_count(); i++) points.add(raw[i]);
        return points.get_count() >= 2;
    }

    // Chainage glouton : on part du premier point et on prend chaque fois le
    // plus proche de ceux qui restent.
    CoreArray<bool> used(raw.get_count());
    for (unsigned int i = 0; i < raw.get_count(); i++) used[i] = false;

    unsigned int current = 0u;
    used[0] = true;
    points.add(raw[0]);

    for (unsigned int step = 1; step < raw.get_count(); step++) {
        unsigned int best = 0u;
        double best_distance = -1.0;
        for (unsigned int i = 0; i < raw.get_count(); i++) {
            if (used[i]) continue;
            const GMathVec3d delta = vsub(raw[i], raw[current]);
            const double squared = vdot(delta, delta);
            if (best_distance < 0.0 || squared < best_distance) {
                best_distance = squared;
                best = i;
            }
        }
        if (best_distance < 0.0) break;
        used[best] = true;
        points.add(raw[best]);
        current = best;
    }
    return points.get_count() >= 2;
}

// Les positions monde des points de controle, dans l'ordre.
inline bool
gather_control_points(OfObject& object, const char *attr_name,
                      CoreVector<GMathVec3d>& points)
{
    if (gather_from_geometry(object, points)) return true;

    OfAttr *attr = (attr_name != 0 && attr_name[0] != '\0')
                 ? object.get_attribute(attr_name) : 0;
    if (attr == 0) attr = get_points_attr(object);
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
                  const CoreVector<double>& radii, const double& fallback,
                  unsigned int arc_steps)
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

            // Le rayon propre a ce coin, s'il en a un.
            const unsigned int corner_index = i % n;
            const double radius = (corner_index < radii.get_count())
                                ? radii[corner_index] : fallback;

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

    // Variante ou chaque portee pend comme un cable reel. Un cable tendu entre
    // deux points ne suit jamais la corde : il decrit une chainette, et c'est
    // cette difference-la qui fait lire « cable » plutot que « tige ».
    //
    // On la resout exactement plutot que de la simuler. Les outils du marche
    // font tourner un solveur physique sur quelques centaines d'images, sans
    // collision, et il faut le relancer a chaque deplacement ; ici c'est une
    // equation scalaire, quatre iterations de Newton, reevaluee par le graphe
    // comme n'importe quel autre attribut.
    //
    // `slack` est le mou : 0,05 veut dire cinq pour cent de longueur de cable
    // en plus que la distance entre les deux points d'accroche. C'est un
    // rapport, donc le reglage survit au deplacement d'un locator -- ce qui ne
    // serait pas le cas d'une longueur absolue, ni du parametre `a` de la
    // chainette, dont la valeur ne veut rien dire hors de sa portee.
    bool
    build_catenary(const CoreVector<GMathVec3d>& cv, const bool& closed,
                   const double& slack, const GMathVec3d& gravity,
                   unsigned int steps)
    {
        m_closed = closed;
        if (steps < 2u) steps = 2u;

        const unsigned int n = cv.get_count();
        if (n < 2u) return false;

        const GMathVec3d up = vnorm(vscale(gravity, -1.0));
        const unsigned int span_count = closed ? n : n - 1u;

        CoreVector<GMathVec3d> pts;
        for (unsigned int s = 0; s < span_count; s++) {
            const GMathVec3d& a = cv[s];
            const GMathVec3d& b = cv[(s + 1u) % n];
            sample_span(a, b, up, slack, steps, pts, s == 0u);
        }
        if (!closed) pts.add(cv[n - 1u]);

        const unsigned int count = pts.get_count();
        if (count < 2u) return false;

        m_position.resize(count);
        for (unsigned int i = 0; i < count; i++) m_position[i] = pts[i];

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

    // Deplace la courbe par un bruit fractal, sans changer sa longueur ni la
    // decrocher de ses extremites. A appeler apres build : le repere et les
    // longueurs sont recalcules ensuite.
    void
    apply_noise(const double& amplitude, const double& frequency,
                unsigned int octaves, const double& roughness,
                const unsigned int& seed, const double& fade_length,
                const double& gravity_bias, const GMathVec3d& gravity)
    {
        const unsigned int count = m_position.get_count();
        if (count < 3u || amplitude <= 1e-9) return;
        if (octaves < 1u) octaves = 1u;
        if (octaves > 8u) octaves = 8u;

        const GMathVec3d down = vnorm(gravity);
        CoreArray<GMathVec3d> moved(count);

        for (unsigned int i = 0; i < count; i++) {
            const GMathVec3d& p = m_position[i];

            // Trois evaluations decalees donnent un vecteur, la ou une seule ne
            // donnerait qu'un scalaire. Les decalages sont arbitraires mais
            // fixes : le bruit doit etre le meme d'une evaluation a l'autre.
            GMathVec3d offset(
                fbm(p, frequency, octaves, roughness, seed),
                fbm(vadd(p, GMathVec3d(31.4, 17.7, 5.3)), frequency, octaves,
                    roughness, seed + 104729u),
                fbm(vadd(p, GMathVec3d(-9.1, 44.2, 23.8)), frequency, octaves,
                    roughness, seed + 224737u));

            // Retirer la composante tangentielle : le cable ondule, il ne
            // s'allonge pas.
            const GMathVec3d& t = m_tangent[i];
            offset = vsub(offset, vscale(t, vdot(offset, t)));

            // Biais de gravite : un cable qui pend s'affaisse plus qu'il ne se
            // souleve. On tire la moitie basse du bruit vers le bas.
            if (gravity_bias > 0.0) {
                const GMathVec3d flat = vsub(down, vscale(t, vdot(down, t)));
                offset = vadd(vscale(offset, 1.0 - gravity_bias),
                              vscale(flat, gravity_bias * fabs(vdot(offset, down))));
            }

            // Fondu aux extremites. Sur une courbe fermee il n'y a pas de bout,
            // donc pas de fondu.
            double weight = 1.0;
            if (!m_closed && fade_length > 1e-9) {
                const double from_start = m_arc[i];
                const double from_end = m_length - m_arc[i];
                const double nearest = (from_start < from_end) ? from_start : from_end;
                if (nearest < fade_length) {
                    const double s = nearest / fade_length;
                    weight = s * s * (3.0 - 2.0 * s);
                }
            }

            moved[i] = vadd(p, vscale(offset, amplitude * weight));
        }

        for (unsigned int i = 0; i < count; i++) m_position[i] = moved[i];

        for (unsigned int i = 0; i < count; i++) {
            const unsigned int prev = (i == 0u) ? (m_closed ? count - 1u : 0u) : i - 1u;
            const unsigned int next = (i + 1u >= count)
                                    ? (m_closed ? 0u : count - 1u) : i + 1u;
            m_tangent[i] = vnorm(vsub(m_position[next], m_position[prev]));
        }
        finalize();
    }

    // Enroule la courbe autour d'elle-meme : ce qui etait l'axe devient le
    // support, et la courbe s'en ecarte pour tourner autour.
    //
    // C'est ce qui permet de poser un cable par-dessus un faisceau existant en
    // lui donnant les memes points de controle : il suit le meme trajet, mais
    // en helice autour de lui.
    //
    // Deux choses le rendent organique plutot que mecanique. Le rayon
    // d'enroulement varie par un bruit tres basse frequence, donc le cable
    // colle au faisceau par endroits et s'en ecarte a d'autres. Et la ou il
    // s'ecarte, il tombe -- l'affaissement est proportionnel a l'ecart, ce qui
    // suffit a lire « il pend » plutot que « il fait un ventre ».
    void
    apply_wrap(const double& radius, const double& turns,
               const double& variation, const double& variation_frequency,
               const double& sag, const unsigned int& seed,
               const GMathVec3d& gravity)
    {
        const unsigned int count = m_position.get_count();
        if (count < 3u || radius <= 1e-9) return;

        const GMathVec3d down = vnorm(gravity);
        CoreArray<GMathVec3d> moved(count);

        for (unsigned int i = 0; i < count; i++) {
            const double s = m_arc[i];

            // Un seul bruit sert aux deux effets : c'est voulu. La ou le cable
            // s'ecarte, il doit aussi tomber ; deux bruits independants
            // donneraient des ventres et des creux sans rapport entre eux.
            const double wave = perlin(s * variation_frequency, 0.0, 0.0, seed);
            const double factor = 1.0 + variation * wave;
            const double r = radius * ((factor > 0.0) ? factor : 0.0);

            const double angle = 2.0 * M_PI * turns * s;
            const GMathVec3d& n = m_normal[i];
            const GMathVec3d b = vnorm(vcross(m_tangent[i], n));

            GMathVec3d p = vadd(m_position[i],
                                vadd(vscale(n, cos(angle) * r),
                                     vscale(b, sin(angle) * r)));

            if (sag > 1e-9 && wave > 0.0) {
                p = vadd(p, vscale(down, sag * wave));
            }
            moved[i] = p;
        }

        for (unsigned int i = 0; i < count; i++) m_position[i] = moved[i];

        for (unsigned int i = 0; i < count; i++) {
            const unsigned int prev = (i == 0u) ? (m_closed ? count - 1u : 0u) : i - 1u;
            const unsigned int next = (i + 1u >= count)
                                    ? (m_closed ? 0u : count - 1u) : i + 1u;
            m_tangent[i] = vnorm(vsub(m_position[next], m_position[prev]));
        }
        finalize();
    }

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

        if (m_closed) {
            // Sur une boucle, une distance qui deborde revient au debut plutot
            // que de se coincer au bout.
            distance = fmod(distance, m_length);
            if (distance < 0.0) distance += m_length;
        } else {
            if (distance <= 0.0) distance = 0.0;
            if (distance >= m_length) distance = m_length;
        }

        // Le segment de couture d'une courbe fermee n'est pas dans la table :
        // il va du dernier echantillon au premier, et se traite a part.
        if (m_closed && distance > m_arc[count - 1u]) {
            const double span = m_length - m_arc[count - 1u];
            const double t = (span > 1e-12) ? (distance - m_arc[count - 1u]) / span : 0.0;
            position = vlerp(m_position[count - 1u], m_position[0], t);
            tangent = vnorm(vlerp(m_tangent[count - 1u], m_tangent[0], t));
            normal = vnorm(vlerp(m_normal[count - 1u], m_normal[0], t));
            normal = vnorm(vcross(vcross(tangent, normal), tangent));
            return;
        }

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
    // Le parametre `a` d'une chainette qui relie deux appuis distants de `span`
    // a l'horizontale et de `rise` a la verticale, avec `length` de cable.
    //
    // L'equation des deux appuis a hauteurs differentes se ramene a celle du
    // cas de niveau en remplacant la longueur par sqrt(S^2 - h^2) : un seul
    // solveur couvre les deux. En posant u = span/(2a), elle devient
    // sinh(u)/u = r, dont la fonction est strictement croissante -- donc une
    // racine et une seule. C'est cette forme qu'il faut resoudre : ecrite
    // directement en `a`, elle a une racine parasite en zero ou Newton se perd.
    static double
    solve_catenary(const double& span, const double& rise, const double& length)
    {
        const double effective = length * length - rise * rise;
        if (effective <= 0.0 || span <= 1e-9) return 0.0;

        const double r = sqrt(effective) / span;
        if (r <= 1.0 + 1e-9) return 0.0;   // cable tendu : pas de mou a repartir

        // Le developpement sinh(u)/u = 1 + u^2/6 + ... donne une amorce tres
        // bonne pour les mous usuels, de l'ordre de quelques pour cent.
        double u = sqrt(6.0 * (r - 1.0));
        if (u < 1e-6) u = 1e-6;

        for (unsigned int i = 0; i < 40u; i++) {
            const double sh = sinh(u);
            const double ch = cosh(u);
            const double f = sh / u - r;
            if (f > -1e-12 && f < 1e-12) break;
            const double df = (u * ch - sh) / (u * u);
            if (df < 1e-12) break;
            const double next = u - f / df;
            u = (next > 1e-9) ? next : u * 0.5;
        }
        return span / (2.0 * u);
    }

    // Les echantillons d'une portee qui pend. `first` dit s'il faut poser aussi
    // le point de depart : sur les portees suivantes il est deja la, pose par
    // la precedente.
    static void
    sample_span(const GMathVec3d& a, const GMathVec3d& b, const GMathVec3d& up,
                const double& slack, const unsigned int& steps,
                CoreVector<GMathVec3d>& out, const bool& first)
    {
        const GMathVec3d delta = vsub(b, a);
        const double rise = vdot(delta, up);
        const GMathVec3d horizontal = vsub(delta, vscale(up, rise));
        const double span = horizontal.get_length();
        const double chord = delta.get_length();

        const double length = chord * (1.0 + ((slack > 0.0) ? slack : 0.0));
        const double param = (span > 1e-9) ? solve_catenary(span, rise, length) : 0.0;

        if (param <= 0.0) {
            // Portee tendue, ou deux appuis a la verticale l'un de l'autre : il
            // n'existe alors aucun plan de chainette, on relie droit.
            if (first) out.add(a);
            for (unsigned int i = 1; i < steps; i++) {
                out.add(vlerp(a, b, double(i) / double(steps)));
            }
            return;
        }

        const GMathVec3d e_x = vscale(horizontal, 1.0 / span);

        // Le point bas n'est pas au milieu de la portee : il se decale vers
        // l'appui le plus bas, d'autant plus que le denivele est grand.
        const double m = atanh(rise / length);
        const double x1 = param * m - span * 0.5;
        const double y1 = param * cosh(x1 / param);

        if (first) out.add(a);
        for (unsigned int i = 1; i < steps; i++) {
            const double x = x1 + span * double(i) / double(steps);
            const double y = param * cosh(x / param);
            out.add(vadd(a, vadd(vscale(e_x, x - x1), vscale(up, y - y1))));
        }
    }

    // Le repere et les longueurs cumulees, une fois les positions et les
    // tangentes en place. Commun aux deux facons de construire la courbe.
    void
    finalize()
    {
        const unsigned int count = m_position.get_count();
        m_normal.resize(count);
        m_arc.resize(count);
        if (count == 0u) { m_length = 0.0; return; }

        // Amorce du repere. N'importe quelle direction non colineaire a la
        // tangente conviendrait au transport, mais pas a l'utilisateur : une
        // amorce arbitraire fait sortir la section tournee d'un quart de tour
        // sur une courbe droite, ce qui ne se voit pas sur un tube -- il est de
        // revolution -- mais saute aux yeux sur un profil non circulaire ou une
        // geometrie deformee. On projette donc le Y du monde sur le plan
        // perpendiculaire a la tangente, et on ne bascule sur Z que si la
        // courbe part justement a la verticale.
        GMathVec3d up(0.0, 1.0, 0.0);
        if (fabs(vdot(m_tangent[0], up)) > 0.999) up = GMathVec3d(0.0, 0.0, 1.0);
        m_normal[0] = vnorm(vsub(up, vscale(m_tangent[0], vdot(m_tangent[0], up))));

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

        // Une courbe fermee ne repete pas son premier point : le segment de
        // couture, du dernier echantillon au premier, existe bel et bien mais
        // ne figure dans aucun intervalle. Sans cette ligne, get_length() le
        // perd et tout ce qui se repartit par longueur -- points distribues,
        // module repete, deformation -- se tasse d'autant.
        if (m_closed && count >= 2u) {
            m_length += vsub(m_position[0], m_position[count - 1u]).get_length();
        }
    }

    CoreArray<GMathVec3d> m_position;
    CoreArray<GMathVec3d> m_tangent;
    CoreArray<GMathVec3d> m_normal;
    CoreArray<double> m_arc;
    double m_length;
    bool m_closed;
};

// Applique le bruit si le node porte les attributs correspondants. Passer par
// ici plutot que par chaque node evite qu'un tube et un nuage de points poses
// sur les memes locators ondulent differemment.
inline void
apply_noise_from_object(OfObject& object, Path& path)
{
    const OfAttr *amplitude = object.get_attribute("noise_amplitude");
    if (amplitude == 0 || amplitude->get_double() <= 1e-9) return;

    const OfAttr *frequency = object.get_attribute("noise_frequency");
    const OfAttr *octaves = object.get_attribute("noise_octaves");
    const OfAttr *roughness = object.get_attribute("noise_roughness");
    const OfAttr *seed = object.get_attribute("noise_seed");
    const OfAttr *fade = object.get_attribute("noise_fade");
    const OfAttr *bias = object.get_attribute("noise_gravity_bias");
    const OfAttr *gravity_attr = object.get_attribute("gravity");

    GMathVec3d gravity(0.0, -1.0, 0.0);
    if (gravity_attr != 0) {
        gravity = GMathVec3d(gravity_attr->get_double(0),
                             gravity_attr->get_double(1),
                             gravity_attr->get_double(2));
    }

    path.apply_noise(amplitude->get_double(),
                     (frequency != 0) ? frequency->get_double() : 0.5,
                     (octaves != 0) ? (unsigned int) octaves->get_long() : 2u,
                     (roughness != 0) ? roughness->get_double() : 0.5,
                     (seed != 0) ? (unsigned int) seed->get_long() : 0u,
                     (fade != 0) ? fade->get_double() : 0.0,
                     (bias != 0) ? bias->get_double() : 0.0,
                     gravity);
}

// Enroule la courbe si le node porte les attributs correspondants. Vient apres
// le bruit : l'enroulement doit suivre la courbe deja irreguliere, pas une
// courbe lisse qu'on froisserait ensuite.
inline void
apply_wrap_from_object(OfObject& object, Path& path)
{
    const OfAttr *radius = object.get_attribute("wrap_radius");
    if (radius == 0 || radius->get_double() <= 1e-9) return;

    const OfAttr *turns = object.get_attribute("wrap_turns");
    const OfAttr *variation = object.get_attribute("wrap_variation");
    const OfAttr *frequency = object.get_attribute("wrap_variation_frequency");
    const OfAttr *sag = object.get_attribute("wrap_sag");
    const OfAttr *seed = object.get_attribute("wrap_seed");
    const OfAttr *gravity_attr = object.get_attribute("gravity");

    GMathVec3d gravity(0.0, -1.0, 0.0);
    if (gravity_attr != 0) {
        gravity = GMathVec3d(gravity_attr->get_double(0),
                             gravity_attr->get_double(1),
                             gravity_attr->get_double(2));
    }

    path.apply_wrap(radius->get_double(),
                    (turns != 0) ? turns->get_double() : 0.3,
                    (variation != 0) ? variation->get_double() : 0.0,
                    (frequency != 0) ? frequency->get_double() : 0.12,
                    (sag != 0) ? sag->get_double() : 0.0,
                    (seed != 0) ? (unsigned int) seed->get_long() : 0u,
                    gravity);
}

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
    if (!gather_control_points(object, 0, cv)) return false;

    const OfAttr *closed_attr = object.get_attribute("closed");
    const OfAttr *steps_attr = object.get_attribute("steps");
    const bool closed = (closed_attr != 0) ? closed_attr->get_bool() : false;
    unsigned int steps = (steps_attr != 0)
                       ? (unsigned int) steps_attr->get_long() : 8u;

    const OfAttr *interpolation = object.get_attribute("interpolation");
    const long mode = (interpolation != 0) ? interpolation->get_long() : 0;

    if (mode == 2) {
        const OfAttr *slack_attr = object.get_attribute("slack");
        const OfAttr *gravity_attr = object.get_attribute("gravity");
        GMathVec3d gravity(0.0, -1.0, 0.0);
        if (gravity_attr != 0) {
            gravity = GMathVec3d(gravity_attr->get_double(0),
                                 gravity_attr->get_double(1),
                                 gravity_attr->get_double(2));
        }
        if (!path.build_catenary(cv, closed,
                                 (slack_attr != 0) ? slack_attr->get_double() : 0.05,
                                 gravity, steps)) return false;
        apply_noise_from_object(object, path);
        apply_wrap_from_object(object, path);
        return true;
    }

    if (mode == 1) {
        const OfAttr *bend = object.get_attribute("bend_radius");
        const double fallback = (bend != 0) ? bend->get_double() : 0.0;

        // Le rayon peut etre repris point par point, quand le node porte la
        // colonne. Une valeur negative veut dire « comme le reglage global » --
        // c'est ce qui permet de ne toucher qu'aux deux ou trois coins qui le
        // meritent sans avoir a remplir toute la colonne.
        CoreVector<double> radii;
        OfAttr *per_point = object.get_attribute("bend");
        if (per_point != 0) {
            const unsigned int count = per_point->get_value_count();
            for (unsigned int i = 0; i < count; i++) {
                const double value = per_point->get_double(i);
                radii.add((value < 0.0) ? fallback : value);
            }
        }
        if (!path.build_beveled(cv, closed, radii, fallback, steps)) return false;
        apply_noise_from_object(object, path);
        apply_wrap_from_object(object, path);
        return true;
    }

    if (!path.build(cv, closed, steps)) return false;
    apply_noise_from_object(object, path);
    apply_wrap_from_object(object, path);
    return true;
}

} // namespace curve_core

#endif
