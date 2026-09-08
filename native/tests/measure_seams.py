# -*- coding: utf-8 -*-
"""Rend le banc d'essai des coutures, puis les MESURE dans le buffer.

    python measure_seams.py avant
    python measure_seams.py apres

Juger une couture a l'oeil sur un apercu reduit est le meilleur moyen de se
tromper -- c'est deja arrive sur ce projet. Une couture est une discontinuite,
donc elle se mesure : dans la bande d'aplat pur du bas de l'image, on prend la
moyenne de chaque colonne, et on regarde le saut d'une colonne a la suivante.
Un aplat convolue par un noyau normalise rend le meme aplat : tout saut
non nul est un artefact, et sa hauteur en pourcentage dit de combien.

Le rendu ne coute presque rien : le layer est un fichier, il n'y a pas un seul
rayon lance. Ce qui se mesure ici est la convolution, seule.
"""
from __future__ import print_function

import os
import re
import struct
import subprocess
import sys

CLARISSE = r"C:\Program Files\Isotropix\Clarisse 5.0 SP14\Clarisse"
MAGICK = r"C:\Program Files\ImageMagick-7.1.1-Q16-HDRI\magick.exe"
NATIVE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# La session interactive de Clarisse garde les .dll du dossier build ouvertes,
# et le linker ne peut alors pas les ecraser. BOKEH_BUILD permet de construire
# ailleurs et de faire pointer cnode dessus, sans fermer sa session.
BUILD = os.environ.get("BOKEH_BUILD") or os.path.join(NATIVE, "build")
PROJECT = r"J:\_WINDOWSTEMP\claude\seams\seams.project"
ROOT = r"J:\_WINDOWSTEMP\claude\seams"

VARIANTS = ["t0_temoin", "t1_vignettage", "t2_chroma_negatif",
            "t3_douceur", "t4_lames_creusees", "t5_optique_marquee",
            "t6_anamorphique"]

# Deux bandes, deux artefacts.
#
# APLAT : sous les boules. Un noyau normalise y rend exactement la source, donc
# tout ce qui s'y voit est un artefact. C'est la que se lit une couture
# d'ENERGIE -- un noyau tronque par une marge trop courte, un vignettage
# quantifie par tuile.
#
# BOULES : la bande des points. Une couture de FORME ne deplace pas la moyenne,
# elle deplace un bord : elle ne se voit que sur un contraste fort.
#
# Les colonnes extremes sont ecartees dans les deux cas : le bord du canvas a
# son propre traitement, qui n'est pas ce qu'on mesure.
BAND_FLAT = (700, 1040)
BAND_BALLS = (60, 560)
BAND_X = (192, 1728)
TILE = 64


def render(tag):
    """Un cnode par image. Le nom de sortie porte l'etiquette avant/apres."""
    for name in VARIANTS:
        out = os.path.join(ROOT, "%s_%s" % (tag, name))
        command = [os.path.join(CLARISSE, "cnode.exe"), PROJECT,
                   "-module_path", os.path.join(CLARISSE, "module"),
                   BUILD,
                   "-image", "build://project/" + name,
                   "-frames_list", "1",
                   "-output", out + ".exr"]
        proc = subprocess.Popen(command, cwd=CLARISSE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        raw = proc.communicate()[0].decode("mbcs", "replace")
        produced = out + ".exr00001.exr"
        note = ""
        for line in raw.splitlines():
            if "[Bokeh]" in line:
                note = re.sub(r"^\d\d:\d\d:\d\d\s+\d+/\d+MB\s*", "", line).strip()
        print("  %-20s %s  %s" % (name, "ok" if os.path.isfile(produced) else "ECHEC",
                                  note))


def read_float_image(path):
    """L'EXR relu en flottants, par un TIFF flottant puis un PFM.

    Le detour est impose par l'outillage : `magick` PLANTE sur les EXR de
    Clarisse (acces interdit, code 0xC0000409), et l'`iconvert` d'OpenImageIO
    ne connait pas le PFM en ecriture. Chacun sait faire une moitie du chemin.
    Le PFM se lit ensuite en dix lignes et rend les VRAIES valeurs HDR, la ou
    `txt:` sort des pourcentages quantifies qui ont deja fait conclure de
    travers sur ce projet.
    """
    tif = path + ".float.tif"
    pfm = path + ".pfm"
    subprocess.check_call([os.path.join(CLARISSE, "iconvert.exe"),
                           "-d", "float", path, tif],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    subprocess.check_call([MAGICK, tif, "-depth", "32",
                           "-define", "quantum:format=floating-point", pfm],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    handle = open(pfm, "rb")
    kind = handle.readline().strip()
    width, height = [int(v) for v in handle.readline().split()]
    scale = float(handle.readline().strip())
    channels = 3 if kind == b"PF" else 1
    order = "<" if scale < 0 else ">"
    data = struct.unpack(order + "f" * (width * height * channels),
                         handle.read(width * height * channels * 4))
    handle.close()
    # Le PFM se stocke de bas en haut : on remet a l'endroit.
    rows = []
    for y in range(height):
        start = (height - 1 - y) * width * channels
        rows.append(data[start:start + width * channels])
    return width, height, channels, rows


def column_means(width, height, channels, rows, channel, band):
    """Moyenne de chaque colonne sur une bande de lignes."""
    means = []
    for x in range(width):
        total = 0.0
        count = 0
        for y in range(band[0], min(band[1], height)):
            total += rows[y][x * channels + channel]
            count += 1
        means.append(total / count if count else 0.0)
    return means


def seam_ratio(means, reference):
    """Le saut moyen SUR la grille des tuiles, contre le saut moyen ailleurs.

    C'est la mesure qui tranche. Une image peut avoir toutes les
    discontinuites qu'elle veut -- ce sont ses details. Mais si les sauts
    tombent preferentiellement sur les multiples de 64, ils ne viennent pas de
    l'image : ils viennent du decoupage en tuiles, et rien d'autre dans la
    chaine n'a cette periode.
    """
    on, on_count, off, off_count = 0.0, 0, 0.0, 0
    peak, peak_x = 0.0, 0
    for x in range(BAND_X[0] + 1, BAND_X[1]):
        step = abs(means[x] - means[x - 1]) / reference * 100.0
        if x % TILE == 0:
            on += step
            on_count += 1
            if step > peak:
                peak, peak_x = step, x
        else:
            off += step
            off_count += 1
    on = on / on_count if on_count else 0.0
    off = off / off_count if off_count else 0.0
    return on, off, peak, peak_x


def report(path, label):
    if not os.path.isfile(path):
        print("  %-22s absent" % label)
        return
    width, height, channels, rows = read_float_image(path)
    # Un PFM gris (Pf) n'a qu'un canal : magick le choisit quand R, V et B
    # sont identiques, ce qui est le cas partout sauf sous aberration
    # chromatique -- justement le seul cas ou les canaux doivent etre lus
    # separement.
    wanted = ((0, "R"), (1, "V")) if channels >= 3 else ((0, "gris"),)
    for channel, cname in wanted:
        for band_name, band in (("aplat", BAND_FLAT), ("boules", BAND_BALLS)):
            means = column_means(width, height, channels, rows, channel, band)
            reference = sum(means[BAND_X[0]:BAND_X[1]]) / (BAND_X[1] - BAND_X[0])
            if reference <= 0.0:
                continue
            on, off, peak, peak_x = seam_ratio(means, reference)
            low = min(means[BAND_X[0]:BAND_X[1]])
            print("  %-22s %-4s %-6s  moyenne %8.4f  creux %+6.2f %%  "
                  "saut/grille %6.3f %%  saut/ailleurs %6.3f %%  rapport %6.1f"
                  % (label, cname, band_name, reference,
                     (low / reference - 1.0) * 100.0, on, off,
                     (on / off) if off > 1e-9 else 0.0))


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else "avant"
    if "--mesure-seule" not in sys.argv:
        print("rendu (%s)" % tag)
        render(tag)
    print("\nbandes : aplat y=%d..%d, boules y=%d..%d, colonnes x=%d..%d"
          % (BAND_FLAT[0], BAND_FLAT[1], BAND_BALLS[0], BAND_BALLS[1],
             BAND_X[0], BAND_X[1]))
    for name in VARIANTS:
        report(os.path.join(ROOT, "%s_%s.exr00001.exr" % (tag, name)), name)


if __name__ == "__main__":
    main()
