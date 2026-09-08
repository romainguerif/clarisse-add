# Les toiles d'araignée dans Clarisse

Écrit le 2026-09-08. Ce n'est pas un exposé de biologie : c'est un cahier de construction,
et chaque fait est là parce qu'il peut devenir un paramètre.

Trois parties : ce que les vraies toiles sont, avec les nombres ; ce que les références de
Romain sont vraiment — et ce ne sont pas des toiles ; ce que Clarisse sait déjà faire,
mesuré plutôt que supposé.

---

## 1. L'architecture réelle des toiles

### 1.1 L'orbitèle : une séquence, et des nombres qui ne sont pas ronds

Fil de pont lâché à la dérive, cadre, rayons, moyeu, **spirale auxiliaire sèche**, puis
**spirale de capture collante posée du bord vers le centre en mangeant l'auxiliaire au fur
et à mesure**. Une heure pour la plupart des aranéides — **45 ± 5 min** chez *Larinia
chloris*, jusqu'à 4 h chez *Nephila*. Deux nuances utiles : **seuls 5 à 8 rayons sont
primaires**, tous les autres sont insérés entre deux existants ; et l'auxiliaire n'est pas
toujours mangée — *Nephila clavipes* la **laisse** dans la toile finie, *Trichonephila
clavata* en fait un échafaudage permanent qui sert de guide de réparation.

| grandeur | valeur | source |
|---|---|---|
| rayons | ***A. diadematus* 32,1 ± 0,5** (n = 96) ; *Larinia* 15,6-19,6 ; *Caerostris* 17-25 ; *Hyptiotes* 4 | [Vollrath 2002](https://european-arachnology.org/esa/wp-content/uploads/2015/08/107-116_Vollrath.pdf) |
| jitter angulaire | **+0,7 à +1,5°** ; replacement d'une attache à **1,9 ± 1,5 mm** | idem |
| moyeu / zone libre / capture | **0,3-0,5 % / ~5 % / ~94 %** de la surface | [Leucauge](https://kerwa.ucr.ac.cr/bitstreams/24655a90-401c-4671-9f6e-d5a44b171c8c/download) |
| asymétrie verticale | rayon inférieur **2× à 4×** le supérieur | [Zschokke 2021](https://pmc.ncbi.nlm.nih.gov/articles/PMC7716925/) |
| inclinaison | **20° ± 11°** à **40° ± 17°** hors verticale — **jamais 0** | idem *Leucauge* |
| pas de spirale | **0,6 à 3,6 mm**, moyenne 1,55 mm, **CV 0,35-0,50** | [Larinia](https://pmc.ncbi.nlm.nih.gov/articles/PMC10254737/) |
| gradient du pas | **haut : 1,45 → 0,92** (fort) · **bas : 1,34 → 1,11** (quasi nul) | [Wixia](https://www.americanarachnology.org/journal-joa/joa-all-articles/article/download/arac-45-2-160.pdf) |

Trois règles de forme valent tous ces nombres.

**Ce n'est pas une spirale, c'est un polygone** : chaque tour est une suite de segments
droits d'un rayon au suivant — ~960 pour une *diadematus* à 32 rayons et 30 tours — et les
ruptures de pente tombent **exactement sur les rayons**.

**Le pas n'est pas monotone** : la spirale de capture suit une même boucle d'auxiliaire
plusieurs tours puis saute à la suivante, d'où des **paquets de boucles serrées séparés
par des sauts**, plus marqués du côté où l'araignée monte.

**Les rayons ne sont pas également espacés, et c'est mécanique.** L'araignée court plus
vite vers le bas, donc attend tête en bas, donc le moyeu remonte. Les angles sont **plus
grands au-dessus du moyeu** (F = 187,28), les rayons du bas sont posés **en dernier**, et
chez *Nephila* la majorité sont **subsidiaires** : ils ne partent pas du moyeu mais
s'accrochent sur un autre rayon. La toile tolère d'ailleurs des tensions très inégales —
cadre cisaillé de 30°, certains rayons s'étirent de 20 % pendant que d'autres pendent
mous, et l'araignée ne corrige rien dans 6 cas sur 15.

### 1.2 Les trois autres familles

**Cobweb (Theridiidae) — la plus proche des références.** Quatre régions : retraite,
nappe, **fils gumfoot**, réseau 3D de soutien. Le scan laser d'une toile entière donne la
topologie utile ([PNAS 2021](https://pmc.ncbi.nlm.nih.gov/articles/PMC8379916/)) : **le
degré d'un nœud vaut 3 dans la grande majorité des cas** — un Y, presque jamais un
carrefour —, la **longueur médiane de segment passe de 2 mm au jour 0 à 4 mm au jour 3**,
et la densification est **inhomogène**, concentrée là où des proies ont été prises. Les
gumfoot : **10 à 32 par toile**, **~50 mm**, **encollés seulement sur les 5 à 15 mm du
bas**, et ce sont des **faisceaux de quatre fibres en deux paires** (3,5 et 2,5 µm). Leur
ancrage au sol est *dendritique*, donc faible : le fil **se décolle au lieu de casser**,
et l'énergie qui soulève la proie est stockée dans l'échafaudage du haut. Le plus gros
levier esthétique : **une araignée affamée fait des gumfoot (>75 % des toiles), une repue
les supprime et remplit l'espace d'un maillage 3D chaotique aux fils 38 % plus épais** —
deux états de la même espèce, pas deux espèces.

**Nappe (Linyphiidae).** **42 à 75 cm²** (8-9 cm de côté), jusqu'à 682 cm² chez
*Linyphia*, toujours **horizontale avec un espace ouvert dessous**, le tangle d'assommage
(~20 cm) **attaché à sa face supérieure** et la tirant vers le haut. **La soie n'y est pas
collante** — l'aggregate n'est qu'un ciment — et c'est un **maillage désordonné de fibres
d'épaisseurs différentes**, pas une grille. **Entonnoir (Agelenidae) :** nappe **20 à
60 cm**, concave, non collante, avec **plusieurs entonnoirs — le cas normal** ; la texture
feutrée n'est pas du cribellé mais l'accumulation de fils épais **déposés en balayages
superposés**.

### 1.3 Les fils, et le seul calcul qui décide de la forme

| fil | diamètre | pré-tension | matériau |
|---|---|---|---|
| capture (flagelliforme) | **2,4-3,5 µm** | **9,7 µN** | 95 MPa, **465 % d'allongement**, module ~1 MPa |
| rayon (ampullacée maj.) | **3,93 µm** | **102,5 µN** | 1217 MPa, 23 % d'allongement, 8 GPa |
| cadre | **7,23 µm** | **962 µN** | idem |
| ancrage | **8,03 µm** | **1741 µN** | idem |

([Yavuz 2024](https://pmc.ncbi.nlm.nih.gov/articles/PMC11444776/), [Blackledge & Hayashi
2006](https://journals.biologists.com/jeb/article/209/13/2452/16128/).) **Le rayon
travaille à 0,7 % de sa charge de rupture et la spirale à 2,3 %** : une toile au repos est
quasi déchargée, toute la marge sert à l'impact.

Les **gouttes de glu** font **30-40 × 20-30 µm**, à **3,5 à 13 par mm** (espacement 80 à
300 µm), et sont à **99,8 % d'eau** — leur volume **double** entre 10 et 90 % d'humidité.
Leur régularité vient de l'**instabilité de Plateau-Rayleigh** : le film cylindrique de
colle se brise en perles régulièrement espacées, exactement comme un filet d'eau.

**Et voici ce qui décide de la forme d'un fil, contre l'intuition.** Un fil de capture de
2,4 µm tendu à 9,7 µN sur 10 cm s'affaisse sous son propre poids de **7 µm — 0,007 % de la
portée**. Il est **rigoureusement droit à l'écran**. Mais une goutte de rosée de 1 mm pèse
5,14 µN, soit **53 % de toute la pré-tension**, et impose un coude de **15,4°** ; une
goutte de 0,5 mm, un coude de 1,9°. **Un fil sec est un segment droit ; un fil chargé de
rosée est un polygone funiculaire qui se brise à chaque goutte — jamais une caténaire
lisse.** C'est l'inverse exact du câble de `curves-clarisse.md`, où le poids propre fait
tout.

### 1.4 Ce qui rend une toile crédible : le désordre est la norme

Le comptage exhaustif de **127 toiles** de *Zygiella x-notata* donne le catalogue
([Pasquet 2013](https://www.eje.cz/pdfs/eje/2013/03/14.pdf)) : **35 anomalies par toile en
moyenne**, étendue **5 à 98**, sur 486 ± 28 unités de spirale. **100 % des toiles ont des
trous** (5,6 en moyenne) et des *stop-and-return* (10,0) ; **84 %** ont des rayons déviés
de plus de 5° (2,6) ; **98 %** ont des fils de spirale collés deux à deux (5,8) ; **45 %**
ont des rayons en Y. Et le désordre n'est pas uniforme : **+60 % d'anomalies dans la
moitié haute**, et plus en périphérie qu'au centre.

Trois faits physiques donnent gratuitement l'aspect « vieille toile ». **Au-dessus de 70 %
d'humidité un bout de fil rompu supercontracte** : il raccourcit **jusqu'à 50 %**,
**double de diamètre** et se **vrille jusqu'à 300°/mm** — un fil qui pend ne peut être ni
droit ni lisse. **Le fil de capture relâché s'enroule dans ses propres gouttes**
(*windlass*), mais seulement en dessous de ~50 % de sa longueur : sur 276 rangées à
tension native, moins de 2 % en montrent — marqueur d'état gratuit. Et **le vieillissement
n'est pas un jaunissement** : aucune mesure colorimétrique n'en documente, et la colle
protégée reste collante après 8 à 10 mois. Ce qui tue l'adhésion, c'est la
**déshydratation irréversible** et la **contamination** — sur 60 jours les particules font
**1 à 20 % du poids de la toile**, et **les grains de plus de 10 µm dominent en surface**,
individuellement visibles. Le bon marqueur est le passage de « gouttes perlées » à « fil
grumeleux et mat ».

Deux repères pour finir. Le **stabilimentum** d'*Argiope*, si spectaculaire en photo, ne
couvre que **0,26 % de la surface de l'orbe** et n'existe que dans ~60 % des toiles. Et
l'ouverture du lobe diffusé par un fil vaut environ **λ/d** : un fil de 1 µm étale la
lumière sur **~32°** quand un ancrage de 8 µm la concentre sur **~4°**. **Plus le fil est
fin, plus il brille large** — d'où le halo de la spirale contre le trait net de l'ancrage.
Sauf en macro 1:1, **un fil est 5 à 40 fois plus fin qu'un pixel** : le rendre comme un
tube à shader de surface est exclu, il faut un modèle de fibre sous-pixel — celui de
`MaterialPhysicalHair`.

*Une recette VFX complète existe déjà — [Simeonova & Anderson, SIGGRAPH
2024](https://eprints.bournemouth.ac.uk/40198/1/SIGGRAPH24_preprint.pdf) : Vellum en
Houdini, rayons raides et spirale souple, shader thin-film à épaisseur bruitée entre 250
et 750 nm, motion blur extrait des seuls rayons.*

---

## 2. Les références de Romain ne sont pas des toiles

### 2.1 Ce que la mesure dit, et où elle corrige l'œil

*Au passage : `J:\_WINDOWSTEMP\claude\refs\` ne contient **aucune image de toile** — dix
fichiers au même horodatage, résidus d'autres sessions. Les vraies références sont sur le
Bureau, `toile-araignee-halloween-deco.jpg` (1500 × 1000) et
`halloweendeko-spinnennetz-…_005b.webp` (210 × 210), les deux fichiers les plus récents du
disque. La seconde, trop petite pour l'analyse, dit l'essentiel : on y voit le bord de la
masse, dense et blanc, qui se délite en fibres séparées.*

Mesuré sur l'image macro (`mesure_toile.py`, `mesure_maille.py`, `mesure_orient.py`, dans
`J:\_WINDOWSTEMP\claude\`) :

| grandeur | mesure | ce qu'on en fait |
|---|---|---|
| largeur des brins | p10 = 3 px, **p50 = 5 px**, p90 = 20 px, p99 = 77 px | rapport **15:1** entre le câble et la fibre |
| densité | **75 à 93 brins par 1000 px** de balayage, écart médian 6-8 px | ~110-140 brins par ligne d'image |
| grain (autocorrélation 1/e) | 2-5 px dans le voile, **20 px** au croisement | sépare les échelles |
| maille du réseau | **40 à 88 px**, ~64 px médian | **25 à 35 mailles** en travers du champ |
| luminance | p50 = 60, p75 = 98, p95 = 166, **0,65 %** > 240 | trois quarts de l'image sous 40 % |
| orientation, champ entier | **isotropie 0,988**, pic à 135° valant 1,7× le plat | — |
| orientation, blocs de 100 px | pic local médian **1,51×** le plat, p90 = 2,56× | — |

**Trois corrections à la description de départ.**

**Il y a trois échelles, pas deux.** Entre le câble et la fibre isolée vit une **mèche** —
quelques dizaines de fibres groupées sur quelques centaines de pixels puis divergentes. Et
le « câble » n'est pas un cylindre : c'est un **ruban plat de fibres parallèles serrées**,
dont on distingue les fibres à l'intérieur ; sa brillance est leur somme, pas un reflet
spéculaire de tube.

**Les fibres ne sont pas « presque parallèles ».** C'est la correction la plus utile, et
elle est chiffrée : l'isotropie du champ des orientations vaut **0,988 sur 1,000**, et
même par blocs de 100 pixels le pic local médian ne dépasse le plat que de **51 %**. Une
seule zone (le quart bas-gauche) montre un alignement franc, à 2,6× le plat. Ce que l'œil
lit comme du parallélisme, ce sont ces **rares faisceaux alignés**, saillants parce que
brillants et nets, sur un fond presque isotrope. **La recette n'est donc pas un champ de
directions cohérent : c'est un fond isotrope plus quelques faisceaux fortement alignés.**

**Un détail absent de la description, et qui signe le matériau : la frisure.** En bas à
droite, une fibre isolée montre une ondulation **périodique et régulière**, en dents de
scie fines (période ~3-4 px, amplitude 1-2 px) : c'est le **crimp** mécanique imposé en
usine aux fibres de rembourrage. Il explique qu'aucune fibre ne soit jamais rectiligne, et
l'aspect moussu du feutre de fond.

Le reste tient. Les nœuds lumineux existent, mais ce ne sont pas des croisements de deux
câbles : ce sont des **points de convergence** d'où les fibres divergent en éventail. Les
grumeaux existent. La profondeur en couches existe, mais se fait surtout par **extinction
et perte de contraste**, pas par flou : un brin proche atteint 255 quand le fond lointain
plafonne vers 40.

### 2.2 Le mécanisme : de l'étirage de nappe, poussé cinquante fois trop loin

Le produit est déclaré **100 % polyester** (PET, ρ = 1,38) par les fabricants
[[made-in-china]](https://ly999999.en.made-in-china.com/product/sjqJLOTcEuVH/China-Halloween-Stretchy-Spider-Web-Color-Cobweb-Decoration-with-Spiders.html).
C'est de l'ouate cardée — la matière du rembourrage. Diamètre **12 à 25 µm** (1,5 à 6
denier, par `d = 11,89·√(den/ρ)`
[[MiniFibers]](https://www.minifibers.com/fiber-measurement-conversions)), longueur de
coupe **32 / 51 / 64 mm**
[[INDA]](https://www.inda.org/about-nonwovens/nonwovens-glossary-of-terms/). Un sachet de
100 g contient de l'ordre de **3 millions de brins** et **150 à 300 km** de fibre.

Le geste — écarter les mains — est exactement l'**étirage** (*drafting*) textile, sans
rouleaux : les fibres que la main avant pince, celles que l'arrière retient, et les
**flottantes**, qui n'ont que le frottement de leurs voisines pour se décider [[Fujino &
Itani]](https://www.jstage.jst.go.jp/article/jte1955/6/2/6_2_1/_pdf). La condition utile
est `l·cosθ < g ≤ 2·l·cosθ` : **l'écartement doit valoir entre une et deux longueurs de
brin**, soit 32 à 150 mm.

**Rien ne casse, tout glisse.** Sans matrice, le cisaillement à l'interface n'est que du
frottement fibre-fibre, donc la longueur critique de décohésion dépasse largement la
longueur de coupe : les extrémités sont **toujours effilées**, jamais des sections nettes.
Et la contraction de Poisson rend le glissement **saccadé**.

**La rupture se localise, et c'est elle qui fabrique les câbles.** Une nappe non tissée en
traction passe par quatre phases : élastique, durcissement pendant que les fibres
**pivotent vers l'axe de traction**, striction, adoucissement [[Int. J. Solids
Struct.]](https://www.sciencedirect.com/science/article/pii/S0020768314000195). Moins de
fibres dans la section, plus de contrainte par fibre, plus de fibres qui glissent : rien
ne stabilise. Le résultat publié est une *zone de rupture localisée formée des fibres
restantes alignées sur la direction de charge* — **exactement le pont brillant de
l'image**. Le câble n'est pas posé là : c'est le résidu d'une déchirure. Le grammage
confirme l'excès — **5 à 11 g/m²** une fois étiré contre 15 g/m² minimum pour un non-tissé
continu : donc plus un voile, mais nécessairement un réseau ajouré.

Les grumeaux sont chiffrés. Les **neps** — nœuds de fibres non ouvertes — ont un cœur de
**0,3 à 3 mm** d'où rayonnent des fibres sur **5 à 25 mm**, et se trouvent à **25 à 300
par gramme**
[[ScienceDirect]](https://www.sciencedirect.com/topics/engineering/fibre-neps), soit 2 500
à 30 000 par sachet. Les **slubs** renflent le brin de **+45 % à +400 %**, **85 %** des
fibres ont une extrémité recourbée, et l'**onde d'étirage** donne la période des
variations d'épaisseur : **4 × la longueur de brin**.

### 2.3 Pourquoi les fibres brillent par segments

Un cylindre n'a pas une normale mais un cercle de normales : il réfléchit dans un **cône**
de demi-angle égal à l'angle entre la lumière et son axe [[Marschner
2003]](https://graphics.stanford.edu/papers/hair/hair-sg03final.pdf). On ne voit un éclat
**que là où l'orientation locale place la caméra sur ce cône** : la brillance par segments
n'est pas du bruit, c'est de la géométrie, et elle est gratuite dès qu'on rend de vraies
courbes. Réglages : **η = 1,55**, largeur longitudinale **β_R = 5° à 10°** — mais le
décalage α_R de 6-10° vers la racine vient des **écailles de cuticule du cheveu**, qu'une
fibre synthétique n'a pas : ici **α ≈ 0**, un seul reflet net. Enfin, après diffraction,
bloom et saturation, une fibre occupe **2 à 4 fois** la largeur de son image géométrique :
le `p50 = 5 px` mesuré plus haut n'est pas la largeur de la fibre — la vraie fibre est
**sous le pixel**, comme celle d'une vraie toile.

---

## 3. Ce que ça demande à Clarisse

Tout ce qui suit a été vérifié en exécutant, sauf mention contraire. Les sondes sont dans
`J:\_WINDOWSTEMP\claude\` (`sonde_fibres.py`, `sonde_semis.py`, `sonde_fur.py`,
`sonde_racines.py`, `sonde_echelle.py`), leurs journaux à côté.

### 3.1 Ce qu'on a déjà, et qui sert directement

- **`native/cable_field/` — `GeometryCableField`.** Un câble par paire entre deux nuages
  de points, appariés par indice, proximité ou hasard, chacun avec son mou et son bruit. Sa
  propre documentation le dit : « la voie pour remplir un volume de faisceaux ou de fibres
  sans poser un seul locator ».
- **`native/tube/` — `GeometryTube`.** Torons non interpénétrants par construction,
  chaînette exacte, bruit projeté dans le plan normal, trim et embouts : ce qu'il faut pour
  **un câble vu de près**.
- **`native/common/cloth_solver.h`.** Solveur XPBD. `Distance{a, b, rest, compliance,
  compression, lambda}` porte **un réglage de compression séparé** — c'est exactement un
  fil : inextensible en tension, libre en compression, donc il pend au lieu de pousser.
  `Anchor` et `inverse_mass = 0` donnent les points épinglés. Rien là-dedans ne suppose un
  maillage : **le solveur marche tel quel sur un graphe de fils quelconque.**

### 3.2 La question tranchée : oui, la fourrure native prend notre géométrie

**Le CID le permet** : `geometry_support` de `GeometryFurGenerator` et de
`GeometryFurInterpolate` est un `reference { filter "Geometry" }`, pas
`GeometryPolymesh` ; et `guides` filtre sur `GeometryFur`, qui est dérivable
(`curves-clarisse.md` §4).

**L'exécution le confirme.** `GeometryFurGenerator` accepte notre `GeometryCableField`
comme support et produit **exactement une courbe par sommet** (2 394 → 2 394) ;
`GeometryFurInterpolate` accepte simultanément un générateur en guides et notre géométrie
en support ; et ça monte à **371 907 courbes en 0,03 s**. Mieux, les racines se sèment où
on veut : un `GeometryPointCloud` marche comme support, **y compris dispersé sur notre
propre géométrie**. La chaîne « notre géométrie → nuage → fourrure » est ouverte de bout
en bout, densité comprise.

**Le rendu le confirme.** `CurveMesh::get_bbox()` renvoie bien une boîte nulle, mesuré
`(0,0,0)-(0,0,0)` : on savait que ça interdit le scatter, restait à savoir si ça interdit
le rendu. **Non** — une image de contrôle (960 × 540, **0,93 s**,
`J:\_WINDOWSTEMP\claude\fur_apercu.png`) montre une sphère native témoin et, à côté, des
dizaines de milliers de brins poussés sur notre `GeometryCableField`, silhouette et brins
lisibles. Ils sortent **noirs** : c'est le matériau poil par défaut, pas un défaut de
géométrie.

Côté C++ : **`CurveMesh` a un constructeur et un `init()` publics, exportés dans
`ix_curve.def`** — un module tiers peut donc fabriquer des courbes natives de A à Z (rayon
par courbe ou par point, couleurs, UV, primvars, `set_roots()` sur un nuage), et sept
enveloppes traçables existent. **`ix_curve.lib` est là mais `native/build.py` ne le lie
pas encore.** Ce qui est **fermé** : le nœud `GeometryBundle` n'agrège rien — c'est un
lecteur Alembic/USD, le vrai agrégateur étant `SceneObjectCombiner` (`filter
"SceneObject"`), qui accepte nos modules ; et `Fur::generate` / `Fur::interpolate` sont
exportées sans leurs structures d'options.

### 3.3 Le coût, mesuré

| brins | profil | sommets | temps |
|---|---|---|---|
| 2 639 | 3 × 12 | 102 921 | 0,20 s |
| 9 986 | 3 × 12 | 389 415 | 0,51 s |
| **29 667** | 3 × 12 | **1 157 013** | **1,12 s** |
| 2 639 | 8 × 40 | 865 592 | 0,42 s |

Le coût par brin est exactement `sides × (steps + 1)` sommets ; le débit est linéaire,
**~26 500 brins/s**. Extrapolation : 100 000 fibres coûtent 3,9 M sommets et ~4 s ; **un
million en coûtent 39 M, ~900 Mo rien qu'en positions, et 40 s**. C'est le mur — et
l'image de référence montre 110 à 140 brins par ligne de balayage, donc un plan macro
plein cadre en demande facilement 10⁵ à 10⁶.

**D'où la règle : le polymesh pour ce qu'on voit de près, les courbes pour le reste.** Les
courbes coûtent trois fois moins en mémoire et en primitives de BVH, et la fourrure les
produit en 0,03 s là où le polymesh met 40 s. Elles perdent le displacement et le scatter,
dont un voile de fibres n'a aucun besoin.

### 3.4 Le solveur comme constructeur de toile

C'est la piste la plus payante, et `cloth-clarisse.md` la nommait déjà : « le solveur est
déjà générique ; ce qui manque à chaque fois, c'est le patron et les points épinglés ».
Pour une toile, le patron est le graphe de fils.

Une toile est un réseau masse-ressort, et le solveur en fait un sans une ligne nouvelle :
un `Distance` par segment, `compression` très souple pour qu'un fil ne pousse jamais,
`inverse_mass = 0` aux ancrages, un peu de gravité. Ce qu'on obtient gratuitement, et
qu'on devrait sinon dessiner à la main : **les rayons se tendent, les spirales se creusent
entre deux rayons, les jonctions prennent leur angle d'équilibre, et un fil coupé retombe
en entraînant ses voisins**. Tout ce que le §1 décrit comme conséquence de la tension sort
de la mécanique au lieu d'être peint.

Deux avertissements de `cloth-clarisse.md` valent ici à l'identique. **Une contrainte
métrique impossible ne produit pas « un peu de tension », elle produit un artefact** : les
longueurs au repos se mesurent donc sur la toile visée, pas sur un cercle parfait. Et **la
première chose à écrire est la sonde** — ici la tension par fil, la flèche de chaque
segment de spirale et le compte de fils cassés.

### 3.5 La proposition

Cinq nodes par ordre de valeur. Les trois premiers suffisent aux références de Romain ;
les deux derniers servent les vraies toiles.

| node | base | ce qu'il fait | coût |
|---|---|---|---|
| **1. `GeometryFiberPull`** | `GeometryPolymesh` | **simule l'étirage** au lieu de le dessiner : traction selon un axe, les fibres pivotent vers lui, la section faible se localise, les survivantes alignées forment les câbles. Sortie à trois niveaux. | **2-3 j** |
| **2. `GeometryFiberVeil`** | `GeometryFur` | le voile en courbes natives — rend un `CurveMesh` construit par nos soins, hérite tessellation, `GasFur` et matériau poil, et peut servir de `guides` à un `GeometryFurInterpolate` natif | **1,5 j** |
| **3. `GeometryWebSolve`** | `GeometryPolymesh` | prend un graphe de fils, le relaxe par le solveur XPBD, rend courbes ou polymesh selon la distance. C'est ici que vivent les fils cassés et pendants. | **2 j** |
| **4. `GeometryOrbWeb`** | `Geometry` | produit le graphe que le node 3 relaxe : séquence, rayons, pas de spirale, zone libre, asymétrie — tout ce que le §1 chiffre | **1 j** |
| **5. `TextureFiberDust`** | `Texture` | poussière et débris, pilotés par `TextureOcclusion` (les recoins se chargent) et `TextureCurveUtility` en `target = Fragment`, qui donne `Radius` et `Curve ID` le long du brin | **0,5 j** |

Le node 1 est **le seul vraiment nouveau** et c'est lui qui rend les références ; la
moitié de son coût est le modèle de rupture localisée, et ses paramètres sortent tels
quels du §2. Le node 2 demande d'ajouter `ix_curve` à `build.py`.

**Ce qu'on obtient sans rien écrire** : la chaîne `GeometryCableField` →
`GeometryPointCloud` → `GeometryFurGenerator` monte **déjà** à 120 000 courbes rendues en
**9,3 s**, avec `MaterialPhysicalHair` pour le shading et `SceneObjectCombiner` pour
empiler les couches. C'est la maquette à monter en premier, en interactif.

**Mais elle a montré sa limite en un rendu, et c'est la découverte de conception de la
journée.** Une fourrure fait des poils **libres à une extrémité** : poussée sur un support
volumique elle remplit le volume et donne un duvet, pas un voile. Or dans la référence
**les fibres relient deux points d'ancrage** — elles sont tendues, pas pendantes, et c'est
de là que viennent les longues lignes rectilignes et les mailles polygonales du §2.1. Le
voile porteur relève donc du modèle « câble entre deux points », pas du modèle « poil » :
le node 2 doit produire des courbes **à deux ancrages**, la fourrure native restant bonne
pour ce qui pendouille vraiment — fibres échappées, extrémités effilées, duvet des couches
lointaines.

**Ce qui manque vraiment** : l'étirage simulé (1), sans équivalent ; le contrôle fin des
courbes depuis notre code (2) ; le réseau relaxé (3). Le reste est de l'assemblage.

---

## 4. Les pièges relevés en route

- **Le semis blue noise rend exactement la moitié du compte demandé** (9 986 sur 20 000),
  quelle que soit la surface — testé à 4, 16 et 64 unités de côté. Les trois autres
  distributions sont exactes jusqu'à 400 000. **Demander le double.**
- **`CurveMesh` a une bbox nulle mais rend quand même.** Elle interdit le scatter, pas le
  rendu — la déduction naturelle était l'inverse.
- **L'échelle réelle passe.** Le même champ de câbles construit à des rayons de 1 mm à
  0,1 µm sur une portée de 30 cm garde le même compte de sommets et une boîte englobante
  saine, jusqu'à un rapport de **1:3 000 000**. On peut donc modéliser une toile à sa taille
  réelle, fil de 1 µm compris, sans échelle inventée. *Vérifié à la construction ; le
  comportement du BVH au rendu à cette échelle reste à confirmer.*
- **Une géométrie sans matériau assigné sort noire**, pas invisible — symptôme
  indistinguable d'un module mal monté. Le témoin natif dans la même image (méthode de
  `sdk-clarisse.md` §9bis) sépare les deux cas en un rendu.
- **`cnode.exe` est dans `<install>/Clarisse/`**, pas à la racine, et `-module_path` doit
  pointer `<install>/Clarisse/module`.
- **Une `Image` ne rend rien sans `Layer3d`.** Régler `.renderer` et `.camera` sur l'Image
  elle-même donne un cadre noir sans message : il faut `ix.cmds.AddLayer(...)` puis régler
  `active_camera` et `renderer` **sur le layer**. Un rendu perdu là-dessus.
- **`numpy` n'est pas installé sur ce poste** ; `PIL` (12.3.0) et ImageMagick le sont. Les
  mesures d'image de ce document sont en PIL pur.
