# Le bokeh — audit des deux nœuds d'optique

Écrit le 2026-09-08, à la demande : « il y a des petits trucs qui buggent, en
valeur élevée ça fait des coutures ».

Les coutures existent, elles sont sur la grille des tuiles de Clarisse, elles
ont **deux causes distinctes**, et les deux sont corrigées et mesurées. Ce
document dit ce que font les deux nœuds, ce qui était faux et pourquoi, et ce
qui reste ouvert.

---

## 1. Ce que font les deux nœuds

**`ImageFilterBokeh`** (`native/bokeh/`) est un filtre d'image dérivé de
`KernelFilter`. Il floute une image finie par convolution avec la PSF d'un vrai
diaphragme. Il lit une profondeur dans un AOV de la même image pour faire
varier le rayon, et sait découper la profondeur en tranches recomposées de
l'arrière vers l'avant.

**`CameraBokeh`** (`native/bokeh_camera/`) est une caméra dérivée de `Camera`.
Elle ne floute rien : elle change la façon dont le moteur tire ses points sur
la lentille, par le crochet `set_lens_sample_callback` de
`RayGeneratorCameraPerspective`. La profondeur de champ est alors calculée par
le lancer de rayons, donc l'occlusion est juste, il n'y a pas d'AOV à fournir,
et le flou se compose avec le flou de bouge et les transparences.

Les deux partagent `common/aperture.h`, la géométrie du diaphragme. C'est
voulu : si les deux ne produisaient pas exactement la même forme, passer de
l'un à l'autre changerait le bokeh. Depuis cet audit, le filtre partage aussi
`common/bokeh_kernel.h`, la construction de la PSF discrétisée — voir §2.

**Ce qu'ils ne se partagent pas, et qu'il faut savoir** : le filtre assombrit
maintenant vraiment les coins sous vignettage optique ; la caméra, non. Le
callback de lentille ne peut pas supprimer un rayon, seul le générateur le
pourrait. La caméra rend donc la FORME de l'amande, pas la perte de lumière.
C'est écrit dans le code, ça reste vrai, et ça vaut d'être su avant de comparer
les deux nœuds sur une même scène.

---

## 2. La sonde, avant tout le reste

Un rendu coûte de l'argent et ne dit pas *de combien* on se trompe. Or tout ce
qui fabrique une couture dans ce filtre se décide dans la construction du noyau
et dans l'application du vignettage, et ni l'une ni l'autre n'a besoin de
Clarisse.

La construction du noyau a donc été **sortie de `bokeh.cpp` vers
`common/bokeh_kernel.h`**, qui ne dépend que de `math.h` et de `vector`.
`tests/bokeh_kernel_probe.cpp` s'y branche et mesure le VRAI noyau — pas une
copie qui dériverait au premier correctif. Sept sections, une seconde
d'exécution, zéro yen :

1. le gain affiché sous vignettage contre la transmission réelle, intégrée
   indépendamment sur une grille fine ;
2. la discontinuité du gain d'un pixel au suivant, sur la grille des tuiles
   contre ailleurs ;
3. la portée réelle du noyau contre la marge que `pre_filter` réserve ;
4. le profil radial selon la douceur ;
5. les cas limites (rayon minimal, lames creusées à fond, anamorphisme extrême,
   NaN) ;
6. la conservation de l'énergie sur un aplat ;
7. la forme de l'ouverture contre sa valeur analytique.

Compilation : `cl /nologo /EHsc /O2 /I..\common bokeh_kernel_probe.cpp`.

Le banc de rendu suit la même idée. `tests/make_seam_source.py` écrit une
source Radiance à la main : un **aplat gris pur** dans le bas de l'image, une
grille de points très lumineux dans le haut, espacés de 301 pixels — premier
avec 64, pour qu'on ne puisse jamais confondre la grille des points et celle
des tuiles. Un noyau normalisé rend un aplat identique à lui-même : **tout ce
qui apparaît sur cet aplat après le filtre est un artefact, sans discussion
possible.** `tests/measure_seams.py` rend les variantes et compare le saut
moyen d'une colonne à la suivante SUR les multiples de 64 et ailleurs. Le
rapport des deux est la mesure de la couture.

Le layer est un `LayerFile` : aucun rayon n'est lancé, le rendu est une
convolution et rien d'autre.

**Un piège d'atelier, découvert en route.** Tant que la session interactive de
Clarisse est ouverte, elle garde les `.dll` de `native/build/` verrouillées et
le linker échoue sur `LNK1104: impossible d'ouvrir le fichier`. Fermer sa
session n'est pas une option. La sortie est de construire ailleurs :
`build_alt.py` réutilise `build.py` avec un autre dossier de sortie, et
`measure_seams.py` lit la variable d'environnement **`BOKEH_BUILD`** pour
pointer `cnode` dessus. La sonde, elle, ne charge rien de Clarisse et n'est
jamais bloquée — une raison de plus de mesurer avant de rendre.

---

## 3. Première cause : le noyau était bâti par tuile

`build_kernel` recevait la position dans le cadre, et le vignettage optique en
dépend. Or `filter` est appelé **par tuile**, et Clarisse n'offre pas d'autre
granularité : le noyau était donc évalué au centre de la tuile, c'est-à-dire
qu'il était une fonction **en escalier** de la position. Une fonction en
escalier saute à chaque marche. C'est la définition d'une couture.

Mesuré, à vignettage 1, rayon 40, image de 1920 de large, tuiles de 64 : **le
centre de gravité du noyau se déplaçait de 4,00 pixels d'une tuile à la
suivante.** Sur une boule de bokeh, qui couvre plusieurs tuiles, cela hache la
boule en blocs — c'est visible à l'œil nu, sans étirement, dans
`coutures_boules.png`.

### Ce que fait la correction

Le vignettage sort du noyau. Le noyau ne dépend plus que des réglages
d'ouverture ; il est identique pour toute l'image. Le vignettage est décrit à
part (`struct Vignette`) et **s'applique par pixel dans la convolution**.

Ça ne coûte rien, et c'est le point qui rend la correction possible : la
troncature du barillet est un **disque**. Son intersection avec un segment
horizontal du noyau se calcule en une racine carrée, et le résultat est encore
un segment — donc la somme préfixée s'applique toujours. Le coût par pixel
reste ce qu'il était : une lecture de somme préfixée par ligne du noyau, plus
deux pixels de bord pour l'antialiasing du bord du barillet.

### Deux erreurs d'énergie trouvées au passage

**Le vignettage n'assombrissait pas.** La référence d'exposition
(`total_unvignetted`) était accumulée **après** le rejet par le disque de
troncature : elle ne comptait donc que les échantillons survivants, et le
rapport valait 1 à un cheveu près.

| frame_r | gain affiché | transmission réelle | erreur |
|---|---|---|---|
| 0,0 | 1,0000 | 1,0000 | 0 % |
| 0,5 | 0,9830 | 0,6850 | **+43,5 %** |
| 1,0 | 0,9760 | 0,3910 | **+149,6 %** |

Autrement dit : le curseur « vignettage optique » donnait la forme en amande et
rien d'autre. Après correction, le gain suit sa propre géométrie à 0,05 % près
sur toute la plage.

**Le disque de troncature comptait deux fois le même bord.** Au centre du
cadre le décalage est nul, la frontière du barillet se confond avec celle de
l'ouverture, et les pixels du pourtour recevaient leur couverture partielle une
fois pour chacune — 0,5 × 0,5 au lieu de 0,5. L'image s'assombrissait de
**0,86 % au centre du cadre** à la seconde où l'on activait le vignettage, là où
il ne doit rien faire du tout. Le disque de troncature est maintenant un demi-
pixel plus grand que l'ouverture, ce qui sépare les deux frontières. Le prix
assumé : la transmission est plus haute de 1,2 % à 2,1 % que celle d'un disque
unité exact — un barillet 1,25 % plus grand, ce qui ne veut rien dire
artistiquement, contre un assombrissement parasite qui, lui, se voit.

---

## 4. Deuxième cause : la marge réservée était plus courte que le noyau

`pre_filter` rend un `kernel_radius` ; Clarisse élargit le proxy de cette marge
de chaque côté de la tuile. Si le noyau est plus large que la marge, les taps
qui débordent sont **silencieusement écartés** par les bornes de la
convolution, la normalisation ne le sait pas, et le résultat s'assombrit — au
bord de chaque tuile, donc sur la grille des tuiles.

Les deux nombres étaient calculés séparément, et par deux formules différentes :

- `pre_filter` réservait `rayon × (1 + 0,18 × |chromatique|)` — la constante
  `CHROMA_SPREAD`, fossile d'une formulation abandonnée ;
- `filter` appliquait `rayon × (1 + chromatique × (décalage − 1))`, où le
  décalage par défaut vaut (0,6 ; 1 ; 1). À dose **négative**, le rouge grandit :
  jusqu'à 1,4 fois le rayon ;
- et la douceur ajoutait par-dessus une jupe allant jusqu'à (1 + douceur) fois
  le rayon, dont personne ne tenait compte nulle part.

Mesuré (portée atteinte / marge réservée, en pixels) :

| réglage | canal | portée | marge | déficit |
|---|---|---|---|---|
| rayon 40, douceur 0,5 | — | 57,98 | 41,50 | **−16,48** |
| rayon 40, chroma −0,5 | R | 48,60 | 45,10 | **−3,50** |
| rayon 40, chroma −1 | R | 56,59 | 48,70 | **−7,89** |
| rayon 80, chroma −1 | R | 112,61 | 95,90 | **−16,71** |
| rayon 40, chroma −1 + douceur 0,5 | R | 80,61 | 48,70 | **−31,91** |

Noter que **chromatique −0,5 est la borne basse du curseur de l'interface**.
Il n'y a rien d'exotique à l'atteindre.

La correction : une seule fonction, `bokeh_kernel_reach(rayon, douceur)`,
appelée à la fois par `pre_filter` — via `widest_reach`, qui prend le maximum
sur les trois canaux — et par la construction du noyau, qui s'en sert pour
borner ses boucles. Après correction, toutes les marges ont au moins 2,5 pixels
de réserve.

### Un troisième effet du même bug : le bokeh devenait carré

Comme les boucles balaient un **carré** de demi-côté `reach`, un noyau plus
large que `reach` n'était pas tronqué en disque mais en carré. À douceur 1 et
rayon 40 : portée obtenue 58 pixels au lieu de 80, avec un poids qui tombait de
25 % à zéro d'un pixel au suivant sur les axes, mais survivait dans les
diagonales. La douceur ne produisait donc pas un bord doux mais un **anneau
dur, aux coins carrés**.

Après correction, le profil descend continûment jusqu'à zéro à 79 pixels, comme
il doit.

---

## 5. Ce que la mesure donne, avant et après

Rendu 1920×1080, tuiles de 64, source = aplat pur + points. Le chiffre est le
saut moyen d'une colonne à la suivante, en pourcentage, mesuré sur la bande
d'**aplat pur** : sur un aplat il doit être nul partout.

| variante | bande | avant : grille / ailleurs | rapport | après : grille / ailleurs | rapport |
|---|---|---|---|---|---|
| optique marquée (rayon 120, 6 lames, vign 0,8, chroma −0,4, douceur 0,3), canal R | aplat | 0,788 % / 0,014 % | **55** | 0,041 % / 0,042 % | **1,0** |
| la même | boules | 0,798 % / 0,024 % | **33** | 0,045 % / 0,046 % | **1,0** |
| vignettage 1 seul | boules | 0,075 % / 0,013 % | **5,9** | 0,063 % / 0,067 % | **0,9** |

Le canal rouge de « optique marquée » creusait à −1,27 % de sa moyenne sur un
aplat parfaitement uniforme. Sous aberration chromatique seule, à rayon 40, le
creux tombait **exactement tous les 64 pixels** : 0,4962 contre 0,4998, sur
toute la largeur de l'image. Après correction : 0,5000 partout, saut 0,000 %.

**Un rapport à ne pas surinterpréter.** La variante « lames creusées » affiche
encore 2,1 sur la bande des BOULES après correction. Ce n'en est pas une : sur
la bande d'**aplat**, la même variante donne 0,000 % partout — donc aucun
artefact d'énergie, et son noyau ne dépend d'aucune position dans le cadre, ce
qui interdit une couture par construction. Le contrôle est dans
`tests/phase_check_seams.py` : il mesure les huit phases modulo 64 au lieu de la
seule phase 0. Sur « optique marquée » les huit sont plates (0,043 % à
0,048 %) ; sur « lames creusées » la phase 0 ressort, parce qu'une étoile à
trois lames a des bords parfaitement droits et que 23 colonnes de mesure
suffisent à ce qu'un de ces bords tombe près d'un multiple de 64. C'est du
contenu, pas de la tuile. **Toujours vérifier une mesure de couture sur
l'aplat avant de conclure.**

### Le contrôle croisé, pour ce que la sonde ne peut pas voir

La sonde somme les taps du noyau un par un. La convolution réelle, elle, passe
par des **segments** intersectés avec le disque de troncature — et sous
anamorphisme ce disque devient une **ellipse** dans l'espace des pixels. C'est
le seul chemin de code que rien d'autre n'exerce, et une erreur y serait
invisible partout ailleurs.

D'où la variante `t6_anamorphique` (rayon 120, vignettage 0,8, anamorphisme
0,5) et `tests/cross_check_seams.py` : la section 8 de la sonde prédit ce que
l'aplat doit valoir colonne par colonne, et le rendu doit tomber dessus. Les
deux implémentations n'ont rien en commun.

| x | rendu | sonde | écart |
|---|---|---|---|
| 200 | 0,25391 | 0,25381 | +0,038 % |
| 700 | 0,33350 | 0,33354 | −0,013 % |
| 1200 | 0,33545 | 0,33560 | −0,045 % |
| 1700 | 0,25757 | 0,25766 | −0,036 % |

Écart maximal 0,045 %. Deux erreurs indépendantes tomberaient rarement sur le
même nombre.

Deux planches dans `J:\_WINDOWSTEMP\claude\seams\` :

- **`coutures_aplat.png`** — le résidu du canal rouge sur l'aplat, avant et
  après, à la même échelle (±0,4 %), avec un trait sur chaque frontière de
  tuile. Avant : une alternance sombre/claire calée sur les traits. Après :
  rien.
- **`coutures_boules.png`** — une boule de bokeh à vignettage 1. Avant : hachée
  en blocs de la taille d'une tuile. Après : une amande lisse, dans un dégradé
  de vignettage continu.

---

## 6. La forme du diaphragme était fausse pour les lames creusées

Trouvé par la section 7 de la sonde, qui compare le rayon au milieu d'une lame
à sa valeur analytique : apothème ± |courbure| × (1 − apothème).

`aperture_edge` choisissait entre les deux intersections du rayon avec l'arc de
la lame selon un critère portant sur la position du centre de l'arc. Ce critère
répond juste **au sommet** de la lame, et faux partout ailleurs — en
particulier au milieu, là où il rendait le gonflement au lieu du creux.

À trois lames et −50 % : rayon **3,50** au milieu de la lame là où la géométrie
donne 0,25. Une ouverture quatorze fois trop grande, très au-delà du disque
circonscrit, donc très au-delà de toute marge réservée.

Le critère juste est le signe de la courbure, et rien d'autre : une lame bombée
gonfle vers l'extérieur (intersection lointaine), une lame creusée rentre vers
le centre (intersection proche).

Deux effets de bord traités en même temps :

- **la tangence.** À quatre lames creusées à fond, le rayon tangente l'arc
  exactement au sommet ; l'arrondi faisait passer le discriminant sous zéro et
  la fonction se rabattait sur l'apothème, mettant le sommet à 0,707 au lieu de
  1. La racine vaut zéro dans ce cas, elle ne vaut pas autre chose.
- **la disparition.** À trois lames, l'apothème vaut 1/2 et le bombement
  maximal vaut 1/2 aussi : à −100 % l'ouverture se pince exactement à zéro et
  disparaît — plus aucun flou, et côté caméra plus aucune profondeur de champ.
  Le bombement creux est maintenant plafonné à neuf dixièmes de l'apothème. Un
  réglage qui éteint la fonction n'est pas un réglage.

**Réserve assumée**, et elle est dans le code : avec la branche proche, le
sommet d'une étoile à **trois lames** est légèrement rogné (0,875 au lieu de 1 à
−50 %). C'est le prix de la description par demi-plans, qui suppose la forme
étoilée vue du centre ; l'arc d'une lame très creusée déborde de son secteur.
L'écart décroît vite et il est **nul dès cinq lames**. Pour une étoile, prendre
cinq lames ou plus.

---

## 7. Le reste de l'audit

### Corrigé

- **`focus_object` sur `CameraBokeh` : déclaré, déverrouillé par le module,
  documenté dans l'interface, et jamais lu.** Le renseigner ne faisait rien du
  tout, en silence — la pire des pannes, puisque l'utilisateur croit avoir fait
  le point. Il est maintenant lu, avec la même convention que le filtre
  d'image : la **projection** sur l'axe de visée, pas la distance euclidienne,
  parce qu'une lentille mince fait le point sur un plan parallèle au capteur.
  `get_config` rend la même valeur, pour que l'affichage du viewport ne
  contredise pas le rendu.
- **`corrective_slices = 2` ne séparait pas l'avant de l'arrière.** Le budget
  hors zone nette valait 1, la borne haute le ramenait à zéro pour l'avant, et
  l'avant se retrouvait sans aucune tranche — alors que la documentation du
  réglage promet exactement l'inverse.
- **L'alpha était assombri par le vignettage** dans le chemin à tranches et
  dans la passe unique. Le vignettage assombrit la lumière, il ne perce pas la
  matière : une matte qui devient transparente dans les coins du cadre est
  fausse, et elle ne s'accordait même pas avec la couverture utilisée pour
  recomposer les tranches, laquelle restait pleine. L'alpha suit maintenant
  toujours la couverture.
- **La largeur en pixels qui convertit le cercle de confusion millimétrique**
  était prise sur l'instantané de profondeur dans `pre_filter` et sur l'image
  entière dans `filter`. Une région de rendu limitée suffit à les séparer, et
  les fractions calculées par tuile ne se seraient plus rapportées au maximum
  mesuré une fois pour toutes. Elle est maintenant mémorisée dans le module.
- **`PRIMES[dim % 22]` sur une table de 24 entrées** dans la caméra : deux
  bases mortes. Sans conséquence visible, mais c'est le genre de constante qui
  finit par tomber à côté.

### Vérifié et sain

Passé à la sonde et au raisonnement, sans rien trouver :

- **conservation de l'énergie** : sur dix combinaisons de rayon, lames,
  courbure, aberration sphérique et douceur, un aplat ressort à sa propre
  valeur à 0,000 % près. La requantification de l'intérieur en niveaux est
  compensée exactement.
- **divisions par zéro** : `f_stop` est borné à 0,01 par le CID et reborné à
  1e-6 dans le code ; `world_scale_multiplier` est borné à 1e-4 ;
  `focus_distance` et `focus_range` sont bornés à 0, donc la portée de repli
  `focus × 0,25 + 1` ne peut pas devenir négative et `pow` ne peut pas rendre
  de NaN ; l'anamorphisme ne produit que des facteurs ≥ 1, jamais 0 ; le rayon
  sous 0,5 sort par un chemin dédié.
- **valeurs extrêmes** : rayon 0,5, rayon 0,6 à trois lames, 32 lames creusées
  à fond, aberration sphérique ±1, anamorphisme ±1, vignettage 1 dans le coin,
  et toutes leurs combinaisons — aucun NaN, aucun total nul, aucune portée
  aberrante.
- **le cas « aucun échantillon plein »** (très petit rayon, forme très creusée)
  ne fait pas fuir d'énergie : le total accumulé ne compte alors que des
  échantillons de bord, qui sont exactement ceux qui seront sommés.
- **le retournement du noyau** (convolution et non corrélation) est bien en
  place dans les deux chemins ; sans lui un polygone impair sortirait tourné de
  180 degrés.
- **`build_slices`** : la zone nette a bien sa tranche de rayon exactement nul,
  et le découpage est calculé sur l'étendue réelle de la CoC signée dans
  l'image entière, dans `pre_filter` — donc identique pour toutes les tuiles.
  C'était déjà juste, et c'est ce qui empêchait une troisième famille de
  coutures.

---

## 8. Ce qui reste ouvert

### Le trou d'alpha derrière un premier plan flou — c'est de l'occlusion

C'est le point le plus important de cette section, parce que **la description
qu'en donnait `passation.md` était incomplète et m'a fait perdre du temps** :
« le composite en tranches perd de l'alpha », avec pour correctif proposé « ne
jamais laisser la couverture descendre sous ce que donnerait un simple flou de
l'alpha ». Deux corrections plausibles ont été écrites, mesurées, et retirées.

Mesures sur `tests/make_foreground_test.py` (un mur net à 80 unités, une boîte
sombre floue à 19, mise au point sur le mur, rayon 30, dix tranches) :

- l'alpha descend à **0,2576** au plus creux, sur une scène entièrement opaque ;
- comme Clarisse **dé-prémultiplie les AOV par l'alpha de sortie**, l'AOV de
  profondeur y ressort à **251,10** pour une géométrie à 79,90 ;
- la carte de CoC signée en travers de la silhouette est une **marche franche** :
  0,00 jusqu'à x = 318, −1,00 à partir de x = 319, sans aucune valeur
  intermédiaire ;
- l'alpha ne fait pas un trou d'un pixel : il vaut 0,276 à x = 319 puis remonte
  lentement (0,30 ; 0,33 ; … 0,57 à x = 335). C'est **une bande large comme le
  rayon de flou, tout autour de la silhouette**, et elle est pire dans les
  coins.

Ce qui a été essayé et ne sert à rien, mesuré :

- un **médian 3×3 sur la CoC**, pour rattacher les pixels de silhouette à l'une
  des deux surfaces : il n'y a rien à rattacher, la carte est déjà une marche.
  Gain : 0,2576 → 0,2759.
- un **plancher de couverture à la somme des couvertures de tranches** : au
  point le plus creux, une seule tranche contribue, donc la somme vaut déjà la
  couverture obtenue. Gain : nul.

La vraie cause est banale et n'a rien à voir avec le découpage : **le mur
derrière la boîte n'a jamais été rendu.** La tranche du fond porte un trou de
la forme exacte de la silhouette, parce que ces pixels-là appartiennent à la
boîte. Quand la boîte se dissout en floutant, plus personne ne couvre ce trou.
C'est la limite de fond du flou 2,5D, pas un défaut d'implémentation.

Le correctif juste est connu et il est plus lourd que ce qui a été tenté :
**prolonger chaque tranche sous tout ce qui est devant elle** avant de la
flouter. Concrètement, pour chaque tranche :

- la **couverture** se calcule sur « cette tranche OU toute tranche plus
  proche », pas sur cette tranche seule ; la tranche la plus lointaine couvre
  alors tout le cadre ;
- la **couleur** se calcule en convolution normalisée — `flou(couleur × masque)
  / flou(masque)` — ce qui la prolonge naturellement dans le trou au lieu d'y
  laisser du noir.

Corriger la couverture sans la couleur ne marche pas : on obtient un alpha juste
et une couleur trois fois trop sombre. Les deux vont ensemble ou pas du tout.

**Ne pas re-tenter le médian ni le plancher de couverture** : ils sont mesurés,
ils ne servent à rien, et le commentaire correspondant est dans
`filter_sliced`.

### Fonctions absentes

- **Image d'ouverture personnalisée** (`Kernel Type = Input` chez Peregrine) :
  un noyau lu dans une texture au lieu d'être construit. Le chemin est déjà
  prêt côté filtre — `Kernel` porte des segments et des taps, il suffirait de
  les extraire d'une image — mais côté caméra il faudrait une table de
  répartition 2D au lieu des deux tables 1D actuelles.
- **Canaux de matte par effet.**
- **Le vignettage d'exposition côté caméra**, qui demande de pouvoir supprimer
  un rayon, donc un générateur et pas seulement un callback.

### Performance, pas encore faite

Trois gaspillages repérés pendant l'audit, aucun corrigé — ils ne changent pas
l'image :

- **le noyau est rebâti à chaque tuile.** Depuis que le vignettage en est
  sorti, il ne dépend plus que des réglages : il pourrait être bâti une fois
  par évaluation dans `pre_filter` et rangé dans le module, comme l'instantané
  de profondeur. Dans le chemin à tranches c'est un noyau par tranche et par
  canal, à chaque tuile.
- **la somme préfixée est rebâtie par tranche et par canal**, soit quatre fois
  le nombre de tranches par tuile.
- **le mode « forme du noyau »** (`output_type = 3`) rebâtit un noyau de rayon
  jusqu'à 200 pixels à chaque tuile, avec la liste complète des taps.

### Divergence assumée entre les deux nœuds

Le filtre assombrit les coins sous vignettage optique ; la caméra rend la forme
sans l'assombrissement. Les deux sont défendables, mais ils ne rendent pas la
même image. Si un jour on veut les raccorder, c'est côté caméra qu'il faut
travailler, et il faudra un générateur de rayons complet.

---

## 9. Les fichiers

| fichier | rôle |
|---|---|
| `native/common/aperture.h` | géométrie du diaphragme, partagée par les deux nœuds |
| `native/common/bokeh_kernel.h` | **nouveau** — construction de la PSF et description du vignettage, sans dépendance à Clarisse |
| `native/bokeh/bokeh.cpp` | le filtre : réglages, profondeur, tranches, convolution |
| `native/bokeh_camera/bokeh_camera.cpp` | la caméra : tables de répartition, callback de lentille |
| `native/tests/bokeh_kernel_probe.cpp` | **nouveau** — la sonde, sept mesures, une seconde |
| `native/tests/make_seam_source.py` | **nouveau** — la source Radiance du banc des coutures |
| `native/tests/make_seam_scene.py` | **nouveau** — le projet, six variantes |
| `native/tests/measure_seams.py` | **nouveau** — rend et mesure le saut sur la grille contre ailleurs |
| `native/tests/view_seams.py` | **nouveau** — les deux planches de comparaison |
| `native/tests/phase_check_seams.py` | **nouveau** — le contrôle des huit phases modulo 64 |
| `native/tests/cross_check_seams.py` | **nouveau** — le contrôle croisé sonde / rendu sous anamorphisme |
| `native/tests/make_foreground_test.py` | le banc premier plan, préexistant |
| `native/tests/measure_edge.py` | la mesure de débord, préexistante |

Le banc premier plan sert aussi de test de non-régression du chemin à
tranches : après tous les changements de cet audit, le débord vaut 9,73 et 9,85
pixels à dix tranches contre 1,29 et 0,31 à une seule — identique à avant.
