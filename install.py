"""Installe ClarisseAdd dans le shelf de Clarisse.

    python install.py                 # installe pour la version la plus recente
    python install.py --version 5.0   # cible une version precise
    python install.py --check         # diagnostic, n'ecrit rien
    python install.py --repair-kit    # retire du shelf les entrees mortes du kit

S'execute avec n'importe quel Python 3, hors de Clarisse : il n'ecrit qu'un
fichier de configuration.  Clarisse doit etre ferme, sinon il reecrira son
``shelf.cfg`` en quittant et effacera l'installation.

Ce que fait l'installeur :

1. genere un stub d'entree par outil dans ``clarisse_add/entry/`` ;
2. remplace les categories ``ClarisseAdd*`` du ``shelf.cfg`` utilisateur, en
   laissant intact tout le reste du fichier ;
3. sauvegarde l'ancien ``shelf.cfg`` a cote, horodate.
"""

from __future__ import print_function

import argparse
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from clarisse_add import __version__, manifest  # noqa: E402
from clarisse_add.core import paths, shelf  # noqa: E402
from clarisse_add.presets import catalog  # noqa: E402


def check():
    """Diagnostic : ce que l'installeur voit de la machine."""
    print("ClarisseAdd %s" % __version__)
    print("  addon      : %s" % paths.ADDON_ROOT)
    print("  presets    : %s" % paths.PRESETS_DIR)
    print("  icones     : %s" % paths.ICONS_DIR)

    versions = paths.installed_config_versions()
    if not versions:
        print("  ! aucun dossier de configuration Clarisse trouve dans %s"
              % paths.user_config_root())
        return 1

    print("  versions   : %s" % ", ".join(versions))
    for version in versions:
        config = paths.shelf_config(version)
        state = "present" if os.path.isfile(config) else "absent (sera cree)"
        print("    %-5s %s  [%s]" % (version, config, state))
        dead = _dead_shelf_entries(config)
        if dead:
            print("           %d entree(s) de shelf pointant vers un script "
                  "inexistant" % len(dead))

    tools = manifest.all_tools()
    presets = catalog.entries()
    print("  outils     : %d dans %d categories"
          % (len(tools), len(manifest.categories())))
    print("  presets    : %d (%d avec bouton dedie, %d parametrables)"
          % (len(presets),
             sum(1 for entry in presets if entry.shelf),
             sum(1 for entry in presets if entry.parameters)))

    absent = [entry for entry in presets if not entry.exists()]
    if absent:
        print("  ! %d preset(s) dont le .project est introuvable : %s"
              % (len(absent), ", ".join(entry.id for entry in absent)))

    missing_icons = [tool for tool in tools if not paths.icon(tool.icon)]
    if missing_icons:
        print("  i %d outil(s) sans icone (le titre sera affiche a la place)"
              % len(missing_icons))
    return 0


def _dead_shelf_entries(config_path):
    """Entrees de shelf dont le ``script_filename`` n'existe pas.

    C'est le symptome typique d'une installation du Survival Kit faite avec un
    Python puis desinstallee : le shelf garde des boutons qui ne font rien.
    """
    if not os.path.isfile(config_path):
        return []
    from clarisse_add.core import project_file

    with io.open(config_path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    try:
        parsed = project_file.parse_string(text)
    except project_file.ParseError:
        return []

    dead = []
    for node in parsed.iter_objects(skip_embedded=False):
        if node.class_name != "shelf_item":
            continue
        script = node.get("script_filename", "")
        if script and not os.path.isfile(script):
            dead.append((node.get("title", "?"), script))
    return dead


def repair_kit(version):
    """Retire du shelf les boutons dont le script n'existe plus.

    Sur cette machine, treize des dix-neuf boutons du Survival Kit pointaient
    vers un ``site-packages`` de Python 3.10 qui n'a jamais contenu le kit.
    L'addon embarque desormais sa propre copie ; ces entrees mortes n'ont plus
    de raison d'etre.
    """
    config = paths.shelf_config(version)
    dead = _dead_shelf_entries(config)
    if not dead:
        print("Aucune entree morte dans %s" % config)
        return 0

    print("%d entree(s) morte(s) dans %s :" % (len(dead), config))
    for title, script in dead:
        print("  %-34s -> %s" % (title, script))

    answer = raw_input_compat(
        "Les supprimer ? Une sauvegarde est faite. [o/N] "
    ).strip().lower()
    if answer not in ("o", "oui", "y", "yes"):
        print("Annule.")
        return 0

    from clarisse_add.core import project_file

    with io.open(config, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    parsed = project_file.parse_string(text)

    dropped = set()
    for node in parsed.iter_objects(skip_embedded=False):
        if node.class_name != "shelf_item":
            continue
        script = node.get("script_filename", "")
        if script and not os.path.isfile(script):
            dropped.update(range(node.line, node.end_line + 1))

    shelf.backup(config)
    lines = [line for number, line in enumerate(text.splitlines(), start=1)
             if number not in dropped]
    with io.open(config, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("%d entree(s) supprimee(s)." % len(dead))
    return 0


def raw_input_compat(prompt):
    try:
        return input(prompt)
    except EOFError:
        return ""


def install(version, slot):
    tools = manifest.all_tools()

    written = shelf.write_entry_scripts(tools)
    pruned = shelf.prune_entry_scripts(tools)
    print("Stubs : %d ecrits, %d obsoletes supprimes (%s)"
          % (written, pruned, paths.ENTRY_DIR))

    config = paths.shelf_config(version)
    report = shelf.install(
        tools, config, manifest.PREFIX, slot=slot,
        select_category=manifest.CATEGORY_MAIN,
    )

    print("Shelf : %s" % config)
    if report["created"]:
        print("  fichier cree")
    if report["backup"]:
        print("  sauvegarde : %s" % os.path.basename(report["backup"]))
    if report["replaced"]:
        print("  %d categorie(s) ClarisseAdd remplacee(s)" % report["replaced"])
    print("  %d bouton(s) dans %d categorie(s), slot %d"
          % (report["items"], report["categories"], slot))
    print("")
    print("Termine. Lancez Clarisse : les onglets '%s...' sont dans le shelf."
          % manifest.PREFIX)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Installe ClarisseAdd dans le shelf de Clarisse.",
    )
    parser.add_argument("--version", dest="version",
                        help="version de Clarisse a cibler (ex. 5.0). "
                             "Par defaut, la plus recente trouvee.")
    parser.add_argument("--slot", type=int, default=shelf.DEFAULT_SLOT,
                        help="slot du shelf (0-7, defaut %d)" % shelf.DEFAULT_SLOT)
    parser.add_argument("--check", action="store_true",
                        help="diagnostic seul, n'ecrit rien")
    parser.add_argument("--repair-kit", action="store_true",
                        help="retire du shelf les boutons dont le script "
                             "n'existe plus")
    args = parser.parse_args(argv)

    if args.check:
        return check()

    versions = paths.installed_config_versions()
    if args.version:
        version = args.version
        if version not in versions:
            print("Attention : aucun dossier de configuration pour Clarisse %s. "
                  "Il sera cree." % version)
    elif versions:
        version = versions[0]
    else:
        print("Aucune installation de Clarisse detectee. Precisez --version.")
        return 1

    if args.repair_kit:
        return repair_kit(version)

    if not 0 <= args.slot <= 7:
        print("Le slot doit etre compris entre 0 et 7.")
        return 1

    return install(version, args.slot)


if __name__ == "__main__":
    sys.exit(main())
