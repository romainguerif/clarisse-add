// Le rayon construit depuis un pixel passe-t-il vraiment par le point qu'on
// visait ?
//
// La question ne peut pas se poser dans le viewport -- il n'y en a pas dans
// cnode -- mais elle n'en a pas besoin : c'est de l'arithmetique pure. On prend
// un point du monde, on le projette avec la fonction de Clarisse, on le
// convertit en pixel comme le fait l'outil, puis on relance un rayon depuis ce
// pixel et on regarde s'il repasse par le point. Si l'aller-retour boucle, la
// construction du rayon est juste et un decalage vient d'ailleurs.
//
// Se compile seul : gmath est fait d'en-tetes et de fonctions inline.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <gmath_vec3.h>
#include <gmath_matrix4x4.h>
#include <gmath_view_point.h>

#include <cstdio>
#include <cmath>

namespace {

// La construction du rayon, recopiee telle quelle depuis l'outil. Si elle
// change la-bas, elle doit changer ici -- c'est le prix d'un test qui ne peut
// pas appeler le code d'origine.
bool
build_ray(const GMathViewPoint& view, const int& width, const int& height,
          const double& px, const double& py,
          GMathVec3d& origin, GMathVec3d& direction)
{
    const double nx = 2.0 * (px / double(width)) - 1.0;
    const double ny = 1.0 - 2.0 * (py / double(height));

    const double h = view.get_horizontal_zoom_factor(1.0);
    const double v = view.get_vertical_zoom_factor(1.0);
    const double aspect = view.get_aspect_ratio();
    const double lx = (aspect != 0.0) ? (nx * h / aspect) : (nx * h);
    const double ly = ny * v;

    const GMathVec3d x_axis = view.get_x_axis();
    const GMathVec3d y_axis = view.get_y_axis();
    const GMathVec3d z_axis = view.get_z_axis();

    origin = view.get_eye_position();

    if (view.is_orthographic()) {
        for (unsigned int i = 0; i < 3; i++) {
            origin[i] += x_axis[i] * lx + y_axis[i] * ly;
            direction[i] = -z_axis[i];
        }
    } else {
        for (unsigned int i = 0; i < 3; i++) {
            direction[i] = x_axis[i] * lx + y_axis[i] * ly - z_axis[i];
        }
    }

    const double length = direction.get_length();
    if (length < 1e-12) return false;
    for (unsigned int i = 0; i < 3; i++) direction[i] /= length;
    return true;
}

// Le pixel d'un point du monde, comme le fait la plume.
bool
project(const GMathViewPoint& view, const int& width, const int& height,
        const GMathVec3d& world, double& px, double& py)
{
    GMathMatrix4x4d inverse;
    view.get_matrix().get_inverse(inverse);

    GMathVec3d camera;
    view.project_point(inverse, world, camera);
    if (camera[2] > -1e-9) return false;

    px = (camera[0] * 0.5 + 0.5) * double(width);
    py = (0.5 - camera[1] * 0.5) * double(height);
    return true;
}

// La distance du point a la droite du rayon.
double
miss(const GMathVec3d& origin, const GMathVec3d& direction,
     const GMathVec3d& target)
{
    const GMathVec3d d(target[0] - origin[0], target[1] - origin[1],
                       target[2] - origin[2]);
    const double along = d[0] * direction[0] + d[1] * direction[1]
                       + d[2] * direction[2];
    const GMathVec3d closest(origin[0] + direction[0] * along - target[0],
                             origin[1] + direction[1] * along - target[1],
                             origin[2] + direction[2] * along - target[2]);
    return closest.get_length();
}

int
run(const char *label, const bool& orthographic, const double& aspect,
    const int& width, const int& height)
{
    GMathViewPoint view;
    view.set_orthographic(orthographic);
    view.set_aspect_ratio(aspect);
    view.set_fields_of_view(45.0 * aspect, 45.0);
    view.set_eye_distance(12.0);
    view.set_center(GMathVec3d(0.4, -0.3, 0.9));
    view.set_rotation(GMathVec3d(-27.0, 34.0, 0.0));

    // Des points repartis dans le cadre, pas seulement au centre : un facteur
    // d'echelle faux ne se voit qu'en s'eloignant du milieu.
    const double spread[] = { -3.5, -1.0, 0.0, 1.2, 3.7 };
    double worst = 0.0;
    unsigned int tested = 0;

    for (unsigned int i = 0; i < 5; i++) {
        for (unsigned int j = 0; j < 5; j++) {
            const GMathVec3d target(spread[i], spread[j],
                                    0.5 * spread[(i + j) % 5]);
            double px, py;
            if (!project(view, width, height, target, px, py)) continue;
            if (px < 0.0 || py < 0.0 || px > width || py > height) continue;

            GMathVec3d origin, direction;
            if (!build_ray(view, width, height, px, py, origin, direction)) {
                continue;
            }
            const double error = miss(origin, direction, target);
            if (error > worst) worst = error;
            tested++;
        }
    }

    const bool ok = (tested > 0u) && (worst < 1e-9);
    printf("%-28s %2u points, ecart max %.3e  %s\n",
           label, tested, worst, ok ? "ok" : "ECHEC");
    return ok ? 0 : 1;
}

} // namespace

int
main()
{
    printf("\nDEBUT========================================\n");
    int failures = 0;
    failures += run("perspective 16/9", false, 16.0 / 9.0, 1920, 1080);
    failures += run("perspective carre", false, 1.0, 1024, 1024);
    failures += run("perspective haute", false, 0.5, 512, 1024);
    printf("  (l'orthographique ne peut pas se verifier ici : project_point\n"
           "   est une projection perspective, il n'y a pas de reference\n"
           "   independante hors d'un viewport.)\n");
    printf("FIN==========================================\n");
    return failures;
}
