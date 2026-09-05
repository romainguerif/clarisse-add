# Provenance et modifications

ClarisseAdd reprend du code écrit par d'autres. Ce fichier dit d'où vient chaque
morceau, sous quelle licence, et ce qui a été changé.

`third_party/originals/` contient les scripts **d'origine, non modifiés**. Les
copies exécutées par l'addon sont dans `clarisse_add/scripts/`. Un `diff` entre
les deux montre exactement ce qui a été touché.

---

## Clarisse Survival Kit

- **Auteur** : Aydin Yanik
- **Source** : https://github.com/aydinyanik/clarisse_survival_kit
- **Version** : 2.0.0
- **Licence** : GPL-3.0 — voir `LICENSE.clarisse_survival_kit`
- **Emplacement** : `clarisse_add/vendor/clarisse_survival_kit/`

C'est cette licence qui impose la GPL-3.0 à l'ensemble du dépôt.

### Ce qui change par rapport à l'amont

Le code du kit est repris **tel quel**, à une exception près :

- `terrain.py:221` — la regex de tuilage était écrite en chaîne normale
  (`"...(?P<tile_x>\d+)..."`), ce qui produit un `SyntaxWarning` sur les Pythons
  récents et deviendra une erreur. Passée en chaîne brute.

Ce qui change, c'est **la façon dont il est installé** :

| | Amont | ClarisseAdd |
|---|---|---|
| Emplacement | `site-packages` d'un Python externe | dans l'addon |
| Installation | `python setup.py install` | aucune |
| Chemins du shelf | absolus, vers `site-packages` | relatifs à l'addon |
| Écriture du `shelf.cfg` | expressions régulières sur l'indentation | parser du format |

Sur la machine de développement, l'installation amont avait laissé **13 boutons
sur 19 pointant vers `…/Python310/lib/site-packages/clarisse_survival_kit/`**, un
dossier qui n'a jamais contenu le kit. Un clic ne produisait rien, sans message.
`python install.py --repair-kit` retire ces entrées mortes.

Les scripts du kit sont exécutés, pas importés (`clarisse_add/tools/kit_runner.py`) :
ils appellent leur fonction d'interface à la dernière ligne du fichier, donc un
`import` ne les lancerait qu'une fois par session.

---

## Lookdev Studio

- **Auteur** : Isotropix SAS — livré avec Clarisse
  (`Clarisse/python3/…/lookdev-studio_environment.py`)
- **Licence** : voir l'en-tête du fichier — usage réservé aux licenciés Isotropix
- **Original** : `originals/lookdev-studio_environment.py`
- **Copie exécutée** : `clarisse_add/scripts/lookdev_studio.py`

### Modifications

1. **`print full_envpath` → supprimé.** Syntaxe Python 2 : le script ne se
   compilait pas sous le Python 3 de Clarisse 5. La ligne était du débogage et
   inondait la console d'une ligne par fichier du dossier de contenus.
2. **`item_exists("project://default" + options_name)`** — il manquait un `/`.
   Le test échouait donc toujours, et le script recréait son objet d'options à
   chaque lancement au lieu de retrouver l'existant.
3. **Dossier de contenus par défaut** lu dans la variable d'environnement
   `CLARISSE_ADD_LOOKDEV_CONTENT` quand aucune préférence n'est enregistrée.

Les HDRI du Lookdev Studio (~490 Mo) ne sont pas dans ce dépôt. Le dossier de
contenus se règle dans la fenêtre de l'outil.

---

## Scripts communautaires

Repris de la collection `ClarisseSTUFF/ClarisseTOOL`. Auteurs d'origine inconnus
ou non crédités dans les fichiers ; les crédits identifiés sont notés.

| Original | Devenu | Modifications |
|---|---|---|
| `distribute.py` | `scripts/distribute.py` | aucune |
| `light_manager.py` | `scripts/light_manager.py` | aucune |
| `LightScatterer_With_Properties.py` | `scripts/light_scatterer.py` | aucune |
| `Gradient_Random.py` | `scripts/gradient_random.py` | aucune |
| `createSunAndSkyOnly.py` | `scripts/sun_sky.py` | aucune |
| `113_ShrinkWrap.py` | **réécrit** en `tools/shrinkwrap.py` | voir ci-dessous |
| `MaterialPerShadingGroup.txt` | **réécrit** en `tools/material_per_shading_group.py` | voir ci-dessous |
| `AutoShrinkWrapScript.txt` | — | doublon de `113_ShrinkWrap.py`, non repris |

Les quatre premiers sont des interfaces éprouvées de plusieurs centaines de
lignes (2006 pour le Light Manager). Les réécrire n'apporterait rien et ferait
courir le risque de casser du code qui marche : ils sont exécutés tels quels par
`clarisse_add/tools/_wrapped.py`.

### Shrink Wrap — réécrit

L'original était court et linéaire, mais :

- créait toujours un contexte `ShrinkWrap` à la racine du projet, ce qui empêche
  de lancer l'outil deux fois ;
- codait en dur 300 subdivisions et un rayon d'occlusion de `hauteur × 1.5` ;
- construisait systématiquement le montage de baking (une dizaine d'objets).

La version de l'addon demande les réglages, crée son contexte là où l'artiste
travaille avec un nom unique, rend le baking optionnel, et refuse de travailler
sur une sélection à bounding box plate plutôt que de produire une grille qui ne
descend jamais.

### Material per Shading Group — réécrit

L'original ne traitait que `ix.selection[0]` et forçait
`MaterialPhysicalStandard`. La version de l'addon traite toute la sélection,
laisse choisir la classe de matériau, et rend optionnels le contexte par groupe
et l'assignation.

---

## Scènes `.project`

`assets/presets/` — 23 scènes de la collection. Crédits identifiés dans
`clarisse_add/presets/catalog.json` (champ `credit`) : Demian Hernandez (Desert,
Ocean, RGB Displacement), vandam (LPE Master File), jiWindowBox de Julien Ignace
porté en OSL (Window Box). Les autres sont des scènes communautaires sans auteur
identifiable.

### Chemins réparés

Deux presets référençaient des fichiers par chemin absolu, sur des machines qui
n'existent plus. Les assets étant présents à côté du `.project`, les chemins ont
été réécrits en `$PDIR` :

- `wall_maker` — `U:/projects/Black_Cauldron/geo/Bricks/Brick_Flat.obj`
  → `$PDIR/geo/Bricks/Brick_Flat.obj` (11 occurrences)
- `decal_perso` — la texture de décal → `$PDIR/…`

Deux presets référencent encore un HDRI absent (`micro_scratch`,
`rgb_displacement`) : c'est l'éclairage de leur scène de démonstration, pas
l'outil lui-même. Le catalogue le signale, et le Preset Browser prévient avant
de fusionner.

---

## Documentation SDK

`docs/clarisse-command-api.txt` est extrait de la documentation Doxygen hors
ligne livrée avec Clarisse 5.0 SP14 (`Clarisse/docs/sdk/namespacecmds.html`).
Documentation © Isotropix SAS, 2009-2023. Reproduite ici sous forme de liste de
signatures, comme référence de travail : Isotropix a fermé, le site officiel et
le forum ne sont plus accessibles.
