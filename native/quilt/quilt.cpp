// Le capitonnage : chaque polygone d'un maillage devient un coussin.
//
// C'est la lecture des references que Romain a montrees. Un matelas capitonne,
// un vetement a panneaux gonfles, une sphere couverte de coussins triangulaires
// -- ce ne sont pas des panneaux de tissu simules independamment, c'est un
// maillage dont chaque face a ete gonflee et bordee d'une couture. La topologie
// d'entree dessine le motif : des quads reguliers donnent le matelas, un
// icosaedre donne la sphere.
//
// La forme n'est pas dessinee, elle est simulee. Une membrane cousue sur le
// contour du polygone, a qui on donne plus de matiere qu'il n'en faut, et
// qu'une pression interne pousse vers l'exterieur. Le surplus doit aller
// quelque part : il se plisse.
//
// Quatre choses, toutes apprises en regardant des rendus rates :
//
//   - Le mou n'est pas un reglage libre. Gonfler un coussin de hauteur h sur un
//     rayon r consomme deja (2/3)(h/r)^2 de longueur d'arc. Si on demande moins
//     de mou que ca, le volume vise et des aretes inextensibles se contredisent
//     et le solveur broie -- c'est exactement le froissement chaotique qu'on a
//     vu. Ici le mou necessaire est calcule, et l'artiste ne regle que le
//     surplus au-dela : celui qui fait les plis.
//
//   - Les compliances sont normalisees par la taille de maille. La flexion
//     isometrique de Bergou a une energie en 3/A : sans ce facteur, doubler la
//     resolution change la matiere. Ce qui rendait la cascade multigrille
//     absurde, chaque niveau simulant un tissu different du precedent.
//
//   - Tout se resout dans un repere normalise, centre sur le coussin et
//     divise par son rayon. Les memes reglages tiennent alors sur un matelas de
//     six metres et sur une facette de sphere centimetrique.
//
//   - Le bord n'est pas une bague rigide posee dans le plan avec une jupe
//     collee dessous. C'est le contour du polygone lui-meme, enfonce dans le
//     sillon, et le tissu roule par-dessus parce qu'on le simule jusque-la. La
//     bande de couture est juste du tissu tendu -- des longueurs au repos
//     raccourcies -- ce qui donne un epaulement net au lieu d'une arete morte.

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
#include <geometry_object.h>
#include <geometry_point_cloud.h>

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>
#include <gmath_vec3.h>
#include <gmath_matrix4x4.h>

#include <curve_core.h>
#include <cloth_solver.h>

#include <quilt.cma>

using namespace curve_core;

namespace {

// Les reglages d'un coussin, rassembles pour ne pas trainer trente arguments.
struct Settings {
    unsigned int resolution;
    unsigned int max_resolution;
    unsigned int levels;
    unsigned int iterations;
    unsigned int smoothing;
    double smoothing_amount;

    double seam;            // largeur de la bande tendue, en fraction du coussin
    double seam_depth;      // enfoncement du sillon, en fraction du rayon

    double puff;            // hauteur visee, en fraction du rayon
    double shoulder;        // exposant du profil : haut = dessus plat, flancs raides
    double squareness;      // arrondi des angles du coussin
    double pressure;        // multiplicateur du volume vise
    double pressure_softness;

    double wrinkles;        // surplus de matiere par rapport au patron du coussin
    double corner_gather;   // surplus supplementaire aux quatre coins
    double wrinkle_reach;   // jusqu'ou le surplus remonte depuis la couture
    double stuffing;        // fermete du rembourrage ; 0 = enveloppe vide

    double stretch;
    double shear;
    double wrinkle_scale;   // largeur de pli visee, en fraction du coussin
    double wrinkle_size;    // la meme, en unites du monde ; 0 = utiliser la fraction

    double gravity_strength;
    GMathVec3d gravity;

    double detail;          // taille de maille visee en unites monde, 0 = resolution fixe

    double jitter;
    unsigned int seed;
};

// Un quad de membrane a resoudre. Le contour est deja enfonce dans le sillon,
// et chaque coin porte sa normale : c'est ce qui fait que deux coussins voisins
// enfoncent leur arete commune au meme endroit, et qu'une sphere capitonnee
// n'ouvre pas de fissures.
struct Patch {
    GMathVec3d corner[4];
    GMathVec3d normal[4];
    double depth[4];        // taille locale du maillage, par coin
    GMathVec3d face_normal;
};

// Ce que le solveur voit : le meme quad, centre et divise par son rayon.
struct LocalPatch {
    GMathVec3d corner[4];
    GMathVec3d normal;
    double area;            // aire normalisee
    double half_width;      // distance moyenne centre - milieu d'arete, normalisee
};

inline unsigned int
grid_index(const unsigned int& i, const unsigned int& j, const unsigned int& n)
{
    return j * (n + 1u) + i;
}

inline GMathVec3d
quad_point(const GMathVec3d c[4], const double& u, const double& v)
{
    return vlerp(vlerp(c[0], c[1], u), vlerp(c[3], c[2], u), v);
}

inline double
smoothstep01(const double& width, const double& x)
{
    if (width <= 0.0) return 1.0;
    double t = x / width;
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return t * t * (3.0 - 2.0 * t);
}

// Le profil du coussin vise : nul sur tout le contour, plat au sommet, et --
// c'est le point delicat -- de pente finie au ras de la couture.
//
// La premiere version elevait un sinus a une puissance inferieure a un, ce qui
// donne bien un dessus aplati et des flancs redresses, mais dont la pente est
// infinie au bord. Un patron a tangente verticale n'est pas suivable : le tissu
// ne peut pas monter droit hors de sa couture, et il se rabattait en un clapet
// dresse au milieu de chaque arete -- un champignon noir, bien visible au
// rendu. La forme 1 - (1 - t)^k a la meme allure et une pente qui vaut
// simplement k, ce qui la rend representable.
inline double
cushion_profile(const double& t, const double& shoulder)
{
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return 1.0 - pow(1.0 - t, shoulder);
}

// La couture est un mplat, pas une tension.
//
// La premiere idee etait de raccourcir les longueurs au repos dans la bande de
// couture pour y tendre le tissu. C'etait une erreur, et le rendu l'a dit :
// cette bande a un bord epingle, donc le tissu ne peut pas y ceder en glissant,
// il ne peut que tirer l'epaulement vers le bas. Et il tire d'autant plus fort
// que l'epaulement est raide, c'est-a-dire au milieu de chaque arete -- d'ou
// une encoche noire, exactement la, sur tous les coussins.
//
// Le vrai capitonnage ne fait pas ca : il laisse un mplat autour du coussin, et
// le bombement ne commence qu'apres. C'est une forme, pas une contrainte, donc
// elle est toujours realisable.
// Le bombement se lit sur la distance au bord, pas sur un produit de deux
// profils. Le produit affaisse les coins deux fois et donne un coussin rond
// perdu au milieu de sa case ; ce qu'on veut est un carre aux angles adoucis,
// qui remplit la sienne. La distance est donc un minimum lisse des deux
// distances aux bords, dont l'exposant regle justement l'arrondi des angles.
inline double
cushion_bump(const double& u, const double& v, const double& shoulder,
             const double& flat, const double& squareness)
{
    const double span = 1.0 - flat;
    if (span <= 1e-6) return 0.0;

    const double du = (2.0 * ((u < 0.5) ? u : 1.0 - u) - flat) / span;
    const double dv = (2.0 * ((v < 0.5) ? v : 1.0 - v) - flat) / span;
    if (du <= 0.0 || dv <= 0.0) return 0.0;

    const double p = squareness;
    double m = pow(pow(du, -p) + pow(dv, -p), -1.0 / p)
             * pow(2.0, 1.0 / p);
    if (m > 1.0) m = 1.0;
    return cushion_profile(m, shoulder);
}

void
add_distance(cloth::Solver& solver, const unsigned int& a,
             const unsigned int& b, const double& give,
             const double& compliance)
{
    cloth::Distance d;
    d.a = a;
    d.b = b;
    d.rest = vsub(solver.positions[a], solver.positions[b]).get_length() * give;
    // La compliance d'un ressort est proportionnelle a sa longueur : une maille
    // deux fois plus fine est deux fois plus raide, et le tissu garde la meme
    // elasticite quelle que soit la resolution.
    d.compliance = compliance * d.rest;
    d.lambda = 0.0;
    solver.distances.add(d);
}

void
add_triangle(cloth::Solver& solver, const unsigned int& a,
             const unsigned int& b, const unsigned int& c)
{
    solver.pressure.triangles.add(a);
    solver.pressure.triangles.add(b);
    solver.pressure.triangles.add(c);
}

// Le surplus de matiere, point par point, en multiplicateur des longueurs du
// patron.
//
// C'est le changement decisif par rapport a la premiere version. Les longueurs
// au repos ne viennent plus du carre a plat mais du coussin vise lui-meme :
// le patron est deja de la bonne taille pour la forme demandee, et ce qu'on
// ajoute ici est du surplus pur. Avant, le mou devait a la fois fournir le
// gonflement et faire les plis, ce qui obligeait a en mettre beaucoup -- et
// beaucoup de mou ne fait pas beaucoup de petits plis, il fait un seul grand.
//
// Zero au bord, ou le tissu est meme legerement retreci : les plis y meurent et
// la bordure reste nette, comme sur les references.
double
surplus_at(const Settings& settings, const double& u, const double& v)
{
    const double du = 2.0 * ((u < 0.5) ? u : 1.0 - u);
    const double dv = 2.0 * ((v < 0.5) ? v : 1.0 - v);
    const double edge = (du < dv) ? du : dv;

    const double band = smoothstep01(settings.seam * 2.0, edge);

    // Ou vit le surplus. Sur un vrai capitonnage, le tissu est tendu sur la
    // couronne du coussin -- c'est la partie la plus bombee, celle qui a le
    // plus de forme a couvrir -- et il en reste en trop dans la couronne basse,
    // juste au-dessus de la couture. Les plis y naissent, montent un peu, et
    // meurent avant le sommet. Etaler le surplus uniformement donne au
    // contraire un dessus grumeleux, qui n'est le dessus de rien.
    double reach = edge - settings.seam * 2.0;
    if (reach < 0.0) reach = 0.0;
    const double ring =
        0.15 + 0.85 * (1.0 - smoothstep01(settings.wrinkle_reach, reach));
    const double extra = settings.wrinkles * ring;

    // Le fronce des coins. Un coin est le seul endroit ou le tissu est tenu
    // dans deux directions a la fois ; sur un vrai capitonnage c'est la qu'on
    // rentre l'exces, et c'est de la que partent les plis en etoile. Il est
    // multiplie par la bande comme le reste : au ras de la couture le tissu
    // doit rester tendu, sinon la bordure redevient molle.
    double corner = 0.0;
    if (settings.corner_gather > 0.0) {
        const double radius = 0.55;
        const double cu = 1.0 - smoothstep01(radius, du);
        const double cv = 1.0 - smoothstep01(radius, dv);
        corner = settings.corner_gather * cu * cv;
    }
    return 1.0 + (extra + corner) * band;
}

// La membrane d'un patch, resolue a la resolution `n`, dans le repere
// normalise. Les positions de depart peuvent venir d'un niveau plus grossier :
// c'est tout l'interet de la cascade, les grandes formes sont deja la et les
// passes fines n'ont plus qu'a poser les plis.
void
solve_patch(const LocalPatch& patch, const Settings& settings,
            const double& bend_alpha, const double& wrinkle,
            const unsigned int& level, const unsigned int& n,
            const unsigned int& seed, CoreArray<GMathVec3d>& grid)
{
    cloth::Solver solver;
    const unsigned int side = n + 1u;
    const unsigned int count = side * side;
    const bool warm = (grid.get_count() == count);

    // Le patron : le coussin vise, dessine analytiquement. Il sert a trois
    // choses a la fois -- les longueurs au repos, le volume vise, et la
    // position de depart. C'est ce qui fait que le solveur commence deja pres
    // de la reponse et n'a plus qu'a decider ou tombent les plis.
    for (unsigned int j = 0; j < side; j++) {
        const double v = double(j) / double(n);
        for (unsigned int i = 0; i < side; i++) {
            const double u = double(i) / double(n);
            solver.positions.add(
                vadd(quad_point(patch.corner, u, v),
                     vscale(patch.normal,
                            settings.puff
                            * cushion_bump(u, v, settings.shoulder,
                                           settings.seam * 2.0,
                                           settings.squareness))));
            solver.inverse_mass.add((i == 0u || j == 0u || i == n || j == n)
                                    ? 0.0 : 1.0);
        }
    }

    for (unsigned int j = 0; j < side; j++) {
        const double v = double(j) / double(n);
        for (unsigned int i = 0; i < side; i++) {
            const double u = double(i) / double(n);
            const unsigned int here = grid_index(i, j, n);
            const double g0 = surplus_at(settings, u, v);

            if (i + 1u < side) {
                const double g1 =
                    surplus_at(settings, double(i + 1u) / double(n), v);
                add_distance(solver, here, grid_index(i + 1u, j, n),
                             0.5 * (g0 + g1), settings.stretch);
            }
            if (j + 1u < side) {
                const double g1 =
                    surplus_at(settings, u, double(j + 1u) / double(n));
                add_distance(solver, here, grid_index(i, j + 1u, n),
                             0.5 * (g0 + g1), settings.stretch);
            }
            // Le cisaillement. Un tissu tisse cede beaucoup plus facilement en
            // biais qu'en droit fil ; sans ce terme la grille se deforme en
            // losanges sans aucune resistance.
            if (i + 1u < side && j + 1u < side) {
                const double gd = surplus_at(settings,
                                             double(i + 1u) / double(n),
                                             double(j + 1u) / double(n));
                add_distance(solver, here, grid_index(i + 1u, j + 1u, n),
                             0.5 * (g0 + gd), settings.shear);
                add_distance(solver, grid_index(i + 1u, j, n),
                             grid_index(i, j + 1u, n),
                             0.5 * (g0 + gd), settings.shear);
            }
        }
    }

    // La flexion, dans les deux directions de la grille. C'est elle qui decide
    // de la largeur des plis : une membrane raide en fait peu et des grands,
    // une souple beaucoup et des petits.
    //
    // Sa pose de repos est le coussin vise, pas le carre a plat. Le
    // capitonnage est cousu puis mis en forme : ce n'est pas au bombement de
    // couter de l'energie de flexion, seulement a ce qui s'en ecarte. Cela
    // laisse la raideur ne gouverner qu'une chose, la largeur des plis, ce qui
    // est exactement ce qu'on veut pouvoir regler.
    for (unsigned int j = 1; j + 1u < side; j++) {
        for (unsigned int i = 1; i + 1u < side; i++) {
            solver.add_bend(grid_index(i, j, n), grid_index(i + 1u, j, n),
                            grid_index(i, j - 1u, n), grid_index(i, j + 1u, n),
                            bend_alpha);
            solver.add_bend(grid_index(i, j, n), grid_index(i, j + 1u, n),
                            grid_index(i - 1u, j, n), grid_index(i + 1u, j, n),
                            bend_alpha);
        }
    }

    // L'enveloppe fermee : la membrane, plus un fond plat. La contrainte de
    // volume n'a de sens que sur une surface close, et le fond reproduit la
    // realite d'un capitonnage, dont le dessous est cousu sur son support.
    const unsigned int floor_point = solver.positions.get_count();
    GMathVec3d floor_centre(0.0, 0.0, 0.0);
    for (unsigned int k = 0; k < 4u; k++) {
        floor_centre = vadd(floor_centre, patch.corner[k]);
    }
    solver.positions.add(vscale(floor_centre, 0.25));
    solver.inverse_mass.add(0.0);

    for (unsigned int j = 0; j < n; j++) {
        for (unsigned int i = 0; i < n; i++) {
            const unsigned int a = grid_index(i, j, n);
            const unsigned int b = grid_index(i + 1u, j, n);
            const unsigned int c = grid_index(i + 1u, j + 1u, n);
            const unsigned int d = grid_index(i, j + 1u, n);
            add_triangle(solver, a, b, c);
            add_triangle(solver, a, c, d);
        }
    }
    for (unsigned int i = 0; i < n; i++) {
        add_triangle(solver, grid_index(i + 1u, 0u, n), grid_index(i, 0u, n),
                     floor_point);
        add_triangle(solver, grid_index(i, n, n), grid_index(i + 1u, n, n),
                     floor_point);
        add_triangle(solver, grid_index(0u, i, n), grid_index(0u, i + 1u, n),
                     floor_point);
        add_triangle(solver, grid_index(n, i + 1u, n), grid_index(n, i, n),
                     floor_point);
    }

    // Le rembourrage : chaque point du tissu est rappele vers le patron par un
    // ressort mou. Une enveloppe close remplie de gaz conserve son volume mais
    // pas sa forme -- donnez-lui du tissu en trop et elle se deforme en une
    // seule grande bosse, ce qui est exactement ce qu'on obtenait. Une ouate,
    // elle, resiste au changement de forme, et c'est elle qui donne au flambage
    // une longueur d'onde definie.
    //
    // La compliance suit l'inverse de l'aire de maille : un sommet represente
    // moins de surface quand la grille se raffine, il doit donc tirer d'autant
    // moins fort, et le rembourrage reste le meme a toute resolution.
    if (settings.stuffing > 1e-6) {
        const double alpha = double(n * n) / (400.0 * settings.stuffing);
        for (unsigned int k = 0; k < count; k++) {
            if (solver.inverse_mass[k] <= 0.0) continue;
            cloth::Anchor anchor;
            anchor.index = k;
            anchor.target = solver.positions[k];
            anchor.compliance = alpha;
            anchor.lambda = 0.0;
            solver.anchors.add(anchor);
        }
    }

    // Le volume vise est celui du patron lui-meme, mesure et non estime : les
    // positions sont encore exactement le coussin dessine, donc `pressure` a 1
    // veut dire « la forme demandee », et rien d'autre.
    solver.pressure.rest_volume = solver.compute_volume();
    solver.pressure.target = settings.pressure;
    solver.pressure.compliance = 0.0;
    solver.pressure.softness = settings.pressure_softness;
    solver.pressure.lambda = 0.0;
    solver.enable_pressure(true);

    // Le point de depart : la forme du niveau precedent s'il y en a un, le
    // patron sinon -- et dans les deux cas un bruit par-dessus.
    //
    // Ce bruit n'est pas cosmetique, et il ne suffit pas de le mettre au
    // premier niveau. Un coussin lisse et symetrique est une configuration
    // stationnaire du solveur : il y resterait quel que soit le surplus de
    // matiere. Et un niveau grossier ne sait representer qu'un seul grand pli ;
    // si les niveaux fins partent de lui sans rien pour les en tirer, ils se
    // contentent de le lisser. C'etait exactement le defaut du premier essai :
    // un unique pli par coussin, identique partout.
    //
    // Sa frequence est celle des plis voulus, pas une frequence arbitraire : on
    // seme directement le mode qu'on veut voir flamber.
    const double frequency = 1.0 / ((wrinkle > 1e-6) ? wrinkle : 1e-6);
    const double amount = settings.jitter * (warm ? 0.5 : 1.0);
    for (unsigned int j = 1; j + 1u < side; j++) {
        const double v = double(j) / double(n);
        for (unsigned int i = 1; i + 1u < side; i++) {
            const double u = double(i) / double(n);
            const unsigned int here = grid_index(i, j, n);
            if (warm) solver.positions[here] = grid[here];

            const double edge = 4.0 * u * (1.0 - u) * v * (1.0 - v);
            const double noise = perlin(u * frequency, v * frequency,
                                        double(level) * 3.7, seed);
            solver.positions[here] =
                vadd(solver.positions[here],
                     vscale(patch.normal, edge * amount * noise));
        }
    }

    solver.solve(settings.iterations,
                 vscale(settings.gravity, settings.gravity_strength));

    grid.resize(count);
    for (unsigned int k = 0; k < count; k++) grid[k] = solver.positions[k];
}

// Prolonge une grille grossiere vers la resolution suivante, par interpolation
// bilineaire.
void
prolongate(const CoreArray<GMathVec3d>& coarse, const unsigned int& from,
           CoreArray<GMathVec3d>& fine, const unsigned int& to)
{
    fine.resize((to + 1u) * (to + 1u));
    for (unsigned int j = 0; j <= to; j++) {
        const double fy = double(j) * double(from) / double(to);
        unsigned int j0 = (unsigned int) fy;
        if (j0 >= from) j0 = (from > 0u) ? from - 1u : 0u;
        const unsigned int j1 = j0 + 1u;
        const double ty = fy - double(j0);

        for (unsigned int i = 0; i <= to; i++) {
            const double fx = double(i) * double(from) / double(to);
            unsigned int i0 = (unsigned int) fx;
            if (i0 >= from) i0 = (from > 0u) ? from - 1u : 0u;
            const unsigned int i1 = i0 + 1u;
            const double tx = fx - double(i0);

            const GMathVec3d a = vlerp(coarse[grid_index(i0, j0, from)],
                                       coarse[grid_index(i1, j0, from)], tx);
            const GMathVec3d b = vlerp(coarse[grid_index(i0, j1, from)],
                                       coarse[grid_index(i1, j1, from)], tx);
            fine[grid_index(i, j, to)] = vlerp(a, b, ty);
        }
    }
}

// Un lissage laplacien sur les points interieurs. Le solveur laisse des
// irregularites de maille -- des sommets qui depassent d'une fraction de
// millimetre -- qui ne se voient pas sur la forme mais accrochent la lumiere en
// speculaire. Quelques passes tres douces les enlevent sans toucher aux plis,
// qui sont beaucoup plus larges qu'une maille.
void
smooth_grid(CoreArray<GMathVec3d>& grid, const unsigned int& n,
            const unsigned int& passes, const double& amount)
{
    if (passes == 0u || amount <= 0.0) return;
    const unsigned int side = n + 1u;
    CoreArray<GMathVec3d> next(grid.get_count());

    for (unsigned int pass = 0; pass < passes; pass++) {
        for (unsigned int k = 0; k < grid.get_count(); k++) next[k] = grid[k];

        for (unsigned int j = 1; j + 1u < side; j++) {
            for (unsigned int i = 1; i + 1u < side; i++) {
                const GMathVec3d average =
                    vscale(vadd(vadd(grid[grid_index(i - 1u, j, n)],
                                     grid[grid_index(i + 1u, j, n)]),
                                vadd(grid[grid_index(i, j - 1u, n)],
                                     grid[grid_index(i, j + 1u, n)])),
                           0.25);
                next[grid_index(i, j, n)] =
                    vlerp(grid[grid_index(i, j, n)], average, amount);
            }
        }
        for (unsigned int k = 0; k < grid.get_count(); k++) grid[k] = next[k];
    }
}

} // namespace

class QuiltModule : public ModulePolymesh {
public:
    QuiltModule() : ModulePolymesh() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "input_geometry", "shading_group",
            "resolution", "max_resolution", "levels", "detail",
            "seam", "seam_depth",
            "puff_relative", "shoulder", "squareness",
            "pressure", "pressure_softness",
            "wrinkles", "corner_gather", "wrinkle_reach", "stuffing",
            "stretch", "shear_stiffness", "wrinkle_scale", "wrinkle_size",
            "gravity", "gravity_strength",
            "iterations", "jitter", "smoothing", "smoothing_amount", "seed"
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
    read_double(const OfObject *object, const char *name, const double& fallback)
    {
        const OfAttr *attr = object->get_attribute(name);
        return (attr != 0) ? attr->get_double() : fallback;
    }

    static long
    read_long(const OfObject *object, const char *name, const long& fallback)
    {
        const OfAttr *attr = object->get_attribute(name);
        return (attr != 0) ? attr->get_long() : fallback;
    }

    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        OfAttr *input = object->get_attribute("input_geometry");
        if (input == 0) return 0;
        OfObject *source = input->get_object();
        if (source == 0) return 0;

        ModuleGeometry *module = source->get_module<ModuleGeometry>();
        if (module == 0) return 0;
        const GeometryObject *geometry = module->get_geometry(false);
        if (geometry == 0) return 0;
        const PolyMesh *mesh = CoreBaseObject::cast<PolyMesh>(geometry);
        if (mesh == 0) return 0;

        const GeometryPointCloud *cloud = mesh->get_point_cloud();
        if (cloud == 0) return 0;

        CoreArray<unsigned int> face_indices;
        CoreArray<unsigned int> face_sizes;
        CoreArray<unsigned int> face_groups;
        mesh->get_polygon_vertex_indices(face_indices);
        mesh->get_polygon_vertex_count(face_sizes);
        mesh->get_polygon_shading_groups(face_groups);

        const unsigned int face_count = face_sizes.get_count();
        if (face_count == 0u) return 0;

        // Le filtre par shading group : la seule selection de faces que
        // Clarisse sache retenir. Un nom vide prend tout le maillage.
        int wanted_group = -1;
        const OfAttr *group_attr = object->get_attribute("shading_group");
        if (group_attr != 0) {
            const CoreString wanted = group_attr->get_string();
            if (wanted.get_count() > 0u) {
                const CoreBasicArray<CoreString>& names =
                    mesh->get_shading_group_names();
                for (unsigned int i = 0; i < names.get_count(); i++) {
                    if (names[i] == wanted) { wanted_group = int(i); break; }
                }
                // Un nom qui ne correspond a rien ne doit pas capitonner tout
                // le maillage par megarde.
                if (wanted_group < 0) return 0;
            }
        }

        ModuleSceneItem *item = source->get_module<ModuleSceneItem>();
        const GMathMatrix4x4d *matrix =
            (item != 0) ? &item->get_global_matrix() : 0;

        const unsigned int point_count = cloud->get_point_count();
        CoreArray<GMathVec3d> points(point_count);
        for (unsigned int i = 0; i < point_count; i++) {
            const GMathVec3f p = cloud->get_position(i);
            GMathVec3d world(static_cast<double>(p[0]),
                             static_cast<double>(p[1]),
                             static_cast<double>(p[2]));
            if (matrix != 0) {
                GMathVec3d transformed;
                GMathMatrix4x4d::multiply(transformed, world, *matrix);
                world = transformed;
            }
            points[i] = world;
        }

        // Les normales aux sommets, accumulees sur tout le maillage. Elles ne
        // servent qu'a une chose, mais elle est decisive : le contour de chaque
        // coussin est enfonce dans le sillon le long de ces normales, et comme
        // deux faces voisines partagent leurs sommets, elles enfoncent leur
        // arete commune exactement au meme endroit. Avec la normale de face, la
        // sphere capitonnee s'ouvrirait a chaque arete.
        CoreArray<GMathVec3d> vertex_normals(point_count);
        CoreArray<double> vertex_scale(point_count);
        CoreArray<double> vertex_weight(point_count);
        for (unsigned int i = 0; i < point_count; i++) {
            vertex_normals[i] = GMathVec3d(0.0, 0.0, 0.0);
            vertex_scale[i] = 0.0;
            vertex_weight[i] = 0.0;
        }
        {
            unsigned int cursor = 0u;
            for (unsigned int f = 0; f < face_count; f++) {
                const unsigned int sides = face_sizes[f];
                const GMathVec3d n = newell(points, face_indices, cursor, sides);
                for (unsigned int s = 0; s < sides; s++) {
                    const unsigned int v = face_indices[cursor + s];
                    const unsigned int w =
                        face_indices[cursor + (s + 1u) % sides];
                    vertex_normals[v] = vadd(vertex_normals[v], n);

                    // La taille locale du maillage, vue depuis ce sommet. Elle
                    // mesure l'enfoncement du sillon, et elle doit venir du
                    // sommet et non de la face : deux faces voisines de tailles
                    // differentes enfonceraient sinon leur arete commune a deux
                    // profondeurs differentes, et le capitonnage se fendrait
                    // tout du long. Cela arrive des le premier maillage un peu
                    // irregulier -- une sphere, par exemple.
                    const double length =
                        vsub(points[w], points[v]).get_length();
                    vertex_scale[v] += length;
                    vertex_weight[v] += 1.0;
                    vertex_scale[w] += length;
                    vertex_weight[w] += 1.0;
                }
                cursor += sides;
            }
            for (unsigned int i = 0; i < point_count; i++) {
                if (vertex_normals[i].get_length() > 1e-12) {
                    vertex_normals[i] = vnorm(vertex_normals[i]);
                }
                vertex_scale[i] = (vertex_weight[i] > 0.0)
                    ? vertex_scale[i] / vertex_weight[i] * 0.7071067811865476
                    : 0.0;
            }
        }

        Settings settings;
        settings.resolution = (unsigned int) read_long(object, "resolution", 14);
        settings.max_resolution =
            (unsigned int) read_long(object, "max_resolution", 40);
        settings.levels = (unsigned int) read_long(object, "levels", 3);
        settings.iterations =
            (unsigned int) read_long(object, "iterations", 160);
        settings.smoothing = (unsigned int) read_long(object, "smoothing", 2);
        settings.smoothing_amount =
            read_double(object, "smoothing_amount", 0.3);
        settings.seam = read_double(object, "seam", 0.06);
        settings.seam_depth = read_double(object, "seam_depth", 0.06);
        settings.puff = read_double(object, "puff_relative", 0.30);
        settings.shoulder = read_double(object, "shoulder", 2.5);
        settings.squareness = read_double(object, "squareness", 4.0);
        settings.pressure = read_double(object, "pressure", 1.0);
        settings.pressure_softness =
            read_double(object, "pressure_softness", 0.005);
        settings.wrinkles = read_double(object, "wrinkles", 0.10);
        settings.corner_gather = read_double(object, "corner_gather", 0.08);
        settings.wrinkle_reach = read_double(object, "wrinkle_reach", 0.5);
        settings.stuffing = read_double(object, "stuffing", 1.0);
        settings.stretch = read_double(object, "stretch", 0.02);
        settings.shear = read_double(object, "shear_stiffness", 4.0);
        settings.wrinkle_scale = read_double(object, "wrinkle_scale", 0.28);
        settings.wrinkle_size = read_double(object, "wrinkle_size", 0.0);
        settings.detail = read_double(object, "detail", 0.0);
        if (settings.shoulder < 1.0) settings.shoulder = 1.0;
        if (settings.squareness < 1.0) settings.squareness = 1.0;
        if (settings.wrinkle_scale < 0.01) settings.wrinkle_scale = 0.01;
        settings.gravity_strength =
            read_double(object, "gravity_strength", 0.05);
        settings.jitter = read_double(object, "jitter", 0.006);
        settings.seed = (unsigned int) read_long(object, "seed", 0);

        if (settings.resolution < 3u) settings.resolution = 3u;
        if (settings.max_resolution < settings.resolution) {
            settings.max_resolution = settings.resolution;
        }
        if (settings.levels < 1u) settings.levels = 1u;
        if (settings.levels > 5u) settings.levels = 5u;

        const OfAttr *gravity_attr = object->get_attribute("gravity");
        settings.gravity = GMathVec3d(0.0, -1.0, 0.0);
        if (gravity_attr != 0) {
            settings.gravity = GMathVec3d(gravity_attr->get_double(0),
                                          gravity_attr->get_double(1),
                                          gravity_attr->get_double(2));
        }
        if (settings.gravity.get_length() > 1e-9) {
            settings.gravity = vnorm(settings.gravity);
        }

        CoreVector<GMathVec3d> out_vertices;
        CoreVector<GMathVec3f> out_uvs;
        CoreVector<unsigned int> out_indices;
        CoreVector<unsigned int> out_sizes;

        unsigned int cursor = 0u;
        for (unsigned int f = 0; f < face_count; f++) {
            const unsigned int sides = face_sizes[f];
            const unsigned int base = cursor;
            cursor += sides;

            if (sides < 3u) continue;
            if (wanted_group >= 0 && f < face_groups.get_count()
                && int(face_groups[f]) != wanted_group) {
                continue;
            }

            build_face(points, vertex_normals, vertex_scale, face_indices,
                       base, sides,
                       settings, settings.seed + f * 7919u,
                       out_vertices, out_uvs, out_indices, out_sizes);
        }

        const unsigned int vertex_total = out_vertices.get_count();
        const unsigned int face_total = out_sizes.get_count();
        if (vertex_total == 0u || face_total == 0u) return 0;

        CoreArray<GMathVec3f> vertices(vertex_total);
        for (unsigned int i = 0; i < vertex_total; i++) {
            vertices[i] = GMathVec3f(float(out_vertices[i][0]),
                                     float(out_vertices[i][1]),
                                     float(out_vertices[i][2]));
        }

        CoreArray<unsigned int> polygon_indices(out_indices.get_count());
        for (unsigned int i = 0; i < out_indices.get_count(); i++) {
            polygon_indices[i] = out_indices[i];
        }

        CoreArray<unsigned int> polygon_vertex_count(face_total);
        CoreArray<unsigned int> polygon_shading_groups(face_total);
        for (unsigned int i = 0; i < face_total; i++) {
            polygon_vertex_count[i] = out_sizes[i];
            polygon_shading_groups[i] = 0u;
        }

        CoreArray<CoreString> shading_group_names(1);
        shading_group_names[0] = "quilt";

        // Un carre UV par coussin : une texture de tissu s y repete sans qu on
        // ait rien a deplier.
        CoreArray<GeometryUvMap> uv_maps(1);
        uv_maps[0].name = "uv";
        uv_maps[0].vertices.resize(vertex_total);
        for (unsigned int i = 0; i < vertex_total; i++) {
            uv_maps[0].vertices[i] = out_uvs[i];
        }

        CoreArray<GMathVec3f> velocities;
        CoreArray<GeometryNormalMap> normal_maps;
        CoreArray<GeometryColorMap> color_maps;

        PolyMesh *result = new PolyMesh;
        result->set(vertices, velocities,
                    polygon_indices, polygon_vertex_count,
                    polygon_shading_groups, shading_group_names,
                    uv_maps, normal_maps, color_maps,
                    true, 0);
        return result;
    }

    // La normale d'un polygone par la formule de Newell : elle tient sur les
    // faces gauches, ce qui est le cas courant des qu'on capitonne autre chose
    // qu'un plan.
    static GMathVec3d
    newell(const CoreArray<GMathVec3d>& points,
           const CoreArray<unsigned int>& face_indices,
           const unsigned int& base, const unsigned int& sides)
    {
        GMathVec3d normal(0.0, 0.0, 0.0);
        for (unsigned int s = 0; s < sides; s++) {
            const GMathVec3d& a = points[face_indices[base + s]];
            const GMathVec3d& b = points[face_indices[base + (s + 1u) % sides]];
            normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
            normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
            normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
        }
        return (normal.get_length() > 1e-12) ? vnorm(normal)
                                             : GMathVec3d(0.0, 1.0, 0.0);
    }

    // Un polygone. Les quads sont traites d'un bloc ; les autres sont decoupes
    // en quads par la regle de Catmull-Clark -- centre, milieux d'aretes, coins
    // -- ce qui permet a un triangle comme a un pentagone de porter une grille
    // reguliere plutot qu'un eventail. C'est ce qui fait tenir la sphere
    // capitonnee aussi bien que le matelas.
    static void
    build_face(const CoreArray<GMathVec3d>& points,
               const CoreArray<GMathVec3d>& vertex_normals,
               const CoreArray<double>& vertex_scale,
               const CoreArray<unsigned int>& face_indices,
               const unsigned int& base, const unsigned int& sides,
               const Settings& settings, const unsigned int& seed,
               CoreVector<GMathVec3d>& out_vertices,
               CoreVector<GMathVec3f>& out_uvs,
               CoreVector<unsigned int>& out_indices,
               CoreVector<unsigned int>& out_sizes)
    {
        const GMathVec3d normal = newell(points, face_indices, base, sides);

        GMathVec3d centre(0.0, 0.0, 0.0);
        GMathVec3d centre_normal(0.0, 0.0, 0.0);
        double centre_scale = 0.0;
        for (unsigned int s = 0; s < sides; s++) {
            const unsigned int v = face_indices[base + s];
            centre = vadd(centre, points[v]);
            centre_normal = vadd(centre_normal, vertex_normals[v]);
            centre_scale += vertex_scale[v];
        }
        centre = vscale(centre, 1.0 / double(sides));
        centre_scale /= double(sides);
        centre_normal = (centre_normal.get_length() > 1e-12)
                      ? vnorm(centre_normal) : normal;

        if (sides == 4u) {
            Patch patch;
            for (unsigned int k = 0; k < 4u; k++) {
                const unsigned int v = face_indices[base + k];
                patch.corner[k] = points[v];
                patch.normal[k] = vertex_normals[v];
                patch.depth[k] = vertex_scale[v];
            }
            patch.face_normal = normal;
            build_patch(patch, settings, seed,
                        out_vertices, out_uvs, out_indices, out_sizes);
            return;
        }

        for (unsigned int s = 0; s < sides; s++) {
            const unsigned int ia = face_indices[base + s];
            const unsigned int ib = face_indices[base + (s + 1u) % sides];
            const unsigned int ip = face_indices[base + (s + sides - 1u) % sides];

            Patch patch;
            patch.corner[0] = vlerp(points[ip], points[ia], 0.5);
            patch.corner[1] = points[ia];
            patch.corner[2] = vlerp(points[ia], points[ib], 0.5);
            patch.corner[3] = centre;

            patch.normal[0] = vnorm(vadd(vertex_normals[ip], vertex_normals[ia]));
            patch.normal[1] = vertex_normals[ia];
            patch.normal[2] = vnorm(vadd(vertex_normals[ia], vertex_normals[ib]));
            patch.normal[3] = centre_normal;
            patch.depth[0] = 0.5 * (vertex_scale[ip] + vertex_scale[ia]);
            patch.depth[1] = vertex_scale[ia];
            patch.depth[2] = 0.5 * (vertex_scale[ia] + vertex_scale[ib]);
            patch.depth[3] = centre_scale;
            patch.face_normal = normal;

            build_patch(patch, settings, seed + s * 131u,
                        out_vertices, out_uvs, out_indices, out_sizes);
        }
    }

    // Un quad de membrane, de bout en bout : mise a l'echelle, cascade de
    // resolutions, lissage, versement dans le maillage.
    static void
    build_patch(const Patch& given, const Settings& settings,
                const unsigned int& seed,
                CoreVector<GMathVec3d>& out_vertices,
                CoreVector<GMathVec3f>& out_uvs,
                CoreVector<unsigned int>& out_indices,
                CoreVector<unsigned int>& out_sizes)
    {
        GMathVec3d centre(0.0, 0.0, 0.0);
        for (unsigned int k = 0; k < 4u; k++) {
            centre = vadd(centre, given.corner[k]);
        }
        centre = vscale(centre, 0.25);

        double extent = 0.0;
        for (unsigned int k = 0; k < 4u; k++) {
            extent += vsub(given.corner[k], centre).get_length();
        }
        extent *= 0.25;
        if (extent < 1e-9) return;

        // Le contour, enfonce dans le sillon le long des normales aux sommets.
        // C'est la seule geometrie epinglee : tout le reste, y compris la
        // remontee du tissu hors du sillon, est simule.
        GMathVec3d world_corner[4];
        for (unsigned int k = 0; k < 4u; k++) {
            world_corner[k] =
                vadd(given.corner[k],
                     vscale(given.normal[k],
                            -settings.seam_depth * given.depth[k]));
        }

        // Le repere normalise : centre sur le coussin, divise par son rayon.
        // Tout ce qui suit y vit, et c'est ce qui fait que les memes reglages
        // donnent la meme forme sur un matelas de six metres et sur une facette
        // de sphere centimetrique.
        LocalPatch local;
        const double inverse = 1.0 / extent;
        for (unsigned int k = 0; k < 4u; k++) {
            local.corner[k] = vscale(vsub(world_corner[k], centre), inverse);
        }
        local.normal = given.face_normal;

        local.area =
            0.5 * (vcross(vsub(local.corner[1], local.corner[0]),
                          vsub(local.corner[3], local.corner[0])).get_length()
                 + vcross(vsub(local.corner[3], local.corner[2]),
                          vsub(local.corner[1], local.corner[2])).get_length());

        GMathVec3d local_centre(0.0, 0.0, 0.0);
        for (unsigned int k = 0; k < 4u; k++) {
            local_centre = vadd(local_centre, local.corner[k]);
        }
        local_centre = vscale(local_centre, 0.25);
        local.half_width = 0.0;
        for (unsigned int k = 0; k < 4u; k++) {
            const GMathVec3d mid =
                vlerp(local.corner[k], local.corner[(k + 1u) % 4u], 0.5);
            local.half_width += vsub(mid, local_centre).get_length();
        }
        local.half_width *= 0.25;

        // La largeur de pli visee, ramenee a une fraction de la largeur du
        // coussin. Donnee en unites du monde, elle devient absolue : un grand
        // coussin fait alors beaucoup de plis fins et un petit deux ou trois,
        // ce qui est le comportement d'un vrai tissu -- sa raideur ne sait rien
        // de la taille de ce qu'on en coud.
        const double width = 2.0 * local.half_width * extent;
        double wrinkle = settings.wrinkle_scale;
        if (settings.wrinkle_size > 1e-9 && width > 1e-9) {
            wrinkle = settings.wrinkle_size / width;
        }
        if (wrinkle > 2.0) wrinkle = 2.0;

        unsigned int target = settings.resolution;
        if (settings.detail > 1e-9) {
            const double cells = 2.0 * extent / settings.detail;
            long value = long(cells + 0.5);
            if (value < 3) value = 3;
            if ((unsigned long) value > (unsigned long) settings.max_resolution) {
                value = long(settings.max_resolution);
            }
            target = (unsigned int) value;
        }
        if (target < 3u) target = 3u;

        // La cascade. Une relaxation en positions ne propage l'information que
        // d'une arete par passe : sur une grille fine, il faudrait des milliers
        // d'iterations pour que deux bords opposes se parlent. En grossier ils
        // se parlent tout de suite, et chaque niveau suivant part de la forme
        // deja trouvee. Les compliances etant normalisees, tous les niveaux
        // simulent bien le meme tissu.
        CoreArray<GMathVec3d> grid;
        unsigned int previous = 0u;
        for (unsigned int level = 0; level < settings.levels; level++) {
            const unsigned int shift = settings.levels - 1u - level;
            unsigned int n = target >> shift;
            if (n < 3u) n = 3u;
            if (n > target) n = target;

            if (previous != 0u && previous != n) {
                CoreArray<GMathVec3d> finer;
                prolongate(grid, previous, finer, n);
                grid.resize(finer.get_count());
                for (unsigned int i = 0; i < finer.get_count(); i++) {
                    grid[i] = finer[i];
                }
            } else if (previous == 0u) {
                grid.resize(0u);
            }

            // La compliance de flexion, calee sur la largeur de pli voulue.
            //
            // Deux physiques se croisent ici. L'energie isometrique de Bergou
            // est en 3/A : la compliance doit suivre l'aire de maille, sans
            // quoi doubler la resolution changerait la matiere -- et la
            // cascade simulerait un tissu different a chaque niveau. Et la
            // longueur d'onde de flambage de Cerda-Mahadevan va en racine
            // quatrieme de la raideur : d'ou la puissance quatre, qui convertit
            // une largeur de pli en une souplesse. Le facteur devant est cale
            // sur une mesure : a largeur de pli egale au coussin, on obtient
            // bien un seul grand pli.
            double lambda = wrinkle;
            const double floor_lambda = 4.0 / double(n);
            if (lambda < floor_lambda) lambda = floor_lambda;
            const double cell_area = local.area / double(n * n);
            const double square = lambda * lambda;

            solve_patch(local, settings, 0.6 * cell_area / (square * square),
                        lambda, level, n, seed, grid);
            previous = n;
        }

        const unsigned int n = previous;
        smooth_grid(grid, n, settings.smoothing, settings.smoothing_amount);

        const unsigned int side = n + 1u;
        const unsigned int first = out_vertices.get_count();

        for (unsigned int j = 0; j < side; j++) {
            for (unsigned int i = 0; i < side; i++) {
                out_vertices.add(vadd(centre,
                                      vscale(grid[grid_index(i, j, n)], extent)));
                out_uvs.add(GMathVec3f(float(double(i) / double(n)),
                                       float(double(j) / double(n)), 0.0f));
            }
        }

        for (unsigned int j = 0; j < n; j++) {
            for (unsigned int i = 0; i < n; i++) {
                out_indices.add(first + grid_index(i, j, n));
                out_indices.add(first + grid_index(i + 1u, j, n));
                out_indices.add(first + grid_index(i + 1u, j + 1u, n));
                out_indices.add(first + grid_index(i, j + 1u, n));
                out_sizes.add(4u);
            }
        }
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryQuilt, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryQuilt)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryQuilt);
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
    QuiltModule *module = new QuiltModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
