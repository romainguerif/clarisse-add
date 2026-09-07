# Un intégrateur pour Clarisse — dossier complet

État de la recherche au 2026-09-07. **Aucun code écrit.**

L'idée : écrire notre propre **intégrateur** — l'algorithme de transport de
lumière — et le brancher dans Clarisse, qui garde la géométrie, le BVH,
l'instanciation et le shading. C'est le seul morceau qu'Isotropix a rendu
enfichable, et c'est celui qui décide du nombre de rayons nécessaires pour une
image propre.

Six recherches de fond ont alimenté ce dossier. Ce qui suit garde les chiffres
et les chemins de fichiers, parce que c'est là qu'est la valeur.

---

## 1. Ce que Clarisse expose — vérifié, pas supposé

### Le point d'entrée

```
ModuleIntegrator::register_model(ui_name, of_class)

cb_create_scene_data(ModuleIntegratorSceneInfo&, AppProgressBar*)
                              -> notre etat persistant, et une pre-passe
cb_destroy_scene_data
cb_get_las(SceneIntegrationData*) -> const GasObject*
                              -> NOTRE structure d'acceleration de lumieres
cb_pre_render / cb_post_render
cb_pre_bucket / cb_post_bucket
cb_pre_packet / cb_post_packet
cb_shade(objet, eval, shader, raytrace, ray_index, ShadingOutput&)
                              -> LE transport de lumiere
cb_get_aov_list
```

`ModuleIntegratorSceneInfo` fournit à la pré-passe **la liste complète des
lumières et des objets de scène**. `cb_get_las` existe précisément pour qu'un
intégrateur fournisse sa propre structure de lumières — ce n'est pas un
détournement, c'est le point d'extension prévu.

**Le paramètre de `cb_shade` n'est pas une profondeur, c'est un `ray_index`.**
Clarisse trace par paquets et nous appelle une fois par rayon touché. **À partir
de là, nous menons le chemin** : nos propres rayons, notre échantillonnage de
lumières, notre terminaison.

### Lancer nos rayons

`ShaderHelpers` expose tout :

```
ray_hit / ray_hit_nearest     intersection SEULE, sans ombrage -> passe de photons
raytrace                       tracer et ombrer, un rayon ou un PAQUET
raytrace_opacity               rayons d'ombre / occlusion
shade / shade_opacity          ombrer une intersection existante
```

Les variantes par paquet (`use_packet`) comptent beaucoup sur CPU.

### Les feux verts sur les questions bloquantes

| Capacité | Preuve | Débloque |
|---|---|---|
| Évaluer un BSDF dans une direction choisie, avec sa pdf | `PbrBxdf::evaluate(wi, sample, scatter, eval)`, `PbrEval::get_pdf()`, `PbrSample::set_direction()` / `is_mis_enabled()` | **MIS et path guiding** |
| Modifier la rugosité d'un lobe avant échantillonnage | `PbrBxdf::configure(albedo, normal, roughness, flags, …)` ; `PbrMaterial::get_bxdf(i)` rend un pointeur **non const** | **Régularisation d'espace de chemins** |
| Substituer notre échantillonneur | `PbrSampler` est abstrait : `get_sample(seed, sample_index, PbrRandVar&)`, `set_dimension` / `increment_dimension`. Et `ModuleIntegratorPathtracer` ajoute `cb_create_random_sampler` | **Owen scrambling, décorrélation** |
| Construire un BVH avec notre heuristique | `GasTree::create(bboxes, GasTreeSplitAlgorithm&, progress)` — `split()` est **virtuelle pure** | **Light BVH, avec le builder de l'hôte** |
| Splitter au point d'ombrage | `CtxPbrShader::prepare_recursion_sampling(...)` ; `CtxShader::push/pop` avec `CtxRecursionData` | **EARS, splitting au premier sommet** |
| Lumières : sample / evaluate / contribution non ombrée | `ModulePhysicalPbrLight::sample()`, `evaluate()`, `get_contribution()` | **NEE, MIS, cull par contribution** |
| État de chemin déjà porté | `PbrRecursionData` : luminance accumulée, **rugosité max accumulée**, pdf du rebond précédent, medium stack. `PbrPath::Bounce::set_tail_closure(cone_angle, contrib, rr_weight)` | différentielles et comptabilité RR déjà là |

### Ce que l'hôte garde

`RendererRaytracer` possède : nombre de rayons AA, motif d'échantillonnage
caméra (**Blue Noise par défaut**), filtre de reconstruction, taille des buckets
(32×32) et des paquets (8×8), et le raffinement adaptatif par pixel
(`refinement_variance_threshold` 0.005) — **désactivé par défaut**,
`refinement_maximum_sample_count = 0`, plusieurs attributs `read_only`.

### Deux contraintes structurelles

**`ShadingOutput` ne permet pas le *splatting*.** Il ne porte que `color`,
`transmittance`, `aovs`, `deep` (`shading_base/shading_output.h`) : une radiance
par échantillon, et c'est le Renderer qui accumule. Donc : pas de light tracing,
pas de projection de photons vers la caméra, pas de BDPT complet, pas de ReSTIR
spatio-temporel, pas de repondération d'outliers à la Zirr (qui exige de
posséder plusieurs framebuffers séparés par magnitude). **Seul le photon *lookup*
aux sommets du chemin caméra est disponible** — et c'est exactement le choix
qu'a fait Corona en production.

**En revanche, « pas d'accès au film » serait faux.** `ctx_shader.h` expose
publiquement, dans le `CtxShader&` **non const** que reçoit `cb_shade` :

```cpp
ImageCanvas *image;                       // l.502, l'image en cours
struct { int x; int y;                    // le PIXEL courant
         CtxBucket image_bucket, render_bucket;
         CtxPixel pixel; ... } camera;    // l.531-546
struct { unsigned int id, count; } thread;
CtxIntegrator *integrator_ctx;            // notre contexte
CtxScratchpad *scratchpad;
```

`CtxPixel` porte `sample_count`, `sample_index`, `sample_ray_count`,
`sample_position`, `sample_pdf`, et **`sample_size` — le différentiel de rayon**,
celui dont Hyperion se sert pour sa rugosification progressive.

Conséquence : **toute statistique par pixel tenue dans notre propre état est
possible** — variance, comptage, pondération, splitting adaptatif à l'intérieur
du chemin. Ce qui manque n'est pas la connaissance du pixel, c'est le droit
d'écrire ailleurs que sur l'échantillon courant.

`[NÉ]` Reste à vérifier la sémantique exacte de `CtxShader::image` en cours de
rendu : pointeur vivant ? lisible sans course ? porte-t-il l'accumulation
courante ? C'est ce qui déciderait si la famille « rejet d'outliers » redevient
envisageable.

**`cb_pre_render` reçoit un `const CtxShader&`**, donc pas de
`new_raytrace_ctx()`. Toute passe de photons en dépend. **C'est le premier test
à écrire, avant toute ligne d'algorithme.**

---

## 2. Les quatre trous trouvés dans Clarisse

Lus dans son propre SDK. Ce sont nos ouvertures.

### 2.1 L'échantillonneur Sobol n'est pas scramblé

`clarisse_pbr/pbr_integrators/pbr_sobol_sampler.h` :

```cpp
const unsigned int sobol_index = seed + sample_index;
const unsigned int n_dim = (m_dimension + n) % SobolSamplerNd::get_dimension_count();
var[n] = SobolSamplerNd::samplef(sobol_index, n_dim);
```

La signature est `samplef(index, dimension, scramble = 0u)`. **Le scramble n'est
jamais passé.** C'est du Sobol brut, décorrélé par un simple décalage d'index.

Burley 2020 mesure sur trois configurations de Cornell Box :

| | RMSE |
|---|---|
| indépendant | 0,020 |
| Sobol, scrambling par chiffres aléatoires | 0,0048 |
| **Owen** | **0,0017** |

Et la convergence passe de O(N⁻¹) à **O(N⁻³ᐟ²)** sur intégrandes lisses, avec
une variance bornée à 2,72× celle de l'indépendant dans le pire cas. Le code
tient en trois listings du papier (§2.7–2.9). Cycles l'utilise
(`kernel/sample/sobol_burley.h`).

### 2.2 La machinerie de *light cuts* est compilée hors du binaire

`clarisse_pbr/pbr_sas/pbr_sas_object.h`, ligne 15 :

```cpp
//#define PBR_WEIGHTED_SAS_OBJECT_USE_TREE_CUT
```

Commentée. Et l'attribut `emissive_geometry_cuts_precision` de
`IntegratorPathtracer` est `hidden yes` avec `value 0.0` — le réglage le plus
grossier.

**C'est un chantier inachevé laissé dans le produit.** Notre plus grande
ouverture, et elle tombe pile sur la technique qui a gagné (§3).

### 2.3 Chaque lumière porte encore un `sample_count` de 16

Plus des multiplicateurs par matériau. C'est exactement le modèle à N boutons
que 3Delight a supprimé et dont ils font leur argument principal (§6).

### 2.4 Mais le path tracer natif fait déjà beaucoup

À égaler avant de prétendre le battre. CID de `IntegratorPathtracer` :
`russian_roulette` 0.1, `clamping_threshold` 10, `low_light_threshold` 0.001
(« illumination non ombrée en dessous de laquelle un échantillon de lumière est
présumé occulté »), `roughness_noise_optimization` 0.25 (c'est-à-dire une
régularisation, déjà là), `fireflies_filtering` 0.25,
`maximum_splitting_depth` 1, profondeurs par canal (diffus 1, réflexion 1,
réfraction 2, total 3).

Autrement dit **Clarisse livre déjà les quatre biais qui survivent en
production** : clamping, rugosification, filtrage de fireflies, troncature de
chemin. Les réimplémenter ne rapporterait rien.

Côté `RendererRaytracer` aussi : `shading_oversampling` (découplage taux
d'ombrage / AA — le mécanisme de 3Delight du §6), `subsample_quality` (taille
des différentiels), filtre AA en Importance Sampling par défaut, échantillonnage
Blue Noise par défaut, et `previz_mode` qui est un **LOD de lumières** déjà
livré.

### 2.5 Le cinquième trou : l'échantillonnage adaptatif est écrit et éteint

Sur `RendererRaytracer` :

| attribut | défaut | ce que c'est |
|---|---|---|
| `refinement_maximum_sample_count` | **0** | = « Refinement Sampling Disabled » |
| `refinement_variance_threshold` | 0.005 | le seuil, implémenté |
| `refinement_variance_mode` | — | Contrast / Standard Deviation |
| `refinement_variance_filter` | — | filtrage des variances voisines |
| `refinement_variance_LUT` | — | métrique perceptuelle |

Tout le mécanisme est là, y compris le filtrage de la carte de variance —
exactement ce que Hyperion décrit en §5.1.1 — et **un seul attribut à zéro le
désactive entièrement**.

C'est probablement le meilleur rapport gain/effort du dossier : **aucune ligne
de C++**. Et le chiffre de contexte qui remet tout à l'échelle : chez Disney, le
denoiser seul permet de rendre à **1/4 à 1/8** du spp autrement nécessaire.
Aucune technique biaisée de tout ce dossier n'approche ce facteur.

---

## 3. Ce qui a gagné en production : l'échantillonnage many-lights

**Les plus gros gains publiés de tout le dossier ne viennent pas du path
guiding.** Ils viennent de là.

| Gain | Contexte | Source |
|---|---|---|
| **> 100×** | Arnold GLS à 10 000 lumières | Autodesk |
| **2,5×** | Arnold 7.4.3, **« same-noise renders »** — la seule base honnête publiée | Autodesk |
| **jusqu'à 6×** | Arnold 7.4.4, éclairages difficiles | Autodesk |
| **4,3× et 6,7×** | Corona / Vévoda et al., **à erreur égale en GI complète** ; 3,6× et 9,3× en direct ; 510× sur un micro-cas d'occlusion. Surcoût 7-8 %, ~100 Mo | papier + implémenté tel quel dans Corona |
| **+5 dB puis +3 dB PSNR** | Conty–Kulla : terme de distance, puis d'orientation | HPG 2018 |
| 363k lumières à **7 rayons d'ombre** par point d'ombrage | Conty–Kulla | HPG 2018 |

Et Manuka chiffre son propre budget : ray tracing **~20 %** du temps, shading
**5-10 %**, **évaluation de la hiérarchie de lumières jusqu'à 25 %**. Le sampler
de lumières est le premier ou deuxième poste de coût d'un moteur.

### Qui a livré quoi

| Moteur | Structure | Défaut |
|---|---|---|
| **pbrt-v4** | `BVHLightSampler` + `CompactLightBounds`, Conty–Kulla | **activé** (`"bvh"`) |
| **Cycles** | light tree, splitting **délibérément retiré** | **activé** |
| **Karma** | arbre à sphères englobantes ; auto à 10 lumières (CPU) / 2 (XPU) | auto |
| **MoonRay** | `LightTree`, Conty–Kulla + splitting adaptatif | désactivé |
| **Hyperion** | « cache points », **conscient de l'occlusion**, millions de lumières | production |
| **Arnold** | GLS — *pas* un arbre : sélection par importance sur budget fixe | désactivé |
| **Corona** | Vévoda et al. 2018, bayésien, conscient de l'occlusion | **activé, sans réglage** |
| **3Delight** | **CDF de lumières**, pas un arbre | — |
| **OSPRay** | **aucune** — `lightSamples = all` | — |

### Les listes d'exclusion — le savoir d'échec, offert

Karma documente que restent **hors** de l'arbre : dome, distant, point,
filtrées, IES, et spots. Et : « les rectangles longs et fins ne conviennent pas
bien au light tree. En interne il représente l'éclairage avec des **sphères
englobantes**, donc ces lumières peuvent ajouter du bruit. »

Cycles exclut les émetteurs de puissance < 0,5 — « pour éviter que des émetteurs
faibles utilisent beaucoup de mémoire et gaspillent des échantillons ».

MoonRay a abandonné le terme de variance d'énergie de Conty–Kulla : « la
variance d'énergie tend vers les extrêmes (soit 0, soit un nombre très élevé) ».
Et neutralise le terme de distance près de la racine : « il ne donne pas
d'information significative quand les boîtes contiennent beaucoup de lumières
dispersées, et il écrase des informations plus utiles ».

### L'esquive MIS de MoonRay — l'astuce la plus utile du dossier

```cpp
misPdf = pdf;  pdf *= lightSelectionPdf;
```

La probabilité de sélection entre dans l'**estimateur** mais **pas dans le poids
MIS**, et le côté BSDF utilise symétriquement la pdf brute. Ça évite les
*bit-trails* de Conty–Kulla. Reste non biaisé, simplement non optimal.

---

### Le contre-exemple qu'il faut connaître : Disney a rejeté le light tree

Burley et al., TOG 2018, §4.1.1, verbatim :

> « **We experimented with hierarchical tree data structures for light selection
> but found them difficult to make useful due to certain edge cases and varied
> light types, in particular those with narrow, highly directional IES
> profiles.** We realized that the lighting did not vary enough between nearby
> shading points to warrant generating a brand new probability distribution for
> each one, even in scenes with many lights. »

D'où les cache points (§7 bis). Ce n'est pas une opinion isolée : SideFX **exclut
les profils IES et les light filters** du light tree de Karma, et Karma exclut
aussi dome, distant, point et spots. Deux studios, même point de rupture.

À mettre en balance avec le fait que SPI Arnold, Cycles, Karma, Manuka et pbrt-v4
le livrent tous. Ce n'est pas « le light tree ne marche pas » — c'est **« le
light tree gère mal les types de lumières exotiques »**, et il faut une liste
d'exclusion dès le premier jour.

### Cinq détails d'implémentation qui décideront du résultat

**1. La SAOH, en entier.** `C_split(i,s) = K_r(i)·[E_L·M_A(L)·M_Ω(L) +
E_R·M_A(R)·M_Ω(R)] / (M_A·M_Ω)`, avec `K_r(i) = length_max/length(i)` en
régularisation, `M_Ω` la mesure d'aire du cône d'orientation, et `E` **non pas
l'énergie totale émise dans toutes les directions** mais « the maximum radiance
emitted in some direction integrated over the emitting area ». Les trois axes
sont explorés.

**2. La mesure de traversée, eq. (3) :**

```
I_s = (f_a · |cos θ_i'| · E / d²) · cos(θ')   si θ' < θ_e, sinon 0
```

avec `θ_u` le demi-angle du cône couvrant toute la bbox vue du point d'ombrage,
`θ_i' = max(θ_i − θ_u, 0)`, et `θ' = max(θ − θ_o − θ_u, 0)`. Le terme
`f_a·|cos θ_i'|` est « a conservative and **arbitrary** approximation of the BSDF
times the irradiance » — ils prennent du diffus et s'en remettent au MIS.

**3. La singularité en 1/d², et la critique de Yuksel.** Le poids part à l'infini
quand le point est dans la bbox de l'enfant. Conty/Kulla prennent la distance au
centroïde ; Yuksel démontre que ça ne règle rien — « this solution reduces the
singularity from an entire bounding box to a point, but **does not always improve
the convergence** of importance sampling, as compared to merely considering the
total light intensities ». Sa parade : **n'inclure `1/d²` que si `d_min > α·δ`
pour les deux enfants** (δ = diagonale de bbox, α = 1 dans tous ses résultats).
Parade de Conty/Kulla quand le splitting est désactivé : **clamper la distance à
la moitié du rayon du cluster**. Plus la **détection de branche morte** de
Yuksel : si `w₁ + w₂ = 0`, remonter, annuler la probabilité du nœud, et
redescendre ailleurs au lieu de gâcher l'échantillon.

**4. Le splitting adaptatif : les deux implémentations open source l'ont
abandonné.** Cycles dit pourquoi, en commentaire de source : « **Cycles does not
support multiple lights per shading point**. Therefore… instead of using a
conservative measure as in the paper, we additionally compute the **minimal**
possible contribution and choose **uniformly between these deux measures**. »
pbrt-v4 fait de même — une seule descente. Mais Conty/Kulla ont mesuré :
« **In an equal time comparison, we have found that splitting always outperforms
purely stochastic traversal.** » Or l'API de Clarisse peut plausiblement prendre
plusieurs échantillons de lumière par point d'ombrage — donc le splitting reste
sur la table, contrairement à Cycles. **À trancher par la mesure.**

Les formules, si on y va — variance du terme géométrique sur l'intervalle (a,b) :
`E[γ] = 1/(ab)`, `V[γ] = (b³−a³)/(3(b−a)a³b³) − 1/(a²b²)`, combinées en
`σ² = (V[e]V[γ] + V[e]E[γ]² + E[e]²V[γ])·N²`, remappées en `⁴√(1/(1+σ))` pour
qu'un seul seuil utilisateur pilote tout.

**5. Les bit-trails pour le MIS.** Les décisions gauche/droite encodées en entier
(Laine 2010), pour qu'un rayon BSDF qui touche une lumière puisse **re-parcourir
l'arbre et calculer sa probabilité sans pointeurs parents**. Et l'arbre **à
quatre branches en SIMD** de Conty/Kulla, qui « reduces the overall depth of the
tree, which **improves stratification** by reducing the amount of sample
stretching » — un gain double, vitesse et qualité.

### Le seul point de mesure CPU de toute la littérature many-lights

**Stochastic Lightcuts** de Cem Yuksel (HPG 2019, Best Paper ; TVCG 2020) est
benchmarké sur **Embree, bi-Xeon 2,4 GHz, 16 cœurs**. Son idée tient en une
phrase : **ignorer les lumières représentatives et tirer une lumière au hasard
dans le sous-arbre**, avec un facteur `p_s/p_i`. Ça supprime la corrélation
d'échantillonnage des lightcuts, lève **toutes les restrictions sur les types de
lumières** (on utilise les vraies lumières), et permet un **plafond dur** de
lumières évaluées par point d'ombrage sans catastrophe de corrélation.

| 1400–1644 lumières, 64 spp | lightcuts@10 | lightcuts@1000 | stochastic@10 |
|---|---|---|---|
| éclairage direct | 25 s (grosse erreur) | 12 min | **43 s** |
| path tracing 5 rebonds | 4 min | 87 min | **6 min** |
| un million de VPL | 3 min | 4 h | **6 min** |

Et Conty/Kulla, sur le même terrain : **363 036 lumières, 16 spp, ~7 rayons
d'ombre par point d'ombrage, ~1700 rayons/pixel → 20 minutes sur un i7 quad
core.** Contre pbrt-v3 et sa distribution spatiale 64³, à 16 spp et 1 rayon
d'ombre : **90 minutes contre 22 secondes.**

**Et la confirmation qu'on cherchait sur Clarisse** : la doc 5.0 (via Wayback,
`clarissewiki.com` et `isotropix.com` ne résolvent plus) ne documente que
`Sample Count`, `Sampling Resolution` (échantillonnage d'importance de la
**texture** d'une lumière, une CDF 2D — pas de la sélection entre lumières) et
`Low Light Threshold`. **La stratégie many-lights documentée de Clarisse est du
culling, pas de l'échantillonnage d'importance hiérarchique.**

Un avertissement pour finir, à ne pas sur-lire : les auteurs de ReSTIR ont mesuré
qu'« a light BVH generally under-performs even our streaming RIS algorithm
(without reuse) » — mais il s'agit du BVH temps réel de Moreau et al. (HPG 2019),
**pas de celui de Conty/Kulla**.

---

## 4. Le path guiding, et pourquoi je le rétrograde

Il marche. Il vaut environ 2× sur les scènes où il aide. Et après huit ans :

- **Désactivé par défaut partout.** Sans exception.
- **V-Ray** : encore « Experimental » après trois ans et demi. Leur propre
  développeur, sur leur forum : *« I managed to have it shave a whole 20 % in a
  specific scene »* — et parfois **aucun gain**.
- **DreamWorks a supprimé le leur** le 2025-03-05, commit `82e2bb78`,
  **−1 109 lignes**. Il n'était branché que dans le chemin scalaire, jamais
  vectorisé.
- **Disney** : 12 % de *Zootopia 2*, avec un système de 2ᵉ génération **bâti sur
  OpenPGL**, mesuré **2,16× plus efficace** que le path tracing sur une scène du
  film à 32 spp. Mais aussi **+15 % à +34 % de temps mur à échantillons égaux**
  sur quatre plans de production, et l'aveu : *« in some cases enabling path
  guiding negatively impacts overall rendering efficiency »*. Détail qui compte :
  une de leurs deux contributions consiste à faire **coller les résultats guidés
  aux biais du clamping**, ce qui les amène à « reconsidérer la nécessité du
  clamping » sur les prochaines productions.
- **Karma** avertit : « cela peut améliorer les éclairages difficiles, **mais
  peut rendre les éclairages faciles plus bruités** ».
- **Arnold l'a explicitement rejeté** dans son papier TOG 2018 : « ces
  techniques impliquent du caching, ce qui ajoute de la complexité, surtout avec
  le motion blur, et est sujet au scintillement en animation ».
- **Open PGL** : pas de version depuis 13 mois, pas de commit sur `main` depuis
  15. Adopté par l'ASWF en avril 2026, mais le README dit encore « Intel » et
  liste ses adoptants comme « TBA ». **On adopterait une base de code, pas une
  dépendance.**

Le mot d'avertissement qui vaut pour nous, du cours SIGGRAPH 2025 : *« the
baseline, rendered in a production renderer, can be much harder to beat than a
baseline rendered with a research renderer. Consequently, we cannot expect gains
observed in a research setting to translate directly into production. »*

**Si on y va quand même**, cinq détails décident du succès, et ils ne sont dans
aucun papier — seulement dans les notes du cours :

1. **Brancher en RIS, pas en MIS.** Le MIS à sélection fixe « does not guarantee
   that it will always improve… **in the worst case, it can significantly
   decrease** ». RIS à deux candidats — un BSDF, un guidé — plus échantillonnage
   défensif. Piège : la pdf RIS **n'est pas utilisable** dans les poids MIS de
   la NEE.
2. **La roulette russe casse sous guidage.** Correctif : facteur
   `c_i = p_guide/p_bsdf` cumulé le long du chemin. Passer en `double` au-delà
   de ~60 rebonds.
3. **Guider vers l'indirect + le direct pondéré MIS**, ni tout ni rien.
4. **Ré-étiqueter les types d'événement de diffusion**, sinon les LPE et les AOV
   se cassent et l'image change de look. Chez nous c'est facile :
   `PbrBxdf::get_type_flags()` existe.
5. **Roughening** (§5).

---

## 5. Les gains bon marché, dans l'ordre

### MIS compensation — trois jours, et c'est le meilleur rapport du dossier

Soustraire une constante de la densité tabulée d'environnement et renormaliser,
en préprocess. **2,75× / 1,75× / 1,16× de NMSE à temps égal** sur de l'IBL ;
1,38 à 1,6× sur du guidage. **Zéro surcoût à l'exécution.** Pire cas observé :
variance ×1,6. En production chez Corona, disponible dans Karma.

### Roughening — la meilleure affaire du dossier après le light BVH

C'est le seul gain qui ne demande **aucun état, aucun film, aucun voisinage** :
une fonction du chemin courant, point.

**Où l'appliquer — la règle de Kaplanyan, et c'est contre-intuitif :** pas à
chaque rebond, mais **uniquement au next event estimation**. Verbatim : « we
decide about the regularization **only at the next event estimation**, because
the path type is completely formed only at this stage ». Et : « to mollify all
delta distributions in the integrand would lead to a **large bias**… we mollify
as few interactions as possible ».

**La version simple, copiable telle quelle** — pbrt-v4, `util/scattering.h`,
quatre lignes, appliquée après le premier rebond non spéculaire :

```cpp
void Regularize() {
    if (alpha_x < 0.3f) alpha_x = Clamp(2 * alpha_x, 0.1f, 0.3f);
    if (alpha_y < 0.3f) alpha_y = Clamp(2 * alpha_y, 0.1f, 0.3f);
}
```

**La version optimisée, et ses chiffres.** OPSR (Weier, Droske, Hanika, Weidlich,
Vorba — EGSR 2021, **code Mitsuba 2 public**) remplace l'heuristique par des
paramètres appris. Correction à noter : ils ont **essayé le throughput du chemin
et l'ont rejeté** — « those strategies did not perform well ». Le modèle est une
fonction de la **suite des rugosités par sommet** et de la **longueur du
chemin** :

> `g(ᾱ,γ) = 1 − ( (1 − α_{k−1}) · ∏(1 − γ·α_i) )` — le **dernier sommet entre
> non atténué**, les précédents mis à l'échelle par γ.

Tables apprises pour les longueurs 2 à 5, Q = 4 niveaux : **1360 facteurs,
5,2 ko**. Presets livrés : low β=0,001 / moderate 0,05 / strong 0,5 / aggressive
4,0.

| scène | baseline | β=0,001 | β=0,05 |
|---|---|---|---|
| Beach, **à bruit égal** | 24 h 17 | 9 h 39 (**2,5×**) | 2 h 09 (**11,3×**) |
| Water Droplets, à bruit égal | 2 h 60 | 1 h 33 (**1,9×**) | 31 min (**5,7×**) |
| Eye, avec guidage | 9 h 18 | 8 h 13 (1,13×) | 6 h 47 (1,37×) |

À lire avec la contrepartie : **−15 à −20 % de spp à temps égal**, coût du rayon
de préfixe supplémentaire et des évaluations rugosifiées.

**Deux pièges qui n'apparaissent pas dans les résumés :**

**1. Le MIS casse.** Verbatim OPSR : « after using the regularised BSDF for the
contribution of a path of length k, **we can no longer use the same regularised
BSDF to sample a path of length k+1** without introducing additional error ». La
parade propre : construire le préfixe avec le BSDF **non biaisé** — ce qui coûte
un rayon de plus par sommet. La variante rapide réutilise le BSDF rugosifié et
accepte une petite erreur. **Jendersie & Grosch (EGSR 2019)** est le papier qui
formalise la correction des poids MIS pour les modèles microfacettes — à lire
avant d'écrire la ligne.

**2. Il faut une rugosité, et les BSDF de Clarisse ne nous la donneront pas.**
La solution est publiée : **Holzschuch, Kaplanyan, Hanika, Dachsbacher**,
SIGGRAPH 2016 Talks — ajuster `f_r ≈ k_v·exp(−(tan θ_h/σ_v)²)` et résoudre

> `σ_v² = (−2·tan θ_h)·f_r(i,o) · ( d f_r(i,o)/d(tan θ_h) )⁻¹`

Verbatim : « **For measured BSDFs, we compute the derivative through finite
differences** », et « for BSDFs with multiple components… Equation (3)
**automatically accounts for their relative importance** ». Autrement dit : il
suffit de **savoir évaluer** le BSDF et de faire une différence finie en
`tan θ_h`. C'est exactement notre situation avec `PbrBxdf`.

En production : **Arnold par défaut** (« surface roughness is clamped to a
minimum value dependent on the maximum roughness seen along the path »),
**Hyperion toujours actif**, Manuka en option, Cycles sous le nom « Filter
Glossy », Octane sous « Caustic Blur ». Clarisse a déjà
`roughness_noise_optimization` — reste à mesurer ce qu'il fait vraiment.

### Roulette russe par albédo, pas par throughput

EARS §6.1 : « nous avons trouvé qu'utiliser le produit des albédos plutôt que le
poids de throughput donnait de meilleurs résultats. Le throughput peut être très
faible si le guiding a une densité élevée à travers des surfaces sombres,
auquel cas la RR classique peut annuler les bénéfices du guiding ».

### RR sur les échantillons de lumière AVANT le rayon d'ombre

Les deux moteurs open source le font. MoonRay : seuil de luminance 0.0375 après
le premier sommet non-miroir, « conduit typiquement à un taux d'élagage
appréciable ». Cycles : `light_sampling_threshold` 0.01.

### Clamp indirect par profondeur, préservant la teinte

**L'implémentation de référence, et elle n'a besoin d'aucun film** — Cycles,
`kernel/film/light_passes.h`, malgré son préfixe `film_` :

```c
/* Clamping is done on a per-contribution basis so that we can write directly
 * to render buffers instead of using per-thread memory, and to avoid the
 * impact of clamping on other contributions. */
const float limit = (bounce > 0) ? sample_clamp_indirect : sample_clamp_direct;
const float sum = reduce_add(fabs(*L));
if (sum > limit) { *L *= limit / sum; }
```

Norme **L1** du spectre, mise à l'échelle uniforme de toute la contribution donc
**teinte préservée**, appliquée **par contribution de transport** et non par
échantillon de pixel, direct/indirect départagés par le seul `bounce > 0`.

Défauts livrés : Cycles `sample_clamp_direct` **0 (éteint)**,
`sample_clamp_indirect` **10**. RenderMan généralise avec `clampLuminance` 10 et
surtout **`clampDepth` = 2** — leur propre exemple : « `clampDepth=2`,
`clampLuminance=4` clamps all indirect illumination to 4 **without affecting
direct** ». MoonRay : `smartClamp` par `clamp/maxComponent`, 10.0 dès la
profondeur 1. Corona : MSI 20, et **0 = mode non biaisé « pas prêt pour la
production »** ; attention, leur MSI est **relatif au color mapping**, donc le
même chiffre biaise différemment selon l'exposition.

Détail que V-Ray et Corona documentent tous deux et qui est exactement notre
cas : ils clampent les **rayons secondaires**, pas les échantillons de l'image
finale. Le biais est alors un **assombrissement**, pas une perte de contraste
d'AA.

Et si on garde un jour un état persistant (guidage, cache), *Path Guiding in
Production* documente un placement que personne n'expose : clamper le
**throughput** de l'échantillon (valeur d'exemple 10) **en laissant intacte la
radiance émise par la lumière**, pour que les échantillons à forte variance ne
corrompent pas la distribution apprise.

### Les trois designs de régularisation livrés, classés par coût

**1. Clamp par contribution avec séparation direct/indirect** — Cycles ci-dessus,
généralisé par le `clampDepth` de RenderMan. **Aucun état.**

**2. Plancher de rugosité piloté par la pdf** — Cycles, `filter_glossy`, défaut
**1.0** :

> `alpha_floor = 0.5 · √(1 − filter_glossy · min_ray_pdf)`

où `min_ray_pdf` part de `FLT_MAX` et est le **minimum courant de la pdf
d'échantillonnage BSDF non guidée** le long du chemin. Le flou est un plancher :
`alpha_x = fmaxf(roughness, alpha_x)`. **Un seul float d'état de chemin.**
(Correction : ce n'est pas piloté par les différentielles de rayon, contrairement
à ce que laisse croire le manuel.)

**3. Rugosité maximale monotone le long de la chaîne** — Karma
(`constrainmaxrough`) et Arnold, qui publie la même règle : « clamped to a
minimum value dependent on the **maximum roughness seen along the path** ». Ou la
version saturante de pbrt-v4, déclenchée sur `anyNonSpecularBounces`. **Un float
ou un booléen.**

**Deux avertissements.** Le piège MIS de §Roughening s'applique aux trois — ne
jamais rugosifier le BSDF tout en calculant les poids MIS contre la pdf non
rugosifiée. Et le caveat documenté de Karma : le clamp de rugosité **ne marche
que sur GGX**, il est largement inerte sur Phong, cone et spéculaire idéal.

**Absences confirmées, qui sont des résultats et pas des trous** : **ni V-Ray, ni
RenderMan, ni Redshift, ni Corona n'ont de roughness clamping.** Leurs contrôles
de variance sont des clamps d'intensité plus la roulette russe. Et **Mitsuba 3
n'a pas d'option `regularize`** — à savoir si on pensait y prototyper.

### Splitter uniquement au premier sommet non-spéculaire

Politique de MoonRay et d'Arnold. Clarisse a déjà `maximum_splitting_depth`.

### Profondeurs de rebond fractionnaires (Karma)

« mettre la limite diffuse à 3.25 et avoir 25 % des rayons avec une limite de 4
et 75 % avec une limite de 3 ». Gratuit, sans équivalent ailleurs.

---

## 6. 3Delight — ce qu'il y a vraiment à prendre

### L'histoire

Travaux commencés en 1999, premier moteur RenderMan-compliant combinant REYES et
ray tracing. Bascule vers le path tracing en **v11 (2014)**. Refonte OSL en
2016. **Sampler de lumières spécifique introduit le 2 novembre 2016** (« permet
le rendu de vastes quantités de lumières surfaciques »), retravaillé en juillet
2017, et **amélioré en continu jusqu'en mars 2025**. NSI remplace RenderMan en
2018 — **il ne reste rien de REYES** : ni hider, ni shading rate, ni photon map.

Toujours vivant, et de loin le plus actif du panel : **version 2.9.208 du
12 août 2026**, une sortie toutes les deux à quatre semaines. Licence gratuite
complète, plafonnée à 12 cœurs — donc **étudiable en pratique**.

### Ce qui est mesuré, et ce qui est vanté

Leur comparatif de 2016, réajusté en posant RMSE ∝ N⁻ᵖ :

| | exposant p | variance |
|---|---|---|
| **3Delight** | **0,627** | ~N⁻¹·²⁵ |
| Arnold | 0,469 | ~N⁻⁰·⁹⁴ |
| RenderMan | 0,502 | ~N⁻¹·⁰⁰ |

3Delight converge **réellement mieux que le Monte Carlo standard**. Mais leur
formule « variance ~ 1/x contre 1/sqrt(x) » **exagère leurs propres chiffres**.

Le résultat honnête est ailleurs, et plus intéressant. À qualité égale :

| | temps | rayons d'ombre | time-to-first-pixel |
|---|---|---|---|
| **3Delight** | **69,5 s** | **23,8 M** | 2 s, **constant** |
| Arnold | 81,0 s | 173,6 M | 0 → 41 s |
| RenderMan | 98,1 s (à qualité moindre) | 376,3 M | 3 → 97 s |

**Seulement ~1,17× plus rapide qu'Arnold en temps mural, mais 7,3× moins de
rayons d'ombre.** Leur échantillon coûte donc beaucoup plus cher.

Et c'est exactement le compromis que Manuka défend : « une nouvelle technique
qui divise l'écart-type par deux peut prendre jusqu'à **quatre fois** plus de
temps par échantillon avant d'atteindre le seuil de rentabilité… optimiser la
réduction de variance est plus important qu'optimiser la génération rapide
d'échantillons ».

### Ce qu'il faut leur prendre — et ce n'est pas un algorithme

**Un seul bouton, et une sélection stochastique du composant par rayon.** Texte
exact de leur doc : « Shading Samples : la quantité de rayons, par pixel, que le
renderer tracera pour effectuer les calculs d'ombrage » — couvrant
« échantillonnage BRDF, échantillonnage de lumières, échantillonnage
sous-surfacique, transparence », le moteur « sélectionne automatiquement le bon
composant d'ombrage à échantillonner, **par rayon** », et **« il n'y a pas de
réglages par matériau, par lumière ou par BRDF »**.

**Clarisse fait exactement l'inverse aujourd'hui** : `sample_count` 16 par
lumière, plus des multiplicateurs par matériau.

**Le découplage rayons caméra / échantillons d'ombrage.** `screen.oversampling`
et `quality.shadingsamples` sont indépendants dans NSI ; « si Shading Samples
est inférieur à Pixel Samples, il est relevé en interne ». Mécaniquement, c'est
du **splitting au premier sommet** — la politique de MoonRay et d'Arnold.

**Le time-to-first-pixel constant.** 2 secondes quel que soit le nombre
d'échantillons, contre 41 et 97 chez les autres. Ça ne change pas le temps de
rendu batch, mais ça change complètement le confort d'itération — et ça fait
paraître le moteur plus rapide qu'il n'est.

**Le retrait automatique de fireflies, activé par défaut** depuis juin 2020.

**Ce qu'ils n'ont pas** : pas d'échantillonnage adaptatif exposé, pas de path
guiding, pas de ReSTIR, pas de light tree — une **CDF** de lumières. Et ils ont
ajouté les caustiques le **26 mai 2026**, mention « pas de qualité finale »,
algorithme non documenté.

**Ma lecture** : leur avantage n'est pas un algorithme secret. C'est un sampler
de lumières coûteux mais très efficace en échantillons, le **refus obstiné
d'exposer des boutons** — ce qui supprime la principale source de gaspillage en
production, un artiste qui règle mal quarante compteurs — une stratification
meilleure que la moyenne, et un TTFP constant. Le reste est de l'ingénierie non
publiée.

---

## 7. Les caustiques

Toutes les solutions livrées en production sont **découplées, optionnelles, ou
fausses**. Et toutes **cassent l'indépendance des buckets** :

- RenderMan : « rendering a crop window may result in a different quality render
  than a full frame render » — les chemins lumière sont générés par pixel et
  stockés dans une photon map.
- V-Ray : les caustiques progressives « exigent l'échantillonneur progressif »,
  sont **non déterministes**, et **ne fonctionnent pas en rendu distribué**.

**Clarisse rend par buckets. À régler dans la conception, pas à découvrir
après.**

### La recette de Corona, publiée en détail

Martin Šik a comparé à temps égal, 10 minutes, sur une scène dont *toute*
l'illumination est caustique : path tracing nu → échec ; path guiding →
« improves the result significantly » ; leur solveur → « the cleanest result ».
Sa conclusion : **« Our solver is based on photon lookups, since we have found
it to be more efficient than path guiding. »**

Son verdict sur les quatre familles :

| Famille | Défaut rédhibitoire |
|---|---|
| Bidirectionnelles | convergence très lente sans guidage des photons |
| Metropolis | artefacts de corrélation, scintillement en animation |
| Path guiding | **caustiques manquantes** — apprentissage trop lent |
| Manifold NEE | **caustiques réfléchies impossibles** (réfraction seulement) |

La recette : VCM amputé des connexions bidirectionnelles et de la projection
vers la caméra — **ce qui chez nous n'est même pas un choix, `ShadingOutput` n'a
pas le film**. Reste : path tracing + NEE + photon lookup. Ne stocker les photons
qu'après la première interaction spéculaire ; arrêter après deux rebonds diffus
consécutifs ; élaguer l'émission depuis un environnement uniforme. Photons
guidés par MCMC, fonction cible = **visibilité × 1/distance²(caméra)**. Pas
d'artefacts Metropolis parce que le MCMC ne sert **que** les chemins photons,
jamais la caméra, et que la définition d'importance est délibérément **simple**.
Nombre de photons automatique : `N′ = P·N·|C| / Σ min(R_p, P)` avec P = 1,5.

Coût : **~2× le path tracing** sur scènes très caustiques, **3× plus rapide
qu'un VCM complet**. Corona 12 U1 (2024-11) : caustiques « about twice as good
in the same amount of render time ».

---

## 7 bis. Le rendu biaisé, et ce que le temps réel a à donner

### La production va vers MOINS de biais, pas plus

C'est le résultat le plus contre-intuitif du dossier, et il est écrit noir sur
blanc dans le TOG 2018 de Hyperion (§2) : « we have been steadily replacing
biased approximations with Monte Carlo solutions. In every case the motivation
for doing so has been artist efficiency, and in every case the quality and
consistency of our imagery also increased. » Fourrure, SSS, nuages : les trois
sont passés d'approximations à la force brute. Leurs premiers prototypes biaisés
— cônes intégrés, partage de radiance entre cônes — ont été abandonnés parce que
« this approach made reasoning about the quality and correctness of the image
difficult ».

Le biais qui survit en production, c'est **clamping + rugosification +
troncature + denoiser**, et rien d'autre. Clarisse a déjà les quatre (§2.4). Le
reste du budget de l'industrie est allé à un **meilleur échantillonnage, non
biaisé**.

Les caches biaisés vivent encore, mais en archiviz : Corona livre son « 4k
Cache », V-Ray garde le Light Cache et a **déprécié** son Irradiance Map. Aucun
moteur VFX — Arnold, RenderMan, Hyperion, Manuka, MoonRay — ne livre de cache
d'irradiance.

### Correction : l'outlier rejection a été testée et REJETÉE par Disney

Même section 4.1.3, verbatim : « One of the approaches we tested was outlier
rejection (DeCoro et al. 2010), but this required a significant amount of
memory, produced different results for different SPP values, and resulted in
significant energy loss for low SPP values. »

Elle ne figure pas parmi les optimisations livrées de Hyperion. Ce qu'ils font à
la place : clamping au niveau du chemin, plus une détection de fireflies **dans
le denoiser** qui remplace entièrement le pixel par son voisinage — en assumant
la perte d'énergie : « it is preferable to preserving the energy as a bright
smudge ».

### Mais il existe une réponse propre, et elle tient dans un bucket

**Correction sur Zirr, Hanika & Dachsbacher** (RWMC, CGF 37(6), 2018) : j'avais
écrit que ça ne transférait pas. C'est trop catégorique. Leur structure est une
**cascade de framebuffers** `B_j` couvrant les brillances `[b^{j−1}, b^{j+1}]`
avec **b = 8**, donc **logarithmique en dynamique** — « none of our b = 8-based
results required more than **six framebuffers** ». Six buffers à l'échelle d'un
bucket, c'est rien. Ce qui dégrade aux bords de tuile, c'est leur critère de
rejet, qui moyenne `n_i` sur un **voisinage 3×3**.

Leur résultat théorique vaut d'être connu, parce qu'il tranche le débat
clamping :

- si **un seul** chemin firefly est échantillonné, l'estimé fini est
  **prouvablement trop haut** ;
- si **tous** sont rejetés (DeCoro), il est **prouvablement trop bas** ;
- la repondération se place entre les deux, avec `V^N_κ[F'] < (1/N)·V + (1/κ)·E[F]²`
  — convergence MC idéale en N **plus une constante en κ**. κ est donc un
  **cadran de variance** utilisateur, et **κ = 1 reste consistant**.

Leur verdict sur le clamping, verbatim : il « **completely breaks high dynamic
ranges, leading to a severe overall loss of brightness** ». Leur parade : suivre
une borne inférieure `E_min[F]` et prendre `r* = min(r*_c, r*_v)`, ce qui
« **regains energy that would be kept by naïve clamping** ».

Mesures auteurs : Bathroom 1024 spp, κ=20 — **RMSE 0,763 → 0,511**. Surcoût au
rendu « we could not reliably measure a performance overhead ». Non destructif :
le résultat non biaisé se reconstruit à tout moment.

### G-MoN — la réponse sans film, et c'est la trouvaille du dossier

**Buisine, Delepoulle & Renaud, EGSR 2021**, implémenté **dans pbrt-v4**.
Découper les n échantillons d'un pixel en **M paquets de k**, prendre la moyenne
de chacun, puis la **médiane des M moyennes**. Adaptativité par **coefficient de
Gini** sur les moyennes triées : `c = ⌊G·⌊M/2⌋⌋`, moyenne tronquée des M−2c
moyennes centrales.

Ce qui compte pour nous, verbatim : « the **use of local pixel information in the
case of Zirr's proposal whereas we only use the sample values local to each
pixel** ». **Aucun voisinage. Aucun film. Aucun bord de tuile.**

| | coût | mémoire | qualité |
|---|---|---|---|
| G-MoN, M=21 | **+1,17 %** (3618 s contre 3576 s) | **< 500 ko par patch 32×32** | Veach à SSIM 0,8 : moyenne **jamais atteinte** en 100 000 spp, MoN 4112 spp, **G-MoN 2626** |

Les auteurs mesurent eux-mêmes l'écart avec Zirr et en donnent la cause : « we
compare our approach to Zirr's method that appears to be **globally better**…
This is mainly due to the use of neighbouring information. **When M is large
enough, G-MoN seems to be quite close to Zirr et al but with only local
information.** » M recommandé : **11 à 25**.

**Un budget de bucket, pour 1 % de coût.** C'est exactement notre contrainte.

### Les cache points de Hyperion — et ils ne sont PAS biaisés

*Cache Points for Production-Scale Occlusion-Aware Many-Lights Sampling and
Volumetric Scattering* — Li, Zhu, Nichols, Kutz, Huang, Adler, Burley, Teece,
DigiPro 2024.

100 000 points candidats semés dans les boîtes englobantes, plus les sommets de
chemins pilotes tracés depuis la caméra ; deux passes d'élagage (kNN spatial,
puis fusion des points dont **les distributions de lumières se ressemblent**) ;
chaque point stocke deux distributions, proches et lointaines, ces dernières sur
**6 plans orientés + 1 récepteur omnidirectionnel**, ne gardant que les lumières
couvrant 97 % de l'énergie (entre 4 et 256). Les distributions lointaines sont
**floutées entre voisins**, ce qui permet de ne consulter **qu'un seul cache
point par sommet**.

L'apprentissage d'occlusion en ligne est un simple compteur tentatives/succès
par lumière et par point, avec décroissance quadratique **qui n'atteint jamais
zéro** — c'est précisément la condition d'absence de biais (ils citent Ward
1991).

| Scène | TTUV uniforme / optimal / cache points | mémoire |
|---|---|---|
| *Us Again*, 4 881 396 lumières | 0,5337 / 2,5552 / **0,2753** | +11,6 % |
| *Encanto*, 38 720 lumières | 0,0966 / 0,0184 / **0,0154** | +2,6 % |
| *Encanto*, 4 406 lumières | 0,0873 / 0,0346 / **0,0267** | +1,7 % |

À lire dans le bon sens : le mode « optimal » (budget par lumière) est **9,4×
plus lent que l'uniforme** sur 4,9 M de lumières. C'est le régime que
`m_light_sample_count` suggère chez Clarisse.

Échecs documentés : spots étroits dans de gros volumes atmosphériques
(*Strange World*) → trous dans les faisceaux, instabilité temporelle, artistes
qui désactivent ; et build explosif si chaque triangle émissif entre
individuellement.

**Transfert direct** : `cb_create_scene_data` reçoit la liste complète des
lumières **et une `AppProgressBar*`** — une passe de construction avant rendu est
architecturalement prévue.

### Path simplification : c'est le bypass de réfraction qui vaut le coup

Deux mécanismes, pas un. Le premier — rugosité croissante avec le diamètre du
rayon, d'après Kaplanyan & Dachsbacher 2013 — **Clarisse l'a déjà**
(`roughness_noise_optimization`), et `CtxPixel::sample_size` fournit le
différentiel.

Le second est celui à prendre : rugosifier les surfaces **réfractives**
détruisait les ombres nettes du soleil à travers une fenêtre. Leur solution :
**traverser tout droit sans dévier**, coefficient de réflexion ajusté pour que
la réflexion avant/arrière corresponde au trajet dévié, et **l'IOR relatif
remplacé par son inverse en sortie** d'objet, ce qui évite la réflexion totale
interne. C'est court, et Disney l'active **toujours** sauf pour une référence.

### Path space filtering — le seul cache biaisé qui tienne ici

Binder, Fricke & Keller, arXiv:1902.05942. Table de hachage sur un descripteur
(position monde quantifiée + normale + direction incidente si non diffus), **le
LOD étant dans le descripteur** — « a heuristic as simple as the distance d
along the path is sufficient ». Jittering avant quantification pour approximer
un noyau de filtre. Double hachage avec vérification ; si la collision n'est pas
résolue, **repli sur la contribution non filtrée**.

Rien n'exige de framebuffer ni d'axe temporel : c'est du hachage monde et des
atomiques, ce qui va très bien sur CPU. **Le piège est le bucket.** L'algorithme
est en deux temps (accumuler, puis moyenner) alors que `cb_shade` doit rendre
une radiance immédiatement ; remplir et interroger en même temps rend le
résultat **dépendant de l'ordre des buckets** et fait apparaître des coutures de
tuiles. Parade : peupler la table dans une passe pilote en
`cb_create_scene_data`, comme Hyperion.

Biais assumé par les auteurs : agrandir la cellule floute les ombres et
**augmente les fuites de lumière**.

### Tiny Glade — rien ne transfère, sauf le raisonnement

Pounce Light, deux personnes : Anastasia Opara (procédural) et Tomasz
Stachowiak (rendu). Source unique : *Rendering tiny glades with entirely too
much ray marching*, Graphics Programming Conference 2024. Pas de deck, pas de
blog technique.

Ce qu'ils livrent : ray tracing **logiciel** (CWBVH, jamais matériel — cible
GTX 670) contre les **proxies de collision**, si grossiers qu'ils n'ont pas de
toits ; **1 rayon pour 16 pixels** ; screen space d'abord, repli sur le BVH
monde, et si le hit monde retombe à l'écran on **rééchantillonne la radiance de
l'écran** ; radiance en SH L1, ZH3 hallucinées, denoiser type ReBLUR, XeGTAO
deux fois. Ce n'est pas du path tracing : un seul final gather, les rebonds
infinis émergeant de la **boucle temporelle**.

Screen space + temporel + GPU : **aucun élément ne transfère.** Ce qui transfère
est la **liste de ses échecs**, et elle pointe dans la même direction que tout le
reste :

| Essayé | Pourquoi abandonné |
|---|---|
| Lightmap top-down | s'effondre dès que les formes se superposent |
| **DDGI** | jamais assez de densité **spatiale** ; aliasing ; **ghosting** au redimensionnement |
| Sondes screen-space (Lumen / GI-1.0) | trop plat, taches sombres, « strange metallic sheen » |
| **ReSTIR** | « complete overkill » — scène **à faible variance** ; le **spatial était moins efficace en échantillons que la force brute** |

Un praticien qui contrôle tout son moteur a essayé sondes, caches et ReSTIR, et
a fini sur force brute + bon filtre parce que sa scène était à faible variance.
Notre situation offline l'est encore plus.

Note : `kajiya`, du même auteur chez Embark (archivé le 2026-06-01), utilise
ReSTIR **et** un cache d'irradiance en clipmap. Tiny Glade n'utilise **rien de
tout ça**. Continuité de vocabulaire, pas d'architecture.

### Radiance Cascades — verdict corrigé : la 3D monde existe depuis juillet 2026

**L'hypothèse de pénombre, exactement.** Une source de taille `w` à distance `d`
d'un occulteur projette une pénombre d'angle `α = 2·arctan(w/2d)`. À distance `D`
de la source, deux contraintes tiennent simultanément — ce sont des **pas de
discrétisation**, et c'est là que tout le monde s'embrouille sur le sens :

```
Δ_p ≲ D      le pas SPATIAL croît   ⇒ résolution spatiale qui DIMINUE avec la distance
Δ_ω ≲ 1/D    le pas ANGULAIRE décroît ⇒ résolution angulaire qui AUGMENTE avec la distance
```

Parce que la pénombre s'élargit linéairement avec la distance (champ lisse, donc
sondes espacées suffisent) tandis que l'angle sous-tendu par la source rétrécit
en 1/D (il faut des bins angulaires fins). Chaque cascade double l'espacement des
sondes, divise par deux le pas angulaire, et couvre un intervalle deux fois plus
long. Coût : `rays(N) = 2ᴺ·rays(0)` mais `cost(N) < 2·cost(0)`.

La propriété qui fait la valeur du truc : si la condition de pénombre est
satisfaite, la représentation est **toujours interpolable linéairement** —
**ni filtrage bilatéral, ni gestion de désocclusion**. Sannikov : « classic
radiance probes indeed require special handling of disocclusion exactly because
they attempt to encode full radiance instead of encoding radiance **intervals** ».

Mémoire : en 2D **comme en 3D complet**, `Σ M_i = 2·M_0`. Le problème n'est pas
l'asymptotique, c'est la constante — Sannikov lui-même : « just storing a
cascade 0 is practically equivalent to **voxelizing the entire scene** — this is
often a dealbreaker for large-scale scenes ».

**Et c'est exactement ce que résout Split Radiance Cascades** (Freeman &
Sannikov, arXiv:2607.20384, **22 juillet 2026**) : **hashmap creuse** de sondes
en espace monde au lieu d'une grille. Le **ray splitting** inverse le merging —
un rayon depuis `p` qui touche à distance `t ∈ ]t_{k−1}, t_k]` dépose
`transmittance = 1` dans toutes les cascades `i < k`, la radiance du hit avec
transmittance 0 à la cascade `k`, et **rien au-dessus** (ils ont testé
l'extension aux cascades supérieures et l'ont rejetée : biais notable). LOD par
`log2(distance de Chebyshev à la caméra)`. Rayons lancés **depuis les surfaces**,
pas depuis les centres de sondes — inspiré de SHaRC, parce que les sondes
enfoncées dans la géométrie donnaient **25 % de sur-occlusion**. Directions en
séquence **R2** à faible discrépance, assignées par préfixe hiérarchique pour
éviter le problème du collectionneur de coupons.

**Le point décisif pour nous**, verbatim de l'abstract : « in **both
single-frame** and temporally accumulated contexts ». Et la conclusion : « capable
of calculating a diffuse bounce efficiently **without the need for temporal
accumulation** ». Benchmark auteurs, RTX 3080 Laptop, monoframe : Sponza
**8,6 ms**, San Miguel **11,5 ms**.

Limites assumées : pas de réflexions spéculaires nettes, fuites de lumière sur la
géométrie plus petite que l'espacement de base, et **biais intrinsèque dû aux
sondes interpolées**. Plus le ringing autour des sources vives, corrigé par le
**bilinear fix** (reprojeter chacun des 4 rayons de merge pour partir de la
position de la sonde interpolante).

Leur propre cadrage, et il est frappant : « ray splitting can be interpreted as
an extension of **path filtering** techniques to filter radiance intervals rather
than full rays ». C'est-à-dire la même famille que le §*path space filtering*
ci-dessus.

**Preuve que ça survit hors GPU et hors temps réel** : DexRT, le solveur de
transfert radiatif non-LTE d'Osborne & Sannikov (RASTI 4, 2025), est écrit en
C++20 portable et compile **pour CPU**.

**Verdict révisé : plausible, pas exclu.** À garder derrière le light BVH, mais
ce n'est plus « la 3D est un problème ouvert ».

### DDGI — verdict corrigé : le temporel est une optimisation, pas une exigence

Je l'avais écarté comme un cache dont l'intérêt serait d'étaler le coût sur les
images. **C'est faux, et les auteurs le disent eux-mêmes.**

L'hystérésis est un **paramètre libre** (0,85–0,98 dans les résultats 2019), et
le papier de 2021 le met **à 0** pour les sondes « Newly Awake », qui convergent
**en une seule image** en traçant beaucoup de rayons. Et sur le multi-rebond,
verbatim 2019 : « Our approach could easily be adapted to collect the per-bounce
results before display… we would still reach interactive shading rates **despite
not being able to amortize the cost of multiple bounces across frames**. »

Rien n'est en espace écran : grille monde, origines de rayons monde, encodage
indépendant de la vue. Les 8×8 / 16×16 existent pour l'occupancy GPU ; les maths
par sonde sont trois produits scalaires, un `pow` et une division.

**Le test de visibilité de Chebyshev, exactement** (du shader de référence,
`sampleIrradianceField.pix`) :

```glsl
float mean     = temp.x;                       // distance moyenne sonde->géométrie
float variance = abs(square(temp.x) - temp.y); // |E[r]² - E[r²]|
float chebyshevWeight = variance / (variance + square(max(distToProbe - mean, 0.0)));
chebyshevWeight = max(pow3(chebyshevWeight), 0.0);   // cubé "pour le contraste"
weight *= (distToProbe <= mean) ? 1.0 : chebyshevWeight;
```

C'est l'inégalité de Cantelli, exactement l'équation 5 des Variance Shadow Maps
(Donnelly & Lauritzen, I3D 2006) : `P(r ≥ d) ≤ σ²/(σ² + (d−r̄)²)`, utilisée comme
**probabilité que le point d'ombrage soit visible depuis la sonde**. Poids exactement
1 si `d ≤ r̄`.

La chaîne complète des poids, dans l'ordre : dos lissé `((n·d+1)/2)² + 0,2` (le
`max(0, n·d)` naïf est présent **mais commenté** — il éliminerait toutes les
sondes en visibilité mutuelle pour les petits détails), biais
`(n + 3·ω_o)·normalBias` (terme de vue pondéré **3×**), Chebyshev cubé,
écrasement des petits poids `w³/0,04` sous 0,2 **avant** le trilinéaire,
trilinéaire, plancher à `1e-6` (« we can't let the weight go to zero because then
all weights could be equally low and get normalized up to 1/n »), puis mélange
perceptuel en racine/carré et `E = 0,5·π·netIrradiance`.

Détail utile : ils ont **essayé et rejeté** les schémas adaptatifs de budget de
rayons — « they introduced many additional scene-dependent user parameters ».
Toutes les sondes reçoivent le même nombre de rayons.

**Conséquence pour nous** (inférence appuyée sur leur couplage documenté « a
higher self-shadow bias is necessary when there is increased variance in the
depth estimate, as would be the case when lower ray counts are used ») : avec un
budget offline, notre variance de profondeur est plus serrée, donc **on peut
tourner avec un biais plus petit et fuir moins** que les défauts temps réel.

Reste que c'est un cache d'irradiance, donc biaisé, et que Stachowiak l'a rejeté
sur Tiny Glade pour la densité spatiale et le ghosting. Mais l'argument
« impossible sans axe temporel » ne tient pas.

### ReSTIR — verdict corrigé aussi : les auteurs disent l'inverse de ce que j'ai écrit

Verbatim, conclusion du papier de 2020 : « although our GPU implementation
targets interactive rendering, **our algorithm applies equally to offline
rendering**. Temporal information may be unavailable when rendering a single
still… though **additional rounds of spatial resampling with some visibility
checks** performed along the way would presumably give samples of similar
quality. »

Et une mesure, pas une opinion (Fig. 11) : « biased spatial reuse also offers
competitive performance without relying on knowledge from prior frames. **The
lack of temporal history also limits bias propagation, and at longer render times
this method can overtake biased spatiotemporal reuse due to reduced bias.** »

GRIS (TOG 2022), dernière phrase de l'abstract : « we can also modify the
algorithm to **guarantee convergence for offline renderers** ».

Et leur travail futur pointe exactement où il faut : « extending our algorithm
beyond screen-space is an important area for future work. Of particular interest
is applying our spatial and temporal resampling algorithm to a **world-space data
structure**; algorithms such as **path space hashing [Binder et al. 2019]** may be
useful in this context. »

Ce qui a été fait : **Zhang & Wang, « World-Space Spatiotemporal Path Resampling
for Path Tracing », CGF (Pacific Graphics 2023)** — grille de hachage monde
consciente des normales, réutilisation de sous-chemins **à partir de sommets non
primaires**, **16,6 % à 41,9 % de MSE en moins pour 4,4 % à 8,4 % de temps en
plus**.

Décomposition honnête : **RIS lui-même (Talbot 2005), le reservoir sampling, la
combinaison de réservoirs et toute la théorie GRIS ne dépendent ni de l'image ni
de la frame.** Ce qui en dépend : la réutilisation temporelle (motion vectors,
M-capping à 20×, validation d'échantillons) et la réutilisation spatiale **telle
qu'implémentée** (disque de 30 pixels, rejet sur profondeur G-buffer à 10 % et
normale à 25°). L'idée de voisinage, elle, demande juste **un point dont
l'intégrande se ressemble** — le pixel est une commodité.

**Aucun moteur offline de production n'a livré de resampling ReSTIR.** Vérifié :
zéro chez SPI Arnold, Manuka, Hyperion, pbrt-v4. La seule vraie tentative est la
PR Cycles « WIP: Cycles: Importance Resampling » de Weizhen Huang, **ouverte,
non mergée**, notée « Currently on hold » le 2025-02-05. Piège de nommage à
connaître : `sample_reservoir()` dans `kernel/light/tree.h` de Cycles est du
reservoir sampling pondéré ordinaire, **pas du RIS** ; et le « RIS » de
RenderMan est le **RenderMan Integrator System**, aucun rapport.

### Le reste : surfels, Lumen, écran

**Surfels / GIBS (EA SEED)** : deux des trois piliers sont en espace écran (spawn
depuis le G-buffer par tuiles 16×16, application par pixel) et l'estimateur MSME
est explicitement inter-images — « infinite bounce **over time** ». Non portable
tel quel. **Quatre pièces survivent** : la grille spatiale graduée par distance,
la **profondeur radiale de Chebyshev par surfel** (même mécanisme que DDGI), le
ray guiding par carte hémisphérique **6×6 en CDF inverse**, et le **budget de
rayons piloté par la variance** avec état dormant pour les surfels convergés.

**Lumen** : chacun de ses étages existe pour **éviter de tracer la vraie
géométrie** — or on a déjà un BVH de production sur des milliards d'instances. Sa
hiérarchie résout un problème qu'on n'a pas, et le *voxel lighting* n'existe que
parce qu'un hit sur le SDF global est **anonyme** (« we don't know which mesh
instance was hit ») — chez nous il est identifié, donc cet étage disparaît.

Trois choses à leur voler quand même, toutes indépendantes de la frame et de
l'écran : le **World Space Radiance Cache** avec son *ray-shortening* et sa
**parallaxe sphérique** (reprojeter l'intersection du rayon de sonde écran sur la
sphère de la sonde monde, en acceptant l'erreur directionnelle plutôt que
l'erreur de position) ; le **product importance sampling** sur quadtree
octaédrique, dont Epic dit eux-mêmes « normally only possible in offline
rendering » ; et l'heuristique de filtrage inter-sondes — **clamper la distance
de hit du voisin à la sienne propre pour préserver les ombres de contact**.

À savoir avant de réinventer : Epic a implémenté le **cone tracing et l'a
rejeté** — « we'd always have to choose between leaking or over-occlusion, and we
could never resolve the lighting from a small distant window ».

**Espace écran (GTAO, SSR, SSAO)** : zéro transfert, et la motivation elle-même
s'évapore chez nous — ces passes existent pour rattraper l'écart entre le
G-buffer rasterisé et la scène de tracing approximative. Un intégrateur CPU sur
triangles exacts n'a pas ce problème. Le chiffre qui résume la famille : GTAO
calcule **une seule direction d'horizon par pixel et par image**, et reconstruit
96 directions effectives à partir d'un voisinage 4×4 **et de 6 rotations
alternées dans le temps**. Retirer le temporel divise son échantillonnage par 6.

---

## 8. La feuille de route

**Le point de départ est acquis, pas à démontrer.** L'indirect est la faiblesse
de Clarisse. Dix ans d'usage le disent ; ce document n'a pas à le remesurer, et
le rendu coûte de l'argent. Ce qui suit part de là.

**0. Allumer l'échantillonnage adaptatif déjà présent — zéro ligne de C++.**
Voir §2.5. Tout le mécanisme est écrit, y compris le filtrage de la carte de
variance et la métrique perceptuelle ; un seul attribut à zéro le désactive.
C'est le premier geste parce que c'est le seul qui ne demande rien.

Piège documenté par Hyperion : sans clamp de l'estimation de variance, les
fireflies aspirent tout le budget — d'où `refinement_variance_filter` et le mode
`Contrast`. Et la limite avouée de leur propre implémentation : un rendu à
128 spp n'a que **trois** occasions de s'adapter, après 16, 32 et 64.

**0 bis. Instrumenter la sélection de lumières native.** Compter les rayons
d'ombre par point d'ombrage sur une scène à beaucoup d'émetteurs. **Si le nombre
croît avec le nombre de lumières**, la stratégie est la boucle par lumière, celle
que Hyperion mesure **9,4× plus lente** que l'uniforme à 4,9 M de lumières — et
l'étape 5 devient le plus gros gain disponible. C'est de l'instrumentation, pas
un banc d'essai : ça se lit dans un compteur, pas dans une série de rendus.

**1. L'intégrateur miroir.** Reproduire le path tracer natif à l'identique,
validé image par image. Sans lui, aucune comparaison n'est possible. Avis
explicite du cours 2025 : comparer **à échantillons égaux** pendant tout le
développement, passer au temps égal seulement à la fin.

**2. MIS compensation.** Trois jours, 2,75× sur l'IBL, aucun risque.

**3. Une semaine de fondations, sans état.** Clamp par contribution en norme L1
avec séparation par profondeur (dix lignes, copiées de Cycles), RR par albédo, RR
sur les échantillons de lumière **avant** le rayon d'ombre, et le **plancher de
rugosité** — un float d'état de chemin, jusqu'à **2,5× à bruit égal** au preset
prudent d'OPSR. Deux préalables avant d'écrire la rugosité : la correction des
poids MIS (Jendersie & Grosch), et l'extraction d'une rugosité de Beckmann par
différences finies (Holzschuch et al.), puisque `PbrBxdf` ne nous en donnera pas.

**3 bis. G-MoN pour les fireflies.** Médiane de M moyennes par pixel, adaptée par
coefficient de Gini. **+1,17 % de coût, < 500 ko par bucket 32×32, aucun
voisinage**, donc aucun problème de bord de tuile. La seule méthode
anti-fireflies de la littérature qui tienne dans notre contrainte, et elle est
déjà implémentée dans pbrt-v4. M entre 11 et 25.

**4. Le sampler Owen-scramblé.** Une semaine, code de référence dans le papier.

**5. Le light BVH.** Le gros morceau et le gros gain. Avec `GasTreeSplitAlgorithm`
surchargé, les listes d'exclusion de Karma, et l'esquive MIS de MoonRay.

**6. L'instrumentation.** Efficacité par lumière = utiles/total, nombre moyen de
lumières échantillonnées, top 10 des inefficaces. **Sans ça, rien n'est
réglable.**

**7. Le cache de radiance et la path simplification.** *(Recherche sur le biaisé
en cours.)*

**8. Le guidage**, et seulement si les mesures de l'étape 6 montrent que le
bruit est bien de l'indirect difficile.

**9. Les caustiques**, avec la contrainte de buckets réglée d'abord.

---

## 9. Les pièges

0. **Livrer un interrupteur « vérité terrain » dès la première ligne.** C'est la
   leçon opérationnelle la plus chère du dossier, et elle vient d'un échec de
   Disney. Leur *shading LOD* : « it **increased rather than reduced memory
   use**… it sometimes **slowed the overall rendering**… **Any artifact in the
   image immediately drew the question "Did you try disabling shading LOD?"**
   Ultimately, the benefit did not justify the cost and this feature was
   removed. » Hyperion et Arnold gardent tous deux un mode de référence pour la
   path simplification. Sans ce bouton, **chaque artefact de chaque image
   devient la faute de notre intégrateur**, et on passe son temps à se
   défendre au lieu d'avancer.
1. **`cb_pre_render` reçoit un `const CtxShader&`.** Le premier test à écrire.
2. **Le baseline n'est pas un path tracer de papier.** Clarisse a déjà un LAS, du
   clamping, un mode progressif, un sampler, une régularisation.
3. **Le guidage rend les fausses images plus fausses.** Guidage + clamping +
   simplification interagissent ; l'image *change* et on ne sait plus si c'est un
   bug ou une correction. Tout désactiver, réactiver un par un.
4. **La leçon de RenderMan XPU** : son 2 à 10× sur RIS vient de la disposition
   des données et de files d'attente, **pas d'un meilleur estimateur**. Pixar :
   *« The focus on data-parallel design is pervasive throughout XPU, and it is
   largely responsible for the improved CPU performance »*. Si notre intégrateur
   est lent, l'algorithme n'est peut-être pas le coupable. MoonRay utilise des
   **régions volontairement chevauchantes** pour réduire la contention entre
   threads sur son arbre d'erreur partagé.
5. **Un échantillonneur d'environnement conscient de l'occlusion est une usine à
   fireflies** quand la pdf apprise sous-estime une direction. Les release notes
   de V-Ray corrigent ça à répétition. Coupler avec un clamp dès le premier jour.

---

## 10. Le code à lire

Tout est permissif et lisible.

| Source | Pour quoi |
|---|---|
| **pbrt-v4** `src/pbrt/lightsamplers.{h,cpp}` | Light BVH Conty–Kulla, **activé par défaut**, avec le chapitre de livre gratuit |
| **MoonRay** `lib/rendering/pbr/light/LightTree.h` | Même papier, cité par numéro de section, + splitting adaptatif. Apache-2.0 |
| **MoonRay** `lib/rendering/pbr/doc/SamplingStrategy.tex` | Table par point d'ombrage choisissant BSDF-IS / MIS / light-IS selon rugosité × angle solide. **Le genre de chose qui ne finit jamais dans un papier** |
| **MoonRay** `rndr/adaptive/AdaptiveRegions.{h,cc}` | Le motif de threading à régions chevauchantes |
| **MoonRay** `RenderStatistics.cc:1348-1382` | L'instrumentation à copier |
| **Cycles** `kernel/sample/sobol_burley.h` | Owen scrambling haché |
| **Cycles** `kernel/light/tree.h` | Light tree sans splitting, avec réservoirs WRS |
| **Cycles** `kernel/integrator/mnee.h` | MNEE, Hanika et al. 2015 |
| **LuxCore** `src/slg/engines/pathtracer.cpp` | Le garde `IsCausticPath` back/forward hybride |
| **appleseed** `renderer/kernel/lighting/` | SPPM avec **importons**, BDPT, light tracing, light tree. MIT, **définitivement gelé** |
| **OpenPathGuidingLibrary/pbrt-v4**, branche `path-guiding` | L'intégration Open PGL la plus propre |

---

## 11. Ce qui n'est pas établi

- **L'algorithme du sampler de lumières de 3Delight**, du Global Light Sampling
  d'Arnold, du `selectionlearningscheme` de RenderMan, de l'Adaptive Environment
  Sampler de Corona, et la structure du light tree de Karma. **Cinq boîtes
  noires** : le comportement est documenté, la méthode ne l'est pas.
- Le denoiser de 3Delight — non nommé nulle part. Leur algorithme de caustiques
  non plus.
- **Aucun chiffre de gain publié pour le path guiding** hors de Disney (2,16× à
  32 spp sur *Zootopia 2*). Ni Pixar, ni SideFX, ni Chaos, ni Blender.
- **L'algorithme derrière le Global Light Sampling d'Arnold reste inconnu** —
  grep sur toute leur doc GLS pour `tree|bvh|hierarch|cluster|resampl|reservoir` :
  **zéro occurrence**, et l'index de recherche d'Autodesk **ne liste pas** le
  papier de Conty/Kulla. « Autodesk Arnold livre un light tree » n'est **pas
  établi**.
- L'échantillonnage de lumières de RenderMan 26/27 : leur doc est une SPA
  JavaScript, non lisible. Ce qu'on peut lire (RM20) décrit une **répartition de
  budget par lumière**, pas une hiérarchie.
- Aucun chiffre pour le light tree de Cycles ni pour l'adaptive light sampling de
  MoonRay.
- Le comparatif de 3Delight date de 2016 et est publié par le vendeur.

Ce qui **n'est pas** une inconnue, et qu'il ne faut pas re-questionner : la
faiblesse de Clarisse en indirect. C'est le constat qui fonde le projet.
