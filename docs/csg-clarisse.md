# Modéliser en CSG dans Clarisse — étude de faisabilité

Écrit le 2026-09-08. Document d'étude, pas de plan de développement : la
question posée est **est-ce faisable, comment, et à quel prix**.

Romain : « des recherches plus poussées sur un potentiel système de modélisation
en CSG dans Clarisse (je sais, ça paraît dingue, mais Clarisse avec sa gestion
des polygones pourrait être assez ouf), ou sinon réflexion sur un potentiel
système de booléens avancé. »

**Ça ne paraît pas dingue. C'est plus ouvert que prévu**, et pour une raison
qu'on n'attendait pas : ce n'est pas la gestion des polygones qui ouvre la
porte, c'est le fait que Clarisse trace des primitives qui n'ont ni points ni
polygones — et que ce point d'entrée est ouvert au SDK. C'est vérifié par une
sonde qui rend.

---

## 0. La réponse en une page

**Ce qu'on a prouvé en écrivant du code.** Un module tiers peut fournir à
Clarisse une géométrie qui **n'existe pas** : ni sommets, ni faces, ni voxels —
seulement une fonction qui répond « ce rayon touche la surface ici ». Clarisse
lui construit son BVH, l'instancie, l'ombre avec le matériau du shading group,
et la rend. La sonde `native/csg_sonde/` calcule l'union, l'intersection et la
différence de deux sphères **dans l'intersecteur**, et les trois se rendent
correctement, avec la paroi concave de la morsure éclairée et l'arête de coupe
nette. Rendu en **0,44 s** pour la scène complète.

**Ce que ça veut dire.** Le CSG implicite dans Clarisse n'est pas un projet de
recherche, c'est un projet d'ingénierie. Le mécanisme est là, il est propre, il
n'y a aucun blocage architectural. Ce qui reste à faire est le travail normal
d'un nœud : les primitives, l'arbre, les UV, le motion blur, l'interface.

**Ce que ça ne résout pas.** Le CSG implicite ne résout que le CSG **entre
primitives implicites**. Faire une différence entre deux maillages importés est
un tout autre problème — celui de la robustesse en arithmétique flottante — et
celui-là est aussi dur dans Clarisse qu'ailleurs. Il a coûté huit ans à l'auteur
de Manifold et six ans à Blender, qui a fini par adopter Manifold.

**Le second acquis vérifié, et il porte l'argument de Romain.** Le
`SceneObjectScatterer` natif accepte cette géométrie sans sommets et
l'instancie. Un CSG implicite se disperse donc à l'échelle de Clarisse — des
millions d'instances d'un objet qui n'existe pas. C'est ça qu'aucun autre
logiciel ne sait faire.

**Les quatre voies, et leur verdict :**

| Voie | Ce que c'est | Verdict |
|---|---|---|
| **A. CSG implicite analytique** | Primitives paramétriques + opérateurs, résolus par intervalles dans `intersect_primitive`. Rien n'est tessellé. | **Ouverte et prouvée.** Un premier nœud utile en quelques jours. Ne marche que sur des primitives, pas sur des maillages. |
| **B. CSG au niveau du rayon sur les BVH existants** | Le nœud interroge les BVH des opérandes (`GasObject::ray_hit`) et compose les intervalles. Marcherait sur **n'importe quelle** géométrie Clarisse, maillages compris. | **Structurellement possible, non vérifiée.** Tous les symboles sont exportés. Un seul point à trancher, nommé au §6. C'est la piste la plus intéressante du document. |
| **C. Level sets OpenVDB** | Maillage → champ de distance → booléen → maillage. | **Ouverte.** OpenVDB 7.0 est livré avec Clarisse, se lie et s'initialise depuis notre code — vérifié. Coût : les arêtes vives et la résolution. |
| **D. Booléens exacts sur maillages** | Arrangements de maillages en arithmétique exacte. | **Le vrai gros morceau, et l'obstacle est juridique.** Des années-homme si on l'écrit ; 3 à 6 semaines si on intègre — mais **la seule bibliothèque utilisable dans un addon fermé est Manifold** (Apache-2.0), et elle exige une entrée manifold. |

**La recommandation, en une phrase** : commencer par A, parce que c'est prouvé
et que ça donne tout de suite quelque chose qu'aucun autre logiciel ne fait
aussi bien que Clarisse pourrait le faire ; sonder B tout de suite après,
parce que si B marche, il rend D presque inutile.

---

## 1. Ce que Clarisse a déjà — inventaire vérifié

Sondé le 2026-09-08 avec `native/tests/csg_probe.py`, qui énumère les **359
classes** du factory, leur arbre d'héritage, leur état de licence et les
attributs de leurs prototypes. Rien ici n'est déduit d'une documentation.

### 1.1 Il n'y a aucun booléen, nulle part

Recherche par mot-clé sur les 359 noms de classes :

```
bool         (rien)          csg          (rien)
union        (rien)          intersect    (rien)
sculpt       (rien)          carve        (rien)
clip         (rien)          trim         (rien)
meta         (rien)          blob         (rien)
iso          (rien)          voxel        (rien)
remesh       (rien)          solid        (rien)
implicit     (rien)          sdf          (rien, hors GeometryUsdFur)
```

Pas de node booléen, pas de metaball, pas de sculpt, pas de remesh, pas de
`GeometryCombiner`. C'est un manque net, pas une fonction cachée sous un autre
nom.

### 1.2 `SceneObjectCombiner` n'est pas un combiner géométrique

C'est le candidat évident, et il ne fait pas ce que son nom laisse croire.
Dérivé de `SceneObjectTree`, il porte **un seul attribut propre** :

```
objects    TYPE_REFERENCE
```

Les 56 autres sont le socle `SceneItem` / `SceneObject`. C'est un **agrégateur**
— il rassemble des objets de scène en un nœud traversable unique, ce qui sert au
scatter et à l'imbrication récursive (voir `sdk-clarisse.md` §10). Aucune
opération ensembliste sur les surfaces.

### 1.3 Les « implicits » de Clarisse existent, et c'est la découverte utile

`GeometryBox`, `GeometrySphere`, `GeometryCylinder` dérivent **directement** de
`Geometry` — pas de `GeometryPolymesh`. La documentation les décrit ainsi :

> Implicits geometries are mathematical surfaces that don't rely on points and
> therefore can't be deformed. They can define a single or multiple primitives
> and they are **directly ray traced without relying on tessellation**.

Leurs attributs propres tiennent en une ligne chacun : `size` pour la boîte,
`radius` pour la sphère, `radius`/`height`/`show_caps` pour le cylindre.
**Aucune opération entre elles.** Ce sont trois primitives isolées.

Mais elles prouvent que le moteur sait tracer autre chose qu'un triangle, et
elles portent quand même `clip_maps`, `displacements` et les sept attributs de
displacement. Le chemin non-tessellé n'est pas un chemin dégradé.

### 1.4 Les volumes : OpenVDB est là, mais ce n'est pas ce qu'on croit

L'arbre `GeometryVolume` compte cinq classes :

| Classe | Ce qu'elle fait | Licence |
|---|---|---|
| `GeometryVolumeFile` | lit un `.vdb`, avec delayed loading | libre |
| `GeometryVolumeBox` | atmosphère uniforme, densité par courbes ou texture | libre |
| `GeometryVolumeSurface` | **génère un volume de densité à partir d'une géométrie** | libre |
| `GeometryVolumeBake` | — | **verrouillée BUiLDER** |
| `GeometryVolumeGenerator` | — | **verrouillée BUiLDER** |

**Piège à ne pas retomber dedans** : `GeometryVolumeSurface` ressemble
furieusement à un « maillage → level set ». Ce n'en est pas un. Ses attributs
propres sont `surface`, `density`, `step_mode`, `step_count`, `step_size` et
sept qualités de rayon. La documentation dit « allows you to generate a volume
from an input geometry […] works best with enclosed geometries ». C'est un
**brouillard**, pour de l'atmosphère, pas une surface implicite. Le rendu est
volumétrique et se paie en pas de marche.

Et les deux classes qui sonnent le plus comme du travail de champ —
`GeometryVolumeBake` et `GeometryVolumeGenerator` — sont **verrouillées par
licence en iFX**. Si un jour on veut leur machinerie, on ne l'aura pas.

### 1.5 Ce qui n'est pas une piste, et pourquoi

- **`TextureDistanceToObject`** — le nom promet un champ de distance. La doc
  dit : « the scene item whose **position** defines the center point used to
  compute distances ». C'est la distance à un **point**, pas à une surface.
  Inutilisable comme SDF.
- **Les `Deformer`** — `ModuleDeformerTopology` porte un `point_count` fixé à la
  construction, sans aucun moyen de le changer. **Un déformeur ne peut pas
  changer la topologie.** Un booléen en est une par définition : la voie est
  fermée, et c'est net.
- **`Displacement`** — neuf attributs, tous des amplitudes et des directions le
  long des normales. Ça déplace une surface, ça n'en supprime pas.
- **Les clip maps** — celles-là méritent mieux qu'un rejet. Une clip map est un
  masque de transparence binaire évalué par fragment, et la doc précise qu'elles
  « render **way faster** » qu'une vraie transparence. C'est un booléen de
  pauvre, gratuit, déjà là : une texture 3D qui vaut > 0,5 troue la surface.
  **Sa limite est structurelle** : elle enlève de la surface, elle n'en crée
  pas. Une sphère clippée montre un trou, pas une cavité — il n'y a pas de paroi
  intérieure ni de bouchon. Pour du feuillage ou de la rouille c'est parfait,
  pour de la modélisation ça ne l'est pas.

---

## 2. Le point d'entrée, et sa preuve

### 2.1 `GeometryObject` ne parle jamais de polygones

C'est le fait central du document. `geometry_object.h` déclare une interface
purement virtuelle dont **huit méthodes seulement** sont obligatoires :

```cpp
virtual GeometryObject *get_copy() const = 0;
virtual GMathBbox3d get_bbox() const = 0;
virtual const CoreBasicArray<CoreString>& get_shading_group_names() const = 0;
virtual unsigned int get_primitive_count() const = 0;
virtual unsigned int get_primitive_edge_count(const unsigned int& id) const = 0;
virtual void compute_primitive_bbox(const CtxEval&, const unsigned int& id,
                                    GMathBbox3d&) const = 0;
virtual unsigned int get_primitive_shading_group_index(const unsigned int&) const = 0;
virtual void compute_fragment_sample(const CtxEval&, const GeometryFragment&,
                                     GeometrySample&) const = 0;
virtual void intersect_primitive(const CtxEval&, const unsigned int& id,
                                 GeometryRaytraceCtx&) const = 0;
```

Rien là-dedans n'exige un maillage. « Combien de primitives, où est la boîte de
chacune, qu'est-ce qu'un rayon y touche, et à quoi ressemble la surface au point
touché. » `PolyMesh` en est **une** implémentation — via `PolyMeshBase :
GeometryObject`. `VolumeSparse`, `VolumeSurface` et `GeometrySphere` en sont
d'autres.

Et le constructeur du BVH est générique par-dessus :

```cpp
bool GasGeometry::create(const CtxEval&, const GeometryObject *geometry_base,
                         const GeometryObject *geometry_deformed, ...);
```

Autrement dit : **une géométrie qui sait donner la boîte de ses primitives et
les intersecter reçoit gratuitement le BVH de Clarisse, son instanciation, ses
shading groups, ses slots de displacement et son motion blur.**

### 2.2 La sonde, et ce qu'elle a rendu

`native/csg_sonde/` — un nœud dérivé de `Geometry` dont `create_resource`
renvoie un `GeometryObject` maison, `CsgObject`, qui calcule dans
`intersect_primitive` l'union, l'intersection ou la différence de deux sphères
analytiques. **Aucun sommet n'est alloué nulle part.**

Le rendu montre les trois opérations côte à côte : deux sphères fusionnées avec
leur arête de rencontre, la lentille de l'intersection avec son bord vif, et la
sphère mordue. Un gros plan sur la morsure montre la **paroi concave
correctement éclairée** — le point qui prouve que ce n'est pas un truquage de
silhouette : la normale de la surface de B y est retournée, et l'éclairage suit.

Ce qui a été vérifié, précisément :

- la classe se déclare, s'instancie, son module s'attache ;
- `get_primitive_count()` et `get_bbox()` sont lus par le moteur ;
- `intersect_primitive` est appelé par le path tracer, avec les rayons déjà
  **transformés en espace local** ;
- `compute_fragment_sample` est appelé pour ombrer ;
- le matériau par défaut du shading group s'applique ;
- toute la chaîne tient en **77 ko de DLL** et rend en 0,44 s.

Pour la refaire : `python build.py csg_sonde`, puis `cnode` sur
`native/tests/make_csg_scene.py` pour construire la scène et sur l'image
`build://project/image` pour la rendre. Le nœud a un mode de bissection
volontaire — un `radius_a` négatif lui fait renvoyer une `GeometrySphere`
native depuis le même `create_resource`, ce qui distingue en une mesure un
défaut de notre `GeometryObject` d'un défaut de montage du module.

*Note d'état au 2026-09-08 : la DLL présente dans `native/build/` est une
version intermédiaire de mise au point, laissée là parce qu'une autre session
rendait en boucle et gardait le dossier verrouillé. Reconstruire avant de
s'en servir.*

### 2.3 Les trois pièges rencontrés, à ne pas redécouvrir

**Le premier a coûté six rendus, et il faut le porter dans
`sdk-clarisse.md`.** `push_intersection` prend un `GeometryMediumDescriptor` par
défaut, dont le constructeur est un inline perdu avec `geometry_fragment.icc`.
Il faut le réécrire soi-même, et **`opacity` doit valoir 1, pas 0** :

```cpp
inline void GeometryMediumDescriptor::clear() {
    opacity = GMathVec3f(1.0f);   // 0 = milieu totalement transparent
    thickness = 0.0f; density = 0.0f; density_diff = GMathVec3f(0.0f);
}
```

Avec `opacity = 0`, le moteur comprend « milieu totalement transparent », un
**filtre d'intersection** rejette le fragment, et **rien ne s'affiche du tout** :
ni silhouette, ni alpha, ni avertissement. Le symptôme est indistinguable d'un
module mal monté.

**Le deuxième est la méthode qui a permis de trancher.** Pour distinguer « notre
`GeometryObject` est mauvais » de « notre module est mal monté », il suffit de
renvoyer une `GeometrySphere` **native** depuis le même `create_resource`. Elle
s'est affichée parfaitement, ce qui a isolé le défaut en une seule mesure. La
sonde garde ce mode : `radius_a` négatif renvoie la sphère native.

**Le troisième est la nature du CSG.** Il se fait sur des **intervalles**, pas
sur des premiers points touchés. Prendre la première intersection de chaque
opérande et comparer les distances donne une image plausible et fausse : le trou
de la différence disparaît dès que le rayon entre par l'arrière de B. Il faut
les deux racines de chaque primitive, et composer les segments.

---

## 3. Voie A — le CSG implicite analytique

### 3.1 Ce que c'est

Un arbre d'opérations sur des primitives paramétriques, évalué **par rayon**.
Chaque nœud sait rendre la liste des intervalles `[t_entrée, t_sortie]` où le
rayon est à l'intérieur de lui. Union, intersection et différence composent ces
listes. La surface est ce qui reste aux bornes.

C'est la géométrie *exacte* : une sphère de rayon 1 est une sphère de rayon 1,
pas une approximation à 10 000 triangles. Le zoom est infini, la silhouette est
parfaite, et la mémoire ne dépend pas de la résolution. Rien de tout ça n'est
vrai d'un maillage.

### 3.2 Ce que dit l'état de l'art, et ce qu'il faut en retenir

Le CSG implicite est un domaine ancien et bien mesuré. Trois chiffres suffisent
à cadrer ce qui est raisonnable.

**Le sphere tracing coûte 10 à 100+ évaluations de champ par rayon.** John Hart,
*Sphere Tracing: A Geometric Method for the Antialiased Ray Tracing of Implicit
Surfaces*, The Visual Computer 12(10), 1996, mesure directement : les zones
convergentes en 10 itérations, les moyennes vers 50, et **plus de 100 sur les
silhouettes** — parce qu'un rayon rasant voit une distance à la surface très
inférieure à la distance au point d'intersection.
<https://graphics.stanford.edu/courses/cs348b-20-spring-content/uploads/hart.pdf>

**La qualité de la borne de distance pilote le temps de rendu, linéairement.**
Hart mesure trois objets identiques sauf la constante de Lipschitz du bruit —
3, 6, 18 — pour des temps relatifs de **1, 7,3 et 27,5**. Et les constantes se
composent *multiplicativement* le long de l'arbre : `Lip(f∘g) ≤ Lip f × Lip g`.
Un arbre profond dégrade sa propre borne.

**Union et complément sont exacts ; intersection et différence ne le sont
pas.** C'est le théorème 3 de Hart : `d(x, A∩B) ≥ max(f_A, f_B)`, une **borne**,
jamais une distance. Et la contamination est totale : si un seul opérande rend
une borne, tout ce qui est au-dessus de lui dans l'arbre en rend une aussi.

**Conséquence directe pour nous, et c'est une bonne nouvelle.** Tout ce qui
précède décrit le coût du **sphere tracing** — la marche par pas de distance.
La sonde n'en fait pas. Elle résout l'intersection rayon/primitive
**analytiquement**, en fermant l'équation du second degré. Le coût est celui
d'un test rayon/sphère, pas de cent évaluations de champ. **Tant qu'on reste
sur des primitives à intersection fermée — sphère, boîte, cylindre, cône, tore,
plan, capsule — le CSG implicite dans Clarisse est de l'ordre du prix d'un
triangle, pas cent fois plus.**

Le sphere tracing ne redevient nécessaire que pour ce qu'on ne sait pas
résoudre analytiquement : le **blend lisse** entre deux formes, le bruit, le
displacement procédural. C'est là qu'est la frontière, et il faut la connaître
d'avance.

**Le prix du blend, chiffré.** Le smooth minimum de Quilez
(<https://iquilezles.org/articles/smin/>) casse structurellement la propriété de
distance : dans la zone de raccord, le gradient n'est plus unitaire. Les
familles « Direct Difference » **sous-estiment** — le marcheur ralentit ; le
smooth-min circulaire **surestime** — le marcheur dépasse et **troue les
objets**. Hart mesure une sous-estimation d'au moins 30 % pour un seul blend
superelliptique. Et Barbier et al. (Lipschitz Pruning, Eurographics 2025)
ajoutent un coût qu'on n'anticipe pas : le blend oblige à **étendre les boîtes
englobantes du rayon de raccord**, ce qui détruit la localité spatiale et rend
l'élagage inopérant.

**L'échelle où ça casse.** Barbier et al. mesurent, sur RTX 4060 en 1920×1080
avec un rayon primaire et un rayon d'ombre, une scène de **6 023 nœuds** à
**9 448 ms par image** en sphere tracing naïf — 15,15 ms avec leur élagage. Ils
écrivent que l'état de l'art antérieur plafonne à « quelques dizaines à quelques
centaines de nœuds ».
<https://wbrbr.org/publications/LipschitzPruning/documents/LipschitzPruning_submitted_to_EG25.pdf>

**Ce que ça impose comme conception** : un arbre CSG implicite doit avoir sa
propre structure d'accélération dès qu'il dépasse quelques dizaines de nœuds.
Dans Clarisse, c'est gratuit — il suffit d'exposer **une primitive par feuille
de l'arbre** au lieu d'une seule primitive globale, et le BVH de `GasGeometry`
s'en charge. La sonde ne le fait pas (elle a une primitive, la boîte entière) :
c'est la première chose à corriger dans un vrai nœud.

### 3.3 Le précédent qui doit servir d'avertissement

Media Molecule a passé quatre ans sur *Dreams*, avec une console dédiée et
l'une des meilleures équipes du monde sur le sujet. Alex Evans a raconté à
SIGGRAPH 2015 (*Learning from Failure*, cours Advances in Real-Time Rendering)
les **quatre moteurs abandonnés** :
<http://media.lolrus.mediamolecule.com/AlexEvans_SIGGRAPH-2015-sml.pdf>

1. **Les polygones** — marching cubes, dual contouring, manifold dual
   contouring : maillages denses, arêtes molles, slivers, auto-intersections.
2. **Le brick engine** — verdict : si on ne fait que des surfaces dures en
   z-buffer, autant faire des polygones.
3. **L'OIT volumétrique** — l'équivalent de 32× 1080p d'échantillons
   semi-transparents en fill rate.
4. **Le refinement renderer** en ray marching pur — magnifique, hors périmètre.

Ce qui a été livré est un **LOD de bricks 8³ raymarchés localement**, réglé pour
qu'un voxel fasse à peu près un pixel à l'écran. Autrement dit : **une
résolution asservie à l'écran, pas au modèle**.

La leçon n'est pas « n'y allez pas ». C'est que **le ray marching de SDF
composé et la polygonisation sont tous les deux des impasses à haute
complexité**, et que ce qui marche est l'intersection analytique ou le LOD
écran. Notre voie A est du côté qui marche, à condition de ne pas dériver vers
le champ composé.

---

## 4. Voie C — les level sets OpenVDB

### 4.1 OpenVDB est livré avec Clarisse, et il est atteignable — vérifié

`openvdb.dll` est dans le dossier d'installation. Les symboles décorés donnent
le namespace de version : **`openvdb::v7_0`** — OpenVDB 7.0. La DLL exporte
**1 139 symboles**.

Vérifié en construisant une bibliothèque d'import depuis la table d'exports
(`dumpbin /exports` → `.def` → `lib /def`), puis en compilant et en **exécutant**
un programme qui appelle `openvdb::v7_0::initialize()` et `uninitialize()` :

```
openvdb initialise
openvdb libere
code=0
```

**La voie VDB est donc ouverte au SDK.** Il n'y a ni en-têtes ni `.lib` livrés,
mais les deux se fabriquent : les en-têtes officiels d'OpenVDB 7.0 sont publics,
et **tous les outils CSG sont des templates**, donc compilés chez nous. La DLL
ne fournit que le noyau — registre de grilles, math, io, compression — et c'est
exactement ce dont on a besoin d'elle.

Les dépendances sont déjà là aussi : `tbb.dll`, `blosc.dll`, `zlib.dll`,
`Half-2_4.dll` et onze DLL Boost 1.70 vc141.

### 4.2 Ce que VDB change, et ce qu'il ne change pas

**Ce qu'il change : la mémoire.** Un level set VDB est en bande étroite, donc sa
mémoire croît en **n², pas en n³**. Ken Museth le mesure directement (*VDB:
High-Resolution Sparse Volumes with Dynamic Topology*, ACM TOG 32(3), 2013,
table III) : le nombre de voxels actifs fait exactement ×4 à chaque doublement
de résolution — 794 720 à 512³, puis 3 189 240, 12 784 621, 51 033 829,
203 923 476, 815 936 095 à 16384³.
<https://www.museth.org/Ken/Publications_files/Museth_TOG13.pdf>

En mémoire : une grille **dense** 4096³ en float32 pèserait **256 Gio**. Le VDB
en bande étroite équivalent : **420 Mo**. Facteur ~624.

**Ce qu'il change aussi : le booléen est quasi gratuit.** Table IX du même
papier — une union de deux level sets à résolution effective **8192³ en 0,03
seconde**. Le partitionnement hiérarchique permet d'opérer au niveau des
**nœuds**, pas des voxels. Museth annonce un gain de plus de 100× sur DT-Grid
pour le CSG.

**Ce qu'il ne change pas, et c'est là qu'il faut être honnête :**

- **Les arêtes vives sont perdues.** Un level set est un champ échantillonné :
  une arête vive plus fine que le voxel n'y survit pas. Il existe une
  reconstruction de features dans `volumeToMesh()` via `setRefGrid()`, mais
  l'algorithme complet **n'est implémenté que dans le plugin Houdini** — pas
  dans la bibliothèque. Le réécrire est un chantier en soi.
- **La résolution est absolue, pas relative à la caméra.** Un objet vu de très
  près dans une grande scène impose son pas de voxel à tout l'objet. Museth
  écrit lui-même qu'un VDB **n'est pas un remplaçant de l'octree** pour
  l'échantillonnage multirésolution, et que le mipmapping n'est pas supporté
  dans un arbre unique.
- **Le maillage de sortie explose, lui, quadratiquement.** Une amphore
  polygonisée à 256³ donne 932 166 triangles (Labsik et al.,
  <https://multires.caltech.edu/pubs/isosurface.pdf>) ; la même à 4096³ en
  donnerait de l'ordre de **240 millions**. Le champ tient en mémoire ; le
  maillage, non.
- **La licence a changé.** OpenVDB est passé de MPL 2.0 à **Apache 2.0** avec la
  version 12.0.0 (31 octobre 2024). La 7.0 livrée avec Clarisse est donc sous
  **MPL 2.0**. C'est compatible avec un addon propriétaire — MPL 2.0 est une
  copyleft de fichier, pas de projet — mais il faudra le vérifier
  formellement si l'addon est distribué.

### 4.3 Ce que fait vraiment l'industrie

Il faut le dire parce que c'est contre-intuitif : **les artistes VFX n'ont pas
« choisi » le SDF, ils ont deux outils et choisissent selon le cas.** Houdini
livre les deux moteurs côte à côte — le `Boolean` SOP polygonal, qui garde la
précision et les arêtes vives mais exige une géométrie propre, et la chaîne
`VDB from Polygons` → `VDB Combine` → `Convert VDB`, qui encaisse la géométrie
sale et les milliers d'opérandes contre une perte de résolution.

Chiffres de terrain relevés chez les praticiens : une taille de voxel de **0,02**
permet de déplacer les formes de façon interactive ; des chanfreins fins
imposent **0,01 unité par voxel**, au prix de la performance. Et le maillage
d'entrée doit être fermé et cohéremment orienté — trous et normales inversées
sont **amplifiés** par la voxelisation.
<https://www.sidefx.com/docs/houdini/nodes/sop/vdbfrompolygons.html>

Côté moteurs, trois des cinq gros savent rendre une isosurface sans la
polygoniser — RenderMan via `RiBlobby` (mais en *dicing* micropolygone,
donc pas vraiment sans tessellation), Arnold via son nœud `implicit` et son
solveur `levelset`, V-Ray via `VRayVolumeGrid` en mode Isosurface. Chaos
documente explicitement que ce mode **prend plus de temps que le mode Mesh**.
Cycles ne sait pas le faire du tout. **Aucun moteur de production ne fait de
sphere tracing d'arbre CSG analytique.** Ce que la voie A propose n'a donc pas
d'équivalent commercial direct — ce qui est à la fois l'intérêt et le risque.

---

## 5. Voie D — les booléens exacts sur maillages

C'est la voie à laquelle on pense en premier, et c'est celle qu'il faut aborder
en dernier. Voici pourquoi, avec les chiffres.

### 5.1 Le mécanisme de l'échec : il est logique, pas métrique

Un booléen ne calcule pas des positions, il prend des **décisions
combinatoires** : ce point est-il au-dessus ou au-dessous de ce plan, ce
triangle est-il devant ou derrière cet autre autour de cette arête. Chacune est
le **signe d'un déterminant**. En flottant, un déterminant proche de zéro sort
parfois avec le mauvais signe — et l'algorithme construit alors une topologie
qui **ne correspond à aucune configuration géométrique réelle**.

Les auteurs d'EMBER (Trettner, Nehring-Wirxel, Kobbelt, SIGGRAPH 2022) le
formulent en une phrase qui doit rester en tête :

> In terms of Hausdorff distances, these errors are **usually not bounded**.

Ce n'est pas « un peu faux ». C'est arbitrairement faux : un pan entier de
maillage peut basculer du mauvais côté. Les quatre symptômes observés sont le
plantage, la **boucle infinie**, le résultat silencieusement faux, et — le
meilleur des cas — l'abandon propre.

Shewchuk documente la boucle infinie dans son propre tétraédriseur : sur une
grille inclinée, la version approchée **ne termine jamais**, coincée dans la
localisation de point à cause d'une incohérence introduite par l'arrondi. Sa
table 9 porte littéralement « ∞ » dans cette colonne. Sa conclusion :
*« Robust arithmetic is not always slower after all. »*

### 5.2 Les faces coplanaires sont la norme, pas le cas limite

C'est le chiffre le plus important de la section. Zhou, Grinspun, Zorin et
Jacobson ont mesuré la « propreté » des **10 000 maillages** de Thingi10K
(*Mesh Arrangements for Solid Geometry*, SIGGRAPH 2016, §7.1) :

| Propriété | % des modèles |
|---|---|
| Pas de bord ouvert | 88,6 % |
| Vertex-manifold | 77,6 % |
| Composante unique | 74,6 % |
| **Pas d'auto-intersection** | **54,7 %** |

Et surtout : sur les **4 524** maillages auto-intersectants, **3 082 ont des
auto-intersections coplanaires** — **68 %**. L'hypothèse de « position
générale », sur laquelle reposent QuickCSG, Cork et la plupart des
implémentations rapides, **est fausse sur les deux tiers des cas
problématiques réels**.

Le coût de ces dégénérescences n'est pas seulement la fausseté, c'est
l'explosion combinatoire. EMBER, figure 25 : l'intersection de 20 cubes
partageant exactement le même plan supérieur et inférieur — **240 triangles
d'entrée** — prend **58,3 secondes** avec la méthode de Zhou et al. (celle de
libigl), 5,6 s avec celle de Cherchi et al., et 5,9 **millisecondes** avec
EMBER. Une minute de calcul pour 240 triangles.

### 5.3 Les prédicats exacts ne sont pas chers — c'est le contresens habituel

Shewchuk (*Adaptive Precision Floating-Point Arithmetic and Fast Robust
Geometric Predicates*, Discrete & Computational Geometry 18(3), 1997) mesure
`orient3d` en microsecondes :

| Méthode | Coplanaire ou quasi | Facteur / flottant nu |
|---|---|---|
| Flottant nu | 0,25 | — |
| **Adaptatif, étage A** (le cas courant) | **0,62** | **× 2,4** |
| Adaptatif, étage D (exact atteint) | 27,29 | × 109 |
| Exact non adaptatif | 32,90 | × 133 |
| MPFUN (multiprécision classique) | 246,64 | × 987 |

**Le prix de la robustesse en régime nominal est un facteur 2,4, pas un facteur
100.** Sur une triangulation de Delaunay 2D complète d'un million de points, la
pénalité totale est de **+8 %** sur des points aléatoires et **+29 %** sur une
grille inclinée. En 3D c'est plus dur — +35 % sur l'aléatoire, ×11,4 sur une
surface de sphère — mais rappelons que sur la grille inclinée la version rapide
**ne rend aucun résultat**.

Et le filtre passe presque toujours : Cherchi et al. mesurent, sur un modèle
réel, `orient2d` appelé **3 102 360 fois** avec **71 échecs de filtre** —
**0,002 %**. Le chemin exact est emprunté par une fraction de pour-mille des
appels.

**Deux pièges d'intégration à connaître avant de s'engager :** ces algorithmes
exigent un contrôle strict de l'arrondi. Le dépôt d'Attene impose
`/fp:strict /Oi` sous MSVC et `-frounding-math -O2` sous GCC/Clang. **Un
`-ffast-math` global est rédhibitoire** — c'est une contrainte concrète sur le
`build.py` de l'addon, sur les unités de traduction concernées.

La licence des prédicats de Shewchuk est le **domaine public**, écrit tel quel
dans l'en-tête de `predicates.c`. ⚠️ Ne pas confondre avec *Triangle*, son autre
logiciel, qui est réservé à l'usage académique.

### 5.4 Le vrai problème n'est pas le prédicat, c'est la construction

Un prédicat exact garantit une réponse juste **sur les coordonnées qu'on lui
donne**. Il ne garantit rien sur le fait que ces coordonnées soient les bonnes.

Or un booléen **construit** des points d'intersection arête/triangle. Ce sont
des fractions rationnelles des coordonnées d'entrée — exactes en rationnel,
**jamais** représentables en double. Dès qu'on les arrondit, on injecte des
points qui ne sont plus sur les arêtes ni sur les plans dont ils sont
l'intersection. Les prédicats suivants répondent alors parfaitement à une
**mauvaise question**.

La documentation de CGAL le dit sur elle-même, et c'est la citation la plus
utile du dossier :

> [avec] exact predicates but inexact constructions, edges split at
> intersections… **the embedding of the output surface mesh might have
> self-intersections**.

**EPIC produit une topologie correcte et un plongement géométrique invalide.**

**Et il y a un problème spécifique à Clarisse.** Passer en EPEC — arithmétique
exacte de bout en bout, à la CGAL — coûte cher, mais surtout **n'est pas
parallélisable**. Zhou et al. l'écrivent : le comptage de références de
`CGAL::Lazy_exact_nt` rend l'accès concurrent *en lecture seule* dangereux, et
ils ont dû poser **un mutex par sommet du maillage**. Cherchi et al. enfoncent
le clou : encapsuler tout code utilisant les nombres CGAL dans une section
critique rend le code parallèle *« only slightly faster than its serial
counterpart »*.

Pour un moteur massivement multi-threadé à évaluation paresseuse, **un noyau
EPEC type CGAL est structurellement mal adapté**. Ce n'est pas seulement lent,
c'est non parallélisable.

**La sortie moderne est celle d'Attene** (*Indirect Predicates for Geometric
Constructions*, Computer-Aided Design 126, 2020) : ne **jamais** calculer les
coordonnées du point construit. On le représente par sa définition —
« intersection de la droite (a,b) avec le plan (c,d,e) » — et on réécrit chaque
prédicat pour qu'il opère sur cette définition, donc sur les seules données
d'entrée. Mesuré sur une triangulation d'un million de points à 100 % de points
implicites : **4,30 s / 654 Mo** contre **50,18 s / 1 606 Mo** pour CGAL EPEC —
**×11,7 en temps**. Et sur des points explicites, le surcoût de
l'infrastructure est de **1,7 %**. On ne paie que ce qu'on utilise. Ces points
implicites sont en outre **thread-safe**, ce que CGAL n'est pas.

### 5.5 Où en est la performance : le problème est résolu, dans la littérature

| Méthode | Thingi10K, moyenne géométrique | Exact ? |
|---|---|---|
| **EMBER** (2022) | **1,6 ms** | oui |
| QuickCSG | 17,9 ms | **non** |
| Cherchi et al. 2020 | 72,4 ms | oui |
| Cork | 138,8 ms | **non** |
| CGAL 5.4 corefinement | 710,9 ms | oui |
| Zhou et al. 2016 (libigl) | 3 163,9 ms | oui |
| CGAL 4.12 Nef | 8 570,1 ms | oui |

EMBER traite **1,2 million de triangles d'entrée en 34 ms** là où Zhou et al.
mettent 141 s — un facteur 4 150. Le vieux dilemme « exact ou rapide » n'existe
plus dans les papiers.

Il existe encore dans le code qu'on a le droit d'utiliser. **Le code d'EMBER
n'est pas publié** : la page projet indique qu'une implémentation « can be
acquired » auprès des auteurs. C'est une négociation, pas un `git clone`.

### 5.6 Le CSG itéré n'est pas résolu, et c'est ce qui devrait décider

Deux problèmes distincts, qu'il faut séparer.

**L'explosion combinatoire.** Zhou et al. mesurent sur dix tétraèdres :
extraire « la région intérieure à au moins 5 des 10 » par un arbre binaire
demande **1 259 opérations booléennes**, et *« the aggregation of complexity is
catastrophic, leading to performance measured in weeks »*. Extraire le même
résultat de leur **arrangement variadique** unique coûte le même prix que
l'union : quelques secondes. **Leçon directe : on n'enchaîne pas les booléens
en arbre, on calcule un arrangement et on en extrait ce qu'on veut.**

**L'accumulation d'erreur d'arrondi.** À la fin il faut bien écrire des `double`,
et arrondir un point d'intersection exact peut créer **de nouvelles**
auto-intersections. Le problème d'arrondir sans casser — le *3D snap rounding*
— était **ouvert jusqu'en 2025**, et l'arrondi de sommets est NP-difficile.

| Mesure | Source |
|---|---|
| L'arrondi naïf donne un complexe valide dans **85,2 %** des cas | Cherchi et al. 2020 |
| L'arrondi introduit des auto-intersections dans **2,19 %** des sorties | Zhou et al. 2016 |
| **9 425 / 9 997** modèles sans auto-intersection après arrondi naïf | Valque & Lazard 2025, intégré dans CGAL 6.1 |
| Heuristique itérée : **99,95 %** | Zhou et al. et Cherchi et al. |

**Ce que ça implique pour un enchaînement de 50 booléens** — et un artiste en
fera bien plus : à 99,95 % de réussite par opération, la probabilité que tout
tienne est de **97,5 %**. À 85 %, elle tombe à **0,03 %**. (Calcul à nous, en
supposant l'indépendance, ce qui est optimiste.)

La seule solution propre est de **rester en entiers à largeur fixe** entre les
opérations — ce que fait EMBER, dont la sortie peut resservir d'entrée sans
perte. C'est-à-dire, précisément, le code qui n'est pas publié.

Et ce n'est pas un problème théorique : SideFX l'écrit dans la documentation du
`Boolean` SOP de Houdini, un logiciel vendu :

> For procedural chaining, precision issues can cause microscopic
> self-intersections in Boolean's output.

Le nœud expose d'ailleurs *« Resolve self-intersections »*, *« Collapse tiny
seam-adjacent edges »* avec un seuil de longueur, et des points rouges dans le
viewport pour signaler les zones à problème.

### 5.7 Ce que font les DCC, et ce que Blender vient de décider

Le fait le plus instructif du dossier. Blender a passé **six ans** sur un
solveur booléen exact — celui de Howard Trickey, arithmétique rationnelle GMP
(`mpq_class`), filtre en double puis repli exact, intersection triangle-triangle
de Guigue & Devillers, triangulation de Delaunay contrainte pour les régions
coplanaires, algorithme de Zhou et al. 2016. Le solveur existe, il marche, il
est lent.

En **4.5 LTS (juillet 2025), Blender a intégré Manifold** — une bibliothèque
tierce **qui n'est pas exacte** — parce qu'elle est plus rapide et plus fiable
en pratique. Les notes de version disent qu'elle est *« usually as fast as, and
sometimes faster than, the float solver »*, avec une restriction nette : *« it
only works when all the arguments are manifold »*.

Manifold est l'œuvre d'Emmett Lalish, aujourd'hui rendering engineer chez Wētā
FX, et il a mis **huit ans** à sortir. Sa thèse n'est pas l'exactitude, c'est la
**cohérence** : la topologie est exacte, la géométrie est flottante, et
l'algorithme s'arrange pour que *« the same question is never asked in two
different ways »*. Il garantit une sortie manifold ; il ne garantit pas
l'exactitude géométrique, et il **exige une entrée manifold**.

Son argument contre les prédicats exacts est exactement le §5.4 :

> Exact predicates excel at determining truth about *existing* geometry but
> falter when new geometry is constructed.

OpenSCAD a fait le même chemin : CGAL Nef → CGAL corefinement (×10 à ×100, mais
avec une stratégie hybride de repli parce que corefinement *« crashes
(sometimes in unrecoverable ways) »*) → **Manifold** (×5 à ×30 de plus).

Et les éditeurs commerciaux ne cachent plus les limites. Maya, documentation
2025 : *« Booleans may fail if an intersection lands on overlapping
components »*, *« may fail or produce unexpected results if an intersection
lands on illegal geometry (non-manifold, lamina) »*. Sur l'ancien algorithme :
*« UV sets disappear during the boolean operation »*. ZBrush Live Boolean :
*« UV's from each SubTool are completely removed »*.

**Le transfert d'attributs est aussi structurant que la robustesse.** Les
méthodes qui conservent la correspondance polygone de sortie → polygone
d'entrée (EMBER, Cherchi, le Boolean SOP de Houdini) transfèrent les UV ; celles
qui reconstruisent la topologie (Nef, BSP purs, voie volumique) les détruisent.
Schmidt & Brochu (Autodesk Research) : *« Methods like CGAL and BSP-based
approaches completely re-tessellate output, causing problems when input meshes
have bound properties like UV-maps. »*

### 5.8 La licence décide, pas la technique

C'est la conclusion la plus utile de toute la section, et elle est
contre-intuitive.

| Bibliothèque | Licence réelle | Addon fermé ? |
|---|---|---|
| **Manifold** | **Apache-2.0**, aucune dépendance obligatoire | ✅ **oui, sans réserve** |
| **Geogram** (Lévy/Inria) | **BSD-3-Clause** | ✅ oui, si TetGen et Triangle sont désactivés |
| Prédicats de Shewchuk | **domaine public** | ✅ |
| cinolib | MIT | ✅ |
| **Cherchi 2020 / 2022** | MIT… mais tire **`Indirect_Predicates`** d'Attene, **LGPL et header-only** | ⚠️ **conformité quasi impossible** |
| MCUT | LGPL-3.0 **ou** commerciale | ⚠️ DLL séparée, ou chèque |
| Cork | LGPL, lien dynamique obligatoire | ⚠️ et **abandonné depuis 2016** |
| **libigl** (le booléen) | MPL-2.0 en surface, **GPL par CGAL** | ❌ |
| **CGAL** (booléens) | **GPL-3.0-or-later** ou commerciale | ❌ sans chèque |
| VolumeMesher, OpenMeshCraft | GPL-3.0 | ❌ |
| QuickCSG | non commercial | ❌ |
| **EMBER** | **aucun code publié** | ❌ |

Deux pièges à connaître :

- **libigl n'est pas la réponse, contrairement à ce qu'on croit.** Le booléen
  est `igl::copyleft::cgal::mesh_boolean`, et le `README` de ce sous-répertoire
  dit lui-même que les organisations développant du logiciel propriétaire
  doivent l'éviter ou acheter les licences. Les en-têtes CGAL du corefinement
  et de Nef portent `GPL-3.0-or-later OR LicenseRef-Commercial`. Le binaire est
  une œuvre dérivée de CGAL.
- **La LGPL en header-only est un piège.** `Indirect_Predicates` d'Attene — la
  brique qui rend le travail de Cherchi si rapide — est LGPL et **entièrement
  en en-têtes**. La LGPL n'autorise la liaison propriétaire qu'à condition que
  l'utilisateur puisse relier son binaire avec une version modifiée de la
  bibliothèque : DLL séparée, ou livraison des fichiers objets. Une bibliothèque
  header-only est inlinée dans le binaire — la condition est inapplicable.
  OpenCASCADE résout exactement ce cas avec un fichier d'exception explicite ;
  Attene n'en a pas. Son `README` invite en revanche à le contacter : *« In case
  this is an important building block for your project… please let us know. »*
  Une exception écrite règlerait la question.

Et le prix de la licence commerciale de CGAL **n'est pas public** — tarification
par composant, sur devis. Les 6 000 €/an affichés sont ceux de la licence
*recherche*, pas de la licence de développement industriel.

### 5.9 Le coût de la voie D, en repères réels

Aucune source ne donne d'estimation en homme-mois. Mais les repères sont nets :

- **Manifold : huit ans** avant publication, par un ingénieur qui en a fait son
  métier.
- **Blender : six ans**, un développeur senior dédié, puis **adoption d'une
  bibliothèque tierce**.
- **Cork : abandonné par son auteur** — *« I have zero time to work on the
  library »*, et son propre avertissement : utilisable *« for a research
  project, perhaps slightly less so for use in a product »*.
- **Dix publications majeures en dix ans**, dont trois à SIGGRAPH ou SIGGRAPH
  Asia, et le snap rounding 3D n'est traité qu'en juin 2025, partiellement.

**Écrire un booléen exact robuste de zéro est un chantier d'années-homme.**
Intégrer une bibliothèque existante est un chantier de jours à semaines.
L'arbitrage n'est pas technique, il est juridique.

Les deux seules briques qu'il serait raisonnable d'écrire soi-même : le
**wrapper** — conversion vers et depuis les structures Clarisse, transfert
d'attributs, gestion des instances — et éventuellement les prédicats de
Shewchuk, qui sont en domaine public et tiennent dans un fichier.

---

## 6. Voie B — le CSG au niveau du rayon, sur les BVH que Clarisse a déjà

C'est la piste la plus intéressante du document, et elle est née de
l'inventaire, pas d'une idée préalable.

### 6.1 L'idée

La voie A ne marche que sur des primitives analytiques. Mais Clarisse sait déjà
intersecter n'importe quelle géométrie — c'est son métier. Si notre
`intersect_primitive` pouvait **demander aux opérandes leurs intervalles**
plutôt que de les calculer, le CSG marcherait sur des maillages, des Alembic,
des USD, des volumes, des instances — tout.

Et il marcherait **sans jamais toucher à un maillage**. Pas d'arrangement, pas
d'arithmétique exacte, pas de faces coplanaires, pas d'arêtes quasi
dégénérées. Toute la classe de problèmes du §5 disparaît, parce qu'on ne
construit aucune géométrie : on compose des segments le long d'un rayon.

L'idée n'est pas neuve — c'est le **diagramme de Roth**, 1982, la façon dont on
faisait du CSG avant que les maillages ne prennent le dessus. Ce qui est neuf
est de l'appliquer à un moteur qui a déjà des BVH sur des milliards de
primitives.

### 6.2 Les précédents, et leurs limites documentées

Trois moteurs comparables le font déjà, et lire ce qu'ils avouent vaut mieux
que d'improviser.

| Moteur | Ce qu'il offre | Ce que la doc reconnaît |
|---|---|---|
| **RenderMan** | CSG au moment du rendu depuis une vingtaine d'années | — |
| **Arnold** | le shader `clip_geo` | *« Only a single clipping geo object can be used »* ; les clippers doivent être *« as watertight as possible »* ; *« self-intersecting geometry or intersecting clipping objects can cause artifacts »* ; les volumes ne sont pas gérés |
| **V-Ray** | `VRayClipper` | avec plusieurs clippers, *« all the objects get cut, but there is no surface being created on the cut, everything is hollow »* ; et côté Chaos : *« it is not easy to determine the correct face ID when clipping so that multi-sub works »* |
| **Clarisse** | **rien** | — |

Ces limites ne sont pas des accidents, ce sont les vraies difficultés de la
voie B, et elles sont toutes les trois **résolubles par le CSG à
intervalles** : un seul clipper parce qu'on ne compose que la première
intersection, une cavité creuse parce qu'on ne retourne pas la normale de la
paroi, un ID de face incertain parce qu'on ne propage pas le shading group de
l'opérande. La sonde du §2 fait déjà les trois correctement sur des sphères —
c'est ce qui rend l'extension crédible.

### 6.3 Ce qui est vérifié

Tous les symboles nécessaires sont **exportés** :

| Symbole | Ce qu'il donne |
|---|---|
| `ModuleGeometry::get_gas(GeometryOverride*)` | le BVH construit d'une géométrie amont |
| `GasObject::ray_hit(CtxEval&, CtxShader&, GeometryRaytraceData&, bool)` | lance un paquet de rayons dans ce BVH |
| `GasObject::ray_hit_nearest(...)` | la variante plus proche seulement |
| `ShaderHelpers::create_shader_ctx(CtxEval&, CtxShader&, ModuleLayerScene&)` | fabrique un contexte de shading |
| `GeometryRaytraceCtx::get_intersection_path(index)` | le chemin d'intersections d'un rayon |

Le motif de lecture d'une géométrie amont est déjà en production dans
`quilt.cpp` : `source->get_module<ModuleGeometry>()` →
`module->get_geometry(false)`. Il suffit de remplacer le dernier appel par
`get_gas()`.

Et l'existence même du mécanisme de transparence de Clarisse — `Alpha Depth`,
`Alpha Threshold`, un chemin d'intersections ordonné par rayon — prouve que le
moteur **sait déjà collecter plusieurs intersections le long d'un rayon**.
C'est exactement la primitive dont un CSG par intervalles a besoin.

### 6.4 Le seul point à trancher, et comment le trancher

`intersect_primitive` reçoit un `const CtxEval&` et un `GeometryRaytraceCtx&`.
Il ne reçoit **pas** de `CtxShader&`, que `GasObject::ray_hit` réclame.

C'est le même genre de blocage que celui identifié pour l'intégrateur
(`cb_pre_render` reçoit un `const CtxShader&`, donc pas de
`new_raytrace_ctx()` — voir `passation.md` §5). Et comme lui, **il se teste en
une demi-journée**, avant d'écrire une ligne d'algorithme :

1. dans `intersect_primitive`, obtenir un `CtxShader` — soit en le fabriquant
   par `ShaderHelpers::create_shader_ctx`, soit en le trouvant accessible depuis
   le `CtxEval` ;
2. appeler `ray_hit` sur le GAS d'un opérande avec un `GeometryRaytraceData`
   à nous ;
3. vérifier qu'on récupère bien **toutes** les intersections, entrantes et
   sortantes, et pas seulement la plus proche ;
4. vérifier que l'appel est réentrant — on est déjà dans une traversée de BVH
   quand on en lance une autre.

**Si les quatre passent, la voie D devient largement inutile** et le projet
change de nature : on obtient un booléen exact, sans maillage, sur n'importe
quelle géométrie, à la précision du moteur.

**Si le point 4 échoue** (réentrance), il reste une porte : faire le CSG non pas
dans `intersect_primitive` mais dans un **filtre d'intersection**
(`push_intersection_filter`), qui est appelé *pendant* la traversée avec
l'intersection en main. Le mécanisme existe — c'est celui qui a rejeté nos
fragments au §2.3 — mais il travaille sur un flux d'intersections déjà trouvées,
ce qui est moins direct.

### 6.5 Ce que ça ne donne pas

Un CSG au niveau du rayon **n'existe qu'au rendu**. Il n'y a pas de maillage à
exporter, pas de topologie à éditer, pas de UV cohérentes sur la surface de
coupe, et le viewport OpenGL ne montrera rien tant qu'on ne lui donne pas une
approximation. C'est un outil de rendu et de look, pas un outil de modélisation
au sens où l'entend un modeleur.

Pour Romain — artiste VFX qui compose des plans, pas un pipeline d'asset — c'est
possiblement exactement ce qu'il faut. Mais c'est une limite à énoncer avant, pas
après.

---

## 7. Ce que Clarisse change vraiment par rapport à Blender ou Houdini

Romain pense que sa gestion des polygones change la donne. **La réponse est oui,
mais pas par le mécanisme qu'on croirait.**

### 7.1 Ce qui ne change rien

Un booléen exact sur deux maillages est un problème d'arithmétique, pas
d'architecture. Que le moteur encaisse un milliard de polygones ne rend pas plus
robuste le test d'orientation de quatre points quasi coplanaires. **Sur la voie
D, Clarisse n'a aucun avantage.** Il en a même un inconvénient : les échelles où
il travaille sont exactement celles où les prédicats flottants cassent le plus.

### 7.2 Ce qui change tout

**Le point d'entrée non tessellé est ouvert.** C'est ça, la vraie différence, et
c'est vérifié. Dans Blender, une géométrie est un `Mesh` : pour ajouter une
primitive rendue sans maillage, il faut modifier Cycles. Dans Houdini, un
booléen produit forcément des polygones ou un volume. Dans Clarisse, une
`GeometryObject` tierce est un citoyen de première classe : elle reçoit le BVH,
l'instanciation, les shading groups, le displacement, le motion blur, sans une
ligne de code de notre part.

**L'échelle est réelle et elle est mesurée.** `GasSceneTree::get_primitive_count()`
rend un `double`, pas un entier 32 bits — donc des entiers exacts jusqu'à 2⁵³.
`GasGeometryBundle` prend des tableaux parallèles d'indices, de matrices et de
visibilités plutôt qu'un objet par instance, accepte un
`set_matrices_callback` pour générer les matrices à la demande, et hérite de
`GasObject` : **les bundles s'imbriquent**. Imbriquer coûte le produit des
nœuds, pas celui des feuilles (voir `sdk-clarisse.md` §10).

Conséquence directe pour le CSG : **un nœud de CSG implicite est instanciable
des millions de fois pour le prix d'un**. Un boulon percé, une brique ébréchée,
un tuyau troué — définis une fois analytiquement, dispersés par un scatterer,
sans jamais matérialiser un triangle. Aucun autre logiciel ne peut faire ça,
parce qu'aucun autre n'a à la fois l'instanciation à cette échelle et un point
d'entrée géométrique ouvert.

**Vérifié, pas déduit.** Un `SceneObjectScatterer` a été branché sur la sonde du
§2, avec une `GeometryPolygrid` comme support. Il l'a accepté sans rien de
particulier : `get_geometry_count()` rend 9, et le rendu montre neuf sphères
mordues, chacune correctement éclairée. **Le scatterer de Clarisse instancie une
géométrie qui n'a pas un seul sommet.** C'est le point où la réponse à la
question de Romain — « Clarisse avec sa gestion des polygones pourrait être
assez ouf » — devient oui, en corrigeant l'énoncé : ce n'est pas la gestion des
polygones, c'est la gestion de ce qui n'en est pas.

**L'évaluation paresseuse par ressources joue dans le bon sens.** Un
`create_resource(RESOURCE_ID_GEOMETRY)` n'est appelé que lorsqu'un attribut
déclaré sale dans `set_resource_attrs` a changé. Un arbre CSG profond ne se
recalcule donc pas à chaque image — ce qui est exactement ce qu'il faut quand
l'évaluation est coûteuse. C'est déjà vérifié en production sur `quilt` et
`tube`.

### 7.3 Ce qui joue contre

- **Pas de SDK.** Chaque en-tête est reconstruit, chaque `.icc` perdu coûte une
  réimplémentation devinée — le `GeometryMediumDescriptor` du §2.3 en est un
  exemple direct, et il a coûté six rendus pour un seul champ.
- **Pas d'outils de modélisation.** Les quinze classes `Tool*` sont
  inventoriées : aucune ne trace, aucune ne sculpte. Un système CSG dans
  Clarisse serait piloté par des **nœuds et des attributs**, pas par des gestes.
  Ça correspond à la culture de l'outil, mais ça exclut d'emblée le
  « modeling » au sens de Plasticity ou de Modeler.
- **Pas de topologie éditable.** Il n'y a aucune structure de demi-arêtes, aucun
  historique de sélection de faces, aucun opérateur de maillage. Tout ce qu'un
  booléen sur maillages exigerait en amont et en aval serait à écrire.

---

## 8. Recommandation

### 8.1 Par quoi commencer

**Le premier nœud livrable en quelques jours : `GeometryCsg`, en voie A.** Un
nœud implicite qui expose un petit arbre CSG sur des primitives analytiques.

Ce qu'il contient au minimum, et rien de plus :

- **Six primitives à intersection fermée** : sphère, boîte, cylindre, cône,
  capsule, tore. Toutes ont une solution analytique connue et bornée.
- **Trois opérateurs** : union, intersection, différence — plus le **chanfrein**
  et le **congé** (arête cassée / arrondie), qui sont ce qu'un artiste demande
  en premier et qui restent analytiques tant qu'on les fait sur l'arête et non
  par blend de champ.
- **Une primitive Clarisse par feuille de l'arbre**, pas une seule primitive
  globale : c'est ce qui donne le BVH gratuitement et évite le mur des
  « quelques centaines de nœuds ».
- **Les UV par projection** — planaire, cylindrique, sphérique, triplanaire.
  Sur une surface analytique, elles sont exactes et gratuites ; il n'y a pas de
  dépliure à préserver puisqu'il n'y a jamais eu de maillage.
- **Un shading group par opérande**, pour qu'on puisse donner un matériau
  différent à la surface de coupe. C'est ce qui fait la différence entre un
  jouet et un outil : la paroi d'une morsure n'a pas la même matière que la
  peau.

Ce nœud est utile tout seul, avant tout CSG avancé : Clarisse n'a **pas de cône,
pas de tore, pas de capsule, pas de chanfrein**, et ses trois implicites ne se
combinent pas.

### 8.2 Immédiatement après : la sonde de la voie B

Une demi-journée, avant toute autre ligne. Les quatre points du §6.4. Le résultat
décide de tout le reste du projet :

- **Si ça passe** : on écrit `GeometryCsgMesh`, un booléen exact sur n'importe
  quelle géométrie, et la voie D est abandonnée.
- **Si ça ne passe pas** : on reste sur A pour les primitives, et on tranche
  entre C (VDB, rapide et sale) et D (exact et cher) selon ce que Romain veut
  vraiment faire.

### 8.3 Si un jour on doit vraiment faire la voie D

La décision est juridique avant d'être technique, et elle se réduit à deux
candidats :

- **Manifold** (Apache-2.0, aucune dépendance obligatoire, vivant, adopté par
  Blender, OpenSCAD et Godot) — le choix par défaut. Sa contrainte : il **exige
  une entrée manifold**, ce qui est une vraie restriction sur de la géométrie
  de production (scans, Alembic, empilements de scatter). Il garantit la
  topologie, pas l'exactitude géométrique.
- **Geogram** (BSD-3, Bruno Lévy/Inria, très actif, CSG exact) — le second
  choix, à condition de désactiver TetGen (AGPL) et Triangle (non commercial).

Tout le reste est fermé : libigl passe par CGAL donc par la GPL, le code de
Cherchi est MIT mais tire les prédicats LGPL header-only d'Attene, EMBER n'est
pas publié, VolumeMesher et OpenMeshCraft sont GPL, QuickCSG est non commercial.

Et dans tous les cas : **ne pas enchaîner les booléens en arbre**. Un
arrangement variadique unique remplace 1 259 opérations binaires — des secondes
au lieu de semaines.

### 8.4 Ce qui serait un piège

- **Commencer par la voie D.** C'est le réflexe — « un booléen, c'est un booléen
  sur des maillages » — et c'est le chemin le plus long, le plus risqué, et
  celui où Clarisse n'apporte rien. Il ne faut y aller que si B est fermée
  *et* si Romain a besoin d'un maillage en sortie.
- **Passer au champ de distance composé.** Dès qu'on remplace l'intersection
  analytique par un `min`/`max` sur des distances, on entre dans le domaine du
  sphere tracing, avec son facteur 10 à 100 sur le coût par rayon et son
  problème de constante de Lipschitz. La tentation viendra du blend lisse. **Il
  faut la garder pour un mode explicite et séparé**, jamais comme comportement
  par défaut.
- **Faire du displacement sur une surface implicite.** Les attributs existent
  sur `GeometrySphere`, mais un displacement change la boîte englobante de la
  primitive — et notre `compute_primitive_bbox` doit alors la majorer, ce qui
  détruit l'efficacité du BVH. À traiter explicitement ou à interdire.
- **Croire que `GeometryVolumeSurface` fait la moitié du travail.** Elle fait un
  brouillard, pas une surface. Le §1.4 le documente pour que ça ne se
  reperde pas.
- **Oublier le viewport.** Un nœud implicite n'a rien à montrer en OpenGL. Il
  faudra une représentation d'aperçu — la boîte englobante au minimum, une
  tessellation grossière au mieux — sans quoi l'objet est invisible tant qu'on
  ne rend pas, et l'outil est inutilisable en set-dressing.
- **Partir sur libigl parce que « c'est la référence ».** C'en est une, et son
  booléen est sous GPL par CGAL interposé. Trois heures de lecture de licences
  avant la première ligne évitent d'avoir à tout jeter.
- **Activer `-ffast-math` globalement dans `build.py`.** Si un jour on lie des
  prédicats exacts, l'optimisation qui supprime la soustraction calculant le
  terme d'erreur détruit l'algorithme silencieusement. Il faut `/fp:strict` sur
  ces unités-là.

---

## 9. Le coût, sans arrondir

| Chantier | Estimation | Confiance |
|---|---|---|
| Sonde de la voie B (§6.4) | **une demi-journée** | haute — c'est de la mesure |
| `GeometryCsg` voie A, six primitives, trois opérateurs, UV, shading groups | **3 à 5 jours** | haute — la sonde a déjà fait le plus dur |
| Chanfreins et congés analytiques corrects sur les arêtes | **1 à 2 semaines** | moyenne — c'est la partie mathématiquement délicate |
| Aperçu viewport correct | **2 à 4 jours** | moyenne |
| Voie B complète si la sonde passe (`GeometryCsgMesh`) | **2 à 4 semaines** | basse — dépend entièrement de ce que la sonde révèle |
| Voie C, chaîne VDB complète (en-têtes, import lib, maillage → level set → CSG → maillage, reconstruction des arêtes) | **3 à 6 semaines** | moyenne — le liage est vérifié, le reste est du travail connu |
| Voie D, en intégrant **Manifold** (wrapper, transfert d'attributs, gestion des instances, réparation d'entrée) | **3 à 6 semaines** | moyenne |
| Voie D, écrite de zéro | **des années-homme** | — ne pas le faire |

**La franchise demandée** : un « système de modélisation CSG » complet, au sens
d'un outil où on modélise vraiment, c'est **plusieurs mois**. Et si on entend
par là un booléen exact sur maillages écrit maison, ce sont des **années** —
c'est la conclusion que tirent, chacun de son côté, l'auteur de Manifold (huit
ans), la Blender Foundation (six ans puis abandon au profit d'une bibliothèque
tierce) et l'auteur de Cork (abandon pur et simple).

Mais ce n'est pas la bonne façon de poser la question. Un **premier nœud CSG
implicite utile et livrable existe à moins d'une semaine**, et il fait déjà
quelque chose que Clarisse ne sait pas faire — et qu'aucun autre moteur ne fait
avec cette instanciation. Le reste se décide après la sonde de la voie B, qui
coûte une demi-journée et qui peut diviser le projet par trois.

---

## 10. Ce qui a été vérifié, et ce qui a seulement été lu

**Vérifié en exécutant du code sur cette machine :**

- les 359 classes du factory, leur arbre, leurs licences, leurs attributs
  (`native/tests/csg_probe.py`, sortie dans `J:\_WINDOWSTEMP\claude\csg_probe.txt`) ;
- l'absence totale de booléen, CSG, metaball, sculpt, remesh ;
- `SceneObjectCombiner` n'a qu'un attribut propre, `objects` ;
- `GeometryVolumeBake` et `GeometryVolumeGenerator` sont verrouillées en iFX ;
- `Geometry`, `GeometryVolume`, `SceneObjectTree` et `Displacement` se dérivent
  toutes les quatre (sonde `cmagen`) ;
- **un `GeometryObject` maison se compile, se lie, s'instancie, reçoit un BVH,
  est intersecté et est ombré** — la sonde `native/csg_sonde/` rend les trois
  booléens ;
- `GeometryMediumDescriptor::clear()` doit poser `opacity = 1` ;
- **le `SceneObjectScatterer` natif instancie cette géométrie implicite** —
  neuf instances construites et rendues, sans rien de particulier ;
- OpenVDB **7.0** est livré, exporte 1 139 symboles, et
  `openvdb::v7_0::initialize()` **se lie et s'exécute** depuis du code à nous ;
- `GasObject::ray_hit`, `ModuleGeometry::get_gas` et
  `ShaderHelpers::create_shader_ctx` sont exportés.

**Lu dans les en-têtes ou la documentation livrée, non exécuté :**

- `ModuleDeformerTopology` fixe son `point_count` à la construction — donc un
  déformeur ne change pas la topologie ;
- la description des implicites (« directly ray traced without relying on
  tessellation ») ;
- le comportement des clip maps ;
- `GeometryVolumeSurface` produit un volume de densité, pas un level set.

**Lu dans la littérature, jamais mesuré ici :** tous les chiffres des §3, §4 et
§5 — Hart, Museth, Quilez, Barbier et al., Evans, et les papiers de booléens
exacts. Ils sont cités avec leur source ; aucun n'a été reproduit sur cette
machine.

**Non vérifié et important :** la réentrance de `GasObject::ray_hit` depuis
`intersect_primitive`, qui décide de la voie B. C'est la première chose à faire.

**À revérifier avant de s'engager :** les licences du §5.8 ont été lues dans les
fichiers `LICENSE` et les en-têtes SPDX des dépôts, pas validées par un
juriste. Deux points restent ouverts — le prix de la licence de développement
industriel de CGAL n'est pas public, et `Indirect_Predicates` porte le texte de
la LGPL 2.1 dans son `LICENSE` mais annonce la v3 dans son `README`.

---

## 11. Sources

**Clarisse** — tout vient de l'installation locale : `csg_probe.py` sur le
factory, les en-têtes reconstruits de `J:\Clarisse-SDK\include\`, les `.def` de
`J:\Clarisse-SDK\lib\`, la documentation livrée dans
`Clarisse\docs\using-clarisse\scene-items\geometries\` et
`Clarisse\docs\rendering-in-clarisse\raytracer\`.

**CSG implicite et SDF**
- Hart, *Sphere Tracing*, The Visual Computer 12(10), 1996 —
  <https://graphics.stanford.edu/courses/cs348b-20-spring-content/uploads/hart.pdf>
- Frisken, Perry, Rockwood, Jones, *Adaptively Sampled Distance Fields*,
  SIGGRAPH 2000 —
  <https://graphics.stanford.edu/courses/cs468-03-fall/Papers/frisken00adaptively.pdf>
- Ju, Losasso, Schaefer, Warren, *Dual Contouring of Hermite Data*, SIGGRAPH
  2002 — <https://www.cs.rice.edu/~jwarren/papers/dualcontour.pdf>
- Schaefer, Ju, Warren, *Manifold Dual Contouring*, IEEE TVCG 13(3), 2007 —
  <https://www.cs.wustl.edu/~taoju/research/dualsimp_tvcg.pdf>
- Museth, *VDB: High-Resolution Sparse Volumes with Dynamic Topology*, ACM TOG
  32(3), 2013 — <https://www.museth.org/Ken/Publications_files/Museth_TOG13.pdf>
- Barbier et al., *Lipschitz Pruning*, CGF 44(2), Eurographics 2025 —
  <https://wbrbr.org/publications/LipschitzPruning/documents/LipschitzPruning_submitted_to_EG25.pdf>
- Galin et al., *Segment Tracing*, CGF 39(2), 2020 —
  <https://aparis69.github.io/public_html/projects/galin2020_Segment.html>
- Quilez, *Smooth minimum* et *Interior SDFs* —
  <https://iquilezles.org/articles/smin/> ·
  <https://iquilezles.org/articles/interiordistance/>
- Evans, *Learning from Failure* (Dreams), SIGGRAPH 2015 —
  <http://media.lolrus.mediamolecule.com/AlexEvans_SIGGRAPH-2015-sml.pdf>
- OpenVDB — <https://www.openvdb.org/about/> ·
  <https://github.com/AcademySoftwareFoundation/openvdb>
- Houdini VDB — <https://www.sidefx.com/docs/houdini/nodes/sop/vdbfrompolygons.html>

**Booléens sur maillages**
- Shewchuk, *Adaptive Precision Floating-Point Arithmetic and Fast Robust
  Geometric Predicates*, DCG 18(3), 1997 —
  <https://www.cs.cmu.edu/~quake/robust.html>
- Kettner, Mehlhorn, Pion, Schirra, Yap, *Classroom Examples of Robustness
  Problems in Geometric Computations*, Comp. Geom. 40(1), 2008
- Jacobson, Kavan, Sorkine-Hornung, *Robust Inside-Outside Segmentation using
  Generalized Winding Numbers*, TOG 32(4), 2013 —
  <https://igl.ethz.ch/projects/winding-number/>
- Zhou, Grinspun, Zorin, Jacobson, *Mesh Arrangements for Solid Geometry*,
  SIGGRAPH 2016 —
  <https://www.cs.columbia.edu/cg/mesh-arrangements/mesh-arrangements-for-solid-geometry-siggraph-2016-compressed-zhou-et-al.pdf>
- Attene, *Indirect Predicates for Geometric Constructions*, CAD 126, 2020 —
  <https://arxiv.org/abs/2105.09772> ·
  <https://github.com/MarcoAttene/Indirect_Predicates>
- Cherchi, Livesu, Scateni, Attene, *Fast and Robust Mesh Arrangements using
  Floating-point Arithmetic*, SIGGRAPH Asia 2020 —
  <https://www.gianmarcocherchi.com/pdf/mesh_arrangement.pdf>
- Cherchi, Pellacini, Attene, Livesu, *Interactive and Robust Mesh Booleans*,
  SIGGRAPH Asia 2022 — <https://arxiv.org/abs/2205.14151>
- Nehring-Wirxel, Trettner, Kobbelt, *Fast Exact Booleans for Iterated CSG using
  Octree-Embedded BSPs*, CAD 135, 2021 — <https://arxiv.org/abs/2103.02486>
- Trettner, Nehring-Wirxel, Kobbelt, *EMBER*, SIGGRAPH 2022 —
  <https://www.graphics.rwth-aachen.de/media/papers/339/ember_exact_mesh_booleans_via_efficient_and_robust_local_arrangements.pdf>
- Diazzi & Attene, *Convex Polyhedral Meshing for Robust Solid Modeling*, TOG
  40(6), 2021 — <https://arxiv.org/abs/2109.14434>
- Lévy, *Exact predicates, exact constructions and combinatorics for mesh CSG*,
  arXiv:2405.12949 / TOG 2025 — <https://arxiv.org/abs/2405.12949>
- Valque & Lazard, *Removing self-intersections… while preserving floating-point
  coordinates*, CGF 44(5), 2025 —
  <https://www.cgal.org/2025/06/13/autorefine-and-snap/>
- Schmidt & Brochu (Autodesk Research), *Adaptive Mesh Booleans*,
  arXiv:1605.01760

**Bibliothèques**
- Manifold — <https://github.com/elalish/manifold> ·
  <https://elalish.blogspot.com/2022/02/introducing-my-manifold-library.html>
- Geogram — <https://github.com/BrunoLevy/geogram>
- CGAL, corefinement et booléens —
  <https://doc.cgal.org/5.6/Polygon_mesh_processing/index.html> · licences
  <https://geometryfactory.com/products/licenses/>
- libigl, sous-répertoire `copyleft` —
  <https://github.com/libigl/libigl/blob/main/include/igl/copyleft/README.md>
- Cork — <https://github.com/gilbo/cork> · MCUT —
  <https://github.com/cutdigital/mcut>

**Documentation d'éditeurs**
- Houdini Boolean SOP —
  <https://www.sidefx.com/docs/houdini/nodes/sop/boolean.html>
- Maya, limites des booléens —
  <https://help.autodesk.com/cloudhelp/2025/ENU/Maya-Modeling/files/GUID-302821C5-343C-4F2B-8228-C5333896B207.htm>
- ZBrush Live Boolean, préservation des données —
  <https://help.maxon.net/zbr/en-us/Content/html/user-guide/3d-modeling/modeling-basics/creating-meshes/live-boolean/data-preservation/data-preservation.html>
- Arnold `clip_geo` —
  <https://help.autodesk.com/view/ARNOL/ENU/?guid=arnold_core_ac_clip_geo_html>
- V-Ray `VRayVolumeGrid` — <https://docs.chaos.com/display/VMAYA/VRayVolumeGrid>
- Blender, solveur exact — `source/blender/blenlib/intern/mesh_intersect.cc`
