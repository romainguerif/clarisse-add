# Passation — où en est ClarisseAdd

Écrit le 2026-09-07, à la fin d'une session trop longue et deux fois compactée.
Mis à jour le 2026-09-08. **À lire en premier dans une nouvelle session.**

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
