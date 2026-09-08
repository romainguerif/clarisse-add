#include <chrono>
// Banc d'essai GPU : le meme champ de distance que sdf_cpu.cpp, sur CUDA.
//
// Trois questions, et une seule compte vraiment :
//
//   A. LA LATENCE D'UN LANCEMENT. Si lancer un noyau coute X microsecondes,
//      alors appeler le GPU depuis intersect_primitive -- qui traite quelques
//      dizaines de rayons a la fois, depuis 24 threads CPU concurrents -- est
//      absurde des que X depasse le temps que le CPU met a resoudre ces rayons.
//      On mesure X, et on en deduit le SEUIL DE RENTABILITE en nombre de rayons.
//
//   B. Le debit en sphere tracing plein ecran, pour l'apercu viewport.
//
//   C. Le debit d'evaluation d'une grille dense, pour la polygonisation / bake.
//
// Compile avec : nvcc -O3 -arch=sm_86 sdf_gpu.cu -o sdf_gpu.exe

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <cuda_runtime.h>

#define CK(x) do { cudaError_t e = (x); if (e != cudaSuccess) { \
    printf("CUDA erreur %s ligne %d : %s\n", #x, __LINE__, cudaGetErrorString(e)); \
    exit(1); } } while (0)

enum { PRIM_SPHERE = 0, PRIM_BOX = 1, PRIM_TORUS = 2 };

struct Prim {
    float cx, cy, cz;
    float p0, p1, p2;
    int   type;
    float bound;
};

// Meme generateur que le banc CPU : meme scene, comparaison valide.
static unsigned int rng_state = 12345u;
static float frand()
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)((rng_state >> 8) & 0xFFFFFF) / (float)0x1000000;
}

static std::vector<Prim> build_scene(int n)
{
    rng_state = 12345u;
    std::vector<Prim> v;
    for (int i = 0; i < n; i++) {
        Prim p;
        p.cx = (frand() * 2.0f - 1.0f) * 1.6f;
        p.cy = (frand() * 2.0f - 1.0f) * 1.6f;
        p.cz = (frand() * 2.0f - 1.0f) * 1.6f;
        p.type = (int)(frand() * 2.999f);
        float s = 0.35f + frand() * 0.45f;
        if (p.type == PRIM_SPHERE) { p.p0 = s; p.p1 = 0; p.p2 = 0; p.bound = s; }
        else if (p.type == PRIM_BOX) {
            p.p0 = s*0.8f; p.p1 = s*0.6f; p.p2 = s*0.9f;
            p.bound = sqrtf(p.p0*p.p0 + p.p1*p.p1 + p.p2*p.p2);
        } else { p.p0 = s; p.p1 = s*0.32f; p.p2 = 0; p.bound = p.p0 + p.p1; }
        v.push_back(p);
    }
    return v;
}

__constant__ Prim d_prims[512];
__constant__ int  d_nprim;
__constant__ float d_blend;

__device__ __forceinline__ float smin_poly(float a, float b, float k)
{
    float h = fmaxf(k - fabsf(a - b), 0.0f) / k;
    return fminf(a, b) - h * h * k * 0.25f;
}

__device__ __forceinline__ float prim_dist(const Prim& p, float x, float y, float z)
{
    float dx = x - p.cx, dy = y - p.cy, dz = z - p.cz;
    if (p.type == PRIM_SPHERE) return sqrtf(dx*dx + dy*dy + dz*dz) - p.p0;
    if (p.type == PRIM_BOX) {
        float qx = fabsf(dx) - p.p0, qy = fabsf(dy) - p.p1, qz = fabsf(dz) - p.p2;
        float mx = fmaxf(qx, 0.f), my = fmaxf(qy, 0.f), mz = fmaxf(qz, 0.f);
        return sqrtf(mx*mx + my*my + mz*mz)
             + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.f);
    }
    float qx = sqrtf(dx*dx + dz*dz) - p.p0;
    return sqrtf(qx*qx + dy*dy) - p.p1;
}

__device__ __forceinline__ float field(float x, float y, float z)
{
    float d = 1e30f;
    if (d_blend <= 0.0f) {
        for (int i = 0; i < d_nprim; i++)
            d = fminf(d, prim_dist(d_prims[i], x, y, z));
    } else {
        for (int i = 0; i < d_nprim; i++) {
            float di = prim_dist(d_prims[i], x, y, z);
            d = (i == 0) ? di : smin_poly(d, di, d_blend);
        }
    }
    return d;
}

// -------------------------------------------------------------- les noyaux

__global__ void k_empty() { }

__global__ void k_trace(const float* __restrict__ rd, float* __restrict__ out,
                        int count, float ox, float oy, float oz)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    float dx = rd[i*3+0], dy = rd[i*3+1], dz = rd[i*3+2];
    float t = 0.0f;
    float res = -1.0f;
    for (int it = 0; it < 128; it++) {
        float d = field(ox + dx*t, oy + dy*t, oz + dz*t);
        if (d < 1e-4f) { res = t; break; }
        t += d;
        if (t > 12.0f) break;
    }
    out[i] = res;
}

__global__ void k_grid(float* __restrict__ out, int G)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = G * G * G;
    if (idx >= total) return;
    int i = idx % G;
    int j = (idx / G) % G;
    int k = idx / (G * G);
    float x = -2.0f + 4.0f * i / (G - 1);
    float y = -2.0f + 4.0f * j / (G - 1);
    float z = -2.0f + 4.0f * k / (G - 1);
    out[idx] = field(x, y, z);
}

// ---------------------------------------------------------------- le pilote

static void make_rays(int w, int h, std::vector<float>& rd)
{
    rd.resize((size_t)w * h * 3);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            float u = ((i + 0.5f) / w * 2.0f - 1.0f) * 1.1f;
            float v = ((j + 0.5f) / h * 2.0f - 1.0f) * 1.1f;
            float dx = u, dy = v, dz = -2.2f;
            float len = sqrtf(dx*dx + dy*dy + dz*dz);
            size_t o = ((size_t)j * w + i) * 3;
            rd[o+0] = dx/len; rd[o+1] = dy/len; rd[o+2] = dz/len;
        }
}

int main(int argc, char** argv)
{
    int nprim = (argc > 1) ? atoi(argv[1]) : 64;
    int w = (argc > 2) ? atoi(argv[2]) : 1024;
    int h = w;

    cudaDeviceProp prop;
    CK(cudaGetDeviceProperties(&prop, 0));
    printf("\n=== Sphere tracing GPU (CUDA) ===\n");
    printf("carte : %s | %d SM | %.1f Go | CC %d.%d\n",
           prop.name, prop.multiProcessorCount,
           prop.totalGlobalMem / 1073741824.0, prop.major, prop.minor);

    std::vector<Prim> prims = build_scene(nprim);
    int np = nprim;
    float blend = 0.25f;
    CK(cudaMemcpyToSymbol(d_prims, prims.data(), sizeof(Prim) * nprim));
    CK(cudaMemcpyToSymbol(d_nprim, &np, sizeof(int)));
    CK(cudaMemcpyToSymbol(d_blend, &blend, sizeof(float)));

    // Reveil du contexte : le premier lancement paie l'initialisation.
    k_empty<<<1,1>>>(); CK(cudaDeviceSynchronize());

    cudaEvent_t e0, e1;
    CK(cudaEventCreate(&e0)); CK(cudaEventCreate(&e1));

    // ---- A. la latence d'un lancement -------------------------------------
    printf("\n--- A. latence d'un lancement de noyau (le chiffre qui decide) ---\n");
    {
        const int N = 20000;
        // A1 : lancement seul, sans synchronisation (cout cote CPU du lancement)
        cudaDeviceSynchronize();
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) k_empty<<<1,32>>>();
        auto t1 = std::chrono::steady_clock::now();
        CK(cudaDeviceSynchronize());
        auto t2 = std::chrono::steady_clock::now();
        double us_enqueue = std::chrono::duration<double, std::micro>(t1-t0).count() / N;
        double us_total   = std::chrono::duration<double, std::micro>(t2-t0).count() / N;
        printf("noyau vide, mise en file seule            : %8.3f us / lancement\n", us_enqueue);
        printf("noyau vide, file + execution (amortie)    : %8.3f us / lancement\n", us_total);
    }
    {
        // A2 : lancement + synchronisation immediate. C'EST le cas d'un appel
        // synchrone depuis intersect_primitive : on ne peut pas continuer sans
        // le resultat.
        const int N = 2000;
        cudaDeviceSynchronize();
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; i++) { k_empty<<<1,32>>>(); cudaDeviceSynchronize(); }
        auto t1 = std::chrono::steady_clock::now();
        printf("noyau vide, lancement + sync (bloquant)   : %8.3f us / aller-retour\n",
               std::chrono::duration<double, std::micro>(t1-t0).count() / N);
    }

    // ---- A3. aller-retour complet avec transfert ---------------------------
    printf("\n--- A3. aller-retour complet : N rayons montes, traces, redescendus ---\n");
    printf("%10s | %12s | %14s | %14s\n",
           "N rayons", "aller-retour", "us / rayon", "Mrayons/s equiv.");
    {
        std::vector<float> rd; make_rays(1024, 1024, rd);
        float *drd = 0, *dout = 0;
        CK(cudaMalloc(&drd, sizeof(float) * 3 * 1024 * 1024));
        CK(cudaMalloc(&dout, sizeof(float) * 1024 * 1024));
        std::vector<float> host_out(1024*1024);

        int sizes[] = { 8, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576 };
        for (int s = 0; s < 9; s++) {
            int N = sizes[s];
            int reps = N <= 4096 ? 2000 : (N <= 65536 ? 500 : 50);
            cudaDeviceSynchronize();
            auto t0 = std::chrono::steady_clock::now();
            for (int r = 0; r < reps; r++) {
                CK(cudaMemcpy(drd, rd.data(), sizeof(float)*3*N, cudaMemcpyHostToDevice));
                k_trace<<<(N+255)/256, 256>>>(drd, dout, N, 0.f, 0.f, 5.f);
                CK(cudaMemcpy(host_out.data(), dout, sizeof(float)*N, cudaMemcpyDeviceToHost));
            }
            auto t1 = std::chrono::steady_clock::now();
            double us = std::chrono::duration<double, std::micro>(t1-t0).count() / reps;
            printf("%10d | %9.1f us | %11.4f us | %13.2f\n",
                   N, us, us / N, N / us);
        }
        cudaFree(drd); cudaFree(dout);
    }

    // ---- B. sphere tracing plein ecran ------------------------------------
    printf("\n--- B. sphere tracing plein ecran %dx%d, %d primitives, melange doux ---\n",
           w, h, nprim);
    {
        std::vector<float> rd; make_rays(w, h, rd);
        const int N = w * h;
        float *drd = 0, *dout = 0;
        CK(cudaMalloc(&drd, sizeof(float)*3*N));
        CK(cudaMalloc(&dout, sizeof(float)*N));
        CK(cudaMemcpy(drd, rd.data(), sizeof(float)*3*N, cudaMemcpyHostToDevice));

        // chauffe
        k_trace<<<(N+255)/256, 256>>>(drd, dout, N, 0.f, 0.f, 5.f);
        CK(cudaDeviceSynchronize());

        const int reps = 50;
        CK(cudaEventRecord(e0));
        for (int r = 0; r < reps; r++)
            k_trace<<<(N+255)/256, 256>>>(drd, dout, N, 0.f, 0.f, 5.f);
        CK(cudaEventRecord(e1));
        CK(cudaEventSynchronize(e1));
        float ms = 0; CK(cudaEventElapsedTime(&ms, e0, e1));
        ms /= reps;

        std::vector<float> out(N);
        CK(cudaMemcpy(out.data(), dout, sizeof(float)*N, cudaMemcpyDeviceToHost));
        long long hits = 0; double sum = 0;
        for (int i = 0; i < N; i++) if (out[i] > 0) { hits++; sum += out[i]; }

        printf("noyau seul (sans transfert) : %8.3f ms | %8.2f Mrayons/s | "
               "%.1f%% touches | somme %.3f\n",
               ms, N / (ms*1000.0), 100.0*hits/N, sum);

        // avec transfert du resultat, ce qu'un apercu viewport devrait payer
        cudaDeviceSynchronize();
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < reps; r++) {
            k_trace<<<(N+255)/256, 256>>>(drd, dout, N, 0.f, 0.f, 5.f);
            CK(cudaMemcpy(out.data(), dout, sizeof(float)*N, cudaMemcpyDeviceToHost));
        }
        auto t1 = std::chrono::steady_clock::now();
        double msr = std::chrono::duration<double, std::milli>(t1-t0).count() / reps;
        printf("noyau + relecture vers l'hote : %8.3f ms | %8.2f Mrayons/s\n",
               msr, N / (msr*1000.0));
        cudaFree(drd); cudaFree(dout);
    }

    // ---- C. grille dense ---------------------------------------------------
    printf("\n--- C. grille dense (polygonisation / bake), melange doux ---\n");
    {
        int Gs[] = { 192, 256, 384, 512 };
        for (int gi = 0; gi < 4; gi++) {
            int G = Gs[gi];
            long long total = (long long)G*G*G;
            float* dout = 0;
            if (cudaMalloc(&dout, sizeof(float)*total) != cudaSuccess) {
                printf("%4d^3 : allocation impossible (%.2f Go)\n",
                       G, sizeof(float)*total/1073741824.0);
                continue;
            }
            k_grid<<<(int)((total+255)/256), 256>>>(dout, G);
            CK(cudaDeviceSynchronize());
            const int reps = 10;
            CK(cudaEventRecord(e0));
            for (int r = 0; r < reps; r++)
                k_grid<<<(int)((total+255)/256), 256>>>(dout, G);
            CK(cudaEventRecord(e1));
            CK(cudaEventSynchronize(e1));
            float ms = 0; CK(cudaEventElapsedTime(&ms, e0, e1));
            ms /= reps;
            printf("%4d^3 = %8.2f M points | %8.2f ms | %9.1f M points/s | "
                   "%9.1f M evaluations de primitive/s | tampon %.2f Go\n",
                   G, total/1e6, ms, total/(ms*1000.0),
                   (double)total*nprim/(ms*1000.0),
                   sizeof(float)*total/1073741824.0);
            cudaFree(dout);
        }
    }

    printf("\n");
    return 0;
}
