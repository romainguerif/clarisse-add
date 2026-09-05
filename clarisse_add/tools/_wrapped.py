"""Enveloppe des scripts Clarisse repris tels quels.

Certains outils de la collection sont des scripts de plusieurs centaines de
lignes, eprouves en production, dont l'interface est ecrite directement contre
l'API GUI de Clarisse (``distribute.py``, ``light_manager.py``).  Les reecrire
sous forme de modules n'apporterait rien et ferait courir le risque de casser
du code qui marche pour un gain esthetique.

On les garde donc intacts dans ``clarisse_add/scripts/`` et on les execute comme
Clarisse le ferait : dans un espace de noms neuf, avec ``ix`` dans les globales.
Consequence utile : le script se rejoue a chaque clic, alors qu'un simple
``import`` ne l'aurait execute qu'une seule fois par session, Python gardant le
module en cache.

Les originaux non modifies restent dans ``third_party/originals/`` pour pouvoir
diffuser ce qui a ete change.
"""

import os

from ..core import log
from ..core.compat import get_ix

SCRIPTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "scripts")


def script_path(name):
    """Chemin d'un script embarque, sans supposer qu'il existe."""
    return os.path.join(SCRIPTS_DIR, name if name.endswith(".py") else name + ".py")


def run_script(name):
    """Execute un script embarque. Renvoie ``True`` s'il est alle au bout."""
    from .. import bootstrap

    path = script_path(name)
    if not os.path.isfile(path):
        log.error("Script embarque introuvable : %s" % path)
        return False
    return bootstrap.run_script_file(path, get_ix())
