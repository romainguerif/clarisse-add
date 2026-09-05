# Peinture, décales, bake — préparation

Document de conception. **Aucun code n'a été écrit.** Il sert à décider ce qu'on
construit, dans quel ordre, et à quoi ça ressemble pour l'artiste, avant d'ouvrir
un éditeur.

Trois besoins, formulés dans cet ordre :

1. peindre à la main directement sur les mesh — possible aujourd'hui via un
   système de particules, mais perfectible ;
2. un système de décales vraiment simple — une node directement accessible dans
   le node graph matériau ;
3. le bake.

Ils ne se ressemblent pas du tout en termes de risque, et la vérification
préalable a déplacé les trois :

- **le bake est déjà livré** — pas seulement le mécanisme : la fonctionnalité,
  son script, son bouton d'étagère, l'UDIM et la dilatation des bords. Il reste
  à l'essayer, pas à l'écrire ;
- **la décale a beaucoup moins à écrire que prévu** — la classe de base
  `TextureSpatial` apporte les neuf modes de projection et surtout un vrai test
  d'occlusion. Il ne manque que la composition et l'empilement ;
- **la peinture est faisable telle qu'on l'espérait** — les UV sont disponibles
  au point d'impact d'un rayon, et Clarisse livre même des blocs de paramètres
  de brosse réutilisables.

C'est tout l'objet d'une préparation. Chacun de ces trois constats aurait coûté
des jours s'il était arrivé au milieu du code, et deux d'entre eux ont
directement corrigé une décision de conception que j'avais déjà écrite.

**Une contrainte structurelle à connaître avant tout** : `cmagen` ne résout que
les classes de base **abstraites**. `Texture`, `TextureSpatial`,
`TextureOperator`, `Tool`, `UvSlot`, `Group`, `ShadingLayer` passent ; les
quinze `Tool*` concrets, `TextureTriplanar`, `TextureMapFile` et `Layer3d`
échouent tous. **On ne peut donc pas dériver d'un nœud livré**, seulement d'une
base. La cause de ce comportement n'est pas établie.

---

## 1. Ce qui existe déjà, vérifié avant de concevoir

C'est la première chose à établir, et elle a changé le plan.

### Le bake en UV existe nativement dans Clarisse

`ModuleLayer3d` porte `is_uv_bake_enabled()`, `get_uv_bake_config()`,
`get_uv_bake_slot()`, `get_uv_bake_range()`, et le groupe d'attributs
`Layer3d::Uv Baking` apparaît dans l'interface. Un Layer 3D sait donc **rendre
dans l'espace UV** au lieu de l'espace caméra : tout le pipeline — matériaux,
éclairage, GI — se bake dans la dépliure.

Et c'est un vrai bake, pas une version au rabais :

| Réglage | Valeurs | Ce que ça permet |
|---|---|---|
| `UvBakeEyeDirection` | `NORMAL` / `CAMERA` | bake indépendant du point de vue, ou vu d'une caméra |
| `UvBakeProjectionMode` | `NONE` / `INSIDE` / `OUTSIDE` / `INSIDE_AND_OUTSIDE` | **une cage** — transfert high-poly vers low-poly |
| `UvBakeProjectionNormal` | `FLAT` / `SMOOTH` | |

`UvBakeConfig::UvGeometry` porte à la fois la tessellation et le displacement,
donc la géométrie déplacée se bake aussi.

Et ce n'est pas qu'une API : **la fonctionnalité est livrée jusqu'au bouton.**
Un script de 590 lignes (`python3/process_uv_bake.py`) qui gère l'UDIM, les
séquences et la dilatation des bords ; un bouton dans l'onglet *Rendering* de
l'étagère par défaut ; un nœud `ImageFilterUVEdgePadding` pour la dilatation ;
une page de manuel dédiée. Ce qui est baké va du beauty complet aux AOV
arbitraires, jusqu'aux propriétés matière extraites par Light Path Expression.

**Conclusion : on n'écrit pas de baker, et on n'écrit probablement rien du
tout.** Le premier geste est de l'essayer.

### Les décales existent, mais se montent à la main

La classe de base `TextureSpatial` porte un groupe **Projection** complet, avec
sous-groupe caméra et transformations UVW. Les trois presets de décales de la
collection s'en servent : un `Scope` sert de projecteur, un `TextureMapFile`
projeté porte l'image, un `TextureGradient` fait l'atténuation, un
`TextureBumpMap` réoriente les normales, et l'empilement passe par un
`MaterialPhysicalMultiblend`.

Ça marche, mais :

- il faut **cinq à six nodes par décale**, et les câbler ;
- l'empilement est **plafonné à six couches** de matériau ;
- rien ne se déplace d'un bloc : bouger une décale veut dire bouger un Scope et
  espérer que le reste suive.

**Conclusion : le mécanisme existe, ce qui manque est une node qui le condense.**
C'est exactement le bon profil pour du C++ : peu de risque, gros gain.

### La peinture n'existe pas

Rien dans le jeu de classes livré ne peint sur une surface. Le contournement
actuel — un système de particules — en est la preuve.

---

## 2. Le système d'outils, et ce qu'il autorise

`Tool` est une classe de base dérivable : vérifié par sonde `cmagen`, elle se
résout contre `tool.dll` et produit un `.cma`.

`ModuleTool` expose exactement le jeu de callbacks qu'un pinceau demande :

| Callback | Ce qu'il donne |
|---|---|
| `cb_process_event` | souris, clavier, et **pression + inclinaison du stylet** |
| `cb_draw_tool_3d` / `cb_draw_tool_2d` | dessiner le curseur de brosse dans le viewport |
| `cb_get_options` | un panneau de réglages propre à l'outil |
| `cb_enter_tool` / `cb_leave_tool` | activation |
| `cb_get_actions` | raccourcis clavier |
| `cb_get_tool_bbox` | cadrage |

`CtxTool` fournit le point de vue, le contexte de dessin GL, et un
`image.can_raycast`.

Le détail qui compte : `CtxToolEvent` porte une structure `tablet` avec
`pressure`, `x_tilt`, `y_tilt`. **On ne met pas la pression du stylet dans un
événement d'outil si le système n'a pas été pensé pour peindre.** Quelqu'un chez
Isotropix avait ça en tête.

---

## 3. La liste des nodes et outils à faire

### 3.1 `TextureDecal` — « Decal [ClarisseAdd] »

Une node par décale, dans le node graph matériau, qui remplace les cinq à six
nodes du montage actuel.

**Classe de base : `TextureSpatial`.** C'est un changement d'avis, et la
vérification l'a imposé. J'avais d'abord écarté `TextureSpatial` en craignant
qu'elle impose à l'artiste une douzaine de réglages de projection génériques.
En regardant ce qu'elle contient réellement, c'est l'inverse : **elle apporte
gratuitement tout ce qu'une décale demande, et mieux que ce que j'aurais écrit.**

Ce dont on hérite sans écrire une ligne :

| Ce qu'on hérite | Pourquoi ça compte pour une décale |
|---|---|
| 9 modes de `projection` — Planar, Cylindrical, Spherical, Cubic, **Camera**, Parametric, Uv, Uv Property, Environment | la projection conique depuis une caméra vient gratuitement |
| **`camera_occlusion`** + `camera_occluders` | *« cherche un occulteur entre l'échantillon et la caméra ; s'il n'y en a pas, l'échantillon est mappé »* — **c'est le vrai test de visibilité**, celui qui empêche la décale de traverser l'objet et d'apparaître derrière |
| **`reference_object`** (ref `SceneItem`) | *« la texture utilise la matrice de transformation de l'objet pour calculer les coordonnées »* — le projecteur, déjà là |
| `projection_translate/rotate/scale` | placement fin |
| `uv_translate/rotate/scale`, **texturables** | recadrage de l'image dans la projection, pilotable par une autre texture |
| `object_space` (Object Base / Instance / World / Object Deformed) | comportement correct sur les instances et les déformations |
| `uv_slot`, `uv_property` | objets à plusieurs dépliures |
| `set_projection_space(...)` | l'aide statique *« pour une projection faite dans un espace arbitraire »* — écrite exactement pour ce cas |

Le `camera_occlusion` mérite qu'on s'y arrête : c'était **le défaut le plus
difficile** de la conception initiale — une projection naïve traverse l'objet et
recolle la décale sur sa face arrière. Je comptais l'approcher par un test
d'angle de normale, qui est un pis-aller. Clarisse fait le test de visibilité
pour de vrai.

**Ce qu'il reste réellement à écrire, et c'est tout :**

1. **la composition** — appliquer la décale par-dessus un shading existant, avec
   sa couverture ;
2. **l'empilement** — chaîner les décales sans plafond.

C'est peu, et c'est exactement ce qui manque au livré.

Le projecteur reste un `Scope` dans l'usage courant — il a une boîte
manipulable dans le viewport, on le déplace comme n'importe quel objet et il
s'anime — mais il passe désormais par `reference_object`, le mécanisme prévu,
au lieu d'une lecture maison.

**Ce qu'on ajoute par-dessus l'héritage**

| Attribut | Type | Rôle |
|---|---|---|
| `color` | `reference` → `Texture` | l'image de la décale ; typiquement un `TextureMapFile` |
| `mask` | `reference` → `Texture` | masque optionnel, multiplié à l'alpha |
| `background` | `reference` → `Texture` | **ce qu'il y a dessous** — c'est ce qui rend la node chaînable |
| `blend_mode` | long | Normal / Multiplier / Addition / Écran / Incrustation |
| `opacity` | double | dosage global |
| `invert_mask` | bool | |
| `tile_mode` | long | Aucun (découpe hors du cadre) / Répéter / Miroir |
| `normal_angle` | angle, 85° | rejeter les surfaces trop rasantes — complément du test d'occlusion, pas son remplaçant |
| `normal_softness` | percentage, 0.2 | douceur de ce rejet |
| `edge_falloff` | percentage, 0.05 | adoucir le bord de la décale |

**Sortie** : RGBA. Si `background` est vide, la node rend la décale seule, en
prémultiplié, avec son alpha de couverture. S'il est branché, elle rend le
composite. Une chaîne de dix décales, c'est dix nodes en série — **et le plafond
de six couches du `MaterialPhysicalMultiblend` disparaît.**

**Pour les autres canaux que la couleur** — rugosité, normale, métal — on branche
**une node par canal, partageant le même projecteur**. L'alpha calculé étant
identique, le masquage reste cohérent d'un canal à l'autre. C'est un peu verbeux
mais c'est idiomatique dans un node graph, et ça évite d'inventer une node à
sorties multiples que Clarisse ne sait pas afficher.

### 3.2 Bake — il n'y a probablement rien à écrire

Deuxième correction, plus radicale que la première. Le bake n'est pas seulement
dans le moteur : **il est déjà livré, avec son bouton, son script et sa
documentation.**

- Fonctionnalité **« UV Baking »**, groupe d'attributs `uv_baking` sur le
  `Layer3d` : `enable_uv_bake`, `uv_bake_geometry`, `uv_bake_slot`,
  `uv_bake_range`, `uv_bake_eye_direction`, `uv_bake_projection_mode`,
  `uv_bake_projection_normal`, `uv_bake_projection_offset`,
  `uv_bake_projection_distance`.
- Un script livré de 590 lignes, `python3/process_uv_bake.py`, qui **gère déjà
  l'UDIM, les séquences d'images et la dilatation des bords d'UV**.
- Un bouton d'étagère livré, `python3/shelves/rendering/uv_baker.py`, onglet
  **Rendering** de l'étagère par défaut.
- Un nœud de dilatation livré, **`ImageFilterUVEdgePadding`** — *« dilatation
  8-connexe sur les composantes connexes »*, `pixel` par défaut 8.

Ce qui est baké : le beauty complet avec lumières et GI, **plus des AOV
arbitraires**, **plus des propriétés matière via Light Path Expressions** —
normale d'ombrage, rugosité. Et le transfert haute vers basse définition se fait
par projection le long de la normale, avec cage `Inside`/`Outside`.

Autrement dit : ce que je proposais d'écrire existe, et en mieux.

**Ce qui reste à faire est donc de la vérification, pas du développement :**

1. **Tester que ça marche dans ton contexte** — c'est la première chose à faire,
   avant toute autre décision.
2. **Un point d'alerte sérieux** : le script de bake par lot dérive de
   `ProcessUvBake(ModuleProcessScriptEngine)`, donc de la classe `Process` — et
   `Process` **est verrouillée par licence** (réservée à BUiLDER). Le chemin
   `Layer3d.enable_uv_bake` est libre, mais **le confort du script par lot
   pourrait ne pas l'être en iFX**. C'est une déduction par héritage, documentée
   nulle part : à tester, pas à croire.
3. Si le script est effectivement bloqué, notre travail se réduit à en refaire
   l'équivalent **sans passer par `Process`** — un bouton d'étagère qui pilote
   directement le Layer 3D. Du Python, quelques dizaines de lignes.

Deux limites documentées à connaître : il est recommandé de **fermer les vues 3D
et Image** avant de lancer le bake par lot (le script change la frame courante),
et la dilatation des bords **s'appuie sur l'alpha** — elle n'a donc aucun effet
si l'alpha remplit toute la texture.

Voie annexe repérée, utile si le bake vers texture ne suffit pas : trois aides
exportées bakent une texture ou un matériau directement dans des **couleurs de
sommets** ou un **nuage de points** — `ShaderHelpers::evaluate_vertices_texture`,
`evaluate_support_texture`, `evaluate_support_material`.

### 3.3 Peinture — `TexturePaint` + `ToolPaint`

Deux morceaux indissociables : une node qui **détient** la peinture, et un outil
qui **écrit** dedans.

**`TexturePaint` — « Paint Layer [ClarisseAdd] »**, classe de base `Texture`.
Elle porte son propre tampon, l'expose au shading en le lisant aux UV du point,
et sait le sauvegarder.

| Attribut | Rôle |
|---|---|
| `resolution` | taille du tampon, par tuile |
| `udim_range` | quelles tuiles existent |
| `file_path` | où la peinture est sauvegardée |
| `save_now` / `reload` | boutons `action` |
| `background` | ce qu'il y a sous la peinture, pour rester chaînable |

**`ToolPaint` — « Paint [ClarisseAdd] »**, classe de base `Tool`.

**On ne réinvente pas la brosse.** Clarisse livre trois blocs de paramètres
réutilisables — `ToolBrush`, `ToolEraser`, `ToolSnapper` — qui ne sont **pas**
des outils mais des objets de réglage, référencés par ses deux outils de
peinture avec `filter "ToolBrush"` + `hidden yes` + `promote_attribute yes`.
On fait pareil, et on hérite d'un jeu de réglages déjà cohérent avec le reste
de l'application :

`brush_type` (View / Sphere / Flat Disk / Hemisphere / Projected Disk),
`brush_size`, `brush_thickness`, **`brush_unit` (Screen ou World)**,
`brush_density`, `brush_strength`, `brush_falloff`, **`brush_curve`** — une
vraie courbe d'atténuation éditable — et **`brush_pressure_mapping`**
(Density / Size / Density+Size / None).

Ça règle d'un coup deux des trois questions que je posais plus bas : l'unité de
la brosse et le pilotage par la pression sont **déjà tranchés par Clarisse**, et
de la même façon pour tous les outils. Autant s'y conformer.

Ce qui reste propre à notre outil :

| Attribut | Rôle |
|---|---|
| `target` | la node `TexturePaint` dans laquelle on peint |
| `brush` / `eraser` | références vers les blocs `ToolBrush` / `ToolEraser` |
| `color` | la couleur, ou une texture à tamponner |
| `blend_mode` | Normal / Multiplier / Éclaircir / Assombrir / Effacer |
| `mirror_x/y/z` | symétrie |
| `tool_type` | Pinceau / Aérographe / Gomme, comme `ToolParticlePaint` |

**La chaîne technique est établie de bout en bout** :

```
   CtxTool.image.x / .y          la position du curseur, avec can_raycast
        │
        ▼
   ShaderHelpers::raycast(inter, ray, eval_ctx, ctx, x, y, w, h, ...)
        │   « Cast a ray for the specified pixel and return the intersection »
        │   exporte par ix_shader ; variante ModuleWidget::raycast aussi
        ▼
   GeometryIntersection  (herite de GeometryFragment)
        │
        ▼
   hit.get_base_geometry()->compute_fragment_uvw(eval, hit, uv_index, uvw, ...)
        │
        ▼
   l'UV du texel a peindre
```

Un piège à ne pas manquer : **`fragment.get_u()` et `get_v()` ne sont PAS les UV
de texture.** La documentation les décrit comme *« parametric coordinates of the
fragment in the primitive »* — des coordonnées barycentriques dans la primitive.
La vraie UV ne s'obtient que par `compute_fragment_uvw`, qui prend en plus un
**index de jeu d'UV**. Confondre les deux donnerait un outil qui peint des
triangles au lieu d'une texture.

L'antécédent à copier existe : `python3/shelves/scatterer/scatterer_paint.py`
est le seul script livré qui active un outil, et il montre le geste exact.

---

## 4. Les gestes, avant les paramètres

Un tableau d'attributs ne dit pas si un outil est agréable. Ce qui suit décrit
ce que l'artiste **fait**, pas ce qu'il règle.

### Poser une décale

Aujourd'hui : fusionner un preset, retrouver le Scope, le déplacer, câbler cinq
nodes, et recommencer pour la deuxième décale — en sachant qu'à la septième le
matériau en couches sature.

Visé :

1. sélectionner l'objet, cliquer **Décale** ;
2. un `Scope` apparaît devant la caméra avec sa boîte manipulable, et la node
   est déjà branchée sur le matériau ;
3. déplacer le Scope, la décale suit — c'est le même geste que déplacer
   n'importe quel objet, et ça s'anime ;
4. choisir l'image dans la node ;
5. pour la suivante, recliquer : la nouvelle node se **chaîne** sur la
   précédente au lieu de consommer une couche de matériau.

Le point qui décide de l'ergonomie est le 3 : on manipule un objet visible dans
le viewport, pas un jeu de coordonnées dans un éditeur d'attributs.

### Baker

1. sélectionner la géométrie, cliquer **Baker UV** ;
2. l'outil monte le Layer 3D, désigne le slot d'UV, la plage UDIM, la
   résolution ;
3. régler ce qu'on veut baker, et lancer ;
4. les fichiers sortent nommés selon la convention UDIM, îlots dilatés.

Rien d'interactif : c'est une opération, pas un mode. D'où le choix d'un bouton
de shelf plutôt qu'une node avec un bouton `action` — sauf si on veut pouvoir
rejouer le bake d'un clic, auquel cas la node se justifie. **À trancher.**

### Peindre

1. sélectionner l'objet, cliquer **Peinture** ;
2. une node `TexturePaint` est créée et assignée, et l'outil s'active ;
3. le curseur affiche un disque **projeté sur la surface** — donc déformé par
   la perspective et par la courbure, ce qui est la seule façon de savoir où on
   peint réellement ;
4. peindre. La pression du stylet pilote la taille ou l'opacité, au choix ;
5. `[` et `]` changent le rayon, comme partout ailleurs ;
6. sauvegarder — explicitement, par un bouton, pas automatiquement.

Trois décisions à prendre, et elles comptent plus que les paramètres :

- **Le rayon est-il en pixels d'écran ou en unités de scène ?** En pixels
  d'écran, la brosse garde sa taille apparente quand on zoome — c'est ce que
  font les logiciels 2D, et c'est ce qu'on attend. En unités de scène, elle
  garde sa taille sur l'objet. Les deux se défendent ; le plus sûr est de
  proposer les deux, avec écran par défaut.
- **Que se passe-t-il au bord d'un îlot d'UV ?** Un coup de brosse à cheval sur
  une couture doit peindre les deux côtés, sinon la couture apparaît. Ça veut
  dire écrire aussi dans les texels voisins en espace UV, pas seulement sous le
  curseur. C'est le détail qui sépare un outil utilisable d'une démo.
- **Quand sauvegarde-t-on ?** Une sauvegarde automatique à chaque trait rend
  l'outil lent et remplit le disque ; aucune sauvegarde fait perdre le travail.
  Un bouton explicite plus un avertissement à la fermeture.

---

## 5. La question qui décidait de tout — tranchée

**Est-ce qu'un rayon lancé depuis l'outil rend les coordonnées UV du point
touché, ou seulement sa position 3D ?**

Tout l'outil de peinture en dépendait, et rien d'autre. Une brosse convertit une
position de souris en un texel : sans UV au point d'impact, cette conversion
n'existe pas et l'outil change de nature.

**Réponse : oui, les UV sont disponibles.** La chaîne est celle-ci :

```
   rayon
     │
     ▼
   GeometryIntersection            geometry_intersection.h:20
     │   class GeometryIntersection : public GeometryFragment
     │   -- une intersection EST un fragment
     ▼
   GeometryFragment                geometry_fragment.h:143
     │   primitive_id, sub_primitive_id, uvw, sub_uvw
     ▼
   GeometryObject::compute_fragment_uvw(CtxEval, GeometryFragment, index, ...)
     │   methode VIRTUELLE, exportee par ix_geometry
     ▼
   UVW
```

Établi par les symboles exportés de `ix_geometry.def` et par l'en-tête : la
méthode est virtuelle sur `GeometryObject`, donc chaque type de géométrie sait
répondre, et elle prend un **index de jeu d'UV** — donc les objets à plusieurs
dépliures sont gérés.

Deux exports voisins confirment que le terrain est complet : **`GeometryUvMap`**
et **`GeometryUvTile`** (construit depuis deux entiers — les coordonnées d'une
tuile UDIM). Le support UDIM est donc présent dans le moteur, pas à réinventer.

**Conséquence : c'est le vrai outil de peinture qui est faisable.** On peint
dans une texture UV, qui produit un fichier réutilisable, exportable, retouchable
ailleurs. Pas un succédané à base de nuage de points.

Le repli n'a plus lieu d'être, mais il reste noté au cas où la mise en œuvre
buterait : peindre dans une vue 2D de la dépliure, où la conversion
souris → texel est directe.

### Ce qui reste ouvert

La vérification a refermé presque tout. Il subsiste trois choses, dont une
seule est un risque.

- **Le confort interactif.** Le viewport re-shade de façon progressive ; un
  trait pourrait traîner derrière la souris. **C'est le seul vrai risque
  restant**, et il se mesure en une sonde courte : un outil minimal qui ne fait
  que dessiner un disque sous le curseur suffit à savoir si le geste répond.
- **Le verrou de licence sur `Process`**, qui décide si le script de bake livré
  est utilisable en iFX (voir 3.2). À tester, pas à supposer.
- **Le nombre de classes verrouillées** : notre mesure antérieure disait 49, la
  documentation livrée en démontre 36. L'écart n'est pas expliqué — la doc ne
  couvre que 346 classes alors que l'installation porte 134 DLL, donc des
  classes verrouillées sans page de documentation sont possibles. Sans
  conséquence pour nous, mais à ne pas oublier dans la référence.

---

## 6. Où ça se range, sous quel nom

### Les noms

On garde la convention des trois nodes d'optique : **le suffixe
`[ClarisseAdd]` dans le `ui_name`**. Ce n'était pas une coquetterie — sans lui,
devant une liste de nodes, on ne sait plus si le flou qu'on règle est celui de
Clarisse ou le nôtre. Le problème sera pire ici, puisque nos nodes vivront à
côté des textures natives dans le même menu.

| Classe | `ui_name` | Base | Où elle apparaît |
|---|---|---|---|
| `TextureDecal` | Decal [ClarisseAdd] | `Texture` | menu de création de textures, node graph matériau |
| `TexturePaint` | Paint Layer [ClarisseAdd] | `Texture` | idem |
| `ToolPaint` | Paint [ClarisseAdd] | `Tool` | à déterminer — voir plus bas |

Les noms des classes suivent la convention de Clarisse : préfixe de famille
puis rôle (`TextureMapFile`, `TextureGradient` → `TextureDecal`). Ça les range
correctement dans les listes triées et ça évite les collisions.

### Le `category` du CID — et il n'est pas optionnel

C'est lui qui décide dans quel sous-menu la node apparaît, et **une classe sans
`category` n'apparaît dans aucun menu**. Le code de peuplement des menus la
saute purement et simplement.

Les chaînes réellement employées par les textures livrées, relevées sur les 346
pages de référence :

| Catégorie | Combien | Ce qu'on y trouve |
|---|---|---|
| `/Texture/Utility` | 25 | `TextureTriplanar`, `TextureCurvature`, `TextureWireframe`, `TexturePointCloud` |
| `/Texture/Color` | 18 | |
| `/Texture/Math` | 18 | |
| `/Texture/Procedural` | 5 | |
| `/Texture/Map` | 4 | `TextureMapFile`, `TextureVertexColorMap` |
| `/Texture/Normal` | 2 | |
| `/Texture/Raytrace` | 1 | `TextureOcclusion` |

**Chaîne retenue : `category "/Texture/Utility"`**, avec la barre oblique
initiale. Les classes de base `Texture` et `TextureOperator` déclarent
`"Texture"` sans barre — c'est une incohérence héritée, à ne pas imiter.

L'attribut frère est **`ui_weight`**, qui ordonne les entrées dans le menu : les
outils livrés vont de 100 (Picker) à 30 (Render Region). À renseigner, sinon nos
nodes tombent en fin de liste.

### Comment un outil atteint l'interface — rien à écrire

Découverte utile : **un `Tool` s'expose sans une seule ligne d'enregistrement.**
Au démarrage, un script Python énumère par réflexion les classes dérivées de
`Tool` dans la fabrique, les trie par `ui_weight`, saute celles qui sont
abstraites, verrouillées, ou **sans `category`**, et construit le menu.

Chaque sous-classe possède une instance unique dans le contexte `tools://`, et
« activer un outil » consiste à poser cet objet dans le slot de sélection
`"tools"` :

```python
tool = ix.application.get_factory().get_object("tools://paint_clarisse_add")
ix.application.get_selection().set_all_slots_selection("tools", tool)
```

Le panneau d'options est **généré** par `WidgetToolOptions` à partir du CID :
`attribute_group`, `preset`, `slider`, `ui_range`, `collapsed`,
`promote_attribute` **sont** l'interface. Le callback `cb_get_options` ne sert
que pour une UI sur mesure.

Seule chose non automatique : **le raccourci clavier**. Il n'existe aucun
fichier de configuration de raccourcis ; ceux des outils livrés sont un
dictionnaire Python en dur, et les outils de peinture livrés n'en ont aucun. On
peut en poser un via `cb_get_actions`, qui rend des `GuiAction` portant leur
propre raccourci, actifs tant que l'outil l'est.

### Les boutons de shelf

Le shelf compte aujourd'hui neuf catégories `ClarisseAdd*` et 48 boutons. On en
ajoute une : **`ClarisseAdd Texturing`**.

| Bouton | Ce qu'il fait |
|---|---|
| Décale | crée un `Scope` + un `TextureDecal` déjà câblés, et branche le tout sur le matériau sélectionné |
| Baker UV | monte le Layer 3D de bake sur la sélection, avec des réglages sains |
| Peinture | crée un `TexturePaint`, l'assigne, et entre dans l'outil |

Le point important est le **premier** : un bouton qui crée le Scope *et* la
node *et* les câble supprime l'essentiel de la corvée actuelle. La node seule
laisserait encore trois branchements à faire à la main.

À prévoir : `assets/icons/` n'a **aucune icône** pour la catégorie Optique, ce
qui fait afficher les boutons en texte. À ne pas reproduire.

---

## 7. Ordre de travail proposé

Trois chantiers de risque très inégal. L'ordre suit le rapport bénéfice sur
risque, pas l'ordre dans lequel tu les as demandés.

**0. Essayer le bake livré.** Une heure, aucune ligne de code, et ça peut
supprimer un chantier entier de la liste. À faire avant tout le reste, et le
seul point à surveiller est le verrou de licence sur `Process`.

**1. La node décale.** Aucune inconnue technique, et le périmètre a fondu :
`TextureSpatial` apporte la projection et l'occlusion, il ne reste que la
composition et l'empilement. Une node au lieu de six, et le plafond de six
couches qui disparaît. C'est aussi le meilleur terrain d'apprentissage pour la
famille `Texture`, dont la peinture aura besoin.

**2. La peinture.** Le morceau ambitieux, mais moins risqué qu'annoncé : la
chaîne curseur → rayon → intersection → UV est établie de bout en bout et
entièrement exportée, les paramètres de brosse sont fournis par `ToolBrush`, et
l'outil s'expose dans l'interface sans une ligne d'enregistrement. À attaquer
par **une sonde de confort interactif** — un outil qui ne fait que dessiner un
disque sous le curseur — parce que c'est la seule chose qui pourrait encore
changer la conception.

Une remarque sur la méthode, tirée des trois nodes d'optique : le coût n'est
jamais dans l'algorithme, il est dans les hypothèses fausses sur ce que le
moteur fournit. Les deux jours perdus sur le bokeh l'ont été sur la convention
de la profondeur et sur la dé-prémultiplication des AOV, pas sur la convolution.
D'où les sondes préalables.
