// Sonde du noyau de bokeh : mesurer au lieu de rendre.
//
// Tout ce qui fait une couture dans ce filtre se decide dans la construction
// du noyau et dans l'application du vignettage, et ni l'une ni l'autre ne
// depend de Clarisse. On les bat donc ici, hors du moteur, et on lit les
// nombres. Une passe complete coute une seconde et zero yen ; le meme
// diagnostic au rendu coute une demi-heure et ne dit pas DE COMBIEN on se
// trompe.
//
// Ce que la sonde mesure :
//
//   1. gain    -- ce que le filtre affiche sur un aplat de valeur 1. C'est la
//                 luminosite du resultat, et sous vignettage elle doit suivre
//                 la transmission reelle de la pupille tronquee.
//   2. T       -- cette transmission, integree independamment sur une grille
//                 fine. C'est ce que le gain DOIT valoir.
//   3. la marche d'un pixel au suivant -- une couture est une discontinuite ;
//                 si le gain saute a la frontiere d'une tuile, on la voit.
//   4. la portee -- le rayon reellement atteint par les taps, compare a la
//                 marge que pre_filter reserve autour de la tuile. Un noyau
//                 plus large que sa marge lit du vide au bord de la tuile.
//   5. les cas limites, 6. la conservation de l'energie, 7. la forme.
//
// Compilation :
//   cl /nologo /EHsc /O2 /I..\common bokeh_kernel_probe.cpp /Fe:probe.exe

#include <stdio.h>
#include <math.h>
#include <vector>

#include "bokeh_kernel.h"

using namespace clarisse_add;

const int IMAGE_W = 1920;
const int IMAGE_H = 1080;
const int TILE = 64;

// La vraie transmission du vignettage optique : quelle fraction de l'aire de
// l'ouverture survit au disque de troncature. Integree sur une grille fine et
// SANS passer par le noyau discretise -- c'est le point, on veut une reference
// que le code mesure ne fabrique pas lui-meme.
static double
true_transmission(const KernelSpec& s, const double& strength,
                  const double& frame_x, const double& frame_y,
                  const double& cut_radius)
{
    Aperture a;
    aperture_init(a, s.blades, s.rotation, s.curvature);

    const double frame_r = sqrt(frame_x * frame_x + frame_y * frame_y);
    const double offset = strength * frame_r;
    double sx = 0.0, sy = 0.0;
    if (offset > 1e-9 && frame_r > 1e-9) {
        sx = -frame_x / frame_r * offset;
        sy = -frame_y / frame_r * offset;
    }

    const int N = 1200;
    double inside = 0.0, kept = 0.0;
    for (int j = 0; j < N; ++j) {
        const double y = -1.5 + 3.0 * (j + 0.5) / N;
        for (int i = 0; i < N; ++i) {
            const double x = -1.5 + 3.0 * (i + 0.5) / N;
            const double rho = sqrt(x * x + y * y);
            if (rho > aperture_edge(a, x, y, rho)) continue;
            inside += 1.0;
            const double dx = x - sx, dy = y - sy;
            if (dx * dx + dy * dy <= cut_radius * cut_radius) kept += 1.0;
        }
    }
    return (inside > 0.0) ? kept / inside : 0.0;
}

static Vignette
make_vignette(const Kernel& k, const KernelSpec& s, const double& strength)
{
    Vignette v;
    v.active = (strength > 1e-6);
    v.strength = strength;
    v.radius = s.radius;
    v.scale_x = k.scale_x;
    v.scale_y = k.scale_y;
    v.half_w = IMAGE_W * 0.5;
    v.half_h = IMAGE_H * 0.5;
    v.feather = 1.0;
    v.cut = Vignette::cut_radius(s.radius, v.feather);
    return v;
}

// Ce que le filtre affiche sur un aplat de valeur 1, pour un pixel donne.
// Somme des poids reellement appliques, divisee par l'energie du noyau entier :
// c'est exactement ce que fait `convolve_pixel` avec preserve_exposure a non.
static double
flat_gain(const Kernel& k, const Vignette& v, const double& px, const double& py)
{
    if (!v.active) return 1.0;
    double ox, oy;
    v.centre(px, py, ox, oy);
    double used = 0.0;
    for (size_t i = 0; i < k.all.size(); ++i)
        used += k.all[i].weight * v.cover(k.all[i].dx, k.all[i].dy, ox, oy);
    return (k.total > 0.0) ? used / k.total : 0.0;
}

// Rayon du tap le plus eloigne, en pixels, et portee sur chaque axe -- c'est
// celle-la qui doit tenir dans la marge du proxy, puisque la marge est
// rectangulaire.
static void
measured_reach(const Kernel& k, double& euclidean, int& axis)
{
    euclidean = 0.0;
    axis = 0;
    for (size_t i = 0; i < k.all.size(); ++i) {
        const int ax = k.all[i].dx < 0 ? -k.all[i].dx : k.all[i].dx;
        const int ay = k.all[i].dy < 0 ? -k.all[i].dy : k.all[i].dy;
        if (ax > axis) axis = ax;
        if (ay > axis) axis = ay;
        const double r = sqrt((double) k.all[i].dx * k.all[i].dx
                              + (double) k.all[i].dy * k.all[i].dy);
        if (r > euclidean) euclidean = r;
    }
}

static KernelSpec
base_spec(double radius, int blades)
{
    KernelSpec s;
    s.radius = radius;
    s.blades = blades;
    s.keep_taps = true;
    return s;
}

// -- 1. le vignettage assombrit-il vraiment ? ---------------------------------
static void
probe_vignetting_energy()
{
    printf("\n== 1. vignettage : gain affiche contre transmission reelle ==\n");
    printf("radius 40, disque, preserve_exposure = non, image %dx%d\n\n",
           IMAGE_W, IMAGE_H);
    // Deux references. `T modele` integre la geometrie que le filtre applique
    // vraiment -- disque de troncature un demi-pixel plus grand que
    // l'ouverture -- et mesure donc l'erreur de DISCRETISATION. `T disque 1`
    // integre le disque unite theorique et mesure l'ecart de MODELE, celui
    // qu'on a choisi pour ne pas compter deux fois le meme bord.
    printf("  frame_r    gain    T modele  ecart    T disque 1  ecart\n");

    KernelSpec s = base_spec(40.0, 0);
    Kernel k;
    bokeh_build_kernel(k, s);
    Vignette v = make_vignette(k, s, 1.0);
    const double cut_units = v.cut / v.radius;

    for (int i = 0; i <= 10; ++i) {
        const double fr = i / 10.0;
        // Un pixel sur l'axe horizontal, a la distance voulue du centre.
        const double px = IMAGE_W * 0.5 + fr * IMAGE_W * 0.5;
        const double g = flat_gain(k, v, px, IMAGE_H * 0.5);
        const double t = true_transmission(s, 1.0, fr, 0.0, cut_units);
        const double t1 = true_transmission(s, 1.0, fr, 0.0, 1.0);
        printf("   %5.2f    %6.4f   %6.4f  %+6.2f %%   %6.4f  %+6.2f %%\n",
               fr, g, t, (t > 0.0 ? (g / t - 1.0) * 100.0 : 0.0),
               t1, (t1 > 0.0 ? (g / t1 - 1.0) * 100.0 : 0.0));
    }
}

// -- 2. la marche d'un pixel au suivant ---------------------------------------
static void
probe_tile_step()
{
    printf("\n== 2. discontinuite du gain sur la grille des tuiles ==\n");
    printf("image %d de large, tuiles de %d, radius 40, disque.\n", IMAGE_W, TILE);
    printf("saut = |gain(x) - gain(x-1)| en %% ; il doit etre du meme ordre\n");
    printf("partout, sur la grille comme ailleurs.\n\n");
    printf("  vignettage   saut moyen /grille   saut moyen /ailleurs   rapport\n");

    KernelSpec s = base_spec(40.0, 0);
    Kernel k;
    bokeh_build_kernel(k, s);

    for (int vi = 0; vi < 3; ++vi) {
        const double strength = 0.3 + vi * 0.35;
        Vignette v = make_vignette(k, s, strength);
        double on = 0.0, off = 0.0;
        int on_count = 0, off_count = 0;
        double previous = flat_gain(k, v, 200.0, IMAGE_H * 0.5);
        for (int x = 201; x < IMAGE_W - 200; ++x) {
            const double g = flat_gain(k, v, x, IMAGE_H * 0.5);
            const double step = fabs(g - previous) / (previous > 0.0 ? previous : 1.0)
                                * 100.0;
            if (x % TILE == 0) { on += step; ++on_count; }
            else { off += step; ++off_count; }
            previous = g;
        }
        on = on_count ? on / on_count : 0.0;
        off = off_count ? off / off_count : 0.0;
        printf("     %.2f          %8.5f %%            %8.5f %%        %6.2f\n",
               strength, on, off, (off > 1e-12) ? on / off : 0.0);
    }
}

// -- 3. portee reelle contre marge reservee -----------------------------------
static void
probe_reach()
{
    printf("\n== 3. portee du noyau contre marge reservee par pre_filter ==\n");
    printf("marge = bokeh_kernel_reach(rayon * echelle de canal, douceur) + 2.5,\n");
    printf("la MEME fonction que celle qui borne les boucles de construction.\n");
    printf("chromatic_offset garde son defaut (0.6, 1, 1)\n\n");
    printf("  radius  chroma  soft  ana   canal   portee/axe    marge   marge nette\n");

    struct Case { double radius, chromatic, softness, anamorphism; };
    const Case cases[] = {
        {40.0,  0.0, 0.0, 0.0}, {40.0,  0.0, 0.5, 0.0}, {40.0,  0.0, 1.0, 0.0},
        {40.0,  1.0, 0.0, 0.0}, {40.0, -1.0, 0.0, 0.0}, {40.0, -0.5, 0.0, 0.0},
        {40.0, -1.0, 0.5, 0.0}, {80.0, -1.0, 0.0, 0.0}, {40.0, -1.0, 0.5, 1.0},
        {40.0, -1.0, 0.5, -1.0},
    };
    const double offsets[3] = {0.6, 1.0, 1.0};

    for (int i = 0; i < 10; ++i) {
        const Case& c = cases[i];
        double widest = 1.0;
        if (c.chromatic != 0.0)
            for (int ch = 0; ch < 3; ++ch) {
                const double sc = 1.0 + c.chromatic * (offsets[ch] - 1.0);
                if (sc > widest) widest = sc;
            }
        const double margin = bokeh_kernel_reach(c.radius * widest, c.softness) + 2.5;

        for (int ch = 0; ch < 3; ++ch) {
            if (ch != 0 && c.chromatic == 0.0) continue;
            const double scale = 1.0 + c.chromatic * (offsets[ch] - 1.0);
            KernelSpec s = base_spec(c.radius * scale, 0);
            s.softness = c.softness;
            s.anamorphism = c.anamorphism;
            Kernel k;
            bokeh_build_kernel(k, s);
            double euclid;
            int axis;
            measured_reach(k, euclid, axis);
            const char *names = "RGB";
            printf("  %5.0f   %+5.2f  %4.2f %+4.1f    %c    %8d    %7.2f   %+8.2f%s\n",
                   c.radius, c.chromatic, c.softness, c.anamorphism, names[ch],
                   axis, margin, margin - axis,
                   (axis > margin) ? "   <-- DEBORDE" : "");
        }
    }
}

// -- 4. la jupe de douceur est-elle tranchee ? --------------------------------
static void
probe_softness()
{
    printf("\n== 4. profil radial du noyau selon la douceur ==\n");
    printf("radius 40, disque. Un bord doux doit descendre a zero en pente ;\n");
    printf("s'il tombe d'un coup, c'est que les boucles s'arretent avant.\n\n");

    const double softs[4] = {0.0, 0.25, 0.5, 1.0};
    for (int si = 0; si < 4; ++si) {
        KernelSpec s = base_spec(40.0, 0);
        s.softness = softs[si];
        Kernel k;
        bokeh_build_kernel(k, s);

        std::vector<double> profile(140, 0.0);
        for (size_t i = 0; i < k.all.size(); ++i) {
            const int r = (int)(sqrt((double) k.all[i].dx * k.all[i].dx
                                     + (double) k.all[i].dy * k.all[i].dy) + 0.5);
            if (r < 140 && k.all[i].weight > profile[r])
                profile[r] = k.all[i].weight;
        }
        double euclid;
        int axis;
        measured_reach(k, euclid, axis);
        printf("  softness %.2f  (portee attendue %.0f px, obtenue %d px sur l'axe)\n",
               softs[si], 40.0 * (1.0 + softs[si]), axis);
        printf("   rho px :");
        for (int r = 34; r <= 84; r += 2) printf(" %3d", r);
        printf("\n   poids  :");
        for (int r = 34; r <= 84; r += 2)
            printf(" %3d", (int)(profile[r] * 100.0 + 0.5));
        printf("\n\n");
    }
}

// -- 5. cas limites -----------------------------------------------------------
static void
probe_edge_cases()
{
    printf("\n== 5. cas limites : le noyau reste-t-il sain ? ==\n");
    printf("  cas                          taps    total   gain coin   axe\n");

    struct Case { const char *name; KernelSpec s; double vignetting; };
    std::vector<Case> cases;
    #define ADD(NAME, SPEC, VIGN) { Case c; c.name = NAME; c.s = SPEC; \
                                    c.vignetting = VIGN; cases.push_back(c); }
    ADD("radius 0.5 (seuil bas)", base_spec(0.5, 0), 0.0)
    ADD("radius 0.6, 3 lames", base_spec(0.6, 3), 0.0)
    {
        KernelSpec s = base_spec(40.0, 3); s.curvature = -1.0;
        ADD("3 lames, roundness -1", s, 0.0)
    }
    {
        KernelSpec s = base_spec(40.0, 3); s.curvature = -0.5;
        ADD("3 lames, roundness -0.5", s, 0.0)
    }
    {
        KernelSpec s = base_spec(40.0, 32); s.curvature = -1.0;
        ADD("32 lames, roundness -1", s, 0.0)
    }
    {
        KernelSpec s = base_spec(40.0, 0); s.spherical = -1.0;
        ADD("spherical -1", s, 0.0)
    }
    {
        KernelSpec s = base_spec(40.0, 0); s.spherical = 1.0;
        ADD("spherical +1", s, 0.0)
    }
    {
        KernelSpec s = base_spec(40.0, 0); s.anamorphism = 1.0;
        ADD("anamorphism +1", s, 0.0)
    }
    {
        KernelSpec s = base_spec(40.0, 0); s.anamorphism = -1.0;
        ADD("anamorphism -1", s, 0.0)
    }
    ADD("vignetting 1", base_spec(40.0, 0), 1.0)
    {
        KernelSpec s = base_spec(40.0, 0); s.spherical = 1.0;
        ADD("vign 1 + spherical 1", s, 1.0)
    }
    {
        KernelSpec s = base_spec(40.0, 0); s.softness = 1.0;
        ADD("vign 1 + softness 1", s, 1.0)
    }
    #undef ADD

    for (size_t i = 0; i < cases.size(); ++i) {
        Kernel k;
        bokeh_build_kernel(k, cases[i].s);
        Vignette v = make_vignette(k, cases[i].s, cases[i].vignetting);
        const double g = flat_gain(k, v, IMAGE_W, IMAGE_H);   // le coin
        double euclid;
        int axis;
        measured_reach(k, euclid, axis);
        const bool bad = (k.total <= 0.0) || (k.total != k.total)
                         || (g != g) || (g < 0.0);
        printf("  %-28s %5d %8.1f   %8.4f  %5d%s\n",
               cases[i].name, (int) k.all.size(), k.total, g, axis,
               bad ? "   <-- SUSPECT" : "");
    }
}

// -- 6. conservation de l'energie ---------------------------------------------
static void
probe_energy()
{
    printf("\n== 6. conservation de l'energie sur un aplat (gain doit valoir 1) ==\n");
    printf("  radius  blades  round  spher  soft   gain    ecart\n");
    struct Case { double radius; int blades; double round, spher, soft; };
    const Case cases[] = {
        { 2.0, 0,  0.0,  0.0, 0.0}, { 8.0, 0,  0.0,  0.0, 0.0},
        {40.0, 0,  0.0,  0.0, 0.0}, {40.0, 6,  0.0,  0.0, 0.0},
        {40.0, 6,  1.0,  0.0, 0.0}, {40.0, 6, -0.8,  0.0, 0.0},
        {40.0, 0,  0.0,  0.9, 0.0}, {40.0, 0,  0.0, -0.9, 0.0},
        {40.0, 0,  0.0,  0.0, 0.5}, {40.0, 5,  0.5,  0.5, 0.3},
    };
    for (int i = 0; i < 10; ++i) {
        const Case& c = cases[i];
        KernelSpec s = base_spec(c.radius, c.blades);
        s.curvature = c.round;
        s.spherical = c.spher;
        s.softness = c.soft;
        Kernel k;
        bokeh_build_kernel(k, s);
        // Sans vignettage, la somme appliquee vaut le total : le gain est 1.
        double applied = 0.0;
        for (size_t j = 0; j < k.runs.size(); ++j)
            applied += (k.runs[j].x1 - k.runs[j].x0 + 1) * k.level_weight;
        for (size_t j = 0; j < k.edge.size(); ++j) applied += k.edge[j].weight;
        const double g = (k.total > 0.0) ? applied / k.total : 0.0;
        printf("  %6.1f  %6d  %+5.2f  %+5.2f  %4.2f  %6.4f  %+7.3f %%%s\n",
               c.radius, c.blades, c.round, c.spher, c.soft, g,
               (g - 1.0) * 100.0, (fabs(g - 1.0) > 0.005) ? "   <-- FUITE" : "");
    }
}

// -- 7. la forme de l'ouverture est-elle celle qu'on demande ? ----------------
//
// Une lame bombee suit un arc passant par les deux sommets. Au sommet le rayon
// vaut 1 par construction ; au MILIEU de la lame il vaut apotheme + bombement
// en convexe, apotheme - bombement en creux. Ce sont deux valeurs connues
// d'avance : il suffit de les comparer a ce que rend aperture_edge.
static void
probe_aperture_shape()
{
    printf("\n== 7. forme de l'ouverture : rayon au milieu de la lame ==\n");
    printf("attendu = apotheme +- |roundness| * (1 - apotheme), le bombement\n");
    printf("creux etant plafonne a 9/10 de l'apotheme pour que l'ouverture ne\n");
    printf("disparaisse pas. Au sommet le rayon doit valoir 1.\n\n");
    printf("  lames  round   milieu attendu   milieu obtenu   sommet   ecart\n");

    const double rounds[4] = {1.0, 0.5, -0.5, -1.0};
    for (int ri = 0; ri < 4; ++ri) {
        for (int n = 3; n <= 9; ++n) {
            Aperture a;
            aperture_init(a, n, 0.0, rounds[ri]);
            const double half = APERTURE_PI / n;
            const double apothem = cos(half);
            double bulge = fabs(rounds[ri]) * (1.0 - apothem);
            if (rounds[ri] < 0.0 && bulge > 0.9 * apothem) bulge = 0.9 * apothem;
            const double expected = (rounds[ri] < 0.0) ? (apothem - bulge)
                                                       : (apothem + bulge);
            const double mid = aperture_edge_at(a, half);
            const double vertex = aperture_edge_at(a, 0.0);
            printf("  %5d  %+5.2f   %12.4f   %13.4f   %6.4f  %+7.4f%s\n",
                   n, rounds[ri], expected, mid, vertex, mid - expected,
                   (fabs(mid - expected) > 0.01) ? "   <-- FAUX" : "");
        }
        printf("\n");
    }
}

// -- 8. prediction pour le banc de rendu --------------------------------------
//
// Les sections precedentes valident le noyau et la couverture du vignettage,
// mais elles somment les taps un par un. La convolution reelle, elle, passe par
// des SEGMENTS intersectes avec le disque de troncature, et sous anamorphisme
// ce disque devient une ellipse dans l'espace des pixels -- un chemin de code
// que rien d'autre n'exerce.
//
// On predit donc ici ce que le filtre doit rendre sur l'aplat a 0,5 du banc de
// rendu, pour la variante anamorphique. Les deux implementations n'ont rien en
// commun : si elles tombent d'accord, l'intersection segment-ellipse est juste.
static void
probe_render_prediction()
{
    printf("\n== 8. prediction pour la variante anamorphique du banc ==\n");
    printf("rayon 120, vignettage 0.8, anamorphisme 0.5, aplat source a 0.5\n");
    printf("a comparer aux colonnes du rendu, ligne y=870\n\n");

    KernelSpec s = base_spec(120.0, 0);
    s.anamorphism = 0.5;
    Kernel k;
    bokeh_build_kernel(k, s);
    Vignette v = make_vignette(k, s, 0.8);
    v.scale_x = k.scale_x;
    v.scale_y = k.scale_y;

    printf("      x  :");
    for (int x = 200; x <= 1700; x += 250) printf(" %8d", x);
    printf("\n  valeur :");
    for (int x = 200; x <= 1700; x += 250)
        printf(" %8.5f", 0.5 * flat_gain(k, v, x, 870.0));
    printf("\n");
}

int
main()
{
    probe_vignetting_energy();
    probe_tile_step();
    probe_reach();
    probe_softness();
    probe_edge_cases();
    probe_energy();
    probe_aperture_shape();
    probe_render_prediction();
    printf("\n");
    return 0;
}
