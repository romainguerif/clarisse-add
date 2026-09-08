# -*- coding: utf-8 -*-
"""Le rapport mesure sur la grille est-il une couture, ou du bruit ?

    python phase_check_seams.py [etiquette]

`measure_seams.py` compare le saut moyen SUR les multiples de 64 au saut moyen
ailleurs. C'est la bonne mesure, mais elle ne porte que sur 23 colonnes, et sur
une image pleine de bords francs il suffit qu'un bord tombe pres d'un multiple
de 64 pour que le rapport monte sans qu'aucune tuile soit en cause.

Le controle est immediat : une couture de tuile ne peut tomber QUE sur la phase
0 modulo 64. On mesure donc les huit phases. Si la phase 0 ne ressort pas du
lot, il n'y a pas de couture.

A croiser toujours avec la bande d'aplat : sur un aplat, un noyau normalise
rend exactement la source, donc le moindre saut non nul y est un artefact et il
n'y a rien a interpreter.
"""
from __future__ import print_function

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure_seams as M

ROOT = r"J:\_WINDOWSTEMP\claude\seams"
STEP = 8


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else "apres"
    for name in M.VARIANTS:
        path = os.path.join(ROOT, "%s_%s.exr00001.exr" % (tag, name))
        if not os.path.isfile(path):
            continue
        width, height, channels, rows = M.read_float_image(path)
        for band_name, band in (("aplat", M.BAND_FLAT), ("boules", M.BAND_BALLS)):
            means = M.column_means(width, height, channels, rows, 0, band)
            reference = (sum(means[M.BAND_X[0]:M.BAND_X[1]])
                         / (M.BAND_X[1] - M.BAND_X[0]))
            if reference <= 0.0:
                continue
            print("\n%s -- %s" % (name, band_name))
            for phase in range(0, M.TILE, STEP):
                total, count = 0.0, 0
                for x in range(M.BAND_X[0] + 1, M.BAND_X[1]):
                    if x % M.TILE == phase:
                        total += abs(means[x] - means[x - 1]) / reference * 100.0
                        count += 1
                print("   phase %2d : %7.4f %%  sur %d colonnes%s"
                      % (phase, total / count if count else 0.0, count,
                         "   <- la seule ou une tuile peut tomber"
                         if phase == 0 else ""))


if __name__ == "__main__":
    main()
