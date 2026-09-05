"""Instanciation de lumieres sur un nuage de points Alembic.

Selectionnez le nuage de points puis lancez l'outil : ses proprietes (couleur,
intensite, temperature...) sont proposees en presets pour piloter les lumieres.
"""

from ._wrapped import run_script


def run(payload=None):
    return run_script("light_scatterer")
