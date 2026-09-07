# Un moteur GPU pour Clarisse — état de la question

Document de réflexion, discussion du 2026-09-07. **Aucun code écrit, aucune
décision engagée.** Il consigne quatre recherches de fond pour que le
raisonnement ne se reperde pas.

La demande de départ : un moteur GPU dans Clarisse, open source, puissant,
rapide, **supportant les caustiques**, et pas trop pénible à intégrer. Périmètre
resserré en cours de route : **petites et moyennes scènes**, dans les limites de
la VRAM — les méga scènes restent au raytracer CPU de Clarisse.

---

## 1. Ce que l'architecture de Clarisse permet

Vérifié dans les en-têtes du SDK reconstruit et par sondes `cmagen`. Trois
portes, trois projets sans rapport.

### Porte 1 — la classe `Renderer` : impasse

`ModuleRenderer` n'expose que **trois callbacks**, tous déclaratifs :
`get_supported_lights`, `get_supported_geometries`, `get_supported_materials`.
Aucun point d'entrée de rendu. La classe est dérivable, mais c'est un
**conteneur de réglages**.

*(La doc d'Isotropix la décrit pourtant comme « classe de base de tous les
renderers, celui de Clarisse comme les moteurs externes intégrés dans
Clarisse » — l'intention existait, la boucle de rendu vit ailleurs.)*

### Porte 2 — l'`Integrator` : le vrai point d'extension conçu

Clarisse sépare le **Renderer** (échantillonnage, buckets, génération des
rayons) de l'**Integrator** (transport de lumière). L'intégrateur est un
véritable greffon :

```
cb_create_scene_data / cb_destroy_scene_data   donnees de scene persistantes
cb_get_las                                     structure d'acceleration propre
cb_pre_render / cb_post_render                 par rendu
cb_pre_bucket / cb_post_bucket                 par tuile
cb_pre_packet / cb_post_packet                 par paquet de rayons
cb_shade                                       LE transport de lumiere
cb_get_aov_list
```

Plus `ModuleIntegrator::register_model(ui_name, of_class)` et l'attribut
`integration_model` du `RendererRaytracer`. Clarisse livre lui-même trois
intégrateurs en DLL séparées (`integrator_pathtracer`,
`integrator_ambient_occlusion`, `integrator_utility`) — l'architecture est
réelle et utilisée.

Vérifié : `Integrator` est dérivable. Et la couche GAS est exposée
(`gas_scene_tree.h`, `gas_object.h`, `ctx_raytrace.h`), donc **un module peut
lancer ses propres rayons dans le BVH de la scène**.

**Ce que ça permet** : hériter gratuitement de toute la scène Clarisse —
géométrie, instanciation à l'échelle, matériaux, textures — et n'écrire que
l'algorithme. C'est la seule voie où « milliards d'instances » et
« caustiques » coexistent. **Mais c'est CPU** : le hook est appelé depuis les
threads de Clarisse, sur une structure d'accélération en mémoire hôte.

### Porte 3 — un `Layer` custom : la voie d'un moteur externe

`Layer` et `LayerScene` sont dérivables (`Layer3d` non — c'est un builtin, et
`cmagen` ne résout que les classes de base abstraites).

`ModuleLayer` n'a qu'une obligation : `get_image(objet, qualite, region)` rend
un `ImageCanvas`. **Comment il fabrique ces pixels ne regarde que lui** — y
compris en pilotant un moteur GPU.

Et la plomberie progressive existe : `update_region`, `bucket_render_start` /
`bucket_render_end`, `add_image_level_update_callback`. Le rendu par régions
dans l'Image View est prévu.

Ce qu'on n'obtient pas : **la scène**. Clarisse donne un objet et rien d'autre.
Il faut parcourir le graphe soi-même et tout traduire.

**Nuance importante sur le vocabulaire** : les matériaux et textures de Clarisse
sont du C++ compilé qui s'évalue sur CPU (`ModuleMaterial::evaluate`,
`ModuleTexture::evaluate`). Il n'existe aucun chemin d'une DLL de matériau vers
du code GPU. Ce problème existe **déjà sur une scène d'un seul cube** — réduire
la taille ne le touche pas. Un moteur GPU dans Clarisse ne peut donc pas
« utiliser les fonctions de Clarisse » : il faut une couche de traduction.

---

## 2. Le paysage des moteurs

### Le constat central sur les caustiques

**En 2026, aucun moteur open source n'a de solution de caustiques
production-grade sur GPU — sauf LuxCore, partiellement.**

Il faut distinguer quatre choses que le marketing confond :

| Approche | Ce que ça vaut |
|---|---|
| « autoriser les chemins caustiques » dans un path tracer | retrait d'un garde-fou, pas une solution (ProRender, MoonRay) |
| **MNEE / shadow caustics** (Cycles) | **réfractif uniquement**, objets à taguer, culling explicitement biaisé — sur GPU |
| **photon mapping / SPPM** | la vraie réponse. appleseed (CPU seul), **LuxCore PhotonGI (GPU)** |
| bidirectionnel / VCM / Metropolis | excellent, **systématiquement CPU seulement** |
| path guiding | améliore l'indirect, **ne résout pas les caustiques** |

Le fait décisif, lu dans le source de LuxCore : `compiledscene.h` — la structure
téléversée sur le device — contient `pgicCausticPhotons` et son BVH. **Le cache
de photons caustiques est résident GPU.** Il y a même un AOV `OUTPUT_CAUSTIC`.

### Le tableau

| | Licence | Actif 2026 | GPU | Caustiques GPU | API d'embarquement | Windows |
|---|---|---|---|---|---|---|
| **LuxCoreRender** | Apache-2.0 | ✅ | CUDA / OpenCL / OptiX | ✅ **PhotonGI** | ✅ documentée | ✅ |
| Cycles | Apache-2.0 | ✅ | le meilleur du lot | ⚠️ MNEE seul | ❌ voir §3 | ✅ |
| appleseed | MIT | ⚠️ 1 mainteneur, 0 release depuis 2019 | ❌ aucun | ❌ | ✅ | ✅ |
| MoonRay | Apache-2.0 | ✅ ASWF | intersection seule | ❌ aucune | ✅✅ la meilleure | ❌ **Linux only** |
| pbrt-v4 | Apache-2.0 | ⚠️ | CUDA/OptiX | ❌ **dégradation silencieuse** | ❌ write-once | ✅ |
| Mitsuba 3 | BSD-3 | ✅✅ | CUDA/OptiX/Metal | ❌ **par décision écrite** | ⚠️ | ✅ |
| OSPRay | Apache-2.0 | ⚠️ 0 release depuis 26 mois | Intel Xe seul | ❌ | ✅ mais castrée | ✅ |
| Radeon ProRender | ❌ **binaire fermé** | ⚠️ | HIP/Vulkan | ❌ | ✅ | ✅ |

**Rejets nets** : ProRender n'est pas open source — le cœur est
`Northstar64.dll`, 42 Mo fermé ; l'Apache-2.0 ne couvre que l'enrobage.
Mitsuba 3 refuse les caustiques explicitement, n'a pas de motion blur et
interdit l'instanciation imbriquée. MoonRay n'a ni caustiques ni Windows.
pbrt-v4 en GPU transforme silencieusement un `sppm` demandé en path tracer
ordinaire — le pire mode d'échec pour un moteur embarqué.

---

## 3. Pourquoi pas Cycles, précisément

Deux raisons indépendantes, chacune suffisante.

**Les caustiques.** MNEE seulement : réfractif, pas de caustiques par réflexion,
objets à taguer manuellement, et un culling de chemins commenté dans leur propre
code comme « removes fireflies **at the cost of bias** ».

**Il n'y a pas d'API d'embarquement.** La documentation de Blender dit
d'utiliser l'API C++ directement, puis : *« There is no real API documentation
at the moment, as the API is subject to change »* et *« see the current
standalone code and its API as a proof of concept only »*. La tâche
« Improve Cycles standalone » a été ouverte par Thomas Dinges le **2014-01-18**
et est **toujours ouverte**. Cycles n'est **pas une bibliothèque partagée** — la
demande de l'auteur de GafferCycles, ouverte en 2023, n'est pas fusionnée.

**Conséquence mesurée : tous les intégrateurs, sans exception, forkent l'arbre.**

Le chiffre qui dit le coût récurrent : sur les **106 commits** touchant le
délégué Hydra **interne au dépôt Cycles**, **82 (77 %) sont des réparations
collatérales** causées par les refactos du cœur. Hors de l'arbre, ce coût est
pour l'intégrateur.

Et le décrochage de version est la règle : McNeel (Rhino) — douze ans
d'intégration commerciale, équipe payée — est resté sur **Cycles 3.5**, a tenté
le passage à 4.4 et **a fait marche arrière en janvier 2026**.

Le témoignage le plus proche de notre cas est une tentative Maya de 2025-2026 :
quatre jours pour un squelette qui rend maillages, caméra et lumières ; **trois
mois plus tard, toujours bloqué sur la découverte des chemins CUDA**. Brecht Van
Lommel y a trouvé un vrai bug dans `tile.get_pass_pixels` en notant que « ce cas
n'affectait pas Blender » — preuve que les chemins non-Blender de l'API ne sont
pas testés, par construction.

---

## 4. Le coût d'une intégration in-process

Estimation issue de trois ponts réels mesurés :

| Pont | Taille | Équipe | Durée | État |
|---|---|---|---|---|
| Blender → Cycles | **15 293 lignes** | l'équipe Cycles | ~15 ans | complet, natif |
| Cycles → Hydra | 6 427 lignes | 19 contributeurs | 4,5 ans | **toujours « expérimental »** |
| ProRender → Maya | **~67 000 lignes** | 22 contributeurs financés AMD | 2017 → 2023 | **mort depuis 2 ans** |
| GafferCycles | 17 234 lignes | — | **4 ans avant première livraison** | vivant, encore nommé « Preview » |

**Estimation : 18 à 30 personne-mois** pour un pont utilisable, 4 à 6 pour une
preuve de concept. Seul et à plein temps : un an et demi à deux ans et demi.

### Où passe le temps

**Dans les matériaux, et ça n'a pas de fin.** Le rapport est constant sur tous
les ponts mesurés :

| Pont | Part des matériaux | Part de la géométrie |
|---|---|---|
| ProRender → Maya | **47 % des fichiers C++** | ~14 % — rapport **8:1** en octets |
| hdRPR (Hydra) | **37 % des octets** | 12 % |
| ProRender → 3ds Max | 50 fichiers sur 206 | ~27 Ko contre 276 Ko |
| BlendLuxCore | `nodes/` = plus gros répertoire (99 fichiers) | — |

L'unité de travail atomique est **un fichier par nœud d'ombrage de l'hôte**. Le
plugin Maya de ProRender consacre 25 `.cpp` aux seuls nœuds **utilitaires** —
clamp, remap, gamma, blend. Pas les shaders.

Clarisse a **112 classes d'ombrage** : 83 textures, 29 matériaux.

Et deux postes cachés que les ratios sous-estiment : **l'interface hôte** (le
plugin Maya a 388 Ko de MEL écrit à la main, dont un seul fichier de réglages de
rendu de 108 Ko) et **la couche d'abstraction du moteur**, qui métastase
systématiquement — `rprApi.cpp` de hdRPR fait 226 Ko à lui seul, 20 % du
codebase, dans un seul fichier.

### Le chiffre qui corrige l'intuition

Cycles for Max, en comparant le fork du moteur à sa base amont :

> **28 commits, 40 fichiers, +315 / −221 lignes** de modifications à Cycles —
> presque uniquement de la plomberie de build.
>
> Contre **38 074 lignes** de code côté hôte.

**Modifier le moteur ne coûte rien. Traduire l'hôte est tout le travail.** Même
forme chez Rhino : 263 Ko de couche C-ABI contre 25 627 lignes de code C# côté
hôte.

### La leçon la plus transférable, et elle nous concerne directement

L'auteur de Cycles for Max — seul postmortem écrit trouvé sur l'ensemble des
ponts — a buté sur ceci : **l'API de matériaux de 3ds Max ne peut pas exprimer un
graphe de shading Cycles.** Max limite chaque nœud à une seule sortie. Sa
conclusion : *« Some cycles nodes simply can't work within the Max material
API »*, et sa résolution a été d'**abandonner les nœuds natifs de l'hôte et
d'écrire son propre éditeur de nœuds** — un projet annexe de 10 700 lignes.

**Clarisse a exactement la même limite** : une texture rend un
`TextureOutput.color`, soit une seule couleur. Le piège est donc le nôtre aussi.

La parade est celle qu'on a déjà identifiée pour le projet three.js :
**mirror, pas traduction.** Plutôt que de tenter de convertir un graphe Clarisse
arbitraire vers LuxCore, fabriquer des nodes Clarisse qui **sont** les 68
textures et 19 matériaux de LuxCore. L'artiste construit alors dans le
vocabulaire de la cible, et l'export devient une transcription et non une
interprétation. Le même principe résout le même problème dans les deux projets.

Un raffinement repéré chez un intégrateur Houdini très récent : il **génère** son
jeu de nodes depuis le registre de nœuds du moteur — 163 nodes publiés
automatiquement plutôt qu'écrits à la main. hdRPR fait de même pour ses réglages
de rendu, à partir d'un schéma déclaratif. C'est le seul motif d'économie
clairement transférable qu'on ait trouvé.

### Les murs techniques

**Les versions.** Clarisse embarque OSL 1.9 / LLVM 9, OptiX 6.5, CUDA 10,
OpenEXR 2.4, TBB 2019, USD v22. Cycles veut LLVM 15+, OptiX 7.3+, CUDA 11+,
OpenEXR 3, oneTBB. Deux LLVM dans un process est un plantage classique.

**La couleur.** Cycles appelle `OCIO::GetCurrentConfig()`, la config **globale
au process** — il ne prend pas de handle. Clarisse charge la sienne.

**Le raccourci Hydra n'existe pas** : l'USD de Clarisse est en v22 et **sans
Hydra** — `HdRenderDelegate` et `UsdImaging` sont absents de la DLL. Aucun render
index où brancher un délégué.

**Un mur qui n'en est pas un** : un rapport annonçait le toolset MSVC comme
incompatible (Clarisse en v141). **Faux dans notre cas, preuve directe à
l'appui** — nos modules sont compilés en **v142** et fonctionnent, avec des
`CoreString` et `CoreVector` qui traversent la frontière. Les toolsets v14x sont
compatibles binairement.

**OSL n'est pas un pont.** Celui de Clarisse est un *nœud de texture* qui rend
une couleur, pas un langage de matériaux, en version 1.9. Celui de Cycles sur
GPU exige OSL ≥ 1.13 et OptiX uniquement.

### Une verrue juridique à connaître

`third_party/atomic/` de Cycles porte des en-têtes **GPL-2.0-or-later** dans 4
fichiers sur 5, **et il est compilé** (`include_directories(../third_party/atomic)`).
Probablement un oubli du relicenciement de 2013. Sur Windows, le fichier utilisé
(`atomic_ops_msvc.h`) est le seul **sans** bloc GPL, et il est remplaçable par
`<atomic>` en une journée. Sans conséquence pour nous — ClarisseAdd est en
GPL-3.0 — mais à savoir.

---

## 5. LuxCoreRender, en détail

**Licence : Apache-2.0 annonce, mais du GPLv3 dans le source.** `COPYING.txt`
est bien la licence Apache 2.0 — mais **sept fichiers portent un en-tete GPLv3**,
dont l'en-tete PUBLIC `include/luxrays/core/geometry/matrix3x3.h`, et ils sont
compiles dans les cibles `luxcore` et `luxcore_static`. Vestige de LuxRender 1.x
que la reecriture d'en-tetes de 2017 n'a pas nettoye.

**Sans consequence dans l'architecture retenue** : en processus externe il n'y a
aucune edition de liens, donc aucune contamination. Et ClarisseAdd est deja en
GPL-3.0. **Cela deviendrait bloquant** pour un produit ferme, ou pour toute
variante in-process. A verifier soi-meme avant tout engagement de ce type.

*(L'addon Blender `BlendLuxCore` est lui franchement en GPL-3.0.)*

**Le projet a failli mourir puis a été relancé.** Commits par an sur `master` :
1 033 en 2019, puis chute à **11 en 2024**, puis **650 en 2025** et 310 en 2026.
Le fondateur (David Bucciarelli) est parti en mai 2022 ; un nouveau mainteneur,
`howetuft`, a repris le projet.

**Le risque principal est humain** : sur les **300 derniers commits, 278 sont
d'une seule personne**. Deux développeurs actifs. Et l'API publique est **en
cours de réécriture** en ce moment (passage aux smart pointers) — il faut
s'attendre à de la churn, pas à de la stabilité.

**L'API d'embarquement**, elle, est exactement la bonne forme :

```
luxcore::Scene        DefineMesh, DefineMeshExt, DefineStrands (poils),
                      DefineImageMap, Parse(Properties),
                      UpdateObjectTransformation, UpdateObjectMaterial,
                      DuplicateObject  ← instanciation en lot
luxcore::RenderSession  Start / Stop / Pause / Resume
                        BeginSceneEdit / EndSceneEdit  ← transaction IPR
                        WaitNewFrame, GetFilm, SaveResumeFile
luxcore::Film         GetOutput<float> par canal
```

**Point d'architecture décisif** : il n'y a **pas de `DefineMaterial()`**. La
géométrie passe par des appels typés, mais **matériaux, textures et lumières se
déclarent en propriétés texte** passées à `Scene::Parse()`. Traduire un
matériau, c'est donc **générer du texte**, pas se lier à une API typée. Moins
cher à écrire, sans vérification à la compilation — et **ça évite précisément la
partie de l'API qui est en train de changer**.

**L'instanciation est de qualité production** : `DuplicateObject` a une forme en
lot (N instances en un appel), avec identifiants par instance et motion blur par
instance.

**Devices** : CUDA, OpenCL, natif CPU. **OptiX** comme structure d'accélération
matérielle. Trois débruiteurs — OptiX, Intel OIDN, BCD. **Pas de HIP** : sur AMD,
OpenCL uniquement. **Pas d'OSL** du tout.

**Système d'ombrage** : 19 matériaux de surface + 3 de volume, **68 textures**,
15 types de lumière. Du même ordre que les 112 classes de Clarisse — la mise en
correspondance est grande mais pas absurde.

**Deux plafonds concrets.** Le BVH GPU lève une exception au-delà de **8 pages
de tampon** (`"Too many vertex pages required in BVHKernels()"`) — il **jette au
lieu de dégrader**. Et LuxCore construit toute la scène à plat en RAM hôte avant
compilation, avec **un objet C++ par instance** et sans instanciation
hiérarchique : le plafond réaliste est **le million d'instances, pas le
milliard**.

Sans conséquence au périmètre retenu, mais à connaître.

**Et l'histoire des ponts hors Blender est sans appel** : l'exportateur Blender a
reçu 3 700 commits ; **tous les autres sont morts sous 250**. Maya abandonné en
2014 — le pont n'a jamais dépassé une classe Camera. Cinema 4D en 2010. 3ds Max
relancé par une personne en 2024-2025, silencieux depuis janvier.

---

## 6. Recommandation

**LuxCore, en processus externe.** Pas d'intégration in-process.

On écrit un exportateur, on lance LuxCore à côté, on récupère l'image.

| | In-process | Processus externe |
|---|---|---|
| Coût | 18-30 personne-mois | **2-4 personne-mois** |
| ABI, LLVM, OCIO, symboles | tous les murs | **aucun** |
| Plantage dans `clarisse.exe` | possible | impossible |
| Rendu interactif dans Clarisse | oui | **non** |
| Déjà à moitié fait | non | **`tropix` (.project → USD)** |

C'est d'ailleurs ce que la seule tentative Cycles-pour-Maya a fini par faire :
elle n'embarquait pas Cycles, elle appelait le binaire standalone.

Si un jour l'export tient et que le manque d'interactivité devient le vrai
frein, le pont in-process reste possible — attaqué en sachant ce qu'il coûte, et
avec un exportateur déjà écrit qui aura fait la moitié de la traduction.

---

## 7. Le premier pas

**Une demi-journée, en Python, hors de Clarisse.**

`pyluxcore` est sur PyPI avec des wheels à jour (2.11.2, 2026-09-06, Python
≥ 3.10 — donc dans *notre* Python, pas celui de Clarisse qui est en 3.7).

```
pip install pyluxcore
```

Puis construire par script une scène minimale — un maillage, un sol, un
matériau de verre, une lumière — activer PhotonGI, et regarder si les caustiques
sortent, sur GPU, à une vitesse qui plaît.

**On répond ainsi à la seule question qui compte vraiment — *est-ce que ce
moteur donne l'image voulue* — avant d'écrire une ligne d'exportateur.** Si la
réponse est non, on a perdu une demi-journée.

---

## 8. Ce qui n'est pas établi

- Le comportement réel de LuxCore au-delà de ~10⁶ instances — **aucun benchmark
  public**. Testable localement en une après-midi.
- Le coût en performance de l'out-of-core LuxCore (le chiffre de ~50 % vient de
  tests de la v2.4, pas d'une mesure récente).
- La compatibilité ABI concrète entre les dépendances de LuxCore (OpenEXR, OIIO,
  TBB, Boost) et celles déjà chargées par Clarisse — **sans objet en processus
  externe**, mais bloquant pour toute variante in-process.
- Les conditions de redistribution du SDK OptiX et du CUDA Toolkit pour un
  tiers — à traiter avant toute distribution binaire.
- Si les caches PhotonGI supportent l'instanciation massive sans explosion du
  nombre de photons requis.
