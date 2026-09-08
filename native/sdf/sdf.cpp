// Une forme definie par un champ de distance, et non par des polygones.
//
// On empile des primitives et on les combine : ajouter, soustraire,
// intersecter, avec un raccord arrondi reglable a chaque operation. Rien n'est
// jamais tessele. Clarisse ne recoit ni sommets ni faces, seulement un objet
// capable de repondre « ce rayon touche la surface ici » -- il lui construit son
// BVH, l'ombre, et le Scatterer l'instancie comme n'importe quelle geometrie.
//
// Trois choses valent d'etre dites, parce qu'elles ne se devinent pas.
//
// **Les booleens y sont exacts et sans cas particulier.** Ni face coplanaire,
// ni arete degeneree, ni trou : on ne decoupe aucun maillage, on compare des
// distances. Le probleme qui a coute six ans a Blender n'existe simplement pas
// dans cette representation. En echange, il ne s'applique qu'a ce qu'on sait
// decrire par une formule -- pas a un maillage importe.
//
// **Le raccord arrondi n'est pas un booleen.** Un booleen dur se resout par
// intervalles : ou le rayon entre dans A, ou il en sort, et on combine. Un
// raccord, lui, n'est ni sur A ni sur B, il nait entre les deux -- il n'y a plus
// d'intervalle a combiner, il faut avancer a tatons le long du rayon. C'est le
// sphere tracing : la distance au point ou l'on est donne la longueur qu'on peut
// franchir sans rien toucher, on l'avance en entier, et on recommence.
//
// **On avance du pas plein.** La distance est une borne inferieure garantie :
// avancer moins ne rend rien plus sur, ca epuise seulement le budget de pas
// avant d'arriver, et les rayons rasants disparaissent alors par plaques. La
// mesure est dans docs/sdf-clarisse.md, et elle est contre-intuitive au point
// qu'il faut l'ecrire ici.
//
// La position de chaque forme vient d'un item de la scene -- un locator, en
// general. C'est deliberé : on le deplace avec l'outil de transformation
// habituel de Clarisse, et la forme suit. Il n'y a donc pas de manipulateur a
// inventer, ce qui est la partie la plus couteuse d'un outil de modelage.

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
#include <gmath_matrix4x4.h>
#include <gmath_bbox3.h>
#include <gmath_ray.h>

#include <cmath>
#include <vector>

#include <sdf.cma>

// Le corps de ce constructeur vit dans un fichier d'implementation perdu a la
// reconstruction du SDK : il faut le fournir. Et l'opacite doit valoir un --
// a zero, un filtre d'intersection jette le fragment et *rien ne s'affiche*,
// sans silhouette, sans alpha, sans message. Ce piege a coute six rendus a la
// sonde CSG ; il est consigne dans docs/sdk-clarisse.md.
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

enum Shape {
    SHAPE_SPHERE = 0, SHAPE_BOX, SHAPE_CYLINDER, SHAPE_CAPSULE,
    SHAPE_CONE, SHAPE_TORUS, SHAPE_PLANE
};

enum Operation { OP_ADD = 0, OP_SUBTRACT, OP_INTERSECT };

// Une forme de la pile, prete a etre evaluee. La matrice va de l'espace du node
// vers celui de la primitive ; `shrink` reconvertit une distance mesuree
// la-dedans en distance du node.
struct Primitive {
    GMathMatrix4x4d to_local;
    double shrink;
    GMathVec3d size;
    double rounding;
    long shape;
    long operation;
    double blend;
};

inline double
clamp01(const double& x)
{
    return (x < 0.0) ? 0.0 : ((x > 1.0) ? 1.0 : x);
}

// Le minimum adouci de Quilez. Sa forme polynomiale a une propriete qui a
// surpris a la mesure : le champ melange est *plus* regulier que le champ dur --
// gradient maximal 1,0005 contre 1,0291 pour un minimum franc. Il sous-estime
// donc la distance, ce qui est exactement le sens qui garde le pas plein sur.
inline double
smooth_min(const double& a, const double& b, const double& k)
{
    if (k <= 1e-12) return (a < b) ? a : b;
    const double h = clamp01(0.5 + 0.5 * (b - a) / k);
    return b * (1.0 - h) + a * h - k * h * (1.0 - h);
}

inline double
smooth_max(const double& a, const double& b, const double& k)
{
    return -smooth_min(-a, -b, k);
}

inline double
length2(const double& x, const double& y)
{
    return sqrt(x * x + y * y);
}

// Les distances exactes des primitives, dans leur propre repere. Toutes sont
// publiees et verifiees ; aucune n'est une approximation.
double
primitive_distance(const Primitive& prim, const GMathVec3d& p)
{
    const GMathVec3d& s = prim.size;

    switch (prim.shape) {
        case SHAPE_BOX: {
            const double qx = fabs(p[0]) - s[0];
            const double qy = fabs(p[1]) - s[1];
            const double qz = fabs(p[2]) - s[2];
            const double ax = (qx > 0.0) ? qx : 0.0;
            const double ay = (qy > 0.0) ? qy : 0.0;
            const double az = (qz > 0.0) ? qz : 0.0;
            double inside = (qx > qy) ? qx : qy;
            if (qz > inside) inside = qz;
            if (inside > 0.0) inside = 0.0;
            return sqrt(ax * ax + ay * ay + az * az) + inside;
        }
        case SHAPE_CYLINDER: {
            const double dx = length2(p[0], p[2]) - s[0];
            const double dy = fabs(p[1]) - s[1];
            const double ax = (dx > 0.0) ? dx : 0.0;
            const double ay = (dy > 0.0) ? dy : 0.0;
            double inside = (dx > dy) ? dx : dy;
            if (inside > 0.0) inside = 0.0;
            return inside + sqrt(ax * ax + ay * ay);
        }
        case SHAPE_CAPSULE: {
            double y = p[1];
            if (y < -s[1]) y = -s[1];
            else if (y > s[1]) y = s[1];
            const double dy = p[1] - y;
            return sqrt(p[0] * p[0] + dy * dy + p[2] * p[2]) - s[0];
        }
        case SHAPE_CONE: {
            // Cone tronque de Quilez : rayon de base, demi-hauteur, rayon de
            // sommet. Un sommet nul donne le cone plein.
            const double h = s[1];
            const double r1 = s[0];
            const double r2 = s[2];
            const double qx = length2(p[0], p[2]);
            const double qy = p[1];

            const double cax = qx - ((qx < ((qy < 0.0) ? r1 : r2))
                                     ? qx : ((qy < 0.0) ? r1 : r2));
            const double cay = fabs(qy) - h;

            const double k2x = r2 - r1;
            const double k2y = 2.0 * h;
            const double dot2k2 = k2x * k2x + k2y * k2y;
            const double t = (dot2k2 > 1e-18)
                ? clamp01(((r2 - qx) * k2x + (h - qy) * k2y) / dot2k2) : 0.0;
            const double cbx = qx - r2 + k2x * t;
            const double cby = qy - h + k2y * t;

            const double sign = (cbx < 0.0 && cay < 0.0) ? -1.0 : 1.0;
            const double da = cax * cax + cay * cay;
            const double db = cbx * cbx + cby * cby;
            return sign * sqrt((da < db) ? da : db);
        }
        case SHAPE_TORUS: {
            const double q = length2(p[0], p[2]) - s[0];
            return length2(q, p[1]) - s[1];
        }
        case SHAPE_PLANE:
            return p[1];
        default:
            break;
    }
    return sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - s[0];
}

// L'intersection d'un rayon avec une boite, par la methode des dalles. Elle
// borne le parcours : sans elle on tatonnerait depuis la camera jusqu'a l'objet.
bool
slab(const GMathBbox3d& box, const GMathVec3d& o, const GMathVec3d& d,
     double& tmin, double& tmax)
{
    tmin = -1e300;
    tmax = 1e300;
    const GMathVec3d lo = box.get_min();
    const GMathVec3d hi = box.get_max();
    for (unsigned int i = 0; i < 3; i++) {
        if (fabs(d[i]) < 1e-15) {
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
            continue;
        }
        const double inv = 1.0 / d[i];
        double t0 = (lo[i] - o[i]) * inv;
        double t1 = (hi[i] - o[i]) * inv;
        if (t0 > t1) { const double swap = t0; t0 = t1; t1 = swap; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
        if (tmin > tmax) return false;
    }
    return true;
}

} // namespace

class SdfObject : public GeometryObject {
public:
    SdfObject(const std::vector<Primitive>& primitives, const GMathBbox3d& bbox,
              const long& max_steps, const double& step_scale,
              const double& precision, const long& uv_mode,
              const double& uv_scale)
        : m_primitives(primitives), m_bbox(bbox), m_max_steps(max_steps),
          m_step(step_scale), m_precision(precision), m_uv_mode(uv_mode),
          m_uv_scale(uv_scale)
    {
        m_shading_groups.resize(1);
        m_shading_groups[0] = CoreString("sdf");
    }

    GeometryObject *
    get_copy() const override
    {
        return new SdfObject(m_primitives, m_bbox, m_max_steps, m_step,
                             m_precision, m_uv_mode, m_uv_scale);
    }

    GMathBbox3d get_bbox() const override { return m_bbox; }

    const CoreBasicArray<CoreString>&
    get_shading_group_names() const override { return m_shading_groups; }

    // Une seule primitive Clarisse, boite englobante globale. Le decoupage en
    // une primitive par forme est une optimisation qui se retourne : le raccord
    // arrondi elargit les boites, elles se recouvrent, et chaque rayon retrace
    // le champ autant de fois qu'il traverse de boites. Mesure a trois fois le
    // cout dans docs/sdf-clarisse.md.
    unsigned int get_primitive_count() const override { return 1u; }

    unsigned int
    get_primitive_edge_count(const unsigned int&) const override { return 4u; }

    void
    compute_primitive_bbox(const CtxEval&, const unsigned int&,
                           GMathBbox3d& bbox) const override
    {
        bbox = m_bbox;
    }

    unsigned int
    get_primitive_shading_group_index(const unsigned int&) const override
    {
        return 0u;
    }

    void
    intersect_primitive(const CtxEval& eval_ctx, const unsigned int& id,
                        GeometryRaytraceCtx& raytrace_ctx) const override
    {
        const int first = raytrace_ctx.get_first_index();
        const int last = raytrace_ctx.get_last_index();

        for (int i = first; i <= last; i++) {
            const unsigned int index = (unsigned int) i;
            const GMathBasicRay<>& ray = raytrace_ctx.get_local_ray(index);
            const GMathVec3d& o = ray.get_position();
            const GMathVec3d& d = ray.get_direction();

            double t0, t1;
            if (!slab(m_bbox, o, d, t0, t1)) continue;
            if (t0 < 0.0) t0 = 0.0;

            double t = t0;
            bool hit = false;
            for (long step = 0; step < m_max_steps && t <= t1; step++) {
                const GMathVec3d p(o[0] + d[0] * t, o[1] + d[1] * t,
                                   o[2] + d[2] * t);
                const double distance = field(p);

                // Le seuil suit la distance parcourue : un objet vu de loin n'a
                // pas besoin de la finesse du meme objet vu de pres, et un seuil
                // fixe ferait ramer l'un ou trembler l'autre.
                const double epsilon = m_precision * ((t > 1.0) ? t : 1.0);
                if (distance < epsilon) { hit = true; break; }
                t += distance * m_step;
            }

            if (!hit || t > t1) continue;
            if (raytrace_ctx.is_distance_clipped(index, t)) continue;

            const GMathVec3d p(o[0] + d[0] * t, o[1] + d[1] * t,
                               o[2] + d[2] * t);
            const GMathVec3d lo = m_bbox.get_min();
            const GMathVec3d extent = m_bbox.get_max() - lo;
            // Le moteur refuse un (u, v, w) hors de [0, 1] sans un mot, et un
            // point sur la paroi de la boite y tombe a un cheveu pres.
            double u = (extent[0] > 1e-12) ? (p[0] - lo[0]) / extent[0] : 0.0;
            double v = (extent[1] > 1e-12) ? (p[1] - lo[1]) / extent[1] : 0.0;
            double w = (extent[2] > 1e-12) ? (p[2] - lo[2]) / extent[2] : 0.0;
            u = clamp01(u); v = clamp01(v); w = clamp01(w);
            raytrace_ctx.push_intersection(eval_ctx, index, id, u, v, w, t);
        }
    }

    void
    compute_fragment_sample(const CtxEval&, const GeometryFragment& fragment,
                            GeometrySample& sample) const override
    {
        const GMathVec3d uvw = fragment.get_uvw();
        const GMathVec3d lo = m_bbox.get_min();
        const GMathVec3d extent = m_bbox.get_max() - lo;
        const GMathVec3d p(lo[0] + uvw[0] * extent[0],
                           lo[1] + uvw[1] * extent[1],
                           lo[2] + uvw[2] * extent[2]);

        // La normale est le gradient du champ, pris par differences centrees.
        // C'est exact a l'ordre deux et ca ne demande rien d'autre que six
        // evaluations -- il n'y a pas de normale stockee a interpoler.
        const double h = 1e-4;
        GMathVec3d n(
            field(GMathVec3d(p[0] + h, p[1], p[2]))
                - field(GMathVec3d(p[0] - h, p[1], p[2])),
            field(GMathVec3d(p[0], p[1] + h, p[2]))
                - field(GMathVec3d(p[0], p[1] - h, p[2])),
            field(GMathVec3d(p[0], p[1], p[2] + h))
                - field(GMathVec3d(p[0], p[1], p[2] - h)));
        const double length = n.get_length();
        if (length < 1e-12) n = GMathVec3d(0.0, 1.0, 0.0);
        else n /= length;

        // Un repere orthonorme autour de la normale, dont le produit vectoriel
        // rend exactement n (Duff et al. 2017). Il porte aussi les UV : une
        // surface implicite n'a aucun depliage naturel, il n'y a pas de faces a
        // plier, donc on projette.
        const double sign = (n[2] >= 0.0) ? 1.0 : -1.0;
        const double a = -1.0 / (sign + n[2]);
        const double b = n[0] * n[1] * a;
        GMathVec3d dpdu(1.0 + sign * n[0] * n[0] * a, sign * b, -sign * n[0]);
        GMathVec3d dpdv(b, sign + n[1] * n[1] * a, -n[1]);

        const double scale = (m_uv_scale > 1e-9) ? m_uv_scale : 1.0;
        dpdu *= scale;
        dpdv *= scale;

        sample.init_surface(p, dpdu, dpdv);
    }

private:
    // Le champ : on empile, et chaque forme combine avec ce qui precede. La
    // premiere pose la matiere -- son operation n'a pas de sens, il n'y a rien
    // a quoi l'appliquer.
    double
    field(const GMathVec3d& p) const
    {
        const size_t count = m_primitives.size();
        if (count == 0u) return 1e30;

        double d = 0.0;
        for (size_t i = 0; i < count; i++) {
            const Primitive& prim = m_primitives[i];

            GMathVec3d local;
            GMathMatrix4x4d::multiply(local, p, prim.to_local);
            const double di =
                (primitive_distance(prim, local) - prim.rounding) * prim.shrink;

            if (i == 0u) { d = di; continue; }
            switch (prim.operation) {
                case OP_SUBTRACT:
                    d = smooth_max(d, -di, prim.blend);
                    break;
                case OP_INTERSECT:
                    d = smooth_max(d, di, prim.blend);
                    break;
                default:
                    d = smooth_min(d, di, prim.blend);
                    break;
            }
        }
        return d;
    }

    std::vector<Primitive> m_primitives;
    GMathBbox3d m_bbox;
    long m_max_steps;
    double m_step;
    double m_precision;
    long m_uv_mode;
    double m_uv_scale;
    CoreArray<CoreString> m_shading_groups;
};

class SdfModule : public ModuleGeometry {
public:
    SdfModule() : ModuleGeometry() {}

protected:
    void
    module_constructor(OfObject& object) override
    {
        ModuleGeometry::module_constructor(object);

        static const char *const names[] = {
            "item", "shape", "operation", "blend", "size", "rounding", "group",
            "uv_mode", "uv_scale",
            "max_steps", "step_scale", "precision", "padding"
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
            SdfObject *object = build();
            if (object != 0) return object;
        }
        return ModuleGeometry::create_resource(id, data);
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

    // La matrice d'un item, ramenee dans le repere du node. On travaille dans
    // ce repere et non dans celui du monde parce que c'est celui des rayons que
    // Clarisse nous passe.
    static bool
    local_matrix(const OfObject& item, const GMathMatrix4x4d& node_inverse,
                 GMathMatrix4x4d& to_local, double& shrink)
    {
        ModuleSceneItem *module =
            const_cast<OfObject&>(item).get_module<ModuleSceneItem>();
        if (module == 0) return false;

        GMathMatrix4x4d item_inverse;
        if (module->get_global_matrix().get_inverse(item_inverse) == 0.0) {
            return false;
        }

        // node -> monde -> primitive.
        GMathMatrix4x4d node_to_world;
        node_to_world.get_inverse(node_to_world);   // place seulement
        GMathMatrix4x4d::multiply(to_local, node_inverse, item_inverse);

        // Une distance mesuree dans le repere de la primitive vaut, ramenee
        // ici, au moins elle-meme divisee par le plus grand facteur d'echelle.
        // On prend donc ce facteur : sous-estimer est le sens qui garde le pas
        // plein sur, surestimer ferait traverser la surface.
        double biggest = 0.0;
        for (unsigned int axis = 0; axis < 3; axis++) {
            const GMathVec3d row(to_local.get_item(0, axis),
                                 to_local.get_item(1, axis),
                                 to_local.get_item(2, axis));
            const double length = row.get_length();
            if (length > biggest) biggest = length;
        }
        shrink = (biggest > 1e-12) ? (1.0 / biggest) : 1.0;
        return true;
    }

    // Empile les formes d'un node, en depliant les groupes. On aplatit a la
    // construction plutot que de descendre l'arbre a chaque rayon : un groupe
    // n'est qu'une commodite d'organisation, il ne doit rien couter au rendu.
    static void
    gather(const OfObject& object, const GMathMatrix4x4d& node_inverse,
           std::vector<Primitive>& out, const unsigned int& depth)
    {
        if (depth > 8u) return;   // un groupe qui se contient lui-meme

        const OfAttr *items = object.get_attribute("item");
        if (items == 0) return;
        const unsigned int rows = items->get_value_count();

        const OfAttr *shapes = object.get_attribute("shape");
        const OfAttr *operations = object.get_attribute("operation");
        const OfAttr *blends = object.get_attribute("blend");
        const OfAttr *sizes = object.get_attribute("size");
        const OfAttr *roundings = object.get_attribute("rounding");
        const OfAttr *sources = object.get_attribute("group");

        for (unsigned int r = 0; r < rows; r++) {
            // Une ligne qui pointe un autre champ prend ce champ tout entier :
            // c'est ce qui fait les groupes.
            if (sources != 0 && r < sources->get_value_count()) {
                OfObject *source = sources->get_object(r);
                if (source != 0 && source != &object
                    && source->get_class().get_name() == "GeometrySdf") {
                    gather(*source, node_inverse, out, depth + 1u);
                    continue;
                }
            }

            OfObject *item = items->get_object(r);
            if (item == 0) continue;

            Primitive prim;
            if (!local_matrix(*item, node_inverse, prim.to_local, prim.shrink)) {
                continue;
            }
            // Une colonne peut etre plus courte que celle des items -- une
            // table remplie par script, par exemple. Lire hors plage rendrait
            // n'importe quoi : on retombe sur le defaut de la forme.
            prim.shape = (shapes != 0 && r < shapes->get_value_count())
                       ? shapes->get_long(r) : SHAPE_SPHERE;
            prim.operation = (operations != 0
                              && r < operations->get_value_count())
                           ? operations->get_long(r) : OP_ADD;
            prim.blend = (blends != 0 && r < blends->get_value_count())
                       ? blends->get_double(r) : 0.0;
            prim.rounding = (roundings != 0 && r < roundings->get_value_count())
                          ? roundings->get_double(r) : 0.0;
            prim.size = GMathVec3d(1.0, 1.0, 1.0);
            if (sizes != 0) {
                // Une colonne a trois composantes s'indexe a la suite : la
                // ligne r occupe les rangs 3r, 3r+1 et 3r+2.
                const unsigned int base = r * 3u;
                if (base + 2u < sizes->get_value_count()) {
                    prim.size = GMathVec3d(sizes->get_double(base),
                                           sizes->get_double(base + 1u),
                                           sizes->get_double(base + 2u));
                }
            }
            out.push_back(prim);
        }
    }

    // La boite d'une primitive, dans le repere du node. On borne large : un
    // cone couche ou une boite tournee ne tiennent dans aucune borne simple, et
    // une boite trop petite ferait disparaitre la forme sans un mot.
    static GMathBbox3d
    primitive_bbox(const Primitive& prim)
    {
        double reach = 0.0;
        for (unsigned int i = 0; i < 3; i++) {
            if (prim.size[i] > reach) reach = prim.size[i];
        }
        if (prim.shape == SHAPE_TORUS) reach = prim.size[0] + prim.size[1];
        reach += prim.rounding;
        reach = sqrt(3.0) * reach / ((prim.shrink > 1e-12) ? prim.shrink : 1.0);

        GMathMatrix4x4d to_node;
        if (prim.to_local.get_inverse(to_node) == 0.0) {
            return GMathBbox3d(GMathVec3d(-reach), GMathVec3d(reach));
        }
        GMathVec3d centre;
        GMathMatrix4x4d::multiply(centre, GMathVec3d(0.0, 0.0, 0.0), to_node);
        return GMathBbox3d(centre - GMathVec3d(reach), centre + GMathVec3d(reach));
    }

    SdfObject *
    build() const
    {
        OfObject *object = get_object();
        if (object == 0) return 0;

        ModuleSceneItem *self = object->get_module<ModuleSceneItem>();
        GMathMatrix4x4d node_inverse;
        if (self != 0) node_inverse = self->get_global_matrix();

        std::vector<Primitive> primitives;
        gather(*object, node_inverse, primitives, 0u);
        if (primitives.empty()) return 0;

        // La boite englobante. Le plan n'en a pas -- il est infini -- donc on le
        // borne sur ce que les autres formes occupent : un plan tout seul n'a
        // rien a delimiter de toute facon.
        bool started = false;
        GMathBbox3d bbox;
        double widest_blend = 0.0;
        for (size_t i = 0; i < primitives.size(); i++) {
            if (primitives[i].blend > widest_blend) {
                widest_blend = primitives[i].blend;
            }
            // Une soustraction ou une intersection ne peuvent qu'enlever de
            // la matiere : les compter ferait une boite plus grande que la
            // forme, et tout ce vide se paierait en parcours de rayon.
            if (primitives[i].operation != OP_ADD && i != 0u) continue;
            if (primitives[i].shape == SHAPE_PLANE) continue;
            const GMathBbox3d box = primitive_bbox(primitives[i]);
            if (!started) { bbox = box; started = true; }
            else bbox.grow(box);
        }
        if (!started) return 0;

        // Le raccord arrondi deborde des primitives qu'il relie, et de combien
        // ne se borne pas exactement. On elargit du plus grand rayon employe,
        // plus la marge que l'artiste peut ajouter.
        const double padding =
            widest_blend + read_double(object, "padding", 0.1);
        bbox = GMathBbox3d(bbox.get_min() - GMathVec3d(padding),
                           bbox.get_max() + GMathVec3d(padding));

        return new SdfObject(primitives, bbox,
                             read_long(object, "max_steps", 256),
                             read_double(object, "step_scale", 1.0),
                             read_double(object, "precision", 1e-4),
                             read_long(object, "uv_mode", 0),
                             read_double(object, "uv_scale", 1.0));
    }
};

IX_BEGIN_DECLARE_MODULE_CALLBACKS(GeometrySdf, ModuleGeometryCallbacks)
    static OfModule *declare_module(OfObject& object, OfObjectFactory& objects);
    static bool destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl);
IX_END_DECLARE_MODULE_CALLBACKS(GeometrySdf)

IX_BEGIN_EXTERN_C

DSO_EXPORT void
on_register_module(OfApp& app, CoreVector<OfClass *>& new_classes)
{
    OfClass *new_class = IX_DECLARE_MODULE_CLASS(GeometrySdf);
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
    SdfModule *module = new SdfModule();
    module->set_object(object);
    return module;
}

bool
IX_MODULE_CLBK::destroy_module(OfObject& object, OfObjectFactory& objects, OfModule *impl)
{
    delete impl;
    return true;
}
