"""Lanceur des outils du Clarisse Survival Kit vendorises.

Le kit est concu pour etre installe dans le ``site-packages`` d'un Python
externe, chaque bouton du shelf pointant vers un chemin absolu du type
``.../Python37/lib/site-packages/clarisse_survival_kit/mix.py``.  C'est fragile :
sur cette machine, treize des dix-neuf boutons pointaient vers un
``Python310/...`` qui n'a jamais contenu le kit, donc treize boutons morts.

Ici, le kit est embarque dans l'addon et lance depuis celui-ci.  Plus rien a
installer, plus de dependance a la version de Python du systeme, et le kit suit
l'addon quand on le deplace.

Ses scripts appellent leur fonction d'interface a la derniere ligne du fichier :
on les execute donc plutot que de les importer, faute de quoi ils ne se
lanceraient qu'une fois par session.
"""

import os

from ..core import log, paths
from ..core.compat import get_ix

KIT_DIR = os.path.join(paths.VENDOR_DIR, "clarisse_survival_kit")


def run(payload=None):
    from .. import bootstrap

    if not payload:
        log.error("kit_runner appele sans nom de script")
        return False

    script = os.path.join(KIT_DIR, payload + ".py")
    if not os.path.isfile(script):
        log.error(
            "Script du Survival Kit introuvable : %s. Le paquet vendorise est-il "
            "complet dans %s ?" % (os.path.basename(script), KIT_DIR)
        )
        return False

    bootstrap.ensure_paths()
    return bootstrap.run_script_file(script, get_ix())
