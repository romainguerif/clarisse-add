// Un tube, ou une corde, balaye le long d'une courbe passant par des points de
// controle.
//
// Clarisse sait deja tout faire avec les courbes -- les tesseler, les rendre,
// les shader -- sous le nom de "fur". Ce qui lui manque, c'est de pouvoir en
// creer une. Et le chemin natif ne convient pas ici : GeometryFur n'expose
// aucun attribut de displacement, et un CurveMesh renvoie une bbox nulle et une
// aire de primitive nulle, donc le scatterer n'a rien a quoi s'accrocher.
//
// D'ou ce module : il produit un vrai maillage polygonal. A partir de la, le
// displacement, la subdivision, le scatter, les UV et l'export marchent sans
// qu'on ait une ligne a ecrire pour eux.
//
// Le mode corde est le meme balayage, applique N fois : chaque toron suit une
// helice autour de l'axe. Sa section reste prise dans le repere de la courbe
// plutot que dans celui, incline, du toron -- l'ellipse que ca produit est
// invisible aux torsions usuelles, et ca evite de recalculer un repere par
// toron et par anneau.
//
// Les UV sont face-varying : la couture du tube demande qu'un meme sommet porte
// u=0 d'un cote et u=1 de l'autre, ce qui est impossible en vertex-varying. La
// grille UV a donc une colonne de plus que de cotes.
//
// Le reste du calcul de courbe -- Catmull-Rom, repere a torsion minimale,
// longueurs d'arc -- vit dans common/curve_core.h, partage avec les autres
// nodes de courbe.

#include <dso_export.h>
#include <of_app.h>
#include <of_object.h>
#include <of_object_factory.h>
#include <of_context.h>
#include <of_item.h>
#include <of_item_handle.h>

#include <app_object.h>
#include <app_selection.h>
#include <of_attr.h>
#include <of_class.h>
#include <of_action.h>

#include <module_geometry.h>
#include <module_polymesh.h>

#include <poly_mesh.h>
#include <geometry_object.h>

#include <core_array.h>
#include <core_string.h>
#include <core_vector.h>

#include <cstdio>
#include <gmath_vec3.h>

#include <curve_core.h>

#include <tube.cma>

using namespace curve_core;

class TubeModule : public ModulePolymesh {
public:
    TubeModule() : ModulePolymesh() {}

protected:
    // Declarer la ressource et ce dont elle depend. Ni DIRTINESS_GEOMETRY dans
    // on_attribute_change, ni output "geometry" dans le CID ne suffisent a
    // faire rappeler create_resource : il faut dire explicitement quels
    // attributs salissent quelle ressource.
    void
    module_constructor(OfObject& object) override
    {
        ModulePolymesh::module_constructor(object);

        static const char *const names[] = {
            "noise_amplitude", "noise_frequency", "noise_octaves",
            "noise_roughness", "noise_seed", "noise_fade", "noise_gravity_bias",
            "points_geometry", "points_order", "point", "bend", "closed", "steps", "radius", "radius_profile",
            "sides", "cap", "strands", "twist", "strand_gap", "strand_noise", "strand_sag", "strand_spread",
            "strand_spread_frequency", "wrap_radius", "wrap_turns",
            "wrap_variation", "wrap_variation_frequency", "wrap_sag", "wrap_seed",
            "strand_noise_frequency", "interpolation", "bend_radius", "slack", "gravity", "trim_start", "trim_end"
        };
        const unsigned int name_count = sizeof(names) / sizeof(names[0]);

        // Deux passes, et surtout pas de resize : CoreArray::resize(n) fait un
        // delete[] suivi d'un new[], il ne preserve rien. Et le constructeur
        // par defaut de OfAttrDirtiness laisse son pointeur non initialise --
        // donc un tableau redimensionne apres coup part en violation d'acces
        // dans set_resource_attrs, loin de sa cause.
        unsigned int count = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            if (object.get_attribute(names[i]) != 0) count++;
        }
        if (count == 0) return;

        CoreArray<OfAttrDirtiness> attrs(count);
        unsigned int k = 0;
        for (unsigned int i = 0; i < name_count; i++) {
            OfAttr *attr = object.get_attribute(names[i]);
            if (attr != 0) {
                // DIRTINESS_ALL, et pas seulement DIRTINESS_GEOMETRY : quand un
                // locator reference bouge, ce qui remonte est DIRTINESS_MOTION.
                // Filtrer sur la geometrie seule laissait le tube en retard d'un
                // changement -- il ne se reconstruisait qu'a la modification
                // suivante, en relisant au passage la nouvelle position.
                attrs[k++] = OfAttrDirtiness(attr, OfAttr::DIRTINESS_ALL);
            }
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
    PolyMesh *
    build_mesh() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        const bool closed = object->get_attribute("closed")->get_bool();
        const bool cap = object->get_attribute("cap")->get_bool() && !closed;
        const OfAttr *profile = object->get_attribute("radius_profile");
        const double radius = object->get_attribute("radius")->get_double();
        const double twist = object->get_attribute("twist")->get_double();

        unsigned int sides = (unsigned int) object->get_attribute("sides")->get_long();
        unsigned int strands = (unsigned int) object->get_attribute("strands")->get_long();
        if (sides < 3u) sides = 3u;
        if (strands < 1u) strands = 1u;

        Path path;
        if (!build_from_object(*object, path)) return 0;

        const unsigned int rings = path.get_sample_count();
        if (rings < 2u) return 0;

        // Raccourcir la gaine aux deux bouts. C'est ce qui permet a un
        // connecteur pose a l'extremite de recouvrir la fin du cable au lieu
        // d'etre traverse par elle -- en gros plan, c'est la difference entre
        // un raccord et un tube qui sort d'un bouchon.
        const OfAttr *trim_start_attr = object->get_attribute("trim_start");
        const OfAttr *trim_end_attr = object->get_attribute("trim_end");
        const double trim_start = (trim_start_attr != 0)
                                ? trim_start_attr->get_double() : 0.0;
        const double trim_end = (trim_end_attr != 0)
                              ? trim_end_attr->get_double() : 0.0;
        const double full_length = path.get_length();
        double trimmed = full_length - trim_start - trim_end;
        if (trimmed < 1e-6) trimmed = 1e-6;
        // Rechantillonner change le placement des anneaux : on ne le fait donc
        // que si un trim est reellement demande, pour que le mode Coudes garde
        // ses anneaux exactement sur les points de tangence des arcs.
        const bool trimming = (trim_start > 1e-9 || trim_end > 1e-9)
                            && full_length > 1e-9 && !closed;

        // Un toron unique occupe tout le rayon ; a partir de deux, ils se
        // rangent sur un cercle et leur rayon propre est celui qui les fait
        // s'effleurer sans se penetrer.
        const double strand_spread =
            object->get_attribute("strand_spread")->get_double();
        const double strand_spread_freq =
            object->get_attribute("strand_spread_frequency")->get_double();
        const double strand_sag = object->get_attribute("strand_sag")->get_double();
        const double strand_gap = object->get_attribute("strand_gap")->get_double();
        const double strand_noise = object->get_attribute("strand_noise")->get_double();
        const double strand_freq =
            object->get_attribute("strand_noise_frequency")->get_double();

        double strand_radius = radius;
        double strand_offset = 0.0;
        double strand_margin = 0.0;
        if (strands > 1u) {
            const double s = sin(M_PI / double(strands));
            const double tight = radius * s / (1.0 + s);
            strand_offset = radius - tight;
            // Le jeu fait maigrir les torons sans deplacer leurs centres. La
            // marge liberee est exactement ce dont chacun dispose pour onduler :
            // si deux voisins viennent l'un vers l'autre de cette marge, ils
            // s'effleurent sans se penetrer. La garantie est geometrique, il n'y
            // a aucune detection de collision a faire.
            strand_radius = tight * (1.0 - strand_gap);
            strand_margin = tight * strand_gap;
        }

        const unsigned int quad_rings = closed ? rings : rings - 1u;
        const unsigned int vertex_count = strands * rings * sides;
        const unsigned int quad_count = strands * quad_rings * sides;
        const unsigned int cap_count = cap ? 2u * strands : 0u;
        const unsigned int polygon_count = quad_count + cap_count;
        const unsigned int index_count = quad_count * 4u + cap_count * sides;

        CoreArray<GMathVec3f> vertices(vertex_count);
        CoreArray<GMathVec3f> velocities;          // pas de motion blur geometrique
        CoreArray<unsigned int> polygon_indices(index_count);
        CoreArray<unsigned int> polygon_vertex_count(polygon_count);
        CoreArray<unsigned int> polygon_shading_groups(polygon_count);
        CoreArray<CoreString> shading_group_names(1);
        shading_group_names[0] = "tube";

        const unsigned int uv_rows = quad_rings + 1u;
        const unsigned int uv_cols = sides + 1u;
        const unsigned int uv_grid = strands * uv_rows * uv_cols;
        CoreArray<GMathVec3f> uvs(uv_grid + (cap ? 2u * strands * sides : 0u));
        CoreArray<unsigned int> uv_indices(index_count);

        const double total = path.get_length();

        const OfAttr *gravity_attr = object->get_attribute("gravity");
        GMathVec3d down(0.0, -1.0, 0.0);
        if (gravity_attr != 0) {
            down = vnorm(GMathVec3d(gravity_attr->get_double(0),
                                    gravity_attr->get_double(1),
                                    gravity_attr->get_double(2)));
        }

        // --- sommets -----------------------------------------------------
        for (unsigned int r = 0; r < rings; r++) {
            const double t = (rings > 1u) ? double(r) / double(rings - 1u) : 0.0;
            double scale = 1.0;
            if (profile != 0) scale = profile->get_curve_double(t);

            GMathVec3d centre, tangent_at, n;
            double arc_here;
            if (trimming) {
                arc_here = trim_start + trimmed * t;
                path.eval_at_distance(arc_here, centre, tangent_at, n);
            } else {
                centre = path.get_position(r);
                tangent_at = path.get_tangent(r);
                n = path.get_normal(r);
                arc_here = path.get_arc_length(r);
            }
            const GMathVec3d b = vnorm(vcross(tangent_at, n));

            // La torsion se compte en tours par unite de longueur reelle, pas
            // par parametre : sinon le pas de la corde se resserrerait dans les
            // virages, ou la courbe est echantillonnee plus dense au metre.
            const double helix = 2.0 * M_PI * twist * arc_here;

            for (unsigned int strand = 0; strand < strands; strand++) {
                const double phase = helix
                                   + 2.0 * M_PI * double(strand) / double(strands);
                // L'ecartement ne descend jamais sous la position serree : le
                // demi-cosinus reste dans [0, 1], donc les torons s'ouvrent ou
                // restent joints, mais ne se rapprochent jamais au point de se
                // penetrer. C'est ce qui laisse la garantie du jeu intacte.
                double spread = 1.0;
                if (strands > 1u && strand_spread > 1e-9) {
                    const double pinch =
                        0.5 - 0.5 * cos(2.0 * M_PI * arc_here * strand_spread_freq);
                    spread = 1.0 + strand_spread * pinch;
                }
                const double offset_here = strand_offset * scale * spread;

                double along_n = cos(phase) * offset_here;
                double along_b = sin(phase) * offset_here;

                // L'affaissement propre au toron et son ondulation puisent
                // dans le meme budget : la marge que le jeu a liberee. Les
                // sommer avant de borner est ce qui garde la garantie de
                // non-penetration vraie quels que soient les deux reglages --
                // les borner separement laisserait leur somme la depasser.
                double extra_n = 0.0;
                double extra_b = 0.0;

                if (strands > 1u && strand_sag > 1e-9) {
                    // Nul aux deux bouts, maximal au milieu : un brin tenu a
                    // ses extremites ne pend pas la ou on le tient.
                    const double bell = (total > 1e-9)
                                      ? sin(M_PI * (arc_here / total)) : 0.0;
                    // Chaque toron a sa propre amplitude, mais toujours la
                    // meme : le faisceau ne doit pas changer d'aspect entre
                    // deux evaluations.
                    const double share =
                        0.5 + 0.5 * perlin(double(strand) * 7.3, 0.0, 0.0, 4241u);
                    const double amount = strand_sag * bell * share;

                    // La gravite projetee dans le plan de la section : c'est la
                    // seule direction ou le toron peut tomber sans changer la
                    // longueur du cable.
                    const GMathVec3d flat =
                        vnorm(vsub(down, vscale(tangent_at, vdot(down, tangent_at))));
                    extra_n += amount * vdot(flat, n);
                    extra_b += amount * vdot(flat, b);
                }

                if (strands > 1u && strand_noise > 1e-9 && strand_margin > 1e-12) {
                    // Un bruit par toron, decale par sa graine, evalue le long
                    // de la courbe. On borne la NORME du deplacement et non
                    // chaque composante : borner separement laisserait passer
                    // un facteur racine de deux dans les diagonales, et c'est
                    // justement la que deux torons voisins se rejoignent.
                    const double u = arc_here * strand_freq;
                    const double wn = perlin(u, 0.0, double(strand) * 13.7, 7717u);
                    const double wb = perlin(u, 5.5, double(strand) * 13.7, 9973u);
                    const double magnitude = sqrt(wn * wn + wb * wb);
                    const double allowed = strand_margin * strand_noise * scale;
                    const double factor = (magnitude > 1e-12)
                                        ? allowed / ((magnitude > 1.0) ? magnitude : 1.0)
                                        : 0.0;
                    extra_n += wn * factor;
                    extra_b += wb * factor;
                }

                if (strands > 1u && strand_margin > 1e-12) {
                    const double magnitude = sqrt(extra_n * extra_n
                                                  + extra_b * extra_b);
                    if (magnitude > strand_margin * scale) {
                        const double clamp = strand_margin * scale / magnitude;
                        extra_n *= clamp;
                        extra_b *= clamp;
                    }
                }
                along_n += extra_n;
                along_b += extra_b;

                const GMathVec3d axis =
                    (strands > 1u)
                    ? vadd(centre, vadd(vscale(n, along_n), vscale(b, along_b)))
                    : centre;

                for (unsigned int s = 0; s < sides; s++) {
                    const double a = 2.0 * M_PI * double(s) / double(sides);
                    const double rr = strand_radius * scale;
                    const GMathVec3d p = vadd(axis,
                                              vadd(vscale(n, cos(a) * rr),
                                                   vscale(b, sin(a) * rr)));
                    vertices[(strand * rings + r) * sides + s] =
                        GMathVec3f(float(p[0]), float(p[1]), float(p[2]));
                }
            }
        }

        // --- UV ------------------------------------------------------------
        for (unsigned int strand = 0; strand < strands; strand++) {
            for (unsigned int r = 0; r < uv_rows; r++) {
                const float v = float(r) / float(uv_rows - 1u);
                for (unsigned int s = 0; s < uv_cols; s++) {
                    uvs[(strand * uv_rows + r) * uv_cols + s] =
                        GMathVec3f(float(s) / float(sides), v, 0.0f);
                }
            }
        }

        // --- topologie ------------------------------------------------------
        unsigned int k = 0;
        unsigned int face = 0;
        for (unsigned int strand = 0; strand < strands; strand++) {
            const unsigned int base = strand * rings * sides;
            const unsigned int uv_base = strand * uv_rows * uv_cols;

            for (unsigned int r = 0; r < quad_rings; r++) {
                const unsigned int r1 = (r + 1u) % rings;
                for (unsigned int s = 0; s < sides; s++) {
                    const unsigned int s1 = (s + 1u) % sides;

                    // Ordre choisi pour que la normale sorte du tube : on tourne
                    // d'abord autour de l'axe, on avance ensuite.
                    polygon_indices[k + 0u] = base + r * sides + s;
                    polygon_indices[k + 1u] = base + r * sides + s1;
                    polygon_indices[k + 2u] = base + r1 * sides + s1;
                    polygon_indices[k + 3u] = base + r1 * sides + s;

                    uv_indices[k + 0u] = uv_base + r * uv_cols + s;
                    uv_indices[k + 1u] = uv_base + r * uv_cols + s + 1u;
                    uv_indices[k + 2u] = uv_base + (r + 1u) * uv_cols + s + 1u;
                    uv_indices[k + 3u] = uv_base + (r + 1u) * uv_cols + s;

                    polygon_vertex_count[face] = 4u;
                    polygon_shading_groups[face] = 0u;
                    face++;
                    k += 4u;
                }
            }
        }

        if (cap) {
            for (unsigned int strand = 0; strand < strands; strand++) {
                const unsigned int base = strand * rings * sides;
                const unsigned int uv_cap = uv_grid + 2u * strand * sides;

                for (unsigned int s = 0; s < sides; s++) {
                    const double a = 2.0 * M_PI * double(s) / double(sides);
                    uvs[uv_cap + s] = GMathVec3f(float(0.5 + 0.5 * cos(a)),
                                                 float(0.5 + 0.5 * sin(a)), 0.0f);
                    uvs[uv_cap + sides + s] = uvs[uv_cap + s];
                }

                // Le disque du depart tourne a l'envers de celui de la fin :
                // les deux normales doivent pointer vers l'exterieur.
                for (unsigned int s = 0; s < sides; s++) {
                    polygon_indices[k + s] = base + (sides - 1u - s);
                    uv_indices[k + s] = uv_cap + (sides - 1u - s);
                }
                polygon_vertex_count[face] = sides;
                polygon_shading_groups[face] = 0u;
                face++;
                k += sides;

                const unsigned int last = base + (rings - 1u) * sides;
                for (unsigned int s = 0; s < sides; s++) {
                    polygon_indices[k + s] = last + s;
                    uv_indices[k + s] = uv_cap + sides + s;
                }
                polygon_vertex_count[face] = sides;
                polygon_shading_groups[face] = 0u;
                face++;
                k += sides;
            }
        }

        // --- assemblage -----------------------------------------------------
        CoreArray<GeometryUvMap> uv_maps(1);
        uv_maps[0].name = "uv";
        uv_maps[0].vertices = uvs;
        uv_maps[0].polygon_indices = uv_indices;

        CoreArray<GeometryNormalMap> normal_maps;  // vide : PolyMesh les calcule
        CoreArray<GeometryColorMap> color_maps;

        PolyMesh *mesh = new PolyMesh;
        mesh->set(vertices, velocities,
                  polygon_indices, polygon_vertex_count, polygon_shading_groups,
                  shading_group_names,
                  uv_maps, normal_maps, color_maps,
                  true, 0);
        return mesh;
    }
};

// Les deux boutons de l'attribute editor. cmagen fabrique leurs declarations
// depuis le CID et les branche dans declare_class ; il ne reste qu'a les
// definir, sous le nom qu'il a choisi : on_<attribut>_<attribut>_action.

// Le nom d'objet est arbitre par add_object, qui refuse un nom deja pris : le
// contexte n'expose ni recherche par nom ni generateur de nom unique public.
static OfObject *
add_unique(OfContext& context, const char *base, const char *class_name)
{
    CoreString name(base);
    for (unsigned int i = 1; i < 100000u; i++) {
        OfObject *created = context.add_object(name, class_name);
        if (created != 0) return created;
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "%s%u", base, i);
        name = CoreString(buffer);
    }
    return 0;
}

static int
on_add_point_add_point_action(const OfAction& action, OfObject& object, void *data)
{
    OfAttr *points = object.get_attribute("point");
    if (points == 0) return 0;

    // Le nouveau point prolonge le dernier segment : c'est le geste attendu
    // quand on clique « ajouter » plusieurs fois de suite, et ca evite de poser
    // deux points au meme endroit, ou la courbe serait degeneree.
    CoreVector<GMathVec3d> existing;
    gather_control_points(object, 0, existing);

    GMathVec3d position(0.0, 0.0, 0.0);
    const unsigned int found = existing.get_count();
    if (found == 1u) {
        position = vadd(existing[0], GMathVec3d(1.0, 0.0, 0.0));
    } else if (found >= 2u) {
        const GMathVec3d last = existing[found - 1u];
        position = vadd(last, vsub(last, existing[found - 2u]));
    }

    OfObject *locator = add_unique(object.get_context(), "point", "Locator");
    if (locator == 0) return 0;

    OfAttr *translate = locator->get_attribute("translate");
    if (translate != 0) {
        translate->set_double(position[0], 0);
        translate->set_double(position[1], 1);
        translate->set_double(position[2], 2);
    }

    const unsigned int count = points->get_value_count();
    points->set_value_count(count + 1u);
    points->set_object(locator, count);

    // La colonne voisine suit la table : sans ca la nouvelle ligne herite d'un
    // rayon de coude indefini au lieu du « comme le global » attendu.
    OfAttr *bend = object.get_attribute("bend");
    if (bend != 0) {
        bend->set_value_count(count + 1u);
        bend->set_double(-1.0, count);
    }
    return 1;
}

static int
on_add_selected_add_selected_action(const OfAction& action, OfObject& object,
                                    void *data)
{
    OfAttr *points = object.get_attribute("point");
    if (points == 0) return 0;

    // AppObject derive de OfApp : la selection vit dans la couche application,
    // pas dans le graphe d'objets, d'ou le passage par la factory puis le cast.
    AppObject *app = CoreBaseObject::cast<AppObject>(
        &object.get_factory().get_application());
    if (app == 0) return 0;

    AppSelection& selection = app->get_selection();
    const unsigned int selected = selection.get_count();
    if (selected == 0u) return 0;

    OfAttr *bend = object.get_attribute("bend");
    unsigned int row = points->get_value_count();

    for (unsigned int i = 0; i < selected; i++) {
        OfItem *item = selection.get_item(i).get_ptr();
        if (item == 0 || !item->is_object()) continue;

        OfObject *candidate = item->to_object();
        // Se prendre soi-meme comme point de controle ferait une dependance
        // circulaire, et Clarisse ne la signale pas toujours proprement.
        if (candidate == 0 || candidate == &object) continue;

        points->set_value_count(row + 1u);
        points->set_object(candidate, row);
        if (bend != 0) {
            bend->set_value_count(row + 1u);
            bend->set_double(-1.0, row);
        }
        row++;
    }
    return 1;
}

static int
on_draw_points_draw_points_action(const OfAction& action, OfObject& object, void *data)
{
    // Le SDK reconstruit n'expose aucun moyen d'activer un outil depuis du code
    // -- rien de tel qu'un set_current_tool dans gui/ ni dans clarisse_app/.
    // Les contraintes natives y arrivent (« Invoke Translate Tool »), donc le
    // mecanisme existe, il est juste hors de ce qu'on sait atteindre. En
    // attendant de le trouver, le bouton fait tout le reste : il cree la plume
    // si besoin et la braque sur cette courbe, de sorte qu'il ne reste qu'a la
    // choisir dans la barre d'outils.
    OfContext& context = object.get_context();

    // Le contexte s'enumere par items, pas par objets : get_object_count donne
    // bien un compte, mais l'accesseur correspondant est get_item, qui rend un
    // OfItem a convertir.
    OfObject *pen = 0;
    for (unsigned int i = 0; i < context.get_object_count(); i++) {
        OfItem *item = context.get_item(i);
        if (item == 0 || !item->is_object()) continue;
        OfObject *candidate = item->to_object();
        if (candidate != 0 && candidate->get_class().get_name() == "ToolCurvePen") {
            pen = candidate;
            break;
        }
    }
    if (pen == 0) pen = add_unique(context, "curve_pen", "ToolCurvePen");
    if (pen == 0) return 0;

    OfAttr *target = pen->get_attribute("target");
    if (target != 0) target->set_object(&object);
    return 1;
}

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometryTube, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometryTube)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometryTube);
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
    // set_object avant tout : Clarisse interroge le module des que l'objet
    // entre dans le contexte, et dereference m_object sans le tester.
    TubeModule *module = new TubeModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
