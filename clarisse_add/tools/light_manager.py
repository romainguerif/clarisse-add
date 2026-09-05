"""Panneau central de gestion des lumieres de la scene.

Deux mille lignes d'interface eprouvee, executees telles quelles : voir
``clarisse_add/tools/_wrapped.py``.
"""

from ._wrapped import run_script


def run(payload=None):
    return run_script("light_manager")
