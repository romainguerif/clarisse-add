# -*- coding: utf-8 -*-
"""Croise le rendu et la sonde sur la variante anamorphique.

    python cross_check_seams.py [sortie_de_la_sonde]

La sonde somme les taps du noyau un par un ; la convolution reelle, elle, passe
par des SEGMENTS intersectes avec le disque de troncature, qui devient une
ellipse sous anamorphisme. Ce sont deux implementations qui n'ont rien en
commun, et le seul chemin de code qui ne soit exerce par rien d'autre.

Si elles tombent d'accord sur l'aplat, l'intersection segment-ellipse est
juste. C'est le genre de verification qui remplace une relecture : deux
erreurs independantes tomberaient rarement sur le meme nombre.

Prealables : `python measure_seams.py apres` (qui rend t6_anamorphique), et une
execution de la sonde dont la sortie est passee en argument -- sa section 8
donne les valeurs attendues.
"""
from __future__ import print_function

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure_seams as M

ROOT = r"J:\_WINDOWSTEMP\claude\seams"
PROBE = r"J:\_WINDOWSTEMP\claude\probe\final.txt"
Y = 870


def predictions(path):
    """Les deux lignes de la section 8 de la sonde : les abscisses, les valeurs."""
    text = open(path).read()
    start = text.find("== 8.")
    if start < 0:
        sys.exit("section 8 absente de %s -- relancer la sonde" % path)
    block = text[start:start + 800]
    xs = re.search(r"x\s+:(.*)", block)
    vs = re.search(r"valeur\s+:(.*)", block)
    if not xs or not vs:
        sys.exit("section 8 illisible dans %s" % path)
    return (list(zip([int(v) for v in xs.group(1).split()],
                     [float(v) for v in vs.group(1).split()])))


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else PROBE
    if not os.path.isfile(probe):
        sys.exit("sortie de sonde introuvable : %s" % probe)
    pairs = predictions(probe)

    render = os.path.join(ROOT, "apres_t6_anamorphique.exr00001.exr")
    if not os.path.isfile(render):
        sys.exit("rendu introuvable : %s\nlancer d'abord : python measure_seams.py apres"
                 % render)

    width, height, channels, rows = M.read_float_image(render)
    print("rendu %dx%d, %d canal(aux), ligne y=%d" % (width, height, channels, Y))
    print("      x     rendu    sonde    ecart")
    worst = 0.0
    for x, expected in pairs:
        got = rows[Y][x * channels]
        gap = (got / expected - 1.0) * 100.0 if expected else 0.0
        if abs(gap) > abs(worst):
            worst = gap
        print("  %5d  %8.5f %8.5f  %+7.3f %%" % (x, got, expected, gap))
    print("\n  ecart maximal %+.3f %% -- %s"
          % (worst, "les deux implementations sont d'accord" if abs(worst) < 0.5
             else "DESACCORD, l'une des deux est fausse"))


if __name__ == "__main__":
    main()
