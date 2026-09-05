# Ce qu'on a appris du SDK Clarisse

Isotropix a fermé en 2023 sans jamais publier de SDK. Tout ce qui suit a été
établi en reconstruisant les en-têtes depuis l'installation, en lisant la
documentation hors ligne archivée, et surtout **en mesurant** : la moitié de ces
points contredit ce qu'on aurait déduit de la documentation, quand elle existe.

Document vivant. On l'alimente à chaque découverte. Chaque affirmation vaut ce
que vaut sa vérification — quand un point n'a pas été mesuré, c'est écrit.

Le SDK reconstruit vit à `J:\Clarisse-SDK` (en-têtes, bibliothèques d'import,
archive de documentation, `.cid` extraits). Il n'est pas dans ce dépôt.

---

## 1. Le système de modules

Une extension native est un triplet :

```
   ma_node.cid          déclaration de la classe et de ses attributs
        │  cmagen.exe
        ▼
   ma_node.cma          en-tête généré : classe encodée + declare_class + create_cma
        │  #include
        ▼
   ma_node.cpp          le code, qui exporte on_register_module
        │  cl + link
        ▼
   ma_node.dll          déposée dans un dossier que Clarisse scanne
```

Le `.cpp` inclut le `.cma`, exporte `on_register_module`, et enregistre ses
callbacks via `IX_BEGIN_DECLARE_MODULE_CALLBACKS` / `IX_CREATE_MODULE_CLBK`.

### cmagen déduit la bibliothèque depuis le nom de la classe de base

C'est le piège d'entrée. Pour résoudre une classe de base, `cmagen` transforme
son nom en nom de bibliothèque : `Camera` → `camera.dll`, qui existe, donc ça
marche. Mais `CameraPerspectiveAdvanced` vit dans `cameras.dll` — la déduction
échoue et la classe reste introuvable.

Le contournement : dériver de la classe de base qui, elle, se résout, et
recopier les attributs de la classe intermédiaire depuis son `.cid` extrait.
C'est ce que fait `CameraBokeh`, qui dérive de `Camera` en portant les attributs
de `CameraPerspectiveAdvanced`.

Options utiles : `-cid_path`, `-search_path`, `-module_path`, `-output_path`,
`-split`, `-slim`, `-monolithic_compatible`, `-icons_file`.

**`cmagen help` plante** : il traite `argv[1]` comme un fichier. Pour découvrir
ses options, lui passer un vrai `.cid`.

### Vérifier qu'une classe de base est dérivable

Une sonde de dix lignes suffit, et c'est la seule réponse fiable :

```
class "Sonde" "<ClasseDeBase>" { ui_name "x" category "y" }
```

puis `cmagen sonde.cid -module_path <install>/module`. S'il produit un `.cma`,
la classe se dérive. Établi ainsi pour `KernelFilter`, `Camera` et `Tool`.

---

## 2. Charger un module sans que l'utilisateur ne fasse rien

**Il n'existe aucune variable d'environnement pour ajouter un chemin de
modules.** `app_env.h` est exhaustif, on l'a lu. Et `-module_path`
**remplace** le chemin par défaut au lieu de s'y ajouter — le passer seul
prive Clarisse de tous ses modules natifs.

Trois faits qui, ensemble, donnent le chargement automatique :

1. `AppObject::scan_modules(CoreStringVector)` ajoute des chemins **à
   l'exécution**, depuis Python (`ix.api.CoreStringVector()`).
2. `CLARISSE_STARTUP_SCRIPT` est honorée, et accepte plusieurs scripts séparés
   par des `;`.
3. Clarisse exécute ces scripts via `PyRun_String` — donc **`__file__` n'y est
   pas défini**. Un lanceur qui se cherche lui-même échoue ; il faut y écrire
   le chemin racine en dur au moment de l'installation.

D'où le montage retenu : `install.py` écrit un lanceur à côté de
`clarisse.env` avec la racine en dur, et l'ajoute à `CLARISSE_STARTUP_SCRIPT`.
Le lanceur appelle `scan_modules` sur `native/build`. Les DLL sont donc lues
directement depuis l'arbre de compilation : reconstruire suffit, il n'y a rien
à recopier.

---

## 3. Les pièges qui font planter Clarisse

### `declare_module` doit appeler `set_object`

```cpp
OfModule *
IX_MODULE_CLBK::declare_module(OfObject& object, OfObjectFactory& objects)
{
    MonModule *module = new MonModule;
    module->set_object(object);      // <-- sans ça, plantage
    return module;
}
```

`OfModule::is_protected()` déréférence `m_object` sans le tester, et
`AppObjectImpl::on_object_factory_event` l'appelle. **L'exemple minimal de la
documentation officielle omet cette ligne et fait planter Clarisse.**

### La signature des callbacks n'est pas celle de la documentation

La doc écrit `ModuleObject *` ; le vrai typedef dit `OfModule *`
(`of_class.h:35-36`). Le compilateur ne le rattrape pas toujours.

### Ne jamais tuer `clarisse.exe`

Les rendus de test passent par `cnode.exe`. `clarisse.exe`, c'est la session
interactive de l'artiste. Un `taskkill` sur un motif trop large a déjà fermé
une session de travail.

---

## 4. Le langage CID, tel qu'il marche vraiment

Types observés et fonctionnels : `double`, `long`, `bool`, `percentage`,
`angle`, `distance`, `subpixel`, `color`, `reference`, `tag`, `string`,
`filename`.

```
        percentage "roundness" {
            doc "Texte affiché en infobulle. Il sert à quelque chose : c'est la
                 seule documentation que l'artiste verra jamais."
            value 0.0
            numeric_range yes -1.0 1.0     # bornes dures
            ui_range yes -1.0 1.0          # bornes du curseur
            slider yes
            animatable yes
        }
        long "mode" {
            value 0
            preset "Libellé lisible" "0"
            preset "Autre" "1"
        }
        reference "focus_object" { filter "SceneItem" value "" }
        tag "depth_aov"          { filter "aov_groups" value "" }
```

Il existe aussi un type **`action`**, qui pose un BOUTON sur la node et
déclenche du C++ quand on clique dessus :

```
        action "bake_now" { doc "Lance le calcul." }
```

`cmagen` en tire un `DECLARE_ATTR_ACTION` et enregistre le callback sur
l'attribut. C'est ce qui permet une node « qui fait quelque chose » plutôt
qu'une node qui se contente d'évaluer — un bake, un export, une reconstruction
de cache. Vérifié par sonde ; le corps n'a pas encore été écrit.

Deux pièges de type :

- **Les attributs `angle` sont stockés en DEGRÉS** et le `.cma` ne convertit
  rien. Lus tels quels comme des radians, 5° demandés donnent 286° réels.
- **Les attributs exprimés en pixels doivent être multipliés par
  `ctx.resolution_multiplier`.** Un flou de 5 px fait 5 px à 100 %, 2,5 à 50 %,
  10 à 200 %. Sans ça, l'aperçu ne ressemble pas au rendu final.

Les `\n` dans une chaîne `doc` passent : on peut y écrire plusieurs
paragraphes, ce qui vaut mieux qu'un pavé.

---

## 5. Le contrat `CtxKernelFilter`, mesuré

**Entièrement non documenté.** Établi par mesure et par désassemblage. C'est
le point le plus coûteux à redécouvrir, donc le plus utile à écrire.

| Champ | Ce qu'il est vraiment |
|---|---|
| `ctx.image` | proxy SOURCE, **déjà élargi** de `kernel_radius` |
| `ctx.region` | la tuile en coordonnées PROXY : `{r, r, w, h}` |
| `ctx.channel_*` | DESTINATION, indexée `y * region.width + x`, **sans marge** |
| `ctx.x0`, `ctx.y0` | la tuile en coordonnées image absolues |
| `ctx.image_quality` | **inutilisable dans `filter`** : reçoit `(int) resolution_multiplier` |

Autres faits :

- Les bords du proxy sont remplis par **répétition du bord** (clamp).
- `ctx.channel_*` arrive **pré-remplie avec la source**. Renvoyer `true` valide
  la tuile, `false` la jette — donc un filtre qui ne fait rien renvoie `true`.
- Dans `pre_filter`, **seul `source_image` est valide** ; `dest_image` porte un
  pointeur invalide et `x0`/`y0` du bruit.
- `pre_filter` tourne une fois, sur le thread appelant. `filter` tourne sur les
  threads du pool, une fois par tuile. **Tout ce qui est global à l'image se
  calcule dans `pre_filter`** — sinon on le refait des centaines de fois, et
  pire, deux tuiles peuvent arriver à des conclusions différentes.
- Les réglages se relisent dans une copie **locale** à chaque tuile. Un global
  partagé se fait écraser par une autre instance du filtre ou par une seconde
  évaluation concurrente ; le symptôme est le pire qui soit, des réglages qui
  ne font « pas tout à fait » ce qu'on demande, par intermittence.

---

## 6. Les pixels et les AOV

`ImageProxy` n'alloue que **cinq** canaux : `r g b a l`. Il ne donnera jamais
accès à autre chose, et un canal absent rend un pointeur nul — le lire sans
garde plante.

Un AOV s'atteint autrement :

```
   ctx.source_image                  ImageCanvas
        │  get_image()
        ▼
   ImageMap
        │  get_channel_by_name("depth.Z")   ou get_channel(ImageMap::CHANNEL_Z)
        ▼
   ImageMapChannel
        │  create_float_buffer(&eval_ctx, x, y, w, h, buffer)
        ▼
   tampon flottant à nous
```

C'est ainsi que procède le denoiser OptiX de Clarisse.

**Le nom du canal n'est pas le nom de l'AOV.** Un attribut `tag` filtré sur
`aov_groups` rend un nom de GROUPE — `depth`. Le canal, lui, s'appelle
`groupe.composante` — `depth.Z`. Chercher le nom tel quel échoue toujours et le
filtre retombe en silence sur un comportement neutre. Chercher le nom exact,
puis les suffixes usuels, puis n'importe quelle composante du groupe.

Constantes disponibles : `CHANNEL_LUMINANCE`, `CHANNEL_R/G/B/A/Z`.

---

## 7. Ce que les AOV contiennent vraiment

Trois découvertes qui coûtent chacune une demi-journée si on ne les a pas.

### La profondeur est une profondeur en Z, pas une distance

`depth.Z` porte la profondeur **projetée sur l'axe de visée**, pas la distance
au point de vue. Mesure : une sphère à 14 unités devant la caméra mais décalée
de 5 sur le côté rend **12,7** là où la distance vaut 13,6. Comparer une
distance euclidienne à cette profondeur fait dériver toute mise au point dès
que le sujet quitte le centre du cadre.

C'est aussi le calcul juste optiquement : une lentille mince fait le point sur
un plan parallèle au capteur, pas sur une sphère centrée sur l'œil.

### Zéro veut dire « aucune géométrie », pas « distance nulle »

Le fond d'un rendu vaut 0. Le traiter comme un objet collé à la caméra lui
donne le flou maximal, ce qui est faux dès que la mise au point est lointaine.
Il faut le placer à l'infini.

### La profondeur des pixels de silhouette vaut `couverture × z`

Le filtre de pixel moyenne les échantillons, et les échantillons de fond
comptent pour zéro. Un pixel à moitié couvert sur une sphère à 12,7 unités rend
donc **6,3** : une profondeur deux fois plus proche que tout ce que contient la
scène. Ces valeurs fantômes se classent parmi les plus floues et s'étalent au
maximum.

La couverture, c'est l'alpha, écrit par le même filtre sur les mêmes
échantillons. **Diviser par lui rend `z` exactement.** Un médian 3×3 a été
essayé d'abord : insuffisant dès que la silhouette fait deux pixels de large.

### Clarisse dé-prémultiplie les AOV par l'alpha de sortie

Conséquence non évidente : **un filtre qui modifie l'alpha corrompt tous les
AOV**. Constaté sur un composite qui perdait de la couverture — l'AOV de
profondeur ressortait à 251 au lieu de 80, soit exactement `80 / 0,32`.

Si un filtre touche à l'alpha, il doit le laisser cohérent, sans quoi le défaut
se manifeste ailleurs et se cherche longtemps.

---

## 8. La caméra et le générateur de rayons

`RayGeneratorCameraPerspective::set_lens_sample_callback` est public. Le
callback rend un couple `(u0, u1)`, et l'aval en tire `r = R·√u0`,
`t = 2π·u1`. La transformation est inversible : émettre `(ρ², θ/2π)` impose
n'importe quelle forme de diaphragme au tirage du rayon.

C'est ce qui permet une vraie profondeur de champ au rendu, sans les artefacts
inhérents à un flou 2D appliqué après coup.

**L'aberration chromatique n'est pas faisable là.** `RayGeneratorData` ne porte
aucune longueur d'onde, et le moteur est RVB, pas spectral.

---

## 9. Ce que Clarisse sait déjà faire, et qu'on a failli réécrire

Deux vérifications qui ont évité du travail inutile. Le réflexe qu'elles
imposent : **avant de concevoir une node, chercher si le moteur porte déjà le
mécanisme**, y compris sous un nom qu'on n'attendait pas.

### Le bake en espace UV existe nativement

`ModuleLayer3d` porte `is_uv_bake_enabled()`, `get_uv_bake_config()`,
`get_uv_bake_slot()`, `get_uv_bake_range()`. Autrement dit **un Layer 3D sait
rendre dans l'espace UV au lieu de l'espace caméra** — donc tout le pipeline de
rendu, matériaux et éclairage compris, se bake dans la dépliure.

Et ce n'est pas un bake au rabais :

| Réglage | Valeurs |
|---|---|
| `UvBakeEyeDirection` | `NORMAL` (indépendant du point de vue) ou `CAMERA` |
| `UvBakeProjectionMode` | `NONE`, `INSIDE`, `OUTSIDE`, `INSIDE_AND_OUTSIDE` |
| `UvBakeProjectionNormal` | `FLAT` ou `SMOOTH` |

`INSIDE`/`OUTSIDE` est une **cage** : c'est ce qui permet de transférer un
high-poly sur un low-poly. Et `UvBakeConfig::UvGeometry` porte à la fois la
tessellation et le displacement, donc la géométrie déplacée se bake aussi.

Le groupe d'attributs `Layer3d::Uv Baking` apparaît dans l'interface — la
fonction est donc accessible à l'artiste, pas seulement au SDK.

La bibliothèque `ix_uv_bake.lib` expose en plus les classes concrètes
`GeometryUvBake`, `GeometryBundleUvBake`, `GasUvBake`, et `texture_tools`
fournit un `TextureEvaluator` bâti sur un `GasUvTree`. Il y a donc les deux
niveaux : le bake tout fait par le layer, et la machinerie brute si on veut
autre chose.

### Un rayon rend les UV du point touché

Question qui décidait de la faisabilité d'un outil de peinture, et la réponse
est oui. La chaîne :

```
   GeometryIntersection            geometry_intersection.h:20
     │   class GeometryIntersection : public GeometryFragment
     ▼
   GeometryFragment                geometry_fragment.h:143
     │   primitive_id, sub_primitive_id, uvw, sub_uvw
     ▼
   GeometryObject::compute_fragment_uvw(CtxEval, GeometryFragment, index, ...)
     ▼
   UVW
```

Une intersection **est** un fragment — elle en hérite. Et
`compute_fragment_uvw` est une méthode **virtuelle** de `GeometryObject`,
exportée par `ix_geometry`, qui prend un **index de jeu d'UV** : les objets à
plusieurs dépliures sont donc gérés.

Deux exports voisins complètent le tableau : `GeometryUvMap` et
`GeometryUvTile` (construit depuis deux entiers — les coordonnées d'une tuile
UDIM). **Le support UDIM est dans le moteur.**

Méthode à retenir : `geometry_intersection.h` avait d'abord semblé vide à la
recherche. Ce sont les **fichiers `.def` de `lib/`** qui ont donné la réponse —
ils listent les symboles C++ décorés réellement exportés, donc un `grep -i uv`
dessus révèle des méthodes qu'un en-tête reconstruit peut avoir perdues.

### La projection de textures existe déjà, mais elle se monte à la main

La classe de base `TextureSpatial` porte un groupe **Projection** complet, avec
un sous-groupe caméra et des transformations UVW. Les presets de décales de la
communauté s'en servent : un `Scope` sert de projecteur, un `TextureMapFile`
projeté porte l'image, et l'empilement passe par un
`MaterialPhysicalMultiblend`.

C'est fonctionnel mais lourd : plusieurs nodes par décale, et l'empilement est
**plafonné à six couches** de matériau. Le mécanisme existe donc, ce qui manque
est une node qui le condense.

---

## 10. Les deux saveurs de licence

Mesuré, pas supposé : **359 classes existent dans les deux saveurs**, 49 sont
verrouillées par licence en iFX, 13 en BUiLDER. La différence de 36 est
exactement la famille d'assemblage de builds — `ImageNode*`, `Process*`,
`RenderScene`, `AovSet`, `SceneAssembly*`, `WidgetBuildView`, `NodalItem*`.

**Toutes les classes de base qui nous servent sont libres dans les deux** :
`ImageFilter`, `KernelFilter`, `Geometry`, `Renderer`, `Widget`, `Texture`,
`Deformer`. Autrement dit, tout ce qu'on veut écrire tourne en iFX.

---

## 11. L'API Python, et ce qu'elle laisse croire

| Ce qu'on croit | Ce qui est vrai |
|---|---|
| `ix.cmds.AddLayer` rend le layer | elle rend **`None` en cas de SUCCÈS** — relire l'attribut `layers` |
| `SetValues` sur un vecteur | il faut **un chemin par composante** : `resolution[0]`, `resolution[1]` |
| `layer_3d.active_camera` | l'attribut est `active_camera` **directement sur `LayerScene`** |
| `materials` | l'attribut s'appelle `override_material` |
| les listes d'AOV sont en lecture seule | `selected_aov_list` / `enabled_aov_list` sont `hidden`, mais **écrivables** : dimensionner puis remplir |
| `cnode` respecte `resolution` | **non** : il rend `resolution_preset × resolution_multiplier` quoi qu'il arrive |

Activer l'AOV de profondeur depuis un script :

```python
selected = layer.get_attribute("selected_aov_list")
enabled  = layer.get_attribute("enabled_aov_list")
selected.set_value_count(1); enabled.set_value_count(1)
selected.set_string("depth", 0); enabled.set_bool(True, 0)
```

---

## 12. Rendre en ligne de commande

```
cnode.exe <projet> \
  -module_path "<install>/module" "<dépôt>/native/build" \
  -image "build://project/<nom de l'image>" \
  -frames_list 1 \
  -output "<sortie>.exr"
```

- Le chemin d'une image est `build://project/<nom>`, pas `project://<nom>`.
- **`-frames_list` est obligatoire** dès que `render_to_disk` est faux, sinon
  l'image est ignorée avec un message qu'on ne lit pas.
- La sortie s'appelle `<sortie>.exr00001.exr` : cnode ajoute son numéro de
  frame au nom complet.
- Construire une scène de test : même commande avec `-script`. La sortie
  standard est noyée sous les avertissements OCIO, donc le script doit écrire
  son propre journal dans un fichier.

---

## 13. Relire les EXR produits

Piège d'outillage, mais il coûte du temps à chaque fois.

`magick` **refuse un EXR de plus de 4 canaux** (`maximum channels exceeded`).
Dès qu'un AOV est présent, il faut passer par le `iconvert.exe` de Clarisse :

```bash
iconvert.exe entree.exr sortie.tif
magick sortie.tif -separate -delete 3--1 -set colorspace RGB -combine -colorspace sRGB apercu.png
magick sortie.tif -separate -delete 0-3 -format "%[fx:p{X,Y}.r]" info:   # lire un AOV
```

Les valeurs rendues par `-format "%[fx:...]"` sont les **vraies valeurs HDR**,
pas des pourcentages. En revanche `txt:-` sort des pourcentages quantifiés et
dans la locale du système — à parser sur le champ `gray(...)`.

Et pour comparer deux images : ne jamais juger sur une vignette réduite avec
`-auto-level`, qui normalise chaque image séparément et fabrique des
différences qui n'existent pas. Mesurer un profil de valeurs le long d'une
ligne.

---

## 14. Méthode : ce qui nous a fait gagner du temps

- **Mesurer avant d'affirmer.** Presque toutes les entrées de ce document
  contredisent une déduction raisonnable. Le rapport d'un nombre — 11,43 pour
  deux diaphragmes, π pour une profondeur corrompue — a résolu plus de
  questions que n'importe quelle relecture de code.
- **Un rendu vaut mieux que dix vérifications.** Le temps de rendu se paie, et
  un test qui tranche vaut mieux que cinquante qui rassurent.
- **Un échec silencieux se journalise.** Un objet de mise au point sans AOV
  branché, un rayon écrêté, un AOV introuvable : à chaque fois le filtre
  faisait quelque chose de raisonnable et de faux. Un `LOG_WARNING` qui dit
  quoi faire vaut mieux qu'un comportement neutre.
- **Les documents d'échappement se corrompent en shell.** Écrire du C++
  contenant des `\n` via un heredoc bash mange les antislashs. Passer par
  l'outil d'édition de fichiers.
