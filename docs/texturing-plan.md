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
préalable a déplacé deux d'entre eux :

- **le bake n'est pas à écrire** — Clarisse sait déjà rendre en espace UV, cage
  comprise ; il est à rendre accessible ;
- **la peinture est plus faisable qu'espéré** — la question qui décidait de sa
  nature est tranchée, les UV sont disponibles au point d'impact d'un rayon.

C'est tout l'objet d'une préparation : ces deux constats auraient coûté
plusieurs jours s'ils étaient arrivés au milieu du code.

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

**Conclusion : on n'écrit pas de baker.** Le travail utile est de rendre celui-ci
accessible — un outil qui monte le layer correctement, gère les UDIM, la
dilatation des îlots et la sortie sur disque. C'est du Python, pas du C++.

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

**Classe de base : `Texture`.** Pas `TextureSpatial`, dont le groupe Projection
générique offrirait à l'artiste une douzaine de réglages qui n'ont pas de sens
pour une décale et qui se contrediraient avec les nôtres. On refait la
projection nous-mêmes, à partir du projecteur, ce qui est une dizaine de lignes.

**Le projecteur est un `Scope`.** C'est le choix qui décide de l'ergonomie : un
Scope a déjà une boîte manipulable dans le viewport, avec ses poignées. L'artiste
déplace donc sa décale comme il déplace n'importe quel objet, il la voit, et elle
suit l'animation. C'est aussi ce que font les presets existants — autant garder
le geste que tu connais déjà et supprimer seulement le câblage.

Et le Scope porte déjà **exactement** les réglages qu'il faut. Relevé dans un
preset :

```
Scope {
    translate ...   rotate ...   scale 0.864  0.345  0.032
    shape 0            # boite / autre forme
    falloff 0.0        # attenuation vers le bord du volume
    falloff_exponent 2.835
    inside_out no
}
```

Sa transformation définit donc le volume à elle seule : ramener la position du
point ombré dans l'espace local du Scope donne des coordonnées bornées à
[−1, 1], d'où l'UV se tire directement, et `|z| > 1` fait sortir du volume.
L'épaisseur en Z du Scope **est** la portée de la décale (0,032 dans le preset :
une dalle mince). Le `falloff` et l'`inside_out` du Scope se réutilisent tels
quels.

Conséquence : la node n'a besoin d'aucun réglage de placement propre. Elle lit
le projecteur, et tout le reste se manipule dans le viewport.

**Entrées**

| Attribut | Type | Rôle |
|---|---|---|
| `projector` | `reference` → `SceneItem` | le Scope (ou tout objet) qui définit le volume de projection |
| `color` | `reference` → `Texture` | l'image de la décale ; typiquement un `TextureMapFile` |
| `mask` | `reference` → `Texture` | masque optionnel, multiplié à l'alpha de l'image |
| `background` | `reference` → `Texture` | **ce qu'il y a dessous** — c'est ce qui rend la node chaînable |

**Sortie** : RGBA. Si `background` est vide, la node rend la décale seule, en
prémultiplié, avec son alpha de couverture. S'il est branché, elle rend le
composite. Une chaîne de dix décales, c'est dix nodes en série — **et le plafond
de six couches du `MaterialPhysicalMultiblend` disparaît.**

**Projection** — volontairement maigre, puisque le Scope porte le placement

| Attribut | Défaut | Rôle |
|---|---|---|
| `projection_type` | Parallèle | Parallèle (le volume du Scope) ou Conique (depuis une caméra) |
| `uv_offset`, `uv_scale`, `uv_rotation` | | recadrage de l'image **dans** la projection, sans toucher au Scope |
| `tile_mode` | Aucun | Aucun (découpe) / Répéter / Miroir |

**Atténuations** — c'est ce qui sépare une décale correcte d'une décale qui bave

| Attribut | Défaut | Rôle |
|---|---|---|
| `normal_angle` | 85° | rejeter les surfaces dont la normale s'écarte trop de l'axe |
| `normal_softness` | 0.2 | douceur de ce rejet |
| `use_scope_falloff` | oui | reprendre le `falloff` et le `falloff_exponent` du Scope |
| `edge_falloff` | 0.05 | sinon, adoucir le bord soi-même |
| `depth_falloff` | 0 | fondu le long de l'axe de projection |

Le test de normale est le point important : sans lui, la décale se plaque
aussi sur la face arrière de l'objet et s'étire sur les surfaces rasantes. C'est
le défaut le plus visible de toute projection naïve.

**Mélange**

| Attribut | Défaut | Rôle |
|---|---|---|
| `blend_mode` | Normal | Normal / Multiplier / Addition / Écran / Incrustation |
| `opacity` | 1.0 | dosage global |
| `invert_mask` | non | |

**Pour les autres canaux que la couleur** — rugosité, normale, métal — on branche
**une node par canal, partageant le même projecteur**. L'alpha calculé étant
identique, le masquage reste cohérent d'un canal à l'autre. C'est un peu verbeux
mais c'est idiomatique dans un node graph, et ça évite d'inventer une node à
sorties multiples que Clarisse ne sait pas afficher.

### 3.2 Outil de bake — Python, pas C++

Puisque `Layer3d` sait déjà baker, l'outil ne calcule rien : il **monte la scène
correctement**, ce que personne n'a envie de refaire à la main.

Un bouton de shelf qui, sur la sélection :

1. crée un `Image` et un `Layer3d`, active `uv_bake` dessus ;
2. renseigne la géométrie, le slot d'UV, la plage UDIM ;
3. règle la résolution, les échantillons, la cage si un high-poly est fourni ;
4. rend, et écrit les fichiers avec la bonne convention de nommage UDIM ;
5. **dilate les îlots** — le point que le bake natif ne fait probablement pas et
   sans lequel les coutures apparaissent au filtrage. À vérifier.

Reste à trancher : est-ce que la dilatation se fait en Python (lent mais simple)
ou dans un petit filtre d'image C++ réutilisable. La seconde option est
probablement meilleure, et c'est un `KernelFilter` de plus — terrain connu.

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

| Attribut | Rôle |
|---|---|
| `target` | la node `TexturePaint` dans laquelle on peint |
| `radius`, `hardness`, `opacity`, `flow` | la brosse |
| `color` | la couleur, ou une texture à tamponner |
| `blend_mode` | Normal / Multiplier / Éclaircir / Assombrir / Effacer |
| `pressure_size`, `pressure_opacity` | ce que la pression du stylet pilote |
| `mirror_x/y/z` | symétrie |

Le geste visé : on entre dans l'outil, on désigne la node cible, le curseur
affiche un disque projeté sur la surface sous la souris, et on peint. La
pression du stylet est disponible dans l'événement, donc l'atténuation par
pression est gratuite.

**C'est ici que se trouve le seul vrai risque du projet** — voir la section
suivante.

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

### Ce qui reste à établir, et qui est moins critique

- **Le confort interactif.** Le viewport de Clarisse re-shade de façon
  progressive ; un trait pourrait traîner derrière la souris. À mesurer tôt,
  parce que si c'est mauvais, ça change la conception.
- **Comment un `Tool` custom se rend accessible** : barre d'outils, menu,
  raccourci. Le mécanisme existe (`cb_get_actions` rend des `GuiAction`) mais le
  chemin exact jusqu'à l'interface n'est pas encore tracé.
- **Si le bake natif dilate les îlots** ou s'il faut l'ajouter.
- **La catégorie exacte** sous laquelle une texture custom apparaît dans les
  menus.

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

### Le `category` du CID

C'est lui qui décide dans quel sous-menu la node apparaît. Les trois nodes
d'optique déclarent `category "ImageFilter"` et `category "Camera"`, et
apparaissent au bon endroit. Pour une texture, la valeur exacte à employer est
**à confirmer** — la vérification en cours doit rendre la liste des chaînes
réellement utilisées par les textures livrées.

Deux options se présenteront :

- **une catégorie existante** (`Texture` ou une de ses sous-catégories) : nos
  nodes se mêlent aux natives, faciles à trouver par ordre alphabétique, et le
  suffixe `[ClarisseAdd]` fait le reste ;
- **une catégorie propre** : un sous-menu à nous, plus visible, mais qui oblige
  à savoir qu'il existe.

Recommandation : **catégorie existante**. Une décale se cherche là où on
cherche une texture, pas dans un tiroir séparé.

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

**1. La node décale.** Aucune inconnue technique, le mécanisme sous-jacent est
déjà là et le gain est immédiat : une node au lieu de six, et l'empilement
déplafonné. C'est aussi le meilleur terrain d'apprentissage pour la classe
`Texture`, dont les deux autres chantiers auront besoin.

**2. L'outil de bake.** Pas de C++ au départ, donc rapide. La seule inconnue est
la dilatation des îlots, et si elle manque c'est un `KernelFilter` de plus —
exactement ce qu'on sait déjà faire.

**3. La peinture.** Le morceau ambitieux. La question qui décidait de sa nature
est tranchée — les UV sont disponibles au point d'impact — donc c'est bien le
vrai outil de peinture qui est visé. Restent deux inconnues de mise en œuvre, à
lever par des sondes courtes avant d'écrire l'outil : le confort interactif du
viewport, et le chemin par lequel un `Tool` custom atteint l'interface.

Une remarque sur la méthode, tirée des trois nodes d'optique : le coût n'est
jamais dans l'algorithme, il est dans les hypothèses fausses sur ce que le
moteur fournit. Les deux jours perdus sur le bokeh l'ont été sur la convention
de la profondeur et sur la dé-prémultiplication des AOV, pas sur la convolution.
D'où les sondes préalables.
