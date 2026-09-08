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

BASE = dict(resolution=96, levels=5, iterations=8, substeps=160, damping=0.02,
            pressure=4.0, pin_rings=2,
            seam=0.055, seam_depth=0.03,
            seam_gather=0.30, seam_wrinkle_scale=0.055,
            wrinkles=0.12, wrinkle_reach=2.0, corner_gather=0.0,
            stretch=0.02, compression=500.0, shear_stiffness=4.0,
            crease_stretch=2.5, wrinkle_variation=0.45,
            wrinkle_scale=0.045, wrinkle_size=0.0,
            gravity_strength=0.0,
            jitter=0.010, smoothing=0, smoothing_amount=0.25, seed=1)

VARIANTS = [
    ("fort oriente", dict(jitter=0.010, crease_stretch=2.5)),
    ("faible isotrope", dict(jitter=0.002, crease_stretch=1.0)),
    ("tres faible", dict(jitter=0.0005, crease_stretch=1.0)),
    ("faible + variation", dict(jitter=0.002, crease_stretch=1.0,
                                wrinkle_variation=0.8)),
    ("faible + long", dict(jitter=0.002, crease_stretch=1.0,
                           substeps=240, iterations=8)),
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

    # La valeur retenue par le node, pas celle qu'on a demandee : le CID borne,
    # et une borne silencieuse fausse toute la lecture.
    n = int(node.get_attribute("resolution").get_long())
    side = n + 1
    cloud = mesh.get_point_cloud()
    if cloud.get_point_count() < side * side:
        print("%-22s : maillage trop court" % label)
        return

    height = [float(cloud.get_position(k)[1]) for k in range(side * side)]

    # Il n'y a plus de patron analytique : la forme est un resultat. On compare
    # donc a un dome lisse cale sur la hauteur obtenue, ce qui suffit largement
    # pour classer des variantes entre elles.
    import math
    top = max(height)
    base = min(height)
    relief = 0.0
    for j in range(side):
        v = float(j) / n
        for i in range(side):
            u = float(i) / n
            dome = base + (top - base) * math.sin(math.pi * u) * math.sin(math.pi * v)
            relief = max(relief, abs(height[j * side + i] - dome))

    # Le debordement : de combien le tissu sort du contour du polygone. Deux
    # coussins voisins partagent ce contour, donc tout ce qui depasse entre
    # dans le voisin. C'est ce qui faisait le champignon noir au milieu de
    # chaque arete.
    over = 0.0
    for k in range(cloud.get_point_count()):
        p = cloud.get_position(k)
        over = max(over, abs(float(p[0])) - 1.0, abs(float(p[2])) - 1.0)

    # Laplacien, nul sur le bord.
    lap = [0.0] * (side * side)
    for j in range(1, side - 1):
        for i in range(1, side - 1):
            lap[j * side + i] = (height[j * side + i - 1]
                                 + height[j * side + i + 1]
                                 + height[(j - 1) * side + i]
                                 + height[(j + 1) * side + i]
                                 - 4.0 * height[j * side + i])

    border = height[0]
    dip = min(height[j * side + i]
              for j in range(1, side - 1) for i in range(1, side - 1)) - border
    edge_mid = lap[1 * side + side // 2]
    peak = max(abs(v) for v in lap) or 1.0
    middle = [lap[(side // 2) * side + i] for i in range(1, side - 1)]
    diagonal = [lap[k * side + k] for k in range(1, side - 1)]
    ring_row = max(2, int(side * 0.09))
    ring = [lap[ring_row * side + i] for i in range(1, side - 1)]

    print("")
    print("[bord %2d] %-24s h %.3f r %.3f mil %d diag %d deb %+.3f"
          % (crossings(ring), label, max(height), relief,
             crossings(middle), crossings(diagonal), over))
    if side <= 34:
        step = 1 if side <= 22 else 2
        for j in range(0, side, step):
            row = ""
            for i in range(0, side, step):
                t = 0.5 + 0.5 * lap[j * side + i] / peak
                index = int(t * (len(RAMP) - 1) + 0.5)
                row += RAMP[max(0, min(len(RAMP) - 1, index))] * 2
            print("  |" + row + "|")


print("")
print("DEBUT" + "=" * 60)
for label, values in VARIANTS:
    probe(label, values)
print("FIN" + "=" * 62)
