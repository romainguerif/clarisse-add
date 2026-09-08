# Le tissu dans Clarisse

Ce document est l'état du chantier « panneaux cloth » : un solveur de tissu
écrit pour Clarisse, et le premier node qui s'en sert, le capitonnage.

Il est écrit pour être relu froid. Ce qui compte n'est pas la liste des
fonctions — le code la donne — mais les six choses qui ont été fausses avant
d'être justes, parce qu'elles se retrouveront à l'identique dans les prochains
nodes de simulation.

---

## 1. Ce qui existe

`native/common/cloth_solver.h` — un solveur XPBD réutilisable. Quatre types de
contraintes : distance, flexion isométrique, ancrage élastique, volume fermé.
Aucune dépendance à Clarisse au-delà de `CoreVector` et `GMathVec3d`, donc il
sert à n'importe quel node.

`native/quilt/` — `GeometryQuilt`. Chaque polygone du maillage d'entrée devient
un coussin capitonné. La topologie d'entrée dessine le motif : des quads
réguliers donnent un matelas, une sphère facettée donne une boule à coussins.
Filtre par shading group pour ne capitonner qu'une partie du maillage.

`native/tests/quilt_probe.py` — une sonde qui lit le champ de plis dans le
maillage, **sans rendu**. Voir §3, c'est l'outil qui a débloqué le chantier.

---

## 2. Le solveur : trois choix qui se défendent

**XPBD, pas PBD.** Un flottant de plus par contrainte — le multiplicateur de
Lagrange accumulé. La différence est décisive pour un outil d'artiste : en PBD
la raideur effective dépend du nombre d'itérations, donc bouger le curseur de
qualité change la forme. En XPBD le point fixe de l'itération est l'équilibre
statique exact, et la qualité ne fait plus que converger vers lui.

**Le pas de temps est fixé à un et n'est jamais exposé.** On ne simule pas une
trajectoire, on cherche un équilibre : à l'état stationnaire, `dt` disparaît de
l'équation. Ce qui fait la matière, ce sont les rapports entre compliances.

**La flexion est celle de Bergou 2006, dite isométrique.** Quatre scalaires
précalculés sur la pose de repos, gradient linéaire, aucune fonction
transcendante. La formulation en angle dièdre de Müller est un zéro sur zéro
exactement quand le tissu est plat — c'est-à-dire dans notre état de départ,
pas dans un cas rare.

La gravité est appliquée une fois par appel à `solve()`, comme un déplacement de
prédiction. Ce n'est pas un raccourci : en XPBD, partir de `x + g` et projeter
en accumulant les λ converge bien vers `x − x_pred = M⁻¹ Σ ∇Cᵀλ`, c'est-à-dire
l'équilibre chargé. Et comme chaque niveau de la cascade remet les λ à zéro, la
succession des niveaux est l'itération de point fixe `x ← proj(x + g)`, qui
converge elle aussi.

---

## 3. La sonde, ou comment arrêter de payer pour voir

Pendant une demi-journée, chaque essai coûtait un rendu. C'est lent, c'est cher,
et surtout ça ne dit pas *pourquoi* : on voit que c'est raté sans voir de
combien.

Or tout ce qu'on cherchait à régler — combien de plis, où, de quelle amplitude —
est déjà dans le maillage. `quilt_probe.py` construit un coussin unique sur un
quad de deux unités et affiche **le laplacien de la hauteur**, pas la hauteur :
un dôme lisse a un laplacien d'un seul signe, tandis que chaque pli y apparaît
comme une alternance creux/bosse. Compter les changements de signe le long d'une
ligne donne directement le nombre de plis, sans avoir à débattre de ce qui est
« lisse ». Trois mesures suffisent :

- **relief** — l'écart maximal au patron analytique, c'est-à-dire l'amplitude
  des plis ;
- **plis** — les changements de signe du laplacien, au milieu, en diagonale, près
  du bord ;
- **débordement** — de combien le tissu sort du contour du polygone. Deux
  coussins voisins partagent ce contour : tout ce qui dépasse entre dans le
  voisin.

Une passe de sonde balaye six réglages en trois secondes et ne coûte rien. La
première chose à faire au prochain chantier de simulation est d'écrire sa sonde.

C'est elle qui a tranché la question qui comptait : **le relief est-il du vrai
flambage ou seulement le bruit d'amorçage qu'on a semé ?** On divise le bruit par
six ; si le relief ne bouge pas, c'est du flambage. Il n'a pas bougé.

---

## 4. Les six erreurs

Chacune donnait une image plausible et fausse. Elles sont rangées de la plus
profonde à la plus locale.

### 4.1 Une enveloppe sous pression n'est pas un coussin

C'était l'erreur de fond. Une membrane close remplie de gaz, à qui on donne du
tissu en trop, ne se plisse pas : elle se déforme en **une seule grande bosse**.
Le volume est conservé, mais rien n'interdit à la forme de changer, et un grand
lobe coûte moins cher en flexion que dix petits plis.

Un vrai rembourrage résiste au changement de *forme*, pas seulement de *volume*.
D'où les ancres (`cloth::Anchor`) : chaque point du tissu est rappelé vers le
patron par un ressort mou. C'est le `K` de la loi de flambage de
Cerda–Mahadevan, `λ ≈ 2π (B/K)^¼` : sans `K`, la longueur d'onde part à l'infini,
autrement dit un seul grand pli. Le réglage s'appelle `stuffing`, et à zéro on
retrouve exactement le ballon.

Avant les ancres, `wrinkle_scale` n'avait aucun effet mesurable. Après, il pilote
le nombre de plis comme prévu.

### 4.2 Le patron doit être le coussin, pas le carré à plat

Première version : longueurs au repos prises sur le carré plat, multipliées par
un « mou » global. Le mou devait alors fournir **à la fois** le bombement et les
plis, ce qui en demandait beaucoup — et beaucoup de mou ne fait pas beaucoup de
petits plis, il en fait un seul, très grand.

Pire, le mou nécessaire au seul bombement est calculable : gonfler à la hauteur
`h` sur un rayon `r` consomme déjà `(2/3)(h/r)²` de longueur d'arc. En dessous de
ça, le volume visé et des arêtes inextensibles se contredisent, le solveur
n'a pas de solution et il **broie** : c'est le froissement chaotique à trous
noirs des premiers rendus.

Maintenant les longueurs au repos sont mesurées sur le coussin visé, dessiné
analytiquement. Le patron est déjà de la bonne taille pour la forme demandée,
`wrinkles` n'ajoute que du surplus pur, et il n'y a plus rien à calculer ni à
compenser. Le même patron sert aussi de volume de référence — mesuré, pas estimé
— et de position de départ.

### 4.3 Les compliances doivent être normalisées par la taille de maille

L'énergie isométrique de Bergou est en `3/A` : la compliance de flexion doit
suivre l'aire de la maille, sinon doubler la résolution change la matière. La
compliance d'un ressort suit sa longueur, pour la même raison.

Sans ça, la cascade multigrille était absurde : chaque niveau simulait un tissu
différent du précédent. Et l'échelle du réglage était fausse d'un facteur mille,
ce qui donnait une flexion pratiquement rigide alors que l'interface affichait
« souple ».

Le réglage exposé n'est d'ailleurs plus une compliance mais une **largeur de
pli** — `wrinkle_scale`, en fraction du coussin. La conversion passe par la
puissance quatre de Cerda. C'est l'unité qui se voit.

### 4.4 Une cascade multigrille ne trouve que ce qu'elle sait représenter

Le niveau grossier d'une cascade ne sait faire qu'un seul grand pli. Si les
niveaux fins partent de lui sans rien pour les en tirer, ils se contentent de le
lisser : un unique pli par coussin, identique partout.

Le bruit d'amorçage doit donc être réinjecté **à chaque niveau**, et à la
fréquence des plis voulus — on sème directement le mode qu'on veut voir flamber.
Il reste une graine, pas un résultat : diviser son amplitude par six ne change
pas le relief obtenu (§3).

### 4.5 Un patron à tangente verticale n'est pas suivable

Le profil du coussin était `(sin πu · sin πv)^k` avec `k < 1`. Ça donne bien un
dessus aplati et des flancs redressés, mais la **pente est infinie au bord**. Le
tissu ne peut pas monter droit hors de sa couture : il se rabattait en un clapet
dressé au milieu de chaque arête — un champignon noir, parfaitement visible au
rendu, et strictement identique sur tous les coussins.

Le remplaçant est `1 − (1 − t)^k`, même allure, pente `k` au bord.

Au passage, le profil ne se lit plus sur un produit de deux profils mais sur un
**minimum lissé** des deux distances aux bords. Le produit affaisse les coins
deux fois et donne un coussin rond perdu au milieu de sa case ; le minimum lissé
donne un carré aux angles adoucis, qui remplit la sienne. Son exposant est le
réglage `squareness`, et c'est lui qui sépare un matelas pneumatique d'un
capitonnage.

### 4.6 On ne tend pas un tissu dont le bord est épinglé

L'idée était de raccourcir les longueurs au repos dans la bande de couture pour
y tendre le tissu et nettoyer la bordure. C'était une erreur : cette bande a un
bord épinglé, donc le tissu ne peut pas y céder en glissant, il ne peut que
tirer l'épaulement vers le bas. Et il tire d'autant plus fort que l'épaulement
est raide, c'est-à-dire au milieu de chaque arête — d'où une encoche noire,
exactement là, sur tous les coussins.

Le vrai capitonnage ne fait pas ça : il laisse un **mplat** autour du coussin, et
le bombement ne commence qu'après. C'est une forme, pas une contrainte, donc
elle est toujours réalisable. Le réglage `seam` donne sa largeur.

Règle générale, valable pour tout le solveur : **une contrainte métrique
impossible ne produit pas « un peu de tension », elle produit un artefact.** Ce
qu'on veut obtenir géométriquement, il faut le mettre dans la forme de repos.

---

## 5. Deux corrections de justesse

**L'enfoncement du sillon se mesure au sommet, pas à la face.** Deux faces
voisines de tailles différentes enfonçaient leur arête commune à deux
profondeurs différentes, et le capitonnage se fendait tout du long. Ça arrive dès
le premier maillage un peu irrégulier — une sphère, par exemple. La profondeur
vient maintenant d'une taille locale attachée au sommet, que les deux faces
lisent à l'identique. Même raisonnement pour la direction : normale au sommet, pas
normale de face.

**La compliance de la pression est relative, pas absolue.** Le gradient du volume
est une somme d'aires : sa norme dépend de la taille du coussin et du pas de la
grille. Multipliée par le dénominateur de la contrainte, elle devient un pur
facteur de relâchement, comparable d'un maillage à l'autre. Et c'est le réglage
qui décide du sort du surplus de tissu : une pression qui cède laisse le coussin
gonfler un peu plus et le surplus disparaît sans faire de pli ; une pression
ferme l'oblige à se plisser. C'est la première chose à regarder quand un coussin
sort trop propre.

---

## 6. L'échelle du monde

Tout se résout dans un repère normalisé, centré sur le coussin et divisé par son
rayon. Les mêmes réglages tiennent donc sur un matelas de six mètres et sur une
facette de sphère centimétrique — c'est le comportement par défaut, et il est
volontairement *non* réaliste : les plis restent proportionnels à l'objet.

Deux réglages rendent la simulation sensible à la taille réelle, pour les scènes
où ça compte :

- `wrinkle_size`, en unités du monde, donne aux plis une taille absolue. Un grand
  coussin en fait alors beaucoup et des fins, un petit deux ou trois. C'est le
  comportement d'un vrai tissu, dont la raideur ne sait rien de la taille de ce
  qu'on en coud.
- `detail`, en unités du monde, donne à chaque coussin sa propre résolution selon
  sa taille réelle, plafonnée par `max_resolution`. Sans ce plafond, une seule
  grande face fait exploser le nombre de sommets.

---

## 7. Les réglages, par ordre d'importance

Pour obtenir une forme :

| réglage | ce qu'il fait |
|---|---|
| `puff_relative` | hauteur du coussin, en fraction de son rayon |
| `squareness` | rond perdu dans sa case, ou carré qui la remplit |
| `shoulder` | dessus bombé, ou dessus plat à flancs raides |
| `seam` / `seam_depth` | largeur et profondeur du mplat de couture |

Pour obtenir des plis :

| réglage | ce qu'il fait |
|---|---|
| `wrinkles` | surplus de matière — sans lui, aucun pli |
| `wrinkle_scale` | largeur des plis, en fraction du coussin |
| `stuffing` | fermeté du rembourrage — à zéro, un ballon, donc un seul pli |
| `pressure_softness` | à monter si le coussin sort trop propre |
| `wrinkle_reach` | jusqu'où les plis remontent depuis la couture |
| `corner_gather` | le fronçage des coins, d'où partent les plis en étoile |

Qualité :

`resolution` borne la finesse (un pli ne peut pas être plus étroit que quatre
mailles), `levels` fait converger, `iterations` affine. Aucun des trois ne change
la matière — c'est tout l'intérêt d'avoir normalisé les compliances.

---

## 8. Ce qui reste

- Un `ToolQuiltPaint` pour construire une sélection de faces dans Clarisse, au
  lieu de dépendre d'un shading group venu de l'amont.
- La collision entre coussins voisins. Elle ne se voit pas encore parce que le
  contour est épinglé et que rien ne déborde (la sonde le mesure : débordement
  nul), mais un `wrinkles` très haut finira par la demander.
- Les autres nodes que le solveur rend possibles : un drapé sur un objet, une
  nappe, un rideau. Le solveur est déjà générique ; ce qui manque à chaque fois,
  c'est le patron et les points épinglés.
