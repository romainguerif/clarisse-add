"""Systeme soleil + ciel physique base sur une texture OSL Nishita.

Le soleil est une lumiere distante dont la couleur et la taille suivent sa
hauteur dans le ciel, via des expressions liees a la texture de ciel.
"""

from ._wrapped import run_script


def run(payload=None):
    return run_script("sun_sky")
