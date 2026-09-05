# -*- coding: utf-8 -*-
"""Mesure la largeur de la transition au bord d'un premier plan flou.

    python native/tests/measure_edge.py

Pas de numpy, pas d'imageio, pas d'OpenImageIO sur cette machine, et PIL
n'ouvre pas ces EXR. On passe donc par les deux outils qui sont la : iconvert
d'Isotropix pour sortir du EXR a cinq canaux, puis ImageMagick pour separer
les canaux et vider une ligne d'un coup. Lire les pixels un par un avec
`magick -format %[fx:p{x,y}]` demanderait une invocation par pixel, soit des
minutes par image ; un `txt:-` sur une bande rend toute la ligne en une fois.

Ce que la sortie veut dire est explique dans README_foreground.md. En deux
mots : `10-90` est la largeur demandee, mais c'est `debord` qui separe
vraiment un filtre juste d'un filtre faux.
"""
from __future__ import print_function

import glob
import os
import re
import subprocess
import sys

CLARISSE = r"C:\Program Files\Isotropix\Clarisse 5.0 SP14\Clarisse"
ICONVERT = os.path.join(CLARISSE, "iconvert.exe")
MAGICK = "magick"

DEFAULT_DIR = r"J:\_WINDOWSTEMP\claude\fg"

# Combien de pixels laisser entre la silhouette et la fenetre ou on releve le
# niveau du fond. Il faut sortir de tout ce que le flou a pu deposer, sinon le
# plateau de reference est deja contamine et la mesure se mord la queue.
BACKGROUND_GAP = 1.6      # en multiples du rayon configure
BACKGROUND_WINDOW = 30    # largeur de la fenetre de releve, en pixels

# Un plateau de fond et un plateau de premier plan trop proches ne definissent
# aucune transition : mieux vaut le dire que rendre un nombre invente.
MIN_CONTRAST = 1e-4

PIXEL = re.compile(r"^(\d+),(\d+): \(([^)]*)\)")


def run(command):
    proc = subprocess.Popen(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    out, err = proc.communicate()
    if proc.returncode != 0:
        raise RuntimeError("%s a echoue :\n%s"
                           % (command[0], err.decode("mbcs", "replace")))
    return out.decode("ascii", "replace")


def dump(path, crop=None):
    """Rend un canal (ou une bande) sous forme de liste de flottants lineaires.

    Le champ entre parentheses de `txt:` est la valeur multipliee par le
    quantum annonce dans l'entete -- 65535 ici. C'est un entier, donc sans
    ambiguite de formatage, et il conserve les valeurs HDR au-dela de 1.0 :
    38.0 ressort en 2490330. Le champ `gray(N%)` du meme ligne dit la meme
    chose avec moins de chiffres.
    """
    command = [MAGICK, path]
    if crop is not None:
        command += ["-crop", crop, "+repage"]
    command += ["-depth", "32", "-define", "quantum:format=floating-point",
                "txt:-"]
    text = run(command)

    lines = text.splitlines()
    header = lines[0]
    quantum = 65535.0
    fields = header.rsplit(":", 1)[-1].strip().split(",")
    if len(fields) >= 4:
        try:
            quantum = float(fields[3])
        except ValueError:
            pass

    width = int(fields[0])
    values = []
    for line in lines[1:]:
        match = PIXEL.match(line)
        if match is None:
            continue
        values.append(float(match.group(3).split(",")[0]) / quantum)
    return values, width


def channels_of(exr, work):
    """EXR -> un fichier TIFF par canal. Renvoie leurs chemins, dans l'ordre."""
    if not os.path.isdir(work):
        os.makedirs(work)
    tif = os.path.join(work, "all.tif")
    # magick n'ouvre pas directement un EXR a cinq canaux ; iconvert si.
    run([ICONVERT, exr, tif])
    pattern = os.path.join(work, "ch_%d.tif")
    for stale in glob.glob(os.path.join(work, "ch_*.tif")):
        os.remove(stale)
    run([MAGICK, tif, "-separate", pattern])
    return sorted(glob.glob(os.path.join(work, "ch_*.tif")),
                  key=lambda p: int(re.search(r"ch_(\d+)", p).group(1)))


def pick_depth_channel(paths, expected_bg):
    """Lequel des canaux est depth.Z.

    L'ordre observe est R, G, B, A, depth.Z, mais on ne le suppose pas : le
    canal de profondeur est celui dont le maximum ressemble a la profondeur du
    fond annoncee par le manifeste. Les canaux de couleur, eux, valent
    quelques unites au plus.
    """
    best, best_gap = None, None
    for index, path in enumerate(paths):
        text = run([MAGICK, path, "-format", "%[fx:maxima]", "info:"])
        try:
            maxima = float(text.strip())
        except ValueError:
            continue
        gap = abs(maxima - expected_bg)
        if best_gap is None or gap < best_gap:
            best, best_gap = index, gap
    return best, best_gap


def find_silhouette(depth, width, height, bg_depth, fg_depth):
    """La ligne la plus large du premier plan, et ses deux bords geometriques.

    On lit la silhouette dans la PROFONDEUR, jamais dans la couleur : le
    filtre ne touche que RGBA, donc l'AOV de profondeur donne toujours la
    position vraie du bord, y compris sur les variantes floutees. C'est ce qui
    permet de mesurer le debord d'un cote et l'autre du bord reel.
    """
    middle = 0.5 * (bg_depth + fg_depth)
    best = None
    for y in range(height):
        row = depth[y * width:(y + 1) * width]
        near = [x for x, value in enumerate(row) if 0.0 < value < middle]
        if not near:
            continue
        span = near[-1] - near[0] + 1
        if best is None or span > best[0]:
            best = (span, y, near[0], near[-1])
    return best


def crossing(profile, start, step, level):
    """Ou le profil normalise traverse `level`, en partant du fond vers l'objet.

    Renvoie une position fractionnaire : sans interpolation, toute largeur
    mesuree serait arrondie au pixel et un ecart de 1 a 3 px ne voudrait plus
    rien dire.
    """
    x = start
    while 0 <= x + step < len(profile):
        a, b = profile[x], profile[x + step]
        if (a >= level) != (b >= level):
            if a == b:
                return float(x)
            return x + step * (a - level) / float(a - b)
        x += step
    return None


def measure_side(profile, edge, step, radius, level_fg, level_bg):
    """Un bord. `step` vaut -1 a gauche (le fond est vers les x decroissants)."""
    contrast = level_bg - level_fg
    if abs(contrast) < MIN_CONTRAST:
        return None

    normalised = [(v - level_fg) / contrast for v in profile]

    # On demarre franchement dans le fond, hors de toute contamination, et on
    # marche vers l'objet.
    start = int(round(edge - step * (BACKGROUND_GAP * radius)))
    start = max(0, min(len(profile) - 1, start))

    x90 = crossing(normalised, start, -step, 0.9)
    x10 = crossing(normalised, start, -step, 0.1)
    if x90 is None or x10 is None:
        return None

    return {
        "x90": x90,
        "x10": x10,
        "width": abs(x10 - x90),
        # Positif = le premier plan a bave sur le fond net, ce qui est le
        # comportement recherche. Vers zero = la silhouette est restee dure.
        "spill": (x90 - edge) * -step,
        "reach": (edge - x10) * -step,
    }


def read_manifest(directory):
    path = os.path.join(directory, "manifest.tsv")
    if not os.path.isfile(path):
        sys.exit("manifeste introuvable : %s\n"
                 "Construire d'abord la scene avec make_foreground_test.py."
                 % path)
    rows = []
    with open(path) as handle:
        head = handle.readline().rstrip("\n").split("\t")
        for line in handle:
            line = line.rstrip("\n")
            if line:
                rows.append(dict(zip(head, line.split("\t"))))
    return rows


def measure_variant(entry, directory):
    name = entry["variant"]
    radius = float(entry["radius"])
    bg_depth = float(entry["bg_depth"])
    fg_depth = float(entry["fg_depth"])
    width = int(entry["width"])
    height = int(entry["height"])

    # cnode suffixe le numero d'image au nom demande : slices_01.exr donne
    # slices_01.exr00001.exr.
    found = sorted(glob.glob(os.path.join(directory, name + "*.exr")))
    if not found:
        return None, "pas de rendu (%s*.exr absent)" % name

    work = os.path.join(directory, "_work", name)
    paths = channels_of(found[0], work)
    if len(paths) < 5:
        return None, ("%d canaux seulement -- l'AOV depth manque, verifier "
                      "output_layer et enabled_aov_list" % len(paths))

    index, gap = pick_depth_channel(paths, bg_depth)
    if gap is None or gap > 0.25 * bg_depth:
        return None, ("aucun canal ne ressemble a une profondeur "
                      "(ecart %.2f sur %.2f attendu)" % (gap or -1, bg_depth))

    depth, _ = dump(paths[index])
    if len(depth) < width * height:
        return None, "profondeur tronquee (%d valeurs)" % len(depth)

    silhouette = find_silhouette(depth, width, height, bg_depth, fg_depth)
    if silhouette is None:
        return None, "premier plan introuvable dans la profondeur"
    span, row, left, right = silhouette

    # La luminance sur cette ligne. Trois canaux plutot qu'un seul : si une
    # aberration chromatique traine, les bords rouge et bleu ne tombent pas au
    # meme endroit et la moyenne le dit au lieu de le cacher.
    crop = "%dx1+0+%d" % (width, row)
    profile = None
    for path in paths[:3]:
        values, _ = dump(path, crop)
        if profile is None:
            profile = list(values)
        else:
            profile = [a + b for a, b in zip(profile, values)]
    profile = [v / 3.0 for v in profile]

    # Les plateaux. Le premier plan est releve au coeur de la silhouette, le
    # fond bien au-dela de la portee du flou, de chaque cote separement : si
    # les deux valeurs de fond different, quelque chose eclaire le mur de
    # travers et la mesure n'est pas fiable.
    core = profile[left + span // 4:right - span // 4 + 1]
    level_fg = median(core)

    gap_px = int(BACKGROUND_GAP * radius) if radius > 0 else 4
    left_bg = window(profile, left - gap_px - BACKGROUND_WINDOW,
                     left - gap_px)
    right_bg = window(profile, right + gap_px,
                      right + gap_px + BACKGROUND_WINDOW)
    if not left_bg or not right_bg:
        return None, "pas assez de fond autour de la silhouette"

    result = {
        "row": row, "left": left, "right": right, "span": span,
        "fg": level_fg, "bg_left": median(left_bg), "bg_right": median(right_bg),
        "radius": radius, "slices": entry["slices_applied"],
        "depth_channel": index,
        "fg_depth_seen": median([depth[row * width + x]
                                 for x in range(left, right + 1)]),
    }
    result["L"] = measure_side(profile, left - 0.5, -1, radius,
                               level_fg, result["bg_left"])
    result["R"] = measure_side(profile, right + 0.5, 1, radius,
                               level_fg, result["bg_right"])
    return result, None


def window(values, start, stop):
    start = max(0, start)
    stop = min(len(values), stop)
    return values[start:stop] if stop > start else []


def median(values):
    ordered = sorted(values)
    count = len(ordered)
    if count == 0:
        return 0.0
    if count % 2:
        return ordered[count // 2]
    return 0.5 * (ordered[count // 2 - 1] + ordered[count // 2])


def show(name, result):
    print("")
    print("  %s" % name)
    print("    rayon configure      %.1f px   (transition attendue ~%.0f px "
          "une fois le filtre corrige)" % (result["radius"],
                                           2.0 * result["radius"]))
    print("    corrective_slices    %s" % result["slices"])
    print("    ligne mesuree        y=%d   silhouette x=%d..%d (%d px), "
          "canal de profondeur %d"
          % (result["row"], result["left"], result["right"], result["span"],
             result["depth_channel"]))
    print("    niveaux              premier plan %.4f   fond %.4f / %.4f"
          % (result["fg"], result["bg_left"], result["bg_right"]))
    for side, label in (("L", "bord gauche"), ("R", "bord droit ")):
        side_result = result[side]
        if side_result is None:
            print("    %s          non mesurable (plateaux confondus)" % label)
            continue
        print("    %s          10-90 = %6.2f px   debord %6.2f px   "
              "penetration %6.2f px"
              % (label, side_result["width"], side_result["spill"],
                 side_result["reach"]))


def main(argv):
    directory = argv[1] if len(argv) > 1 else DEFAULT_DIR
    rows = read_manifest(directory)

    print("Largeur de transition au bord d'un premier plan flou")
    print("dossier : %s" % directory)

    measured = 0
    for entry in rows:
        result, problem = measure_variant(entry, directory)
        if result is None:
            print("")
            print("  %s" % entry["variant"])
            print("    %s" % problem)
            continue
        show(entry["variant"], result)
        measured += 1

    print("")
    print("  debord = de combien le premier plan flou deborde sur le fond NET.")
    print("  C'est le nombre qui dit si la correction a pris : proche de zero,")
    print("  la silhouette est restee dure ; proche du rayon, elle se dissout.")
    return 0 if measured else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
