# Rendu non-photoréaliste dans Clarisse

État au 2026-09-08. Deuxième chantier ouvert ce jour, mené en parallèle des
courbes. Aucun code écrit — c'est un dossier de préparation.

---

## 1. Ce que Romain vise

Quatre couvertures de romans SF, données comme cibles. Elles sont très
éloignées les unes des autres, et c'est voulu : il veut **une boîte à outils
très complète, pour beaucoup de styles**, pas un look.

| Référence | Ce qu'elle demande techniquement |
|---|---|
| **Dufour, *Outrage et rébellion*** (Folio SF) — encre et lavis | le support domine : papier laissé vide sur 80 % de la surface, traits gestuels, éclaboussures qui n'obéissent à aucune géométrie, rouge saturé qui déborde du trait, bords de lavis plus sombres que leur centre |
| **Hamilton, *Pandora's Star*** (Del Rey) — acrylique empâtée | touches de brosse **visibles et directionnelles, qui suivent la forme** (donc espace objet), relief de la peinture qui accroche la lumière, détails suggérés et non décrits, bleu froid contre orange chaud |
| **Hamilton, *Pandore menacée*** (Bragelonne) — concept art atmosphérique | la brume **est** la structure : plans séparés par l'atmosphère, valeurs écrasées au fond, contraste réservé au premier plan, silhouettes lisibles avant tout détail |
| **Hamilton, *Judas démasqué*** (Milady) — illustration numérique dure | aplats saturés, peu de dégradés, et surtout **des ombres violettes et non noires** : l'obscurité est remplacée par une couleur, pas par une absence |

**Aucune des quatre n'a de contour noir.** Même la première : ses traits sont
gestuels, pas des contours d'objets. C'est le réflexe du NPR anime, et ce n'est
pas ce qu'il vise — ça déclasse la brique « outline » dans les priorités.

Ce qu'elles partagent : une **palette contrainte** avec une dominante et un
accent complémentaire, et un **contraste chaud/froid** plutôt que de luminance.
Jamais la palette naturelle de l'éclairage.

Autres réponses de cadrage : **image fixe *et* animation** (donc la cohérence
temporelle est un problème de premier ordre, pas un raffinement) ; ça doit
marcher **sur ses scènes existantes** si possible, mais **des matériaux dédiés
sont acceptables**.

---

## 2. Les quatre axes

Les quatre références ne demandent pas quatre systèmes, mais le même petit
noyau réglé à quatre endroits :

| axe | rôle | Dufour | Pandora | Pandore | Judas |
|---|---|---|---|---|---|
| **tonal** | la lumière → une valeur | binaire | doux | écrasé | quantifié dur |
| **chromatique** | la valeur → une couleur | 3 tons | bleu/orange | chaud/froid brumeux | ombres violettes |
| **matière** | comment la surface est déposée | encre, bavure | touche empâtée | — | aplat |
| **densité** | comment le détail se dissout | vide | touches lâches | brume | — |

Les axes **chromatique** et **densité** ne demandent que la beauté et `depth`.
Ils marchent sur les scènes existantes sans toucher au shading, et livrent à
eux seuls la troisième image en entier. C'est par là qu'il faut commencer.

L'axe **matière** est le seul qui demande vraiment du travail, et il se sépare
en deux : la touche qui suit la forme relève du shading (espace objet), tandis
que l'empâtement se fait en post — on génère une carte de normales depuis la
touche et on ré-éclaire l'image en lumière rasante.

---

## 3. La loi d'organisation : chaque effet appartient à un espace

Le *shower door* n'est pas un défaut à corriger après coup, c'est le symptôme
d'**un effet placé dans le mauvais espace**.

| espace | ce qui y appartient | s'il est ailleurs |
|---|---|---|
| **objet** | hachures, granulation sur la matière, usure, zones peintes | l'objet glisse sous un rideau |
| **caméra** | contours, épaisseur de trait constante à l'écran | le trait grossit avec la distance |
| **support** | grain du papier, texture de toile | la feuille se met à suivre les objets |

Le papier *doit* rester fixe à l'écran — c'est une feuille. Les hachures
*doivent* coller à l'objet. Chaque brique de la boîte déclare donc son espace,
et c'est ça qui décide où elle est implémentée.

**L'atout de Clarisse, que le NPR moderne n'exploite pas** : presque tout le NPR
récent est du post-process 2D, parce qu'il vient du temps réel et du GPU.
Clarisse évalue ses textures **à la surface, en 3D, pendant le rendu**. Tout ce
qui appartient à l'espace objet peut donc être fait dans le shading, où la
cohérence temporelle est gratuite par construction — pas d'advection, pas de
motion vectors, pas de mip-maps de bruit.

---

## 4. Ce que Clarisse a déjà — l'inventaire

### Le contour est déjà écrit : `SubPixelFilterOutline`

La doc utilisateur l'annonce mot pour mot pour « blueprints or cel-shading
style renderings ». **Quatre détecteurs indépendants**, chacun avec sa largeur,
sa couleur et son alpha propres — exactement la séparation qu'il aurait fallu
écrire :

| trait | attributs |
|---|---|
| silhouette | `silhouette_width` |
| contours intérieurs | `inner_width` |
| arêtes vives | `normal_threshold` (angle, défaut 40°) + `normal_width` |
| frontière de matériau | `use_shading_group` + `shading_group_width` |
| discontinuité de profondeur | `depth_threshold` + `depth_width` |

Plus `line_width` en subpixels, `outline_mode` (`Fast` / `Accurate` — ce
dernier gère motion blur et profondeur de champ), `geometry_group` pour
restreindre à un sous-ensemble. **Tous les attributs de couleur, largeur et
alpha sont `texturable yes` et `shading_variable yes`** : le trait peut être
piloté par une texture ou varier par objet, sans dupliquer de nœud.

### Rendre sans éclairage — trois voies natives

- `MaterialMatte` — « with no light shading », sa `color` est texturable ;
- `LayerScene.override_material` — un `MaterialMatte` branché là donne un pass
  flat global en un clic ;
- `IntegratorUtility` — `Raw Color` / `Simple Shading` / `Textured Color`.

### Les briques de shading utiles

| Besoin | Node |
|---|---|
| **quantification / posterize** | **`TextureQuantize`** — quantifie une couleur selon un pas, lui-même texturable donc variable par pixel |
| rampe | `TextureGradient` (`gradient[4]`), modes Incidence / Slope / Parametric / UV |
| remap par courbe | `TextureRemap` (`curve[4]`) en shading, `ImageFilterRemap` (`curve[3]`) en 2D |
| rim / facing ratio | `TextureIncidence` (`front`, `rim`, `exponent`), `TextureFresnel`, `TextureUtility` → `Is Facing?` |
| courbure | `TextureCurvature` (`Gaussian` / `Mean` / **`Sampled`** par ray-casting), ou `TextureUtility` → `dNdu` / `dNdv` (gratuit) |
| cavité | `TextureOcclusion` mode `Inner`, avec ses deux courbes de falloff |
| hachures | `TextureModulo` |
| bruits | Perlin, Fractal, **Cellular (Worley)**, Checker, Grid, Random |
| wireframe | `TextureWireframe` (rayons primaires seulement) |
| shader arbitraire | **OSL** (`TextureOslFile`, `TextureOslScript`) |

### Le point qui règle la cohérence temporelle nativement

`TextureSpatial` (base de toutes les textures projetées) offre
`projection = Camera`, avec `camera_occlusion` **et `sticky_projection`** — la
projection est alors faite à un **frame de référence** au lieu du frame
courant. C'est l'ancrage anti-shower-door, natif, sans code.

Et `TextureUtility` sort `Camera Sample Position (Pixel Space)` : les
coordonnées écran par fragment, de quoi ancrer une trame sans passer par une
projection.

### Le rail à connaître : `Layer.output_layer` + `LayerReference`

`Layer.output_layer` permet de faire d'un AOV le **RGBA** d'un layer.
`LayerReference` « references an image layer of the current image as a new
layer, typically used to add a stack of 2D filters on an existing layer ».

Ensemble : un edge-detect posé sur un `LayerReference` réglé sur
`output_layer = depth` ou `world_normal` marche **sans une ligne de code côté
moteur**. Seul le kernel est à écrire.

### Les AOV

133 buffers intégrés. Convention `groupe.composante`. Les utiles :
`world_normal` **et** `camera_normal` (les deux espaces sont natifs),
`camera_position`, `depth`, `albedo`, `motion_vector` (exige
`motion_blur_mode = Motion Vectors` sur le raytracer), la famille
`pbr_*_i_direct` / `pbr_*_i_indirect` pour le direct/indirect séparé, les
cryptomattes.

> **Attention** : la doc officielle écrit **`depth.z` en minuscule**. Nos notes
> antérieures disent `depth.Z`. À vérifier au premier usage.

Ce qui n'est pas built-in mais se fabrique en trois nœuds, via `TextureUtility`
puis `AovStore` : position monde, Object ID, Shading Group ID, UV, occlusion
(`TextureOcclusion`), courbure (`TextureCurvature`).

Deux contournements utiles : `SubPixelFilterMaxDiff` écrit « la différence
maximale entre échantillons caméra » dans n'importe quel buffer — c'est une
edge-map gratuite ; `SubPixelFilterForegroundBackground` donne un matte
fond/sujet.

### Ce qui manque, et qu'il faudra écrire

Posterize 2D, edge detect image-space, sharpen, LUT 3D en filtre, lavis /
aquarelle / bleed, `MaterialToon` (reconstituable via `MaterialMatte` +
`TextureIncidence` + `TextureQuantize`). `TextureStreak` n'existe pas.

### Les bases de filtre dérivables

Les cinq classes de filtre sont **toutes abstraites**, donc toutes dérivables :

| Base | Callbacks | Verdict |
|---|---|---|
| `KernelFilter` | `cb_pre_filter`, `cb_filter`, `cb_post_filter` | connu, déjà utilisé par `bokeh` |
| `PixelFilter` | `cb_pre_filter`, `cb_apply_filter` | dérivable |
| `WholeImageFilter` | idem KernelFilter | dérivable, mais `CtxWholeImageFilter` **n'a pas de `channel_r/g/b/a`** — à élucider avant de s'y engager |
| **`SubPixelFilter`** | 11 callbacks | **la voie royale** |
| `ImageFilter` | aucune structure de callbacks | inutilisable |

**`SubPixelFilter` est le point d'entrée le plus riche du logiciel pour du
NPR.** Ses contextes donnent accès aux buffers de sortie R/G/B/A, à un
`AOVDescriptor` (nom → indices de canaux), aux **tableaux d'AOV par pixel**,
aux buffers deep, au noyau d'antialiasing — et `cb_pre_shade` / `cb_post_shade`
reçoivent un `CtxShader&` et un `CtxRaytrace*`, donc le fragment complet :
intersection, matériau, shading group, normale géométrique **ou** lissée.
C'est exactement le niveau d'accès dont dispose `SubPixelFilterOutline`.

### L'antialiasing est sur le renderer, pas sur le layer

`RendererRaytracer`, groupe `sampling>advanced_anti_aliasing` : sample pattern
(`Stratified` / `Random` / `Stratified Jitter` / **`Blue Noise`**), filtre de
reconstruction (Box, Triangle, Gaussian, Blackman-Harris, Mitchell, Lanczos),
et **le mode d'intégration** : `Splatting` accumule dans les pixels voisins,
`Importance Sampling` seulement dans le pixel courant. Ce dernier est celui que
réclament les débruiteurs — et le NPR.

À noter aussi : `tone_mapping_curve` (`curve[3]`) remappe la couleur des
**sous-échantillons avant le filtrage pixel**. Très pertinent pour du NPR.

---

## 5. La simulation d'encre et d'aquarelle

Dossier de recherche complet, vérifié papier en main. Les PDF sont dans
**`J:\Clarisse-SDK\papers\`** (209 Mo, hors dépôt).

### Le meilleur candidat pour un node Clarisse : le LBE de MoXi

**Chu & Tai, *MoXi: Real-Time Ink Dispersion in Absorbent Paper*, ACM TOG
24(3):504-511, 2005.** Lattice Boltzmann D2Q9, BGK à un temps de relaxation :

```
f_i(x + e_i·Δt, t + Δt) = (1 − ω)·f_i(x, t) + ω·f_i^eq(x, t)
f_i^eq = w_i · { ρ + ρ_0·ψ·[ (3/c²)(e_i·u) + (9/2c⁴)(e_i·u)² − (3/2c²)(u·u) ] }
ψ = smoothstep(0, α, ρ)          α ∈ [0.2, 0.5]
```

`w_0 = 4/9`, `w_{1..4} = 1/9`, `w_{5..8} = 1/36`, `ω = 0.5`. Le facteur `ψ` sur
les seuls termes d'advection est ce qui permet une frontière libre sans creuser
de densités négatives.

**Pourquoi celui-là** : le LBE est **purement local** — pas de résolution de
Poisson, donc tuilable en OpenMP sans synchronisation globale, contrairement au
solveur de Stam qu'utilise Van Laerhoven. C'est l'argument explicite de Chu.
Coût mémoire : ~150 Mo en 2K, ~600 Mo en 4K.

Perméabilité variable par bounce-back fractionnaire, avec un champ de blocage
`κ = k1 + k2·G + k3·A + k4·g + k5·h` (grain du papier, alun, colle,
accumulation). Pinning des bords par un seuil `σ`. Edge darkening par
évaporation inégale (`ε_s ≤ 0.005` sur ρ, `ε_b = 5e-5` sur les `f_i` bouncés).

**Correction à une idée reçue** : MoXi ne contient **aucun terme de tension de
surface**. L'évolution spontanée des taches vient de la frontière libre, du
pinning et de l'évaporation différentielle.

### Le plus simple à coder en premier

**Wang, Li & Zhu, JPCS 1004:012026, 2018** (open access, PDF récupéré). Modèle
de tubes capillaires : un automate cellulaire à 4 voisins, sept formules
scalaires, aucune vitesse, aucun champ vectoriel.

```
Q_ij = 0                              si S_i < S_t
       −(Φ(i)²/μ)·(S_i − S_j)         si S_i ≥ S_t
S_t  = S_water + e^(−ω_mean)·(S_ink − S_water)
```

Et surtout, il se pilote par la géométrie et non par un pinceau :
`k_i = |fwidth(normal)| / |fwidth(position)|` — la courbure par pixel, qu'on a
gratuitement avec nos AOV.

### L'edge darkening en une ligne, si on ne veut pas simuler

**Curtis et al., *Computer-Generated Watercolor*, SIGGRAPH 97** (PDF récupéré) :

```
p ← p − η·(1 − M')·M          M = masque de zone mouillée, M' = M flouté (K=10)
                              η ∈ [0.01, 0.05]
```

C'est un pur filtre image : un flou gaussien et une soustraction. À essayer
**avant** toute simulation.

### Si on veut des coulures

**Herson, Paris & Michel (Adobe), *Dripping Thin Films for Real-time Digital
Painting*, CGF 45(2), Eurographics 2026** — le plus récent et le plus
proprement dérivé. Approximation de lubrification, stencils locaux, trois
paramètres exposés à l'artiste (épaisseur, fluidité, hydrophobie). Son schéma
explicite impose `Δt ∝ Δx⁴`, ce qui le limite en temps réel — **en offline
c'est justement là qu'on gagne**, on peut faire les sous-pas qu'ils ne peuvent
pas se permettre.

### Attention licence

**Mixbox** (Sochorová & Jamriška 2021, mélange de pigments Kubelka-Munk,
`github.com/scrtwpns/mixbox`) est en **CC BY-NC** : usage non commercial
uniquement. Le dépôt est en GPL-3.0 et public — il faut implémenter
Kubelka-Munk à la main depuis les formules de Curtis/Chu, qui sont dans la
littérature. Chu note d'ailleurs que **KM est théoriquement inadapté à
l'aquarelle** (les pigments se logent entre les fibres, ils ne forment pas des
couches translucides empilées) et qu'un mélange soustractif simple est
souvent plus juste et plus prévisible.

### Le code libre le plus proche de notre cas

**`github.com/semontesdeoca/MNPR`** — framework NPR temps réel pour Maya, avec
une pipeline aquarelle complète en GLSL pilotée par G-buffer
(`quadPigmentApplication`, `quadEdgeManipulation`, `quadSubstrate`,
`quadGapsOverlaps`, `quadColorTransform`). Copié dans
`J:\Clarisse-SDK\papers\mnpr\`. La thèse associée est libre : Montesdeoca,
NTU Singapour 2018, DOI `10.32657/10220/47356`.

---

## 6. Ordre de travail proposé

1. **Étalonnage par la profondeur** (axes chromatique + densité) — un
   `PixelFilter` ou un `KernelFilter` lisant `depth`. Marche sur les scènes
   existantes, livre la troisième image, ne demande aucun changement de scène.
2. **Le posterize et la palette** — d'abord monter `TextureQuantize` +
   `MaterialMatte` sans écrire de code, pour voir ce qui manque vraiment.
3. **L'edge darkening de Curtis** — un flou et une soustraction.
4. **La touche empâtée** — normales générées depuis une texture de touche,
   ré-éclairage rasant. C'est l'image 2, et c'est le morceau qui a le plus de
   valeur ajoutée.
5. **Le `SubPixelFilter` custom** — seulement quand on saura ce que
   `SubPixelFilterOutline` ne sait pas faire. Pas avant.

Le socle est commun avec le débruiteur : filtre plein écran plus lecture des
AOV. Ce qu'on écrit pour l'un sert à l'autre.

---

## 7. Ce qui n'a pas été fait

L'agent chargé de l'état de l'art général (extraction de contours, abstraction,
cohérence temporelle, pipelines de production — Spider-Verse, Arcane, Klaus,
Mitchells) avait quatre de ses cinq axes rentrés quand il a été arrêté pour la
pause. **Sa synthèse n'a pas été rendue.** Les PDF qu'il avait rassemblés sont
en revanche sauvés dans `J:\Clarisse-SDK\papers\` : Bousseau 2006 et 2007
(advection bidirectionnelle de texture — la réponse « cohérence temporelle »),
Luft 2005/2006, Doran, Grabli 2004, Kass 2011, Montesdeoca 2016/2017,
Deegan 1997 (l'effet coffee-ring, la physique derrière l'edge darkening).

À reprendre là si le sujet redevient prioritaire.
