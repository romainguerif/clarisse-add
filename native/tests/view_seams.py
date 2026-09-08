# -*- coding: utf-8 -*-
"""Fabrique les planches de comparaison avant / apres du banc des coutures.

    python view_seams.py

Une couture de moins d'un pour cent ne se voit pas sur une image affichee
telle quelle, et l'etirer autour d'une valeur fixe ne marche pas non plus ici :
apres correction le vignettage assombrit VRAIMENT l'image, donc les deux cotes
n'ont plus le meme niveau et la meme fenetre d'affichage mentirait.

On montre donc ce qu'on a mesure, et rien d'autre : le RESIDU. Pour chaque
colonne de la bande d'aplat on prend sa moyenne, on lui retire une version
lissee d'elle-meme, et on etire ce qui reste au meme facteur des deux cotes.
Un aplat n'a aucun residu ; ce qui apparait est la couture, et son abscisse se
lit directement.

La planche des boules, elle, n'a besoin d'aucun artifice : chaque boule est
ramenee a son propre maximum, ce qui neutralise la difference de niveau, et la
forme se compare a l'oeil nu.
"""
from __future__ import print_function

import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure_seams as M

MAGICK = r"C:\Program Files\ImageMagick-7.1.1-Q16-HDRI\magick.exe"
ROOT = r"J:\_WINDOWSTEMP\claude\seams"

BAND_Y = (700, 1040)
BAND_X = (192, 1728)
TILE = 64
SMOOTH = 33          # largeur du lissage qui sert de reference
STRETCH = 0.004      # +- 0,4 % de la moyenne locale sur toute la hauteur
STRIP_H = 90

BALL = (272, 272, 360, 360)


def residual(path, channel):
    width, height, channels, rows = M.read_float_image(path)
    if channels < 3:
        channel = 0
    means = []
    for x in range(width):
        total = 0.0
        for y in range(BAND_Y[0], min(BAND_Y[1], height)):
            total += rows[y][x * channels + channel]
        means.append(total / (min(BAND_Y[1], height) - BAND_Y[0]))

    out = []
    half = SMOOTH // 2
    for x in range(width):
        lo, hi = max(0, x - half), min(width, x + half + 1)
        local = sum(means[lo:hi]) / (hi - lo)
        out.append((means[x] - local) / local if local > 0 else 0.0)
    return out


def write_pgm(path, width, height, pixels):
    """Un PGM 8 bits : dix lignes, aucune dependance, et magick le relit."""
    handle = open(path, "wb")
    handle.write(("P5\n%d %d\n255\n" % (width, height)).encode("ascii"))
    handle.write(bytes(bytearray(pixels)))
    handle.close()


def strip(values, out):
    """Le residu en bande grise, avec un trait sur chaque frontiere de tuile."""
    width = BAND_X[1] - BAND_X[0]
    row = []
    for x in range(BAND_X[0], BAND_X[1]):
        v = 0.5 + values[x] / (2.0 * STRETCH)
        row.append(max(0, min(255, int(v * 255.0 + 0.5))))
    pixels = []
    for y in range(STRIP_H):
        if y >= STRIP_H - 8:
            # La regle : un trait tous les 64 pixels. Si les pointes du residu
            # tombent dessus, la couture vient du decoupage en tuiles.
            pixels += [255 if ((x + BAND_X[0]) % TILE == 0) else 40
                       for x in range(width)]
        else:
            pixels += row
    write_pgm(out, width, STRIP_H, pixels)
    return out


def ball(path, out):
    """Une boule, avec un niveau ajuste sur elle seule.

    Le fond de la source est un aplat a 0,5 et la boule ne le depasse que de
    quelques pour cent : etalee sur un disque de rayon 120, l'energie d'un
    point vaut 400 / (pi 120 carre), soit moins d'un centieme. Un affichage
    brut ne montre donc que du blanc. On prend le vingtieme centile de la
    vignette comme noir et son maximum comme blanc : chaque cote est ajuste
    sur lui-meme, ce qui est la seule comparaison honnete puisque le
    vignettage corrige assombrit reellement l'un des deux.
    """
    width, height, channels, rows = M.read_float_image(path)
    x0, y0, w, h = BALL
    values = []
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            values.append(rows[y][x * channels])
    ordered = sorted(values)
    black = ordered[len(ordered) // 5]
    white = ordered[-1]
    if white <= black:
        white = black + 1e-6
    pixels = []
    for v in values:
        t = (v - black) / (white - black)
        if t < 0.0:
            t = 0.0
        if t > 1.0:
            t = 1.0
        pixels.append(int(t ** (1.0 / 2.2) * 255.0 + 0.5))
    write_pgm(out, w, h, pixels)
    return out


def label(source, out, text):
    subprocess.check_call([MAGICK, source, "-colorspace", "sRGB",
                           "-background", "black", "-fill", "white",
                           "-pointsize", "20", "label:%s" % text,
                           "-gravity", "west", "-append", out],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main():
    # -- planche 1 : le residu de l'aplat --------------------------------------
    pieces = []
    for tag, title in (("avant", "AVANT"), ("apres", "APRES")):
        path = os.path.join(ROOT, "%s_t5_optique_marquee.exr00001.exr" % tag)
        raw = strip(residual(path, 0), os.path.join(ROOT, "%s_res.pgm" % tag))
        piece = os.path.join(ROOT, "%s_res.png" % tag)
        # Pas de signe pour cent dans le libelle : magick le lit comme une
        # sequence de format et le remplace par autre chose.
        label(raw, piece, "%s -- residu du canal rouge sur l'aplat, plage "
                          "+-%.1f pour cent, trait blanc = frontiere de tuile"
                          % (title, STRETCH * 100))
        pieces.append(piece)
    target = os.path.join(ROOT, "coutures_aplat.png")
    subprocess.check_call([MAGICK] + pieces + ["-background", "gray30",
                                               "-append", target],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(target)

    # -- planche 2 : une boule -------------------------------------------------
    pieces = []
    for tag, title in (("avant", "AVANT"), ("apres", "APRES")):
        path = os.path.join(ROOT, "%s_t1_vignettage.exr00001.exr" % tag)
        raw = ball(path, os.path.join(ROOT, "%s_ball.pgm" % tag))
        piece = os.path.join(ROOT, "%s_ball.png" % tag)
        label(raw, piece, "%s -- vignettage 1, boule ramenee a son maximum" % title)
        pieces.append(piece)
    target = os.path.join(ROOT, "coutures_boules.png")
    subprocess.check_call([MAGICK] + pieces + ["-background", "gray30",
                                               "+append", target],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(target)


if __name__ == "__main__":
    main()
