"""Remplissage aleatoire d'un TextureGradient entre deux couleurs."""

from ._wrapped import run_script


def run(payload=None):
    return run_script("gradient_random")
