# -*- coding: utf-8 -*-
"""Lit le champ de plis d'un seul coussin, sans rendu.

Juger un capitonnage au rendu coute cher et repond lentement. Or ce qu'on
cherche a regler -- combien de plis, ou, de quelle amplitude -- est deja tout
entier dans le maillage.

Ce qu'on affiche est le laplacien de la hauteur, pas la hauteur : un dome lisse
a un laplacien d'un seul signe, tandis que chaque pli y apparait comme une
alternance creux/bosse. Compter les changements de signe le long d'une ligne
donne donc directement le nombre de plis, sans avoir a debattre de ce qui est
« lisse ».

Le coussin est un quad unique de deux unites de cote, horizontal, donc la
hauteur est simplement la coordonnee Y.
"""

BASE = dict(resolution=32, levels=3, iterations=300,
            puff_relative=0.32, shoulder=2.5,
            pressure=1.0, pressure_softness=0.005,
            seam=0.10, seam_depth=0.09,
            wrinkles=0.14, wrinkle_reach=0.45, corner_gather=0.12,
            stretch=0.02, shear_stiffness=4.0,
            wrinkle_scale=0.18, wrinkle_size=0.0,
            stuffing=1.2, gravity_strength=0.0,
            jitter=0.006, smoothing=0, smoothing_amount=0.25, seed=1)

VARIANTS = [
    ("mplat 0.06", dict(seam=0.06)),
    ("mplat 0.10", dict()),
    ("mplat 0.16", dict(seam=0.16)),
    ("epaule 4.0", dict(shoulder=4.0)),
    ("portee 0.8", dict(wrinkle_reach=0.8)),
]

RAMP = "@%#*+=-:. "   # negatif -> positif

grid = ix.cmds.CreateObject("carre", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(grid) + ".size[0]", str(grid) + ".size[1]"], ["2", "2"])
ix.cmds.SetValues([str(grid) + ".spans[0]", str(grid) + ".spans[1]"], ["1", "1"])

counter = [0]


def crossings(values):
    """Nombre de changements de signe, en ignorant le bruit sous le dixieme."""
    peak = max(abs(v) for v in values) if values else 0.0
    if peak < 1e-12:
        return 0
    keep = [v for v in values if abs(v) > 0.1 * peak]
    return sum(1 for k in range(1, len(keep)) if keep[k] * keep[k - 1] < 0.0)


def probe(label, values):
    counter[0] += 1
    node = ix.cmds.CreateObject("q%d" % counter[0], "GeometryQuilt",
                                "Global", "project:/")
    ix.cmds.SetValues([str(node) + ".input_geometry"], [str(grid)])
    settings = dict(BASE)
    settings.update(values)
    for key, value in settings.items():
        if node.get_attribute(key) is None:
            print("  !! pas d'attribut %s" % key)
            continue
        ix.cmds.SetValues([str(node) + "." + key], [str(value)])

    mesh = node.get_module().get_geometry()
    if mesh is None:
        print("%-22s : RIEN" % label)
        return

    n = int(settings["resolution"])
    side = n + 1
    cloud = mesh.get_point_cloud()
    if cloud.get_point_count() < side * side:
        print("%-22s : maillage trop court" % label)
        return

    height = [float(cloud.get_position(k)[1]) for k in range(side * side)]

    # Le patron analytique : c'est de lui que le tissu s'ecarte, et cet ecart
    # est exactement ce qu'on appelle un pli.
    import math
    extent = math.sqrt(2.0)
    puff = float(settings["puff_relative"])
    shoulder = float(settings["shoulder"])
    depth = float(settings["seam_depth"])
    relief = 0.0
    for j in range(side):
        v = float(j) / n
        for i in range(side):
            u = float(i) / n
            flat = 2.0 * float(settings["seam"])
            span = 1.0 - flat
            du = (2.0 * min(u, 1.0 - u) - flat) / span
            dv = (2.0 * min(v, 1.0 - v) - flat) / span
            du = min(1.0, max(0.0, du))
            dv = min(1.0, max(0.0, dv))
            bump = ((1.0 - (1.0 - du) ** shoulder)
                    * (1.0 - (1.0 - dv) ** shoulder))
            patron = -depth * extent + extent * puff * bump
            relief = max(relief, abs(height[j * side + i] - patron))

    # Laplacien, nul sur le bord.
    lap = [0.0] * (side * side)
    for j in range(1, side - 1):
        for i in range(1, side - 1):
            lap[j * side + i] = (height[j * side + i - 1]
                                 + height[j * side + i + 1]
                                 + height[(j - 1) * side + i]
                                 + height[(j + 1) * side + i]
                                 - 4.0 * height[j * side + i])

    # Le debordement : de combien le tissu sort du contour du polygone. Deux
    # coussins voisins partagent ce contour, donc tout ce qui depasse entre
    # dans le voisin. C'est ce qui faisait le champignon noir au milieu de
    # chaque arete.
    over = 0.0
    for k in range(cloud.get_point_count()):
        p = cloud.get_position(k)
        over = max(over, abs(float(p[0])) - 1.0, abs(float(p[2])) - 1.0)

    border = height[0]
    dip = min(height[j * side + i]
              for j in range(1, side - 1) for i in range(1, side - 1)) - border
    edge_mid = lap[1 * side + side // 2]
    peak = max(abs(v) for v in lap) or 1.0
    middle = [lap[(side // 2) * side + i] for i in range(1, side - 1)]
    diagonal = [lap[k * side + k] for k in range(1, side - 1)]
    ring = [lap[(side // 5) * side + i] for i in range(1, side - 1)]

    print("")
    print("[%+.4f] %-22s h %.3f r %.3f p %d/%d/%d"
          % (over, label, max(height), relief,
             crossings(middle), crossings(diagonal), crossings(ring)))
    for j in range(side):
        row = ""
        for i in range(side):
            t = 0.5 + 0.5 * lap[j * side + i] / peak
            index = int(t * (len(RAMP) - 1) + 0.5)
            row += RAMP[max(0, min(len(RAMP) - 1, index))] * 2
        print("  |" + row + "|")


print("")
print("DEBUT" + "=" * 60)
for label, values in VARIANTS:
    probe(label, values)
print("FIN" + "=" * 62)
