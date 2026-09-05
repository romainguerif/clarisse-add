# Les trois nodes d'optique — état des lieux

Trois modules C++ natifs, écrits pour Clarisse iFX 5.0 SP14 : un filtre de
profondeur de champ, une caméra qui fait le flou au tir du rayon, et un filtre
d'aberration chromatique.

Ce document dit **où on en est** : ce qui marche et a été vérifié au rendu, ce
qui reste incertain, ce qui manque. Le fonctionnement du SDK lui-même est dans
[`sdk-clarisse.md`](sdk-clarisse.md) ; l'inventaire des paramètres est plus bas.

---

## 1. Ce que sont ces trois nodes, et pourquoi trois

La référence explicite du projet est **Bokeh de Peregrine Labs** (racheté par
Foundry en 2022, aujourd'hui nœud natif de Nuke). L'objectif n'est pas de s'en
inspirer mais d'en reprendre les fonctions et les paramètres.

Une profondeur de champ peut se calculer à deux endroits, et les deux ont leur
usage :

| | **Bokeh [ClarisseAdd]** — filtre | **Camera Bokeh [ClarisseAdd]** — caméra |
|---|---|---|
| Où | filtre d'image sur un Layer 3D | classe de caméra, à la place d'une perspective |
| Quand | après le rendu | au tir du rayon |
| Coût | se règle sans relancer le rendu | il faut re-rendre |
| Qualité des bords | approximative aux ruptures de profondeur | exacte, aucun artefact |
| Ce qu'il sait faire | tout le contrôle artistique | la vérité optique |

Le troisième, **Chromatic Aberration [ClarisseAdd]**, est séparé parce que
l'aberration chromatique *ne peut pas* se faire sur la caméra : `RayGeneratorData`
ne porte aucune longueur d'onde et le moteur est RVB, pas spectral. Elle se fait
donc en post, et comme elle n'a rien à voir avec la mise au point, elle a sa
propre node plutôt que d'encombrer les deux autres.

Les trois portent le suffixe `[ClarisseAdd]` dans leur nom d'interface. C'était
une demande explicite : sans ça, on ne sait plus si le flou qu'on règle est
celui de Clarisse ou le nôtre.

---

## 2. Ce qui est vérifié, et comment

Rien de ce qui suit n'est « ça devrait marcher » : chaque ligne correspond à un
rendu regardé ou à une valeur mesurée.

### Le filtre Bokeh

| Ce qui est vérifié | Comment |
|---|---|
| Formes de diaphragme | rendus : hexagone, lames concaves, anamorphique, œil-de-chat |
| Mise au point pilotée par l'AOV de profondeur | la nettété se déplace entre deux sphères ; RMS distincts |
| Mise au point en visant un objet | distances journalisées **14** et **38**, exactement les profondeurs attendues |
| Profondeur projetée sur l'axe, pas radiale | `depth.Z` mesurée à 12,72 là où la distance vaut 13,58 |
| Tranches correctives | le liseré autour d'un sujet net disparaît ; un premier plan flou déborde enfin sur ce qui est derrière |
| Correction de la profondeur des silhouettes | la CoC signée passe de −0,61 à **0,0** dans une scène sans premier plan flou |
| Les cinq sorties de réglage | rendues et regardées : l'hexagone s'affiche, le rouge tombe sur la sphère nette |
| Objectif réel | rapport des rayons f/1.4 ÷ f/16 = **11,43**, soit exactement 16/1,4 |
| Table des unités de scène | la même optique en centimètres sature le plafond de rayon |

### La caméra Bokeh

Œil-de-chat et anamorphisme vérifiés au rendu. La profondeur de champ vient du
générateur de rayons, donc elle n'a par construction aucun artefact de bord.

### L'aberration chromatique

Frange orange nettement visible à 18 %. Les trois blocs — décalage latéral,
longitudinal, frange — se voient séparément.

---

## 3. Inventaire des paramètres

### 3.1 Bokeh — `ImageFilterBokeh`, base `KernelFilter`, `bokeh.dll`

Se pose sur un **Layer**, groupe *Filters* de l'éditeur d'attributs.

**Output**

| Attribut | Type | Défaut | Rôle |
|---|---|---|---|
| `output_type` | long | 0 | 0 image, 1 bloom seul, 2 visualisation du point, 3 forme du noyau, 4 mattes |
| `normalize_visualization` | bool | oui | borne la visualisation à 0–1 ; décochée, les canaux portent le rayon en pixels |

**Lens** — le mode physique, désactivé par défaut

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `real_world_lens` | bool | non | | calcule le flou par la lentille mince ; neutralise Radius, Focus Range, Blur Falloff |
| `lens_from_camera` | bool | non | | reprend focale et ouverture sur la caméra du layer |
| `focal_length` | double | 35.0 | 1–2000 | focale en mm |
| `f_stop` | double | 5.6 | 0.5–64 | ouverture |
| `film_format` | long | 0 | 8 presets | 35 Academy, Super 35, 24×36, APS-C, Super 16, 65 mm, M4/3, Personnalisé |
| `aperture_width` / `_height` | double | 21.95 / 16.0 | 0.1–200 | capteur en mm ; piloté sauf en Personnalisé |
| `world_scale` | long | 2 (m) | 5 presets | mm, cm, m, pouces, pieds |
| `world_scale_multiplier` | double | 1.0 | | pour les scènes modélisées en blocs |
| `max_kernel_size` | long | 128 | 1–1024 | plafond du rayon ; au-delà le flou est **écrêté** |

**Aperture**

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `radius` | subpixel | 8.0 | 0–250 | rayon du bokeh en pixels ; avec profondeur, c'est le **maximum** |
| `blades` | long | 0 | 0–32 | 0 = disque, 5–9 = polygone des optiques diaphragmées |
| `rotation` | angle | 0 | | rotation du diaphragme |
| `roundness` | percentage | 0 | −1…1 | 0 polygone, +1 disque, −1 étoilé |
| `softness` | percentage | 0 | 0–1 | douceur du bord de l'ouverture |
| `anamorphism` | percentage | 0 | −1…1 | ovalisation ; positif = vertical |

**Focus**

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `depth_aov` | tag `aov_groups` | vide | | AOV de profondeur ; vide = flou uniforme |
| `focus_object` | reference `SceneItem` | vide | | objet visé ; rend `focus_distance` inutile mais **exige l'AOV** |
| `depth_mode` | long | 0 | | 0 distance réelle, 1 inverse (1/z) |
| `focus_distance` | distance | 10.0 | 0–1e6 | plan de netteté, le long de l'axe caméra |
| `focus_range` | distance | 2.0 | 0–1e6 | épaisseur de la zone nette |
| `blur_falloff` | double | 1.0 | 0.1–8 | vitesse de montée du flou |
| `focus_side` | long | 0 | | 0 les deux, 1 arrière, 2 avant |
| `front_multiplier` | double | 1.0 | 0–10 | flou du côté proche seulement |
| `back_multiplier` | double | 1.0 | 0–10 | flou du côté lointain seulement |

**Slices**

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `corrective_slices` | long | 10 | 1–64 | qualité des **bords**, pas quantité de flou ; 1 = passe unique |

**Highlights**

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `threshold` | double | 1.0 | | seuil de reprise, en luminance linéaire |
| `gain` | double | 0.0 | 0–100 | 0 = neutre, conserve l'énergie exactement |
| `bloom_curvature` | double | 2.0 | 0–8 | module la reprise selon l'endroit du cadre ; 1 = uniforme |

**Optics**

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `preserve_exposure` | bool | non | | garde l'amande du vignettage sans son assombrissement |
| `optical_vignetting` | percentage | 0 | 0–1 | œil-de-chat vers les bords |
| `spherical_aberration` | percentage | 0 | −1…1 | + anneau « bulle de savon », − bokeh crémeux |
| `chromatic_aberration` | percentage | 0 | −1…1 | **dosage** de la frange |
| `chromatic_offset` | color | 0.6 1 1 | | **teinte** de la frange, défaut de Peregrine |

### 3.2 Camera Bokeh — `CameraBokeh`, base `Camera`, `bokeh_camera.dll`

Se choisit comme `active_camera` d'un Layer 3D. Les trois premiers groupes sont
repris **verbatim** du CID extrait de `CameraPerspectiveAdvanced` — cmagen ne
sait pas résoudre cette classe de base, qui vit dans `cameras.dll`. Ils n'ont
donc aucune infobulle.

**camera** : `horizontal_field_of_view` (39.6°), `vertical_field_of_view`
(26.99°), `focal_length` (0.05, en unités de scène).

**camera > film_back** : `horizontal_aperture` (0.036), `vertical_aperture`
(0.024), `film_offset` (0, 0), `lens_ratio` (1), `fit_mode` (Horizontal),
`overscan` (caché).

**camera > depth_of_field** : `enable_dof` (non), `f_stop` (5.6),
`focus_distance` (5), `focus_object`. Les trois derniers sont `read_only` dans
le CID ; **le module lève le verrou** quand `enable_dof` passe à vrai.

**bokeh**

| Attribut | Type | Défaut | Rôle |
|---|---|---|---|
| `enable_bokeh` | bool | oui | décoché, on retombe sur le disque uniforme |
| `blades` | long | 0 | 0 = disque parfait |
| `blade_rotation` | angle | 0 | |
| `blade_curvature` | percentage | 0 | 0 polygone, +1 disque, −1 étoilé |
| `aperture_softness` | percentage | 0 | |
| `anamorphism` | percentage | 0 | |

**bokeh > optics** : `optical_vignetting`, `spherical_aberration`,
`aperture_swirl` (le bokeh tourbillonnant des Petzval — n'existe pas dans le
filtre).

### 3.3 Chromatic Aberration — `ImageFilterChromaticAberration`, base `KernelFilter`

Trois blocs séparés, chacun neutre à zéro.

**lateral** — le décalage radial, celui qui colore les bords du cadre

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `lateral_amount` | percentage | 0 | 0–0.5 | décalage max en % de la demi-diagonale ; 0,04–0,15 % sur une vraie optique |
| `lateral_spectrum` | color | 1, 0.5, 0 | | position de chaque canal sur le segment de dispersion |
| `lateral_falloff` | double | 1.0 | 0.5–4 | exposant de la croissance radiale |

**longitudinal** — le flou différentiel entre canaux, uniforme sur le cadre

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `longitudinal_amount` | subpixel | 0 | 0–32 | en pixels |
| `longitudinal_defocus` | color | 1, 0, 1 | | part de flou par canal ; le défaut garde le vert net |

**fringe** — la frange violette, qui est du blooming seuillé et non de
l'aberration chromatique

| Attribut | Type | Défaut | Plage | Rôle |
|---|---|---|---|---|
| `fringe_amount` | double | 0 | 0–4 | intensité |
| `fringe_threshold` | double | 1.0 | | seuil sur `max(r, g, b)` |
| `fringe_knee` | percentage | 0.5 | 0–1 | adoucit le passage du seuil, contre le scintillement |
| `fringe_radius` | pixel | 4 | 0–64 | étendue |
| `fringe_color` | color | 0.55, 0.25, 1 | | teinte |

**common** : `center` (0, 0 — le centre optique) et `samples` (7, forcé impair).

### 3.4 Comment le filtre calcule, en deux chemins

Savoir lequel est pris change tout ce qu'on observe.

```
                     depth_aov résolu ?
                    /                  \
                  non                  oui
                   |                     |
                   |          corrective_slices >= 2 ?
                   |            /              \
                   |          non              oui
                   ▼           ▼                ▼
             flou uniforme  échelle de      TRANCHES
                            12 paliers      rayon constant
                                            + composite
```

**Chemin à tranches** — pris quand l'AOV est résolu **et** que
`corrective_slices ≥ 2`. Chaque tranche est extraite, floutée seule à rayon
constant en prémultiplié, puis composée de l'arrière vers l'avant. Une tranche
absente de la tuile est sautée après une simple passe linéaire, ce qui rend le
coût très inférieur au nombre de tranches sur une scène ordinaire.

**Chemin à passe unique** — tout le reste. Le rayon varie par pixel, en 12
paliers (`DEPTH_STEPS`), parce que `pre_filter` ne peut déclarer qu'un seul
`kernel_radius` pour toute l'image. C'est le chemin rapide, et le chemin fautif
aux ruptures de profondeur.

Les modes de réglage (`output_type ≥ 2`) court-circuitent les deux.

Deux points de méthode qui expliquent la structure du code :

- **Tout ce qui est global à l'image se calcule dans `pre_filter`** : la
  profondeur, la mise au point sur objet, l'optique, et surtout les bornes des
  tranches. Calculées par tuile, deux tuiles voisines appliqueraient des rayons
  différents à la même profondeur — et la couture se verrait.
- **Les réglages se relisent dans une copie locale à chaque tuile.** Un global
  partagé se fait écraser par une seconde instance du filtre ou par une
  évaluation concurrente.

---

## 4. Ce qui ne marche pas encore

### Un défaut identifié, diagnostiqué, non corrigé

**Le composite à tranches perd de l'alpha** dans la bande située juste à
l'intérieur de la silhouette d'un premier plan flou. La cause est comprise : la
tranche du fond y a un trou — la géométrie cachée n'a jamais été rendue — et
rien ne le comble, donc la couverture accumulée n'atteint pas 1.

Deux conséquences, l'une visible et l'autre insidieuse :

- une bande légèrement sombre au bord intérieur d'un premier plan très flou ;
- **tous les AOV sont corrompus** dans cette bande, parce que Clarisse les
  dé-prémultiplie par l'alpha de sortie. Mesuré : l'AOV de profondeur ressortait
  à **251** au lieu de 80.

Le correctif est identifié — ne jamais laisser la couverture descendre sous ce
que donnerait un simple flou de l'alpha, en prenant pour référence l'alpha
source floutée par le noyau de la tranche à laquelle le pixel appartient — mais
**il n'est pas écrit**.

À noter que Peregrine documente la même famille de problème sans la résoudre :
« due to the nature of this corrective stage the overall Bokeh level may need to
be increased ». Leur nœud ne fait ni bouchage de trou ni étalement d'alpha.

### Quatre défauts trouvés à la relecture, non corrigés

Repérés en relisant le code pour rédiger ce document. Aucun n'a été mesuré, donc
chacun reste à confirmer — mais chacun est plausible à la lecture.

**La marge réservée pour l'aberration chromatique est trop petite.**
`pre_filter` demande `radius × (1 + 0,18 × |chromatic|)`, une constante héritée
de l'époque où l'écart entre canaux était symétrique et fixe. Mais `filter`
applique désormais `1 + chromatic × (chroma_offset[c] − 1)`, qui avec l'offset
par défaut (0.6, 1, 1) et un dosage **négatif** monte à **1,6 ×** sur le rouge.
Le noyau déborde alors la marge du proxy et se fait tronquer.

**`focus_object` de la caméra ne sert à rien.** Il est déclaré dans le CID,
et `update_dof_lock` prend soin de le déverrouiller — mais **aucune ligne de
`bokeh_camera.cpp` ne le lit**. `create_ray_generator` et `get_config` ne
prennent que `focus_distance`. Il est possible que Clarisse le résolve en amont
via son `input "motion_translate"`, mais rien ne le fait dans notre code.

**Le flou longitudinal de l'aberration chromatique écrase le décalage latéral**
au lieu de s'y composer (`chroma.cpp:358` : `value = sum / norm`). Les deux
effets ne peuvent donc pas s'additionner. Rien dans le code n'indique si c'est
voulu.

**Le bouton « Aberration Chromatique » affiche le message du Bokeh.**
`_add_filter_class` reçoit un `title` et une `note` qu'il n'utilise pas ; le
message final est codé en dur. Cosmétique, mais déroutant.

À quoi s'ajoute un détail : **aucune icône `optics_*` n'existe** dans
`assets/icons`, donc les quatre boutons du shelf s'affichent avec leur titre.

### La limite structurelle du flou en 2D

Un filtre appliqué après le rendu ne sait pas ce qu'il y a **derrière** les
objets : cette information n'a jamais été calculée. Les tranches correctives
ramènent le défaut à une bande étroite, elles ne le suppriment pas. La node
caméra, elle, n'a pas ce problème.

C'est le partage d'usage : arrière-plan flou → le filtre suffit ; premier plan
flou → la caméra.

---

## 5. Ce qui manque par rapport à Peregrine

Établi par recherche documentaire (voir `J:\Clarisse-SDK\NOTES_peregrine_bokeh.md`,
et une seconde passe qui a exhumé le changelog archivé du plugin).

| Poste Peregrine | État |
|---|---|
| `correctiveSlices` | **fait** |
| `frontmultiplier` / `backmultiplier` | **fait** |
| `outputType` — 5 visualisations | **fait** |
| Objectif réel (`focalLength`, `fStop`, `filmFormat`, `worldScale`) | **fait** |
| `bloomCurvature`, `kChrAbbOff` | **fait** |
| `depthStyle`, `focalPlane`, `focusRegionSize`, `kSoftness`, lames, courbure | **fait** |
| `Kernel Type = Input` — ouverture par image | **manque** |
| Canaux de matte par effet (defocus / bloom / aberration) | **manque** |
| Entrée Deep | hors périmètre : Clarisse n'expose pas de deep aux filtres d'image |

Deux ajouts qui **n'existent pas** chez Peregrine et qu'il faut assumer comme
tels : le vignettage optique (œil-de-chat) et la mise au point en visant un
objet. Peregrine n'a ni l'un ni l'autre.

---

## 6. Ce qui reste à vérifier

- Que Clarisse ré-évalue bien le filtre quand l'AOV de profondeur change.
- Le coût réel des tranches sur une scène lourde. Sur le banc d'essai les
  rendus tiennent en quelques secondes, mais le banc est minuscule et la plupart
  des tranches y sont vides — donc sautées.
- Le comportement sur une scène avec de la vraie transparence. Tout le
  raisonnement sur la couverture suppose un rendu opaque.

---

## 7. Où se trouve quoi

```
native/
  build.py              cmagen -> cl -> link, pour un module ou pour tous
  common/aperture.h     geometrie du diaphragme, partagee par le filtre et la camera
  bokeh/                le filtre                 -> bokeh.dll
  bokeh_camera/         la camera                 -> bokeh_camera.dll
  chroma/               l'aberration chromatique  -> chroma.dll
  hello/                ne fait rien, et c'est le but : il prouve la chaine
  tests/                bancs d'essai et scripts de mesure
  build/                les .dll, lues directement par Clarisse

clarisse_add/manifest.py        la source de verite des boutons (categorie
                                "ClarisseAdd Optique", 4 entrees)
clarisse_add/tools/optics.py    ce que font ces boutons
```

Les bancs d'essai de `native/tests/` se rangent en trois familles :

- **le harnais** — `run.py` charge un module dans `cnode` et verifie en
  quelques secondes que la classe est declaree, qu'elle vient bien de notre
  DLL, que l'attribut porte sa valeur par defaut et que le module s'attache ;
- **les constructeurs de scene**, lances par `cnode -script`, qui sauvent un
  `.project` par variante : `make_focus_test`, `make_modes_test`,
  `make_foreground_test`, `make_camera_scene`, `make_shape_test`, et quelques
  sondes plus anciennes ;
- **les mesures**, en Python ordinaire hors de Clarisse : `measure_edge.py`
  lit les EXR rendus et sort la largeur de transition d'une silhouette.

Deux d'entre eux meritent d'etre connus. `make_shape_test` place des pastilles
minuscules tres pres de l'objectif : la tache produite **est** l'image de
l'ouverture, c'est le seul montage ou la forme se lit sans ambiguite. Et
`make_foreground_test` utilise des **emetteurs** et non des surfaces diffuses,
parce que l'eclairage indirect ramenait un contraste voulu de 6:1 a 1,7:1.

Les modules se chargent tout seuls au démarrage de Clarisse : `install.py`
inscrit un lanceur dans `CLARISSE_STARTUP_SCRIPT`, qui appelle `scan_modules`
sur `native/build`. **Reconstruire suffit** — il n'y a rien à recopier, et
`install.py` n'a besoin d'être relancé que si la liste des modules change.
