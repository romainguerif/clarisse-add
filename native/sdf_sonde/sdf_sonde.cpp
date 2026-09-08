// Sonde : Clarisse accepte-t-il un champ de distance resolu par SPHERE TRACING ?
//
// La sonde csg_sonde (native/csg_sonde/) a deja prouve qu'un GeometryObject
// tiers sans sommets se rend. Mais elle resolvait des equations du second
// degre : cout d'un test rayon/sphere, borne, exact. Des qu'on veut le MELANGE
// DOUX -- la signature visuelle de MagicaCSG -- il n'y a plus de solution
// analytique et il faut MARCHER le long du rayon.
//
// Cette sonde-la repond a trois choses que la premiere ne pouvait pas :
//
//   1. la marche tient-elle dans le contrat de intersect_primitive ? (rien
//      n'interdit d'y boucler, mais rien ne le promet non plus)
//   2. combien d'iterations par rayon le moteur paie-t-il REELLEMENT sur une
//      scene rendue -- pas sur un banc synthetique, mais avec les rayons
//      secondaires, les ombres et l'antialiasing de Clarisse ;
//   3. le decoupage en une primitive Clarisse par blob fait-il travailler le
//      BVH du moteur comme hierarchie de bornes du champ.
//
// Elle compte tout et ecrit ses statistiques dans un fichier au dechargement
// de la DLL, parce que la sortie standard de cnode est noyee sous les
// avertissements OCIO.
//
// Le point delicat, et il vaut d'etre note : compute_fragment_sample ne recoit
// que (u, v, w) et un sub_primitive_id. Pour un sphere tracer il n'y a pas de
// parametrisation naturelle -- alors on se sert de (u, v, w) comme d'un
// conteneur : la position du point touche, normalisee dans la boite englobante.
// Les trois canaux sont des doubles et le moteur les refuse hors de [0, 1] ;
// normalisee, elle passe. La normale est ensuite reconstruite par differences
// centrees du champ, soit six evaluations de plus par fragment ombre.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_attr.h>
#include <of_class.h>

#include <module_geometry.h>
#include <module_scene_item.h>

#include <geometry_object.h>
#include <geometry_raytrace_ctx.h>
#include <geometry_fragment.h>
#include <geometry_sample.h>

#include <core_array.h>
#include <core_string.h>
#include <gmath_vec3.h>
#include <gmath_ray.h>

#include <cstdio>
#include <cmath>
#include <vector>
#include <atomic>

#include <sdf_sonde.cma>

// Meme piege que dans csg_sonde : le corps du constructeur vit dans un .icc
// perdu, et opacity doit valoir 1. Voir docs/csg-clarisse.md 2.3.
inline GeometryMediumDescriptor::GeometryMediumDescriptor() { clear(); }

inline void
GeometryMediumDescriptor::clear()
{
    opacity = GMathVec3f(1.0f);
    thickness = 0.0f;
    density = 0.0f;
    density_diff = GMathVec3f(0.0f);
}

namespace {

// ---------------------------------------------------------------- compteurs

// Pas d'atomiques : sur 24 threads elles fausseraient la mesure qu'on cherche a
// faire. On accumule par thread et on somme a la fin. La perte de quelques
// comptes sur une course n'a aucune importance a cette echelle.
struct Counters {
    long long rays;
    long long iterations;
    long long prim_evals;
    long long capped;
    long long hits;
    long long shaded;
    // Taille des paquets que le moteur passe a intersect_primitive. C'est LE
    // chiffre qui decide si un noyau GPU lance depuis la peut etre rentable :
    // un aller-retour CUDA coute ~20 us, un rayon CPU ~0,04 us.
    long long packets;
    long long hist[7];           // 1 | 2-4 | 5-8 | 9-16 | 17-32 | 33-64 | 65+
    long long packet_max;
};

__declspec(thread) Counters t_counters = {0,0,0,0,0,0,0,{0,0,0,0,0,0,0},0};

// La somme des comptes de tous les threads. Les atomiques ne sont touchees
// qu'au versement -- une fois tous les 65 536 rayons par thread -- jamais dans
// la boucle chaude : la mesure ne doit pas se mesurer elle-meme.
struct Totals {
    std::atomic<long long> rays, iterations, prim_evals, capped, hits, shaded;
    std::atomic<long long> packets, packet_max;
    std::atomic<long long> hist[7];
    Totals() : rays(0), iterations(0), prim_evals(0),
               capped(0), hits(0), shaded(0), packets(0), packet_max(0) {
        for (int i = 0; i < 7; i++) hist[i] = 0;
    }
};
Totals g_total;

void lock_init() {}

void
flush_counters()
{
    g_total.rays       += t_counters.rays;
    g_total.iterations += t_counters.iterations;
    g_total.prim_evals += t_counters.prim_evals;
    g_total.capped     += t_counters.capped;
    g_total.hits       += t_counters.hits;
    g_total.shaded     += t_counters.shaded;
    g_total.packets    += t_counters.packets;
    for (int i = 0; i < 7; i++) g_total.hist[i] += t_counters.hist[i];
    if (t_counters.packet_max > g_total.packet_max.load())
        g_total.packet_max = t_counters.packet_max;
    Counters z = {0,0,0,0,0,0,0,{0,0,0,0,0,0,0},0};
    t_counters = z;
}

struct Reporter {
    ~Reporter()
    {
        flush_counters();
        const long long rays = g_total.rays.load();
        if (rays == 0) return;
        FILE *f = fopen("J:\\_WINDOWSTEMP\\claude\\sdf_sonde\\stats.txt", "a");
        if (!f) return;
        fprintf(f, "rayons=%lld iterations=%lld it_par_rayon=%.2f "
                   "eval_prim=%lld eval_par_rayon=%.1f touches=%lld "
                   "plafonnes=%lld (%.4f%%) fragments_ombres=%lld\n",
                rays, g_total.iterations.load(),
                (double)g_total.iterations.load() / (double)rays,
                g_total.prim_evals.load(),
                (double)g_total.prim_evals.load() / (double)rays,
                g_total.hits.load(), g_total.capped.load(),
                100.0 * g_total.capped.load() / (double)rays,
                g_total.shaded.load());
        fprintf(f, "  paquets=%lld rayons_par_paquet=%.2f max=%lld"
                   "  histogramme 1:%lld 2-4:%lld 5-8:%lld 9-16:%lld"
                   " 17-32:%lld 33-64:%lld 65+:%lld\n",
                g_total.packets.load(),
                g_total.packets.load()
                    ? (double)rays / (double)g_total.packets.load() : 0.0,
                g_total.packet_max.load(),
                g_total.hist[0].load(), g_total.hist[1].load(),
                g_total.hist[2].load(), g_total.hist[3].load(),
                g_total.hist[4].load(), g_total.hist[5].load(),
                g_total.hist[6].load());
        fclose(f);
    }
};
Reporter g_reporter;

// ------------------------------------------------------------------ le champ

struct Blob {
    GMathVec3d center;
    double radius;
    double bound;
    int type;                    // 0 sphere, 1 boite, 2 tore
    double p1, p2;
};

inline double
smin_poly(const double& a, const double& b, const double& k)
{
    double h = k - fabs(a - b);
    if (h < 0.0) h = 0.0;
    h /= k;
    const double m = a < b ? a : b;
    return m - h * h * k * 0.25;
}

inline double
blob_dist(const Blob& p, const GMathVec3d& q)
{
    const double dx = q[0] - p.center[0];
    const double dy = q[1] - p.center[1];
    const double dz = q[2] - p.center[2];
    if (p.type == 0) {
        return sqrt(dx*dx + dy*dy + dz*dz) - p.radius;
    } else if (p.type == 1) {
        const double qx = fabs(dx) - p.radius * 0.8;
        const double qy = fabs(dy) - p.radius * 0.6;
        const double qz = fabs(dz) - p.radius * 0.9;
        const double mx = qx > 0.0 ? qx : 0.0;
        const double my = qy > 0.0 ? qy : 0.0;
        const double mz = qz > 0.0 ? qz : 0.0;
        double inner = qx > qy ? qx : qy;
        if (qz > inner) inner = qz;
        if (inner > 0.0) inner = 0.0;
        return sqrt(mx*mx + my*my + mz*mz) + inner;
    }
    const double qx = sqrt(dx*dx + dz*dz) - p.p1;
    return sqrt(qx*qx + dy*dy) - p.p2;
}

class SdfObject : public GeometryObject {
public:
    SdfObject(const long& blob_count, const double& blend, const long& mode,
              const bool& split, const double& step_scale,
              const long& max_it, const double& eps)
        : m_blend(blend), m_mode(mode), m_split(split),
          m_step(step_scale), m_maxit(max_it), m_eps(eps),
          m_count(blob_count)
    {
        lock_init();
        build();
        m_shading_groups.resize(1);
        m_shading_groups[0] = CoreString("sdf");
    }

    GeometryObject *get_copy() const override
    {
        return new SdfObject(m_count, m_blend, m_mode, m_split,
                             m_step, m_maxit, m_eps);
    }

    GMathBbox3d get_bbox() const override { return m_bbox; }

    const CoreBasicArray<CoreString>& get_shading_group_names() const override
    { return m_shading_groups; }

    unsigned int get_primitive_count() const override
    { return m_split ? (unsigned int)m_blobs.size() : 1u; }

    unsigned int get_primitive_edge_count(const unsigned int&) const override
    { return 4; }

    void compute_primitive_bbox(const CtxEval&, const unsigned int& id,
                                GMathBbox3d& bbox) const override
    {
        if (!m_split) { bbox = m_bbox; return; }
        // La boite d'un blob est elargie du rayon de melange : c'est
        // exactement le cout cache du melange doux, celui que Barbier et al.
        // decrivent -- il detruit la localite spatiale.
        const Blob& b = m_blobs[id];
        const double r = b.bound + m_blend;
        bbox = GMathBbox3d(b.center - GMathVec3d(r), b.center + GMathVec3d(r));
    }

    unsigned int get_primitive_shading_group_index(const unsigned int&) const override
    { return 0; }

    void intersect_primitive(const CtxEval& eval_ctx, const unsigned int& id,
                             GeometryRaytraceCtx& raytrace_ctx) const override
    {
        const int first = raytrace_ctx.get_first_index();
        const int last = raytrace_ctx.get_last_index();

        {
            const long long n = (long long)(last - first + 1);
            t_counters.packets++;
            if (n > t_counters.packet_max) t_counters.packet_max = n;
            int b = 0;
            if (n > 64) b = 6; else if (n > 32) b = 5;
            else if (n > 16) b = 4; else if (n > 8) b = 3;
            else if (n > 4) b = 2; else if (n > 1) b = 1;
            t_counters.hist[b]++;
        }

        // La liste de blobs pertinents pour cette primitive. En mode decoupe
        // elle est precalculee et courte : c'est le BVH du moteur qui joue le
        // role de hierarchie de bornes du champ.
        const int *list;
        int list_count;
        if (m_split) {
            list = &m_neighbors[m_neighbor_start[id]];
            list_count = m_neighbor_start[id+1] - m_neighbor_start[id];
        } else {
            list = m_all.empty() ? 0 : &m_all[0];
            list_count = (int)m_all.size();
        }

        GMathBbox3d box;
        if (m_split) {
            const Blob& b = m_blobs[id];
            const double r = b.bound + m_blend;
            box = GMathBbox3d(b.center - GMathVec3d(r), b.center + GMathVec3d(r));
        } else {
            box = m_bbox;
        }

        for (int i = first; i <= last; i++) {
            const unsigned int index = (unsigned int)i;
            const GMathBasicRay<>& ray = raytrace_ctx.get_local_ray(index);
            const GMathVec3d& o = ray.get_position();
            const GMathVec3d& d = ray.get_direction();

            double t0, t1;
            if (!slab(box, o, d, t0, t1)) continue;
            if (t0 < 0.0) t0 = 0.0;
            if (t1 <= t0) continue;

            t_counters.rays++;

            if (m_mode == 1) {
                // Voie de comparaison : union dure resolue analytiquement, la
                // meme chose que fait csg_sonde. Aucun pas de marche.
                double best = 1e300;
                bool found = false;
                for (int k = 0; k < list_count; k++) {
                    const Blob& b = m_blobs[list[k]];
                    double a0, a1;
                    t_counters.prim_evals++;
                    if (!ray_sphere(o, d, b.center, b.radius, a0, a1)) continue;
                    if (a0 < 0.0) a0 = a1;
                    if (a0 < 0.0) continue;
                    if (a0 < t0 || a0 > t1) continue;
                    if (a0 < best) { best = a0; found = true; }
                }
                t_counters.iterations++;
                if (found && !raytrace_ctx.is_distance_clipped(index, best)) {
                    push(eval_ctx, raytrace_ctx, index, id, o + d * best, best);
                    t_counters.hits++;
                }
                if ((t_counters.rays & 0xFFFF) == 0) flush_counters();
                continue;
            }

            // Sphere tracing.
            double t = t0;
            int it = 0;
            bool hit = false;
            for (; it < m_maxit; it++) {
                const GMathVec3d p = o + d * t;
                const double dist = field(p, list, list_count);
                if (dist < m_eps) { hit = true; break; }
                t += dist * m_step;
                if (t > t1) break;
            }
            t_counters.iterations += (it + 1);
            if (it >= m_maxit) t_counters.capped++;
            if (!hit) continue;
            if (raytrace_ctx.is_distance_clipped(index, t)) continue;
            push(eval_ctx, raytrace_ctx, index, id, o + d * t, t);
            t_counters.hits++;

            if ((t_counters.rays & 0xFFFF) == 0) flush_counters();
        }
    }

    void compute_fragment_sample(const CtxEval&, const GeometryFragment& fragment,
                                 GeometrySample& sample) const override
    {
        // (u, v, w) porte la position normalisee dans la boite. On la
        // denormalise, on prend le gradient par differences centrees, et on
        // fabrique un repere tangent dont le produit vectoriel rend la normale.
        const GMathVec3d uvw = fragment.get_uvw();
        const GMathVec3d lo = m_bbox.get_min();
        const GMathVec3d ext = m_bbox.get_max() - lo;
        const GMathVec3d p(lo[0] + uvw[0] * ext[0],
                           lo[1] + uvw[1] * ext[1],
                           lo[2] + uvw[2] * ext[2]);

        const int *list = m_all.empty() ? 0 : &m_all[0];
        const int n = (int)m_all.size();
        const double h = 1e-4;
        GMathVec3d g(
            field(p + GMathVec3d(h,0,0), list, n) - field(p - GMathVec3d(h,0,0), list, n),
            field(p + GMathVec3d(0,h,0), list, n) - field(p - GMathVec3d(0,h,0), list, n),
            field(p + GMathVec3d(0,0,h), list, n) - field(p - GMathVec3d(0,0,h), list, n));
        double len = g.get_length();
        if (len < 1e-12) g = GMathVec3d(0.0, 1.0, 0.0);
        else g /= len;

        t_counters.shaded++;

        // Repere orthonorme autour de la normale (Duff et al.), pour que
        // cross(dpdu, dpdv) rende exactement g.
        const double sg = g[2] >= 0.0 ? 1.0 : -1.0;
        const double a = -1.0 / (sg + g[2]);
        const double b = g[0] * g[1] * a;
        const GMathVec3d dpdu(1.0 + sg * g[0] * g[0] * a, sg * b, -sg * g[0]);
        const GMathVec3d dpdv(b, sg + g[1] * g[1] * a, -g[1]);

        sample.init_surface(p, dpdu, dpdv);
    }

private:
    static bool
    ray_sphere(const GMathVec3d& o, const GMathVec3d& d, const GMathVec3d& c,
               const double& r, double& t0, double& t1)
    {
        const GMathVec3d oc = o - c;
        const double a = d.dot(d);
        const double bb = 2.0 * oc.dot(d);
        const double cc = oc.dot(oc) - r * r;
        const double disc = bb * bb - 4.0 * a * cc;
        if (disc < 0.0 || a == 0.0) return false;
        const double root = sqrt(disc);
        t0 = (-bb - root) / (2.0 * a);
        t1 = (-bb + root) / (2.0 * a);
        return true;
    }

    static bool
    slab(const GMathBbox3d& b, const GMathVec3d& o, const GMathVec3d& d,
         double& tmin, double& tmax)
    {
        tmin = -1e300; tmax = 1e300;
        const GMathVec3d lo = b.get_min(), hi = b.get_max();
        for (int k = 0; k < 3; k++) {
            if (fabs(d[k]) < 1e-15) {
                if (o[k] < lo[k] || o[k] > hi[k]) return false;
            } else {
                double inv = 1.0 / d[k];
                double a = (lo[k] - o[k]) * inv;
                double c = (hi[k] - o[k]) * inv;
                if (a > c) { double s = a; a = c; c = s; }
                if (a > tmin) tmin = a;
                if (c < tmax) tmax = c;
                if (tmin > tmax) return false;
            }
        }
        return true;
    }

    double
    field(const GMathVec3d& p, const int *list, const int& count) const
    {
        t_counters.prim_evals += count;
        double d = 1e30;
        if (m_blend <= 0.0) {
            for (int i = 0; i < count; i++) {
                const double di = blob_dist(m_blobs[list[i]], p);
                if (di < d) d = di;
            }
        } else {
            for (int i = 0; i < count; i++) {
                const double di = blob_dist(m_blobs[list[i]], p);
                d = (i == 0) ? di : smin_poly(d, di, m_blend);
            }
        }
        return d;
    }

    void
    push(const CtxEval& eval_ctx, GeometryRaytraceCtx& ctx,
         const unsigned int& index, const unsigned int& id,
         const GMathVec3d& p, const double& t) const
    {
        const GMathVec3d lo = m_bbox.get_min();
        const GMathVec3d ext = m_bbox.get_max() - lo;
        double u = (p[0] - lo[0]) / ext[0];
        double v = (p[1] - lo[1]) / ext[1];
        double w = (p[2] - lo[2]) / ext[2];
        // Le moteur refuse (u, v, w) hors de [0, 1], sans un mot. Un point sur
        // la paroi de la boite y tombe a 1e-16 pres : on serre.
        if (u < 0.0) u = 0.0; else if (u > 1.0) u = 1.0;
        if (v < 0.0) v = 0.0; else if (v > 1.0) v = 1.0;
        if (w < 0.0) w = 0.0; else if (w > 1.0) w = 1.0;
        ctx.push_intersection(eval_ctx, index, id, u, v, w, t);
    }

    void
    build()
    {
        unsigned int seed = 12345u;
        struct R { unsigned int s;
                   double operator()() {
                       s = s * 1664525u + 1013904223u;
                       return (double)((s >> 8) & 0xFFFFFF) / (double)0x1000000; } };
        R rnd; rnd.s = seed;

        m_blobs.clear();
        for (long i = 0; i < m_count; i++) {
            Blob b;
            b.center = GMathVec3d((rnd()*2.0-1.0)*1.6,
                                  (rnd()*2.0-1.0)*1.6,
                                  (rnd()*2.0-1.0)*1.6);
            b.type = (int)(rnd() * 2.999);
            b.radius = 0.35 + rnd() * 0.45;
            if (b.type == 0)      b.bound = b.radius;
            else if (b.type == 1) b.bound = b.radius * sqrt(0.8*0.8 + 0.6*0.6 + 0.9*0.9);
            else { b.p1 = b.radius; b.p2 = b.radius * 0.32; b.bound = b.p1 + b.p2; }
            m_blobs.push_back(b);
        }

        GMathVec3d lo(1e30), hi(-1e30);
        for (size_t i = 0; i < m_blobs.size(); i++) {
            const double r = m_blobs[i].bound + m_blend;
            for (int k = 0; k < 3; k++) {
                if (m_blobs[i].center[k] - r < lo[k]) lo[k] = m_blobs[i].center[k] - r;
                if (m_blobs[i].center[k] + r > hi[k]) hi[k] = m_blobs[i].center[k] + r;
            }
        }
        m_bbox = GMathBbox3d(lo, hi);

        m_all.resize(m_blobs.size());
        for (size_t i = 0; i < m_blobs.size(); i++) m_all[i] = (int)i;

        // Voisinage : le blob j influence le champ pres du blob i seulement si
        // leurs surfaces sont a moins du rayon de melange l'une de l'autre. Le
        // test sur les spheres englobantes est conservateur.
        m_neighbor_start.clear();
        m_neighbors.clear();
        m_neighbor_start.push_back(0);
        for (size_t i = 0; i < m_blobs.size(); i++) {
            for (size_t j = 0; j < m_blobs.size(); j++) {
                const GMathVec3d dv = m_blobs[j].center - m_blobs[i].center;
                const double reach = m_blobs[i].bound + m_blobs[j].bound
                                   + 2.0 * m_blend;
                if (dv.get_length() <= reach) m_neighbors.push_back((int)j);
            }
            m_neighbor_start.push_back((int)m_neighbors.size());
        }
    }

    std::vector<Blob> m_blobs;
    std::vector<int>  m_all;
    std::vector<int>  m_neighbors;
    std::vector<int>  m_neighbor_start;
    GMathBbox3d m_bbox;
    double m_blend, m_step, m_eps;
    long   m_mode, m_maxit, m_count;
    bool   m_split;
    CoreArray<CoreString> m_shading_groups;
};

} // namespace

class SdfSondeModule : public ModuleGeometry {
public:
    SdfSondeModule() : ModuleGeometry() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModuleGeometry::module_constructor(object);

        static const char *const names[] = {
            "blob_count", "blend", "mode", "split_primitives",
            "step_scale", "max_iterations", "epsilon" };
        const unsigned int name_count = sizeof(names) / sizeof(names[0]);

        unsigned int present = 0;
        for (unsigned int i = 0; i < name_count; i++)
            if (object.get_attribute(names[i]) != 0) present++;
        if (present == 0) return;

        // CoreArray::resize ne preserve rien et OfAttrDirtiness ne s'initialise
        // pas : compter d'abord, allouer a la taille exacte. Voir sdk-clarisse.md.
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
            const OfObject *object = get_object();
            if (object != 0) {
                const OfAttr *a_count = object->get_attribute("blob_count");
                const OfAttr *a_blend = object->get_attribute("blend");
                const OfAttr *a_mode  = object->get_attribute("mode");
                const OfAttr *a_split = object->get_attribute("split_primitives");
                const OfAttr *a_step  = object->get_attribute("step_scale");
                const OfAttr *a_maxit = object->get_attribute("max_iterations");
                const OfAttr *a_eps   = object->get_attribute("epsilon");
                return new SdfObject(
                    a_count ? a_count->get_long()   : 64,
                    a_blend ? a_blend->get_double() : 0.25,
                    a_mode  ? a_mode->get_long()    : 0,
                    a_split ? a_split->get_bool()   : true,
                    a_step  ? a_step->get_double()  : 1.0,
                    a_maxit ? a_maxit->get_long()   : 128,
                    a_eps   ? a_eps->get_double()   : 1e-4);
            }
        }
        return ModuleGeometry::create_resource(id, data);
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometrySdfSonde, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometrySdfSonde)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometrySdfSonde);
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
    SdfSondeModule *module = new SdfSondeModule();
    module->set_object(object);      // sans ca, plantage : voir sdk-clarisse.md 3
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
