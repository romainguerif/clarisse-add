// Banc d'essai : combien coute un sphere tracer de champ de distance sur CPU ?
//
// Le document sdf-clarisse.md a besoin de chiffres mesures, pas d'ordres de
// grandeur lus dans des papiers. On mesure ici quatre choses :
//
//   1. le cout d'un rayon en sphere tracing, scalaire, un thread ;
//   2. le gain de la vectorisation AVX2 (8 rayons par paquet) ;
//   3. le gain du multithreading sur 24 threads ;
//   4. le gain de l'elagage par boite englobante par rayon.
//
// Et deux variantes du champ : melange dur (min) contre melange doux (smin de
// Quilez), parce que c'est precisement la frontiere qui interesse le document.
//
// Aucune dependance. Compile avec : cl /O2 /arch:AVX2 /EHsc /std:c++14

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <immintrin.h>

// ---------------------------------------------------------------- la scene

enum { PRIM_SPHERE = 0, PRIM_BOX = 1, PRIM_TORUS = 2 };

struct Prim {
    float cx, cy, cz;      // centre
    float p0, p1, p2;      // rayon / demi-cotes / (grand rayon, petit rayon)
    int   type;
    float bound;           // rayon de la sphere englobante autour du centre
};

static std::vector<Prim> g_prims;
static float g_blend = 0.0f;   // 0 = melange dur

// Generateur reproductible : on veut que toutes les variantes voient la meme
// scene, sinon on compare des choses differentes.
static unsigned int rng_state = 12345u;
static float frand()
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)((rng_state >> 8) & 0xFFFFFF) / (float)0x1000000;
}

static void build_scene(int n)
{
    rng_state = 12345u;
    g_prims.clear();
    g_prims.reserve(n);
    for (int i = 0; i < n; i++) {
        Prim p;
        p.cx = (frand() * 2.0f - 1.0f) * 1.6f;
        p.cy = (frand() * 2.0f - 1.0f) * 1.6f;
        p.cz = (frand() * 2.0f - 1.0f) * 1.6f;
        p.type = (int)(frand() * 2.999f);
        float s = 0.35f + frand() * 0.45f;
        if (p.type == PRIM_SPHERE) {
            p.p0 = s; p.p1 = 0.0f; p.p2 = 0.0f;
            p.bound = s;
        } else if (p.type == PRIM_BOX) {
            p.p0 = s * 0.8f; p.p1 = s * 0.6f; p.p2 = s * 0.9f;
            p.bound = sqrtf(p.p0*p.p0 + p.p1*p.p1 + p.p2*p.p2);
        } else {
            p.p0 = s;            // grand rayon
            p.p1 = s * 0.32f;    // petit rayon
            p.p2 = 0.0f;
            p.bound = p.p0 + p.p1;
        }
        g_prims.push_back(p);
    }
}

// ------------------------------------------------------------- champ scalaire

static inline float smin_poly(float a, float b, float k)
{
    // smin polynomial de Quilez. C'est la formule qui detruit la propriete de
    // distance : dans la zone de raccord le gradient n'est plus unitaire.
    float h = k - fabsf(a - b);
    h = h > 0.0f ? h : 0.0f;
    h /= k;
    float m = a < b ? a : b;
    return m - h * h * k * 0.25f;
}

static inline float prim_dist(const Prim& p, float x, float y, float z)
{
    float dx = x - p.cx, dy = y - p.cy, dz = z - p.cz;
    if (p.type == PRIM_SPHERE) {
        return sqrtf(dx*dx + dy*dy + dz*dz) - p.p0;
    } else if (p.type == PRIM_BOX) {
        float qx = fabsf(dx) - p.p0;
        float qy = fabsf(dy) - p.p1;
        float qz = fabsf(dz) - p.p2;
        float mx = qx > 0.0f ? qx : 0.0f;
        float my = qy > 0.0f ? qy : 0.0f;
        float mz = qz > 0.0f ? qz : 0.0f;
        float outside = sqrtf(mx*mx + my*my + mz*mz);
        float inside = std::min(std::max(qx, std::max(qy, qz)), 0.0f);
        return outside + inside;
    } else {
        float qx = sqrtf(dx*dx + dz*dz) - p.p0;
        return sqrtf(qx*qx + dy*dy) - p.p1;
    }
}

// Le champ complet, sans elagage : toutes les primitives a chaque evaluation.
static inline float field(float x, float y, float z, long long* evals)
{
    const int n = (int)g_prims.size();
    *evals += n;
    float d = 1e30f;
    if (g_blend <= 0.0f) {
        for (int i = 0; i < n; i++) {
            float di = prim_dist(g_prims[i], x, y, z);
            d = di < d ? di : d;
        }
    } else {
        for (int i = 0; i < n; i++) {
            float di = prim_dist(g_prims[i], x, y, z);
            d = (i == 0) ? di : smin_poly(d, di, g_blend);
        }
    }
    return d;
}

// Le champ restreint a une liste de primitives (elagage par rayon).
static inline float field_subset(float x, float y, float z,
                                 const int* idx, int count, long long* evals)
{
    *evals += count;
    float d = 1e30f;
    if (g_blend <= 0.0f) {
        for (int i = 0; i < count; i++) {
            float di = prim_dist(g_prims[idx[i]], x, y, z);
            d = di < d ? di : d;
        }
    } else {
        for (int i = 0; i < count; i++) {
            float di = prim_dist(g_prims[idx[i]], x, y, z);
            d = (i == 0) ? di : smin_poly(d, di, g_blend);
        }
    }
    return d;
}

// ---------------------------------------------------------- le sphere tracer

static const float EPS = 1e-4f;
static const float TMAX = 12.0f;
static int         MAXIT = 128;

struct RayStats {
    long long hits;
    long long iters;
    long long evals;
    double    checksum;
    long long capped;      // rayons ayant atteint le plafond d'iterations
};

// Quand il est non nul, chaque rayon y depose sa distance d'arrivee (ou -1).
// Sert a comparer deux reglages de pas et a mesurer le depassement.
static float* g_record = 0;

// Sur-relaxation a 1 : on reste conservateur. Le facteur de securite est le
// vrai reglage a exposer si le champ n'est pas une distance exacte.
static float g_step_scale = 1.0f;

static void trace_scalar(const float* ro, const float* rd, int count,
                         RayStats& st, bool cull)
{
    std::vector<int> idx(g_prims.size());
    for (int r = 0; r < count; r++) {
        const float ox = ro[0], oy = ro[1], oz = ro[2];
        const float dx = rd[r*3+0], dy = rd[r*3+1], dz = rd[r*3+2];

        int sub = 0;
        if (cull) {
            // Test rayon / sphere englobante : on ne garde que les primitives
            // que ce rayon peut toucher. C'est la hierarchie de bornes du
            // pauvre, faite une fois par rayon au lieu d'une fois par pas.
            for (int i = 0; i < (int)g_prims.size(); i++) {
                const Prim& p = g_prims[i];
                float mx = p.cx - ox, my = p.cy - oy, mz = p.cz - oz;
                float t = mx*dx + my*dy + mz*dz;
                float px = mx - t*dx, py = my - t*dy, pz = mz - t*dz;
                float d2 = px*px + py*py + pz*pz;
                float rr = p.bound + g_blend;   // le melange elargit la boite
                if (d2 <= rr*rr) idx[sub++] = i;
            }
        }

        float t = 0.0f;
        int it = 0;
        bool hit = false;
        for (; it < MAXIT; it++) {
            float x = ox + dx*t, y = oy + dy*t, z = oz + dz*t;
            float d = cull ? field_subset(x, y, z, idx.data(), sub, &st.evals)
                           : field(x, y, z, &st.evals);
            if (d < EPS) { hit = true; break; }
            t += d * g_step_scale;
            if (t > TMAX) break;
        }
        st.iters += (it + 1);
        if (it >= MAXIT) st.capped++;
        if (hit) { st.hits++; st.checksum += t; }
        if (g_record) g_record[r] = hit ? t : -1.0f;
    }
}

// ------------------------------------------------------------ champ AVX2 (x8)

static inline __m256 prim_dist8(const Prim& p, __m256 x, __m256 y, __m256 z)
{
    const __m256 cx = _mm256_set1_ps(p.cx);
    const __m256 cy = _mm256_set1_ps(p.cy);
    const __m256 cz = _mm256_set1_ps(p.cz);
    __m256 dx = _mm256_sub_ps(x, cx);
    __m256 dy = _mm256_sub_ps(y, cy);
    __m256 dz = _mm256_sub_ps(z, cz);

    if (p.type == PRIM_SPHERE) {
        __m256 s = _mm256_add_ps(_mm256_mul_ps(dx,dx),
                   _mm256_add_ps(_mm256_mul_ps(dy,dy), _mm256_mul_ps(dz,dz)));
        return _mm256_sub_ps(_mm256_sqrt_ps(s), _mm256_set1_ps(p.p0));
    } else if (p.type == PRIM_BOX) {
        const __m256 sign = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
        __m256 qx = _mm256_sub_ps(_mm256_and_ps(dx, sign), _mm256_set1_ps(p.p0));
        __m256 qy = _mm256_sub_ps(_mm256_and_ps(dy, sign), _mm256_set1_ps(p.p1));
        __m256 qz = _mm256_sub_ps(_mm256_and_ps(dz, sign), _mm256_set1_ps(p.p2));
        const __m256 zero = _mm256_setzero_ps();
        __m256 mx = _mm256_max_ps(qx, zero);
        __m256 my = _mm256_max_ps(qy, zero);
        __m256 mz = _mm256_max_ps(qz, zero);
        __m256 outside = _mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(mx,mx),
                          _mm256_add_ps(_mm256_mul_ps(my,my), _mm256_mul_ps(mz,mz))));
        __m256 inside = _mm256_min_ps(_mm256_max_ps(qx, _mm256_max_ps(qy, qz)), zero);
        return _mm256_add_ps(outside, inside);
    } else {
        __m256 s = _mm256_add_ps(_mm256_mul_ps(dx,dx), _mm256_mul_ps(dz,dz));
        __m256 qx = _mm256_sub_ps(_mm256_sqrt_ps(s), _mm256_set1_ps(p.p0));
        __m256 t = _mm256_add_ps(_mm256_mul_ps(qx,qx), _mm256_mul_ps(dy,dy));
        return _mm256_sub_ps(_mm256_sqrt_ps(t), _mm256_set1_ps(p.p1));
    }
}

static inline __m256 smin8(__m256 a, __m256 b, float k)
{
    const __m256 sign = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
    __m256 vk = _mm256_set1_ps(k);
    __m256 h = _mm256_sub_ps(vk, _mm256_and_ps(_mm256_sub_ps(a, b), sign));
    h = _mm256_max_ps(h, _mm256_setzero_ps());
    h = _mm256_div_ps(h, vk);
    __m256 m = _mm256_min_ps(a, b);
    return _mm256_sub_ps(m, _mm256_mul_ps(_mm256_mul_ps(h, h),
                                          _mm256_mul_ps(vk, _mm256_set1_ps(0.25f))));
}

static inline __m256 field8(__m256 x, __m256 y, __m256 z,
                            const int* idx, int count, long long* evals)
{
    *evals += (long long)count * 8;
    __m256 d = _mm256_set1_ps(1e30f);
    if (g_blend <= 0.0f) {
        for (int i = 0; i < count; i++)
            d = _mm256_min_ps(d, prim_dist8(g_prims[idx[i]], x, y, z));
    } else {
        for (int i = 0; i < count; i++) {
            __m256 di = prim_dist8(g_prims[idx[i]], x, y, z);
            d = (i == 0) ? di : smin8(d, di, g_blend);
        }
    }
    return d;
}

// Paquet de 8 rayons. On itere tant qu'au moins une voie est vivante : c'est la
// vraie limite de la vectorisation en sphere tracing. Une voie qui rase une
// silhouette et prend 128 pas force les sept autres a l'attendre.
static void trace_avx2(const float* ro, const float* rd, int count,
                       RayStats& st, bool cull)
{
    std::vector<int> all(g_prims.size());
    for (int i = 0; i < (int)g_prims.size(); i++) all[i] = i;
    std::vector<int> idx(g_prims.size());

    const __m256 vox = _mm256_set1_ps(ro[0]);
    const __m256 voy = _mm256_set1_ps(ro[1]);
    const __m256 voz = _mm256_set1_ps(ro[2]);

    for (int base = 0; base + 8 <= count; base += 8) {
        float dxs[8], dys[8], dzs[8];
        for (int l = 0; l < 8; l++) {
            dxs[l] = rd[(base+l)*3+0];
            dys[l] = rd[(base+l)*3+1];
            dzs[l] = rd[(base+l)*3+2];
        }
        __m256 dx = _mm256_loadu_ps(dxs);
        __m256 dy = _mm256_loadu_ps(dys);
        __m256 dz = _mm256_loadu_ps(dzs);

        const int* plist = all.data();
        int pcount = (int)g_prims.size();
        if (cull) {
            // Elagage a l'echelle du PAQUET : une primitive est gardee si au
            // moins une des huit voies peut la toucher. C'est plus laxiste que
            // l'elagage par rayon -- le prix de la vectorisation.
            int sub = 0;
            for (int i = 0; i < (int)g_prims.size(); i++) {
                const Prim& p = g_prims[i];
                __m256 mx = _mm256_sub_ps(_mm256_set1_ps(p.cx), vox);
                __m256 my = _mm256_sub_ps(_mm256_set1_ps(p.cy), voy);
                __m256 mz = _mm256_sub_ps(_mm256_set1_ps(p.cz), voz);
                __m256 t = _mm256_add_ps(_mm256_mul_ps(mx,dx),
                           _mm256_add_ps(_mm256_mul_ps(my,dy), _mm256_mul_ps(mz,dz)));
                __m256 px = _mm256_sub_ps(mx, _mm256_mul_ps(t,dx));
                __m256 py = _mm256_sub_ps(my, _mm256_mul_ps(t,dy));
                __m256 pz = _mm256_sub_ps(mz, _mm256_mul_ps(t,dz));
                __m256 d2 = _mm256_add_ps(_mm256_mul_ps(px,px),
                            _mm256_add_ps(_mm256_mul_ps(py,py), _mm256_mul_ps(pz,pz)));
                float rr = p.bound + g_blend;
                __m256 cmp = _mm256_cmp_ps(d2, _mm256_set1_ps(rr*rr), _CMP_LE_OQ);
                if (_mm256_movemask_ps(cmp)) idx[sub++] = i;
            }
            plist = idx.data();
            pcount = sub;
        }

        __m256 t = _mm256_setzero_ps();
        __m256 alive = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
        __m256 hitmask = _mm256_setzero_ps();
        int it = 0;
        for (; it < MAXIT; it++) {
            __m256 x = _mm256_add_ps(vox, _mm256_mul_ps(dx, t));
            __m256 y = _mm256_add_ps(voy, _mm256_mul_ps(dy, t));
            __m256 z = _mm256_add_ps(voz, _mm256_mul_ps(dz, t));
            __m256 d = field8(x, y, z, plist, pcount, &st.evals);

            __m256 hit = _mm256_and_ps(alive,
                          _mm256_cmp_ps(d, _mm256_set1_ps(EPS), _CMP_LT_OQ));
            hitmask = _mm256_or_ps(hitmask, hit);
            alive = _mm256_andnot_ps(hit, alive);

            // On n'avance que les voies vivantes.
            __m256 step = _mm256_and_ps(alive,
                           _mm256_mul_ps(d, _mm256_set1_ps(g_step_scale)));
            t = _mm256_add_ps(t, step);
            alive = _mm256_and_ps(alive,
                     _mm256_cmp_ps(t, _mm256_set1_ps(TMAX), _CMP_LE_OQ));
            if (_mm256_movemask_ps(alive) == 0) break;
        }
        st.iters += (long long)(it + 1) * 8;

        float ts[8]; _mm256_storeu_ps(ts, t);
        int hm = _mm256_movemask_ps(hitmask);
        for (int l = 0; l < 8; l++)
            if (hm & (1 << l)) { st.hits++; st.checksum += ts[l]; }
    }
}

// ----------------------------------------------------------------- le pilote

static double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static void make_rays(int w, int h, std::vector<float>& rd, float* ro)
{
    ro[0] = 0.0f; ro[1] = 0.0f; ro[2] = 5.0f;
    rd.resize((size_t)w * h * 3);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            float u = ((i + 0.5f) / w * 2.0f - 1.0f) * 1.1f;
            float v = ((j + 0.5f) / h * 2.0f - 1.0f) * 1.1f;
            float dx = u, dy = v, dz = -2.2f;
            float len = sqrtf(dx*dx + dy*dy + dz*dz);
            size_t o = ((size_t)j * w + i) * 3;
            rd[o+0] = dx/len; rd[o+1] = dy/len; rd[o+2] = dz/len;
        }
    }
}

struct Result {
    const char* label;
    double ms;
    long long rays, hits, iters, evals;
    double mrays;
};

static void report(const char* label, double ms, long long rays,
                   const RayStats& st)
{
    printf("%-42s %9.1f ms | %7.3f Mrayons/s | %6.1f it/rayon | "
           "%7.1f eval/rayon | %5.1f%% touches | somme %.3f\n",
           label, ms, (double)rays / (ms * 1000.0),
           (double)st.iters / (double)rays,
           (double)st.evals / (double)rays,
           100.0 * (double)st.hits / (double)rays,
           st.checksum);
}

int main(int argc, char** argv)
{
    int w = 512, h = 512;
    int nprim = 64;
    if (argc > 1) nprim = atoi(argv[1]);
    if (argc > 2) w = h = atoi(argv[2]);

    build_scene(nprim);
    std::vector<float> rd;
    float ro[3];
    make_rays(w, h, rd, ro);
    const long long rays = (long long)w * h;

    printf("\n=== Sphere tracing CPU ===\n");
    printf("scene : %d primitives | image %dx%d = %lld rayons primaires | "
           "eps %.0e | max %d iterations\n\n", nprim, w, h, rays, EPS, MAXIT);

    const int nthreads = (int)std::thread::hardware_concurrency();

    for (int mode = 0; mode < 2; mode++) {
        g_blend = (mode == 0) ? 0.0f : 0.25f;
        printf("--- melange %s (k = %.2f) ---\n",
               mode == 0 ? "DUR (min)" : "DOUX (smin de Quilez)", g_blend);

        // 1. scalaire, sans elagage
        {
            RayStats st = {0,0,0,0.0,0};
            double t0 = now_ms();
            trace_scalar(ro, rd.data(), (int)rays, st, false);
            report("scalaire, 1 thread, sans elagage", now_ms()-t0, rays, st);
        }
        // 2. scalaire, elagage par rayon
        {
            RayStats st = {0,0,0,0.0,0};
            double t0 = now_ms();
            trace_scalar(ro, rd.data(), (int)rays, st, true);
            report("scalaire, 1 thread, elagage/rayon", now_ms()-t0, rays, st);
        }
        // 3. AVX2, sans elagage
        {
            RayStats st = {0,0,0,0.0,0};
            double t0 = now_ms();
            trace_avx2(ro, rd.data(), (int)rays, st, false);
            report("AVX2 x8, 1 thread, sans elagage", now_ms()-t0, rays, st);
        }
        // 4. AVX2 + elagage par paquet
        {
            RayStats st = {0,0,0,0.0,0};
            double t0 = now_ms();
            trace_avx2(ro, rd.data(), (int)rays, st, true);
            report("AVX2 x8, 1 thread, elagage/paquet", now_ms()-t0, rays, st);
        }
        // 5. AVX2 + elagage + N threads
        {
            std::vector<RayStats> sts(nthreads);
            memset(sts.data(), 0, sizeof(RayStats)*nthreads);
            const long long chunk = ((rays / nthreads) / 8) * 8;
            double t0 = now_ms();
            std::vector<std::thread> pool;
            for (int k = 0; k < nthreads; k++) {
                long long b = k * chunk;
                long long e = (k == nthreads-1) ? rays : b + chunk;
                pool.push_back(std::thread([&, k, b, e]() {
                    trace_avx2(ro, rd.data() + b*3, (int)((e-b)/8*8), sts[k], true);
                }));
            }
            for (size_t k = 0; k < pool.size(); k++) pool[k].join();
            double ms = now_ms() - t0;
            RayStats tot = {0,0,0,0.0,0};
            for (int k = 0; k < nthreads; k++) {
                tot.hits += sts[k].hits; tot.iters += sts[k].iters;
                tot.evals += sts[k].evals; tot.checksum += sts[k].checksum;
            }
            char lbl[128];
            sprintf(lbl, "AVX2 x8, %d threads, elagage/paquet", nthreads);
            report(lbl, ms, rays, tot);
        }
        printf("\n");
    }

    // Debit brut d'evaluation de champ : le chiffre transferable, celui qu'on
    // comparera au GPU. Une grille dense, comme pour une polygonisation.
    {
        g_blend = 0.25f;
        const int G = 192;   // 192^3 = 7,08 M points
        long long evals = 0;
        double t0 = now_ms();
        double acc = 0.0;
        for (int k = 0; k < G; k++) {
            float z = -2.0f + 4.0f * k / (G-1);
            for (int j = 0; j < G; j++) {
                float y = -2.0f + 4.0f * j / (G-1);
                for (int i = 0; i < G; i++) {
                    float x = -2.0f + 4.0f * i / (G-1);
                    acc += field(x, y, z, &evals);
                }
            }
        }
        double ms = now_ms() - t0;
        printf("--- grille dense %d^3 (%.2f M points), melange doux, 1 thread ---\n",
               G, (double)G*G*G/1e6);
        printf("%9.1f ms | %.1f M points/s | %.1f M evaluations de primitive/s | somme %.1f\n\n",
               ms, (double)G*G*G/(ms*1000.0), (double)evals/(ms*1000.0), acc);
    }

    // La meme grille, multithreadee : le point de comparaison honnete du GPU.
    {
        g_blend = 0.25f;
        const int G = 192;
        std::vector<long long> ev(nthreads, 0);
        std::vector<double> ac(nthreads, 0.0);
        double t0 = now_ms();
        std::vector<std::thread> pool;
        for (int t = 0; t < nthreads; t++) {
            pool.push_back(std::thread([&, t]() {
                for (int k = t; k < G; k += nthreads) {
                    float z = -2.0f + 4.0f * k / (G-1);
                    for (int j = 0; j < G; j++) {
                        float y = -2.0f + 4.0f * j / (G-1);
                        for (int i = 0; i < G; i++) {
                            float x = -2.0f + 4.0f * i / (G-1);
                            ac[t] += field(x, y, z, &ev[t]);
                        }
                    }
                }
            }));
        }
        for (size_t k = 0; k < pool.size(); k++) pool[k].join();
        double ms = now_ms() - t0;
        long long evals = 0; double acc = 0.0;
        for (int t = 0; t < nthreads; t++) { evals += ev[t]; acc += ac[t]; }
        printf("--- meme grille, %d threads (scalaire) ---\n", nthreads);
        printf("%9.1f ms | %.1f M points/s | %.1f M evaluations de primitive/s | somme %.1f\n\n",
               ms, (double)G*G*G/(ms*1000.0), (double)evals/(ms*1000.0), acc);
    }

    // La meme grille, AVX2 ET multithreadee. C'est la SEULE comparaison
    // honnete avec le GPU : opposer un CPU scalaire a un GPU vectorise
    // gonflerait le facteur d'un ordre de grandeur.
    {
        g_blend = 0.25f;
        const int G = 192;
        std::vector<int> all(g_prims.size());
        for (int i = 0; i < (int)g_prims.size(); i++) all[i] = i;
        std::vector<double> ac(nthreads, 0.0);
        double t0 = now_ms();
        std::vector<std::thread> pool;
        for (int th = 0; th < nthreads; th++) {
            pool.push_back(std::thread([&, th]() {
                long long ev = 0;
                double acc = 0.0;
                float buf[8];
                for (int k = th; k < G; k += nthreads) {
                    __m256 z = _mm256_set1_ps(-2.0f + 4.0f * k / (G-1));
                    for (int j = 0; j < G; j++) {
                        __m256 y = _mm256_set1_ps(-2.0f + 4.0f * j / (G-1));
                        for (int i = 0; i + 8 <= G; i += 8) {
                            float xs[8];
                            for (int l = 0; l < 8; l++)
                                xs[l] = -2.0f + 4.0f * (i+l) / (G-1);
                            __m256 x = _mm256_loadu_ps(xs);
                            __m256 d = field8(x, y, z, all.data(),
                                              (int)all.size(), &ev);
                            _mm256_storeu_ps(buf, d);
                            for (int l = 0; l < 8; l++) acc += buf[l];
                        }
                    }
                }
                ac[th] = acc;
            }));
        }
        for (size_t k = 0; k < pool.size(); k++) pool[k].join();
        double ms = now_ms() - t0;
        double acc = 0.0;
        for (int th = 0; th < nthreads; th++) acc += ac[th];
        long long pts = (long long)(G/8*8) * G * G;
        printf("--- meme grille, %d threads, AVX2 x8 ---\n", nthreads);
        printf("%9.1f ms | %.1f M points/s | %.1f M evaluations de primitive/s | somme %.1f\n\n",
               ms, (double)pts/(ms*1000.0),
               (double)pts*g_prims.size()/(ms*1000.0), acc);
    }

    // ---- robustesse : que vaut le pas quand le champ n'est plus une distance
    //
    // Le smin de Quilez casse la propriete de distance dans la zone de raccord.
    // La question pratique n'est pas "de combien" mais "est-ce que le marcheur
    // traverse la surface". On trace la meme image a plusieurs facteurs de pas
    // et on compare a une reference tres conservatrice (pas x 0,1).
    {
        printf("=== Robustesse du pas sur un champ melange ===\n");
        printf("reference : facteur de pas 0,10 (conservateur)\n\n");
        const int RW = 256;
        std::vector<float> rd2;
        float ro2[3];
        make_rays(RW, RW, rd2, ro2);
        const int NR = RW * RW;

        std::vector<float> ref(NR), cur(NR);

        for (int mode = 0; mode < 2; mode++) {
            g_blend = (mode == 0) ? 0.0f : 0.25f;
            // La reference a besoin d'un plafond tres haut : a pas 0,10 il faut
            // dix fois plus d'iterations. Avec MAXIT = 128 la reference elle-meme
            // rate la moitie des surfaces, et toute la table devient fausse.
            MAXIT = 20000;
            g_step_scale = 0.10f;
            g_record = ref.data();
            RayStats st = {0,0,0,0.0,0};
            trace_scalar(ro2, rd2.data(), NR, st, true);
            const long long ref_hits = st.hits;
            MAXIT = 128;

            printf("--- melange %s --- (reference : %lld touches, %.1f it/rayon)\n",
                   mode == 0 ? "DUR" : "DOUX (k = 0,25)",
                   ref_hits, (double)st.iters/NR);
            printf("%8s | %8s | %10s | %8s | %14s | %12s | %10s\n",
                   "pas", "it/rayon", "touches", "plafonnes", "fuites",
                   "ecart moyen", "ecart max");

            const float scales[] = { 0.50f, 0.80f, 0.95f, 1.00f, 1.20f, 1.50f };
            for (int s = 0; s < 6; s++) {
                g_step_scale = scales[s];
                g_record = cur.data();
                RayStats c = {0,0,0,0.0,0};
                trace_scalar(ro2, rd2.data(), NR, c, true);
                // Une "fuite" est un rayon qui touchait dans la reference et
                // qui ne touche plus : le marcheur a saute par-dessus la
                // surface. C'est le mode d'echec visible d'un champ non
                // lipschitzien -- des trous dans l'objet.
                long long leaks = 0;
                double sum = 0.0, worst = 0.0;
                long long both = 0;
                for (int i = 0; i < NR; i++) {
                    if (ref[i] > 0 && cur[i] < 0) leaks++;
                    else if (ref[i] > 0 && cur[i] > 0) {
                        double e = fabs(cur[i] - ref[i]);
                        sum += e; both++;
                        if (e > worst) worst = e;
                    }
                }
                printf("%8.2f | %8.1f | %10lld | %8lld | %6lld (%5.2f%%) | %12.6f | %10.6f\n",
                       scales[s], (double)c.iters/NR, c.hits, c.capped,
                       leaks, 100.0*leaks/(double)(ref_hits ? ref_hits : 1),
                       both ? sum/both : 0.0, worst);
            }
            printf("\n");
        }
        g_record = 0;
        g_step_scale = 1.0f;
    }

    // ---- la mesure qui donne le facteur de securite --------------------------
    //
    // Un sphere tracer est correct si et seulement si le champ est 1-lipschitzien,
    // c'est-a-dire si |grad f| <= 1 partout. Si le maximum vaut L > 1, le champ
    // SURESTIME la distance d'un facteur L et le pas sur doit valoir 1/L.
    // On mesure L par differences centrees sur une grille, pour plusieurs
    // rayons de melange. C'est le seul chiffre qui dit s'il faut brider le pas.
    {
        printf("=== Norme du gradient du champ : |grad f| ===\n");
        printf("(un sphere tracer a pas plein est sur si et seulement si max <= 1)\n\n");
        printf("%12s | %10s | %10s | %12s | %12s\n",
               "melange k", "max", "p99,99", "%% > 1,01", "pas sur = 1/max");
        const float ks[] = { 0.0f, 0.05f, 0.10f, 0.25f, 0.50f, 1.00f };
        const int G = 160;
        const float hstep = 1.0f / 4096.0f;
        std::vector<float> norms;
        norms.reserve((size_t)G*G*G);
        for (int ki = 0; ki < 6; ki++) {
            g_blend = ks[ki];
            long long ev = 0;
            double maxn = 0.0;
            long long over = 0, total = 0;
            norms.clear();
            for (int k = 0; k < G; k++) {
                float z = -2.2f + 4.4f * k / (G-1);
                for (int j = 0; j < G; j++) {
                    float y = -2.2f + 4.4f * j / (G-1);
                    for (int i = 0; i < G; i++) {
                        float x = -2.2f + 4.4f * i / (G-1);
                        // On ne mesure que le voisinage de la surface : c'est la
                        // que le marcheur passe, et la que le melange agit.
                        float d0 = field(x, y, z, &ev);
                        if (fabsf(d0) > 0.6f) continue;
                        float gx = (field(x+hstep,y,z,&ev) - field(x-hstep,y,z,&ev));
                        float gy = (field(x,y+hstep,z,&ev) - field(x,y-hstep,z,&ev));
                        float gz = (field(x,y,z+hstep,&ev) - field(x,y,z-hstep,&ev));
                        float n = sqrtf(gx*gx + gy*gy + gz*gz) / (2.0f*hstep);
                        norms.push_back(n);
                        total++;
                        if (n > 1.01f) over++;
                        if (n > maxn) maxn = n;
                    }
                }
            }
            std::sort(norms.begin(), norms.end());
            float p9999 = norms.empty() ? 0.0f
                        : norms[(size_t)(norms.size() * 0.9999)];
            printf("%12.2f | %10.4f | %10.4f | %11.4f%% | %15.3f\n",
                   ks[ki], maxn, p9999,
                   total ? 100.0*over/total : 0.0,
                   maxn > 0 ? 1.0/maxn : 0.0);
        }
        printf("\n");
    }

    return 0;
}
