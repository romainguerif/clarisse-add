// Le capitonnage : chaque polygone d'un maillage devient un coussin.
//
// C'est la lecture des references que Romain a montrees. Un matelas capitonne,
// un vetement a panneaux gonfles, une sphere couverte de coussins triangulaires
// -- ce ne sont pas des panneaux de tissu simules independamment, c'est un
// maillage dont chaque face a ete gonflee et bordee d'une couture. La topologie
// d'entree dessine le motif : des quads reguliers donnent le matelas, un
// icosaedre donne la sphere.
//
// Trois observations tirees des images, et chacune est un morceau du code :
//
//   - un sillon separe les coussins. C'est un retrait du contour vers le
//     centre, plus un enfoncement : la couture est en creux, pas a plat.
//   - le bombement n'est pas une bosse spherique. Il monte vite en quittant la
//     couture puis s'aplatit, comme un volume rempli qui pousse contre son
//     enveloppe. D'ou un profil reglable par courbe plutot qu'une formule.
//   - les plis naissent aux coins et meurent vers le centre. Un coin est le
//     seul endroit ou le tissu est tendu dans deux directions a la fois ; c'est
//     de la qu'il se fronce. Les repartir uniformement sur le pourtour donne
//     un aspect de ballon, pas de coussin cousu.
//
// La subdivision se fait par un eventail depuis le centre du polygone, ce qui
// traite les quads et les n-gones sans cas particulier -- necessaire des que le
// maillage d'entree n'est pas une grille.

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

class QuiltModule : public ModulePolymesh {
public:
    QuiltModule() : ModulePolymesh() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "input_geometry", "shading_group", "resolution",
            "seam", "slack", "puff_relative", "pressure", "seam_depth",
            "stiffness", "bend_stiffness", "iterations", "jitter",
            "gravity", "seed"
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
    // Un coussin en cours de construction : ses sommets et ses quads, en
    // indices locaux, avant d'etre verses dans le maillage final.
    struct Cushion {
        CoreVector<GMathVec3d> vertices;
        CoreVector<GMathVec3f> uvs;
        CoreVector<unsigned int> quads;
    };

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
        // Clarisse retienne. Un nom vide prend tout.
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

        // Les positions du maillage source, remises dans le monde.
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

        unsigned int resolution =
            (unsigned int) object->get_attribute("resolution")->get_long();
        if (resolution < 2u) resolution = 2u;

        const double seam = object->get_attribute("seam")->get_double();
        const double slack = object->get_attribute("slack")->get_double();
        const double pressure = object->get_attribute("pressure")->get_double();
        const double stiffness = object->get_attribute("stiffness")->get_double();
        const double bend = object->get_attribute("bend_stiffness")->get_double();
        const double jitter = object->get_attribute("jitter")->get_double();
        const unsigned int iterations =
            (unsigned int) object->get_attribute("iterations")->get_long();

        const OfAttr *gravity_attr = object->get_attribute("gravity");
        GMathVec3d gravity(0.0, -1.0, 0.0);
        if (gravity_attr != 0) {
            gravity = GMathVec3d(gravity_attr->get_double(0),
                                 gravity_attr->get_double(1),
                                 gravity_attr->get_double(2));
        }
        const double puff_relative =
            object->get_attribute("puff_relative")->get_double();
        const double seam_depth = object->get_attribute("seam_depth")->get_double();

        const unsigned int seed =
            (unsigned int) object->get_attribute("seed")->get_long();

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

            build_cushion(points, face_indices, base, sides, resolution,
                          seam, slack, puff_relative, pressure, seam_depth,
                          stiffness, bend, iterations, jitter, gravity,
                          seed + f * 7919u,
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

        // Les UV sont vertex-varying : chaque coussin porte son propre carre
        // 0-1, donc une texture de tissu se repete par coussin sans qu'on ait
        // a la deplier.
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

    // Un coussin, simule.
    //
    // La forme ne vient plus d'un profil dessine mais d'un equilibre : une
    // membrane cousue sur son contour, a qui on a donne plus de matiere qu'il
    // n'en faut pour couvrir le trou, et qu'une pression interne pousse vers
    // l'exterieur. C'est ce surplus de matiere -- le mou -- qui doit aller
    // quelque part, et qui produit les bourrelets et les plis. Un profil, lui,
    // ne fait que deplacer la surface : il ne sait pas qu'il y a de la matiere
    // en trop.
    //
    // Le volume est ferme par un fond plat epingle, ce qui fait deux choses a
    // la fois : ca donne un sens a la contrainte de pression, qui n'est definie
    // que sur une enveloppe fermee, et ca reproduit la realite d'un capitonnage
    // -- le dessous est cousu sur son support et ne bouge pas.
    static void
    build_cushion(const CoreArray<GMathVec3d>& points,
                  const CoreArray<unsigned int>& face_indices,
                  const unsigned int& base, const unsigned int& sides,
                  const unsigned int& resolution,
                  const double& seam, const double& slack,
                  const double& puff_relative, const double& pressure,
                  const double& seam_depth,
                  const double& stiffness, const double& bend_stiffness,
                  const unsigned int& iterations,
                  const double& jitter, const GMathVec3d& gravity,
                  const unsigned int& seed,
                  CoreVector<GMathVec3d>& out_vertices,
                  CoreVector<GMathVec3f>& out_uvs,
                  CoreVector<unsigned int>& out_indices,
                  CoreVector<unsigned int>& out_sizes)
    {
        GMathVec3d centre(0.0, 0.0, 0.0);
        for (unsigned int s = 0; s < sides; s++) {
            centre = vadd(centre, points[face_indices[base + s]]);
        }
        centre = vscale(centre, 1.0 / double(sides));

        // Normale de Newell : elle tient sur les faces gauches, ce qui est le
        // cas courant des qu'on capitonne autre chose qu'un plan.
        GMathVec3d normal(0.0, 0.0, 0.0);
        for (unsigned int s = 0; s < sides; s++) {
            const GMathVec3d& a = points[face_indices[base + s]];
            const GMathVec3d& b = points[face_indices[base + (s + 1u) % sides]];
            normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
            normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
            normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
        }
        normal = vnorm(normal);

        double extent = 0.0;
        double area = 0.0;
        for (unsigned int s = 0; s < sides; s++) {
            const GMathVec3d& a = points[face_indices[base + s]];
            const GMathVec3d& b = points[face_indices[base + (s + 1u) % sides]];
            extent += vsub(a, centre).get_length();
            area += vcross(vsub(a, centre), vsub(b, centre)).get_length() * 0.5;
        }
        extent /= double(sides);

        unsigned int span = resolution / 2u;
        if (span < 2u) span = 2u;
        const unsigned int loop = sides * span;
        const unsigned int rings = resolution;

        // Le contour interieur : la couture. C'est lui qui tient la membrane.
        CoreVector<GMathVec3d> contour;
        for (unsigned int s = 0; s < sides; s++) {
            const GMathVec3d& a = points[face_indices[base + s]];
            const GMathVec3d& b = points[face_indices[base + (s + 1u) % sides]];
            for (unsigned int j = 0; j < span; j++) {
                contour.add(vlerp(a, b, double(j) / double(span)));
            }
        }

        cloth::Solver solver;

        // Anneau 0 : le contour d'origine, enfonce, epingle.
        for (unsigned int i = 0; i < loop; i++) {
            solver.positions.add(vadd(contour[i], vscale(normal, -seam_depth)));
            solver.inverse_mass.add(0.0);
        }

        // Anneaux 1 a rings : la membrane. Elle part plate, un peu au-dessus du
        // plan pour que la pression sache de quel cote pousser -- et surtout
        // avec un bruit, sans lequel elle resterait plate quoi qu'on fasse : le
        // plan est une configuration exactement stationnaire du solveur, les
        // corrections y sont toutes coplanaires.
        for (unsigned int r = 1; r <= rings; r++) {
            const double u = double(r - 1u) / double(rings - 1u);
            const double shrink = (1.0 - seam) * (1.0 - u);
            for (unsigned int i = 0; i < loop; i++) {
                GMathVec3d p = vadd(centre,
                                    vscale(vsub(contour[i], centre), shrink));
                const double bump = 0.02 * extent * (1.0 - shrink);
                const double noise = jitter * extent
                    * perlin(double(i) * 0.35, double(r) * 0.35, 0.0, seed);
                p = vadd(p, vscale(normal, bump + noise));
                solver.positions.add(p);
                solver.inverse_mass.add(1.0);
            }
        }

        const unsigned int apex = solver.positions.get_count();
        solver.positions.add(vadd(centre, vscale(normal, 0.03 * extent)));
        solver.inverse_mass.add(1.0);

        // Le fond : un seul point, epingle, qui ferme le volume.
        const unsigned int floor_point = solver.positions.get_count();
        solver.positions.add(vadd(centre, vscale(normal, -seam_depth)));
        solver.inverse_mass.add(0.0);

        // --- les contraintes ---------------------------------------------
        //
        // Les longueurs au repos sont celles du patron a plat multipliees par
        // le mou. C'est tout le mecanisme : sans surplus de matiere, la
        // membrane est deja tendue a plat et la pression ne peut que l'etirer,
        // ce qui donne une bosse lisse. Avec du surplus, elle se plisse.
        const double give = 1.0 + slack;

        for (unsigned int r = 1; r <= rings; r++) {
            for (unsigned int i = 0; i < loop; i++) {
                const unsigned int here = r * loop + i;
                const unsigned int next = r * loop + (i + 1u) % loop;
                add_distance(solver, here, next, give, stiffness);

                const unsigned int below = (r - 1u) * loop + i;
                add_distance(solver, below, here, give, stiffness);

                // Cisaillement : les diagonales du quad. Sans elles la membrane
                // se deforme en losanges sans resistance.
                const unsigned int below_next = (r - 1u) * loop + (i + 1u) % loop;
                add_distance(solver, below, next, give, stiffness * 4.0);
                add_distance(solver, here, below_next, give, stiffness * 4.0);
            }
        }
        for (unsigned int i = 0; i < loop; i++) {
            add_distance(solver, rings * loop + i, apex, give, stiffness);
        }

        // La flexion, dans les deux directions de la grille.
        if (bend_stiffness < 1e12) {
            for (unsigned int r = 1; r + 1u <= rings; r++) {
                for (unsigned int i = 0; i < loop; i++) {
                    const unsigned int i1 = (i + 1u) % loop;
                    const unsigned int prev = (i + loop - 1u) % loop;
                    solver.add_bend(r * loop + i, r * loop + i1,
                                    (r - 1u) * loop + i,
                                    (r + 1u <= rings) ? (r + 1u) * loop + i : apex,
                                    bend_stiffness);
                    solver.add_bend((r - 1u) * loop + i, r * loop + i,
                                    r * loop + prev, r * loop + i1,
                                    bend_stiffness);
                }
            }
        }

        // --- le volume ferme ----------------------------------------------
        for (unsigned int r = 0; r < rings; r++) {
            for (unsigned int i = 0; i < loop; i++) {
                const unsigned int i1 = (i + 1u) % loop;
                add_triangle(solver, r * loop + i, r * loop + i1,
                             (r + 1u) * loop + i1);
                add_triangle(solver, r * loop + i, (r + 1u) * loop + i1,
                             (r + 1u) * loop + i);
            }
        }
        for (unsigned int i = 0; i < loop; i++) {
            const unsigned int i1 = (i + 1u) % loop;
            add_triangle(solver, rings * loop + i, rings * loop + i1, apex);
            // Le fond, oriente a l'envers pour que l'enveloppe soit fermee.
            add_triangle(solver, i1, i, floor_point);
        }

        solver.pressure.rest_volume = 4.0 * area * puff_relative * extent;
        solver.pressure.target = pressure;
        solver.pressure.compliance = 0.0;
        solver.pressure.lambda = 0.0;
        solver.enable_pressure(true);

        solver.solve(iterations, vscale(gravity, 0.0005 * extent));

        // --- versement dans le maillage de sortie --------------------------
        const unsigned int first = out_vertices.get_count();
        for (unsigned int r = 0; r <= rings; r++) {
            for (unsigned int i = 0; i < loop; i++) {
                out_vertices.add(solver.positions[r * loop + i]);
                const double radial = (r == 0u)
                    ? 1.0
                    : (1.0 - seam) * (1.0 - double(r - 1u) / double(rings - 1u));
                const double a = 2.0 * M_PI * double(i) / double(loop);
                out_uvs.add(GMathVec3f(float(0.5 + 0.5 * radial * cos(a)),
                                       float(0.5 + 0.5 * radial * sin(a)),
                                       0.0f));
            }
        }
        const unsigned int out_apex = out_vertices.get_count();
        out_vertices.add(solver.positions[apex]);
        out_uvs.add(GMathVec3f(0.5f, 0.5f, 0.0f));

        for (unsigned int r = 0; r < rings; r++) {
            for (unsigned int i = 0; i < loop; i++) {
                const unsigned int i1 = (i + 1u) % loop;
                out_indices.add(first + r * loop + i);
                out_indices.add(first + r * loop + i1);
                out_indices.add(first + (r + 1u) * loop + i1);
                out_indices.add(first + (r + 1u) * loop + i);
                out_sizes.add(4u);
            }
        }
        for (unsigned int i = 0; i < loop; i++) {
            const unsigned int i1 = (i + 1u) % loop;
            out_indices.add(first + rings * loop + i);
            out_indices.add(first + rings * loop + i1);
            out_indices.add(out_apex);
            out_sizes.add(3u);
        }
    }

    static void
    add_distance(cloth::Solver& solver, const unsigned int& a,
                 const unsigned int& b, const double& give,
                 const double& compliance)
    {
        cloth::Distance d;
        d.a = a;
        d.b = b;
        d.rest = vsub(solver.positions[a], solver.positions[b]).get_length() * give;
        d.compliance = compliance;
        d.lambda = 0.0;
        solver.distances.add(d);
    }

    static void
    add_triangle(cloth::Solver& solver, const unsigned int& a,
                 const unsigned int& b, const unsigned int& c)
    {
        solver.pressure.triangles.add(a);
        solver.pressure.triangles.add(b);
        solver.pressure.triangles.add(c);
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
