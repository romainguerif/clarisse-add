# Passation — où en est ClarisseAdd

Écrit le 2026-09-07, à la fin d'une session trop longue et deux fois compactée.
Mis à jour le 2026-09-08 au soir. **À lire en premier dans une nouvelle
session, et à relire en entier après chaque compaction** — un résumé de ce
document n'est pas ce document, et la journée du 8 en a produit deux.

Le bilan le plus récent est la section 0 bis. Elle dit ce qui est vrai
aujourd'hui ; les sections suivantes portent le contexte durable.

---

## 0. Ce qui a changé le 2026-09-08 — la priorité n'est plus l'intégrateur

Romain : « avant de faire le moteur de rendu, on va faire quelques petits trucs
plus simples, à commencer par le système de curves ». L'intégrateur (§5) et le
débruiteur (§6) restent valables mais passent derrière.

Quatre chantiers ouverts, chacun avec son document :

- **`curves-clarisse.md`** — des tubes, cordes, câbles et branches posés à la
  main, vus de près, sur lesquels on puisse scatter. **Quatre modules livrés et
  vérifiés au rendu** : `tube` (tubes, cordes à torons, coudes, chaînette,
  trim), `curve_points` (distribution et embouts), `curve_deform`, plus la
  plume `pen` — celle-là reste à essayer en interactif, un outil ne se teste
  pas dans `cnode`.
- **`npr-clarisse.md`** — une boîte à outils NPR pour beaucoup de styles, à
  partir de quatre couvertures de romans données comme cibles. Aucun code, mais
  l'inventaire est fait et il change la donne : `SubPixelFilterOutline` existe
  déjà et fait les quatre types de contours.
- **`cloth-clarisse.md`** — un solveur de tissu XPBD réutilisable
  (`native/common/cloth_solver.h`) et son premier client, `GeometryClothPanel`, qui
  transforme chaque polygone d'un maillage en coussin capitonné. C'est une vraie
  simulation, pas un bombement peint : Romain l'avait demandé explicitement.
  Le document liste les six erreurs de modélisation qui ont chacune produit une
  image plausible et fausse — elles se reposeront à l'identique au prochain node
  de simulation. **À lire avant d'écrire quoi que ce soit qui simule.**

  Deux leçons en sortent qui dépassent le tissu. La première : écrire une sonde
  qui lit le résultat dans le maillage plutôt que de juger au rendu — juger au
  rendu est lent, cher, et ne dit pas de combien on se trompe. La seconde : une
  contrainte métrique impossible ne produit pas « un peu de tension », elle
  produit un artefact ; ce qu'on veut obtenir géométriquement doit être mis dans
  la forme de repos.

- **`sdf-clarisse.md`** — la suite de `csg-clarisse.md` : le sphere tracing d'un
  champ de distance **mélangé** rend dans Clarisse, sonde à l'appui, et coûte
  2,5 fois l'intersection analytique. Contient la réponse mesurée à la question
  du GPU. Trois acquis qui se reposeront ailleurs : **Clarisse appelle
  `intersect_primitive` avec 1,41 rayon en moyenne** — donc aucun lancement de
  noyau GPU par rayon n'est envisageable, jamais ; **`ix_glutils.lib` réexporte
  GLEW en entier jusqu'à OpenGL 4.6, compute shaders compris**, vérifié à
  l'édition de liens, donc il y a un chemin GPU dans le viewport sans CUDA ; et
  **OptiX ne sert pas qu'au débruiteur — le 3D View est un raytracer OptiX
  complet**, avec ses PTX d'intersection implicite précompilés et fermés.

**Une correction importante est tombée ce jour-là**, et elle vaut pour tout le
projet : la règle « on ne dérive que des classes abstraites » est **fausse**.
Ce qui compte est qu'un module porte le nom de la classe
(`snake_case(Classe).dll` dans `module/`). `sdk-clarisse.md` est corrigé. Ça
ouvre `GeometryFur`, `GeometryParticle`, `GeometryVolume`, `SceneObjectTree`,
`Deformer`, `Locator`, `Displacement` — sondés et vérifiés.

Deuxième correction, à porter dans `J:\Clarisse-SDK\RECONSTRUCTION.md` : les 19
en-têtes de `module/` réputés bloqués échouaient sur `gmath_transform.icc`, pas
sur `geometry_sample.icc`, et **les corps inline perdus d'une classe marquée
`X_EXPORT` sont récupérables depuis la DLL à l'édition de liens**. Le passif
réel des 29 stubs est donc bien plus petit qu'annoncé.

Enfin : **209 Mo de PDF de recherche** (aquarelle, encre, NPR) sont dans
`J:\Clarisse-SDK\papers\`, hors dépôt. Ils ne sont pas retrouvables en ligne
sans effort — ne pas les effacer.

---

## 0 bis. Bilan du 2026-09-08 au soir

> **Si tu lis ceci apres une compaction, relis-le en entier, et relis aussi les
> documents qu'il cite plutot que leur resume.** Cette journee a ete longue et
> deux fois compactee ; ce qui suit est ce qui reste vrai, pas ce dont on se
> souvient. Chaque fois qu'un resume et un document se contredisent, c'est le
> document qui a raison -- il a ete ecrit en regardant le code.

### Ce qui a ete livre aujourd'hui

Tout compile, tout se charge dans `cnode`, et tout ce qui est marque « rendu » a
ete regarde a l'image.

| Node | Etat |
|---|---|
| `GeometryClothPanel` (ex `GeometryQuilt`) | **rendu.** Passe au modele des solveurs de production : force de pression, depart a plat, tissu agrandi, compression libre, vraie dynamique. Trente plis par coussin, invariant d'echelle verifie sur un facteur mille. |
| `GeometrySelect` | **verifie par les nombres.** Selection de faces par regle -- texture, normale, aleatoire -- plus une liste explicite, sortie en shading group, chainable. |
| `ToolFaceSelect` | **se charge, jamais manipule.** Outil de viewport : survol, clic, Maj, Ctrl, peinture. Deux bugs de pointage deja trouves et corriges *avant* tout essai, par un test d'aller-retour. |
| `GeometrySdf` | **rendu.** Modelisation par champ de distance : sept primitives, trois operations, raccord arrondi par ligne, pile ordonnee, groupes. |
| `GeometryTube` | **rendu.** Gagne les gouttelettes -- le rayon enfle en chapelet, profil de sphere, largeur deduite pour que la goutte soit ronde. |
| `ImageFilterBokeh`, `CameraBokeh` | **mesures.** Les coutures de tuile sont corrigees : le saut d'une colonne a la suivante passe de cinquante-cinq fois le bruit de fond a un. |

Quatre documents de recherche sont tombes le meme jour :
`cloth-clarisse.md`, `bokeh-clarisse.md`, `csg-clarisse.md`, `fluent-audit.md`,
`sdf-clarisse.md`. Ils portent les mesures ; ce fichier-ci ne porte que l'etat.

### Ce qui attend Romain, dans Clarisse ouvert

Trois choses, parce que `cnode` n'a pas de viewport et qu'on ne lance jamais
`clarisse.exe` soi-meme.

1. **Essayer `ToolFaceSelect` et `ToolCurvePen`**, categorie *Create*. Il faut le
   raccourci **« Clarisse iFX 5.0 SP14 (ClarisseAdd) »** cree sur le bureau : un
   outil arrive par `scan_modules` n'entre jamais dans une barre d'outils deja
   construite. C'est verifie dans le journal de demarrage.
2. **La selection en orthographique.** Le correctif est raisonne, pas mesure :
   `project_point` de Clarisse est une projection perspective, il n'existe pas de
   reference independante hors d'un viewport. La perspective, elle, est prouvee a
   6e-15 pres par `native/tests/ray_roundtrip.cpp`.
3. **La sonde `paint_gl`**, une demi-journee. Elle decide de tout l'apercu GPU.
   Voir `sdf-clarisse.md` §7.4.

### Le chantier en cours : la toile d'araignee

Romain veut des images d'art de toiles. **La recherche est finie et tient en
deux documents**, qui font autorite sur tout ce qui suit -- si un resume les
contredit, ce sont eux qui ont raison :

- **`toile-clarisse.md`** : les deux references de Romain (de la fibre etiree
  en macro, pas une toile) et l'integration dans Clarisse ;
- **`toile-orbitele.md`** : la vraie toile d'araignee, mesuree. Litterature
  primaire, chaque nombre avec son URL. **Sa page de garde liste les neuf
  points qui changent ce qu'on allait ecrire, et sa derniere section donne la
  fiche des valeurs d'usine du node.** Lire ces deux morceaux suffit pour
  commencer ; le corps est une reference ou l'on revient.

Ce qui est deja decide, et qui ne changera pas :

- **Ses references ne sont pas des toiles tissees.** C'est de la fibre etiree.
  Une araignee construit fil par fil avec un plan ; ca, c'est une masse de fibres
  qu'on a tiree. Les deux se modelisent tres differemment.

  Trois corrections a ma premiere lecture, toutes mesurees sur les images et
  toutes consignees dans `toile-clarisse.md` -- je les note ici parce qu'elles
  changent la recette, pas seulement la description :

  - **les fibres ne sont pas paralleles.** Isotropie mesuree a 0,988 sur 1,000,
    et par blocs de cent pixels le pic local median ne depasse le plat que de
    moitie. Ce que l'oeil lit comme du parallelisme, ce sont de **rares faisceaux
    saillants sur un fond isotrope** -- on ne fabrique pas un fond isotrope comme
    on fabrique un peigne ;
  - **trois echelles et non deux**, et le « cable » est un **ruban plat de fibres
    paralleles**, pas un cylindre. Un tube ne le fera donc pas ;
  - chaque fibre porte une **frisure periodique**, signature de l'ouate
    polyester. Je ne l'avais pas nommee et elle se voit.
- **`GeometryWeb`**, deux modes : *orbitele* (cadre, moyeu, rayons, spirale a pas
  variable) et *enchevetre*. La regle qui fait tout dans le second : **un fil peut
  s'accrocher a un autre fil**, pas seulement a un ancrage. C'est de la que
  viennent les V, les points de traction et l'aspect recursif.

  **Le mode orbitele se construit dans l'ordre de l'araignee**, parce que c'est
  cet ordre qui produit les irregularites justes : cadre, puis rayons du haut
  vers le bas -- chacun insere **sous** le precedent, et c'est le point
  d'attache au cadre qui decide de l'angle au moyeu --, puis le moyeu, puis une
  auxiliaire **logarithmique** du centre vers le bord, puis la spirale de
  capture **arithmetique** du bord vers le centre. Les valeurs d'usine :
  **32 rayons** (le compte ne depend pas de la taille de l'araignee, le
  diametre si), **13,2 degres d'angle en haut contre 8,4 en bas**, moyeu sur
  les dix premiers pour cent du rayon, zone libre jusqu'a trente, capture
  au-dela, **pas de 5,6 mm** croissant vers le haut et constant vers le bas,
  **plus ou moins trente pour cent d'un tour a l'autre**.

  **Trois reglages independants**, a ne pas fusionner : la forme generale de la
  toile, l'asymetrie d'extension (le bas 1,3 a 1,4 fois plus grand) et
  l'asymetrie angulaire -- cette derniere **survit a une extension nulle**,
  chez 92 araignees sur 93.

  **Et il faut du desordre de topologie** : **trente-cinq anomalies par toile**,
  dont des rayons devies (84 % des toiles), des rayons en Y (45 %) et des
  rayons surnumeraires (15 %). Un bruit ajoute apres coup ne les remplace pas.
- **La relaxation reutilise `cloth_solver.h`.** Une toile est un reseau
  masse-ressort, exactement ce que ce solveur sait faire : ancrages epingles,
  fils en contraintes de distance, un peu de gravite -- et les tensions et les
  coudes aux jonctions apparaissent au lieu d'etre dessines. C'est le deuxieme
  client du solveur, et c'est ce qu'on annoncait en l'ecrivant.

  **Les raideurs relatives sont mesurees et il ne faut pas les inventer** :
  amarrage, cadre et rayon sont dans un rapport de tension **10 : 7 : 1**,
  identique chez quatre especes donc independant de la geometrie, et la spirale
  est a **un dixieme du rayon**. Ce sont les compliances a cabler.

  **Attention au piege inverse** : une spirale de capture reelle **n'a jamais
  de mou**, parce que le treuil capillaire enroule l'excedent de fil dans la
  gouttelette a tension constante. Si notre relaxation sort des spirales molles,
  c'est un artefact et pas du realisme.
- **L'ancrage ne demande pas de nouveau node.** Verifie : Clarisse possede
  `TextureCurvature` (les aretes saillantes) et `TextureOcclusion` (les recoins
  abrites) -- exactement les deux criteres ou une araignee accroche. Avec le
  scatterer natif et notre `GeometrySelect`, l'ancrage est deja exprimable.
- **La question des courbes natives est tranchee**, et bien mieux qu'espere.
  `GeometryFurGenerator` et `GeometryFurInterpolate` **acceptent une geometrie
  produite par un module tiers** -- leur filtre porte sur `Geometry` et non sur
  `GeometryPolymesh`. Notre `GeometryCableField` passe et monte a **371 907
  courbes en 0,03 seconde**. Mieux : `CurveMesh` a un constructeur et un `init()`
  publics et exportes, donc un module a nous peut fabriquer des courbes natives
  de bout en bout -- il faudra lier `ix_curve`, que `build.py` ignore encore.

  Mais **la fourrure ne fera pas le voile** : un poil est libre a une extremite,
  ce qui donne un duvet, alors que les fibres de la reference **relient deux
  ancrages**. Le voile releve du modele « cable entre deux points », pas du
  modele « poil ». Verifie au rendu.

  Au passage, `GeometryBundle` **n'agrege rien** : c'est un lecteur Alembic et
  USD. Le vrai agregateur est `SceneObjectCombiner`. Ma supposition etait fausse.
- **Sur le tube**, les perles sont faites. Restent le *clumping* des torons, les
  fibres echappees, l'attenuation aux extremites -- un fil casse s'affine, il ne
  s'arrete pas net -- et la frisure periodique.

  **Deux corrections mesurees a ce que j'ai livre**, toutes deux dans
  `toile-orbitele.md` : les gouttes reelles ne sont **pas rondes** mais une fois
  et demie plus longues que larges (rapport mesure **1,2 a 1,65**, moyenne
  **1,35**), donc la largeur deduite automatiquement doit viser 1,35 et non 1 ;
  et **elles alternent grosses et petites**, la secondaire faisant de 2 % a
  59 % de la primaire selon l'espece. Enfin l'espacement : **28 a 35 fois le
  rayon du fil**, et non les **neuf** que donne la formule de Rayleigh non
  visqueuse -- un reglage cale sur neuf donne des perles trois fois trop
  serrees.

- **Une contre-intuition a retenir avant d'ecrire quoi que ce soit.** Un fil de
  soie sec est **rigoureusement droit** : sa fleche vaut sept millieme de pour
  cent de la portee. Ce n'est pas la gravite qui le plie, c'est la rosee, et elle
  le plie en **polygone funiculaire** -- des segments droits entre les gouttes --
  et non en chainette. C'est l'inverse exact de ce que fait notre node de cable,
  et c'est ce qui distingue une toile d'un cable mou.

- **Deux pieges mesures** : le semis en bruit bleu rend **exactement la moitie**
  du compte demande, quelle que soit la surface, alors que les trois autres
  distributions sont exactes ; et une `Image` sans `Layer3d` rend un cadre noir
  sans le moindre message.

### Le node SDF : la suite

Ordonnee, et detaillee dans `sdf-clarisse.md` §9.1.

1. **Les UV** par projection triplanaire. L'attribut `uv_mode` a ete **retire**
   parce que le code ne le lisait pas ; `uv_scale` agit.
2. **Une matiere par forme**, demande explicitement. Question a trancher avant
   d'ecrire : une primitive Clarisse par forme -- mesure a trois fois le cout --
   ou un shading group variable au fragment.
3. **La polygonisation** par dual contouring adaptatif depuis le champ
   analytique. C'est elle qui permettra un panneau de tissu sur une forme CSG.
   On peut faire mieux que MagicaCSG ici : il polygonise depuis une grille de
   voxels, nous interrogeons la formule exacte, donc les aretes vives se
   reconstruisent au lieu d'etre rabotees.
4. **La retopologie se delegue.** Instant Meshes est installe. En **processus
   separe seulement**, jamais lie, licence a verifier, et sur un geste
   volontaire -- pas pendant l'evaluation de la geometrie.

### Les pieges trouves aujourd'hui, a ne pas redecouvrir

Ils ont tous coute du temps, et aucun ne se devine.

- **Une colonne de table ne peut pas etre de type tableau.** Clarisse l'ecrit
  dans son journal et l'attribut devient **invisible dans l'editeur**. Trois
  colonnes scalaires a la place.
- **Chaque colonne d'une table se dimensionne separement.** N'en dimensionner
  qu'une laisse les autres a leur defaut, et les ecritures hors plage passent en
  silence.
- **`source` est un mot reserve** dans un CID.
- **`GeometryMediumDescriptor` doit etre fourni a la main**, avec l'opacite a un.
  A zero, rien ne s'affiche -- sans silhouette, sans alpha, sans message.
- **`NOMINMAX` avant `windows.h`**, sinon `gmath_vec2.h` casse sur une erreur de
  syntaxe qui ne parle de rien.
- **`GMathViewPoint::get_eye_position`**, pas `get_translation`. Et en
  orthographique l'etendue se mesure **a la distance de pivot**, pas a une unite.
- **Les modificateurs clavier sont sur `keyboard`**, meme pendant un evenement
  souris.
- **`GMathVec3d v(double(x), ...)` est une declaration de fonction**, pas une
  construction. `static_cast` leve l'ambiguite.
- **Interroger un attribut absent ecrit un avertissement a chaque evaluation** :
  demander la classe de l'objet d'abord.
- **La barre d'outils est construite avant le script de demarrage** : un outil
  n'y entre que si son module est charge par `-module_path`.

### La methode qui a marche, et qu'il faut reprendre

- **Ecrire une sonde qui mesure dans les donnees plutot que juger au rendu.**
  C'est ce qui a debloque le tissu apres une demi-journee perdue, et c'est ce qui
  a trouve les deux bugs de l'outil de selection avant meme qu'il soit essaye.
  Le rendu coute cher et ne dit pas *de combien* on se trompe.
- **Verifier plutot que supposer.** Les pixels noirs le long des coutures : lus
  un par un, opaques, donc de la geometrie et pas un trou. Le rayon de l'outil :
  un aller-retour arithmetique, exact en perspective, faux de cinq unites en
  orthographique.
- **Ne jamais declarer un reglage que le code ne lit pas.** Un curseur qui ne
  fait rien est pire qu'un curseur absent : on le tourne, rien ne bouge, et on
  cherche ailleurs. Deux ont ete retires aujourd'hui pour cette raison.

### La direction, pour ne pas la perdre

Romain veut **construire des scenes de A a Z dans Clarisse**. Pas tout, mais une
bonne base : le CSG pour les volumes, les courbes pour les cables, le tissu pour
les panneaux, la toile pour la matiere. Les trois chantiers convergent vers le
meme genre d'image -- machines a modules repetes, capsules et cylindres,
raccords arrondis courts, panneaux capitonnes.

**Le risque principal n'est pas mathematique, il est ergonomique.** Clarisse n'a
aucun outil de modelisation, et attraper une forme a la souris pour la deplacer
en voyant le resultat en direct est ce qui coutera le plus cher. Le node SDF
contourne le probleme en prenant la transformation d'un locator : l'outil de
deplacement habituel fait le travail, et il n'y a pas de manipulateur a ecrire.
C'est un contournement qui tient longtemps, pas une solution definitive.

---

## 1. Avec qui on travaille

Romain Guerif, artiste VFX, **expert Clarisse depuis dix ans**. Il connaît le
moteur et ses faiblesses par cœur — c'est précisément pourquoi il veut le
modifier.

Quatre consignes non négociables, répétées :

- **Ne pas lui donner de commandes à taper.** Installations, compilations, tests
  et rendus se lancent soi-même. Lui remettre un bloc de shell est un échec.
  L'exception est une action refusée par le classificateur de permissions — là,
  il faut expliquer et le laisser décider.
- **Le rendu coûte de l'argent.** « 400 yens pour une heure de rendu mec ». Un
  test qui tranche vaut mieux que cinquante qui rassurent.
- **Ne jamais toucher `clarisse.exe`.** C'est sa session interactive. Les rendus
  de test passent par `cnode.exe`. Un `taskkill` trop large la lui a déjà fermée.
- **Utiliser des agents** pour les recherches longues et les inventaires.

Et une cinquième, apprise à ses dépens ce soir : **ne pas lui redemander de
prouver ce qu'il sait.** J'ai proposé trois fois de mesurer le bruit de
l'indirect avant de commencer ; il a fini par répondre « pas besoin de test,
arrête avec ça ». Quand il énonce un fait sur Clarisse, c'est une donnée
d'entrée. Et j'ai aggravé le cas en lui expliquant que `Diffuse Depth = 1`
plafonnait son énergie — c'est un champ qu'il pousse à chaque rendu. **Ne pas
confondre valeur par défaut et valeur utilisée.**

Il travaille en français.

---

## 2. Le contexte matériel

- Clarisse **iFX 5.0 SP14**, `C:\Program Files\Isotropix\Clarisse 5.0 SP14\`.
  Logiciel arrêté, **aucun SDK officiel**.
- SDK reconstruit hors ligne dans `J:\Clarisse-SDK\` — en-têtes + doc de
  référence HTML (`docs/reference/technical/*.html`, qui contiennent les CID
  complets avec les valeurs par défaut).
- Dépôt : `C:\Users\Anon\Desktop\ClarisseAdd`, branche `main`, propre au commit
  `904387f`.

**Piège machine à connaître** : `python3` n'existe pas sur ce poste, seulement
`python`. Un `python - <<EOF` en arrière-plan ouvre le REPL interactif, part en
boucle d'erreur et écrit ~1 Go/h dans le fichier de tâche. C'est arrivé ce soir
— 635 Mo. Utiliser `python -c`, un fichier de script, ou du grep. Les heredocs
bash cassent aussi les échappements `\n` dans les littéraux C++ : passer par
Write/Edit.

Autre chose à savoir : les fichiers `tasks/<id>.output` d'un sous-agent ne
contiennent **que le prompt de lancement**, jamais l'avancement. Un agent en
cours et un agent terminé y sont identiques. Pour l'état d'un agent, c'est
`ListAgents`.

---

## 3. Ce qui est construit

Addon shelf devenu addon C++. Neuf modules natifs dans `native/` : l'optique
(`bokeh`, `bokeh_camera`, `chroma`), le témoin `hello`, et les courbes
(`tube`, `curve_points`, `curve_deform`, `pen`), plus `common/` et
`build.py`.

Chaîne de compilation : `.cid` → `cmagen.exe` → `.cma` → `.cpp` → `.dll`.

**Contrainte structurante** : `cmagen` déduit le nom du module de celui de la
classe de base et le cherche dans `module/`. La liste des bases dérivables est
donc **celle des 134 DLL de ce dossier** — et non celle des classes abstraites,
comme on l'a cru jusqu'au 2026-09-08. Voir §0.

---

## 4. Les documents, et ce que chacun couvre

| document | contenu |
|---|---|
| **`curves-clarisse.md`** | **Chantier en cours.** Le système de courbes de Clarisse (il s'appelle « Fur »), pourquoi on ne passe pas par lui, la preuve que le chemin polymesh se lie, l'état des quatre modules, les pièges d'API, et ce que dit l'état de l'art des générateurs de câbles. |
| **`npr-clarisse.md`** | Le NPR : les quatre images cibles, les quatre axes, l'inventaire de ce que Clarisse a déjà (beaucoup), le dossier simulation d'encre, l'ordre de travail. |
| **`csg-clarisse.md`** | **Étude de faisabilité du CSG**, écrite le 2026-09-08. Clarisse n'a aucun booléen — vérifié sur les 359 classes. Mais `GeometryObject` est une interface ouverte qui ne mentionne jamais un polygone : la sonde `native/csg_sonde/` rend trois booléens analytiques sans un seul sommet, et le scatterer les instancie. Quatre voies comparées, une recommandation, et une sonde d'une demi-journée qui décide de tout le reste. **Deux points sont corrigés par `sdf-clarisse.md` : le découpage en une primitive par feuille, et « aucun moteur de production ne fait ça ».** |
| **`sdf-clarisse.md`** | **MagicaCSG, les champs de distance, et la question du GPU**, écrit le 2026-09-08. Le sphere tracing avec mélange doux **rend dans Clarisse** — sonde, images et chiffres. L'état de l'art (MagicaCSG discrétise, comme Dreams, comme tout le monde) et les mesures GPU/CPU sur cette machine. **Réponse au GPU : pas de noyau par rayon (mesuré, 1,41 rayon par appel contre 20 µs de latence), mais `ix_glutils.lib` réexporte GLEW jusqu'à GL 4.6 avec les compute shaders — vérifié à l'édition de liens.** |
| **`toile-orbitele.md`** (736 l.) | **La toile d'araignée réelle, mesurée**, écrit le 2026-09-08. Littérature primaire, chaque nombre avec son URL : séquence de construction, comptes de rayons, pas de spirale par espèce, asymétries, diamètres et mécanique des soies, gouttelettes de glu, tensions. **Sa page de garde liste les neuf points qui corrigent ce qu'on allait écrire ; sa dernière section est la fiche des valeurs d'usine de `GeometryWeb`.** Le résultat le plus contre-intuitif : **un fil sec ne fait pas de chaînette**. |
| **`toile-clarisse.md`** (405 l.) | Les deux références de Romain — de la fibre étirée en macro, pas une toile tissée — mesurées image par image, et l'intégration Clarisse. Corrige trois de mes lectures à l'œil : les fibres ne sont **pas** parallèles (isotropie 0,988), il y a **trois** échelles, et le « câble » est un **ruban plat**. |
| **`cloth-clarisse.md`** (495 l.) | Le panneau de tissu : XPBD, le modèle de Fluent, la sonde laplacienne qui a débloqué la qualité, et pourquoi la pression est une **force** et non une contrainte de volume. |
| **`fluent-audit.md`** (1164 l.) | Audit du plugin Fluent : ce qu'il fait, comment, et lesquels de ses outils se portent dans Clarisse. |
| **`bokeh-clarisse.md`** (479 l.) | Le nœud et la caméra bokeh, et la mesure des coutures de tuile. |
| **`sdk-clarisse.md`** (642 l.) | **La référence vivante.** Système de modules, contrainte cmagen, chargement sans action utilisateur, pièges à crash, langage CID, contrat `CtxKernelFilter` mesuré, accès aux AOV, générateur de rayons caméra, saveurs de licence, mensonges de l'API Python, invocation de `cnode`, recettes EXR. **À lire avant d'écrire du C++.** |
| **`integrateur-clarisse.md`** (1288 l.) | Le gros dossier du soir. Voir §5. |
| **`optique-etat.md`** (397 l.) | État des trois nœuds d'optique, inventaire des paramètres, les deux chemins de flou, et quatre défauts trouvés en le rédigeant. |
| **`texturing-plan.md`** (574 l.) | Préparation décales / bake / paint. Le décal dérive de `TextureSpatial`. Le bake existe déjà dans Clarisse, il ne reste qu'à le tester. |
| **`clarisse-threejs.md`** (311 l.) | Clarisse comme interface d'auteur pour three.js. Principe directeur de Romain : **miroir, pas traduction** — « on fera les matériaux threejs disponibles dans clarisse, pour que les paramètres soient les mêmes ». |
| **`moteur-gpu.md`** (419 l.) | Pourquoi aucun moteur externe ne peut exécuter le shading de Clarisse. Conclusion : GPU et « tout mon shading marche » sont exclusifs. LuxCore porte du GPLv3 compilé malgré son annonce Apache. |
| **`architecture.md`**, **`project-format.md`** | L'addon et le format de projet. |

---

## 5. Où on en est : l'intégrateur

C'est le chantier en cours. Six rapports de recherche condensés dans
`integrateur-clarisse.md`. Les points à retenir :

**L'idée.** Écrire notre propre `Integrator` — l'algorithme de transport de
lumière — et le brancher dans Clarisse, qui garde géométrie, BVH, instanciation
et shading. C'est le seul morceau qu'Isotropix a rendu enfichable.

**Les cinq trous trouvés dans Clarisse :**
1. Le sampler Sobol **n'est pas scramblé** — `samplef(index, dim)` sans le
   troisième argument. Owen ferait passer le RMSE de 0,0048 à 0,0017.
2. La machinerie de **light cuts est compilée hors du binaire** —
   `//#define PBR_WEIGHTED_SAS_OBJECT_USE_TREE_CUT`.
3. Chaque lumière porte un `sample_count` de 16, plus des multiplicateurs par
   matériau — le modèle à N boutons que 3Delight a abandonné.
4. Le path tracer natif fait déjà beaucoup (clamping, régularisation, filtrage
   de fireflies) : à égaler avant de prétendre le battre.
5. **L'échantillonnage adaptatif est écrit et éteint** —
   `Refinement Maximum Sample Count` affiche littéralement « Refinement Sampling
   Disabled » dans l'interface, sur le nœud `path_tracer`, groupe Sampling.

**Le premier test à écrire, avant toute ligne d'algorithme** : `cb_pre_render`
reçoit un `const CtxShader&`, donc pas de `new_raytrace_ctx()`. Si on ne peut
pas tracer de rayons là, toute la famille photon/caustiques tombe.

**L'ordre retenu** : allumer l'adaptatif (zéro code) → instrumenter la sélection
de lumières → l'intégrateur miroir → MIS compensation → fondations sans état
(clamp L1, RR par albédo, plancher de rugosité) → G-MoN → sampler Owen → light
BVH → guidage seulement si les mesures le justifient.

**Décision de conception prise ce soir.** Suivre 3Delight sur le nombre de
boutons, mais en distinguant deux familles : les **boutons de budget** fusionnent
en un seul (« Quality »), les **toggles d'algorithme** ne sont pas des réglages
utilisateur — allumés et `hidden yes` dans le CID. Ce qui reste public :
Quality, Path Depths, Clamp + profondeur, et surtout **Ground Truth**, une case
qui débraye tout ce qui est biaisé. Sans ce bouton, chaque artefact de chaque
rendu devient la faute de notre intégrateur — c'est la leçon du *shading LOD* que
Disney a fini par supprimer.

---

## 6. Le fil ouvert : le débruiteur

**Romain : « le denoiseur là pour l'instant c'est très vieux ce qui est présent
dans Clarisse. »** Vérifié : `optix.6.5.0.dll`, sortie 2019, dernière version
avant la réécriture de l'API en 7.0. **Aucun Intel OIDN dans l'install.**

Ce qu'on avait déjà établi et qu'il ne faut pas re-chercher :

- `KernelFilter` a quatre classes concrètes : DefocusBlur, **Denoiser**,
  GaussianBlur, UVEdgePadding.
- **`ImageFilterOptixDenoiser` dérive de `WholeImageFilter`** — image entière,
  pas de tuiles. **C'est la bonne classe de base pour un débruiteur, et elle
  n'est verrouillée dans aucune saveur de licence.**
- Le motif de déclaration d'AOV : `tag "albedo_AOV" { filter "aov_groups" }`,
  puis lecture du canal par son nom. **Le nom du canal n'est pas le nom de
  l'AOV** — c'est `groupe.composante`, donc `depth.Z` et pas `depth`.
- Les AOV doivent être **activés sur le Layer 3D** avant d'apparaître dans la
  liste déroulante du filtre.
- Un débruiteur sur la beauté seule marcherait tout de suite ; le mode qualité
  d'OIDN veut **albédo et normales** en entrée.

**Pourquoi ça mérite de passer devant l'intégrateur** : le chiffre de Disney est
que le débruiteur seul permet de rendre à **1/4 à 1/8 du spp** autrement
nécessaire. Aucune technique de tout le dossier intégrateur n'approche ce
facteur. Et on sait déjà écrire des filtres d'image — `bokeh.cpp` en est un.

Contrainte documentée d'OIDN à respecter : « weighted pixel sampling (splatting)
introduces correlation between neighboring pixels, which causes the denoising to
fail ». Bonne nouvelle, le filtre AA de Clarisse est en **Importance Sampling
par défaut**, ce que RenderMan documente justement comme le mode « needed by some
postprocessing algorithms such as denoisers ».

---

## 7. Ce qui est cassé et pas réparé

**Dans le bokeh :**
- Le composite en tranches **perd de l'alpha** dans la bande juste à l'intérieur
  de la silhouette d'un premier plan flou : la tranche de fond a un trou net. Et
  comme Clarisse **dé-prémultiplie les AOV par l'alpha de sortie**, ça corrompt
  l'AOV de profondeur — mesuré 251,10 là où le rendu brut donne 79,90, soit
  exactement `80 / 0,32`. Le correctif est identifié (ne jamais laisser la
  couverture descendre sous ce que donnerait un simple flou de l'alpha), pas
  écrit.
- La marge chromatique réservée dans `pre_filter` (`1 + 0,18·|chroma|`) est plus
  petite que l'échelle appliquée par `filter` (jusqu'à 1,6× avec le décalage par
  défaut et une dose négative).
- `focus_object` sur `CameraBokeh` est déclaré et déverrouillé mais **jamais
  lu**.
- Le flou longitudinal dans `chroma.cpp:358` **écrase** le décalage latéral au
  lieu de le composer.
- `_add_filter_class` ignore ses `title`/`note`, donc le bouton d'étagère chroma
  affiche le message du bokeh.

**Fonctions Peregrine encore absentes** : image d'ouverture personnalisée
(`Kernel Type = Input`), canaux de matte par effet.

**Noté pour plus tard, à sa demande** : un lecteur de fichiers **deep** et un
lecteur de **gaussian splats** pour Clarisse.

---

## 8. Les faits durement acquis, à ne pas redécouvrir

- **`depth.Z` est la profondeur projetée sur l'axe caméra**, pas la distance
  radiale — mesuré 12,72 contre 13,58. Pour viser un objet, il faut
  `extract_frame` puis produit scalaire sur l'axe de visée.
- **La profondeur des silhouettes vaut `couverture × z`** — antialiasing oblige.
  Corrigé en divisant par l'alpha.
- **Le CoC signé** est monotone en profondeur ; le non signé est en V et
  fusionnerait l'avant et l'arrière dans la même tranche.
- **Gather ≡ scatter seulement à rayon constant** — c'est toute la raison d'être
  des tranches correctives.
- `cnode -image "project://..."` ne rend rien en silence : le chemin est
  `build://project/<nom>`, et `-frames_list 1` est obligatoire.
- `magick` refuse les EXR à plus de 4 canaux — passer par `iconvert.exe` de
  Clarisse vers du TIFF.
- **Ne pas juger une image sur un montage réduit en `-auto-level`.** Ça m'a fait
  conclure à tort que les images tranchées étaient plus sombres. Mesurer les
  valeurs numériquement.

---

## 9. Mémoire

Quatre fichiers dans
`C:\Users\Anon\.claude\projects\C--Users-Anon-Desktop-ClarisseAdd\memory\` :
`clarisse-env`, `clarisse-addon-projet`, `romain-methode-travail`,
`piege-python-heredoc`. Ils sont à jour au 2026-09-07.
