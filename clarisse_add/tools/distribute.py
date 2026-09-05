"""Repartition des objets selectionnes (ligne, carre, rectangle, cercle).

Interface complete reprise telle quelle : voir ``clarisse_add/tools/_wrapped.py``
pour la raison.
"""

from ._wrapped import run_script


def run(payload=None):
    return run_script("distribute")
