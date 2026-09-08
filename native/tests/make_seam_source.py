# -*- coding: utf-8 -*-
"""Ecrit l'image source du banc d'essai des coutures, en Radiance RGBE.

    python make_seam_source.py

Deux contenus dans une seule image, parce qu'une couture peut se manifester de
deux facons et qu'un seul rendu doit montrer les deux :

- un APLAT gris uniforme. Un noyau normalise rend un aplat identique a
  lui-meme, quelle que soit sa forme. Tout ce qui apparait dessus apres le
  filtre est donc un artefact, sans discussion possible. C'est le test le plus
  severe qui soit : il n'a aucun detail derriere lequel se cacher.
- une grille de POINTS tres lumineux, decalee de la grille de tuiles de
  Clarisse pour qu'on ne puisse pas confondre les deux. Chaque point devient
  une boule de bokeh : c'est la que se lit une couture de FORME, celle qui
  hache la boule au lieu d'etager la luminosite.

On l'ecrit a la main plutot que par magick : le RGBE tient en trente lignes,
et cela evite de dependre d'un outil externe pour la seule chose qui doit etre
exactement reproductible dans ce banc d'essai.
"""
from __future__ import print_function

import math
import os
import struct

OUT = r"J:\_WINDOWSTEMP\claude\seam_src.hdr"
# 1920 x 1080 parce que cnode rend a la resolution des preferences du projet
# quoi qu'on ecrive dans l'attribut `resolution` de l'image. Autant que la
# source soit a l'echelle du rendu : sinon le LayerFile l'etire, les points
# deviennent des taches et le banc d'essai ne prouve plus rien.
WIDTH = 1920
HEIGHT = 1080
FLAT = 0.5          # l'aplat
DOT = 400.0         # les points, largement au-dessus de 1 : de vraies hautes lumieres
# 301 est premier avec 64 : aucun point ne tombe sur une frontiere de tuile, ce
# qui interdit de confondre la grille des tuiles avec celle des points. Et
# l'ecart laisse deux boules de rayon 120 se toucher sans se recouvrir.
SPACING = 301
OFFSET = 151
DOTS_BELOW = 600    # les points s'arretent la ; le bas reste un aplat pur


def encode(r, g, b):
    """Un pixel RGBE. Mantisse commune, exposant commun -- c'est tout Radiance."""
    peak = max(r, g, b)
    if peak < 1e-32:
        return b"\x00\x00\x00\x00"
    mantissa, exponent = math.frexp(peak)
    scale = mantissa * 256.0 / peak
    return struct.pack("BBBB",
                       min(255, int(r * scale)), min(255, int(g * scale)),
                       min(255, int(b * scale)), exponent + 128)


def main():
    folder = os.path.dirname(OUT)
    if not os.path.isdir(folder):
        os.makedirs(folder)

    # Les points restent en haut. Le bas doit etre un aplat PUR : avec un rayon
    # de 40 une boule deborde de 40 pixels, et il faut une bande ou rien du
    # tout ne se passe pour pouvoir affirmer que ce qu'on y mesure est un
    # artefact et rien d'autre.
    dots = set()
    for y in range(OFFSET, DOTS_BELOW, SPACING):
        for x in range(OFFSET, WIDTH, SPACING):
            dots.add((x, y))

    flat = encode(FLAT, FLAT, FLAT)
    bright = encode(DOT, DOT, DOT)

    rows = []
    for y in range(HEIGHT):
        row = [flat] * WIDTH
        if y < DOTS_BELOW:
            for x in range(OFFSET, WIDTH, SPACING):
                if (x, y) in dots:
                    row[x] = bright
        rows.append(b"".join(row))

    handle = open(OUT, "wb")
    handle.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
    handle.write(("-Y %d +X %d\n" % (HEIGHT, WIDTH)).encode("ascii"))
    handle.write(b"".join(rows))
    handle.close()
    print("%s ecrit : %d x %d, %d points" % (OUT, WIDTH, HEIGHT, len(dots)))


if __name__ == "__main__":
    main()
