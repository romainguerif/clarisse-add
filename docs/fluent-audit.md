# Fluent, lu pour Clarisse

Audit d'un addon Blender commercial — *Fluent : Power Trip*, de Rudy Michau
(CG Thoughts) — dans un seul but : trouver ce qui mérite d'exister chez nous, et
sous quelle forme.

Écrit le 2026-09-08. Le code lu est la version **4.3.2**, extraite dans
`J:\_WINDOWSTEMP\claude\fluent\`.

**Cadre.** Le manifeste déclare `SPDX:GPL-2.0-or-later` et l'en-tête des sources
dit GPL v3 ou ultérieure — comme tout addon Python de Blender, qui n'a pas le
choix. Les techniques y sont donc publiques par construction et la lecture est
au-dessus de tout soupçon. Ça ne change rien à la règle qu'on s'est donnée :
**on ne recopie rien**. Ce document décrit des mécanismes avec nos mots. De
toute façon, un script qui orchestre des modificateurs Blender n'a rien à donner
à un node C++ de Clarisse au-delà de ses idées — et ce sont les idées qui
valent.

**Deux sujets sont hors périmètre, traités ailleurs** : le panneau de tissu
(`cloth-clarisse.md`, chantier de Romain) et la robustesse des booléens —
arithmétique exacte, prédicats, coplanarité — qui fait l'objet de
`csg-clarisse.md`. Le §3.9 se contente de noter ce que Fluent fait et renvoie
là-bas.

---

## 1. Ce qu'est Fluent

31 551 lignes de Python, 40 opérateurs, 94 icônes, une interface traduite en
sept langues, et une dizaine de `.blend` d'assets embarqués. Blender 5.2
minimum — c'est un code récent, calé sur l'API nommée des Geometry Nodes
(`modifier.properties.inputs.Socket_N.value`).

Le cœur n'est pas là où on l'attendrait. `modifiers.py` fait 6 547 lignes et ne
contient **aucune classe enregistrable auprès de Blender** : ce sont vingt-cinq
contrôleurs modaux, un par paramètre réglable à la souris. Il n'y a presque
aucune mathématique dans tout l'addon — `math_functions.py` fait 39 lignes et
réimplémente le produit scalaire. **La valeur de Fluent n'est pas géométrique,
elle est organisationnelle** : savoir dans quel ordre empiler vingt
modificateurs Blender pour que le résultat reste prévisible et réajustable après
cinquante coupes.

Deux moitiés :

- **le flux booléen** — dessiner une forme sur une surface et obtenir
  immédiatement une coupe ré-éditable, plus tout ce qu'il faut pour survivre à
  cinquante coupes : chanfreins, réparation de normales, exploration,
  duplication, synchronisation ;
- **Power Trip** — les générateurs : tuyaux, câbles, chaînes, grilles ajourées,
  vis et rivets, tôles, texte, cylindre dépliable. C'est le palier payant, et le
  verrou est un test d'existence du dossier `power_trip/`.

Une observation qui commande tout le reste : sur les quinze types de modificateurs
Blender que Fluent utilise, six — `BOOLEAN`, `BEVEL`, `SOLIDIFY`, `ARRAY`,
`MIRROR`, `SCREW` — **n'ont strictement aucun équivalent dans Clarisse**. Le
recensement complet des occurrences, tous fichiers confondus : `MIRROR` 37,
`NODES` 35, `BOOLEAN` 34, `BEVEL` 30, `ARRAY` 27, `DISPLACE` 25, `SOLIDIFY` 15,
`CURVE` 13, `WEIGHTED_NORMAL` 8, `SCREW` 7, `DECIMATE` 7, `SUBSURF` 6,
`SIMPLE_DEFORM` 6, `WELD` 5, `TRIANGULATE` 2.

---

## 2. Ce que Clarisse a en face

C'est la moitié du travail d'audit, et il faut la poser avant de juger quoi que
ce soit. Tout ce qui suit a été vérifié sur l'installation 5.0 SP14 et le SDK
reconstruit, pas déduit.

### 2.1 Ce qui n'existe pas

**Aucun CSG. Nulle part.** Les 346 classes de la documentation de référence ont
été passées en revue : il n'existe ni `GeometryBoolean`, ni `CombinerBoolean`,
ni rien d'approchant. `SceneObjectCombiner` **fusionne des objets de scène en un
seul pour le rendu** — sa documentation le dit mot pour mot — ce n'est pas une
opération ensembliste sur des maillages. Et une recherche du mot « boolean »
dans toute la documentation utilisateur ne rend que le **type de données**.

Manquent aussi, et la liste compte : le bevel, le solidify, l'array, le miroir,
la révolution, l'extrusion, l'inset, le texte, le dépliage UV, les groupes de
sommets, le mode édition, et toute sélection de composants persistante.

**Trois déformeurs livrés**, en tout et pour tout : `DeformerDisplacement`,
`DeformerFlatten`, `DeformerMddPlayer`. Toute la famille taper / twist / bend /
lattice est absente.

**Et une contrainte d'architecture qu'il faut connaître avant de concevoir quoi
que ce soit** : `ModuleDeformerTopology` — l'objet que reçoit `pre_deform` — ne
porte que le nombre de points, en lecture seule, plus deux drapeaux et
l'échantillonnage temporel. Il n'a **aucun moyen de changer le nombre de
points**. Autrement dit : **un déformeur ne peut pas changer la topologie**.
Tout ce qui ajoute ou retire de la géométrie doit être un node dérivé de
`GeometryPolymesh` qui lit ses entrées et reconstruit.

### 2.2 Ce qui existe, et qu'on a déjà prouvé

**Un node peut lire le maillage d'une géométrie d'entrée.** Ce n'est pas une
hypothèse : `quilt.cpp` le fait tous les jours. La chaîne est
`get_module<ModuleGeometry>()` → `get_geometry(false)` → `cast<PolyMesh>` →
`get_point_cloud()` pour les positions, plus `get_polygon_vertex_indices`,
`get_polygon_vertex_count`, `get_polygon_shading_groups`. `PolyMesh` donne aussi
les jeux d'UV (indexés ou par polygone), les color maps, les normal maps, les
creases et la visibilité par arête.

**Un node peut avoir plusieurs entrées géométriques** : `cable_field.cpp` en lit
deux. Un node booléen à deux entrées est donc structurellement acquis.

**Un node peut écrire** : `PolyMesh::set()` prend positions, vélocités, indices,
comptes par polygone, **shading groups par polygone avec leurs noms**, jeux
d'UV, normal maps, color maps. Plus `set_creases()` et
`set_primitive_edge_visibility()`.

**`GeometryPolymesh` porte une subdivision complète** : `subdivision_mode`,
`smoothing_mode`, `smoothing_angle`, `uv_interpolation`, `crease_mode`, et
surtout une tessellation **pilotée par les creases** —
`crease_driven_subdivision_offset`, `crease_driven_min_subdivision`,
`crease_driven_max_subdivision`. Plus le displacement, sept attributs.

**Un `Tool` reçoit tout ce qu'il faut pour un geste.** `pen.cpp` déclare
`cb_draw_tool` (2D), `cb_draw_tool_3d` (OpenGL immédiat), `enter_tool`,
`leave_tool` et `process_event`. Et `CtxToolEvent` porte `mouse.x/y/dx/dy`, les
trois boutons avec leur état pressé, la touche clavier, et **tous les
modificateurs** — shift, ctrl, alt, meta, caps, num, scroll. Réserve honnête :
la plume n'a jamais été essayée en interactif, `cnode` n'ayant pas de viewport.
La structure est là, le geste reste à valider.

**Une texture peut lancer des rayons.** `TextureCurvature` le fait, sa
documentation l'écrit (« computed through ray-casting to get a smoother result »)
et elle expose pour cela `radius`, `bias`, `sample_count`, un `Group` de
géométries, `intersection_mode` et `sidedness`. Trois sorties : courbure
gaussienne, moyenne, ou échantillonnée. C'est la preuve que le motif est
supporté par le moteur.

**Une texture peut piloter une géométrie.** `ShaderHelpers::evaluate_support_texture`
et `evaluate_vertices_texture` sont exportées — signature lue dans
`ix_shader.def`, jamais appelée par nous. Elles évaluent une texture en chaque
point d'un nuage ou en chaque sommet d'une géométrie et rendent un RGBA par
point.

**Le scatter.** Le `SceneObjectScatterer` disperse sur **les sommets** d'une
géométrie support. Les nuages viennent de `GeometryPointCloud` (densité,
échantillonnage par importance piloté par texture, décimation par texture),
`GeometryPointArray` (grille régulière 3D, décimation par texture),
`GeometryPointUvSampler` et `GeometrySceneItemCloud`. Variance de position, de
rotation et d'échelle, avec un pas de quantification par axe.

**Les `Group` sont réactifs et réglés par règles** : `inclusion_rule`,
`exclusion_rule`, `result_filter`, `update_mode` en Automatique. Un groupe se
remplit tout seul.

**Les `ShadingLayer`** sont des règles d'affectation de matériau, hiérarchiques.

**L'instanciation tient des échelles sans commune mesure** — voir
`sdk-clarisse.md` §10.

**Le type d'attribut `action`** pose un bouton sur le node.

### 2.3 Ce que ça coûte d'écrire un node

Nos modules livrés vont de 265 lignes (`curve_points`) à 1 782 (`bokeh`).
`tube.cpp` fait 616 lignes, `quilt.cpp` 1 246, `cable_field.cpp` 416. Le noyau
partagé `curve_core.h` en fait 1 114. C'est l'étalon à garder en tête quand on
lit « faisable » plus bas.

---

## 3. Le flux booléen

### 3.1 Le modèle de données

Un booléen chez Fluent, c'est un modificateur `BOOLEAN` sur l'objet **cible**,
pointant vers un objet **coupeur** distinct. Tout le reste est convention :

- le coupeur est renommé `<cible>_bool`, parenté à la cible, délié de la scène
  et lié à une collection dédiée (`Bool_Objects` par défaut), passé en filaire,
  invisible au rendu, à la caméra et aux ombres ;
- le modificateur s'appelle `.f_boolean` — le point initial le masque des listes
  de Blender tout en le rendant adressable par le code ;
- l'état sémantique vit sur le **coupeur**, pas sur la cible : une propriété
  personnalisée `fluent_operation` qui vaut `CUT`, `SLICE` ou `INSET`, plus
  `fluent_slice` / `fluent_inset` qui pointent vers les objets satellites que
  ces modes fabriquent.

La clé maîtresse est `fluent_type` — 51 occurrences dans le code, une quinzaine
de valeurs (`box`, `poly`, `path`, `prism`, `sphere`, `revolver`, `plate`,
`circular`, `pipe`, `wire`, `head_screw`, `grid`, `unknow`). **Un objet qui ne
la porte pas est invisible pour toute la machinerie.** À côté : `fluent_id`
(identifiant de groupe pour la synchronisation), `fluent_auto_res`,
`fluent_pipe_data` et `fluent_wire_data` — ces deux derniers étant des
dictionnaires stockant le tracé pour permettre la ré-édition.

**Il n'existe aucun historique sérialisé.** L'historique *est* la pile de
modificateurs plus le graphe d'objets. Le lien inverse coupeur → cible n'est pas
stocké : il est retrouvé par balayage de tous les objets visibles à chaque
besoin. C'est robuste au renommage, au copier-coller et à l'undo ; c'est coûteux,
et ça casse dès qu'une cible est cachée.

### 3.2 L'ordre, qui est la vraie invention

`Tools/constants.py` ne contient aucune fonction : que des tables. La principale
est `fluent_stack_order`, un dictionnaire `fluent_type` → liste ordonnée de
rôles. Pour un `box`, vingt-quatre entrées, dans cet ordre :

```
scale, rotate, taper_x, taper_y, first_bevel, first_solidify, taper_z,
pre_second_bevel_bottom, second_bevel_bottom, pre_second_bevel_top,
second_bevel_top, second_solidify, boolean, pre_chamfer, chamfer, frame,
array_x, array_y, array_z, center_array_x, center_array_y, center_array_z,
circular_array, mirror, auto_smooth
```

Trois choses s'y lisent d'un coup d'œil, et chacune est une décision : le
**taper Z arrive après le solidify** (il agit sur le volume, pas sur le profil) ;
le **booléen arrive avant le chanfrein et le cadre** ; les **arrays et le miroir
sont toujours en dernier**, ce qui garantit qu'un miroir posé au début reste
valable après dix coupes.

`place_in_stack()` insère un nouveau modificateur juste **avant le premier
modificateur qui doit logiquement le suivre**, en balayant la pile réelle à
l'envers. Ça marche même quand la pile est incomplète. La comparaison se fait
par sous-chaîne, ce qui absorbe naturellement les suffixes `.001` de Blender.

Trois autres tables complètent le dispositif : les modificateurs qui *changent
la dimension* (pour mesurer une taille brute en les neutralisant), ceux à
**ignorer lors d'un « reuse »** (tout ce qui est placement et non forme : scale,
rotation, arrays, miroir), et une matrice d'incompatibilité par type — une
sphère exclut quatorze modificateurs.

**Retenir ceci, parce que c'est la conclusion la plus utile de tout l'audit :
ces ~2 500 lignes résolvent un problème que Clarisse n'a pas.** Chez nous le
graphe *est* l'ordre. Voir §6.C.1.

### 3.3 Le cutter — dessiner sur une surface

C'est le geste central, et il est bien conçu.

**Le plan de travail est un vrai objet mesh caché dans la scène**, un quad de
2000 × 2000 unités nommé `f_drawing_plane`. Toute la conversion 2D → 3D se
réduit à un lancer de rayon sur ce plan, ce qui rend le résultat directement en
coordonnées locales du plan avec Z = 0 : **toute la logique de tracé est ensuite
du 2D pur**. C'est une simplification radicale et payante.

Le plan est orienté par la normale de la face touchée — et le rayon n'est pas
tiré sur la cible mais sur **une copie dont les chanfreins et les weighted
normals ont été retirés et les modificateurs appliqués**, pour tomber sur une
face réellement plane plutôt que sur un congé. Le roulis autour de Z est ensuite
résolu par une boucle qui essaie 1°, puis 0,1°, puis 0,01°, puis 0,0005° jusqu'à
ce que les axes de la grille retombent alignés sur le monde. C'est de la force
brute assumée, et le résultat utilisateur est exactement le bon.

**Le snapping est mesuré en pixels écran**, pas en unités monde : seuil de 16 px
multiplié par le facteur d'interface. L'accroche est donc uniforme quel que soit
le zoom. Deux couches de points, de couleurs différentes : les points de grille
et les vrais sommets. Le mode « étendu » (actif par défaut) étend l'accroche à
**tous les sommets coplanaires de l'objet**, pas seulement ceux de la face
touchée — ce qui permet de s'accrocher à une arête située à l'autre bout d'un
flanc plat.

Le second système de contraintes, sous Ctrl, est le point le plus fin du
fichier : alignement en X ou Y sur un sommet déjà posé, et contrainte à 45° par
rapport au sommet précédent ou au premier. Et quand les deux sont actifs en même
temps, le code ne choisit pas — il calcule **l'intersection de la droite
d'alignement et de la droite à 45°**. Les sommets de référence sont alors
dessinés en rouge : le retour visuel dit *pourquoi* ça a accroché.

**La géométrie n'est pas construite à la validation.** Un mesh minimal plus une
pile de modificateurs nommés est créé dès le premier clic, et le glissé ne fait
que piloter des valeurs :

| Forme | Mesh de base | Rig |
|---|---|---|
| rectangle | 4 sommets, 1 face | coordonnées réécrites à chaque mouvement |
| cercle | **2 sommets, 1 arête** | Displace de rayon + Screw + Decimate |
| sphère | **1 sommet** | Displace + Screw 180° sur Y + Screw 360° |
| polygone | **500 sommets pré-alloués** | indice qui avance, `remove_doubles` à la fin |

**Un cercle est donc un segment et un Screw.** Sa résolution reste un paramètre
vivant pour toujours. Ctrl force trois pas — un triangle — et Alt six : un
hexagone n'est qu'un cercle basse résolution. Zéro allocation par mouvement de
souris, et un plafond dur à 500 points pour le polygone libre.

**Le sens du booléen est déduit du signe de l'épaisseur** : positive, c'est une
union ; négative, une différence ; et le franchissement de zéro bascule
l'opération en direct pendant le glissé. L'artiste ne choisit jamais « ajouter ou
retirer », il tire une épaisseur. Un seul geste, deux intentions.

Enfin, la même touche de validation change la nature du résultat : `Espace`
détruit les faces et ne garde que les arêtes — le tracé devient un **chemin**
qui sera épaissi ; un clic gauche avec Shift le ferme en solide. **Le même geste
produit soit un volume, soit un profil.**

Compte de clics pour percer un trou rectangulaire : une touche, un clic pour
poser le plan et démarrer, un clic pour valider le rectangle, un glissé pour la
profondeur, un clic droit pour sortir. **Quatre clics, aucun menu, aucun objet
nommé, aucune pile touchée.**

### 3.4 Slice et Inset — deux objets réels, pas deux modes

**Slice.** Le modificateur de la cible passe en `DIFFERENCE`, puis **la cible est
dupliquée** — mesh et pile entière, donc toutes ses coupes antérieures — et dans
cette copie le booléen qui pointe vers le même coupeur est basculé en
`INTERSECT`. Un coupeur, deux cibles, deux opérations complémentaires. Revenir en
mode coupe supprime simplement la copie.

**Inset.** Ni un vrai inset, ni un coupeur rétréci : **une coque de la cible**.
La cible est dupliquée, son booléen retiré, elle reçoit un solidify non-manifold
d'épaisseur 0,05, puis un `INTERSECT` vers le coupeur — ce qui découpe dans la
coque le morceau situé sous le tracé. Enfin le booléen d'origine est redirigé
vers cet objet inset. Le résultat **épouse la surface réelle de la cible**, y
compris courbe. C'est ce qui le distingue d'un coupeur mis à l'échelle. Et là
encore, le signe de l'épaisseur bascule entre bosse et creux.

### 3.5 Les chanfreins — quatre familles, deux systèmes

C'est là que dort le savoir-faire chiffré.

**Quatre familles, à des étages différents :**

1. `.f_First_Bevel.<n>` — arrondit les **coins du profil dessiné**, avant le
   solidify. Fluent nettoie d'abord le profil par une dissolution à 5°, puis ne
   retient que les sommets appartenant **exactement à deux arêtes** — les vrais
   coins d'un contour fermé — et crée pour chacun **son propre groupe de sommets
   et son propre modificateur Bevel** en mode `VGROUP` / `VERTICES`. On règle
   donc chaque coin d'un rectangle indépendamment, sans rien détruire, en
   cliquant sur une pastille à l'écran. Le prix : autant de modificateurs que de
   coins, et une pile retriée du plus étroit au plus large.
2. `.f_Second_Bevel_Top` / `_Bottom` — les faces d'extrémité du volume extrudé.
3. `.f_Pre_Chamfer` / `.f_Chamfer` — un chanfrein après le booléen.
4. `.f_outer_bevel` — celui qui traite **les arêtes créées par la coupe**.

**Et voici le principe qui vaut d'être retenu :** les trois premières familles
sont limitées par un **groupe de sommets**, la quatrième par un **angle** (40°).
Fluent marque ce qu'il a fabriqué lui-même, parce qu'il sait exactement quels
sommets c'est ; il déduit par l'angle ce qu'il n'a pas fabriqué, parce que la
sortie d'un booléen ne lui dit rien. C'est exactement la bonne distinction, et
elle a une conséquence directe pour nous — §6.B.2.

**Valeurs par défaut du chanfrein de finition** : `limit_method` ANGLE,
`angle_limit` **40°**, `width` **0,01**, `miter_outer` **MITER_ARC**,
`harden_normals` **vrai**, `use_clamp_overlap` **faux**, suivi d'un
`WEIGHTED_NORMAL` de poids **100** repoussé en toute dernière position. Sur le
câble, le même chanfrein est réduit à **0,001** de large : à cette taille il
n'arrondit rien, **il ne sert qu'à durcir les normales et accrocher la
lumière**.

**Deux systèmes**, réglés par une préférence : `SIMPLE` (défaut) pose **un seul**
chanfrein pour tout l'objet, replacé en avant-dernière position après chaque
opération ; `MULTIPLE` pose **un chanfrein par booléen**, inséré juste après lui,
en recopiant les réglages du précédent — et désactive automatiquement un
chanfrein dont la largeur est identique à celle de son voisin, pour ne pas
empiler des redondances.

### 3.6 La résolution en segments par mètre

Le meilleur réglage de tout l'addon, et le plus facile à reprendre.

On ne règle pas un nombre de segments : on règle une **densité linéaire**. La
formule prend la longueur d'arc concernée, la multiplie par une résolution
exprimée en segments par mètre, borne, et arrondit au supérieur.

- pour un congé : longueur d'arc du quart de cercle, **π · r / 2** ;
- pour un cylindre : le périmètre entier, **2π · r** ;
- pour un solide de révolution : `r` = le plus grand |x| du profil, puis 2π · r.

`model_resolution` vaut **16 segments par mètre** par défaut. Bornes :
`min_auto_bevel_segments` **4** (côté préférences ; 1 côté scène),
`min_auto_cylinder_segments` **16** (borné 3–64), maxima à **0** = illimité. Et
pour les cylindres seulement, le compte est forcé au **multiple de 4** le plus
proche — ce qui garantit des arêtes alignées sur les axes.

Conséquence : un congé de 2 mm et un congé de 2 cm reçoivent automatiquement des
comptes différents et le même lissé perçu. Et **changer ce seul nombre re-résout
toute la scène**.

Exception codée en dur, révélatrice : les chanfreins dont le profil vaut
exactement **0,25** sont exclus de l'automatisme — ce sont les chanfreins droits
à deux segments, volontairement fixes. Le profil 0,25 apparaît 17 fois dans le
code, 0,08 six fois (concave), et le défaut utilisateur est 0,50 (arc de
cercle). La touche `C` cycle convexe → droit → concave.

### 3.7 Le motif « un node group écrit un attribut, un modificateur natif le lit »

C'est la technique la plus transposable de tout l'addon, et elle répond au
problème central du hard surface procédural : **sélectionner des arêtes qui
n'existent pas encore**.

Pour le chanfrein : on crée deux groupes de sommets vides `top_face` et
`bottom_face` ; on pose un modificateur Geometry Nodes dont les deux sorties
booléennes **écrivent dans ces attributs** ; puis un Bevel natif juste après, en
mode `VGROUP`, les consomme. La sélection est calculée procéduralement et se met
à jour toute seule quand la forme change, tandis que le biseau reste un Bevel
natif, rapide et complet. Même schéma pour les congés de face haute et basse, et
pour les arêtes d'un objet enroulé.

Les sept `.blend` de `geometry_nodes/` sont ces node groups. Ce qu'ils font,
d'après leurs nodes et l'usage qu'en fait le Python :

| Fichier | Ce qu'il fait |
|---|---|
| `chamfer.blend` | sélectionne la face supérieure par orientation de normale (tournée par la rotation du node de rotation) et l'écrit dans deux attributs |
| `second_bevel.blend` | sélectionne les arêtes bordant la face haute ou basse, via frontières de face sets et angle d'arête ; entrées Width, Bottom/Top, All, Limit angle, Rotation |
| `frame.blend` | fabrique un cadre : extrait le contour, le rétrécit de `Width`, le ré-extrude de `Depth`, et rend séparément les sélections `Inner` et `Outer` |
| `array.blend` | array circulaire : instancie sur les points d'un arc échantillonné, avec rotation par instance, angle de balayage partiel et objet support optionnel |
| `face_selection.blend` | compare la géométrie coupée à une source par échantillonnage du point le plus proche et marque les faces dont la normale diverge — moteur de sélection à usage unique |
| `normal_repair.blend` | **lance un rayon depuis chaque point vers l'objet source et remplace la normale par celle trouvée** ; entrées Object, Threshold, Scale, Distance, Debug |
| `smooth_by_angle.blend` | l'asset officiel de Blender, embarqué parce que `mesh.use_auto_smooth` a disparu en 4.1 |

### 3.8 Les outils de survie

Ils existent tous parce que le système repose sur des conventions de nommage et
des objets cachés. Ce sont des symptômes autant que des outils, et c'est
pourquoi ils comptent : ils disent le coût du modèle.

**Boolean Explorer** — ce n'est pas un navigateur d'historique. C'est un moyen de
**retrouver un coupeur parmi cinquante objets cachés** : il les masque tous et
n'en affiche qu'un à la fois, avec quatre tris (par X, par Y, par Z, ou dans
l'ordre de la pile — la seule trace de chronologie disponible). Un clic ouvre le
coupeur désigné dans l'éditeur.

**Face Extraction** — l'artiste sélectionne des faces, en obtient un objet
séparé, **remis à plat** par une manœuvre en plusieurs temps (vue orthographique
du dessus alignée sur la face, curseur, empty temporaire servant de parent le
temps de neutraliser la rotation). Trois débouchés selon l'appelant : un coupeur
qui épouse exactement une face existante, une **tôle** (`plate` — l'opérateur
correspondant fait vingt lignes et se contente d'appeler l'extraction), ou une
promotion en objet Fluent.

**Become Fluent** — promotion d'un objet ordinaire. Il retire tous les
modificateurs, convertit, applique un Decimate en mode **DISSOLVE** — ce qui
**fusionne les faces coplanaires en un seul n-gone** — puis **vérifie que
l'objet est plan** en comparant les normales de tous les polygones, arrondies à
cinq décimales. S'il ne l'est pas, refus. Une courbe, elle, est routée vers
l'outil câble, à condition d'avoir une seule spline en Bézier.

**Normal Repair** — le problème est l'artefact d'ombrage sur les arêtes
chanfreinées issues d'un booléen. La méthode : transférer les normales d'une
version *propre* de l'objet — la même, ses booléens retirés — vers la version
coupée, par un `DATA_TRANSFER` en `POLYINTERP_LNORPROJ` limité à un groupe de
sommets. En trois étapes guidées, avec une pré-sélection automatique des faces
suspectes par le node group `face_selection`, un réglage de fusion de sommets à
la souris, et un matcap métallisé pour juger.

La version temps réel remplace le transfert figé par un node group qui **lance un
rayon vers la source et écrit directement la normale trouvée**, avec trois
réglages — distance de tir 0,01, seuil, et intensité de mélange 0,1 — et un masque
peint en couleurs de sommets pour visualiser où ça agit. La source est
**composable** : plusieurs objets sélectionnés sont joints dans un node group
construit à la volée, un `Object Info` par objet. Le modificateur est épinglé en
fin de pile. Le prix : **un lancer de rayon par point, en permanence**, sans gel
ni cache. Et l'outil refuse de se lancer si l'objet ne porte pas déjà un
chanfrein Fluent — il est inséparable du système.

**Autocomplete** — fige le modèle : duplication, l'original est caché, puis
**tous les modificateurs sont appliqués un par un depuis le haut**, avec entre
chaque application un marquage automatique des arêtes vives au-delà de **35°**
en sharp *et* en seam, et pour finir un dépliage UV complet en `ANGLE_BASED`.
Raffinement notable : quand le modificateur à appliquer est un booléen dont le
coupeur est un cylindre, les coutures UV du cylindre sont **calculées
géométriquement** avant l'application — détection de l'axe par les centres des
capuchons, regroupement des sommets par projection sur cet axe, puis recherche
gloutonne d'un chemin longitudinal. C'est le seul endroit de l'addon où une
topologie est analysée finement.

**Building Animation** — le modèle se construit tout seul dans la timeline. Ce
n'est ni une animation de visibilité ni une animation d'échelle : c'est
**l'animation de la pile de modificateurs elle-même**. Tout est masqué, puis
chaque famille est rallumée dans un ordre fixe, par keyframes sur la valeur
nulle puis la valeur cible, avec une durée de base de **12 images divisée par la
vitesse** (24 pour les arrays). Quand un booléen est rencontré, la fonction
**descend récursivement dans le coupeur** pour animer sa propre construction
avant de rallumer la coupe.

**Auto Support** — celui-là mérite d'être noté. Il construit un mesh « couteau »
maison de 18 sommets, aux dimensions du booléen **plus une marge égale à
2 × la largeur du dernier chanfrein par angle + 0,01**, le joint à la cible,
lance une intersection, puis supprime les sommets du couteau : il ne reste que
**les boucles d'arêtes créées par l'intersection**, tout autour du futur trou.
Des boucles de support engendrées par intersection plutôt que par modificateurs,
avec une marge dérivée du chanfrein existant. C'est du vrai savoir-faire hard
surface.

S'ajoutent : le nettoyeur de coupeurs orphelins, le renommeur, le nettoyeur de
groupes de sommets, la synchronisation des coupeurs dupliqués par `fluent_id`, la
duplication qui **devine l'opération** d'après le signe de l'épaisseur du
solidify, trois bascules d'affichage technique, et un nettoyeur de tranche qui
supprime les paires de faces opposées coplanaires (produit scalaire < −0,99,
sommets confondus à 0,001 près).

### 3.9 Robustesse des booléens — constat, et renvoi

Traité en détail dans `csg-clarisse.md`. Les faits observés ici, simplement
notés :

- **`solver = 'FLOAT'` sur les quatre sites de création, sans condition.** Aucune
  occurrence de `'EXACT'` ni de `'MANIFOLD'` dans tout l'addon. Fluent n'emploie
  que le solveur rapide.
- Aucun `double_threshold`, `use_self` ou `use_hole_tolerant` n'est jamais écrit.
- **Aucun nettoyage préalable** de la cible : pas de weld, pas de fusion par
  distance, pas de triangulation, pas de recalcul de normales avant ou après.
- **Trois parades anti-coplanarité, toutes empiriques** : l'offset du premier
  solidify à **−0,95** et non −1, ce qui décale le volume presque entièrement
  d'un côté du plan ; la traversée automatique qui multiplie la profondeur
  mesurée par **1,01** ; et un déplacement de ±0,01 pour les tracés ouverts.
- Un nettoyage *a posteriori* des faces superposées, décrit ci-dessus.

Le choix est cohérent avec le produit — l'interactivité contre la robustesse —
mais il est assumé sans filet.

---

## 4. Les générateurs

### 4.1 Le tuyau

Deux clics sur deux surfaces, et un tuyau rigide les relie en cheminement
**strictement orthogonal**, sortant perpendiculairement de chaque surface, coudes
arrondis. On fait ensuite défiler les six itinéraires possibles.

Le chemin est **toujours une polyligne de six points**, calculée analytiquement :
les deux points cliqués, deux points de sortie à `root_length` le long des
normales (défaut **0,2**), et deux coins intermédiaires alignés sur les axes du
monde. Les six variantes sont les **six ordres possibles de parcours des axes X,
Y, Z**. C'est du routage de Manhattan à trois segments, sans aucune recherche de
chemin.

**Et le coude est simplement un modificateur Bevel en mode sommets appliqué à la
polyligne, avant la conversion en tube.** Sa largeur *est* le rayon de courbure
(défaut 0,1). Le point élégant : un **Weld est placé avant**, si bien que quand
deux points coïncident — cas typique où les deux extrémités sont alignées sur un
axe — ils fusionnent et le coude disparaît proprement au lieu de produire une
singularité. Les cas dégénérés se règlent tout seuls.

Aucune collision, aucun routage, aucun évitement d'obstacle. C'est un outil de
**placement assisté**, pas de routage. Aucune gestion d'UV non plus.

Les accessoires — colliers, gaine, connecteurs — sont posés par **câblage à chaud
du node tree** : ajouter un collier crée un node de groupe et le branche entre un
reroute repéré par son nom et un `Join Geometry`. Le node tree devient une
structure de données extensible à l'exécution. Un collier est initialisé à un
rayon valant **deux fois celui du tuyau**, et sa sélection déclenche une
**micro-animation** — son offset oscille en sinus, amplitude 0,02, pendant
environ 0,6 s — pour signaler lequel est concerné. Détail d'ergonomie bien vu.

### 4.2 Le câble

Même geste, mais la géométrie support est une vraie courbe de Bézier. **Le point
technique central : les poignées sont alignées sur les normales des surfaces**, à
une distance `stiffness` (défaut **0,5**). Un seul scalaire contrôle la raideur
de sortie aux deux bouts et donne immédiatement une courbe crédible. Subtilité :
quand on rejoue le curseur de raideur, le code **recalcule la direction depuis
les poignées courantes** au lieu de relire la normale stockée — délibérément,
pour qu'on puisse réorienter à la main sans tout perdre.

La torsion est bien traitée : `twist_mode` en **MINIMUM** (l'équivalent d'un
transport parallèle), `twist_smooth` à 8, plus deux réglages de tilt aux deux
extrémités — et un `smooth_tilt()` appelé **après chaque réglage**, qui interpole
la torsion sur toute la courbe. Deux valeurs à régler, l'interpolation déléguée à
l'opérateur natif.

**La pendaison est une simulation, pas une caténaire.** La courbe est
rééchantillonnée à **256 points par mètre**, convertie en polyligne, une zone
d'épinglage est construite aux deux bouts couvrant environ **deux diamètres de
câble avec un plancher de 20 segments** — ajustable à la main —, et les poids
d'épinglage y sont répartis par **une cubique empirique de coefficients
(3,88 ; 0,39 ; 0,05 ; 0,224)** qui donne un dégradé en S, nul au bord de la zone
et total aux extrémités. C'est ce qui évite la cassure nette entre partie fixe et
partie libre. Puis un modificateur Cloth (`time_scale` 4, `pin_stiffness` 1),
100 images, et enfin **reconversion en Bézier à poignées automatiques avec une
décimation à 2,5 %** : on paie une simulation dense une fois, on retombe sur une
courbe légère et éditable.

C'est pragmatique, et c'est exactement ce que `curves-clarisse.md` a choisi de ne
pas faire : notre chaînette est **résolue**, avec le mou exprimé en pourcentage,
donc déterministe, instantanée, et survivant au déplacement d'un point
d'accroche. Rien dans ce que fait Fluent ici ne remet cette décision en cause —
au contraire, la dépendance du résultat aux réglages cloth de la scène de
l'utilisateur la confirme.

### 4.3 La chaîne

Le node group est **généré par du code Python**, pas chargé depuis un `.blend`.
La recette, complète et courte : un quadrilatère paramétré par `Width` (0,1) et
`Height` (0,05), un **Fillet Curve** de rayon `Radius link` (0,05) à six
segments, un aller-retour *curve → mesh → merge by distance (0,001) → curve* pour
obtenir une courbe fermée et propre, puis un tubage par un cercle de résolution
64 et de rayon `Radius` (0,01, plafonné à la moitié de la longueur du maillon —
la seule contrainte géométrique réelle du fichier).

Deux astuces valent d'être notées. Les maillons sont répartis par un
rééchantillonnage **à un pas égal à `longueur − rayon × 2,1`** : le facteur 2,1
fait que les maillons se chevauchent d'un peu plus de deux fois le rayon du fil,
donc **ils s'enfilent les uns dans les autres au lieu de se toucher**. Et
l'orientation combine une rotation de **π/2 par index** — l'alternance classique
— avec un **jitter aléatoire dans [0 ; 0,5] radian** qui casse la régularité
mécanique. Aucune collision, aucune simulation : de l'instanciation paramétrique
pure.

### 4.4 Les grilles ajourées

Quinze motifs, chacun dans son propre `.blend` : alpha, beta, gamma, delta,
epsilon, zeta, eta, sigma, omega, carré, triangle, hexagone, losange, cercle, et
cercle en quinconce.

Ce ne sont **pas** des node groups : ce sont des **objets pré-modélisés** —
`Grid_Hex`, `Grid_Diamond`… — appendés depuis `<fichier>.blend/Object/`, et qui
arrivent avec leur pile de modificateurs déjà montée : `Solidify`, `Displace_Z`,
`Array_X`, `Array_Y`, plus `ArrayX_from_bool` / `Y` / `Z`,
`DisplaceX_to_center` / `Y`, `Mirror_from_bool`, et un node group `frame`.

Deux choses sont malignes.

**La taille des trous est une shape key.** La tuile porte deux formes — trou
serré, trou large — et l'artiste interpole entre les deux au glissé. La topologie
ne change jamais, et rien n'est détruit.

**La grille hérite de la répétition de son hôte.** L'outil ne s'applique pas à
une surface mais à un **booléen existant** : la grille prend la translation et
l'euler de sa matrice, et une échelle initiale telle qu'**une tuile fasse un
dixième de la hauteur de la découpe** — le 10 est en dur, c'est un choix
esthétique. Puis, pour ajuster la couverture, l'outil désactive temporairement
tous les booléens, miroirs, arrays et cadres de la cible et **incrémente le
compteur d'array d'un cran à la fois, en forçant une évaluation du depsgraph et
en relisant les dimensions après chaque pas**, jusqu'à dépasser la cible de 2 %.
Plafond en dur : 500 itérations. C'est brutal — une évaluation complète par pas —
mais c'est robuste : le code ne sait rien de la géométrie du motif. Le
débordement de 2 % est assumé, recentré par deux Displace, et c'est le booléen
parent qui taille les bords. Enfin les arrays et le miroir de l'hôte sont
recopiés sur la grille, pour que la perforation reste alignée quand le panneau
se répète.

**Et voici la limite qui compte : il n'existe aucun mode « sur une surface
courbe ».** Pas de shrinkwrap, pas de projection, pas de déformation par cage,
pas de suivi de normale. La grille est un pavage plat posé sur une matrice. Même
un booléen `circular` — le cylindre déplié du §4.6 — n'est pas suivi. C'est le
plus gros trou fonctionnel de tout Power Trip, et c'est précisément là qu'un
node paramétrique aurait quelque chose à dire (§7.10).

### 4.5 Les vis et rivets

Neuf styles de tête × six styles d'empreinte d'outil, soit **54 combinaisons**
pré-modélisées, plus un fichier de rivets. Ce sont des bibliothèques
substantielles — 8,8 Mo et 3,0 Mo —, et ce sont les fichiers de travail de
l'auteur : on y trouve aussi les versions haute densité et les emporte-pièces
qui ont servi à creuser les empreintes cruciformes.

Le geste : l'artiste passe en sélection de faces, choisit celles à visser,
valide — et **les faces choisies sont dupliquées puis séparées en un objet
indépendant** qui devient le support. Tous ses sommets entrent dans un groupe
nommé `screws`, qui est le masque de semis.

Le mécanisme est ensuite **exactement le motif du scatter**. Deux node groups
sont appendés depuis `Rivets.blend` : **`Rivets`**, qui instancie les têtes, et
**`Rivets.Holes`**, qui fabrique les perçages. Le support reçoit le premier ;
une **copie du support** reçoit le second et devient un coupeur booléen sur la
cible. La tête elle-même est un **objet appendé branché dans un socket Object** :
elle est instanciée, pas modélisée. Le miroir est recopié du support vers la
copie pour que les deux restent synchrones.

Les nodes du groupe disent la méthode : un centre de masse par îlot de faces
pour la position, un rééchantillonnage du contour pour le mode « seulement les
coins », **la normale de la face pour l'orientation**, et une fusion par distance
pour supprimer les doublons aux coins partagés. Trois modes — au centre de chaque
îlot, sur les coins, ou un par face. Réglages : largeur et retrait à **0,1** tous
les deux, mémorisés d'une vis à l'autre dans des propriétés de scène.

**Le filetage n'est pas modélisé** : on ne pose que la tête. Et le trou est
optionnel — un bouton crée ou supprime le vrai modificateur booléen entre la
cible et la copie.

Retenir la structure, parce qu'elle décide de la suite : **des points, un objet
instancié dessus, et un trou par point**. La première moitié est gratuite chez
nous. La seconde demande un booléen.

### 4.6 Le cylindre dépliable

L'idée la plus intéressante de tout Power Trip, et elle tient en une phrase :
**on modélise à plat, et le pliage est un modificateur en fin de chaîne.**

Un rectangle est construit par calcul : sa largeur vaut le périmètre **2π·r**,
`r` étant lu dans le déplacement du cylindre d'origine, et sa hauteur
l'épaisseur de ce cylindre. On y trace ses découpes à plat — avec tous les outils
habituels, sur une surface plane où le snapping et la grille ont un sens — puis
un node group l'enroule. Plier ou déplier ne fait que **basculer la visibilité de
trois modificateurs** : le `knife`, le `bend` et le `decimate`. L'état plat est
la vérité ; le cylindre est un modificateur. Le rayon se règle en **réécrivant
les coordonnées des sommets** (`x = ±périmètre/2`), pas par une mise à l'échelle,
et l'épaisseur est plafonnée au rayon.

La pile complète, dans l'ordre : solidify d'épaisseur 0,01, un correcteur de
solidify, **deux passes de couteau** (résolution 12, tolérance 0,0001), le
pliage, puis une dissolution à 0,5° délimitée par les normales.

La difficulté reconnue est la **couture** : la documentation de l'outil explique
qu'une option « Fix Border » corrige le défaut d'ombrage là où les deux bords se
rejoignent, quand une addition booléenne déborde du rectangle — et prévient
qu'elle ralentit le viewport.

### 4.7 Text2Mesh

Convertit un objet texte en maillage et le **nettoie**. Une dissolution limitée
à 5° réduit le glyphe tessellé en n-gones, puis un solidify (offset 0, épaisseur
0,2) lui donne son volume.

Le milieu du code est un contournement plutôt qu'une technique : les sommets
alignés horizontalement sont déplacés d'un millième de la hauteur, une
dissolution à 0,1° est relancée, puis ils sont ramenés — deux fois. C'est une
ruse pour forcer l'opérateur de Blender à supprimer des sommets rigoureusement
colinéaires qu'il laisse sinon en place. Ça n'a rien à nous apprendre.

---

## 5. Les huit idées qui valent plus que les outils

1. **La résolution en segments par mètre.** π·r/2 pour un congé, 2π·r pour un
   cylindre, ×16 seg/m, borné, arrondi au supérieur, et forcé au multiple de 4
   pour les cylindres. Un seul nombre re-résout toute la scène.
2. **Le signe de l'épaisseur pilote l'opération.** L'artiste ne choisit jamais
   entre ajouter et retirer ; il tire une épaisseur et le zéro fait la bascule.
3. **Le coude est un chanfrein de sommets, précédé d'un weld.** Le rayon reste
   réglable a posteriori et les cas dégénérés se résolvent d'eux-mêmes.
4. **On marque ce qu'on a fabriqué, on déduit ce qu'on n'a pas fabriqué.**
   Groupe de sommets pour les arêtes qu'on a créées soi-même, angle limite pour
   celles qui sortent d'un booléen.
5. **Modéliser à plat, plier en fin de chaîne.** L'état plat est la vérité.
6. **Le même tracé donne au choix un volume ou un profil**, selon la touche de
   validation.
7. **Le plan de travail est un objet caché, et le snapping se mesure en pixels.**
   La conversion 2D → 3D devient un lancer de rayon, la logique de tracé reste du
   2D, et l'accroche est uniforme quel que soit le zoom.
8. **Un enchaînement d'outils qui présélectionne le réglage suivant le plus
   probable.** À la fin du tracé, l'éditeur démarre déjà en train de régler
   l'épaisseur. On atterrit sur ce qu'on allait vouloir faire.

Et une neuvième, en creux : **rien n'est jamais supprimé, tout est désactivé.**
Les modificateurs sont créés d'avance avec leur visibilité éteinte et rallumés
quand leur valeur devient non nulle. Ça stabilise les indices de pile. Chez nous
c'est sans objet — mais c'est un bon rappel de ce que coûte une architecture par
pile.

---

## 5 bis. Les valeurs par défaut, rassemblées

Elles portent plus de savoir-faire que le code. Toutes lues, aucune déduite.
Fluent déclare 119 propriétés Blender ; celles qui suivent sont celles qui
décrivent de la matière plutôt que de l'interface.

**Résolution**

| | |
|---|---|
| densité du modèle | **16 segments par mètre** |
| minimum par congé | **4** (1 côté scène) |
| minimum par cylindre | **16**, borné 3–64 |
| maxima | **0** = illimité |
| cylindres forcés au multiple de | **4** |
| longueur d'arc d'un congé | π·r/2 |
| longueur d'arc d'un cylindre | 2π·r |

**Chanfreins**

| | |
|---|---|
| chanfrein de finition : angle limite | **40°** |
| chanfrein de finition : largeur | **0,01** |
| onglet extérieur | arc |
| durcissement des normales | **oui** |
| écrêtage des chevauchements | **non** |
| weighted normal qui suit | poids **100**, toujours en dernier |
| profil par défaut | **0,50** (arc de cercle) |
| profil « droit » | **0,25**, à 2 segments, exclu de la résolution auto |
| profil « concave » | **0,08** |
| angle limite du dernier chanfrein | **35°** |
| second chanfrein : angle | **30°**, limité par poids d'arête |
| chanfrein de support d'un câble | **0,001**, réduit à 0,0001 avec gaine |
| dissolution du profil avant chanfrein de coin | **5°** |

**Épaisseurs et volumes**

| | |
|---|---|
| premier solidify : offset | **−0,95** (et non −1) |
| second solidify | **−0,01**, offset 0 |
| solidify d'inset | **0,05**, non-manifold, offset 0 |
| solidify d'un tracé ouvert | **0,025**, non-manifold |
| solidify d'un cylindre déplié | **0,01** |
| traversée automatique | profondeur mesurée × **1,01** |
| déplacement anti-coplanarité d'un tracé | **±0,01** |

**Répétition**

| | |
|---|---|
| array : espacement initial | plus grande dimension × **1,2** |
| array : compte | **3**, offset constant |
| grille : une tuile fait | **1/10** de la hauteur de la découpe |
| grille : surdimensionnement | **2 %**, plafond 500 itérations |

**Câbles, chaînes, tuyaux**

| | |
|---|---|
| raideur d'un câble à la sortie de surface | **0,5** |
| longueur de sortie perpendiculaire d'un tuyau | **0,2** |
| rayon de coude d'un tuyau | **0,1** |
| torsion | minimum twist, lissage **8**, résolution 64 |
| chaîne : maillon | **0,1 × 0,05**, congé 0,05 à 6 segments |
| chaîne : rayon du fil | **0,01**, plafonné à la moitié de la longueur |
| chaîne : pas d'entrelacement | longueur − rayon × **2,1** |
| chaîne : rotation par maillon | **π/2**, plus un jitter dans [0 ; 0,5] rad |
| pendaison : échantillonnage | **256 points par mètre** |
| pendaison : zone d'épinglage | ≈ 2 diamètres, plancher **20** segments |
| pendaison : cubique de poids | **(3,88 ; 0,39 ; 0,05 ; 0,224)** |
| pendaison : décimation finale | **2,5 %** |

**Divers**

| | |
|---|---|
| vis : largeur et retrait | **0,1** tous les deux |
| couteau du cylindre déplié | résolution **12**, tolérance 0,0001 |
| dissolution après révolution | **0,1°** ; **0,5°** pour un revolver |
| marquage des arêtes vives au gel | **35°** |
| texte : dissolution, épaisseur | **5°**, **0,2** |
| sensibilité des curseurs | **300** normal, ÷10 avec Ctrl, ×10 avec Shift |

Trois de ces nombres méritent d'être retenus parce qu'ils ne se devinent pas :
l'offset de solidify à **−0,95** plutôt que −1, qui écarte le volume du plan de
la face ; le **2,1** de la chaîne, qui fait que les maillons s'enfilent au lieu
de se toucher ; et la **cubique d'épinglage**, qui est la différence entre un
câble qui casse net à la sortie du mur et un câble qui se relâche.

---

## 6. Le classement

### A. Excellent et faisable

**A.1 — Le chanfrein d'arête shadé.** Une texture qui perturbe la normale près
des arêtes vives, à la manière du *Bevel* de Cycles. Zéro géométrie, zéro
mémoire, et ça résout le problème que Fluent traite avec un chanfrein de 0,001
de large : **accrocher la lumière sur une arête**. `TextureCurvature` prouve
qu'une texture de Clarisse peut lancer des rayons et expose déjà les bons
réglages — rayon, biais, nombre d'échantillons, groupe de géométries,
latéralité. C'est le meilleur rapport effet visuel / lignes de code de toute la
liste, et c'est indépendant de tout le reste. *Estimation : de l'ordre de
`chroma.cpp`, 400 lignes.*

**A.2 — La famille de déformeurs manquante.** Taper, twist, bend, et surtout
**l'enroulement** du §4.6. `Deformer` est dérivable, on en a écrit un, il ne
change pas la topologie et Clarisse appelle `cb_deform` en parallèle sur des
plages disjointes. Trois déformeurs livrés dans tout Clarisse, c'est une lacune
béante et bon marché à combler. *Estimation : ~400 lignes pour les quatre,
`curve_deform.cpp` en fait 412 à lui seul.*

**A.3 — La résolution en segments par mètre, rétro-portée sur les courbes.**
`curves-clarisse.md` la liste déjà en priorité 2 ; Fluent donne la formule
exacte et les bornes. `tube.cid` expose aujourd'hui `sides` et `steps`, deux
compteurs absolus. Les remplacer par une densité — et la faire piloter par un
node partagé, ce que Blender ne permet pas — fait passer trois cents câbles du
gros plan au décor d'un seul curseur. *Estimation : une vingtaine de lignes.*

**A.4 — Les vis, rivets et boulons.** La moitié du travail est déjà faite par
Clarisse : un nuage de points plus un Scatterer, et on a les têtes. Ce qui manque
est un node qui **pose des points sur une surface selon une règle** (le long
d'un bord, en quinconce, aux coins d'un panneau) — c'est-à-dire un cousin de
`curve_points`, mais surfacique. Les perçages, eux, relèvent de B.1 ; et pour un
panneau plat un node qui fabrique le panneau *avec* ses trous s'en passe. *La
partie faisable : ~300 lignes.*

**A.5 — Le panneau perforé.** Même raisonnement, et il porte loin. Plutôt que de
projeter un motif puis de le soustraire, **un node construit directement le
panneau troué** à partir d'un motif de tuile, d'un contour et d'un pas. Il n'y a
alors aucun booléen : le node sait où sont ses trous puisqu'il les place. Et
comme il connaît ses propres arêtes, il peut leur donner leur chanfrein, leurs
creases et leur shading group au passage — trois choses que Fluent doit
reconquérir après coup.

Et il peut faire ce que Fluent ne sait pas faire du tout : **suivre une surface
courbe** (§7.10) et **régler la taille des trous par une texture** plutôt que par
un nombre unique (§7.4). Ce n'est pas un portage, c'est un meilleur outil pour
moins de travail. *Estimation : ~600 lignes, l'ordre de `quilt.cpp` — qui pave
déjà un maillage d'entrée polygone par polygone, donc la moitié du chemin est
faite.*

**A.6 — Le tuyau orthogonal.** Notre `tube` sait déjà faire des coudes de rayon
constant, et mieux que Blender — `curves-clarisse.md` documente pourquoi. Ce qui
manque est le **routeur de Manhattan à six points** entre deux points et deux
normales, plus le défilement des six itinéraires. *Estimation : ~150 lignes dans
`curve_core.h`.*

**A.7 — Le cadre, le listel, la rainure.** Extraire le contour d'une face,
rentrer de `Width`, ressortir de `Depth`. Un node à une entrée géométrique.
*Estimation : ~350 lignes.*

**A.8 — La chaîne.** `tube` et `curve_points` font déjà 90 % du travail. La
recette du §4.3 — le pas d'entrelacement à `longueur − rayon × 2,1`, la rotation
de π/2 par maillon, le jitter borné — tient en trois lignes une fois le maillon
disponible comme objet à instancier. *Estimation : ~100 lignes.*

**A.9 — Le geste.** Le plan de travail caché, le snapping en pixels, la
combinaison des contraintes, le curseur horizontal universel avec Shift/Ctrl et
re-verrouillage anti-saut, la saisie numérique en parallèle, l'aide contextuelle
permanente. `CtxToolEvent` porte tout ce qu'il faut. **Réserve honnête** : la
plume est écrite mais n'a jamais été essayée en interactif, et c'est le seul
poste de cette liste dont la faisabilité repose sur la structure et non sur un
essai. À lever avant d'y investir. *Estimation : inconnue tant que le premier
essai n'est pas fait.*

### B. Excellent mais coûteux

**B.1 — Le booléen.** C'est le grand absent, et c'est le seul élément de cette
liste qui débloque tous les autres d'un coup : le cutter, la tranche, l'inset,
l'extraction de face, les perçages de vis, la projection de grille sur une
surface courbe. Écrire un CSG robuste est un chantier, pas une tâche — et c'est
précisément le sujet de `csg-clarisse.md`.

Ce qui plaide pour, malgré le coût : chez nous ce serait un **node**, pas un
modificateur destructif. Donc l'ordre est le graphe, les coupeurs peuvent être un
`Group` réglé par règles qui se remplit tout seul, et les faces coupées reçoivent
**leur propre shading group nommé** — donc leur matériau par une règle de
Shading Layer, pour toute la scène, d'un coup. Aucun des trois n'est possible
chez Blender.

**B.2 — Le chanfrein géométrique.** Un vrai node de bevel demande une structure
de demi-arêtes, la gestion des onglets, l'écrêtage des chevauchements — et
surtout la question « quelles arêtes ». Fluent y répond en marquant ce qu'il a
fabriqué et en déduisant le reste par l'angle. **Chez nous la bonne réponse est
différente et meilleure : c'est le node booléen qui chanfreine sa propre sortie**,
parce qu'il sait exactement quelles arêtes il vient de créer. Il n'y a alors
aucun angle limite à régler, aucun groupe de sommets à maintenir, et le nettoyeur
de groupes de sommets de Fluent n'a pas de raison d'exister.

Et il existe une porte de sortie moins chère, qu'il faut essayer d'abord :
`smoothing_angle` plus des **creases** posées par le node, avec la subdivision
adaptative pilotée par crease. Un chanfrein de support de 1 mm dont le seul rôle
est d'accrocher la lumière n'a aucune raison d'être de la géométrie chez nous.

**B.3 — La réparation de normales.** Elle présuppose le booléen, et il se
pourrait qu'elle soit inutile : le problème que Fluent résout est celui d'un
chanfrein géométrique posé sur une topologie sale. Avec des creases et un angle
de lissage, il pourrait ne pas se poser. **À réexaminer une fois le booléen
écrit, pas avant.**

**B.4 — L'inset qui épouse la surface.** La trouvaille de Fluent — une coque
solidifiée de la cible, intersectée avec le coupeur — demande le booléen *et* le
solidify. Les deux manquent. Le résultat est beau et il n'y a pas de raccourci.

**B.5 — Le cylindre dépliable.** Le pliage est un déformeur, donc A.2. Mais tout
son intérêt est de **couper à plat**, ce qui ramène à B.1. Moitié gratuite,
moitié bloquée.

### C. Sans objet dans Clarisse

**C.1 — Toute la machinerie de pile.** `fluent_stack_order`, `place_in_stack`,
les 57 noms de modificateurs préfixés d'un point, les trois tables de « reuse »,
la sauvegarde et la reconstruction de piles, le déplacement d'index. C'est le
cœur de Fluent et ça représente le gros de ses 6 547 lignes centrales. **Ça
résout un problème que nous n'avons pas : chez nous le graphe *est* l'ordre.**
Brancher un node sur un autre, c'est le placer. Il n'y a rien à trier, rien à
replacer après chaque opération, aucune table à tenir à jour pour chaque nouveau
type d'objet. C'est le constat le plus important de cet audit, et il vaut d'être
énoncé positivement : **une grande partie de la complexité de Fluent est le prix
de la pile de modificateurs, pas le prix du hard surface.**

**C.2 — Le Boolean Explorer.** Il résout « retrouver mon coupeur parmi cinquante
objets cachés ». Chez nous les entrées d'un node sont visibles dans le graphe,
rien n'est caché, et l'Explorer, le Graph Editor et le widget de recherche
existent déjà.

**C.3 — L'animation de construction.** Elle anime une pile de modificateurs.
Chez nous tout attribut de node est animable de naissance : il n'y a rien à
construire, seulement des courbes à poser.

**C.4 — Autocomplete / le gel.** « Appliquer toute la pile et déplier les UV. »
Clarisse n'applique jamais rien — le graphe est évalué au rendu. Geler veut dire
exporter en Alembic, ce qui existe. Et la moitié « déplier » est à la fois
impossible (aucun dépliage UV dans Clarisse) et inutile : **un node qui fabrique
une géométrie connaît sa paramétrisation naturelle et émet ses propres UV**,
comme `tube` le fait déjà en face-varying.

**C.5 — Become Fluent, le nettoyeur de groupes de sommets, le renommeur, les
bascules d'affichage, le nettoyeur de tranche.** Ce sont des soins d'hygiène pour
un système bâti sur des conventions de nommage et des objets cachés. Aucun de ces
deux fondements n'existe chez nous.

**C.6 — Text2Mesh.** Clarisse n'a aucun objet texte : il n'y a rien à convertir.
Si un jour on veut du texte, c'est un tout autre problème — lire un contour de
police et le polygoniser — et la partie astucieuse du code de Fluent est un
contournement d'un défaut d'un opérateur Blender, donc du pur gaspillage à
porter.

**C.7 — La sélection de composants.** Il n'existe aucune sélection de composants
persistante dans Clarisse, et en construire une reviendrait à construire un mode
édition. **Tout doit s'exprimer en règle ou en champ, jamais en sélection.**
C'est une contrainte, mais c'est aussi une discipline : elle interdit
structurellement de fabriquer un outil dont le résultat dépend d'un état caché.

**C.8 — Les remailleurs externes** (Instant Meshes, QuadRemesher). Ils ne servent
qu'au tissu, hors périmètre, et Clarisse ne fait pas de retopologie.

### Dans quel ordre, si on y va

Trois remarques pour finir, qui valent plus que le classement lui-même.

**La liste A ne dépend pas de la liste B.** Le chanfrein shadé, les déformeurs,
la densité de segments, le panneau perforé, le tuyau orthogonal, le cadre et la
chaîne se font tous **sans booléen**. C'est important : le seul poste vraiment
cher n'est pas un préalable, et on peut livrer une bonne moitié de l'intérêt de
Fluent sans jamais écrire un CSG.

**Trois d'entre eux prolongent du code qui marche déjà.** A.3 est un correctif de
vingt lignes sur `tube` ; A.6 ajoute un mode de tracé à `curve_core.h` ; A.8
assemble `tube` et `curve_points`. Ce sont les trois premiers à faire, parce
qu'ils rapportent tout de suite et ne risquent rien.

**Et une chose est à essayer avant tout le reste, parce qu'elle est gratuite** :
poser des creases et un angle de lissage sur une géométrie générée, et regarder
si le rendu a encore besoin d'un chanfrein géométrique. Si la réponse est non,
B.2 et B.3 disparaissent tous les deux du budget — c'est-à-dire le second et le
troisième postes les plus chers de tout l'audit.

---

## 7. Les intrications avec Clarisse

C'est la question que Romain pose explicitement : qu'est-ce qui rendrait ces
outils **plus forts chez nous que chez Blender**. Neuf réponses.

**7.1 — Le graphe est l'ordre.** Développé en C.1. Ce n'est pas seulement du
travail en moins : c'est une différence de nature. Chez Fluent, ajouter un
nouveau type de modificateur au système coûte une entrée dans douze tables. Chez
nous, ça coûte un node.

**7.2 — Le coupeur est un `Group` réactif.** Un booléen dont l'entrée est un
`Group` avec `inclusion_rule` en mode Automatique ramasse tout seul chaque
nouveau coupeur créé dans un contexte. Fluent, lui, doit poser un modificateur de
plus par coupeur, le placer au bon rang, et balayer toute la scène pour retrouver
le lien inverse. Là où il a besoin de trois cents lignes de plomberie, une règle
suffit.

**7.3 — Les shading groups nommés, et les Shading Layers.** `PolyMesh::set()`
prend un shading group par polygone **et leurs noms**. Un node booléen qui nomme
`cut` les faces qu'il vient de créer donne à toutes les faces coupées de la scène
leur matériau intérieur par **une seule règle de Shading Layer**. Chez Blender il
faudrait un index de matériau par objet, posé à la main. C'est probablement le
gain le plus immédiat et le moins cher de toute la liste.

**7.4 — Les textures pilotent la géométrie.** `ShaderHelpers::evaluate_support_texture`
évalue une texture en chaque point d'un nuage. La taille des trous d'une grille
est une **shape key** chez Fluent — un seul nombre pour tout le panneau. Chez
nous ce serait un champ : les trous se resserrent près d'un bord, s'ouvrent au
centre, suivent une carte peinte. Même chose pour une densité de rivets, une
largeur de chanfrein, une profondeur d'inset. *(Signature lue dans `ix_shader.def`,
jamais appelée : à essayer avant d'en dépendre.)*

**7.5 — L'instanciation.** Les têtes de vis de Fluent sont de la géométrie
réalisée, jointe au maillage. Chez nous ce sont des instances, et
`sdk-clarisse.md` §10 explique pourquoi un mur de deux cent mille rivets ne coûte
presque rien : une courte liste de géométries de base plus des tableaux
parallèles, avec des matrices qui peuvent même être **générées à la demande**, et
des bundles qui s'imbriquent. Le seul outil de Fluent qui gagne un ordre de
grandeur en passant chez nous, c'est celui-là.

**7.6 — L'échelle.** Fluent ajuste le nombre de tuiles d'une grille en
**incrémentant un compteur dans une boucle Python** jusqu'à couvrir la
bounding box. Nous le calculerions. Et surtout : la question « est-ce que je peux
me permettre un million de tuiles » ne se pose pas de la même façon des deux
côtés.

**7.7 — Un jeu d'UV supplémentaire comme canal de données.** `PolyMesh::set()`
accepte **plusieurs jeux d'UV, en flottants à trois composantes**. Un node
booléen peut y écrire la **distance à l'arête de coupe** ; un node de perforation,
la distance au trou le plus proche. Ce champ alimente ensuite l'usure d'arête, la
salissure, un chanfrein shadé, une variation de rugosité — de la donnée pour
laquelle Blender aurait besoin d'un attribut nommé et Clarisse d'à peu près rien.
Les color maps existent aussi mais sont en 8 bits par canal, donc réservées à ce
qui tolère la quantification.

**7.8 — Le displacement et la subdivision viennent gratuitement.** C'est la
leçon de `curves-clarisse.md` §3, et elle vaut pour tout ce qui est proposé ici :
parce qu'on produit un vrai polymesh, le displacement, la subdivision Catmull-
Clark, le scatter, les UV et l'export marchent sans qu'on écrive une ligne pour
eux. C'était la raison de ne pas passer par `GeometryFur` ; c'est la même raison
de ne pas inventer un format intermédiaire pour le hard surface.

**7.9 — Les creases, plutôt que des boucles de support.** `PolyMesh::set_creases()`
existe, et `GeometryPolymesh` porte une tessellation **pilotée par les creases**
avec ses bornes min et max. Un node de hard surface peut donc **émettre des
creases au lieu de fabriquer des boucles de support** : moins de sommets, une
netteté réglable après coup, et une subdivision qui se raffine là où c'est
nécessaire. Fluent ne peut pas faire ça — son chanfrein de support est de la
géométrie, et il paie ensuite en réparation de normales. C'est la piste à essayer
**avant** d'écrire un node de bevel géométrique.

**7.10 — La surface courbe, qui est le trou de Fluent.** Nulle part dans Power
Trip un motif ne suit une surface courbe : ni les grilles, ni les vis au-delà de
la normale de leur face, ni les tôles. Le seul cas courbe est le cylindre
déplié, et c'est un cas particulier reconstruit à la main. Ce n'est pas un
oubli, c'est ce que coûte une architecture par pile : pour projeter un motif sur
une surface quelconque il faudrait un shrinkwrap sur une géométrie qui n'existe
pas encore au moment où le modificateur s'exécute. **Chez nous le node lit sa
surface d'accueil en entrée.** Il connaît ses positions, ses normales, sa
paramétrisation ; poser un motif dessus est une évaluation, pas une projection
inverse. C'est l'endroit où un node paramétrique bat structurellement une pile de
modificateurs, et ça ne demande aucun booléen.

---

## 8. Ce que je n'ai pas traité

- **Le panneau de tissu.** Hors périmètre, traité dans `cloth-clarisse.md`. Je
  note seulement, sans développer, que les préférences de l'addon exposent une
  résolution de remaillage, une pression, une raideur, une frame de fin, un
  nombre de boucles d'épinglage et le choix d'un remailleur externe — et que
  c'est la seule dépendance à un binaire tiers de tout l'addon.
- **La robustesse des booléens.** Constats au §3.9, développement dans
  `csg-clarisse.md`.
- **Les `.blend` binaires.** Je n'ai pu lire que les chaînes qu'ils contiennent —
  noms de nodes, noms et types de sockets d'interface, valeurs pilotées depuis le
  Python. Ce que fait exactement un node group à l'intérieur est donc **déduit**,
  pas lu. C'est signalé partout où ça s'applique.

---

## 9. Lu, et supposé

**Lu directement dans le code**, et vérifiable en une commande : l'identité et la
version de l'addon ; le recensement des 40 opérateurs et des 15 types de
modificateurs avec leurs comptes ; les 119 propriétés déclarées avec leurs
valeurs par défaut et leurs bornes ; les tables de `constants.py`, y compris
l'ordre de pile complet ; le fait que `solver = 'FLOAT'` est le seul solveur
employé ; les modes de limitation des chanfreins et leur répartition entre
`VGROUP` et `ANGLE` ; la formule de résolution automatique ; le contenu des
dossiers d'assets et le fait que les grilles sont des **objets** appendés, non
des node groups ; l'ajustement de la grille par incrémentation d'un compteur
avec relecture des dimensions, son plafond de 500 itérations et son
surdimensionnement de 2 % ; **l'absence totale de projection sur surface courbe
dans tout Power Trip**, vérifiée par recherche explicite ; le mécanisme de
pliage du cylindre — trois modificateurs dont on bascule la visibilité ; le
mécanisme des vis — sélection de faces, séparation en support, deux node groups,
une copie qui devient coupeur ; les sources complètes de `plate`, `text2mesh`,
`circular` et `becomefluent`.

**Lu côté Clarisse**, sur l'installation et le SDK reconstruit : la liste des 346
classes de référence et l'absence totale de CSG ; les attributs du Scatterer, du
Point Cloud, du Point Array, du Group, du ShadingLayer, de TextureCurvature, de
TextureExtractProperty, du Polymesh et du Displacement ; l'API publique de
`PolyMesh` ; le fait que `ModuleDeformerTopology` n'expose aucun moyen de changer
le nombre de points ; les champs de `CtxToolEvent` ; l'export de
`ShaderHelpers::evaluate_support_texture` et `evaluate_vertices_texture` dans
`ix_shader.def` ; et, dans notre propre code, la lecture d'un maillage d'entrée
par `quilt.cpp` et la double entrée géométrique de `cable_field.cpp`.

**Supposé**, et signalé comme tel dans le texte : le fonctionnement interne des
node groups livrés en `.blend`, déduit de leurs nodes et de leur usage ; le fait
que `FLOAT` soit bien le nouvel identifiant du solveur rapide dans l'énumération
de Blender 5.x ; le comportement de `evaluate_support_texture`, dont seule la
signature a été lue ; et les estimations de volume de code du §6, qui sont des
ordres de grandeur calés sur nos modules existants, pas des mesures.

**Une chose n'est ni lue ni supposée, elle est à essayer** : le geste interactif
d'un `Tool` dans Clarisse. Toute la partie A.9 en dépend, et `curves-clarisse.md`
le signale déjà comme le seul point ouvert du chantier des courbes. Un après-midi
avec la plume dans une session interactive lèverait la question pour de bon.
