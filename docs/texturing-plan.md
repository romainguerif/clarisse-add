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

Ils ne se ressemblent pas du tout en termes de risque. Le résultat de la
vérification préalable est d'ailleurs qu'**un des trois n'est probablement pas à
écrire du tout**.

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

**Projection**

| Attribut | Défaut | Rôle |
|---|---|---|
| `projection_type` | Parallèle | Parallèle (boîte) ou Conique (depuis une caméra) |
| `use_projector_shape` | oui | prendre les dimensions du Scope au lieu des réglages ci-dessous |
| `size` | 1, 1 | largeur et hauteur, en unités du projecteur |
| `depth_range` | 0, 1 | jusqu'où la décale porte le long de l'axe |
| `offset`, `scale`, `rotation` | | placement 2D dans la projection |
| `tile_mode` | Aucun | Aucun (découpe) / Répéter / Miroir |

**Atténuations** — c'est ce qui sépare une décale correcte d'une décale qui bave

| Attribut | Défaut | Rôle |
|---|---|---|
| `normal_angle` | 85° | rejeter les surfaces dont la normale s'écarte trop de l'axe |
| `normal_softness` | 0.2 | douceur de ce rejet |
| `edge_falloff` | 0.05 | adoucir le bord de la décale elle-même |
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

## 4. La question qui décide de tout

**Est-ce qu'un rayon lancé depuis l'outil rend les coordonnées UV du point
touché, ou seulement sa position 3D ?**

Tout l'outil de peinture en dépend, et rien d'autre n'en dépend. Une brosse
convertit une position de souris en un texel : sans UV au point d'impact, cette
conversion n'existe pas.

Les deux issues possibles, et elles ne mènent pas au même produit :

**Si les UV sont disponibles** — on peint directement dans une texture UV. C'est
le vrai outil de peinture, celui qui donne un fichier qu'on peut retoucher
ailleurs, réutiliser, exporter.

**Si elles ne le sont pas** — on peut toujours peindre, mais dans une
représentation en espace 3D : nuage de points, attribut de sommet, ou texture
volumique. C'est-à-dire une version bien meilleure de ce que tu fais déjà en
particules — brosse correcte, pression du stylet, symétrie, sauvegarde — mais
pas une texture UV.

Les deux valent le coup d'être construits. Ils n'ont simplement pas la même
promesse, et il serait malhonnête de t'annoncer le premier avant d'avoir la
réponse.

Ce qui reste à établir en même temps, et qui est moins critique :

- Le confort interactif. Le viewport de Clarisse re-shade de façon progressive ;
  un trait pourrait traîner derrière la souris. À mesurer tôt, parce que si
  c'est mauvais, ça change la conception — on peindrait alors dans une vue
  dédiée plutôt que dans le viewport 3D.
- Comment un `Tool` custom se rend accessible : barre d'outils, menu, raccourci.
- Si le bake natif dilate les îlots ou non.

---

## 5. Ordre de travail proposé

Trois chantiers de risque très inégal. L'ordre suit le rapport bénéfice sur
risque, pas l'ordre dans lequel tu les as demandés.

**1. La node décale.** Aucune inconnue technique, le mécanisme sous-jacent est
déjà là et le gain est immédiat : une node au lieu de six, et l'empilement
déplafonné. C'est aussi le meilleur terrain d'apprentissage pour la classe
`Texture`, dont les deux autres chantiers auront besoin.

**2. L'outil de bake.** Pas de C++ au départ, donc rapide. La seule inconnue est
la dilatation des îlots, et si elle manque c'est un `KernelFilter` de plus —
exactement ce qu'on sait déjà faire.

**3. La peinture.** Le morceau ambitieux, et le seul dont la promesse dépend
d'une réponse qu'on n'a pas encore. À attaquer par une sonde courte sur la
question des UV, **avant** d'écrire quoi que ce soit d'autre.

Une remarque sur la méthode, tirée des trois nodes d'optique : le coût n'est
jamais dans l'algorithme, il est dans les hypothèses fausses sur ce que le
moteur fournit. Les deux jours perdus sur le bokeh l'ont été sur la convention
de la profondeur et sur la dé-prémultiplication des AOV, pas sur la convolution.
D'où les sondes préalables.
