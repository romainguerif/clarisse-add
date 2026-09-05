# ClarisseAdd

Boîte à outils unifiée pour **Isotropix Clarisse iFX 5.0 SP14**, installée en un
shelf. Elle rassemble trois choses qui existaient déjà mais dispersées, et donc
inutilisées : le *Clarisse Survival Kit*, une collection de scripts
communautaires, et une bibliothèque de scènes `.project` paramétrables.

**43 boutons, 7 catégories, 23 presets.**

```bash
python install.py
```

Clarisse doit être fermé pendant l'installation : il réécrit son `shelf.cfg` en
quittant et effacerait l'installation.

---

## Pourquoi

Les outils étaient là, sur le disque, depuis des années. Le problème n'était pas
de les écrire, il était de les retrouver :

- le Survival Kit était installé dans le `site-packages` d'un Python externe, et
  **13 de ses 19 boutons pointaient vers un `Python310/` qui n'a jamais contenu
  le kit** — un clic, rien, aucune erreur ;
- les scripts communautaires (`distribute.py`, `light_manager.py`, le Light
  Scatterer…) n'étaient dans aucun shelf : il fallait les ouvrir à la main dans
  l'éditeur de script ;
- les 23 scènes `.project` — Wall Maker, Window Box, la collection de bruits OSL,
  les scatterers — n'étaient référencées nulle part.

ClarisseAdd embarque tout, ne dépend d'aucune installation Python externe, et se
déplace avec son dossier.

## Ce qu'il y a dedans

| Catégorie | Boutons | |
|---|---|---|
| **ClarisseAdd** | 3 | Preset Browser, Reload, À propos / Logs |
| **Scatter** | 3 | Distribute, Shrink Wrap, Light Scatterer |
| **Lights** | 3 | Light Manager, Sun & Sky, LPE Setup |
| **Look-dev** | 3 | Lookdev Studio, Material per Shading Group, Gradient Random |
| **Scene** | 4 | Scene Audit, Relink Files, Cleanup, Batch Rename |
| **Presets** | 8 | les scènes-outils les plus utilisées, en accès direct |
| **Survival Kit** | 19 | le kit complet, réparé et embarqué |

### Les scènes `.project` deviennent de vrais outils

C'est la partie la moins évidente. Une scène comme *Window Box* ou *Wall Maker*
n'est pas un exemple à regarder : c'est un montage paramétrable, dont les
réglages sont déclarés **dans le fichier** sous forme d'attributs custom.

ClarisseAdd lit le format `.project` ([`core/project_file.py`](clarisse_add/core/project_file.py)),
en extrait ces déclarations, et **génère la fenêtre de réglages** :

```
Window Box              12 réglages   (profondeur de pièce, overscan, rideaux…)
OSL Noise Collection    62 réglages   (9 textures OSL)
MicroScratch            10 réglages
Wall Maker               8 réglages   (taille de brique, dimensions, joints)
```

Rien n'est codé en dur par preset. Déposer un `.project` porteur d'attributs
custom dans `assets/presets/`, relancer `python tools/build_catalog.py`, et il
devient un outil avec son panneau.

Le **Preset Browser** liste les 23 scènes par catégorie, affiche l'inventaire de
chacune (extrait du fichier), signale celles dont un asset manque, et les
fusionne dans le contexte choisi.

## Installation

```bash
python install.py --check        # diagnostic, n'écrit rien
python install.py                # installe pour la version installée
python install.py --version 5.0  # cible une version précise
python install.py --repair-kit   # retire du shelf les boutons morts
python uninstall.py              # retire les catégories ClarisseAdd
```

Sans `--version`, l'installeur cible une version dont **l'application** est
installée, pas simplement la plus récente à avoir un dossier de configuration.
Clarisse laisse ses préférences derrière lui quand on le désinstalle, et une
version d'essai ouverte une seule fois suffit à créer un dossier : se caler sur
le numéro le plus élevé revient, tôt ou tard, à écrire un shelf que rien ne lit.

L'installeur :

1. génère un stub d'entrée par outil dans `clarisse_add/entry/` ;
2. remplace les catégories `ClarisseAdd*` du `shelf.cfg` utilisateur ;
3. sauvegarde l'ancien fichier, horodaté, à côté.

**Il ne touche à rien d'autre.** Le `shelf.cfg` est relu avec le même parser que
les `.project` — c'est le même format — ce qui donne les plages de lignes exactes
de chaque catégorie ; l'édition se fait par tranches de lignes. Un test vérifie
qu'en dehors des catégories de l'addon, une installation ne modifie qu'une seule
ligne du fichier : `category_selected`.

*(Le Survival Kit, lui, réécrit `shelf.cfg` à coups d'expressions régulières
calées sur des niveaux d'indentation. C'est ce qui a produit les 13 boutons
morts.)*

## Développement

```bash
python -m pytest tests -q        # 241 tests, sans Clarisse
python tools/check_api.py        # vérifie les noms d'API contre le SDK
python tools/build_catalog.py    # réindexe assets/presets/
python tools/build_icons.py      # génère les icônes manquantes (Pillow)
```

Clarisse ne se pilote pas hors interface sans licence CNode : impossible donc
d'exécuter le code de l'addon en dehors de l'application. `check_api.py` comble
une partie du trou en confrontant chaque `ix.cmds.X` et `ix.api.Y` du code à la
documentation Doxygen hors ligne — 183 commandes et 1054 classes. Ça ne vérifie
pas les arguments, mais un nom inexistant est attrapé avant d'arriver dans le
shelf.

Le bouton **Reload ClarisseAdd** recharge tout le code sans redémarrer Clarisse :
purge de `sys.modules`, régénération des stubs, réenregistrement des boutons à
chaud via `ix.application.get_shelf().add_item()`.

### Structure

```
clarisse_add/
  bootstrap.py        point d'entrée unique, appelé par les stubs
  manifest.py         déclaration des 43 boutons — source de vérité
  core/
    project_file.py   lecteur du format .project (et de shelf.cfg)
    shelf.py          écriture du shelf, par tranches de lignes
    scene.py          contextes, sélection, merge, batch d'undo
    ui.py             formulaires déclaratifs
    files.py          références de fichiers de la scène
    paths.py  log.py  compat.py
  tools/              un module par bouton, chacun expose run()
  presets/            catalogue des .project (JSON généré)
  scripts/            scripts repris tels quels, exécutés par _wrapped.py
  vendor/             Clarisse Survival Kit embarqué
  entry/              stubs générés (git-ignorés)
assets/
  presets/            23 scènes .project + leurs assets
  icons/              43 icônes
third_party/
  originals/          les scripts d'origine, non modifiés, pour diff
docs/
  clarisse-command-api.txt   206 commandes ix.cmds, extraites du SDK
```

### Écrire un outil

Ajouter une ligne dans `manifest.py`, créer le module, relancer `install.py` :

```python
# clarisse_add/tools/mon_outil.py
from ..core import scene, ui
from ..core.compat import get_ix

def run(payload=None):
    ix = get_ix()
    values = ui.Form("Mon outil", [
        ui.Number("count", "Nombre", default=10, minimum=1, maximum=100, integer=True),
    ]).run()
    if values is None:
        return False
    with scene.command_batch("Mon outil"):
        ...
    return True
```

`run()` ne doit jamais lever : `bootstrap.launch` attrape tout, journalise la
pile complète et le signale à l'artiste. Un outil qui échoue en silence est pire
qu'un outil absent.

## Documentation

- [`docs/architecture.md`](docs/architecture.md) — comment l'addon s'accroche à Clarisse
- [`docs/project-format.md`](docs/project-format.md) — le format `.project`, tel que lu ici
- [`docs/clarisse-command-api.txt`](docs/clarisse-command-api.txt) — 206 commandes `ix.cmds`
- [`third_party/README.md`](third_party/README.md) — provenance et modifications

Le SDK complet de Clarisse 5.0 SP14 (4786 fichiers de doc Doxygen hors ligne) est
archivé séparément : Isotropix a fermé, la documentation en ligne a disparu.

## Licence

**GPL-3.0-or-later**, imposée par le *Clarisse Survival Kit* d'Aydin Yanik qui est
embarqué dans `clarisse_add/vendor/`. Les scripts repris, leurs auteurs et les
modifications apportées sont listés dans
[`third_party/README.md`](third_party/README.md).
