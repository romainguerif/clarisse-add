# Les courbes dans Clarisse, et le tube qu'on écrit par-dessus

État au 2026-09-08. Premier des « petits chantiers » que Romain veut faire
passer avant l'intégrateur.

---

## 1. Ce qu'il a demandé

Quatre usages, énoncés dans cet ordre :

- **des tubes** — câbles, cordes, tuyaux, branches, racines, posés à la main ;
- **distribuer le long** — lampadaires, pierres sur un sentier, rivets ;
- **trajectoire** — faire courir une caméra ou un objet sur un chemin ;
- **déformation** — plier une géo existante le long d'une courbe.

Et **pas de cheveux ni de fourrure**. C'est le seul cas qui aurait été dur, et
il est hors périmètre.

Trois précisions décisives, données ensuite : **« super qualité »**, vu **de
près**, et **« je veux pouvoir scatter des trucs dessus »**.

---

## 2. Clarisse a déjà un système de courbes complet. Il s'appelle « Fur ».

C'est la découverte qui redimensionne le chantier. Tout existe **sauf la
création**.

| Brique | Classe / node |
|---|---|
| Conteneur de courbes | `CurveMesh` (`include/curve/curve_mesh.h`), un seul `init()`, rayons par vertex possibles, UV, couleurs, vélocités, primvars via `CurveMeshProperty` |
| Tessellation en rendu | `TubeMesh` / `RibbonMesh`, trois familles (`Fixed`, `Fast`, `Nakamaru`), pilotées par l'attribut caché `tessellation_mode` (défaut `New` = 2) |
| Nodes de scène | `GeometryFur` (abstraite) → `GeometryFurFile` (.fmg ZBrush), `GeometryFurGenerator`, `GeometryFurInterpolate`, `GeometryAbcFur`, `GeometryUsdFur` |
| Matériaux | `MaterialPhysicalHair` (lobes Marschner nommés R / TRT-g / TT / glints) et `MaterialPhysicalHairFiber` (mélanine, cuticle tilt) |
| Shading le long | `TextureCurveUtility` — sort `P`, `Tangent`, `Radius`, `Curve ID`, avec `target` = `Root` / `Tip` / **`Fragment`** |
| Câblage par défaut | chaque Integrator embarque un `MaterialPhysicalHair` nommé `curve_default` — une courbe importée rend sans qu'on assigne quoi que ce soit |

**L'interpolation est du Hermite cubique câblé en dur** (`curve_tools.h`,
`compute_hermite_coefs`). Il n'existe aucun choix de basis — ni bspline, ni
catmull-rom, ni bézier. `CurveMesh::compute_curve_sample(i, v, pos, tan)`
évalue la courbe, et **cette fonction est bindée en Python**.

**Ce qui n'existe pas** : aucun outil de dessin (les quinze `Tool*` sont
inventoriés, aucun ne trace), aucun node « curve from mesh edges », aucune
contrainte « suivre un chemin », aucun `GeometryCurve` / `Spline` / `NURBS`.
Les courbes n'entrent que par Alembic, USD ou `.fmg`.

---

## 3. Pourquoi on ne passe quand même pas par là

Les deux précisions de Romain ferment le chemin natif, chacune pour une raison
mesurable :

- **`GeometryFur` n'expose aucun attribut de displacement.** Zéro, là où
  `GeometryPolymesh` et `GeometryBundle` en ont sept
  (`displacement_mode`, `displacement_tessellation_mode`, etc.). Pas de gros
  plan de qualité sans displacement.
- **`CurveMesh::get_bbox()` renvoie une bbox nulle et
  `estimate_primitive_area()` renvoie `0.0`** (les méthodes de rendu sont
  neutralisées, le conteneur n'est pas traçable). Les distributions
  surfaciques du scatterer — `Random`, `Blue Noise` — n'ont rien à quoi
  s'accrocher. **On ne peut pas scatter sur une fur.**

D'où la décision : **on produit un vrai polymesh**. Plus lourd en mémoire, mais
le displacement, la subdivision, les UV, le scatter, l'export et tout le reste
marchent alors sans qu'on écrive une ligne pour eux.

Troisième fait qui ferme une porte : **Python ne peut pas créer de géométrie**.
`ModuleGeometry` est en lecture seule côté géo (pas de `set_geometry`), et
`ModuleFur` n'est même pas bindé. La seule écriture possible depuis Python est
un nuage de points, via `ModuleParticle.set_particles()` sur un
`GeometryParticleContainer`. Le C++ est obligatoire.

---

## 4. Correction : la règle de dérivabilité n'était pas celle qu'on croyait

Documentée dans `sdk-clarisse.md`, rappelée ici parce qu'elle a débloqué ce
chantier. Ce n'est **pas** « seules les classes abstraites se dérivent ».
`cmagen` déduit le nom du module de celui de la classe (`snake_case(Classe).dll`)
et le cherche dans `module/`. **La liste des bases dérivables est la liste des
134 DLL de `module/`.**

Sondes passées le 2026-09-08 (`class "Probe" "<Base>" { … }` + `cmagen`) :

| Résolvent | Échouent (contrôles négatifs) |
|---|---|
| `Geometry`, `GeometryFur`, `GeometryPolymesh`, `GeometryParticle`, `GeometryVolume`, `GeometryBundle`, `SceneObject`, `SceneObjectTree`, `SceneObjectScatterer`, **`Deformer`**, `Displacement`, `Locator` | `GeometryAbcFur`, `GeometryFurGenerator` |

`Deformer` et `Locator` dérivables ouvrent les usages (e) et (d) sans détour.

---

## 5. Le chemin polymesh se lie — vérifié, puis prouvé

Le risque annoncé était que le SDK reconstruit ait des stubs vides là où il
faut. **Il ne l'a pas.** Calcul de fermeture transitive des `#include` :

- fermeture de `poly_mesh.h` : 76 en-têtes, un seul fichier d'implémentation
  tiré, `core_atomic.icc` — celui qui a été réécrit à la main et validé ;
- fermeture de `module_polymesh.h` : 128 en-têtes, `core_atomic.icc` +
  `gmath_transform.icc` ;
- **aucun `geometry_*.icc` n'est atteint.** `geometry_object.h` ne fait que
  des déclarations en avant de `GeometrySample`, `GeometryFragment`,
  `GeometryRaytraceCtx`.

Et `gmath_transform.icc` n'est pas un problème non plus : ses sept fonctions ne
sont appelées nulle part dans le SDK, et de toute façon `GMathTransform` porte
`GMATH_EXPORT` **au niveau de la classe**, donc MSVC a exporté jusqu'à ses
inline — elles sont dans `ix_gmath.def`.

> **Correction à porter dans `J:\Clarisse-SDK\RECONSTRUCTION.md`** — deux points.
> (1) Les 19 en-têtes de `module/` qui échouaient tombaient sur
> **`gmath_transform.icc`**, pas sur `geometry_sample.icc` : le compte était
> juste, la cause était fausse. (2) Pour toute classe marquée `X_EXPORT` au
> niveau classe, **les corps inline perdus sont récupérables à l'édition de
> liens depuis la DLL** — ce qui réduit le passif réel des 29 stubs à leurs
> seules entités non exportées (`struct GeometrySource`, 0 symbole, en est une).

Libs ajoutées à `native/build.py` : **`ix_poly`** (PolyMesh), **`ix_gmath`**
(c'est elle qui neutralise le stub), **`ix_geometry`** (GeometryUvMap,
GeometryPointCloud).

`PolyMesh` n'a pas d'`init()` : tout passe par **un seul appel `set()`** à onze
arguments (vertices, velocities, polygon_indices, polygon_vertex_count,
polygon_shading_groups, shading_group_names, uv_maps, normal_maps, color_maps,
handle_degenerated_quad, progress_bar). `set_uv_map_compression_mode()` doit
être appelé **avant** `set()` pour être pris en compte.

---

## 6. `native/tube/` — ce qui marche aujourd'hui

`GeometryTube`, dérivé de `GeometryPolymesh`. Il lit une liste de points de
contrôle (n'importe quel `SceneItem` ; un locator est le choix naturel) et
balaie un profil circulaire le long d'une Catmull-Rom qui passe par eux.

**Compilé, lié et chargé du premier coup.** Mesuré dans `cnode` avec quatre
locators en L, `steps=8`, `sides=12` :

```
classe 'GeometryTube' declaree : OUI
declaree par          : …/native/build/tube.dll
classe de base        : GeometryPolymesh
module C++ attache    : OUI
points branches       : 4
geometrie via         : get_geometry -> PolyMesh
vertex_count          : 300      (25 anneaux x 12 cotes)
polygon_count         : 290      (288 quads + 2 caps)
primitive_count       : 308      (les caps, n-gons de 12, sont triangules)
uv_map_count          : 1
bbox                  : [0.00 -0.20 -0.20] -> [4.20 2.03 4.03]
```

Les trois comptes tombent exactement sur la prédiction, et le débord de la
bbox au-delà des points de contrôle est l'overshoot normal d'une Catmull-Rom.

Deux choses faites correctement dès le départ, parce qu'elles coûtent cher
après coup :

- **le repère à torsion minimale** (double réflexion, Wang et al. 2008).
  Frenet-Serret se retourne aux inflexions et n'est pas défini quand la
  courbure s'annule — c'est-à-dire sur toute portion droite, ce qui est le cas
  ordinaire d'un câble. Le tube y vrillerait brutalement.
- **les UV en face-varying.** La couture du tube exige qu'un même sommet porte
  `u=0` d'un côté et `u=1` de l'autre : impossible en vertex-varying. La grille
  UV a donc une colonne de plus que de côtés.

### L'invalidation, résolue

Le maillage se reconstruit maintenant à chaque changement — mesuré : rayon 0.5
élargit la bbox, `sides = 6` fait passer de 300 à 150 sommets, et déplacer un
locator déplace le tube **immédiatement**.

Ce qui ne suffisait pas : ni `dirtiness |= DIRTINESS_GEOMETRY` dans
`on_attribute_change`, ni `output "geometry"` dans le CID. Il faut **déclarer
la ressource et ses dépendances** dans `module_constructor` :

```cpp
CoreArray<OfAttrDirtiness> attrs(count);          // taille exacte, voir ci-dessous
attrs[k] = OfAttrDirtiness(attr, OfAttr::DIRTINESS_ALL);
set_resource_attrs(ModuleGeometry::RESOURCE_ID_GEOMETRY, attrs);
```

**Deux pièges, chacun payé d'un essai :**

- **`CoreArray::resize(n)` ne préserve rien.** Son corps est
  `delete[] m_array; m_array = new T[size];` — les éléments déjà remplis sont
  détruits. Et le constructeur par défaut de `OfAttrDirtiness` laisse son
  pointeur **non initialisé**. Un tableau dimensionné large puis réduit après
  remplissage part donc en `EXCEPTION_ACCESS_VIOLATION` dans
  `OfAttrPtr::operator=`, au fond de `set_resource_attrs`, loin de sa cause.
  Compter d'abord, allouer à la taille exacte ensuite. Il existe
  `resize(size, preserve)` pour l'autre besoin.
- **`DIRTINESS_GEOMETRY` ne suffit pas pour une référence.** Quand un locator
  bouge, ce qui remonte est `DIRTINESS_MOTION`. Filtrer sur la géométrie seule
  laissait le tube en retard d'un changement : il se reconstruisait à la
  modification suivante, en relisant au passage la nouvelle position — un
  symptôme trompeur, parce que la position finissait par être juste.
  `DIRTINESS_ALL` sur tous les attributs déclarés règle la question, et nos
  sept attributs affectent tous la géométrie de toute façon.

### Le rendu, validé

Un rendu de contrôle a été fait (6 s en 1920×1080) sur une courbe en S de quatre
points, 24 côtés, rayon 0.25, avec un sol et deux lumières. **Le tube est
correct** : normales sorties du bon côté, pas de vrillage dans les virages,
caps fermés, ombre portée cohérente, shading lisse. Le repère à torsion minimale
fait son travail.

Scène de test : `native/tests/make_tube_scene.py`.

### Piège de test rencontré

Les chemins d'objets ne s'écrivent pas à la main. Un objet créé par
`ix.cmds.CreateObject("cp0", …, "project:/")` a pour nom complet
**`build://project/cp0`**, pas `project:/cp0`. Un `SetValues` sur un chemin
inventé échoue en silence — ça a fait croire pendant deux essais que le module
était en cause. Toujours passer par `str(objet)`.

---

## 7. L'architecture visée

Les quatre usages ne partagent qu'**une seule fonction** : pour un paramètre
`t`, rendre une position, une tangente et un repère. Les tubes en sont un
balayage, la distribution un échantillonnage, la trajectoire un `t` animé, la
déformation une projection puis une reconstruction. Un seul cœur, quatre
enveloppes.

| Node | Base | État |
|---|---|---|
| `GeometryTube` | `GeometryPolymesh` | **écrit**, invalidation à finir |
| mode corde / câble | même node | à faire — N torons hélicoïdaux, `strands`, `twist`, `strand_radius` |
| distribution le long | `GeometryParticle` | à faire — un point par intervalle de **longueur d'arc** (une abscisse uniforme tasserait les objets dans les virages), avec orientation ; le Scatterer natif consomme derrière |
| déformeur | `Deformer` | à faire — base `ProjectItem`, `embedded_only`, callbacks `cb_pre_deform` / `cb_deform` / `cb_post_deform`, `deform` appelé **en parallèle sur des plages disjointes** de sommets |
| trajectoire | `Locator` | à faire |
| poser les points | shelf Python | à faire — créer une rangée de locators, insérer, fermer |

**Scatter sur le tube : rien à écrire.** Un polymesh a une aire, le Scatterer
natif fonctionne dessus. C'est tout l'intérêt d'avoir refusé le chemin fur.

Pour les orientations d'un nuage de points : `GeometryPointCloud` ne porte
**ni matrices ni quaternions**, seulement positions, normales et vélocités. Le
scatterer compose l'orientation à partir des normales (`use_support_normals`)
et de ses attributs `scatter_rotation*`, qui sont **texturables** — donc une
orientation arbitraire passe par des `GeometryPointProperty` lues par une
texture, pas par un tableau de matrices. Il n'y a pas de troisième voie.

---

## 8. Le type d'attribut `curve` (à ne pas confondre)

Dans Clarisse, « curve » désigne deux choses disjointes : la géométrie courbe
ci-dessus, et le **petit éditeur de courbe de l'attribute editor** — c'est
`FCurve`, `OfPlugType::TYPE_CURVE`. `GeometryTube` s'en sert pour le profil de
rayon le long du tube.

Il n'existe que **treize occurrences** dans tout Clarisse :
`GeometryFurInterpolate` (`influence_shape`, `clump_shape`), `TextureOcclusion`
(`distance_falloff`, `normal_falloff`), `LightPhysical` (`attenuation_curve`),
`ToolBrush` (`brush_curve`), `RendererRaytracer`
(`motion_blur_shutter_curve`, `tone_mapping_curve` en `curve[3]`),
`GeometryVolumeBox` (`density_falloff`), `ImageFilterRemap` (`output`,
`curve[3]`), `TextureRemap` (`output`, `curve[4]`).

Sérialisation : `key <type> <x> <y> <tcb…> <tangentes…> <flags>`, le premier
champ étant `FCurveKey::KeyType` — `0=LINEAR, 1=STEP, 2=TCB, 3=HERMITE,
4=BEZIER`. Le premier élément d'un tableau s'écrit `value[]`, les suivants
`value[1]`, `value[2]`.

Lecture C++ : `OfAttr::get_curve_double(t)`, `get_curve_vec3d`, `get_curve()`.
Python : `ix.cmds.AddCurveValue` / `SetCurve` / `SetCurveKeyType` — le dépôt
s'en sert déjà dans `shrinkwrap.py:55` et `gradient_random.py:41`.
`GuiCurve` **n'est pas bindé** : impossible d'embarquer un éditeur de courbe
dans une UI de script.
