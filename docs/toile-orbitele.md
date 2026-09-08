# La toile orbitèle : ce qu'il faut savoir avant d'écrire `GeometryWeb`

> Recherche menée le 8 septembre 2026 par un agent dédié, sur littérature
> primaire uniquement, chaque nombre accompagné de son URL. Le corps du
> document commence à la section 1 et n'a pas été retouché : c'est le dossier
> de mesures. Cette page de garde-ci est ma lecture d'ingénieur — ce que ces
> mesures obligent à changer dans notre code.
>
> Ce fichier complète `toile-clarisse.md`, qui porte sur les **références de
> Romain** (fibre étirée, macro, trois échelles) et sur l'intégration Clarisse.
> Celui-ci porte sur la **toile d'araignée réelle**, qui est un autre objet.

## Ce que le dossier change dans ce qu'on allait faire

Neuf points. Les quatre premiers contredisent une hypothèse qu'on avait déjà
écrite quelque part ; c'est pour eux que ce document existe.

**1. Un fil de soie sec est droit. Pas de chaînette.** C'est le résultat le
plus contre-intuitif du lot, et il est sans appel : le paramètre de chaînette
d'une fibre de 3 µm tendue à 100 µN vaut **1 109 m**, quand la portée en fait
0,1. La flèche d'un rayon d'un mètre entier est de **0,11 mm** — un pixel sur
une image de 9000. Notre node de câble part de la chaînette ; pour une toile,
c'est physiquement faux et ça se verra. Les fils se posent **droits**, et ce
qui les courbe, ce sont des **charges ponctuelles** : la rosée pèse trois mille
fois le poids propre du fil, et une seule goutte d'un millimètre vaut 5 % de la
prétension d'un rayon. La courbure d'une toile de rosée est donc un **polygone
funiculaire** — des segments droits entre les gouttes — et pas une courbe lisse.

**2. Les gouttes de colle sont 2 à 4 fois plus espacées que Rayleigh ne le
dit.** Le rapport mesuré λ/R va de **12 à 35** selon l'espèce, jamais 9. La
formule non visqueuse λ = 9,02 R est le bon point de départ physique et la
mauvaise valeur pratique : la glu est visqueuse (Oh ≈ 6, soit ~100 cP), et λ
croît en √Oh. Pour du *Araneus* ou de l'*Argiope* — les toiles de jardin qu'on
veut rendre — c'est **λ ≈ 28 à 35 R**. Un réglage calé sur 9 R donne des perles
collées les unes aux autres.

**3. Nos gouttes sont trop rondes.** J'ai livré ce matin une largeur de perle
déduite pour que la goutte soit **exactement ronde**. Les gouttes réelles ne le
sont pas : Opell les modélise en **paraboles**, avec un rapport
longueur/largeur de **1,2 à 1,65** (moyenne **1,35**) — des onduloïdes enfilés
sur le fil. Et il y a des **gouttes secondaires** entre les primaires, dont le
volume va de 1,8 % (*Argiope aurantia*) à **59 %** (*Larinioides*) de la
primaire. La déduction automatique doit viser 1,35, pas 1,0, et le node gagne
une alternance grosse/petite.

**4. Une toile parfaite est fausse.** **35 ± 1 anomalies par toile** chez
*Zygiella* : **84 %** des toiles ont au moins un rayon dévié, **45 %** un rayon
en Y, **15 %** un rayon surnuméraire. Et le pas de spirale varie de **±30 %
d'un tour à l'autre**. Le générateur doit produire ce désordre-là, pas un
bruit uniforme ajouté après coup : ce sont des accidents de construction, donc
des accidents **de topologie**, pas de position.

**5. Le pas de spirale est sectoriel, pas radial.** L'erreur naturelle serait
de faire croître le pas avec le rayon. Le vrai comportement : il **croît vers
le haut** (pente +0,022) et reste **constant ou décroît vers le bas** (−0,010).
La maille se resserre vers le bas. Deux régressions le montrent chez deux
genres différents, l'une significative au nord et **non significative au sud**.

**6. Deux spirales, deux lois.** L'auxiliaire est **logarithmique**, la
spirale de capture est **arithmétique** (Archimède). Elles tournent dans le
même sens, avec **94 % de similarité d'enroulement**, et la capture est posée
**du bord vers le centre** en mangeant l'auxiliaire — ce qui laisse de petites
boules de soie enroulées sur les rayons, visibles en macro. Le moyeu conserve
la partie interne de l'auxiliaire, qu'on appelle à tort « spirale de renfort ».

**7. Deux asymétries indépendantes, à ne pas confondre.** L'asymétrie
**d'extension** (le bas est 1,3 à 1,4× plus grand) et l'asymétrie **angulaire**
(13,2° en haut contre 8,4° en bas). La seconde **survit** à une extension
nulle : même dans une toile verticalement symétrique, les angles restent plus
petits en bas, chez **92 araignées sur 93**. Ce sont donc deux réglages
séparés dans le node, et la forme générale de la toile en est un troisième
(décorrélé, R² = 0,02).

**8. La hiérarchie des tensions est un rapport, pas une valeur.** **10 : 7 : 1**
pour amarrage : cadre : rayon, mesuré identique chez quatre espèces, donc
indépendant de la géométrie. La spirale est à **1/10 du rayon**. C'est
exactement ce qu'il faut pour régler les compliances relatives dans
`cloth_solver.h` — et ça donne au passage la bonne raideur relative sans avoir
à inventer des unités.

**9. La spirale de capture n'a jamais de mou.** Le treuil capillaire enroule
l'excédent de fil **dans la gouttelette** : à tension constante, la longueur
s'ajuste. Le fil s'allonge jusqu'à trois fois sans casser et revient sans
affaissement. Conséquence directe : si notre relaxation produit des spirales
molles, c'est un artefact, pas du réalisme. Le mou visible d'une vraie toile
vient des **points d'attache** et des **charges**, jamais du fil lui-même.

## Ce que j'en retiens pour le node

Le générateur se construit dans l'ordre de l'araignée, parce que c'est cet
ordre qui produit les irrégularités justes : ancrages → cadre → rayons (du haut
vers le bas, chacun inséré **sous** le précédent, le point d'attache au cadre
décidant l'angle au moyeu) → moyeu → auxiliaire logarithmique du centre vers le
bord → spirale de capture arithmétique du bord vers le centre. La relaxation
par `cloth_solver.h` vient après, avec les compliances au rapport 10:7:1, et
elle n'a pas à inventer la forme — seulement à la détendre.

La fiche de valeurs par défaut est en fin de document : c'est elle qui donne
les valeurs d'usine du node.

---

# TOILE ORBITÈLE (Araneidae) — DOSSIER TECHNIQUE CHIFFRÉ POUR RECONSTRUCTION 3D

**Note liminaire — vocabulaire français normalisé.** Zschokke a publié une table de correspondance anglais/allemand/français des termes de la toile : *fil d'attache* (anchor thread), *cadre* / *cadre secondaire* (primary/secondary frame), *câble suspenseur* ou *fil suspenseur* (bridge thread), *rayon* ou *diamètre* (radius), *moyeu* (hub), *spirale interne* (hub spiral / strengthening spiral), *spirale auxiliaire* / *provisoire* / **sèche** (auxiliary spiral), *fil spiralaire* / *spire captrice* / *spirale gluante* (sticky spiral), *coudes en épingle à cheveux* ou *grecques* (U-turns), *fil avertisseur* (signal thread), *stabilimentum*. Table complète : https://bio.staern.li/pdf/zschokke1999joa.pdf (J. Arachnol. 27:542-546, Table 1)

---

## 1. SÉQUENCE DE CONSTRUCTION

### 1.1 L'ordre exact, étape par étape

Sources primaires : Zschokke 1996, *Rev. Suisse Zool.* hors-série 709-720 — https://bio.staern.li/pdf/zschokke1996rsz.pdf · Zschokke & Vollrath 1995, *Eur. J. Entomol.* 92:523-541 — https://bio.staern.li/pdf/zschokke1995eje.pdf

**a) Exploration + pontage.** En labo (sans vent) : l'araignée fixe sa traîne en haut d'un support, descend, contourne par le bas, remonte, tend et fixe. En nature : le fil part **au vent** et s'accroche de l'autre côté ; les toiles d'*A. diadematus* franchissent couramment **plusieurs mètres**. Distance parcourue pendant l'exploration seule : **2,79 à 63,21 m** (médiane **5,61 m**) sur support simple, **6,55 à 212,53 m** (médiane **27,60 m**) sur support complexe. Reconstruire sur un cadre existant coûte beaucoup moins (U=1, p=0,005).

**b) Proto-moyeu + proto-rayons.** L'araignée pose et retire des fils sans motif visible jusqu'à ce qu'un point de convergence émerge. Elle le complète avec **4 à 7 proto-rayons** (Petrusewiczowa 1938, Mayer 1952, Krieger 1992). **31 des 34** constructions de proto-rayon suivent une de six variantes du même patron, dont la méthode « Tarzan » : fixer le fil, marcher quelques centimètres, se laisser tomber en pendule.

**c) Premier fil de cadre = toujours le fil du HAUT.** L'araignée le tend **assise au milieu du fil**, ce qui est mécaniquement absurde (il faut se soulever soi-même) et reste inexpliqué.

**d) Déplacement du moyeu + premier rayon définitif.** Chez *A. diadematus*, ce déplacement est unique et la position devient finale dans **25 des 32** cas (7/32 déplacements ultérieurs, p=0,0011) — https://bio.staern.li/pdf/zschokke1995eje.pdf. Chez *Larinioides patagiatus* au contraire, le moyeu bouge dans **11 cas sur 12**.

**e) Cadre + rayons.** Cadre primaire (attaché aux fils d'attache) et cadre secondaire. Sur **47 cadres primaires** observés, **19 furent construits SANS insérer un rayon en même temps** — ce qui contredit la règle de Coddington 1986. Peu de cadres secondaires : **8 sur 55**. Règle absolue : le nouveau rayon est **toujours inséré immédiatement SOUS un rayon existant**, jamais au-dessus. Les rayons au-dessus du moyeu sont posés avant ceux du dessous (Krieger 1992).

**f) Moyeu.** En cherchant un espace pour le rayon suivant, l'araignée tourne autour du moyeu ; ce mouvement de rotation **continue après le dernier rayon** et devient la spirale interne. Chez *A. diadematus* : **32 sur 32** webs font au moins une boucle complète après le dernier rayon.

**g) Passage à la spirale auxiliaire.** « Le tournoiement autour du moyeu bascule **soudainement et sans interruption** dans la construction de la spirale auxiliaire » — construite du **centre vers la périphérie**, sans demi-tour : chez *A. diadematus*, seulement **2 des 32** toiles (**16 %**) contiennent un unique demi-tour dans l'auxiliaire. Chez *Uloborus walckenaerius* : **17 des 22** toiles, moyenne **2,4 ± 1,8** demi-tours — https://bio.staern.li/pdf/zschokke1995pb.pdf

**h) Demi-tour + spirale de capture, du BORD vers le CENTRE.** L'inversion de sens entre auxiliaire et capture est présente dans **97 des 100** toiles analysées (p<0,001) — https://bio.staern.li/pdf/zschokke1993bas.pdf. L'auxiliaire est mangée fil par fil, laissant des **petites boules de soie enroulées sur les rayons** (visibles au rendu macro).

**i) Réfection du moyeu**, puis l'araignée s'installe au centre.

**j) Recyclage.** Toile remplacée **chaque nuit** (Wiehle 1927) ; les fils d'attache et de cadre sont réutilisés, **tous les rayons et toute la spirale de capture sont refaits** (Carico 1986) ; l'ancienne toile est ingérée.

### 1.2 Durées mesurées

| Phase | *Araneus diadematus* | *Uloborus walckenaerius* |
|---|---|---|
| Rayons | **0 → ~16 min** | 0 → ~12 min |
| Spirale auxiliaire | **~16 → ~19 min (≈3 min)** | ~12 → ~15 min |
| Spirale de capture | **~19 → ~63 min (47 min mesurées)** | ~15 → ~75 min (**62 min**) |
| **Total** | **≈ 60-65 min** | ≈ 75 min |

Fig. 2 et texte de Zschokke & Vollrath 1995, *Physiol. Behav.* 58:1167-1173 — https://bio.staern.li/pdf/zschokke1995pb.pdf. La spirale auxiliaire est **le poste le plus court de toute la construction** : ~5 % du temps total.

**Longueur de spirale de capture dans ces toiles : 1088 cm (Araneus) contre 369 cm (Uloborus)** — même source. Distance totale parcourue pour bâtir la toile (rayons + 2 spirales) : **7,86 à 18,46 m, médiane 13,64 m** — https://bio.staern.li/pdf/zschokke1996rsz.pdf

**Autres espèces** — https://bio.staern.li/pdf/zschokke1995eje.pdf : la plupart des Araneidae **≈ 1 heure** ; ***Nephila* jusqu'à 4 heures** ; *Fecenia singaporiensis* **2 h 30** ; *Cyrtophora citricola* **plusieurs nuits** pour une toile qui dure **des semaines** (une orbe normale dure **un jour ou deux, quelques jours au plus**).

### 1.3 Vitesses de l'araignée (à utiliser pour animer)

Table 1 et 2 de https://bio.staern.li/pdf/zschokke1995pb.pdf, en cm/s :

| | Rayons (vitesse max) | Auxiliaire (max / moy) | Capture (max / moy) |
|---|---|---|---|
| *A. diadematus* (n=30) | **6,77 ± 1,36** | **1,70 ± 0,26** / **0,76 ± 0,15** | **1,83 ± 0,36** / **0,47 ± 0,11** |
| *U. walckenaerius* (n=18) | 6,77 ± 1,66 | 1,71 ± 0,46 / 0,73 ± 0,24 | 0,95 ± 0,29 / **0,12 ± 0,02** |

Vitesse de **production de soie** (corrigée du temps de fixation) : auxiliaire **2,80 ± 1,11 cm/s**, capture **0,83 ± 0,32 cm/s** chez *Araneus* ; **2,12 ± 0,80** et **0,10 ± 0,01** chez *Uloborus*. Autrement dit : **la pose de rayon est 4 fois plus rapide que la pose de spirale**, et la spirale de capture est ~3,4× plus lente que l'auxiliaire.

Temps d'arrêt à chaque fixation sur un rayon : **≈ 1 s** — https://bio.staern.li/pdf/zschokke1995eje.pdf

### 1.4 L'auxiliaire sert de main courante (« handrail »)

Zschokke 1993 — https://bio.staern.li/pdf/zschokke1993bas.pdf :
- **Similarité d'enroulement auxiliaire/capture : 94,4 % ± 10,2 (n=100)**, >90 % dans la plupart des toiles. Les deux spirales tournent dans le **même sens**.
- Nombre moyen de demi-tours dans la spirale de capture : **11,0 ± 5,6 (n=100)**, dont seulement **2,1 ± 2,3** dans le tour le plus externe de l'auxiliaire ; **73 toiles** en ont ≤2 dans cette zone, **29** aucun. Les demi-tours sont donc **concentrés sur le bord extérieur** de la toile.
- Effet de rendu : l'araignée suit **plusieurs boucles de capture par boucle d'auxiliaire** avant de casser celle-ci ⇒ les tracés de capture forment des **faisceaux (« bundling »)** autour de l'ancienne auxiliaire. Fort chez *Gasteracantha cancriformis* et *Hyptiotes* (similarité 100 %), faible chez *A. diadematus*, **nul chez *Zygiella x-notata* et *Uloborus***.

---

## 2. NOMBRE DE RAYONS

| Espèce | Nombre de rayons | Source |
|---|---|---|
| ***Araneus diadematus*** | **32,1 ± 0,5** (N=5 araignées, n=96 toiles) | https://european-arachnology.org/esa/wp-content/uploads/2015/08/107-116_Vollrath.pdf |
| *A. diadematus* | **33,1** (somme des quadrants, n=194 quadrants) | https://core.ac.uk/download/85215251.pdf |
| *A. diadematus* (valeur classique) | **25 à 30**, angles **12 à 15°** | https://en.wikipedia.org/wiki/Araneus_diadematus |
| *Zygiella x-notata* | **26 ± 1** (boîtes), **25 ± 1** (cadres), n=25 chacun | https://zoologicalstudies.springeropen.com/counter/pdf/10.1186/1810-522X-53-11 |
| *Larinioides (Nuctenea) sclopetaria* | **18 ± 2,2** (adultes, n=27) ; **17,9 ± 2,7** (juvéniles, n=22) | https://zenodo.org/records/16392188/files/bhlpart228731.pdf |
| *Nephila clavipes* | **plus de rayons subsidiaires que de rayons normaux** ; rayons quasi parallèles | https://bio.staern.li/pdf/zschokke1995eje.pdf |
| *Hyptiotes paradoxus* | **4 rayons** exactement (orbe réduite) | idem |
| Toile de référence des modèles EF | **33 rayons** (Zaera 2014) · **35 rayons** (Soler & Zaera 2016) | https://pmc.ncbi.nlm.nih.gov/articles/PMC4233696/ · https://www.nature.com/articles/srep31265 |

**Sous-catégories** : **4 à 7 proto-rayons**, puis **5 à 8 rayons primaires** (moyennes par araignée **6 ± 0,74 · 6,8 ± 0,68 · 5,9 ± 0,58**), le reste en rayons secondaires — https://european-arachnology.org/esa/wp-content/uploads/2015/08/107-116_Vollrath.pdf

**Le nombre de rayons ne dépend PAS de la taille de l'araignée.** Corrélations de Pearson (*L. sclopetaria*, adultes/juvéniles) : céphalothorax **0,03 / −0,12 (ns)** · longueur de patte **0,03 / −0,19 (ns)** · masse **0,06 / −0,09 (ns)** — alors que l'aire de capture corrèle à **0,48\*\* / 0,80\*\*** — https://zenodo.org/records/16392188/files/bhlpart228731.pdf. Chez *Zygiella* : R²=0,02, n=127, ns — http://www.eje.cz/doi/10.14411/eje.2013.065.pdf. Interprétation publiée : il existe un **nombre minimal de rayons nécessaire à la stabilité**, et les grosses araignées augmentent le **diamètre de la soie** plutôt que le nombre de rayons.

**Le DIAMÈTRE de la toile, lui, dépend de la taille** : **D = 22,28 × L^0,78** (D en mm, L = longueur du corps en mm ; SE de l'exposant 0,127, P=1,2×10⁻⁷). Allométrie **négative**. Diamètres mesurés *A. diadematus* : **66,5 à 210 mm**, moyenne **137,9 ± 40,6 mm** — https://www.americanarachnology.org/journal-joa/joa-all-volumes/detail/article/download/arac-51-02-217.pdf?no_cache=1. La toile pèse **≈ 0,2 % de la masse de l'araignée**, soit **< 0,5 mg** pour **~20 m de soie** et **>1000 jonctions** — https://journals.biologists.com/jeb/article/216/18/3388/11584/ · https://www.bumblebee.org/invertebrates/Araneae5.htm

---

## 3. PAS DE SPIRALE (mesh height)

### 3.1 Valeurs en mm — 17 espèces, table complète

Herberstein & Heiling 1998, *Eur. J. Entomol.* 95:367-371, Table 1 — https://www.eje.cz/pdfs/eje/1998/03/10.pdf. « Mesh height » = distance moyenne entre deux spires collantes.

| Espèce | **Pas (mm)** | Proie moy. (mm) |
|---|---|---|
| *Eriophora transmarina* | **7,5** | 5,7 |
| *Eriophora fuliginea* | **5,6** | 2,2 |
| ***Araneus diadematus*** | **5,6** | 2,9 |
| *Argiope aurantia* | **4,6** | 16,3 |
| *Parawixia bistriata* (grande toile) | **4,5** | 8,3 |
| *Meta reticulata* | **3,9** | 2,0 |
| *Argiope argentata* | **3,7** | 2,8 |
| *Argiope keyserlingi* | **3,5** | 5,1 |
| ***Zygiella x-notata*** | **3,4** | 2,4 |
| *Argiope trifasciata* | **2,7** | 10,2 |
| *Nephila clavipes* | **2,6** | 2,1 |
| ***Cyclosa conica*** | **2,5** | 2,0 |
| *Nephila plumipes* | **2,0** | 5,4 |
| *Larinioides sclopetarius* | **2,0** | 2,5 |
| *Leucauge venusta* | **1,9** | 3,0 |
| *Micrathena gracilis* | **1,4** | 7,0 |
| *Parawixia bistriata* (petite toile) | **1,4** | 2,0 |
| *Mangora placida* | **1,1** | 4,0 |

**Aucune corrélation significative pas ↔ taille de proie** (R_p=0,18, n=19, p>0,05). Ne câblez donc pas le pas sur une « proie cible ».

Autres valeurs utiles : *Zygiella x-notata* en labo **2,7 ± 0,2 mm**, **31 ± 1 tours**, **520 ± 25 cm** de fil spiralaire, aire de capture **114 ± 11 cm²** — https://zoologicalstudies.springeropen.com/counter/pdf/10.1186/1810-522X-53-11 · *Argiope aurantia* **4,8 ± 0,7 mm**, *A. trifasciata* **3,3 ± 1,2 mm** — https://pmc.ncbi.nlm.nih.gov/articles/PMC6745672/ · toiles de référence EF **4,0 mm** (Zaera) et **4,0 mm** (Yavuz).

**Espacement des jonctions spirale/rayon le long d'un rayon** : **2,06 ± 0,29 mm** (*Araneus*), **2,04 ± 0,27 mm** (*Zygiella*) — https://link.springer.com/content/pdf/10.1007/s00114-018-1561-1.pdf

### 3.2 Comment le pas varie du moyeu vers le bord — la réponse est SECTORIELLE

C'est le point le plus mal compris et le plus important pour un rendu crédible. **Le pas n'est pas uniformément croissant : il croît vers le haut et reste constant, voire décroît, vers le bas.**

- ap Rhisiart & Vollrath 1994, *Behav. Ecol.* 5:280-287 — https://core.ac.uk/download/85215251.pdf — régressions hauteur de maille vs largeur de maille chez *A. diadematus* :
  - **Nord (haut)** : hauteur = **0,106 × largeur + 1,43** — F₁,₃₁₂=161,06, **P<0,001**
  - **Sud (bas)** : hauteur = **0,011 × largeur + 2,86** — F₁,₄₆₁=1,87, **non significatif**
  - Verbatim : dans les secteurs bas la maille garde **une taille donnée sur toute la toile**, alors qu'en haut elle **s'élargit du moyeu vers la périphérie**.
- Zschokke & Nakata 2015, *Biol. J. Linn. Soc.* 114:659-672 (191 toiles de *Cyclosa*) — https://bio.staern.li/pdf/zschokke2015bjls.pdf — pente d'augmentation de la largeur de maille vers la périphérie : **au-dessus du moyeu +0,022**, côtés **+0,009 / +0,010**, **au-dessous du moyeu −0,010** (F₃,₃₅₆=123,6, P<0,0001). **La maille se RESSERRE vers le bas.**
- Forme de la maille (longueur/largeur) : **1,93 près du moyeu → 4,75 en périphérie**. Peters 1951 : **3,58** pour *Zygiella*, **5,10** pour *Araneus* (même source).
- Longueur des mailles périphériques par quadrant : haut **7,58**, côtés **6,60**, bas **5,84** (F₃,₃₅₆=35,73, P<0,0001).
- Variabilité (CV) de la largeur de maille : haut **0,308**, bas **0,267**.
- **Variation d'un tour à l'autre : ≈ 30 %** (Vollrath, données non publiées citées dans https://bio.staern.li/pdf/zschokke1993bas.pdf). **Ne posez pas une spirale parfaitement régulière.**

### 3.3 La loi géométrique sous-jacente

- **Vollrath & Mohren 1985**, *Naturwissenschaften* 72:666-667 — https://link.springer.com/article/10.1007/BF00497445 : la **spirale auxiliaire est LOGARITHMIQUE** (pas croissant géométriquement), la **spirale de capture est ARITHMÉTIQUE** (Archimède, pas nominalement constant). Deux lois différentes pour deux spirales, c'est le résultat structurant.
- **Règle de segment (Peters 1939, 1947, 1954)**, reprise par Zschokke 2002 — https://bio.staern.li/pdf/zschokke2002ea.pdf : *« les distances entre les boucles de soie collante sont proportionnelles aux distances entre les rayons »*. Le mécanisme physique donné : les rayons qui arrêtent la proie s'écartant vers la périphérie, les boucles collantes doivent s'écarter aussi pour couvrir uniformément. La proportionnalité exacte est contestée (ap Rhisiart & Vollrath 1994) mais **la relation de base est confirmée**.
- **Exception qui confirme** : *Nephila* et *Cyrtophora* insèrent des **rayons subsidiaires** pour garder l'écart inter-rayons constant ⇒ chez elles **le pas de spirale ne croît PAS du centre vers le bord** — https://bio.staern.li/pdf/zschokke2002ea.pdf

### 3.4 La règle de la patte (leg-span rule)

- **Vollrath 1987, 1992** : *« le pas résulte de la longueur de la patte utilisée pour fixer le fil spiralaire sur les rayons »* — cité verbatim dans https://www.eje.cz/pdfs/eje/1998/03/10.pdf. C'est une **contrainte anatomique**, pas une stratégie de chasse.
- Confirmation expérimentale par amputation : **Vollrath 1987, *Nature* 328:247-248**, « Altered geometry of webs in spiders with regenerated legs » — https://www.nature.com/articles/328247a0
- Vollrath et al. 2002 décrit le geste : l'araignée mesure l'angle entre deux rayons voisins **avec ses première et deuxième pattes du côté extérieur du corps**, en s'agrippant au moyeu avec les pattes intérieures — https://european-arachnology.org/esa/wp-content/uploads/2015/08/107-116_Vollrath.pdf
- **Mais la première boucle n'obéit pas à cette règle** : Eberhard 2012 (*Bull. Br. Arachnol. Soc.* 15:224-227) montre que sur les rayons courts l'araignée touche le cadre et cale la première boucle sur la distance auxiliaire↔cadre (R²=0,27 et 0,37, p<0,001), alors que sur les rayons longs il n'y a **aucune relation** (R²=0,00004, p=0,97) — https://britishspiders.org.uk/system/files/library/150703.pdf

### 3.5 Facteurs environnementaux (pour justifier une variation)

**Vent 0,5 m/s** pendant la construction : toiles plus petites et plus rondes, **moins de spires, pas plus large**. **Température 24 → 12 °C** : spirale de capture avec **moins de mailles et plus espacées**. Vent constant 1,1 m/s pendant 7 jours : aire de capture réduite (F=4,35, P<0,001), maille plus lâche (F=11,40, P=0,001), **volume des gouttes plus grand** (F=5,43, P=0,02) — https://doi.org/10.1242/jeb.083618

---

## 4. ZONE LIBRE, MOYEU, SPIRALE DE RENFORT

### 4.1 Définitions rigoureuses

Source : Zschokke 1999, *J. Arachnol.* 27:542-546 — https://bio.staern.li/pdf/zschokke1999joa.pdf

- **Moyeu (hub)** : zone centrale de fils densément maillés, où convergent les rayons.
- **Spirale interne / spirale de renfort (hub spiral)** — définition exacte et souvent mal citée : *« la spirale interne est la partie la plus intérieure de la spirale auxiliaire, celle qui n'est PAS retirée pendant la construction de la spirale collante »*. Ce n'est donc pas une soie à part : **c'est l'auxiliaire qui survit au centre**.
- **Zone libre (free zone)** : anneau de **rayons seuls**, sans aucune spirale, entre le moyeu/spirale interne et la première boucle collante. Fonction : permettre à l'araignée de **passer de la face avant à la face arrière de la toile** sans se coller, et de foncer sur une proie sans être freinée.
- **Aire de capture** = moyeu + zone libre + spirale gluante ; les estimateurs de longueur de fil retranchent l'aire du moyeu+zone libre — https://academic.oup.com/aesa/article/94/3/490/11838

### 4.2 Dimensions à utiliser

Aucune étude ne publie une « largeur de zone libre » moyenne. En revanche, **les géométries de référence des modèles éléments finis sont explicitement calées sur des toiles réelles** et donnent des rayons exploitables :

| Modèle | Moyeu | Zone libre | Spirale collante | Bord |
|---|---|---|---|---|
| Yavuz et al. 2024 (*A. diadematus*) — https://doi.org/10.1098/rsos.240986 | **0 → 2 cm** | **2 → 8 cm** | **8 → 20 cm** | 20 cm + ancrages 4 cm |
| Zaera et al. 2014 — https://pmc.ncbi.nlm.nih.gov/articles/PMC4233696/ | spirale de moyeu **7,5 → 30 mm** | **30 → 40 mm** | **40 → 110 mm** | polygone R=141,4 mm |

**Règle pratique** : moyeu ≈ **10 % du rayon de la toile**, zone libre s'étendant jusqu'à **30-40 % du rayon**, spirale de capture sur les **60-70 %** externes. Chez Zaera la zone libre ne fait que **10 mm sur 110** (9 %), chez Yavuz **6 cm sur 20** (30 %) — la fourchette est large, prenez ~15-25 % du rayon.

**Placement de la première boucle collante** — les deux repères mesurés (Eberhard 2012, https://britishspiders.org.uk/system/files/library/150703.pdf) sont la **distance TS-F** (dernière boucle d'auxiliaire → cadre) et la **distance TS-L1** (dernière auxiliaire → première boucle collante). Sur rayon court, l'araignée touche le cadre et corrèle les deux ; sur rayon long, elle ne le touche pas et la relation disparaît. Trois fonctions publiées de l'auxiliaire : **pont de circulation**, **maintien des rayons en position** contre le poids de l'araignée, et **guide de placement** pour la spirale collante.

**Rigidité du moyeu** : chez *Zygiella*, un **moyeu rigide** et le secteur libre sont décrits comme deux mécanismes physiques de transmission de l'information vibratoire — https://link.springer.com/content/pdf/10.1007/s00114-018-1561-1.pdf. Atténuation le long d'un rayon : **−2,25 à −1,13 dB/cm** (*Zygiella*), **−3,12 à −0,79 dB/cm** (*Araneus*).

---

## 5. POURQUOI LES RAYONS NE SONT PAS ÉQUIRÉPARTIS

### 5.1 Asymétrie verticale — chiffres par espèce

Convention Zschokke : **a = (haut − bas)/(haut + bas)** ; a négatif = bas plus grand ; **ratio bas/haut = (1−a)/(1+a)**.

| Espèce | Indice a | **Ratio bas/haut** | Source |
|---|---|---|---|
| ***Araneus diadematus*** | **−0,167** (n=30, P<0,0001) | **1,40** | https://bio.staern.li/pdf/zschokke2011joa.pdf |
| *A. diadematus* | −0,166 ± 0,015 ; témoins −0,209 ± 0,025 | 1,40 / **1,53** | https://bio.staern.li/pdf/coslovsky2009jib.pdf |
| *A. diadematus* (longueurs de rayons) | nord **83,8 ± 1,4 mm** / sud **107,7 ± 2,1 mm** → −0,125 | **1,285** | https://core.ac.uk/download/85215251.pdf |
| *Larinioides sclopetaria* | aire haut **14 680 ± 5 760 mm²** / bas **26 676 ± 9 141 mm²** | **1,82** | https://zenodo.org/records/16392188/files/bhlpart228731.pdf |
| *Cyclosa* spp. (Hawaii) | asymétrie de moyeu **0,28 ± 0,02** (n=226) | 1,39 | https://www.americanarachnology.org/journal-joa/joa-all-articles/article/download/arac-030-01-0070.pdf/ |
| *Tetragnatha* spp. | **0,09 ± 0,02** | 1,10 | idem |
| *Verrucosa arenata* (tête en HAUT) | −0,20 | **1,50** | https://bio.staern.li/pdf/zschokke2006cjz.pdf |
| ***Nephila clavipes*** (semi-orbe) | **−0,64** — moyeu à **9 ± 2 cm** du haut, **41 ± 4 cm** du bas | **4,6** | idem |
| *Cyclosa ginnaga* (tête en haut) | **+0,12** (P<0,001) | **inversée**, haut/bas 1,27 | https://bio.staern.li/pdf/nakata2010prsb.pdf |
| *C. argenteoalba* (tête en haut) | **+0,077** | inversée 1,17 | idem |
| *C. octotuberculata* (tête en bas) | **−0,055** | 1,12 | idem |
| *C. confusa* tête en bas / de côté / tête en haut | **−0,105** / −0,03 (ns) / **+0,048** | 1,23 / ~1,06 / inversée | idem |
| *Micrathena*, *Gasteracantha* (toiles inclinées) | ≈ 0 | **1,0 symétrique** | https://bio.staern.li/pdf/zschokke2015bjls.pdf |

**Décentrage du moyeu** : **d = |a| × rayon vertical moyen**. Pour *A. diadematus*, le moyeu est **16,7 % du rayon vertical au-dessus du centre géométrique**, soit **8,4 % du diamètre vertical**. Étalonnage publié : un moyeu décalé de **10 %** donne une asymétrie de moyeu de **0,18**, de **20 %** → **0,23** — https://www.americanarachnology.org/journal-joa/joa-all-articles/article/download/arac-030-01-0070.pdf/

**Forme générale de la toile** (indépendante de l'asymétrie !) : *A. diadematus* verticale, forme = (largeur−hauteur)/(largeur+hauteur) = **−0,0627 ± 0,0190 (n=13)** ⇒ **la toile est ~13 % plus haute que large** ; couchée à l'horizontale pendant la construction : **+0,0087 ± 0,0435**, donc ronde — https://bio.staern.li/pdf/zschokke1993bas.pdf. Rapport diamètre horizontal/vertical **RD** : *Zygiella* **0,71**, *A. diadematus* **0,80**, *L. sclopetarius* **0,95** — https://academic.oup.com/aesa/article-pdf/94/3/490/40522932/aesame0490.pdf. **Forme de la toile et décentrage du moyeu sont décorrélés (R²=0,02, n=226)** : deux réglages indépendants dans votre rig.

### 5.2 Pourquoi : l'araignée descend plus vite qu'elle ne monte

ap Rhisiart & Vollrath 1994 — https://core.ac.uk/download/85215251.pdf — vitesse moyenne pendant une attaque réelle (*A. diadematus*) :

| Quadrant | Vitesse (mm/s) | Temps de réaction | Temps d'orientation |
|---|---|---|---|
| **Nord (haut)** | **88,4 ± 16,0** | 0,07 ± 0,02 s | 0,14 ± 0,02 s |
| Ouest | 92,8 ± 12,0 | 0,07 | 0,15 |
| Est | 115,2 ± 16,9 | 0,08 | 0,14 |
| **Sud (bas)** | **164,4 ± 14,5** | 0,41 ± 0,36 s | 0,49 s |

Vitesse : **F₃,₄₇=4,85, p<0,01** ; réaction et orientation **non significatives**. **Ratio bas/haut = 1,86.** Carte d'accessibilité en m/s par octant : **0,068 en haut** → **0,125-0,128 en bas** (ratio **1,88**). Recalcul d'après Masters & Moffat 1983 : **153,9 mm/s en descente contre 86,9 mm/s en montée (ratio 1,77)**.

**Deux contrôles décisifs**, même source : (i) les mêmes toiles **couchées à plat** inversent la tendance (nord 191 mm/s, sud 60 mm/s, ns) ; (ii) **retourner la toile** ne change rien (F₁,₁₁=0,134, ns) — l'araignée court vite **vers le bas géographique**, pas vers le bas de la toile.

Nakata & Zschokke 2010 (*Cyclosa*, cm/s) — https://bio.staern.li/pdf/nakata2010prsb.pdf : les espèces **tête en haut** ne montrent **aucune différence** montée/descente (p=0,12 à 0,79) ; les espèces **tête en bas** descendent significativement plus vite : *C. confusa* **3,8 → 6,4 cm/s** (p=0,005), *C. octotuberculata* **3,5 → 8,2 cm/s** (p<0,001). ANOVA descente **F₄,₆₁=13,10, p<0,001** ; ANOVA montée **F₄,₆₁=0,79, p=0,533**. **La vitesse de montée est un plafond quasi invariant (~3 cm/s) ; c'est la descente qui varie.**

### 5.3 Effet de la taille / de la masse

- *L. sclopetaria* : asymétrie **exponentielle** en fonction de la largeur du céphalothorax, **y = 0,982 × 10^(0,24x)**, R²=0,374, df=47, **P<0,001** — https://zenodo.org/records/16392188/files/bhlpart228731.pdf
- *Nephila pilipes*, n=100 toiles : **la masse prédit l'asymétrie mieux que la taille, la longueur de patte ou le stade** — https://pubmed.ncbi.nlm.nih.gov/21060982/
- **Contre-exemple à mentionner** : chez *A. diadematus*, ajouter **+50 % de masse** ne change **rien** (témoins −0,209 ± 0,025 vs lestées −0,217 ± 0,021 ; Welch t=0,23, **p=0,821**). L'explication alternative est un **coût de construction** : la moitié haute coûte moins de **temps** (−0,091 ± 0,020, p<0,001) mais plus d'**énergie** (+0,066 ± 0,014, p<0,001) ; l'asymétrie corrèle avec la proportion de coût en temps (**r=−0,64, p=0,003**) — https://bio.staern.li/pdf/coslovsky2009jib.pdf
- **Orientation de l'araignée liée à sa taille** (*C. confusa*, régression logistique ordinale χ²=19,50, p<0,001) : petites → tête en haut, moyennes → de côté, grandes → tête en bas. Modèle biomécanique : **longueur de corps optimale pour la montée = 7,4 mm**.

### 5.4 Quand l'asymétrie est-elle décidée ? Pendant l'AUXILIAIRE

Expérience de basculement, Zschokke 2011 — https://bio.staern.li/pdf/zschokke2011joa.pdf :
- Toile couchée **pendant la spirale auxiliaire** : asymétrie **Δ=+0,085, P=0,0061** ; excentricité de l'auxiliaire **Δ=0,115, P<0,0001** ; excentricité de la collante **Δ=0,106, P<0,0001**.
- Toile couchée **pendant la spirale collante seulement** : asymétrie **Δ=−0,057, ns (P=0,076)**, mais **asymétrie d'espacement Δ=−0,093, P=0,0002**.
- **Chaîne causale reconstruite (LISREL, χ²=103,5, P<0,0001)** : orientation pendant l'auxiliaire → excentricité auxiliaire (**0,115**) → excentricité collante (**0,921**) et asymétrie de toile (**0,800**) → ratio de spirale (**0,545**) → asymétrie d'espacement (**1,007**).
- **Traduction en pipeline 3D : la FORME asymétrique est fixée tôt (auxiliaire), l'ESPACEMENT asymétrique est fixé tard (spirale collante).**

**Microgravité (ISS, *Trichonephila clavipes*)** : 31 toiles construites lumières éteintes sont **toutes à peu près symétriques** ; lumières allumées, certaines retrouvent l'asymétrie terrestre (la lumière sert de repère de substitution). Régularité : **2,77 (SD 1,41)** en apesanteur contre **5,56 (SD 1,34)** au sol (n=47+47) — https://pmc.ncbi.nlm.nih.gov/articles/PMC7716925/

### 5.5 Les ANGLES entre rayons — la règle indépendante

**Zschokke & Nakata 2015**, 93 araignées / 191 toiles de *Cyclosa* — https://bio.staern.li/pdf/zschokke2015bjls.pdf :

| Classe de toile | Angle quadrant **HAUT** | Angle quadrant **BAS** | ratio |
|---|---|---|---|
| Bas plus grand (57 toiles) | **9,93°** | **7,19°** | **1,38** |
| Extensions similaires (47 toiles) | **8,80°** | **6,59°** | **1,34** |
| Haut plus grand (43 toiles) | **7,89°** | **6,37°** | **1,24** |

**Point capital** : même dans les toiles **verticalement symétriques**, les angles restent plus petits en bas (**8,80° vs 6,59°**). Régression **asymétrie angulaire = 0,14 − 0,20 × asymétrie d'extension** (r²=0,11, P=0,0011), avec **ordonnée à l'origine P<0,0001** : l'asymétrie angulaire **survit à une extension nulle**. En horizontal l'ordonnée à l'origine devient **ns (P=0,24)** — la règle n'existe que dans l'axe vertical. Les angles sont plus grands en haut chez **92 des 93 araignées** et dans **189 des 191 toiles**.

**Angles dérivés du comptage par quadrant chez *A. diadematus*** (Table 1, ap Rhisiart & Vollrath 1994 — https://core.ac.uk/download/85215251.pdf) : nord **6,8 ± 0,1** rayons, ouest 7,7, est 7,9, sud **10,7 ± 0,16** (ANOVA **F₃,₇₇₂=199,88, P<0,001**) ⇒ **13,2° en haut contre 8,4° en bas**, ratio **1,57**. Comptage cumulé de Vollrath et al. 2002 sur 30 toiles : nord **113**, sud **226**, est 185, ouest 196 — soit **exactement 2 rayons en bas pour 1 en haut**, ⇒ **23,9° au nord contre 12,0° au sud**.

**Explication publiée** : *hypothèse de la longueur de segment idéale* (Zschokke 2002) — l'araignée vise une longueur constante de fil collant entre deux rayons ; là où le rayon est long (en bas), il faut un angle plus petit. Complément (Eberhard 2014) : les mailles étant plus étroites en bas, un angle plus petit évite que deux boucles voisines se touchent et fusionnent.

### 5.6 La règle de pose, et le rayon de sortie

**Vollrath et al. 2002, p. 110** — https://european-arachnology.org/esa/wp-content/uploads/2015/08/107-116_Vollrath.pdf — description opératoire complète : l'araignée tourne autour du moyeu, mesure l'angle entre deux rayons voisins **avec ses pattes I et II du côté extérieur** ; si l'angle dépasse la consigne, elle grimpe **toujours sur le rayon le plus HAUT des deux** (par rapport à la gravité), marche sur le cadre, attache en un point X, rembobine sa traîne et **revient en ligne droite** au moyeu. **C'est le point X sur le cadre qui détermine l'angle au moyeu.**

**Règle du rayon de sortie supérieur — vérifiée sur 14 genres** : *« toutes nos araignées, à la remarquable exception de* Hyptiotes paradoxus*, ont toujours utilisé le rayon supérieur comme rayon de sortie »*, avec **seulement 2 exceptions isolées** (une chez *Argiope lobata*, une chez *Zilla diodia*) sur des centaines d'observations — https://bio.staern.li/pdf/zschokke1995eje.pdf. Chez *A. diadematus*, **n=731 rayons**, aucune exception ; chez *Uloborus*, **80 % de 447 rayons** — https://bio.staern.li/pdf/zschokke1995pb.pdf

**Ordre de construction** — « coefficient de construction de rayon » (rang/total, n=30 toiles) : **nord 0,41 · est 0,41 · ouest 0,46 · sud 0,71** (F=59,32, P<0,0001). **Les rayons du bas sont posés en dernier.**

**Ce n'est PAS la tension qui guide.** Distordre le cadre à 30° ne modifie pas le placement (F=0,30, P=0,59) ; l'araignée remplace un rayon coupé **à 1,9 ± 1,5 mm près** (N=7 toiles, n=38 coupes). Mécanisme proposé : **intégration de trajet (path integration)** — décaler expérimentalement le rayon de sortie de quelques mm change l'angle du nouveau rayon de façon prévisible. Distorsion extrême (cadre cisaillé de 30°) : certains rayons **étirés de 20 %**, d'autres **relâchés au point de pendre** ; l'araignée s'interrompt en moyenne **5 minutes (4'52" ± 4'65")** et ne pose qu'**un seul rayon de renfort**.

### 5.7 Rayons qui bifurquent

**Cadres secondaires / rayons en Y** — Soler & Zaera 2016, *Sci. Rep.* 6:31265 — https://www.nature.com/articles/srep31265 : *Araneus* **9/11** toiles · *Cyclosa* **8/8** · *Zygiella* **13/14** · *Argiope* **4/4** · *Nephila* **5/5**. Deux règles générales : l'araignée évite (i) de connecter un rayon au cadre **dans l'alignement d'un point d'ancrage**, (ii) de faire se rejoindre les extrémités de deux segments contigus du cadre secondaire. *Uloborus diversus*, qui n'a pas de cadre secondaire, **courbe ses rayons** pour éviter l'alignement.

**Anomalies quantifiées chez *Zygiella x-notata*, n=127 toiles** — http://www.eje.cz/doi/10.14411/eje.2013.065.pdf :

| Type | Total | % anomalies | % de toiles | moy./toile |
|---|---|---|---|---|
| **Rayon dévié** (>5° d'une droite) | 306 | 6,4 % | **84 %** | 2,6 (2,0) |
| **Rayon en Y** (se scinde en 2 avant le cadre) | 102 | 2,1 % | **45 %** | 1,6 (0,1) |
| **Rayon surnuméraire** (ne part pas du centre) | 21 | 0,4 % | **15 %** | 1,1 (0,3) |

Total anomalies : **35 ± 1 par toile** (5 à 98), dont **3,1** sur les rayons et **21,4** sur la spirale. **Mettez-en dans votre générateur : une toile parfaite est fausse.**

**Rayons subsidiaires** : chez *Nephila clavipes*, **plus nombreux que les rayons normaux**, attachés à un autre rayon ou à la spirale auxiliaire (qui **reste dans la toile finie**) ; typiquement les rayons **montants** sont normaux, une grande partie des rayons **descendants** sont subsidiaires — https://bio.staern.li/pdf/zschokke1995eje.pdf. Eberhard 1990 distingue **9 types** de construction de cadre chez cette espèce.

**Rayons doublés** : sur toute leur longueur au-dessus du moyeu chez ***Caerostris darwini*** ; partiellement chez ***Zilla diodia***, qui construit ses rayons **simple brin près du centre (tensions faibles) et double brin en périphérie** — https://bio.staern.li/pdf/zschokke2002ea.pdf

### 5.8 Le secteur libre de *Zygiella x-notata*

**Aucune valeur angulaire moyenne n'est publiée.** Ce qui existe :
- Modèle géométrique de Venner et al. 2001 — https://academic.oup.com/aesa/article-pdf/94/3/490/40522932/aesame0490.pdf : triangle de base B, hauteur H, sommet au moyeu ; fraction d'aire manquante **a = (B·H/2) / [π·((D_v+D_h)/4)²]** ; toutes les formules de longueur de fil sont multipliées par **(1−a)**. Angle au sommet = **2·arctan((B/2)/H)**.
- Mode de construction : soit **demi-tour au bord du secteur**, soit **retrait des spires de part et d'autre du fil avertisseur en fin de construction** — https://bio.staern.li/pdf/zschokke1995eje.pdf. Le secteur est donc borné par **deux rayons existants** : il couvre un **nombre entier** de secteurs inter-rayons. Avec 22-26 rayons (angle inter-rayon **13,8° à 16,4°**), un secteur libre de 1 à 3 intervalles fait **≈ 14° à 50°** *(valeur dérivée, non mesurée)*.
- **La première toile d'une *Zygiella* fraîchement émergée est plus ronde et n'a PAS de secteur libre.**
- **Fil avertisseur** : **8 à 16 filaments** dans un fil neuf, **4 à 14** dans un fil réparé (9 araignées, n=63) — https://pmc.ncbi.nlm.nih.gov/articles/PMC4707845/
- Cas inversé rare : quand la retraite est **sous** le moyeu, l'araignée décale le moyeu **vers le bas** et il y a **plus de spires au-dessus** (Le Guelte 1967).

---

## 6. LES FILS : DIAMÈTRES ET MÉCANIQUE

### 6.1 Avertissement méthodologique capital

Presque tous les labos publient un **« diamètre de fil hypothétique »** : celui d'une fibre unique fictive dont la section vaut la section totale des **deux brins réels**. La formule publiée est **d_h = √2 × d_brin** — https://www.blackledgelab.com/s/J_Evol_Biol_2010_biomaterial_coevolution.pdf. **Un diamètre publié à 3,5 µm = deux fibrilles de 2,5 µm côte à côte.** La soie majeure ampullacée ET la flagelliforme sont des **paires** ; le cadre peut monter à **3-4 brins** — https://link.springer.com/article/10.1186/s42649-025-00114-6

### 6.2 Diamètres — les cinq soies d'un même individu

*Argiope argentata*, même méthode, même labo — Blackledge & Hayashi 2006, *J Exp Biol* 209:2452 — https://www.blackledgelab.com/s/J_Exper_Biol_2006a-Argiope.pdf

| Soie | Ø publié (paire) | Ø par fibrille |
|---|---|---|
| **Majeure ampullacée** (traîne, rayon, cadre) | **3,5 ± 0,2 µm** | ≈ **2,5 µm** |
| **Tubuliforme / cylindriforme (cocon)** | **5,3 ± 0,8 µm** — la plus grosse | ≈ 3,7 µm |
| **Mineure ampullacée** (spirale auxiliaire) | **1,1 ± 0,7 µm** | ≈ **0,8 µm** |
| **Aciniforme** (emmaillotage) | **0,33 ± 0,02 µm** — la plus fine | fibre unique |
| **Flagelliforme** (cœur de la spirale collante) | **3,5 ± 0,4 µm** | ≈ 2,5 µm |

**Flagelliforme, 16 espèces (section transversale, Opell et al. 2022)** — https://pmc.ncbi.nlm.nih.gov/articles/PMC9327512/ : de **1,3 ± 0,02 µm²** (*Cyclosa turbinata*, *Leucauge venusta*) à **36,2 ± 3,5 µm²** (*Argiope aurantia*), soit des **diamètres équivalents de 1,29 à 6,79 µm** — donc **0,9 à 4,8 µm par fibrille**. **Le cliché « cœur sub-micronique » n'est vrai que pour les petites espèces.** Diaz et al. 2018 donnent **1 à 5 µm** — https://pubmed.ncbi.nlm.nih.gov/29992763/

**Rayon vs spirale : ratio de diamètre 1,43 ± 0,34** (22 espèces, Sensenig et al. 2010) — **pas 3× ni 5×** comme on le lit souvent. Diamètre maximal de fil dans une toile, toutes espèces : **6 µm** ; gamme des radiales **1 à 6 µm** — https://pmc.ncbi.nlm.nih.gov/articles/PMC3385755/

**Cadre vs rayon : aucune source primaire ne donne de ratio chiffré** — c'est une lacune assumée. Le cadre est épaissi non par une fibre plus grosse mais par **passages multiples** (3-4 brins observés).

**Piriforme (disque d'attache)** : fibrille **< 300 nm** chez *Trichonephila clavata* ; **1,2 ± 0,5 µm** chez *Cupiennius salei* ; **1 550 ± 100** fibrilles par disque de charpente, **188 ± 20** par disque gumfoot ; disque **1,8 × 3,3 mm**, épaisseur **5 ± 1 µm**, masse **≈ 5 µg** — https://www.frontiersin.org/journals/materials/articles/10.3389/fmats.2020.00138/full · https://www.blackledgelab.com/s/Nature-Comm_2012_pyriform.pdf

**Nano-structure (pour le close-up)** : fibrilles internes **145 ± 18 nm**, espacement **230 ± 22 nm** — https://www.nature.com/articles/s41598-023-33839-z. Cristallites β **≈ 2 × 5 × 7 nm**, **10-25 %** du volume.

### 6.3 Scaling avec la masse de l'araignée — l'équation à câbler

Ortlepp & Gosline 2008, *J Exp Biol* 211:2832, sur une gamme de masse ×3000 (**0,36 mg → 1,15 g**) chez *A. diadematus* — https://journals.biologists.com/jeb/article/211/17/2832/17736/

> **Section de la traîne : A = 5,86 × 10⁻⁹ · M^0,739** (m², M en kg ; r²=0,90)
> **Force à la rupture : F_max = 11,2 · M^0,786** (N)

| Masse | Ø fil (paire) | Ø fibrille | Force rupture |
|---|---|---|---|
| 0,36 mg | 0,36 µm | 0,25 µm | **96 µN** |
| 10 mg | 1,23 µm | 0,87 µm | 1,4 mN |
| **100 mg** | **2,87 µm** | **2,03 µm** | **8,0 mN** |
| 500 mg | 5,21 µm | 3,68 µm | 30 mN |
| 1,15 g | 7,09 µm | 5,01 µm | **55 mN** |

Contrainte à la rupture **~1,11-1,19 GPa inchangée** sur toute la gamme ; déformation à la rupture **0,249 ± 0,010** inchangée ; module **10,6 ± 1,0 GPa**. **Variabilité intra-toile : les diamètres varient de 100 % dans une seule toile**, et de **≤600 % entre 9 individus** — https://www.blackledgelab.com/s/Invert_Biol_2005.pdf

### 6.4 Mécanique — la table canonique de Gosline 1999

*J Exp Biol* 202:3295, Table 1 — https://journals.biologists.com/jeb/article/202/23/3295/8324/

| Matériau | E (GPa) | σ_max (GPa) | ε_max | Ténacité (MJ/m³) | Hystérésis |
|---|---|---|---|---|---|
| **Soie MA (traîne) *Araneus*** | **10** | **1,1** | **0,27** | **160** | **65 %** |
| **Soie visqueuse (capture) *Araneus*** | **0,003** | **0,5** | **2,7** | **150** | **65 %** |
| Cocon *Bombyx mori* | 7 | 0,6 | 0,18 | 70 | — |
| Tendon (collagène) | 1,5 | 0,15 | 0,12 | 7,5 | 7 % |
| Élastine | 0,001 | 0,002 | 1,5 | 2 | 10 % |
| Caoutchouc synthétique | 0,001 | 0,05 | **8,5** | 100 | — |
| Nylon | 5 | 0,95 | 0,18 | 80 | — |
| **Kevlar 49** | **130** | **3,6** | 0,027 | **50** | — |
| **Acier haute résistance** | **200** | **1,5** | 0,008 | **6** | — |

**La soie de capture est ~10× plus résistante que n'importe quel caoutchouc naturel ou synthétique.** La traîne est **3 à 10× plus tenace** que ses équivalents industriels.

**La soie n'a PAS une valeur unique — elle dépend de la vitesse de déformation** (Table 2, même source) : de **0,0005 s⁻¹** à **20-50 s⁻¹**, E passe de **9,8 à 25-40 GPa**, σ de **0,65 à 2,0-4,0 GPa**, ténacité de **91 à 500-1000 MJ/m³**. À la vitesse d'un impact d'insecte (~1 m/s, rupture en 0,02 s), *« la résistance égale virtuellement celle du Kevlar »*.

**Point de plasticité (yield)** : linéaire jusqu'à **1-2 % de déformation**, puis simple changement de pente pour la MA (net et brutal pour la tubuliforme ; la contrainte **chute** au yield pour l'aciniforme et la mineure ampullacée) — https://www.blackledgelab.com/s/J_Exper_Biol_2006a-Argiope.pdf

**Spirale collante en détail** (mêmes essais) : E **0,001 ± 0,0001 GPa (= 1 MPa)**, σ vraie **534 ± 40 MPa**, ε vraie **1,72 ± 0,05**, ε d'ingénieur **4,65 ± 0,26** — elle casse à **≈ 450 % de sa longueur initiale**, elle est *« trois ordres de grandeur plus souple et un ordre de grandeur plus extensible que les soies sèches »*, et son module de stockage reste **quasi nul jusqu'à 100 % d'allongement**. Chez *A. diadematus* (Köhler & Vollrath 1995) : ε d'ingénieur **4,75 ± 0,16**, ténacité **283 ± 18 MJ/m³**.

**Panorama inter-espèces de la MA** — https://www.blackledgelab.com/s/PLoS_2010.pdf : moyenne toutes espèces **E 8,8 GPa, σ 878 MPa, ε 0,20, ténacité 107 MJ/m³** ; *Nephila clavipes* **13,8 GPa / 1215 MPa / 111** ; record ***Caerostris darwini*** **11,5 GPa / 1652 MPa / ε 0,52 / ténacité 354 (max 520) MJ/m³** — *« plus de 10 fois mieux que le Kevlar »*.

**Module de la flagelliforme, 16 espèces** : de **1,81 ± 0,19 MPa** (*Argiope argentata*) à **98 ± 19,9 MPa** (*Verrucosa arenata*) ; ténacité de **10,3** à **272 MJ/m³** — https://pmc.ncbi.nlm.nih.gov/articles/PMC9327512/. **Facteur 50 entre espèces** — le « 3 MPa » de Gosline n'est qu'un point.

### 6.5 Supercontraction

Gosline 1999 : dans l'eau, la MA **rétrécit de 40-50 %** et sa **raideur initiale chute de trois ordres de grandeur** ; déclenchement dans l'air **au-delà de ~90 % HR** ; fraction cristalline **20-25 % → 10-12 %**, l'eau représentant alors **50 % du volume**.
Boutry & Blackledge 2013 — https://journals.biologists.com/jeb/article/216/19/3606/11706/ : seuil **au-delà de 70 % HR** (désaccord avec Gosline) ; *Argiope trifasciata* libre rétrécit **jusqu'à la moitié de sa longueur** ; **250× moins raide** (et non 1000×) ; **presque 2× plus extensible** ; contrainte de supercontraction générée dans la toile tendue **~70 MPa**. Effet fonctionnel : déflexion de la toile **68 ± 4 mm (sec) → 97 ± 5 mm (humide)**, capture de proies **38 % → 77 %**.
**Point capital pour la 3D** : dans une toile **ancrée**, la soie ne peut pas raccourcir — *« elle développera une petite tension supplémentaire »*. **L'humidité tend la toile, elle ne la rétracte pas.**

### 6.6 Densité et forces de rupture

**Densité : 1,30 à 1,35 g/cm³** — https://pmc.ncbi.nlm.nih.gov/articles/PMC10003856/ · Mortimer 2016 utilise **1325 kg/m³**, Zaera 2014 **1098 kg/m³**. Contre **7,8 g/cm³** pour l'acier : c'est de là que vient le « 5× plus résistant que l'acier à masse égale ».

**Forces d'arrachement d'un disque d'attache sur verre** : *Nephila senegalensis* **35,7 ± 8,9 mN** · *Cupiennius salei* **35,1 ± 16,5** · *Argiope trifasciata* **28,3 ± 9,1** · *Thomisus onustus* **6,7 ± 3,9** · *Parasteatoda* **2,9 ± 1,4** ; sur **Téflon** ça tombe à **3,3-8,0 mN** — https://pmc.ncbi.nlm.nih.gov/articles/PMC4233695/

### 6.7 Désaccords à signaler dans votre doc

1. **Diamètre flagelliforme** : « sub-micronique » n'est vrai que chez *Cyclosa*/*Leucauge* (~0,9 µm/fibrille) ; *Argiope aurantia* est à **~4,8 µm/fibrille**. Fourchette honnête : **0,9-5 µm**.
2. **Diamètre publié = fibre unique ou paire ?** Les sources se contredisent dans leur formulation. Le recoupement CSA/diamètre chez *A. argentata* prouve qu'il s'agit de la **paire**. Divisez par **√2**.
3. **Module de la MA** : 10 GPa (Gosline) · 8,0 ± 0,8 (Blackledge) · 10,6 ± 1,0 (Ortlepp) · 13,8-22 (*Nephila*) · **1,2 ± 0,2 GPa** (Köhler & Vollrath 1995 tel que reporté). Et le module **triple avec la vitesse de déformation**. **Il n'existe pas UNE valeur.**
4. **Supercontraction** : seuil **70 %** vs **90 % HR** ; chute de raideur **×250** vs **×1000**. Le retrait **40-50 %** fait consensus.

---

## 7. GOUTTELETTES DE GLU ET RAYLEIGH-PLATEAU

### 7.1 Correction importante sur la constante que vous citiez

**2π√2 ≈ 8,886, ce n'est PAS 9,02.** Ce sont deux résultats distincts :

- **Critère de Plateau** (instabilité possible) : **λ > 2πR₀ ≈ 6,283 R₀** — https://en.wikipedia.org/wiki/Plateau%E2%80%93Rayleigh_instability
- **Rayleigh 1878, relation de dispersion exacte** (jet non visqueux) : ω² = (σ/ρR₀³)·kR₀·(1−k²R₀²)·I₁(kR₀)/I₀(kR₀). Maximum en **kR₀ ≈ 0,697**, d'où **λ_max = 2πR₀/0,697 = 9,016 R₀ ≈ 9,02 R₀ = 4,508 × diamètre** — https://bulletin.incas.ro/files/patrascu_balan__vol_14_iss_2.pdf (*INCAS Bulletin* 14(2):75-85, 2022) ; confirmé « λₚ ≈ 9,01R₀ » par https://pmc.ncbi.nlm.nih.gov/articles/PMC10372655/
- **2π√2 R = 8,886 R** est l'**approximation d'onde longue** (I₁(x)/I₀(x) ≈ x/2 ⇒ kR₀ = 1/√2 = 0,7071). Écart avec 9,02 : **1,5 %**.
- **Formule visqueuse généralisée, directement utilisable** : **λ / (πD₀) = √(2 + 3√2·Oh)** avec **Oh = η/√(ρσR₀)** (nombre d'Ohnesorge) — même source, éq. (18). À Oh→0 on retombe sur 8,886 R₀. **λ croît comme √Oh.**
- **Tomotika 1935** (fil visqueux dans un autre liquide) : pour β = η_int/η_ext = 1, **λₚ ≈ 11,16 R₀** ; *« les longueurs d'onde plus grandes dominent quand β > 0,3 »*.

**Volume et rayon de goutte** : V = πR²λ ⇒ **r = (3R²λ/4)^(1/3)**. Avec λ = 9,02R : **r = 1,891 R**, diamètre **3,78 R**, soit un ratio diamètre/espacement de **0,42** — des gouttes espacées de **2,4 diamètres**. Si la fibre occupe l'axe : **r = [3(R²−a²)λ/4]^(1/3)** (correction < 1,5 % dans les cas réels).

### 7.2 Le mécanisme, en citations primaires

> *« As an axial line emerges from the flagelliform spigot's tip, it is coated with aggregate gland solution… Plateau–Rayleigh instability causes the aggregate material to quickly form a series of evenly spaced droplets that exhibit a bead on a string (BOAS) morphology. »* — https://journals.biologists.com/jeb/article/221/6/jeb161539/20865/

Le déclencheur est l'absorption d'eau : *« as LMMCs in the aggregate solution attract atmospheric moisture, the cylinder becomes unstable and divides into droplets, within which an adhesive glycoprotein core condenses »* (même source). Confirmé par https://academic.oup.com/jeb/article/35/6/879/7317873 et https://www.blackledgelab.com/s/J-Adhesion-2011.pdf

**Échelle de temps : aucune mesure directe publiée n'a été trouvée.** Toute la littérature primaire dit « quickly », « soon », « instantaneously ». La seule valeur chiffrée croisée (« moins de quelques dizaines de secondes ») vient d'une revue non vérifiable en texte intégral. **À citer avec prudence.**

### 7.3 Géométrie mesurée — 16 espèces

Opell & Hendricks 2009, *J Exp Biol* 212:3026, à ~55 % HR — https://journals.biologists.com/jeb/article/212/18/3026/18383/

| Espèce | Goutte L × l (µm) | Volume (µm³) | **Gouttes/mm** |
|---|---|---|---|
| *Araneus marmoreus* | **67,1 × 50,2** | 79 730 | 3,7 ± 0,3 |
| *Argiope aurantia* | 62,7 × 40,2 | 59 287 | 3,5 ± 0,4 |
| *Araneus bicentenarius* | 50,5 × 41,5 | 43 234 | 6,2 ± 1,7 |
| *Larinioides cornutus* | 41,6 × 30,6 | 20 264 | 6,2 ± 1,4 |
| *Argiope trifasciata* | 41,5 × 25,1 | 12 005 | 6,1 ± 0,6 |
| *Micrathena gracilis* | 30,2 × 23,5 | 7 510 | 9,9 ± 1,9 |
| *Verrucosa arenata* | 27,9 × 22,5 | 6 055 | 7,4 ± 0,8 |
| *Metepeira labyrinthea* | 23,5 × 16,4 | 3 014 | 17,5 ± 3,5 |
| *Leucauge venusta* | 13,8 × 10,0 | 698 | 29,9 ± 2,1 |
| *Cyclosa turbinata* | 13,2 × 9,8 | 608 | 33,4 ± 6,9 |
| *Mangora maculata* | **4,4 × 4,5** | 40 | **67,6 ± 9,0** |

**Enveloppe globale : diamètre 4 → 67 µm ; volume 0,04 → 80 pL ; densité 3,5 → 68 gouttes/mm (soit 35 à 680 par cm).**

**Espacement centre-à-centre MESURÉ** (Opell & Hendricks 2007 — https://journals.biologists.com/jeb/article/210/4/553/17230/) : *A. marmoreus* **264,2 ± 53,8 µm** · *A. trifasciata* **149,9 ± 24,0** · *A. pegnia* **72,4 ± 7,8** · *Metepeira* **46,6 ± 10,6** · *Cyclosa turbinata* **26,2 ± 2,9** · *Leucauge venusta* **20,1 ± 1,7**.

**Ratio espacement / largeur de goutte : de 2 à 6** (les grosses espèces étant les plus espacées) — le chiffre le plus directement exploitable en 3D.

**Les gouttes ne sont PAS sphériques.** Opell modélise le profil comme une **parabole** : **V = 2π·l²·L/15 = 0,419·L·l²** (contre 0,524 pour un ellipsoïde). **Rapport d'aspect L/l ≈ 1,2 à 1,65** (moyenne 1,35) pour les primaires, **1,45 à 2,05** pour les secondaires. Ce sont des **onduloïdes enfilés sur le fil**, pas des billes.

**Alternance grosses / petites : documentée et mesurée.** *« many species feature large droplets with one or more smaller droplets between each pair of larger droplets »*. Volumes secondaires : *Larinioides cornutus* **12 021 ± 6 994 µm³ (= 59 % du primaire !)** · *A. marmoreus* 3 264 · *Argiope aurantia* **1 073 (1,8 % du primaire)** · *Cyclosa* 74 · *Mangora* 13. **Nombre de secondaires par primaire : 1,14 chez *Argiope aurantia*, 0,08 chez *Araneus pegnia*** (certaines espèces n'en ont aucune). Le motif « une grosse + deux petites » **n'est pas attesté quantitativement** ; la variable publiée est un ratio continu.

**Structure interne, 3 couches** : granule d'ancrage opaque (**≈ 15 % du volume**), noyau glycoprotéique (**821 à 8 625 µm³** à 55 % HR ⇒ sphère équivalente **11,6 à 25,4 µm**), couche aqueuse externe qui déborde sur le fil — https://journals.biologists.com/jeb/article/213/2/339/9977/ · https://journals.plos.org/plosone/article?id=10.1371%2Fjournal.pone.0196972

### 7.4 Confrontation modèle / mesures — le point pratique

En posant R = √(V/πλ), le rapport **λ/R observé va de 12 à 35, jamais 9** :

| Espèce | λ/R |
|---|---|
| *Argiope aurantia* | **35** |
| *Argiope trifasciata* | 34 |
| *Araneus marmoreus* | 28 |
| *Larinioides cornutus* | 17 |
| *Leucauge venusta* | 13 |
| *Cyclosa turbinata* | **12** |

**Un simulateur réglé sur le Rayleigh non visqueux (λ = 9R) produira des gouttes 2 à 4× trop rapprochées.** Visez **λ ≈ 15-20 R** pour les petites espèces, **28-35 R** pour *Araneus* / *Argiope*. Le calage inverse via l'éq. (18) donne **Oh ≈ 6**, soit **η ≈ 0,1 Pa·s ≈ 100 cP**. La résolution physique complète passe par la **viscoélasticité** — signature classique du régime *beads-on-a-string* des solutions de polymères, où une grosse goutte se forme vite puis le ligament s'amincit et engendre des **gouttes satellites** : cela explique directement le motif primaire/secondaire.

### 7.5 Le treuil capillaire (« windlass »)

**Elettro, Neukirch, Vollrath & Antkowiak, PNAS 113(22):6143-6147, 2016** — https://arxiv.org/pdf/1605.05293 (texte intégral gratuit) · https://www.pnas.org/doi/abs/10.1073/pnas.1602451113 · version détaillée : https://arxiv.org/pdf/1501.00962

- Fil de capture de *Nephila edulis*, gouttes de **250-300 µm**, volume nanolitre : **tension plateau T_P ≈ 2,5 µN**, courbe complète **0 à 7 µN**.
- **Trois régimes** : I (T > T_P) fibre droite, ressort élastique · **II (T ≈ T_P) fibre flambée puis bobinée DANS la goutte, longueur variable à tension constante** · III (T < T_P) contracté.
- Le fil s'allonge jusqu'à **3× la longueur du fil dans la toile** sans casser et revient **sans hystérésis notable ni affaissement**.
- **Condition critique de bobinage** : **h < (4γ·cosθ / E)^(1/3) · D^(2/3)** (h = rayon de fibre, γ = tension superficielle, θ = angle de contact, E = module, D = diamètre de goutte). La rigidité de flexion variant en **h⁴**, le phénomène est **restreint aux filaments micrométriques** — des gouttes de 80 µm sur un cheveu ne le font pas flamber.
- **Tension plateau** : **T_P = 2πhγ·cosθ − ½·πEh⁴/D²**
- Vérification pour une araignée réelle (γ=0,04 N/m, cosθ=0,9, E=5 MPa, D=100 µm) : **h < 6,6 µm** — le rayon flagelliforme réel (0,6-3,4 µm) satisfait largement la condition. **Le treuil est physiquement inévitable.**
- Analogue synthétique : fil TPU **h = 2,3 ± 0,15 µm**, **E = 17 ± 3 MPa**, θ = 36 ± 7°, goutte de silicone (γ = 21,1 mN/m) de **380 µm** ⇒ **+9000 % de déformation à rupture**, **contraction ×5 sans aucun affaissement**.
- **Fig. 5 verbatim** : *« Bringing the ends together leads the filament to exhibit a characteristic catenary-like sagging shape. This behaviour changes after deposition of a small silicone oil droplet: bringing the extremities closer leaves the filament taut while the excess filament enters the central droplet. »*

**Conséquence directe pour votre outil : une spirale de capture réelle n'a JAMAIS de mou, quelle que soit la déformation de la toile.** Sans cette auto-adaptation, *« single sticky strands would touch during relaxation events and thereby irremediably damage the web »*.

### 7.6 Collant et humidité

- **Force par gouttelette** (*Larinioides cornutus*, sonde de verre, 25 °C / 40 % HR) : de **60 µN à 1 µm/s** à **≈ 400 µN à 100 µm/s** — *« deux ordres de grandeur au-dessus des forces capillaires »* : c'est la **glycoprotéine**, pas le film aqueux, qui colle. Comportement de **solide viscoélastique** — https://www.nature.com/articles/ncomms1019
- **Force par mm de fil** : *Argiope aurantia* **330 ± 30 µN** sur 2 133 µm ⇒ **≈ 155 µN/mm** ; *A. trifasciata* **230 ± 25 µN** ⇒ ≈ 108 µN/mm — https://pmc.ncbi.nlm.nih.gov/articles/PMC6745672/
- **Mécanisme « pont suspendu » et saturation** : les gouttes des bords fournissent le plus d'adhésion, chaque goutte plus intérieure ne contribue que **0,6993** de la précédente ; ***« little adhesion accrues beyond a span of 20 droplets »***. **≈ 50 % de l'énergie de détachement est stockée dans la fibre axiale**, pas dans la glu.
- **Gonflement avec l'humidité** : *L. cornutus* **+100 % de volume entre 10 % et 90 % HR** (soit **+26 % de diamètre**) ; à 90 % HR, **~50 % d'eau** dans le volume ; glu **lavée** de ses LMMC : seulement **~20 %** de prise d'eau — https://www.ncbi.nlm.nih.gov/pmc/articles/PMC5964112/. *Argiope aurantia* **258,5 ± 26,5 %** plus grande à 90 % qu'à 20 % HR, contre **160,6 ± 8,5 %** pour *Neoscona crucifera* — https://journals.biologists.com/jeb/article/216/16/3023/11546/. **Espèces peu hygroscopiques = habitats humides ; très hygroscopiques = habitats secs.**
- **Les LMMC** (composés hygroscopiques) : GABamide, N-acétyltaurine, choline, bétaïne, acide iséthionique, acide cystéique, lysine, sérine, nitrate et dihydrogénophosphate de potassium, pyrrolidone. Ils **ne cristallisent pas** sur une large gamme de pression de vapeur, contrairement à NaCl — https://www.blackledgelab.com/s/J-Adhesion-2011.pdf
- **Viscosité au maximum d'adhésion : 10⁵-10⁶ cP (100-1000 Pa·s), identique chez toutes les espèces** — mais atteinte à une HR différente pour chacune — https://pubs.acs.org/doi/abs/10.1021/acsnano.5b05658

---

## 8. FLÈCHE, TENSION, CATÉNAIRE

### 8.1 Tensions mesurées — la hiérarchie

**Wirth & Barth 1992, *J Comp Physiol A* 171:359-371** — https://link.springer.com/article/10.1007/BF00223966 (résumé intégral) :

> *« In the orb web of* Araneus diadematus *forces decrease from mooring threads to frame threads and radii, a typical ratio being **10:7:1**. The smaller number of radii in the upper than in the lower half of the orb is paralleled by force ratios of **2:1 to 3:1**. »*

Les valeurs sont **identiques chez 4 espèces** (*A. diadematus*, *Zygiella x-notata*, *Nuctenea umbratica*, *Nephila clavipes*) — donc **indépendantes de la géométrie**. Seuls les **fils d'amarrage de *Nephila*** sortent du lot avec des forces anormalement élevées. La prétension radiale **change avec la masse de l'araignée** (F/m similaire entre individus). Les rayons **posés en premier** sont plus tendus.

**Valeurs absolues** — Yavuz et al. 2024, *R. Soc. Open Sci.* 11:240986, Table 1 (modèle EF calé sur profils mesurés) — https://doi.org/10.1098/rsos.240986 :

| Fil | Ø (µm) | **Prétension (µN)** |
|---|---|---|
| **Spirale collante** | 2,40 | **9,7 ± 0,11** |
| **Rayon** | 3,93 | **102,5 ± 15,7** (**122 µN** près du cadre) |
| **Cadre** | 7,23 | **962,5 ± 62,0** |
| **Ancrage** | 8,03 | **1741 ± 0,66** |

Compatible avec **Lin & Sobek 1998 : 132 µN (rayons), 10 µN (spirales)**. Ratio du modèle **17 : 9,4 : 1**, à comparer au **10 : 7 : 1** mesuré. **Retenez : rayon = 1, cadre ≈ 7 à 10, amarrage ≈ 10 à 17 ; spirale ≈ 1/10 du rayon.**

**En contrainte** : prétension moyenne mesurée par ondes transverses sur 3 toiles : **29, 46 et 40 MPa** — https://pmc.ncbi.nlm.nih.gov/articles/PMC5046944/. Rapportée à une rupture de 1,1 GPa, **la prétension vaut 1 à 4 % de la rupture** (0,19 % pour la spirale, 0,77 % pour le rayon, 2,1 % pour le cadre, 3,1 % pour l'ancrage). **La toile est très loin de son point de rupture au repos.**

**Gradient le long d'un rayon** : *« les tensions de chaque boucle de spirale produisent une force centripète sur les rayons, d'où une augmentation de tension le long de chaque rayon du centre vers la périphérie »* (Wirth & Barth 1992, cité par https://bio.staern.li/pdf/zschokke2002ea.pdf). *Zilla diodia* adapte : rayons **simple brin près du centre, double brin en périphérie**.

**Paramètres matériaux des modèles EF** (Zaera 2014 — https://pmc.ncbi.nlm.nih.gov/articles/PMC4233696/) :

| | Amarrage + cadre | Rayon | Spirale visqueuse |
|---|---|---|---|
| Diamètre | **5,0 µm** | **3,5 µm** | **2,3 µm** |
| E | **12,0 GPa** | **12,0 GPa** | **1,2 GPa** |
| Limite d'élasticité | **220 MPa** | **190 MPa** | **30 MPa** |
| ε rupture | **0,27** | **0,27** | **2,5 (250 %)** |

### 8.2 Caténaire — les formules

**Chaînette exacte** — https://en.wikipedia.org/wiki/Catenary :
```
y = a · cosh(x/a)        avec  a = H / w
```
H = composante horizontale de la tension (**constante sur toute la corde**), w = poids par unité de longueur (N/m). Tension locale **T = w·y = w·a·cosh(x/a)** — **maximale aux ancrages**. Longueur d'arc **s = a·sinh(x/a)**.

**Approximation parabolique (faible flèche)** :
```
d = w·L² / (8·H)        ⟺        H = w·L² / (8·d)
```
Validité : la parabole est *« presque exacte quand l'élévation est inférieure à 45° »* (Galilée) ; en pratique **d/L < 1/8 (12,5 %)**, et sous **10 %** l'écart est inférieur au pour-cent.

**Longueur de câble vs portée** :
```
S ≈ L · (1 + 8d² / (3L²))      soit   S − L ≈ 8d² / (3L)
```
**Conséquence visuelle capitale : 1 % de mou (S/L = 1,01) → d/L = 6,1 %.** Un mou minuscule produit une flèche très visible.

### 8.3 La gravité propre est-elle visible sur un fil de soie ? NON

Masse linéique à ρ = 1300 kg/m³ :

| Ø | **Masse linéique** | **Poids linéique** |
|---|---|---|
| **1 µm** | **1,02 µg/m** | **10,0 nN/m** |
| 2,4 µm (spirale) | 5,88 µg/m | 57,7 nN/m |
| **3 µm** | **9,19 µg/m** | **90,2 nN/m** |
| 3,93 µm (rayon) | 15,77 µg/m | 154,7 nN/m |
| **5 µm** | **25,5 µg/m** | **250,4 nN/m** |
| 7,23 µm (cadre) | 53,4 µg/m | 523,6 nN/m |

**Paramètre de chaînette a = H/w pour une fibre de 3 µm** : **11 m** à H=1 µN · **111 m** à 10 µN · **1 109 m** à 100 µN · **11 093 m** à 1 mN. **a est ~10 000 fois plus grand que la portée : la chaînette est rigoureusement une droite à l'écran.**

**Flèche gravitationnelle d'un fil de 3 µm** :

| Portée | H = 1 µN | H = 10 µN | **H = 100 µN (rayon)** |
|---|---|---|---|
| 10 cm | 0,113 mm | 0,011 mm | **0,0011 mm** |
| 30 cm | 1,01 mm | 0,101 mm | **0,010 mm** |
| **1 m** | 11,3 mm | 1,13 mm | **0,113 mm (d/L = 1,1×10⁻⁴)** |

**Un rayon de 3 µm tendu à 100 µN sur 1 m entier fléchit de 0,11 mm — un pixel sur une image de 9000 px.** Le poids total du fil (0,09 µN/m) vaut **0,09 % de sa prétension**. **Ne simulez pas de chaînette gravitationnelle sur la soie sèche : ce serait physiquement faux.**

### 8.4 Ce qui courbe VRAIMENT les fils : les charges ponctuelles

Poids d'une goutte d'eau : **50 µm → 0,0006 µN** · **250-300 µm (taille des gouttes de colle) → 0,080-0,139 µN** · **500 µm → 0,64 µN** · **1 mm (rosée) → 5,14 µN**.

**Une seule goutte de rosée de 1 mm pèse 5 µN = 5 % de la prétension d'un rayon et 53 % de celle d'une spirale ; c'est 57× le poids total d'un mètre de fil de 3 µm.**

Charge linéique de rosée (gouttes de 500 µm tous les 2 mm) : **321 µN/m**, soit **×3 560** le poids propre. Flèche résultante :

| Portée | H = 3 µN | H = 10 µN | **H = 100 µN** |
|---|---|---|---|
| 5 cm | 33 mm (d/L=0,67) | 10 mm (0,20) | **1,0 mm (0,020)** |
| **10 cm** | 134 mm — vraie chaînette profonde | 40 mm (0,40) | **4,0 mm (0,040)** |
| 20 cm | festons | 161 mm (0,80) | 16 mm (0,080) |

**Règle : la soie sèche est droite, la soie chargée est courbe, le facteur entre les deux est de l'ordre de 3 000.** Un rayon chargé de rosée fléchit de **4 mm sur 10 cm** — parfaitement visible. Une spirale chargée sort du domaine parabolique et forme de **vrais festons**. **Mais rappelez-vous §7.5 : sans rosée, le treuil capillaire garde les segments entre gouttes tendus.** Le mou visible d'une toile réelle vient des **points d'attache** et des **charges ponctuelles**, jamais du poids propre.

### 8.5 Le poids de l'araignée

- Masses : *A. diadematus* juvénile de labo **≈ 20 mg** ; modèles publiés balayant **0 à 150 mg** ; congénères de terrain jusqu'à **800 mg** (*A. bicentenarius*) et **1270 mg** (*A. trifolium*) — https://pmc.ncbi.nlm.nih.gov/articles/PMC3385755/. Ordre de grandeur pour une femelle adulte : **100-400 mg = 1 à 4 mN**.
- **Une araignée de 100 mg pèse 981 µN, soit ~10× la prétension d'UN rayon.**
- **Effet mesuré (le résultat le plus utile)** — https://doi.org/10.1098/rsos.240986 : l'araignée au moyeu **augmente la tension des rayons du HAUT et diminue celle des rayons du BAS**, ce qui **reproduit exactement le ratio 2:1-3:1 de Wirth & Barth**. Sans araignée : rayons uniformes **≈ 125 µN** ; à **490 µN** (50 mg) : 101 → 167 µN en haut ; à **980 µN** (100 mg) : **~300 µN en haut** et **effondrement vers 0 en bas**.
- **Seuil critique, verbatim** : *« If the mass of the spider is higher than **75 mg**, the tension of the bottom radii gets so small that the segments close to the hub **start to sag** and signal transmission to the hub is eliminated completely. »*
- **Enfoncement du moyeu (calculé, EA = 0,133 N, 8 rayons efficaces, toile R=10 cm)** : 20 mg → **0,018 mm** · 50 mg → **0,046 mm** · 100 mg → **0,092 mm** · 150 mg → **0,138 mm**. Le seuil de mou calculé tombe entre 50 et 100 mg — **recoupant exactement le seuil publié de 75 mg**. **Retenez : l'araignée enfonce le moyeu de 0,05 à 0,15 mm — invisible ; ce qui est visible, c'est le MOU qui apparaît dans les rayons inférieurs au-delà de ~75 mg.**
- Amplitudes de déplacement au moyeu sous vibration de proie : **0 à 16 µm** ; variations de tension **0 à 3 µN** ; seuil sensoriel des sensilles en fente : **1,4 à 30 nm**.

### 8.6 Impact de proie — trois ordres de grandeur au-dessus

- Énergies : **mouche ≈ 30 µJ** · **grosse sauterelle ≈ 1100 µJ** ; impacts mesurés **19 à 1707 µJ** — https://pmc.ncbi.nlm.nih.gov/articles/PMC3385755/
- **Répartition de la dissipation** (*A. aurantia*, 1245 µJ) : **rayons 1067 ± 497 µJ (86 %)** · **spirale de capture 60 ± 30 µJ (5 %)** · **traînée aérodynamique 85 ± 13 µJ (7 %)**. **Ce sont les rayons qui arrêtent la proie, pas la spirale.**
- Déformation des rayons à la rupture **≈ 30 %** ; capacité d'amortissement **0,50 ± 0,30**.
- **Course de la toile pendant l'absorption : ≈ 20 cm** ; vitesses de déformation jusqu'à **300 % s⁻¹** ; *« webs never catching any projectile faster than 3,5 m/s »* — https://journals.biologists.com/jeb/article/216/18/3388/11584/
- **Écart entre « araignée posée » (0,1 mm) et « proie qui frappe » (10-20 cm) : trois ordres de grandeur.**
- **Vent** : formule de Morison, **Re entre 10⁻¹ et 10¹**, loi de puissance C_D avec **B = 12,18, m = 0,629** ; **les gouttes de colle ajoutent 30 % de traînée** — https://pmc.ncbi.nlm.nih.gov/articles/PMC4233696/. Observation de terrain : *« in windy conditions in nature webs do flap about without apparently perturbing the spider »*.

---

## FICHE DE VALEURS PAR DÉFAUT — *Araneus diadematus* femelle adulte

| Grandeur | Valeur |
|---|---|
| Diamètre de toile | **14 cm** (66,5 à 210 mm) ; **D = 22,28·L^0,78** |
| Forme | **~13 % plus haute que large** (indice −0,063) |
| Rayons | **32** ; nord 6,8 / est 7,9 / ouest 7,7 / sud 10,7 |
| Angles inter-rayon | **13,2° en haut, 8,4° en bas** (ratio ~1,5) |
| Asymétrie verticale | **rayon bas / rayon haut = 1,3 à 1,4** ; moyeu à **+16 % du rayon vertical** |
| Moyeu / zone libre / spirale | **0-10 % / 10-30 % / 30-100 %** du rayon |
| Pas de spirale | **5,6 mm**, **croissant vers le haut, constant vers le bas**, ±30 % d'un tour à l'autre |
| Demi-tours dans la spirale de capture | **11 ± 6**, concentrés en périphérie |
| Longueur totale de soie / jonctions / masse | **~20 m** / **>1000** / **<0,5 mg** |
| Rayon / cadre | 2 fibrilles de **≈ 2,9 µm** (fil apparent **4 µm**), E ≈ **10 GPa**, σ **1,1 GPa**, ε **25 %**, hystérésis **65 %** |
| Spirale auxiliaire | 2 brins de **≈ 0,8 µm** |
| Spirale de capture | 2 brins de **≈ 2,5 µm**, E ≈ **1-3 MPa**, ε rupture **170-475 %** |
| Gouttes de colle | **Ø 30-70 µm**, **espacement 150-270 µm**, **3,5-6 gouttes/mm**, **λ ≈ 28-35 R** |
| Tensions | spirale **10 µN** · rayon **100-130 µN** · cadre **960 µN** · ancrage **1740 µN** |
| Flèche de gravité propre | **négligeable — modélisez les fils droits** |
| Durée de construction | **≈ 60 min** (rayons 16 / auxiliaire 3 / capture 45) |

---

## RÉSERVES ET LACUNES ASSUMÉES

1. **Aucune mesure de temps de fragmentation Rayleigh-Plateau publiée** dans la littérature primaire.
2. **Aucune valeur angulaire moyenne publiée pour le secteur libre de *Zygiella*** — seulement le modèle géométrique de Venner et al. Les 14-50° donnés sont **dérivés**.
3. **Aucune source primaire ne donne un ratio de diamètre cadre/rayon** ; seul le ratio **rayon/spirale = 1,43 ± 0,34** existe.
4. Les valeurs absolues en µN du corps de **Wirth & Barth 1992 restent inaccessibles** (article payant) ; tous les ratios viennent du résumé intégral, les µN absolus de Lin & Sobek 1998 et Yavuz 2024.
5. **Aucune mesure publiée du gradient continu asymétrie ↔ angle d'inclinaison de la toile** — les données sont binaires (vertical/horizontal).
6. Les valeurs « gouttes/mm » divergent entre publications d'un même groupe (*C. turbinata* : 27,5 en 2007 vs 33,4 en 2009). **Prenez des fourchettes, pas des points.**
7. **Correction à votre énoncé** : 2π√2 ≈ **8,886**, pas 9,02 — ce sont deux résultats différents (approximation d'onde longue vs solution de Bessel exacte), écart 1,5 %.
8. Le budget de 200 recherches web de la session a été épuisé ; les sources canoniques Eggers & Villermaux 2008 et Quéré 1999 (cas spécifique « film sur fibre », par opposition au jet libre) n'ont pas pu être atteintes.
