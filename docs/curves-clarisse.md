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

## 6. Ce qui est construit

Cinq modules, tous compilés, chargés et vérifiés au rendu — sauf la plume, dont
le geste ne peut pas se tester en ligne de commande.

| Module | Classe | Base | État |
|---|---|---|---|
| `native/tube/` | `GeometryTube` | `GeometryPolymesh` | tubes, cordes à torons, coudes, câble qui pend, trim |
| `native/curve_points/` | `GeometryCurvePoints` | `GeometryParticle` | distribution le long, embouts aux extrémités |
| `native/curve_deform/` | `DeformerCurve` | `Deformer` | plie une géométrie le long de la courbe |
| `native/pen/` | `ToolCurvePen` | `Tool` | pose les points dans la vue — **à essayer en interactif** |
| `native/common/curve_core.h` | — | — | le cœur partagé : trois modes de tracé, repère, longueurs d'arc |

### Les trois modes de tracé

Ils vivent tous dans `curve_core::Path` et se choisissent par l'attribut
`interpolation`, commun aux nodes :

- **Lisse** — Catmull-Rom passant par les points.
- **Coudes** — segments droits raccordés par des arcs de rayon constant, comme
  la tuyauterie. Le retrait d'un coin vaut `r/tan(α/2)`, borné à un peu moins de
  la moitié du segment le plus court : une seule passe, aucune collision
  possible entre deux coudes voisins. C'est le budget par demi-segment, et
  l'état de l'art confirme que c'est le bon choix — le `Limit Radius` de Blender,
  qui rééquilibre entre sommets voisins, est un point douloureux reconnu de son
  bug tracker. **Le rayon se règle globalement ou point par point.**
- **Câble qui pend** — chaînette exacte par portée, résolue et non simulée.

### La chaînette, et pourquoi elle est résolue plutôt que simulée

Tous les outils du marché simulent : Cablerator fait tourner un solveur sur
400 images, sans collision, et son auteur documente que ça « donne parfois des
résultats bizarres » ; Fluent simule 100 images et son auteur explique qu'il a
choisi l'éditabilité contre la collision. Ici c'est une équation scalaire.

Trois décisions valaient d'être prises correctement :

- **Le paramètre exposé est le mou en pourcentage**, pas la longueur de câble ni
  le paramètre `a` de la chaînette. C'est un rapport, donc le réglage survit au
  déplacement d'un point d'accroche — ce que ne fait aucun des deux autres.
- **Les appuis à hauteurs différentes se ramènent au cas de niveau** en
  remplaçant la longueur `S` par `√(S² − h²)`. Un seul solveur couvre les deux,
  et le point bas se décale de lui-même vers l'appui le plus bas. C'est visible
  au rendu, et c'est ce qui distingue une chaînette d'une parabole.
- **L'équation est résolue sous la forme `sinh(u)/u = r`**, strictement
  croissante donc à racine unique, plutôt qu'écrite en `a` où une racine parasite
  en zéro égare Newton. Amorce par `u₀ = √(6(r−1))`, quatre itérations suffisent.

### Les points de contrôle : une table

Le tube porte une **table** `control_points` : une ligne par point, le locator
dans la première colonne, son **rayon de coude** à côté (négatif = reprendre le
global). Trois boutons l'accompagnent :

- **Add Point** pose un locator dans le prolongement du dernier segment ;
- **Add Selected** ajoute tout ce qui est sélectionné dans la scène, dans
  l'ordre — c'est le geste habituel : poser ses locators, les sélectionner,
  cliquer ;
- **Draw Points** braque la plume sur cette courbe.

Le motif vient des contraintes natives, qui déclarent `action "set_offset"` avec
pour documentation « Invoke Translate Tool ». Un bouton qui invoque un outil est
l'idiome de Clarisse, pas un bricolage.

**Réserve** : le SDK reconstruit n'expose aucun moyen d'**activer** un outil
depuis du code — rien qui ressemble à un `set_current_tool` dans `gui/` ni dans
`clarisse_app/`. Les contraintes natives y arrivent, donc le mécanisme existe et
reste à trouver. En attendant, *Draw Points* crée la plume et la vise ; il reste
à la choisir dans la barre.

### Les embouts

`curve_points` a un mode **Extrémités** : deux points, et surtout **deux
tangentes opposées** — un connecteur posé au départ doit regarder vers
l'extérieur du câble, sinon les deux embouts pointent du même côté. Avec
`end_offset` pour les sortir du câble, et `trim_start` / `trim_end` sur le tube
pour raccourcir la gaine.

**Deux réglages séparés, pas un** : l'un recule la gaine, l'autre avance
l'embout. C'est ce que fait Cablerator, et c'est ce qui permet au connecteur de
recouvrir la fin du câble au lieu d'être traversé par elle.

Le trim rééchantillonne par longueur d'arc, ce qui déplace les anneaux : on ne
le fait donc que si un trim est réellement demandé, pour que le mode Coudes
garde ses anneaux exactement sur les points de tangence des arcs.

### Ce qui a été vérifié au rendu

Tube en S (300 sommets, comptes exacts) · cordes à 1, 3 et 7 torons · coudes à
deux rayons avec bornage sur segment court · rayon de coude par point · 14
instances régulièrement espacées consommées par le Scatterer natif · chaînette
à quatre mous plus un cas à appuis inégaux · câble complet avec gaine trimmée,
huit colliers et deux connecteurs · déformeur sur hélice.

Le déformeur a été mesuré plutôt que regardé : sommet extrême au départ exact de
la courbe, extrême opposé à l'arrivée exacte, espacement constant à 0,3 % près,
160 000 sommets sans qu'aucun reste au repos — ce dernier point vérifiant que le
code est réentrant, puisque Clarisse appelle `cb_deform` en parallèle sur des
plages disjointes.

### Ce qui n'a pas pu être testé

**Le geste de la plume.** `cnode` n'a pas de viewport, donc aucun événement
souris n'y arrive jamais. Ce qui est vérifié : la classe se déclare, s'instancie,
son module s'attache, ses attributs sont là. Ce qui ne l'est pas : le repère
exact des coordonnées souris, le moment d'appel du tracé, l'activation dans la
barre. Les trois se lèveront au premier essai interactif.

---

## 7. Les pièges d'API payés en route

Ceux qui dépassent ce chantier sont dans `sdk-clarisse.md`. Les autres :

- **Les chemins d'objets ne s'écrivent pas à la main.** Un objet créé dans
  `project:/` a pour nom complet `build://project/<nom>`. Un `SetValues` sur un
  chemin inventé échoue **en silence**. Toujours `str(objet)`.
- **Une table CID aplatit ses colonnes en attributs à part entière.** Une table
  `control_points` avec une colonne `point` donne un attribut nommé `point`. Elle
  se remplit avec `AddTableRow` puis une écriture dans la colonne indexée ;
  écrire dans la colonne sans avoir créé la ligne ne signale rien et ne fait
  rien.
- **Les shading groups n'existent qu'après construction de la géométrie.**
  Assigner un matériau avant échoue avec un message qu'on lit mal.
- **`cmagen` nomme les callbacks d'action `on_<attribut>_<attribut>_action`** et
  les déclare `static`. Les définir dans un namespace anonyme en fait d'autres
  fonctions, et la déclaration reste orpheline.
- **La catégorie d'un outil doit exister.** Les seules sont `Brush`, `Create`,
  `View`, `Select`, `Transform`, `Measure`. Une catégorie inventée fait
  disparaître la classe des menus sans le moindre message.
- **`OfContext::ensure_unique_name` est privé** et le contexte n'expose aucune
  recherche par nom : on laisse `add_object` refuser un nom pris et on incrémente.
- **Clarisse verrouille les DLL de ses modules** tant qu'il tourne. On savait
  qu'il faut le relancer après une reconstruction ; on ne peut pas reconstruire
  pendant.
- Libs découvertes : `ix_resource` (la vtable de `ParticleCloud` réclame
  `ResourceData::is_serializable`), `ix_ctx` (`CtxDraw`), `ix_app`
  (`AppSelection`), `opengl32` — et `windows.h` avant `GL/gl.h`.
- **`AddValues` sur `.deformers` rend `None`** : l'objet créé se relit sur
  l'attribut, et son nom est le nom de classe amputé de sa base, en minuscules
  (`DeformerCurve` → `curve`).
- **Un déformeur n'a besoin ni de `set_resource_attrs` ni de
  `on_attribute_change`** — l'inverse de la règle du tube, parce qu'il ne possède
  aucune ressource. Le `output "geometry"` du CID suffit.

---

## 8. Ce que dit l'état de l'art, et ce qui reste à faire

Deux dossiers de recherche ont été rassemblés : l'un sur **Fluent** (l'addon
Blender que Romain cite en référence), l'autre sur les **générateurs de câbles**
en général.

**Ce qu'on a appris de plus utile** : l'outil câble de Fluent est plus simple
qu'il n'y paraît — deux clics, deux points, aucun point intermédiaire, une
courbe dont les tangentes sortent selon la normale des faces cliquées, et **un
seul réglage**, `Root strength`. Toute la richesse perçue vient des extras
(coil, gaine spiralée, conduit aplati, chaîne, anneaux, connecteurs) et du verbe
**« Pick »** : l'anneau, le connecteur et le maillon se choisissent en cliquant
n'importe quel objet de la scène. Fluent ne livre presque aucune bibliothèque.

**Ce qui reste à faire, par ordre de valeur :**

1. **Les tangentes d'accroche** (`root_strength`). Remplacer le point fantôme du
   Catmull-Rom aux extrémités par `P₀ + n·force`, où `n` est la normale de la
   surface d'accroche. Une quinzaine de lignes dans `curve_core.h`.
2. **La résolution en segments par mètre**, qui remplacerait `sides` et `steps`.
   C'est le meilleur réglage de Fluent — une densité en unités monde plutôt que
   des compteurs absolus, un seul curseur pour faire passer trois cents câbles
   du gros plan au décor. Et chez nous elle pourrait être pilotée par un node
   partagé, ce que Blender ne permet pas.
3. **Le raycast dans la plume.** `ShaderHelpers::raycast` existe et **rend la
   normale au point touché** — c'est ce qui débloque le geste à deux clics, et
   la normale alimente le point 1.
4. **Le bruit** : Perlin deux octaves, **projeté dans le plan normal** (jamais de
   composante tangentielle, sinon les UV et le pavage des modules dérivent),
   fondu à zéro autour de chaque point épinglé, fréquence en unités monde.
   Meilleur rapport qualité visuelle / lignes de code de toute la liste.
5. **Les UV en longueur d'arc** : `V = longueur / tile_length` plutôt que
   normalisé 0-1, pour une densité de texel constante quelle que soit la
   longueur du câble. Un retour de production documente que les UV automatiques
   de balayage cassent dans les coudes, et c'est exactement pourquoi.
6. **Les profils coil, gaine spiralée et conduit** : la même hélice que nos
   torons avec d'autres rayons et d'autres pas.

**Ce que les deux rapports déconseillent** : ne pas construire de collision ni
de simulation. Coût élevé, résultat non déterministe, dépendance du node à de la
géométrie externe — et même l'outil commercial dédié documente que la sienne
dérape. Un mou exact, du bruit, et un locator posé à la main là où le câble doit
toucher couvrent presque tout pour une fraction du travail.

Un détail relevé et déjà juste chez nous : la torsion des torons se compte **en
tours par unité de longueur réelle** et non en tours sur la courbe entière.
Sinon le pas de la corde changerait à chaque déplacement d'un locator.

---

## 9. Le type d'attribut `curve` (à ne pas confondre)

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
