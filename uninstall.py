"""Retire ClarisseAdd du shelf de Clarisse.

    python uninstall.py                 # toutes les versions detectees
    python uninstall.py --version 5.0

Seules les categories ``ClarisseAdd*`` sont retirees du ``shelf.cfg`` ; le reste
du fichier n'est pas touche, et une sauvegarde horodatee est faite avant.  Les
fichiers de l'addon restent sur le disque : il suffit de relancer ``install.py``
pour revenir en arriere.
"""

from __future__ import print_function

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from clarisse_add import manifest  # noqa: E402
from clarisse_add.core import paths, shelf, startup  # noqa: E402


def main(argv=None):
    parser = argparse.ArgumentParser(description="Retire ClarisseAdd du shelf.")
    parser.add_argument("--version", dest="version",
                        help="version de Clarisse a nettoyer. Par defaut, toutes.")
    parser.add_argument("--purge-entries", action="store_true",
                        help="supprime aussi les stubs generes dans "
                             "clarisse_add/entry/")
    args = parser.parse_args(argv)

    versions = [args.version] if args.version else paths.installed_config_versions()
    if not versions:
        print("Aucune configuration Clarisse trouvee.")
        return 1

    total = 0
    for version in versions:
        config = paths.shelf_config(version)
        removed = shelf.uninstall(config, manifest.PREFIX)
        total += removed
        state = "%d categorie(s) retiree(s)" % removed if removed else "rien a retirer"
        print("%-5s %s  [%s]" % (version, config, state))

        # Le crochet de demarrage vit ailleurs que le shelf : il faut le
        # retirer explicitement, sinon Clarisse continuerait d'executer un
        # script d'un addon desinstalle.
        env_file = paths.clarisse_env_file(version)
        config_dir = os.path.dirname(env_file)
        changed, saved = startup.disable(env_file,
                                         startup.hook_script(config_dir))
        startup.remove_hook(config_dir)
        if changed:
            print("      script de demarrage retire de clarisse.env")
            if saved:
                print("      sauvegarde : %s" % os.path.basename(saved))

    if args.purge_entries and os.path.isdir(paths.ENTRY_DIR):
        removed = shelf.prune_entry_scripts([])
        print("Stubs supprimes : %d" % removed)

    if total:
        print("\nRelancez install.py pour reinstaller.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
