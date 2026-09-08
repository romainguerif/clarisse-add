// Balayer un paquet de polylignes en un seul maillage.
//
// Trois nodes en ont besoin -- la toile, l'etirage de nappe, et bientot le
// voile -- et tous les trois ont le meme probleme : des milliers de fils de
// longueurs tres differentes, chacun avec son epaisseur et sa matiere. Le
// balayage du tube ne sert pas ici : il suppose un fil unique dont on choisit
// tout.
//
// Deux details qui ont coute du temps ailleurs et qu'on garde ici :
//
//   - **on compte avant d'allouer.** Les fils n'ont pas tous le meme nombre de
//     points -- une spirale en a des centaines la ou un fil d'amarrage en a
//     six -- donc une allocation par fil, ou une allocation au plus grand,
//     serait absurde. Une passe de comptage, une allocation, une passe
//     d'ecriture.
//
//   - **le rayon peut varier le long du fil.** C'est ce qui permet les
//     gouttelettes de glu sur une soie et les surepaisseurs d'un brin etire --
//     deux phenomenes differents, une seule mecanique. Un profil vide veut dire
//     rayon constant.

#ifndef CLARISSE_ADD_STRAND_SWEEP_H
#define CLARISSE_ADD_STRAND_SWEEP_H

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>
#include <gmath_vec3.h>
#include <poly_mesh.h>

#include <curve_core.h>

namespace strand {

// Un fil a balayer.
struct Strand {
    CoreVector<GMathVec3d> points;
    // Facteur de rayon point par point. Vide, ou de taille differente de
    // `points`, vaut rayon constant.
    CoreVector<double> profile;
    unsigned int group;
    double radius;

    Strand() : group(0u), radius(0.01) {}
};

// Rend zero si rien n'est balayable -- aucun fil de deux points au moins.
inline PolyMesh *
sweep(const CoreVector<Strand>& strands, const CoreArray<CoreString>& groups,
      unsigned int sides)
{
    using namespace curve_core;

    if (sides < 3u) sides = 3u;

    // Passe de comptage. On garde les chemins construits : les reconstruire a
    // l'ecriture couterait autant que le balayage lui-meme.
    CoreVector<Path> paths;
    CoreVector<unsigned int> owner;

    unsigned int total_vertices = 0u;
    unsigned int total_quads = 0u;

    for (unsigned int i = 0; i < strands.get_count(); i++) {
        if (strands[i].points.get_count() < 2u) continue;
        Path path;
        if (!path.build(strands[i].points, false, 1u)) continue;
        const unsigned int rings = path.get_sample_count();
        if (rings < 2u) continue;

        paths.add(path);
        owner.add(i);
        total_vertices += rings * sides;
        total_quads += (rings - 1u) * sides;
    }
    if (total_quads == 0u) return 0;

    const unsigned int uv_cols = sides + 1u;
    unsigned int total_uvs = 0u;
    for (unsigned int k = 0; k < paths.get_count(); k++) {
        total_uvs += paths[k].get_sample_count() * uv_cols;
    }

    CoreArray<GMathVec3f> vertices(total_vertices);
    CoreArray<GMathVec3f> velocities;
    CoreArray<unsigned int> polygon_indices(total_quads * 4u);
    CoreArray<unsigned int> polygon_vertex_count(total_quads);
    CoreArray<unsigned int> polygon_shading_groups(total_quads);
    CoreArray<GMathVec3f> uvs(total_uvs);
    CoreArray<unsigned int> uv_indices(total_quads * 4u);

    unsigned int base = 0u;
    unsigned int uv_base = 0u;
    unsigned int corner = 0u;
    unsigned int face = 0u;

    for (unsigned int k = 0; k < paths.get_count(); k++) {
        const Path& path = paths[k];
        const Strand& fil = strands[owner[k]];
        const unsigned int rings = path.get_sample_count();
        const bool shaped = fil.profile.get_count() == rings;

        for (unsigned int r = 0; r < rings; r++) {
            const GMathVec3d& centre = path.get_position(r);
            const GMathVec3d& normal = path.get_normal(r);
            const GMathVec3d binormal = path.get_binormal(r);
            const double here = shaped ? (fil.radius * fil.profile[r])
                                       : fil.radius;

            for (unsigned int s = 0; s < sides; s++) {
                const double angle = 2.0 * M_PI * double(s) / double(sides);
                const GMathVec3d p =
                    vadd(centre, vadd(vscale(normal, cos(angle) * here),
                                      vscale(binormal, sin(angle) * here)));
                vertices[base + r * sides + s] =
                    GMathVec3f(float(p[0]), float(p[1]), float(p[2]));
            }

            const float v = float(r) / float(rings - 1u);
            for (unsigned int s = 0; s < uv_cols; s++) {
                uvs[uv_base + r * uv_cols + s] =
                    GMathVec3f(float(s) / float(sides), v, 0.0f);
            }
        }

        for (unsigned int r = 0; r + 1u < rings; r++) {
            for (unsigned int s = 0; s < sides; s++) {
                const unsigned int s1 = (s + 1u) % sides;
                polygon_indices[corner + 0u] = base + r * sides + s;
                polygon_indices[corner + 1u] = base + r * sides + s1;
                polygon_indices[corner + 2u] = base + (r + 1u) * sides + s1;
                polygon_indices[corner + 3u] = base + (r + 1u) * sides + s;

                uv_indices[corner + 0u] = uv_base + r * uv_cols + s;
                uv_indices[corner + 1u] = uv_base + r * uv_cols + s + 1u;
                uv_indices[corner + 2u] = uv_base + (r + 1u) * uv_cols + s + 1u;
                uv_indices[corner + 3u] = uv_base + (r + 1u) * uv_cols + s;

                polygon_vertex_count[face] = 4u;
                polygon_shading_groups[face] = fil.group;
                face++;
                corner += 4u;
            }
        }

        base += rings * sides;
        uv_base += rings * uv_cols;
    }

    CoreArray<GeometryUvMap> uv_maps(1);
    uv_maps[0].name = "uv";
    uv_maps[0].vertices = uvs;
    uv_maps[0].polygon_indices = uv_indices;

    CoreArray<GeometryNormalMap> normal_maps;
    CoreArray<GeometryColorMap> color_maps;

    PolyMesh *mesh = new PolyMesh;
    mesh->set(vertices, velocities,
              polygon_indices, polygon_vertex_count, polygon_shading_groups,
              groups, uv_maps, normal_maps, color_maps, true, 0);
    return mesh;
}

} // namespace strand

#endif
