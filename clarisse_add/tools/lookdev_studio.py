"""Environnement de lookdev commutable.

Version portee en Python 3 du script livre avec Clarisse, avec deux correctifs
(voir l'en-tete de ``clarisse_add/scripts/lookdev_studio.py``).  Le dossier de
contenus se regle dans la fenetre, ou via la variable d'environnement
``CLARISSE_ADD_LOOKDEV_CONTENT``.
"""

from ._wrapped import run_script


def run(payload=None):
    return run_script("lookdev_studio")
