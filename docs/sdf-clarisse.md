# Modéliser en champ de distance dans Clarisse — MagicaCSG, et la question du GPU

Écrit le 2026-09-08. Étude, pas plan de développement.

Romain : « fais des recherches sur MagicaCSG par exemple, ce serait juste
parfait si on pouvait avoir la même chose dans Clarisse ; autre point important,
ce serait possible d'utiliser la carte graphique pour ce mode ? ça pourrait pas
mal débloquer en puissance. »

Ce document prolonge `csg-clarisse.md`, qui a prouvé le point d'entrée : un
module tiers peut fournir à Clarisse une géométrie sans sommets ni faces,
résolue dans l'intersecteur, et le Scatterer natif l'instancie. Ce qui restait
ouvert était **le passage de l'arithmétique d'intervalles au sphere tracing** —
obligatoire dès qu'il y a mélange doux. C'est fait, et mesuré.

Le plan suit les deux questions. **§1 à §4 : l'état de l'art** — ce que
MagicaCSG fait vraiment, ce que Dreams, Womp, Clayxels et Substance Modeler ont
dû sacrifier, la théorie du sphere tracing, et ce que les moteurs de production
savent faire. **§5 à §8 : Clarisse et le GPU** — la sonde qui rend, ce que le SDK
expose, où la carte graphique sert et où elle ne sert pas, et ce qu'on gagne sur
processeur avant d'y toucher. **§9 à §11 :** recommandation, chiffrage, et le
partage entre ce qui a été vérifié, lu, ou supposé.

---

## 0. La réponse en une page

**Le fait le plus important sur MagicaCSG n'est pas dans sa page d'accueil : il
n'évalue pas son arbre à chaque pixel, il le cuit dans une grille, une par objet,
à résolution réglable au clavier.** C'est la fonction officielle *Independent
Volume Resolution*, c'est pour ça que les glyphes fins « cassent à basse
résolution de volume », et c'est pour ça qu'on conseille de baisser la résolution
avant d'exporter. Dreams fait pareil, Clayxels fait pareil, Substance Modeler
fait pareil. **Cinq équipes indépendantes en quinze ans ont toutes discrétisé,
et personne dans le grand public ne rend l'arbre analytique par pixel.** La
raison est identifiée et elle ne nous concerne pas : le temps réel à 16 ms par
image pendant qu'on sculpte. Clarisse peut se permettre plusieurs secondes, et en
échange il obtient la résolution infinie, la silhouette exacte, et
l'instanciation par millions. **Ce n'est donc pas une copie qu'on ferait.**

**Le mélange doux marche dans Clarisse. Vérifié en rendant.** Une sonde,
`native/sdf_sonde/`, sphere-trace un champ de 64 primitives mélangées par le
`smin` polynomial de Quilez à l'intérieur de `intersect_primitive`. Clarisse
construit le BVH, appelle la marche, reçoit la normale reconstruite par
gradient, ombre le fragment et rend l'image. Aucun sommet n'est alloué. Les
images se refont en une minute avec `run.py` ; celles de cette session étaient
dans un dossier de travail temporaire et n'ont pas été versées au dépôt.

**Le mélange doux coûte 2,5 fois l'intersection analytique.** Sur la même scène,
dans le même moteur : 1,850 s pour une union dure résolue par équation du second
degré, 4,681 s pour le champ mélangé sphere-tracé. Ce n'est pas le facteur 10 à
100 que la littérature laisse craindre — parce que ce facteur décrit un sphere
tracer naïf, et qu'il suffit de ne pas être naïf.

**Le résultat contre-intuitif, et il corrige `csg-clarisse.md`.** Ce document
recommandait d'exposer « une primitive Clarisse par feuille de l'arbre » pour
obtenir le BVH gratuitement. **Avec du mélange doux, c'est trois fois plus
lent** : 13,888 s contre 4,681 s pour une seule primitive globale. La raison est
mesurée : le mélange oblige à élargir chaque boîte du rayon de raccord, les
boîtes se recouvrent, et chaque rayon retrace le champ **13 fois** au lieu d'une.
C'est exactement le mécanisme que Barbier et al. décrivent — le mélange détruit
la localité spatiale — observé ici dans Clarisse.

**Le `smin` polynomial ne casse pas la propriété de distance dans le sens
dangereux.** Mesure directe de `max |∇f|` sur une grille de 4 millions de points
au voisinage de la surface : **1,0005 à k = 0,25**, et **0,9999 à k = 1,0**,
contre **1,0291 pour le `min` dur**. Le champ mélangé est plus régulier que
l'union dure, pas moins. Il **sous-estime** la distance, ce qui est le côté sûr :
on paie des itérations (+33 %), on ne troue rien. Le pas plein reste sûr. La
théorie dit la même chose — le gradient d'un `smin` est l'interpolation des
gradients d'entrée, donc de norme ≤ 1 par convexité — et il est utile que la
mesure et le raisonnement se rejoignent sans s'être vus.

**Le vrai coût du mélange n'est pas la distance, c'est le culling.** Trois
sources indépendantes le disent, dont la nôtre : Alex Evans titre une planche
**« Soft blend breaks ALL THE CULLING »**, Keeter observe que « the many smooth
blends are leading to less useful interval evaluation results », et notre mesure
du découpage ci-dessous en donne le facteur. C'est le point de conception à
retenir, et il n'est pas où on l'attendait.

**Le mode d'échec n'est pas la bande de Mach, c'est le trou.** Dans un shader
temps réel qui colore par le compteur d'itérations, un plafond trop bas donne des
bandes. Dans un moteur qui ne pousse une intersection que si elle a convergé, il
donne **des plaques de surface manquantes**. Rendu à 12 itérations : 16,25 % des
rayons plafonnés, et l'objet se dissout par morceaux à silhouette nette. C'est
un artefact non rattrapable, pas un assombrissement.

**Et réduire le pas ne sécurise rien.** Contre l'intuition : à pas 0,5 il y a
**dix fois plus** de rayons plafonnés qu'à pas 1,0 (1,29 % contre 0,13 %) et le
rendu coûte 2,4 fois plus cher. Le petit pas ne protège pas d'un dépassement, il
fait mordre le plafond d'itérations.

### La carte graphique — la réponse courte

**L'analyse de Romain est juste, et elle l'est de cinq ordres de grandeur.**

| Mesuré | Valeur |
|---|---|
| Rayons par appel à `intersect_primitive` | **1,41 en moyenne, un seul dans 99,79 % des appels** |
| Coût d'un aller-retour GPU bloquant (noyau vide) | **20,28 µs** |
| Coût CPU d'un rayon, 24 threads, AVX2 | **0,030 µs** |
| Seuil de rentabilité d'un lancement | **~680 rayons** juste pour amortir la latence |
| Seuil réel avec transferts | **~262 000 rayons** par lancement |

Un noyau GPU lancé depuis `intersect_primitive` traiterait **1,41 rayon pour
20 µs**, là où le processeur les fait en 0,042 µs — **483 fois moins cher**. Et
il manque **cinq ordres de grandeur de rayons** pour que le rapport s'inverse.
Ce serait pire encore en pratique, parce que 24 threads de rendu se
disputeraient un seul contexte CUDA et qu'une synchronisation en bloquerait un
entier à chaque appel.

**Mais il y a une porte GPU, et elle n'est ni CUDA ni OpenCL.** Elle est
vérifiée à l'édition de liens : `ix_glutils.lib`, livré avec Clarisse, réexporte
**GLEW en entier — 2 735 symboles, jusqu'à OpenGL 4.6**, `glDispatchCompute`,
`glBindImageTexture`, `glMemoryBarrier` et les SSBO compris. Un module tiers s'y
lie et les charge. Et `ModuleGeometry` hérite déjà de
`ModuleGlObject::paint_gl(GlUtilsCtx&)`. **Le chemin pour un aperçu SDF calculé
sur la carte graphique dans le viewport de Clarisse existe.**

Sa limite est nette et vérifiée : `cnode.exe` **n'a aucune dépendance
OpenGL**. Ce chemin n'existe que dans la session interactive. Le rendu final,
lui, reste CPU — ce qui est la bonne nouvelle pour une ferme.

**Le facteur GPU/CPU brut, mesuré deux fois par deux voies indépendantes : 22×.**
Une RTX 3090 évalue 201 milliards de distances de primitive par seconde ; le
i9-7920X en AVX2 sur 24 threads en fait 9,1 milliards. Mais **sur une image
entière le facteur net tombe à 3,1×**, parce que le CPU rattrape l'essentiel par
un meilleur élagage. Le GPU n'est pas un multiplicateur de puissance générique :
il gagne là où le travail est massif, uniforme et lancé en un seul coup —
c'est-à-dire l'aperçu et le bake, pas le rayon.

### Ce qu'on recommande

1. **Un nœud SDF utile existe à une semaine.** Sphere tracing, six primitives,
   union / intersection / différence **et mélange doux**, primitive unique, pas
   plein, plafond adaptatif.
2. **Ne pas découper en une primitive par feuille tant qu'il y a du mélange** —
   c'est mesuré comme un piège, pas comme une optimisation.
3. **Le GPU vient après, et par OpenGL compute, pour l'aperçu seul.** Deux à
   quatre semaines, et ça ne touche pas le rendu final.
4. **CUDA : non.** Mono-fabricant, absent des fermes, et le seul endroit où il
   servirait — le bake d'une grille — est déjà tenu par 24 threads en AVX2 à
   un facteur 22 près, pour un chantier dix fois moins cher.

Le coût honnête d'un « MagicaCSG dans Clarisse » complet, avec outils
interactifs et matériaux : **plusieurs mois**. Le coût d'un nœud qui fait déjà
quelque chose qu'aucun autre moteur ne fait — du SDF mélangé, instancié un
million de fois par le Scatterer — est d'une semaine.

**Et une correction à `csg-clarisse.md` §4.3**, qui écrivait qu'aucun moteur de
production ne sphere-trace un arbre CSG analytique : **Octane le fait**, sous le
nom de Vectron, avec une empreinte VRAM nulle et des opérateurs *smooth / round /
stair* à rayon réglable. La voie proposée ici a donc un précédent commercial qui
fonctionne. Ce n'est pas un pari architectural.

---

## 1. MagicaCSG — ce qu'il fait vraiment

### 1.1 Le fait qui change la conception, et qu'on n'attendait pas

**MagicaCSG n'évalue pas son arbre de primitives à chaque pixel. Il le cuit dans
une grille, une par objet, à résolution réglable.**

La page officielle liste comme fonction majeure **« Independent Volume
Resolution »**, présente depuis la 0.1.0 y compris dans la démo gratuite, avec
dans le changelog : *« Independent Resolution for each volume (Press ⊕ / ⊖) »*.
Trois faits convergent vers la même lecture :

1. chaque objet a **sa propre résolution**, réglée au clavier ;
2. ephtracy écrit lui-même, à propos des polices SDF, qu'il faut préférer des
   graisses semi-grasses **« sinon les glyphes cassent sur leurs arêtes fines à
   basse résolution de volume »** ;
3. l'export passe par marching cubes, et les artistes recommandent de **baisser
   la résolution avant d'exporter** — ce qui n'aurait aucun sens sur un champ
   analytique.

C'est architecturalement la même chose que Dreams : des primitives analytiques
évaluées dans un volume discrétisé. Ephtracy n'a jamais rien publié sur son
moteur ; la résolution en voxels n'est nulle part.

⚠️ **Piège de source à connaître.** Plusieurs résumés en ligne affirment que
« MagicaCSG stocke ses champs de distance en textures volumiques et les rend par
sphere tracing ». Cette phrase vient en réalité d'un article d'Unreal Engine de
Ryan Brucks, pas d'ephtracy. Elle ne documente pas MagicaCSG.

**Ce que ça implique directement pour nous** : ce que Romain admire dans
MagicaCSG — la fluidité, le mélange, le rendu immédiat — est obtenu **au prix
d'une résolution finie par objet**. La voie analytique de Clarisse n'a pas cette
limite. Ce n'est donc pas une copie qu'on ferait, c'est autre chose, et sur ce
point précis c'est mieux.

### 1.2 Le modèle de données

- Un **stroke** est la primitive. C'est le mot d'ephtracy pour ce que Dreams
  appelle un *edit*.
- Un **brush** n'est **pas** un objet, c'est un **outil** : le *Stroke brush* de
  la 0.0.4 est « a tool (on the top bar) to add new strokes by clicking on the
  surface ». Changer ses réglages n'affecte que le prochain stroke posé.
- **Hiérarchie** : scène → objets (chacun son volume, sa résolution, instanciable
  depuis la 0.1.0) → calques → strokes. **L'ordre de la liste de strokes est
  signifiant** : un stroke agit sur ce qui le précède. Les groupes booléens
  (*Subgroup Boolean*) ne sont arrivés qu'en 0.5.0, les groupes imbriqués en
  0.7.5.

C'est donc **une liste ordonnée avec des calques**, enrichie tardivement d'une
hiérarchie — pas un arbre CSG général dès le départ. Le même choix que Dreams,
et pour la même raison : une liste se cull, un arbre profond se compose mal.

**Les primitives** : sphère, sphère en norme L (paramètre *Power* →
superellipsoïde), ellipsoïde haute qualité, boîte, cylindre, ovale, cône (avec
un mode avancé iso et courbe), tore, prisme, étoile, polygone, triangle,
tétraèdre, trapèze, losange, quadrilatère à quatre points, lignes, et **splines
de Bézier 2D/3D jusqu'à quatre points de contrôle** avec *taper*. Puis, dans les
versions Patreon : hélice, sweep le long d'une spline, glyphes de texte, SVG,
famille de cercles, SDF de maillage (prototype), CSG instancié, trim.

### 1.3 Le mélange doux, sa signature

Union, soustraction, intersection — plus une opération **Replace** qui ne change
que la couleur, avec une opacité.

Les modes de raccord sont **fillet lisse, Groove, Chamfer et Avoid**, plus une
continuité C0/C1/C2 sur les profils de sweep. Le mode Groove applique le booléen
« dans une certaine épaisseur autour de la surface ».

**Le point de conception à retenir : le mélange est un attribut *par stroke*, pas
un réglage global.** Chaque primitive porte son propre rayon et son propre mode
de raccord. C'est ce qui donne le contrôle local — un raccord dur ici, un congé
gras là — et c'est exactement ce qu'il faut reproduire.

L'unité du rayon de mélange et la sémantique exacte d'« Avoid » ne sont
documentées que sur Patreon. **Pas trouvé.**

### 1.4 Le rendu et les matériaux

**OpenGL 4.6**, Windows 64 bits, 2 Go de VRAM minimum — spécification inchangée
depuis 2021. **Path tracer** avec Open Image Denoise, soleil/ciel ou HDRI,
sortie image fixe et séquences de turntable.

La 0.2.0 a fusionné le viewport et le rendu : on édite, on déplace et on
sélectionne **dans** l'image path-tracée. Le tableau officiel l'appelle
*Interactive SDF based Path Tracer*. Il n'y a donc plus vraiment de « mode rendu
final » séparé, mais un path tracer progressif permanent.

L'algorithme précis — sphere tracing de la grille ou du champ ? — n'est pas
publié. **Pas trouvé.**

**Matériaux** : diffus et métal (metallic/roughness) dès 2021, puis *Dual
Materials* et *Scattering Materials* (donc du SSS) dans les versions Patreon.
Assignation par stroke, via la couleur et l'opacité de l'opération Replace, et
par objet en mode rendu.

### 1.5 Les limites, et celle qui décide

**La limite économique est la plus dure.** Le tableau des fonctions distingue la
démo gratuite de la beta Patreon :

| | Démo | Beta Patreon |
|---|---|---|
| Licence | **non commerciale** | oui |
| Path tracer interactif, instanciation, résolution par volume | oui | oui |
| **Modes Groove / Chamfer / Avoid** | **non** | oui |
| **Matériaux Dual / Scattering** | **non** | oui |
| Sweep, hélice, glyphes, SVG | **non** | oui |
| **Export de maillage** | **non** | **oui** |

**La version gratuite ne peut pas exporter, ni utiliser les raccords avancés, et
n'autorise aucun usage commercial.** Tout ce qui est exploitable est derrière le
Patreon, à partir de 2 $/mois.

**La qualité de l'export**, mesurée par un artiste sur un modèle réel :

- fichier **PLY de 48,9 Mo, 1 018 696 sommets, 509 960 triangles** ;
- après nettoyage sous Blender : *Merge by Distance* retire **761 976 sommets**,
  soit **75 % de doublons** ;
- résultat utilisable après retopologie : 18 000 sommets, 36 700 triangles,
  748 ko.

Autrement dit : **soupe de triangles de marching cubes, non soudée, sans quads,
sans UV, avec la couleur en attribut de sommet**, au format **PLY uniquement**.
Le décimateur de Blender y ouvre des trous ; il faut passer par une retopologie
puis un bake. Baisser la résolution avant export fait passer le nettoyage
« d'heures à secondes ».

**Le nombre maximal de strokes, la taille de scène et les seuils de plantage :
pas trouvé.** Aucun fil de forum ne les documente, ce qui est cohérent avec la
taille de la communauté (voir ci-dessous).

### 1.6 La vitalité du projet, et l'échelle réelle

Vivant mais confidentiel. Dernière version **0.7.6, le 16 août 2026** ; avant,
avril 2026, juillet 2025, août 2024. Deux versions en quatre mois.

Le chiffre qui situe : la démo totalise **2 267 téléchargements**, contre
**494 000** pour les deux builds de MagicaVoxel sur le même dépôt. MagicaCSG
pèse **0,5 %** de l'audience de MagicaVoxel. La meilleure documentation publique
est la trentaine de tutoriels courts de Pixel Fondue. On n'y trouve rien sur les
UV, le texturing, le rigging, l'animation ni l'export avancé.

**Un concurrent direct à connaître** : **SDF Modeler** de Sascha Rode (0.5.3,
juillet 2025) — même architecture, path tracer maison, PLY seul, maillage dense
à retopologier, mais **gratuit, sur Windows, Linux et macOS Apple Silicon**,
OpenGL et Metal, avec des raccords *push / avoid / emboss / deboss*.

---

## 2. Ce que chacun a dû sacrifier

C'est la partie qui apprend le plus, et elle converge vers une seule
observation.

### 2.1 Dreams — le dossier le mieux documenté qui existe

Tout ce qui suit vient du talk d'Alex Evans à SIGGRAPH 2015, *Learning from
Failure*, 145 planches avec les notes du conférencier. *(À noter : le talk
« Umbra to Ubiquity » cherché dans la mission n'existe pas — le talk d'Umbra
Ignite 2015 est le même contenu, Media Molecule le dit sur son propre blog.)*

**Le modèle : une liste, pas un arbre.** Evans est explicite : « we support a
simple **list, not tree** of CSG edits ». Les modèles font **de 1 à
100 000 edits**. Opérations : ajouter, soustraire, ou colorer seulement — plus le
mélange doux.

**Le premier sacrifice, et il est assumé** : ils ont **exclu la déformation de
domaine et tout effet non local** (le flou), « much to the chagrin of z-brush
experienced artists ». C'est le prix payé pour que le culling reste possible.
Retenons-le : c'est un choix de conception, pas une limite technique.

**Leur `soft_min`, et la phrase qui compte** :

```c
soft_min(a, b, r) {
    float e = max(r - abs(a - b), 0);
    return min(a, b) - e*e*0.25f / r;
}
```

C'est, à la normalisation près, **exactement le `smin` polynomial de Quilez** —
celui qu'on a mesuré au §8.4. Sa propriété critique : **il redevient exactement
`min` dès que `|a−b| > r`**, ce qui est précisément ce qui permet de le culler.

Et Evans titre une planche : **« Soft blend breaks ALL THE CULLING »**. Il faut
que le mélange repasse en dur au-delà d'une distance « otherwise you can never
cull either side », et il faut anticiper la quantité de *mélange futur* lors du
culling, parce que le mélange **augmente la portée d'influence** de chaque
primitive.

**C'est exactement ce qu'on a mesuré au §5.4** — boîtes élargies, recouvrement,
travail dupliqué ×13. Media Molecule l'écrivait en 2015 ; on l'a re-rencontré
dans Clarisse en 2026, sans le chercher.

**Leur évaluateur, la seule brique jamais jetée en cinq ans.** Plus de
40 compute shaders, dont des monstres de 3 000 instructions. Raffinement
hiérarchique par blocs de 4×4×4 d'un coup, pour coller aux wavefronts de 64
threads. **Norme L∞ au lieu de L2** — parce que beaucoup de primitives non
uniformes ont un champ bien plus simple en max-norm, et parce qu'une sphère en
max-norm est un cube, donc elle épouse la forme des nœuds de la hiérarchie.
Arithmétique d'intervalles pour du culling supplémentaire.

Les chiffres, sur PS4 :

| Modèle | Edits | Voxels | Temps d'évaluation | Culling |
|---|---:|---:|---:|---:|
| crystals_dad | 8 274 | 5,2 M | 0,091 s | 99,90 % |
| head_lion | 53 976 | 4,7 M | 0,227 s | 99,96 % |
| sphere_woman | 24 407 | 0,85 M | 0,342 s | 99,22 % |

Gamme typique : **600 à 54 000 edits**, donnant **1 à 10 M de voxels de
surface**. La force brute théorique serait de 100 milliards d'évaluations,
« which is too many ». **Le culling mesuré dépasse 99 % partout** — c'est ça qui
fait exister le produit.

**Le cimetière — quatre moteurs jetés en trois ans.**

1. **Les polygones.** Marching cubes en compute : « the meshes are dense and the
   edges are mushy and there are slivers » ; **2 millions de quads pour une
   simple grosse sphère**. Dual contouring : impossible de garder les arêtes
   vives vives et le lisse lisse, arêtes qui ondulent, trous,
   auto-intersections, et une fois corrigées la surface n'est plus manifold —
   « Manifold? Non-Self-Intersecting? **Pick one** :( ».
2. **Le moteur de bricks** (cubes de 8³ raymarchés localement). **Il a servi les
   artistes pendant deux ans.** Evans a ensuite passé **plus d'un an** sur la
   transparence par ordre indépendant, pour rien : **« easily 32× 1080p »**
   pixels non opaques, et une complexité en profondeur qui varie sur **deux
   ordres de grandeur** entre un pixel de fond dur et un pixel rasant. Il note
   que c'est « moralement le même problème que beaucoup d'approches de sphere
   tracing, où les pixels de bord sont beaucoup, beaucoup plus durs ».
3. **Le refinement renderer** en froxels. Magnifique — antialiasing pré-filtré,
   ombres douces, SSS par cone tracing — et **4 à 10 fois trop lent**.
4. **Et le clou dans le cercueil est artistique, pas technique.** Le rendu direct
   du champ « ne laissait rien à l'imagination » ; les designers produisaient ce
   qui ressemblait à « de l'Unreal non texturé, en plus lent ». Après une
   confrontation de plusieurs heures avec le directeur artistique montrant une
   peinture à l'huile en disant « literally that », **début 2014 ils ont tout
   jeté et recommencé**. Seul l'évaluateur a survécu.

**Ce qui a livré : les splats.** Un point par voxel feuille à changement de
signe, domaine **~900³ → ~2 M de points** par sculpture, **un point tient en un
dword** (position, normale et rugosité bit-packées, couleur en DXT1). Le SDF
devient une représentation **intermédiaire** : il ne sert plus qu'à semer les
points. Rendu par `atomic min` 64 bits, **des dizaines de millions de points par
frame**, puis TAA très agressive. Chiffre publié : **28,2 M de splats en
4,38 ms**, soit ~640 millions de points par seconde.

**Les sacrifices, précisément :**

| Domaine | Ce qui a été accepté |
|---|---|
| Transparence | **Purement stochastique**, nettoyée par la TAA. Evans : « still in a fairly noisy/ghosty place. TODO! » |
| Bruit | La frame avant TAA « verges on white noise. **It's terrifying.** » |
| Ombres | **64 shadow maps de 128×128** réparties sur toutes les lumières locales, échantillonnées « quite poorly ». Dans le jeu livré : 64 spots max, dont **16 seulement projettent des ombres**. |
| Réflexions | Environment map. **Aucune réflexion tracée.** |
| GI | Voxelisation binaire à **1 bit par voxel** en cascades ~64³. Le test de GI émissif : « the variance is INSANELY high, so it isn't usable ». |
| Profondeur de champ | **Pas de flou** : on disperse les splats dans un disque écran. « It doesn't quite look like blur, because it isn't — it's literally the objects exploding a little bit ». |
| Modélisation | Pas de déformation de domaine, pas de flou, pas de pull/smear à la ZBrush. |
| Détail | Plafonné par le domaine ~900³. |

**La leçon pour nous, et elle est rassurante** : la quasi-totalité de ces
sacrifices sont des sacrifices de **temps réel à 30 images/seconde sur une
console de 2013**. Dans Clarisse, on rend en secondes ou en minutes, avec le path
tracer natif, ses vraies ombres, ses vraies réflexions et son vrai flou. **On
n'hérite d'aucun de ces compromis.** Le seul qui nous concerne est celui du
mélange qui casse le culling — mesuré au §5.4 — et celui du coût des rayons
rasants — mesuré au §8.5.

### 2.2 Clayxels — Dreams dans Unity, et sa documentation est honnête

Un *clayxel* n'est ni un cube ni un voxel au sens strict : des SDF analytiques
sont évaluées sur une grille grossière raffinée progressivement, et produisent
**un nuage de points**. La FAQ officielle est catégorique — Clayxels **« ne fait
pas de ray marching lourd »**, il splatte un nuage léger autour de la surface.
**C'est exactement l'architecture Dreams.**

Trois modes : *PolySplats* (triangles face-caméra), *MicroVoxelSplats* (le plus
léger, **n'utilise aucun polygone**, résolution de rendu indépendante du
viewport), *SmoothMesh* (maillage temps réel, le plus lourd).

**Structure** : un `ClayContainer` contient des `ClayObjects` hiérarchisés, qui
**s'influencent de bas en haut**. Encore une liste ordonnée, encore une valeur
de mélange par objet.

**Ce que sa documentation avoue, et c'est utile** : le paramètre décisif de
performance n'est pas une résolution abstraite, c'est **le volume englobant du
container**. Les conseils officiels sont de désactiver l'auto-bounds, de serrer
les boîtes, et de **découper une sculpture complexe en plusieurs containers**. Le
symptôme de dépassement est documenté noir sur blanc : « Clayxels crashing unity
with a **gpu timeout error** » → baisser la taille maximale de boîte à 2 ou 1.

Plateformes : Windows et macOS M1 seulement. **Pas de mobile** — « the compute
shader is not mobile compatible yet and even if eventually it might get there,
it will most likely be too slow ». Pas de WebGL. Le contournement est de figer
en maillage, ce qui supprime tout le runtime. Export : FBX après freeze.

### 2.3 Womp, et Substance 3D Modeler

**Womp** (navigateur, beta mai 2024, 4,7 M$ levés). **Sa technologie de rendu
n'est documentée nulle part** — aucun blog technique, aucun talk, aucune mention
publique de WebGL, WebGPU, ray marching ou SDF. La communication parle d'un
moteur « 100 % cloud » permettant de rendre verre et métal sur téléphone, ce qui
**suggère** un viewport local et un rendu final déporté. C'est une inférence, pas
une source. Limites documentées : **16 lumières**, rendu **4K** en image fixe,
HD en vidéo. Export OBJ, FBX, USD, glTF, Collada, STL, PLY — la qualité du
maillage n'est documentée nulle part. Modèle : gratuit à vie limité, Pro à
~10-13 $/mois pour le dessin libre, les formes avancées, **l'intersection
booléenne** et la 4K.

**Substance 3D Modeler** (ex-Oculus Medium, racheté par Adobe en 2019). La doc
Adobe est nette : **des SDF stockés dans une grille 3D de millions de voxels**,
chaque voxel portant la distance signée. « The amount of detail that can be
stored in a certain space depends on the number of voxels available » — avec
l'analogie explicite d'une image de 4 pixels. **La résolution se règle par
calque.** Le comportement en mémoire est documenté et brutal : VRAM d'abord,
puis RAM avec perte de performance, puis fichier d'échange, puis **plantage par
manque de mémoire**. La pile d'annulation stocke des **deltas** de voxels
modifiés. Valeurs numériques, limites de calques, pipeline d'export : **pas
trouvé**, la page Adobe refuse les requêtes automatisées.

### 2.4 La convergence, et c'est le résultat de tout l'état de l'art

**MagicaCSG, Dreams, Clayxels, Substance Modeler et SDF Modeler font tous la
même chose : primitives SDF analytiques → évaluation dans un volume discrétisé à
résolution réglable → nuage de points splatté ou maillage.**

**Personne, dans le grand public, ne rend l'arbre analytique directement par
pixel.** Cinq équipes indépendantes, sur quinze ans, ont convergé vers la même
architecture.

Il y a deux façons de lire ça, et il faut les tenir ensemble.

*La lecture prudente* : si tout le monde discrétise, c'est probablement qu'il y a
une bonne raison. Elle existe et elle est identifiée — **le temps réel**. Il faut
une image toutes les 16 ms pendant qu'on sculpte ; une grille se met à jour
incrémentalement et se rend en temps borné, un arbre analytique se paie à chaque
pixel de chaque image.

*La lecture qui nous concerne* : **cette raison ne s'applique pas à nous.** On ne
sculpte pas au geste, on rend un plan. Clarisse peut se permettre plusieurs
secondes par image, et en échange il obtient ce qu'aucun des cinq n'a : une
résolution infinie, une silhouette exacte, une mémoire indépendante de l'échelle,
et — c'est le point — une géométrie **instanciable un million de fois par le
Scatterer sans matérialiser un seul sommet**.

C'est là qu'est la valeur du croisement, et pas ailleurs.

---

## 3. La théorie, et deux idées reçues à corriger

### 3.1 Hart 1996, les trois définitions qui comptent

- **Borne de distance signée** : `|f(x)| ≤ d(x, surface)`. On n'a **jamais**
  besoin de la vraie distance, seulement d'une **sous-estimation**.
- **Lipschitz** : `|f(x) − f(y)| ≤ λ‖x − y‖`. La dérivée n'a pas besoin
  d'exister — c'est ce qui autorise `min` et `max`.
- **Théorème 1** : si `λ ≥ Lip f`, alors `f/λ` est une borne valide. Donc
  `λ = 1` équivaut à un champ de distance exact.
- **Théorème 2** : la convergence est **linéaire**, pas quadratique. Le sphere
  tracing paie sa robustesse en vitesse.
- **Théorème 3** : `min` (union) est exact, le complément est exact, mais
  **`max` (intersection, soustraction) n'est qu'une borne inférieure**. Dès qu'il
  y a un `max` dans l'arbre, on n'est déjà plus sur une distance exacte.

Et le critère d'arrêt `ε` **est la précision géométrique voulue**, pas un
paramètre de sécurité. Le plafond d'itérations est **le seul détecteur de
non-convergence** : un rayon rasant ne diverge pas, il stagne.

### 3.2 Première idée reçue : « le mélange doux casse le sphere tracing »

**Faux pour la formule qu'on utilise, et on l'a mesuré au §8.4.**

L'argument de Quilez est propre : le gradient du `smin` polynomial est
**l'interpolation linéaire des gradients des champs d'entrée** ; si ceux-ci sont
unitaires, l'interpolée a une norme `≤ 1` par convexité. Donc `Lip ≤ 1`, donc le
champ reste une borne valide. Il l'écrit lui-même : ces fonctions « ne
sur-estimeront jamais la vraie distance, on peut donc les utiliser sans risque
pour le raymarching et la détection de collision ».

**Notre mesure indépendante le confirme** : `max |∇f| = 1,0005` à k = 0,25.

L'ampleur de l'erreur se déduit du code publié : Quilez a normalisé toutes ses
variantes pour que **l'écart maximal à `min(a,b)` vaille exactement `k`**,
atteint sur l'axe médian du raccord. C'est un **décalage constant**, pas une
fraction — donc un coût nul hors de la zone de raccord.

**Le vrai piège n'est pas la sous-estimation, c'est la non-rigidité.** Les
variantes exponentielle et « root » ne retournent **jamais** exactement `a` ni
`b` : leur support est infini, elles distordent tout l'espace, et on paie le
ralentissement sur la totalité du rayon. La quadratique s'annule pour
`|a−b| ≥ 4k`, la cubique pour `6k`. **Il faut une variante à support compact**,
et c'est aussi ce qui permet de la culler — c'est précisément la propriété que
Media Molecule met en avant dans son `soft_min`.

Deux réserves à garder : la garantie suppose des opérandes **exactes** (si l'une
vient d'un twist ou d'un scale, la composition hérite du pire), et **le smooth
*max* n'a pas cette propriété**. Le seul opérateur franchement dangereux de la
liste de Quilez est le *Circular Geometric*, qui sur-estime dans les zones
convexes.

**Ce qui casse réellement la borne**, et il faut le savoir avant d'exposer un
attribut :

- **Le scale non uniforme.** La formulation sûre est
  `sdf(p / s) * min(s.x, s.y, s.z)` — on multiplie par le **plus petit** facteur,
  parce que le pire cas dicte le pas partout. Hart chiffre le coût : un
  ellipsoïde 100/100/1 obtenu par scale d'une sphère donne au pire **1 % de la
  vraie distance**, soit ~100× d'itérations. D'où le laconique de Quilez : « Non
  uniform scaling is not possible (while still getting a correct SDF) ».
- **La répétition de domaine par `mod`**, et c'est le cas dangereux : l'approche
  naïve **sur-estime** quand l'instance la plus proche n'est pas dans la tuile
  courante, ce qui produit **des trous qui bougent avec la caméra**. Il faut
  tester le voisinage 2×2×2.
- **Le twist et le bend n'ont aucune constante de Lipschitz sur ℝ³.** Hart le
  démontre : pour tout `λ` on trouve deux points assez loin de l'axe dont
  l'écartement est amplifié davantage. **Aucun facteur de sécurité constant ne
  rend le twist sûr partout** — il faut borner le domaine, et calculer le facteur
  sur le rayon maximal du volume englobant.

Et le rattrapage général, qui est l'ancêtre du segment tracing :
`d ≈ |f(x)| / ‖∇f(x)‖`.

*Note : les facteurs de sécurité empiriques 0,7 à 0,9 qui circulent sur
Shadertoy ne sont justifiés par aucune source primaire. Le seul facteur
rigoureusement fondé est le ×1/2 des fractales, pour l'estimateur de
Hubbard-Douady.*

### 3.3 Deuxième idée reçue : « un rayon coûte 100 itérations »

**Les chiffres publiés, et ils sont bien plus doux :**

| Mesure | Valeur | Source |
|---|---|---|
| **Moyenne réelle, rayons primaires** | **~17 pas/rayon** | Quilez, NVScene 2008, scène *Slisesix* |
| Total pour une image 1280×720 | 50 M évaluations, 60 % primaires / 40 % ombrage | idem |
| Silhouettes et rasants | **> 100 itérations** | Hart 1996, fig. 8 |
| Plafond temps réel raisonnable | **64 à 80** | Keinert 2014 |
| Halver le pas | **double le nombre de pas** | Hart 1996, fig. 13 |

Les valeurs `MAX_STEPS 100` ou `255` qui circulent viennent de modèles
Shadertoy, pas de mesures. **Notre mesure dans Clarisse — 5,94 itérations par
entrée dans l'intersecteur — est du même ordre, et même meilleure**, parce que le
test de boîte fait démarrer le rayon au contact du volume plutôt qu'à la caméra.

### 3.4 Le mode d'échec en rasant, et ce qui le corrige

La convergence étant linéaire, un rayon qui frôle une arête approche
asymptotiquement sans jamais franchir `ε` : il s'arrête **en l'air, derrière
l'objet**. Hart : aux silhouettes « the distance to the surface is only a
fraction of the distance to the ray intersection ». C'est ce qu'on a rendu au
§8.5.

Les parades, par ordre de solidité :

1. **`ε` proportionnel à la distance parcourue** — le mieux fondé. L'erreur en
   coordonnées monde décroît en `1/d`, donc un seuil proportionnel donne une
   erreur constante en espace écran. Keinert le code littéralement :
   `erreur = rayon / t`, comparé au rayon du pixel. **C'est ce qu'il faut mettre
   dans le nœud, et c'est ce que notre sonde n'a pas.**
2. **Garder le meilleur point plutôt que le dernier** : mémoriser le point de
   plus petit rayon de sphère en espace écran, et terminer dès qu'il passe sous
   le demi-pixel.
3. **Une itération de point fixe en post-traitement** : trois itérations
   suffisent généralement à supprimer les artefacts de discontinuité.

*Et une note d'hygiène : colorer par le compteur d'itérations pour simuler de
l'occlusion ambiante est du folklore. Hart et Quilez visualisent ce compteur
pour profiler, pas pour ombrer. La contrepartie honnête, elle, est documentée :
imposer un pas minimum, en échangeant de la précision de silhouette contre un
plafond de coût.*

### 3.5 Les accélérations publiées, et ce qu'elles rapportent vraiment

**Enhanced Sphere Tracing** (Keinert et al., STAG 2014). Sur-relaxation
`δ = f(p)·ω` avec `ω ∈ [1;2)`, et **repli fondé sur le chevauchement des sphères
consécutives** : si `|f(pᵢ₋₁)| + |f(pᵢ)| < δᵢ₋₁`, les sphères sont disjointes,
on a peut-être sauté la surface, on recule. Gains mesurés : **−17,5 %** sur une
scène de ville, **+16,6 %** sur un bijou, `ω ≈ 1,2` optimal. Les auteurs sont
francs : trouver un bon `ω` « can be challenging », et la variante agressive perd
sur GPU à cause de la divergence.

**Retenir : la sur-relaxation rapporte 15 à 20 %, pas un facteur 2.** Ça ne vaut
pas la complexité en première version — et notre mesure du §8.4 montre qu'une
sur-relaxation *sans* le test de repli troue 23 % de la surface à ω = 1,5.

**Segment Tracing** (Galin et al., Eurographics 2020). Bornes de Lipschitz
**locales** sur un segment candidat, avec la subtilité qui compte : la borne
porte sur la dérivée **directionnelle le long du rayon**, pas sur la norme du
gradient — donc la méthode attaque précisément le cas pathologique, le rayon
rasant. Et l'écart entre les deux colonnes de leur table est tout le message :

| Objet | Requêtes de champ | Temps de rendu |
|---|---|---|
| Fluide (50 000 primitives) | ×4 400 | **×6,0** |
| Cluster (2 001) | ×3 844 | **×93** |
| Roi d'échecs (17) | ×7,6 | ×1,3 |

« Reducing the number of field function queries does not necessarily improve
computation time » — évaluer la borne locale coûte cher. Et leur honnêteté vaut
d'être citée : quand la distribution de primitives est uniforme, « the benefit is
limited or negative », et **un octree bat le segment tracing dès que sa
profondeur dépasse 7**.

**Le papier qui règle le problème autrement, et qu'il faut connaître** :
Keeter 2020, *Massively Parallel Rendering of Complex Closed-Form Implicit
Surfaces* (SIGGRAPH). Il **abandonne le sphere tracing** : hiérarchie spatiale
peu profonde à haut facteur de branchement (tuiles 64×64 → 8×8 → pixel),
arithmétique d'intervalles à chaque niveau pour éliminer les régions vides **et
surtout élaguer la liste d'instructions** de la région survivante — la complexité
d'expression chute de **deux ordres de grandeur**. La phrase qui nous concerne :
la méthode **ne requiert que la continuité C0**, « allowing for warping and
blending operations which break Lipschitz continuity ».

Ses chiffres : sous **8 ms par image même à 4096²** ; en 3D à 1024³ sur Tesla
V100, des modèles d'architecture au-delà de 40 images/seconde — mais **une
sculpture à raccords lisses à 11 images/seconde seulement**. Et son diagnostic
rejoint le nôtre : « the many smooth blends are leading to **less useful interval
evaluation results** ». **Le mélange doux dégrade l'élagage, quelle que soit la
méthode d'élagage.** Trois sources indépendantes le disent — Media Molecule,
Keeter, et notre §5.4.

C'est aussi la brique de **libfive**, noyau de CAO par représentation
fonctionnelle avec maillage manifold. À regarder si un jour on veut un bake
propre.

**Les hiérarchies de bornes**, gains publiés : volumes englobants **×4,8**
(Hart) ; BVH sur primitives SDF **×8** (Quilez). Et une note contre-intuitive :
Hart a testé l'octree et **n'a mesuré aucun gain** — son intérêt selon lui n'est
pas de vider l'espace mais « the imposition of **local bounds** on the Lipschitz
constants », c'est-à-dire l'idée du segment tracing, vingt-quatre ans plus tôt.

---

## 4. Ce que les moteurs de production savent faire, et ce que Clarisse changerait

### 4.1 Le tableau, et une correction à `csg-clarisse.md`

| Moteur | SDF sur grille | **SDF analytique ray-marché** | Méthode |
|---|---|---|---|
| **Octane** | oui | **oui, natif et documenté — Vectron** | SDF compilée en OSL, **empreinte VRAM nulle** |
| **Arnold** | oui (`implicit` + solveur `levelset`) | **oui** (`implicit` + field shader OSL) | ray marching, normales par différences finies |
| **RenderMan** | oui (`impl_openvdb`) | oui, en écrivant une DSO | **dicing** micropolygone, pas du sphere tracing |
| Karma | non | non | il faut mailler |
| Cycles / Blender | **non** — « there is no current support for rendering them as surfaces » | non | il faut mailler |
| **Clarisse** | volumes seulement | **rien** | — |

**Correction à porter dans `csg-clarisse.md` §4.3**, qui écrivait « aucun moteur
de production ne fait de sphere tracing d'arbre CSG analytique ». **C'est faux :
Octane le fait, et c'est documenté.** Vectron rend des SDF définies en OSL
« without the need to convert them to a Mesh first », avec une **empreinte
mémoire nulle**, et livre les opérateurs Union, Subtract, Intersect, Clip, Ink,
Inset, Offset — **avec des variantes smooth, round et stair à rayon réglable**.
Le contrat OSL est simple : une structure avec `objId`, `matId`, `u`, `v`, `dist`,
et le point en espace local.

Ça change la lecture du risque : la voie qu'on propose **a un précédent
commercial qui fonctionne**. Ce n'est plus un pari architectural, c'est un
rattrapage. *(Les paramètres du ray marcher de Vectron — pas maximal, epsilon,
gestion de Lipschitz — ne sont pas documentés.)*

### 4.2 Houdini : des booléens durs sur grille, et aucun `smin`

C'est le point le plus utile pour situer ce qu'on gagnerait.

`VDB Combine` ne propose que **SDF Union / Intersection / Difference** —
c'est-à-dire `min` et `max` purs. **Aucune variante douce n'existe dans
OpenVDB.** Ce qu'on a à la place :

- **`VDB Smooth SDF`** : de la diffusion (courbure moyenne, Laplacien), rayon
  **en voxels**. C'est un lissage **global de la surface**, pas un congé local.
- **`VDB Reshape SDF`** : Dilate / Erode / Open / **Close**. Le Close — dilater
  puis éroder du même montant — est **le congé de production sous Houdini**.
  C'est de la morphologie mathématique, et la différence conceptuelle est nette :
  **Close applique un rayon global à toute la surface**, alors que `smin` est
  local à chaque paire et gratuit.
- **`VDB Renormalize SDF`**, dont l'existence même est l'aveu qu'après une
  opération le champ n'est plus une distance.

**Et Copernicus a apporté le bon vocabulaire… en 2D.** Le `SDF Blend COP` de
Houdini 20.5/21 offre Union/Intersect/Subtract **avec des raccords None /
Smooth / Round / Chamfer**. C'est littéralement le vocabulaire de MagicaCSG —
mais cantonné à la 2D. Le moteur d'évaluation GPU existe désormais dans Houdini,
le vocabulaire aussi, et **il n'y a toujours aucun node SDF 3D analytique**.

### 4.3 Le triangle indépassable de la représentation

C'est la façon la plus compacte de dire pourquoi la voie analytique existe.

> **Bande étroite** = peu de mémoire, mais **pas sphere-traçable**.
> **SDF plein** = sphere-traçable, mais **O(N³)**.
> **Analytique** = sphere-traçable **et** O(1) en mémoire, au prix du coût par
> échantillon.

**Pourquoi une bande étroite ne se sphere-trace pas** : hors de la bande, la
valeur est **constante**, égale à `3 × taille de voxel` (OpenVDB fixe la
demi-largeur à 3 dans son code source). Le pas maximal depuis l'extérieur est
donc de trois voxels, quelle que soit la distance réelle : le sphere tracing
dégénère en marche à pas fixe, **~342 pas** pour traverser un domaine de 1024
voxels. **La valeur de distance ne sert plus à rien pour accélérer.** OpenVDB
accélère par la **topologie** — un HDDA qui saute les tuiles inactives de 8³,
128³, 4096³ en lisant des masques de bits — pas par la distance.

Et si on veut un SDF plein, on retombe en O(N³) : **4,29 Go à 1024³** en
float32. C'est exactement le choix d'Epic : les Mesh Distance Fields d'Unreal
sont pleins et **plafonnés à 128³–256³ par maillage**, précisément parce qu'ils
doivent être cone-tracés. **Le plafond de résolution est le prix du sphere
tracing sur grille.**

**L'ordre de grandeur qui tranche.** Un nœud d'arbre CSG — type, matrice, quatre
paramètres, rayon de mélange, identifiant de matériau — pèse **64 à 128 octets**.
Cinq cents primitives font **40 ko**, soit *vingt feuilles VDB*. Le même objet en
bande étroite à 4096³ pèse 420 Mo, et en grille dense 275 Go. Et surtout :
**la grille a une résolution, l'expression n'en a pas.** Si on décide après coup
de rendre l'objet en gros plan, la grille est à refaire ; l'expression, non.

**Le coût de l'animation est le vrai coût caché.** Museth mesure, à 4096³ : un
booléen entre deux level sets coûte **0,01 s** — quasi gratuit — mais rasteriser
5 M de sphères en level set coûte **~3 s par image**, et une advection avec
renormalisation **4,39 s**. **Le booléen sur grille est gratuit ; c'est la
rasterisation et la renormalisation qui coûtent, 300 à 440 fois plus.** C'est
structurellement pourquoi Houdini ne pourra jamais offrir ce que fait MagicaCSG
tant que la représentation est une grille. L'analytique s'anime gratuitement :
quatre octets à écrire.

Le contraste le plus net vient de Seyb et al. (Titan RTX, 1080p, temps d'image
complets) :

| Scène | **Analytique direct** | Marching cubes 256³ | 512³ | 1024³ |
|---|---:|---:|---:|---:|
| Dinosaure | **10 ms** | 150 ms | 2 075 ms | 9 689 ms |
| Tête | **13 ms** | 193 ms | 2 326 ms | 12 220 ms |

Leur remarque décisive : l'extraction d'isosurface est imbattable **si
l'isosurface ne change pas** — « but in an editing context we want to enable the
user to quickly change both the deformation as well as the underlying geometry ».

### 4.4 Ce que ça donne pour Clarisse

**Il n'y a rien à brancher : il y a tout à écrire.** Aucune trace de surface
implicite dans Clarisse au-delà de ses trois primitives isolées, aucun rendu de
level set en surface — seulement des volumes OpenVDB depuis iFX 2.0.

Mais **le point d'entrée est ouvert et prouvé** (§5), ce qui est le contraire de
la situation de Blender ou de Houdini. Et deux choses qu'aucun des cinq outils du
§2 ne peut faire tombent gratuitement :

- **l'instanciation.** `csg-clarisse.md` §7.2 l'a vérifié : le
  `SceneObjectScatterer` natif accepte une géométrie sans un seul sommet.
  Un boulon percé, un galet mélangé, une brique ébréchée — définis une fois
  analytiquement, dispersés par millions ;
- **le rendu de production.** Path tracer, vraies ombres, vraies réflexions,
  vrai flou, vrais matériaux Clarisse. Tous les sacrifices du §2.1 tombent.

Ce qu'on n'aura pas, et qu'il faut dire d'avance : **la sculpture au geste**.
Clarisse n'a aucun outil de modélisation. Ce qu'on peut faire est un système de
**nœuds** à champ de distance — paramétrique, animable, instanciable. C'est la
version Clarisse de l'idée, pas une copie de MagicaCSG.

---

## 5. Le point d'entrée dans Clarisse — ce qui était acquis, et ce qui ne l'était pas

### 5.1 L'acquis de `csg-clarisse.md`

Rappel en trois lignes, parce que tout le reste en dépend. `GeometryObject` est
une interface purement virtuelle qui ne mentionne jamais un polygone : huit
méthodes, dont « combien de primitives », « où est la boîte de chacune »,
« qu'est-ce qu'un rayon y touche » et « à quoi ressemble la surface au point
touché ». La sonde `native/csg_sonde/` a prouvé qu'un `GeometryObject` maison se
compile, se lie, reçoit le BVH de Clarisse, est intersecté, est ombré, et que le
`SceneObjectScatterer` natif l'instancie.

Ce qu'elle n'avait pas prouvé : elle résolvait des **équations du second degré**.
Coût d'un test rayon/sphère, exact, borné. Le mélange doux n'a pas de solution
analytique — il faut **marcher** le long du rayon. Rien dans le contrat de
`intersect_primitive` n'interdit d'y boucler, mais rien ne le promet non plus.

### 5.2 La sonde SDF, et ce qu'elle a rendu

`native/sdf_sonde/` — classe `GeometrySdfSonde`, dérivée de
`Geometry`. Le champ est un ensemble de sphères, boîtes et tores repliés par
`smin` polynomial. La marche se fait dans `intersect_primitive`, la normale est
reconstruite par différences centrées dans `compute_fragment_sample`.

**Elle rend.** Le mélange est visible : les primitives se fondent l'une dans
l'autre par des raccords lisses, les arêtes propres à chaque primitive restent
vives, l'ombrage spéculaire est net — ce qui prouve que la normale par gradient
est correcte et pas seulement plausible. Les deux stratégies d'exposition
(primitive unique et une primitive par blob) donnent la **même surface**, ce qui
sert de contrôle : la différence entre elles est purement de performance.

**Le détail d'implémentation qui bloquait, et sa sortie.**
`compute_fragment_sample` ne reçoit du moteur que `(u, v, w)` et un
`sub_primitive_id`. Un sphere tracer n'a aucune paramétrisation naturelle à y
mettre. La sortie est de se servir de `(u, v, w)` **comme d'un conteneur** : on y
range la position du point touché, normalisée dans la boîte englobante. Les trois
canaux sont des `double`, la précision est largement suffisante, et le moteur les
accepte tant qu'ils restent dans `[0, 1]` — d'où la nécessité de serrer les
valeurs de bord.

C'est le point à retenir pour tout futur nœud implicite : **`(u, v, w)` est le
seul canal de trois doubles entre l'intersecteur et l'ombrage.** Le
`sub_primitive_id` en donne un quatrième, entier.

### 5.3 Les chiffres, mesurés dans Clarisse

Scène de 64 primitives, image 400 × 400, antialiasing et ombres par défaut,
temps annoncé par le raytracer lui-même (le temps de mur inclut ~2 s de
démarrage de `cnode` et ne veut rien dire) :

| Configuration | Rendu | Entrées dans l'intersecteur | it/entrée | éval/entrée | Rayons plafonnés |
|---|---|---|---|---|---|
| **Analytique**, union dure de sphères | **1,850 s** | 21,6 M | 1,00 | 53,0 | 0 % |
| Sphere tracing, **union dure**, découpé | 2,838 s | 16,4 M | 4,96 | 116,1 | 0,066 % |
| Sphere tracing, **mélange doux**, découpé | 13,888 s | 33,1 M | 6,59 | 219,5 | 0,163 % |
| Sphere tracing, **mélange doux**, primitive unique | **4,681 s** | 2,49 M | 5,94 | 473,7 | 0,111 % |
| Idem, **pas 0,5** | 33,249 s | 32,1 M | 14,82 | 474,2 | 1,40 % |
| Idem, **plafond 12 itérations** | 1,771 s | 0,33 M | 4,44 | 354,1 | **16,25 %** |

Ce qui se lit dedans :

**Le sphere tracing d'une union dure coûte 1,53 fois l'intersection
analytique.** C'est le prix de la marche elle-même, sans mélange : cinq
itérations là où l'analytique en fait une. Modeste.

**Le mélange doux coûte 2,53 fois l'analytique** (4,681 / 1,850), toutes causes
confondues. C'est le vrai chiffre à retenir, et il est bien plus doux que ce que
la littérature suggère.

**Le découpage en une primitive par blob coûte 2,97 fois la primitive unique**
(13,888 / 4,681). Voir §5.4 — c'est le résultat qui corrige la recommandation
précédente.

**Diviser le pas par deux multiplie le coût par 2,39** et fait passer les rayons
plafonnés de 0,11 % à 1,40 %.

*Précision sur la colonne « entrées » : le compteur mesure les appels à
`intersect_primitive`, pas les rayons distincts. En primitive unique les deux
coïncident ; en découpé, un rayon entre dans autant de boîtes qu'il en traverse.
C'est justement l'objet du §5.4.*

### 5.4 Le piège du découpage — la correction à `csg-clarisse.md`

`csg-clarisse.md` §3.2 écrivait : « il suffit d'exposer une primitive par feuille
de l'arbre au lieu d'une seule primitive globale, et le BVH de `GasGeometry`
s'en charge. La sonde ne le fait pas : c'est la première chose à corriger dans
un vrai nœud. »

**C'est vrai sans mélange, faux avec.** La sonde SDF fait les deux et mesure :

| | primitive unique | une primitive par blob |
|---|---|---|
| Appels à l'intersecteur | 2,49 M | 33,10 M — **× 13,3** |
| Évaluations de primitive, total | 1,18 G | 7,26 G — **× 6,2** |
| Rendu | **4,681 s** | 13,888 s — **× 2,97** |

Le mécanisme est direct et il est visible dans le journal de construction de la
scène : la boîte de chaque blob doit être **élargie du rayon de mélange**, parce
que le champ mélangé s'étend au-delà de la primitive. Les boîtes se recouvrent,
un rayon en traverse treize, et il **repart de zéro dans chacune** — la marche
n'a pas de mémoire d'une primitive à l'autre.

Le BVH fait bien son travail : il évite tout le travail sur les rayons qui
manquent l'objet, ce qui explique pourquoi le rapport en temps (2,97) est
meilleur que le rapport en évaluations (6,2). Mais il ne compense pas la
duplication.

**Ce que ça n'établit pas** : cette scène est dense — 64 primitives de rayon 0,35
à 0,80 dans un cube de 3,2 unités, elles se chevauchent toutes. Sur des grappes
bien séparées, le découpage redeviendrait gagnant. La leçon transférable n'est
donc pas « ne jamais découper », c'est : **le découpage n'est pas gratuit et son
gain dépend de la densité ; il se mesure, il ne se suppose pas.** Et il n'y a
aucune raison de le faire dans une première version.

### 5.5 Ce que la sonde n'a pas prouvé

- **Le motion blur.** `GasGeometry::create` prend une géométrie de base et une
  géométrie déformée ; on n'a pas essayé.
- **Le displacement.** `csg-clarisse.md` §8.4 le signale déjà comme un piège :
  il change la boîte de la primitive, qu'il faut alors majorer.
- **Les UV.** Sur une surface implicite mélangée, il n'y a pas de dépliure
  naturelle. Les projections planaire, cylindrique, sphérique et triplanaire
  sont exactes et gratuites, mais elles ne suivent pas la forme.
- **Le viewport.** Un nœud implicite n'a rien à montrer en OpenGL tant qu'on ne
  lui donne pas d'aperçu. C'est le sujet du §6.
- **La réentrance** de `GasObject::ray_hit` depuis `intersect_primitive`, qui
  reste la sonde à une demi-journée de `csg-clarisse.md` §6.4. Elle décide de la
  possibilité d'un CSG sur des maillages, pas du SDF.

---

## 6. La carte graphique — ce que le SDK expose

Trois sous-questions posées par Romain, traitées dans l'ordre.

### 6.1 `gl_view_display_mode` est un cul-de-sac — et c'est net

C'était la piste de départ : un module peut-il fournir son propre mode
d'affichage dans le viewport ? **Non.**

`J:\Clarisse-SDK\include\gl_view_display_mode\` contient deux fichiers, dont un
qui n'est qu'une macro d'export. Le fichier utile fait 70 lignes et définit :
un `enum ShadingMode` de neuf valeurs, un POD `ShadingModeParams`, et quatre
accesseurs statiques inline sur un tableau global. **Aucune classe de base de
module, aucune méthode virtuelle, aucun `register_*`, et pas une seule mention
d'OpenGL** — l'en-tête n'inclut que `core_log.h` et `core_string.h`.

Confirmé par le binaire : `ix_gl_view_display_mode.def` compte **13 symboles**,
tous des constructeurs de `ShadingModeParams` et les quatre accesseurs. Et
confirmé par la contrainte `cmagen` : il n'existe **aucun**
`gl_view_display_mode.dll` ni `display_mode.dll` dans le dossier `module\` de
l'installation. Il n'y a pas de classe à dériver.

Côté objet de scène, l'attribut existe — `display_mode` sur `Geometry` — mais
son énumération est figée dans `module_scene_object.h:258-266` : cinq valeurs,
`WIREFRAME` à `PREVIZ`. Pas de registre extensible.

### 6.2 Ce que le SDK expose vraiment, et c'est mieux

**Aucune API de calcul GPU n'est exposée.** Balayage de 853 en-têtes :
`opencl`, `vulkan`, `nvrtc`, `cudart` — zéro occurrence. `cuda` apparaît dans
trois fichiers, uniquement sous forme de gardes `#if !defined(__CUDACC__)` et de
macros `CUDA_DEVICE` : ce sont des en-têtes partagés entre le CPU d'Isotropix et
leur propre compilation `.cu`. `optix` apparaît une fois, dans un commentaire.

**Mais le viewport 3D de Clarisse est déjà un raytracer OptiX complet.** Le
dossier `Clarisse\ptx\` contient cinq fichiers PTX compilés (NVVM, CUDA 10.1,
`sm_60`), dont `implicit_geometries.cu.ptx` avec les six entrées
`implicit_box_intersect/bounds`, `implicit_sphere_intersect/bounds`,
`implicit_cylinder_intersect/bounds`. `module\widget_3d_view.dll` dépend
d'`optix.6.5.0.dll` et de `nppig64_10.dll`. Isotropix a donc écrit exactement le
mécanisme dont on aurait besoin — un intersecteur implicite sur GPU — et il est
fermé : `OptixIntersectionProgram` est un `enum`, les PTX sont précompilés,
aucune classe OptiX n'est déclarée dans le SDK. Et `OptixSceneItemHandle` porte
un `is_gpu_compliant()`, ce qui **suggère** (non vérifié) qu'une géométrie tierce
retomberait sur le chemin CPU du viewport.

*À noter pour `passation.md` : l'idée reçue selon laquelle OptiX ne sert qu'au
débruiteur est fausse. Il sert aussi, et surtout, au 3D View.*

### 6.3 Le vrai chemin GPU : OpenGL compute — vérifié à l'édition de liens

`J:\Clarisse-SDK\lib\ix_glutils.def` contient **2 735 symboles GLEW**. La
bibliothèque livrée avec Clarisse réexporte GLEW en entier.

**Vérifié en compilant et en exécutant** (`native/sdf_sonde/gl_probe.cpp`) :
un programme qui déclare les pointeurs à la main et se lie à `ix_glutils.lib`
résout et charge :

```
glDispatchCompute          -> 00007FFBA8093A70
glCreateShader             -> 00007FFBA80930E8
glShaderSource             -> 00007FFBA80931B8
glUseProgram               -> 00007FFBA8093270
glGenBuffers               -> 00007FFBA8093060
glBindBufferBase           -> 00007FFBA8094AE8
glMemoryBarrier            -> 00007FFBA8094780
glGenFramebuffers          -> 00007FFBA8093E90
glBindImageTexture         -> 00007FFBA8094778
GLEW_ARB_compute_shader    -> 00007FFBA8097F0A
GLEW_VERSION_4_3           -> 00007FFBA8097EA7
glewInit / glewIsSupported -> ...
```

Les drapeaux de version vont jusqu'à `__GLEW_VERSION_4_6`, et
`__GLEW_ARB_compute_shader`, `__GLEW_ARB_shader_storage_buffer_object` et
`__GLEW_ARB_shader_image_load_store` sont tous les trois présents.

**Ce que ça veut dire, et c'est le résultat central du volet GPU : un module
tiers peut lancer des compute shaders GLSL dans le contexte OpenGL de
Clarisse.** Sans embarquer CUDA, sans dépendance externe, sans contrainte de
fabricant. Le pointeur est nul tant que `glewInit` n'a pas tourné dans un
contexte courant — attendu, et c'est justement ce qui borne l'usage.

**Deux pièges de déclaration.** Les fonctions GLEW sont exportées comme des
**pointeurs de donnée** (`__glewXxx`), pas comme des fonctions : il faut
`__declspec(dllimport)`, sinon le linker cherche un symbole local. Et
`glew.h` n'est pas livré avec le SDK reconstruit — soit on le récupère (il est
libre), soit on redéclare à la main ce dont on se sert.

### 6.4 Où accrocher le dessin

Deux crochets, tous les deux vérifiés dans les en-têtes.

**`ModuleGlObject::paint_gl(GlUtilsCtx&)`** — `module_gl_object.h:51`, une
virtuelle avec son callback `cb_paint_gl`. Et la chaîne d'héritage est celle
qu'il faut :

```
ModuleGeometry : ModuleSceneObject : ModuleSceneItem : ModuleGlObject
```

**Notre nœud SDF hérite donc déjà de `paint_gl`.** Avec `pre_paint_gl`,
`destroy_gl_data` et `get_gl_bbox`.

**`ModuleTool::cb_draw_tool_3d(OfObject&, CtxTool&)`** — `module_tool.h:60`, et
`ctx_tool.h:44-51` donne la structure `gl` avec `view_point`, la taille du
viewport, son offset, le contexte de dessin et l'objet regardé.
`GMathViewPoint` fournit matrice, position de l'œil, champ de vision et ratio :
de quoi reconstruire vue et projection exactes. `module\tool.dll` existe, donc
`ModuleTool` est dérivable.

**Le piège commun aux deux** : `GlUtilsCtx` et `GlUtilsGlCtx` sont
**forward-déclarés seulement**. `glutils.h` et `gl_program.h` font partie des
en-têtes privés d'Isotropix, jamais publiés. Ils sont *liables* — 80 symboles
C++ dans `ix_glutils.def`, dont un wrapper GLSL `GlProgram` complet et une
quarantaine de helpers `GlUtils::draw_*` — mais il faut redéclarer les types à
la main, ABI-compatibles. C'est le même travail que le
`GeometryMediumDescriptor` de `csg-clarisse.md` §2.3, en plus gros.

**Le contexte est ancien.** Les shaders embarqués dans `ix_gui.dll` et
`widget_3d_view.dll` disent `#version 120`, et les imports de
`widget_3d_view.dll` sont du GL 1.x : `glBegin`, `glVertex3d`, `glNewList`,
`glSelectBuffer`. Autrement dit un contexte de compatibilité OpenGL 2.1. Y mêler
du GL 4.x obtenu via GLEW est possible — c'est le principe d'un contexte de
compatibilité — mais il faudra sauvegarder et restaurer scrupuleusement l'état
fixed-function autour de nos appels, sans quoi le reste du viewport se dérègle.
**Non vérifié.**

### 6.5 La limite structurelle, vérifiée : pas de GL hors du viewport

`dumpbin /dependents` sur `cnode.exe` : **aucune dépendance OpenGL, aucune
dépendance OptiX**. `clarisse.exe` dépend d'`ix_gui.dll`, qui porte le GL.

**Conséquence : tout ce que le §6.3 ouvre n'existe que dans la session
interactive.** Un aperçu GPU est un aperçu, au sens strict. Le rendu final, en
interactif comme en ferme, reste CPU.

Ce n'est pas une mauvaise nouvelle. C'est même l'architecture qu'on aurait
choisie : le GPU sert à ce qui doit être immédiat, le CPU à ce qui doit être
juste et reproductible, et il n'y a jamais deux moteurs à faire coïncider dans
l'image finale.

### 6.6 Le pool de threads, lui, est exposé et propre

Pour le §7, il faut savoir que Clarisse prête son ordonnanceur.
`sys_thread_task_manager.h` déclare `SysThreadTaskManager` avec
`add_generic_task(CoreFunction<void (unsigned int)>&&)`, `get_thread_count()`,
`wait_until_completed()`, et des gestionnaires hiérarchiques à tâches
préemptibles. La chaîne d'accès depuis un module est courte et vérifiée :

```
OfItem::get_application()  ->  OfApp : AppBase
AppBase::get_task_manager()          (app_base.h:420)
AppBase::run_thread_task(...)        (app_base.h:194)
```

`ix_sys.lib` est reconstruit. **Un module tiers peut donc soumettre du travail
au pool de Clarisse**, sans créer ses propres threads et sans concurrencer
l'évaluateur. Il n'y a en revanche **pas** de `parallel_for` : TBB est dans
l'installation mais n'est importé que par `ix_volume`, `openvdb` et `libusd_ms`
— ce n'est pas l'ordonnanceur de Clarisse.

---

## 7. Où le GPU peut servir, et où il ne peut pas

Toute cette section est mesurée sur la machine de Romain : **Intel i9-7920X**
(12 cœurs, 24 threads, 2,9 GHz, AVX-512 disponible mais non utilisé ici),
64 Go, **NVIDIA RTX 3090** (82 SM, 24 Go, CC 8.6), CUDA 12.9, pilote 576.57,
Windows en mode WDDM. Bancs dans `native/sdf_sonde/` (`sdf_cpu.cpp`, `sdf_gpu.cu`).

*Note factuelle : la machine a 12 cœurs physiques et 24 threads, pas 24 cœurs.
Ça ne change aucune conclusion — le scaling mesuré est de 13,3× — mais autant
que le chiffre soit juste dans le document.*

### 7.1 Lancer un noyau par rayon : la mesure qui ferme la question

**La taille des paquets que Clarisse passe à l'intersecteur.** Mesurée par la
sonde, sur un rendu complet :

```
paquets = 1 818 782     rayons par paquet = 1,41     maximum = 225
histogramme   1 rayon : 1 814 882   (99,79 %)
              33-64   :        70
              65+     :      3 830
```

**Clarisse appelle `intersect_primitive` avec un seul rayon dans 99,79 % des
cas.** Les paquets larges existent — jusqu'à 225 — mais ils sont deux mille fois
plus rares.

**Le coût d'un lancement GPU.** Noyau vide, RTX 3090, WDDM :

| Mesure | Coût |
|---|---|
| Mise en file seule (sans attendre) | 16,74 µs |
| **Lancement + synchronisation** (le cas d'un appel bloquant) | **20,28 µs** |

*La valeur est haute en partie parce que la carte est en WDDM et pilote
l'écran ; en TCC sous Linux on lit plutôt 5 à 10 µs dans la littérature. Même
divisée par quatre, la conclusion ne bouge pas.*

**L'aller-retour complet**, N directions montées, tracées, résultats
redescendus :

| N rayons | Aller-retour | µs/rayon | Débit équivalent |
|---:|---:|---:|---:|
| 8 | 456,3 µs | 57,04 | 0,02 Mrayons/s |
| 64 | 447,3 µs | 6,99 | 0,14 |
| 256 | 389,7 µs | 1,52 | 0,66 |
| 4 096 | 2 640 µs | 0,644 | 1,55 |
| 65 536 | 2 813 µs | 0,043 | 23,3 |
| 262 144 | 7 681 µs | 0,029 | 34,1 |
| 1 048 576 | 12 698 µs | 0,012 | **82,6** |

À comparer au CPU : **33,56 Mrayons/s** sur 24 threads en AVX2 avec élagage,
soit **0,030 µs par rayon**.

**Les trois conclusions, chiffrées :**

1. Il faut **~680 rayons** dans un lancement rien que pour amortir la latence
   (20,28 / 0,030).
2. Avec les transferts, le GPU ne dépasse le CPU 24 threads qu'à partir de
   **~262 000 rayons par lancement**.
3. Clarisse en fournit **1,41**. On est **cinq ordres de grandeur** en dessous
   du seuil.

Et deux aggravations qu'on n'a pas mesurées mais qui vont toutes les deux dans
le même sens : les 24 threads de rendu se disputeraient un contexte CUDA unique,
et chaque `cudaDeviceSynchronize` bloquerait un thread de rendu entier.

**Verdict : appeler le GPU depuis `intersect_primitive` est exclu.** Ce n'est
pas « probablement lent », c'est arithmétiquement impossible.

### 7.2 Où le GPU gagne vraiment : le débit brut

Deux mesures indépendantes du même rapport, ce qui est plutôt rassurant.

**Par la grille dense** — le cas de la polygonisation ou du bake, massivement
parallèle en un seul lancement :

| | Points/s | Évaluations de primitive/s |
|---|---:|---:|
| CPU, 1 thread, scalaire | 1,5 M | 95,8 M |
| CPU, 24 threads, scalaire | 19,9 M | 1 273 M |
| **CPU, 24 threads, AVX2** | **142,0 M** | **9 085 M** |
| **GPU, RTX 3090** | **3 150 M** | **201 608 M** |

Rapport GPU / CPU vectorisé multithreadé : **22,2×**.

*Il faut insister là-dessus : comparer le GPU au CPU **scalaire** donnerait 158×
et serait malhonnête. Le facteur réel est 22.*

**Par le sphere tracing plein écran** — même scène, même champ :
GPU 104,55 Mrayons/s × 64 primitives × 15,9 itérations ≈ **106 G évaluations/s**,
CPU 33,56 Mrayons/s × 142,3 évaluations ≈ **4,8 G évaluations/s**. Rapport :
**22,3×**.

Les deux voies donnent le même nombre. **Le RTX 3090 vaut 22 fois le i9-7920X en
débit brut d'évaluation de champ de distance.**

**Mais le facteur net sur une image entière n'est que de 3,1×** : 10,03 ms
contre 31,2 ms pour 1024². Parce que le CPU compense presque tout par un
meilleur algorithme — l'élagage par paquet lui fait faire 142 évaluations par
rayon là où le noyau GPU naïf en fait 1 018. **Le GPU brut vaut 22 fois le CPU
brut ; un CPU bien écrit récupère les trois quarts de l'écart.**

C'est la leçon la plus utile du volet 2, et elle vaut au-delà du SDF.

### 7.3 Les deux usages où le GPU est le bon outil

**L'aperçu interactif dans le viewport.** Un compute shader qui sphere-trace le
champ dans un framebuffer, 1024² en 10 ms, soit 100 images/seconde pour la
géométrie seule. C'est ce qui donnerait le retour immédiat de MagicaCSG pendant
qu'on manipule les primitives. Le champ tient en SSBO — 64 primitives font 2 ko —
et se met à jour à chaque changement d'attribut sans reconstruire quoi que ce
soit. **C'est là que le GPU débloque vraiment de la puissance**, et c'est le
seul endroit.

**Le bake d'une grille de distance.** 512³ = 134 millions de points en
**45,5 ms** sur la carte, contre ~0,95 s sur 24 threads en AVX2. Utile pour
exporter en VDB, pour polygoniser, ou pour construire un cache. Mais 0,95 s en
CPU est déjà acceptable pour une opération qu'on lance à la demande : le gain de
22× est réel et ne justifie probablement pas, à lui seul, une dépendance CUDA.

**Ce que le GPU ne peut pas faire ici**, et il faut l'écrire pour que ce ne soit
pas re-proposé : le rendu final. Non pas par manque de puissance, mais parce que
le shading de Clarisse est du C++ compilé qui s'évalue sur CPU
(`ModuleMaterial::evaluate`) et qu'il n'existe aucun chemin d'une DLL de matériau
vers du code GPU. `moteur-gpu.md` §1 l'établit déjà : GPU et « tout mon shading
marche » sont exclusifs. Rien ici ne le change.

### 7.4 Reste une conjecture, et il faut le dire

Le §6 établit que les symboles GL modernes se lient et que `paint_gl` est
héritée. Il **n'établit pas** que `paint_gl` soit effectivement appelée sur un
`ModuleGeometry` tiers, ni qu'un compute shader lancé depuis là fonctionne dans
le contexte de compatibilité de Clarisse. Ces deux points **exigent une session
interactive** — donc `clarisse.exe`, que je ne lance pas.

C'est le premier test à faire, et il coûte une demi-journée : un module qui
dérive `Geometry`, surcharge `paint_gl`, appelle `glewInit()` puis dessine trois
lignes de couleur, et qu'on regarde apparaître dans le 3D View. Si ça marche, le
reste est de l'ingénierie connue. Si ça ne marche pas, le repli est
`ModuleTool::draw_tool_3d`, et si celui-là échoue aussi, l'aperçu GPU tombe.

---

## 8. Ce qu'on gagne sur CPU avant de sortir le GPU

C'est la troisième question de Romain, et c'est celle dont la réponse est la
plus rentable.

### 8.1 Le tableau complet

Scène de 64 primitives, image 1024², rayons primaires, i9-7920X :

| Variante | Mélange dur | Mélange doux |
|---|---:|---:|
| Scalaire, 1 thread, sans élagage | 2 790 ms | 10 081 ms |
| Scalaire, 1 thread, **élagage par rayon** | 568 ms | 1 880 ms |
| **AVX2** ×8, 1 thread, sans élagage | 415 ms | 1 256 ms |
| AVX2 ×8, 1 thread, élagage par paquet | 88,9 ms | 228 ms |
| **AVX2 ×8, 24 threads, élagage** | **20,9 ms** | **31,2 ms** |

**Du point de départ naïf au point d'arrivée : 323×** sur le champ mélangé
(10 081 → 31,2 ms). Décomposé :

| Technique | Gain isolé |
|---|---|
| **Élagage par boîte englobante** (une fois par rayon ou par paquet) | **× 5,4** |
| **Vectorisation AVX2**, 8 rayons par paquet | **× 8,0** |
| **24 threads** | **× 7,3** ici, **× 13,3** sur une charge assez longue |

Trois remarques honnêtes sur ces chiffres.

**Le facteur SIMD de 8,0 sur 8 voies n'est pas une erreur de mesure.** Il vient
de deux effets qui s'ajoutent : la vectorisation elle-même, et le fait que
l'élagage par *paquet* coûte huit fois moins que l'élagage par *rayon*. Le gain
propre de la SIMD, à travail égal, est de **6,7× à 8,0×** selon le mélange —
mesuré sans élagage des deux côtés.

**Le scaling à 24 threads mesuré ici (7,3×) sous-estime la machine**, parce que
le test dure 31 ms et que la création des threads pèse. Sur la grille dense, qui
tourne cinq secondes, le scaling est de **13,3×** — cohérent avec 12 cœurs
physiques plus l'hyperthreading. C'est ce dernier chiffre qu'il faut retenir.

**AVX-512 n'a pas été essayé.** Le i9-7920X (Skylake-X) le supporte. Il
donnerait 16 voies au lieu de 8, mais avec la pénalité de fréquence connue de
cette génération : le gain net est **inconnu, probablement entre 1,2× et 1,6×**,
et ce serait un binaire non portable sur une ferme hétérogène. À ne pas faire
sans mesure.

### 8.2 Le coût du mélange doux, décomposé

| | Dur | Doux (k = 0,25) | Rapport |
|---|---:|---:|---:|
| Itérations par rayon | 12,1 | 14,8 | × 1,22 |
| Évaluations par rayon (avec élagage) | 70,3 | 142,3 | **× 2,02** |
| Temps, 24 threads AVX2 | 20,9 ms | 31,2 ms | × 1,49 |

Le mélange coûte **+22 % d'itérations** — parce que `smin` sous-estime la
distance et fait avancer moins vite — et surtout **× 2 d'évaluations**, parce
que la boîte de chaque primitive doit être élargie du rayon de mélange et que
l'élagage garde donc deux fois plus de candidats. C'est le même mécanisme qu'au
§5.4, mesuré ici à l'échelle de la primitive plutôt que de la boîte Clarisse.

### 8.3 Le passage à l'échelle en nombre de primitives

Image 512², 24 threads AVX2, mélange doux :

| Primitives | Débit | Évaluations par rayon |
|---:|---:|---:|
| 16 | 50,0 Mrayons/s | 50,3 |
| 64 | 26,2 Mrayons/s | 153,2 |
| 256 | 13,5 Mrayons/s | 390,0 |

Multiplier les primitives par 16 ne multiplie le coût que par **3,7** — l'élagage
travaille. Mais on est loin du logarithmique : le nombre de candidats retenus
croît en ×7,8 pour ×16 primitives, parce que la densité augmente à volume
constant.

**Le goulot à venir est identifiable** : l'élagage est un test de sphère
englobante fait linéairement sur toutes les primitives, une fois par paquet.
C'est O(N) par paquet. Au-delà de quelques centaines de primitives, il faudra
une vraie hiérarchie — un BVH sur les primitives du champ, construit une fois
dans `create_resource`. Barbier et al. mesurent qu'un élagage de Lipschitz bien
fait fait passer une scène de 6 023 nœuds de 9 448 ms à 15 ms par image sur
RTX 4060 ; c'est le bon ordre de grandeur de ce qu'on laisse sur la table.

**Extrapolation, non mesurée** : à ~1 000 primitives et sans hiérarchie, on
serait autour de 5 Mrayons/s, soit quelques secondes pour un plan 1080p en
rayons primaires. Utilisable, mais c'est le point où la hiérarchie devient
obligatoire.

### 8.4 La robustesse du pas — deux résultats contre-intuitifs

**Mesure directe de la norme du gradient**, différences centrées sur une grille
de 160³ restreinte au voisinage de la surface (≈ 4 M points échantillonnés) :

| Mélange k | max \|∇f\| | p99,99 | Part > 1,01 | Pas sûr = 1/max |
|---:|---:|---:|---:|---:|
| **0,00 (min dur)** | **1,0291** | 1,0004 | 0,0019 % | 0,972 |
| 0,05 | 1,0008 | 1,0003 | 0 % | 0,999 |
| 0,10 | 1,0005 | 1,0003 | 0 % | 1,000 |
| 0,25 | 1,0005 | 1,0003 | 0 % | 1,000 |
| 0,50 | 1,0004 | 1,0002 | 0 % | 1,000 |
| 1,00 | **0,9999** | 0,9939 | 0 % | 1,000 |

Un sphere tracer à pas plein est correct si et seulement si le champ est
1-lipschitzien, c'est-à-dire si `|∇f| ≤ 1` partout.

**Le `smin` polynomial de Quilez laisse le champ 1-lipschitzien.** Le maximum
est à 1,0005 pour k = 0,25 — un écart de cinq dix-millièmes, compatible avec
l'erreur de la différence finie — et il **descend** à 0,9999 quand on augmente le
mélange. Le seul dépassement réel est celui du **`min` dur** (1,0291), et il est
localisé sur les arêtes vives de l'union, où le gradient est discontinu.

**Ce qu'il faut en retenir, et c'est une nuance importante** : la littérature dit
vrai — `smin` détruit la propriété de distance exacte, `f(x) ≠ d(x, surface)`.
Mais elle la détruit **par en dessous**. Le champ mélangé sous-estime la
distance, ce qui est précisément le côté sûr pour un marcheur. On paie ça en
itérations, pas en trous.

Ce n'est pas une propriété du mélange doux en général, c'est une propriété de
**cette formule-là**. Le smooth-min circulaire, un scale non uniforme, un
displacement procédural ou un bruit de constante de Lipschitz supérieure à 1 sont
tous des façons de casser la borne dans le mauvais sens. **La règle de
conception qui en découle** : exposer un facteur de sûreté sur le pas, le laisser
à 1 pour les opérateurs qu'on a mesurés, et le baisser automatiquement dès qu'un
opérateur non borné entre dans l'arbre.

**Le second résultat contre-intuitif : réduire le pas dégrade l'image.** Même
scène, référence tracée à pas 0,10 avec un plafond de 20 000 itérations, mélange
doux :

| Facteur de pas | it/rayon | Rayons plafonnés | Fuites vs référence |
|---:|---:|---:|---:|
| 0,50 | 31,7 | 1 008 | 854 (1,57 %) |
| 0,80 | 18,5 | 279 | 220 (0,40 %) |
| 0,95 | 14,7 | 175 | 133 (0,24 %) |
| **1,00** | **13,4** | **153** | **115 (0,21 %)** |
| 1,20 | 8,0 | 64 | 406 (0,74 %) |
| 1,50 | 5,0 | 15 | 12 725 (23,35 %) |

À pas 0,5, on paie 2,4 fois plus d'itérations **et** on perd sept fois plus de
surface qu'à pas 1,0 — parce que le petit pas fait mordre le plafond avant
d'atteindre la surface. Au-delà de 1,0, la sur-relaxation naïve troue vraiment :
23 % de la surface disparaît à pas 1,5.

**Le réglage sûr est exactement 1,0**, et il n'y a rien à gagner en dessous. La
sur-relaxation reste possible mais exige le mécanisme de repli d'*Enhanced Sphere
Tracing* (Keinert et al. 2014) — on avance de plus que la distance, on vérifie
que les sphères se recouvrent, et on recule si elles ne se recouvrent pas. **Non
implémenté, non mesuré.**

### 8.5 Le mode d'échec, vu dans Clarisse

C'est l'observation qui remplace le terme « bandes de Mach » par ce qu'on voit
réellement.

Rendu avec un plafond de 12 itérations : **16,25 % de rayons plafonnés**, et
l'objet **se dissout par plaques**. Pas de gradation, pas de bande sombre : des
morceaux de surface entiers manquent, avec des bords nets, et on voit à travers.

L'explication est structurelle. Dans un shader temps réel qui colore par
`itérations / plafond`, un rayon non convergé donne une couleur — d'où les
bandes. Dans un moteur de production, un rayon non convergé **ne pousse aucune
intersection** : il n'y a pas de surface, donc pas de pixel. Les zones touchées
sont celles où le rayon rase la surface, c'est-à-dire les silhouettes et les
concavités — exactement les endroits où l'œil le voit.

**Conséquence de conception** : le plafond d'itérations n'est pas un réglage de
qualité, c'est un garde-fou contre les boucles infinies, et il faut le mettre
haut. À 128, on est à 0,13 % de rayons perdus sur cette scène ; c'est encore
visible sur une silhouette propre. La vraie parade est d'**adapter le seuil
d'arrêt à la taille du pixel** — un rayon qui a parcouru une longue distance a
un cône plus large et peut s'arrêter plus tôt — plutôt que de monter le plafond.
Hart le décrivait déjà en 1996 comme la propriété centrale du sphere tracing
(l'antialiasing géométrique gratuit) ; on ne l'a pas implémenté.

---

## 9. Recommandation, et ce que ça coûte

### 9.1 Par quoi commencer, et l'ordre

**1. Le nœud `GeometrySdf`, une semaine.** Le sphere tracing est prouvé dans
Clarisse, la sonde en est le squelette. Ce qu'il contient :

- **Six à huit primitives** : sphère, boîte, boîte arrondie, cylindre, capsule,
  cône, tore, plan. Toutes ont une SDF exacte publiée.
- **Union, intersection, différence**, en dur et en doux, avec un **rayon de
  mélange par opération** — c'est le vocabulaire de MagicaCSG et il n'y a pas
  de raison d'en inventer un autre.
- **Une seule primitive Clarisse**, boîte englobante globale. Le découpage est
  une optimisation à mesurer plus tard, et le §5.4 montre qu'elle peut coûter.
- **Pas plein, plafond haut** (256), seuil d'arrêt proportionnel à la distance
  parcourue.
- **Les UV par projection** — planaire, cylindrique, sphérique, triplanaire.
- **Un shading group par primitive du champ**, ce qui permet de donner une
  matière différente à chaque morceau. Sur un champ mélangé, l'attribution se
  fait par la primitive la plus proche, avec une transition dans la zone de
  raccord : c'est ce qui fait la différence entre un jouet et un outil.

Ce nœud est utile seul : Clarisse n'a **ni cône, ni tore, ni capsule, ni
congé**, et ses trois implicites ne se combinent pas.

**2. La sonde `paint_gl`, une demi-journée, en interactif.** §7.4. Elle décide
de tout l'aperçu GPU, et elle exige `clarisse.exe` — donc Romain, ou une session
où j'ai le droit de l'ouvrir.

**3. L'aperçu GPU par compute shader, deux à quatre semaines**, si la sonde
passe. Le champ en SSBO, un compute shader qui sphere-trace dans une texture,
un blit dans le viewport. C'est ce qui donne la sensation MagicaCSG.

**4. Une hiérarchie de bornes sur les primitives du champ, une semaine**, quand
on dépassera quelques centaines de primitives. Pas avant : à 64 primitives
l'élagage linéaire suffit.

**5. Le bake vers une grille ou un VDB, une à deux semaines.** `openvdb.dll` 7.0
est livré avec Clarisse et se lie — c'est vérifié dans `csg-clarisse.md` §4.1.
Sur CPU d'abord ; 24 threads en AVX2 font une grille 512³ en une seconde.

### 9.2 Ce qui serait un piège

- **Partir sur CUDA.** Mono-fabricant, absent d'une ferme quelconque, et le seul
  usage où il gagnerait — le bake — est déjà tenu par le CPU à un facteur 22
  près, pour un chantier dix fois moins cher. L'OpenGL compute est déjà lié dans
  le process et ne coûte aucune dépendance.
- **Découper en une primitive par feuille par principe.** §5.4 : mesuré à
  ×2,97 de coût sur une scène dense.
- **Baisser le facteur de pas « pour être sûr ».** §8.4 : c'est exactement
  contre-productif.
- **Confondre l'aperçu et le rendu.** Si l'aperçu GPU et le rendu CPU ne donnent
  pas exactement la même surface, chaque écart deviendra un bug rapporté. Il faut
  soit partager la formule à la ligne près, soit assumer et documenter l'écart.
- **Croire que le GPU va « débloquer de la puissance » partout.** 22× en brut,
  3,1× en net sur une image, 0 sur le rendu final. C'est un outil précis, pas un
  multiplicateur.
- **Vouloir MagicaCSG.** C'est un outil de *sculpture directe*, piloté au geste.
  Clarisse n'a aucun outil de modélisation — les quinze classes `Tool*` sont
  inventoriées dans `csg-clarisse.md` §7.3 et aucune ne trace ni ne sculpte. Ce
  qu'on peut faire est un **système de nœuds à champ de distance**, ce qui est
  autre chose : moins direct, mais paramétrique, animable, et instanciable un
  million de fois. C'est la version Clarisse de l'idée, pas une copie.

### 9.3 Le coût, sans arrondir

| Chantier | Estimation | Confiance |
|---|---|---|
| Nœud `GeometrySdf` : primitives, opérations, mélange, UV, shading groups | **5 à 8 jours** | haute — la sonde a fait le plus dur |
| Sonde `paint_gl` en interactif | **une demi-journée** | haute |
| Aperçu GPU par compute shader dans le viewport | **2 à 4 semaines** | **basse** — dépend entièrement de la sonde, et `GlUtilsCtx` est à redéclarer |
| Hiérarchie de bornes sur le champ | **1 semaine** | moyenne |
| Bake vers grille / VDB, sur CPU | **1 à 2 semaines** | moyenne |
| Édition interactive au geste (outils, manipulateurs) | **plusieurs mois** | basse — il n'y a rien à réutiliser |
| Matériaux par primitive, transitions correctes dans les raccords | **2 à 3 semaines** | basse |

**« Un MagicaCSG dans Clarisse » au sens complet est un chantier de plusieurs
mois**, et l'essentiel de ce temps n'est pas dans le champ de distance : il est
dans l'interaction, qui est précisément ce que Clarisse n'a pas.

**Mais un nœud SDF avec mélange doux, rendu par le path tracer, instancié par le
Scatterer, existe en une semaine.** Et ça, aucun autre logiciel ne le fait :
MagicaCSG ne sait pas instancier, Houdini doit voxeliser, Blender doit
tesseller. La valeur n'est pas dans la copie de l'outil, elle est dans le
croisement.

---

## 10. Ce qui a été vérifié, ce qui a été lu, ce qui est supposé

**Vérifié en exécutant du code sur cette machine :**

- un `GeometryObject` maison qui **sphere-trace** un champ de distance mélangé
  se compile, se lie, est appelé par le raytracer et est ombré — cinq
  configurations rendues, images et statistiques dans
  `native/sdf_sonde/`, images rendues dans le dossier de travail temporaire ;
- tous les chiffres des §5.3, §5.4, §7.1, §7.2, §8.1 à §8.5 — bancs dans
  `native/sdf_sonde/` (`sdf_cpu.cpp`, `sdf_gpu.cu`) ;
- **Clarisse passe 1,41 rayon par appel à `intersect_primitive`**, un seul dans
  99,79 % des cas, maximum 225 ;
- la latence d'un lancement CUDA sur cette carte : 16,74 µs en file, 20,28 µs
  avec synchronisation ;
- **les symboles GLEW de `ix_glutils.lib` se lient et se chargent depuis du code
  à nous**, `glDispatchCompute` compris (`native/sdf_sonde/gl_probe.cpp`) ;
- `cnode.exe` n'a **aucune** dépendance OpenGL ni OptiX ; `clarisse.exe` dépend
  d'`ix_gui.dll` ;
- `max |∇f| = 1,0005` pour le `smin` polynomial à k = 0,25, contre 1,0291 pour
  le `min` dur.

**Lu dans les en-têtes, les tables d'exports ou les binaires, non exécuté :**

- `gl_view_display_mode.h` ne définit aucune classe de base de module ;
  `ix_gl_view_display_mode.def` compte 13 symboles ; aucun DLL correspondant
  dans `module\` ;
- `ModuleGeometry : ModuleSceneObject : ModuleSceneItem : ModuleGlObject`, et
  `paint_gl` est une virtuelle avec son callback ;
- `ModuleTool::cb_draw_tool_3d` et la structure `CtxTool::gl` ;
- `GlUtilsCtx` et `GlUtilsGlCtx` sont forward-déclarés seulement ; `glutils.h` et
  `gl_program.h` ne sont pas livrés ;
- le contexte GL de Clarisse est en `#version 120` avec des appels GL 1.x ;
- les cinq fichiers PTX de `Clarisse\ptx\`, dont `implicit_geometries.cu.ptx`,
  et la chaîne de dépendances `widget_3d_view.dll → optix.6.5.0.dll` ;
- `SysThreadTaskManager::add_generic_task` et son accès depuis `OfApp`.

**Supposé, et à vérifier avant de s'engager :**

- que `paint_gl` soit **appelée** sur un `ModuleGeometry` tiers — non vérifiable
  sans session interactive ;
- qu'un compute shader GL 4.3 fonctionne dans le contexte de compatibilité de
  Clarisse sans dérégler le viewport ;
- qu'une géométrie tierce retombe sur le chemin CPU du 3D View
  (`is_gpu_compliant()` existe, on n'a pas lu ce qu'il teste) ;
- que le découpage par primitive redevienne gagnant sur des grappes séparées ;
- le gain d'AVX-512 sur cette génération de processeur ;
- le comportement au-delà de ~500 primitives.

**Lu dans une source primaire, jamais reproduit ici :** tous les chiffres des §1
à §4 — le talk d'Alex Evans (145 planches, notes du conférencier comprises), les
papiers de Hart, Keinert, Galin, Keeter, Seyb, Museth, Knoll, les articles de
Quilez, la documentation d'OpenVDB, de Houdini, d'Adobe, de Clayxels et d'OTOY,
et le HTML de la page officielle de MagicaCSG.

**Ce qui n'a pas été trouvé, et qui manquerait pour décider finement :**

- **les internals de MagicaCSG** — résolution de volume en voxels, algorithme du
  path tracer, nombre maximal de strokes, benchmarks. Ephtracy n'a jamais rien
  publié, et le changelog détaillé vit derrière Patreon, protégé par CAPTCHA ;
- l'unité du rayon de mélange et la sémantique exacte du mode « Avoid » ;
- **la technologie de rendu de Womp** — aucune source publique ne dit WebGL,
  WebGPU, ray marching ni SDF ; leur communication ne parle que de « cloud » ;
- les valeurs numériques de résolution de Substance 3D Modeler, et le détail de
  son pipeline d'export ;
- les défauts de paramètres du nœud `implicit` d'Arnold et les paramètres du ray
  marcher de Vectron — OTOY documente l'API OSL, pas l'algorithme ;
- **aucun benchmark publié ne compare sphere tracing analytique et intersection
  VDB sur la même scène.**

**Une correction de source, pour que personne ne la refasse** : plusieurs
résumés en ligne attribuent à MagicaCSG un « stockage en textures volumiques
rendu par sphere tracing ». La phrase vient d'un article Unreal de Ryan Brucks,
pas d'ephtracy. Et le talk « Umbra to Ubiquity » de Media Molecule n'existe pas :
la présentation d'Umbra Ignite 2015 est le même contenu que SIGGRAPH 2015.

---

## 11. Sources

**Clarisse** — tout vient de l'installation locale et du SDK reconstruit :
`J:\Clarisse-SDK\include\`, `J:\Clarisse-SDK\lib\*.def`,
`C:\Program Files\Isotropix\Clarisse 5.0 SP14\Clarisse\`.

**Documents du projet** — `csg-clarisse.md` (le point d'entrée non tessellé, les
quatre voies du CSG, les licences des booléens), `moteur-gpu.md` (pourquoi aucun
moteur externe ne peut exécuter le shading de Clarisse), `sdk-clarisse.md` (les
pièges du SDK reconstruit, l'invocation de `cnode`).

**Sphere tracing et champs de distance**

- Hart, *Sphere Tracing: A Geometric Method for the Antialiased Ray Tracing of
  Implicit Surfaces*, The Visual Computer 12(10), 1996 —
  <https://graphics.stanford.edu/courses/cs348b-20-spring-content/uploads/hart.pdf>
- Keinert, Schäfer, Korndörfer, Ganse, Stamminger, *Enhanced Sphere Tracing*,
  STAG 2014 — <https://diglib.eg.org/handle/10.2312/stag.20141233.001-008>
- Galin, Guérin, Paris, Peytavie, *Segment Tracing Using Local Lipschitz
  Bounds*, CGF 39(2), Eurographics 2020 —
  <https://aparis69.github.io/public_html/projects/galin2020_Segment.html>
- Barbier et al., *Lipschitz Pruning*, CGF 44(2), Eurographics 2025 —
  <https://wbrbr.org/publications/LipschitzPruning/documents/LipschitzPruning_submitted_to_EG25.pdf>
- Quilez, *Smooth minimum* — <https://iquilezles.org/articles/smin/> ·
  *Distance functions* — <https://iquilezles.org/articles/distfunctions/>
- Frisken, Perry, Rockwood, Jones, *Adaptively Sampled Distance Fields*,
  SIGGRAPH 2000 —
  <https://graphics.stanford.edu/courses/cs468-03-fall/Papers/frisken00adaptively.pdf>
- Museth, *VDB: High-Resolution Sparse Volumes with Dynamic Topology*, ACM TOG
  32(3), 2013 — <https://www.museth.org/Ken/Publications_files/Museth_TOG13.pdf>
- Keeter, *Massively Parallel Rendering of Complex Closed-Form Implicit
  Surfaces*, SIGGRAPH 2020, ACM TOG 39(4) —
  <https://www.mattkeeter.com/research/mpr/> · libfive —
  <https://github.com/libfive/libfive>
- Seyb, Jacobson, Nowrouzezahrai, Jarosz, *Non-linear sphere tracing for
  rendering deformed signed distance fields*, SIGGRAPH Asia 2019, TOG 38(6) —
  <https://cs.dartmouth.edu/~wjarosz/publications/seyb19nonlinear.html>
- Knoll, Hijazi, Kensler, Schott, Hansen, Hagen, *Interactive Ray Tracing of
  Arbitrary Implicits with SIMD Interval Arithmetic*, RT 2007 —
  <https://www.sci.utah.edu/~knolla/rtia-rt07.pdf>
- Quilez, *Distance functions* —
  <https://iquilezles.org/articles/distfunctions/> · *SDF repetition* —
  <https://iquilezles.org/articles/sdfrepetition/> · *SDF bounding volumes* —
  <https://iquilezles.org/articles/sdfbounding/>

**Modélisation SDF grand public**

- Evans, *Learning from Failure* (Dreams), Advances in Real-Time Rendering,
  SIGGRAPH 2015 —
  <https://advances.realtimerendering.com/s2015/AlexEvans_SIGGRAPH-2015-sml.pdf>
  · la même présentation à Umbra Ignite 2015 —
  <https://www.youtube.com/watch?v=u9KNtnCZDMI>
- MagicaCSG, page officielle et changelog —
  <https://ephtracy.github.io/index.html?page=magicacsg> · source HTML de la
  table des fonctions —
  <https://raw.githubusercontent.com/ephtracy/ephtracy.github.io/master/magicacsg.html>
- Qualité de l'export MagicaCSG, mesures d'un artiste —
  <https://siegetower.pages.dev/lounge/2024-08-30-experimenting-with-magicacsg-exporting-to-blender>
- SDF Modeler (Sascha Rode) —
  <https://www.cgchannel.com/2025/07/check-out-streamlined-free-3d-modeling-tool-sdf-modeler/>
- Womp, beta —
  <https://www.cgchannel.com/2024/05/free-browser-based-3d-modeling-app-womp-is-now-in-beta/>
- Clayxels, documentation officielle —
  <https://www.clayxels.com/uploads/4/7/2/7/47277855/clayxels_documentation.pdf>
  · FAQ — <https://www.clayxels.com/faq.html>
- Substance 3D Modeler, *How does Modeler work?* —
  <https://helpx.adobe.com/substance-3d-modeler/technical-support/how-does-modeler-work.html>

**Moteurs de production et grilles**

- OpenVDB, `Types.h` (demi-largeur de bande fixée à 3) —
  <https://github.com/AcademySoftwareFoundation/openvdb>
- Houdini, `VDB Combine` —
  <https://www.sidefx.com/docs/houdini/nodes/sop/vdbcombine.html> ·
  `VDB Reshape SDF` —
  <https://www.sidefx.com/docs/houdini/nodes/sop/vdbreshapesdf.html> ·
  `SDF Blend COP` (2D) —
  <https://www.sidefx.com/docs/houdini/nodes/cop/sdfblend.html>
- OTOY, Vectron —
  <https://docs.otoy.com/standaloneSE/Vectron.html> · Volume SDF —
  <https://docs.otoy.com/standaloneSE/VolumeSDF.html>
