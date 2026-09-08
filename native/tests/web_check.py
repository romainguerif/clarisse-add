# -*- coding: utf-8 -*-
"""Mesure une toile plutot que de la regarder.

Ce qu'on veut verifier sont des nombres publies, pas des impressions : le
nombre de rayons, le rapport des angles entre le haut et le bas, le rapport des
longueurs, et la facon dont le pas de la spirale varie du moyeu vers le bord.
Tout cela se lit dans le maillage, sans lancer un seul rayon.

Trois lecons de la premiere version, qui se trompait sur ses quatre mesures :

  - **on ne sonde pas des sommets comme si c'etait une courbe.** Un fil est une
    polyligne ; une fenetre plus etroite que l'espacement de ses points n'en
    attrape qu'un sur deux, et on compte alors quinze rayons sur trente-deux.

  - **on isole la spirale en construisant la toile deux fois.** Les accesseurs
    par groupe de shading reclament des tableaux C++ que SWIG n'expose pas.
    Mais une toile dont la zone libre est poussee au maximum n'a plus de
    spirale et reste identique par ailleurs : la spirale est exactement ce que
    la premiere a de plus que la seconde.

  - **on mesure le pas le long d'un rayon precis**, pas dans un secteur. La
    spirale ne pose un sommet que la ou elle croise un rayon ; en visant un
    rayon nomme, chaque tour donne un point et un seul, et les ecarts sont
    directement le pas.

Et une lecon sur la toile elle-meme : le polygone du cadre a sa propre
anisotropie -- vingt-quatre pour cent avec cinq ancrages, soit plus que
l'asymetrie qu'on cherche a mesurer. Les tests d'asymetrie tournent donc avec
beaucoup d'ancrages, ou le cadre est presque une ellipse. Une sonde choisit ses
conditions.

Lancer :
    cnode empty.project -module_path <module> <build> -script web_check.py
"""
from __future__ import print_function

import math

print("")
print("DEBUT" + "=" * 50)

BASE = dict(radius=0.07, radii=32, asymmetry=1.4, angle_asymmetry=1.57,
            aspect=1.0, anchors=24, anchor_spread=0.0, frame_margin=0.0,
            # Les fils d'amarrage sortent du cadre : ils fausseraient l'etendue.
            anchor_length=0.0,
            hub=0.10, hub_turns=5, free_zone=0.30,
            spiral_pitch=0.04, pitch_growth_top=0.0, pitch_growth_bottom=0.0,
            pitch_variation=0.0, spiral_uturns=0, free_sector=0.0,
            deviated_radii=0, y_radii=0, extra_radii=0,
            depth=0.0, thread_radius=0.0015, radius_variation=0.0,
            sides=3, seed=1)

COUNTER = [0]


def make(overrides):
    COUNTER[0] += 1
    node = ix.cmds.CreateObject("toile%d" % COUNTER[0], "GeometryWeb",
                                "Global", "project:/")
    values = dict(BASE)
    values.update(overrides)
    for key, value in values.items():
        if node.get_attribute(key) is None:
            print("  !! pas d'attribut %s" % key)
            continue
        ix.cmds.SetValues([str(node) + "." + key], [str(value)])
    return node


def points_of(node):
    """Les sommets du maillage, projetes dans le plan de la toile."""
    module = node.get_module()
    mesh = module.get_geometry() if module is not None else None
    if mesh is None:
        return None
    positions = mesh.get_point_cloud().get_positions()
    return [(float(positions[k][0]), float(positions[k][1]))
            for k in range(len(positions))]


def hub_of(points):
    """Le moyeu : l'endroit le plus dense du maillage. Tous les rayons y
    convergent et la spirale du moyeu s'y enroule cinq fois."""
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    lo_x, hi_x, lo_y, hi_y = min(xs), max(xs), min(ys), max(ys)
    step = max(hi_x - lo_x, hi_y - lo_y) / 40.0

    counts = {}
    for x, y in points:
        counts[(int((x - lo_x) / step), int((y - lo_y) / step))] = \
            counts.get((int((x - lo_x) / step), int((y - lo_y) / step)), 0) + 1
    best = max(counts, key=counts.get)
    cx = lo_x + (best[0] + 0.5) * step
    cy = lo_y + (best[1] + 0.5) * step

    near = [p for p in points
            if (p[0] - cx) ** 2 + (p[1] - cy) ** 2 < (step * 1.5) ** 2]
    if not near:
        return cx, cy
    return (sum(p[0] for p in near) / len(near),
            sum(p[1] for p in near) / len(near))


def polar(points, hub):
    return [(math.hypot(x - hub[0], y - hub[1]),
             math.atan2(x - hub[0], y - hub[1])) for x, y in points]


def cluster(values, gap):
    if not values:
        return []
    groups = [[values[0]]]
    for v in values[1:]:
        if v - groups[-1][-1] < gap:
            groups[-1].append(v)
        else:
            groups.append([v])
    return [sum(g) / len(g) for g in groups]


def angular_cluster(angles, gap):
    values = sorted(a + 2 * math.pi if a < 0 else a for a in angles)
    groups = cluster(values, gap)
    if len(groups) > 1 and (values[0] + 2 * math.pi) - values[-1] < gap:
        groups = groups[:-1]
    return groups


def signed(angle):
    return math.atan2(math.sin(angle), math.cos(angle))


def verdict(got, want, tolerance):
    return "ok" if abs(got - want) <= tolerance else "ECART"


def probe(label, overrides, radii=32, asymmetry=None, angle_asymmetry=None,
          pitch=None, growth_top=None, growth_bottom=None, spread=0.2):
    print("")
    print("--- %s" % label)

    node = make(overrides)
    points = points_of(node)
    if not points:
        print("  ECHEC  aucune geometrie")
        return None

    hub = hub_of(points)
    pol = polar(points, hub)
    extent = max(r for r, _ in pol)
    print("  sommets %d, etendue %.5g" % (len(points), extent))

    result = {}

    # --- combien de rayons, comptes dans la zone libre ou il n'y a qu'eux
    band = [a for r, a in pol
            if abs(r - extent * 0.16) < extent * 0.03]
    angles = angular_cluster(band, 0.35 * 2 * math.pi / radii)
    result["rayons"] = len(angles)
    print("  rayons                        : %d   attendu %d   %s"
          % (len(angles), radii, "ok" if len(angles) == radii else "ECART"))

    # --- l'asymetrie des angles
    if len(angles) >= 8 and angle_asymmetry is not None:
        ordered = sorted(angles)
        deltas = []
        for i in range(len(ordered)):
            a = ordered[i]
            d = ordered[(i + 1) % len(ordered)] - a
            if d <= 0:
                d += 2 * math.pi
            deltas.append((a + d * 0.5, d))
        top = [d for m, d in deltas if math.cos(m) > 0.7]
        bottom = [d for m, d in deltas if math.cos(m) < -0.7]
        if top and bottom:
            mt, mb = sum(top) / len(top), sum(bottom) / len(bottom)
            result["angle_haut_bas"] = mt / mb
            print("  angle haut / bas              : %.2f deg / %.2f deg = "
                  "%.2f   attendu %.2f  %s"
                  % (math.degrees(mt), math.degrees(mb), mt / mb,
                     angle_asymmetry,
                     verdict(mt / mb, angle_asymmetry, 0.15)))

    # --- l'asymetrie des longueurs
    reach = {}
    reach["haut"] = max((r for r, a in pol if math.cos(a) > 0.985), default=0.0)
    reach["bas"] = max((r for r, a in pol if math.cos(a) < -0.985), default=0.0)
    if asymmetry is not None and reach["haut"] > 1e-12:
        got = reach["bas"] / reach["haut"]
        result["etendue_bas_haut"] = got
        print("  etendue bas / haut            : %.2f   attendu %.2f  %s"
              % (got, asymmetry, verdict(got, asymmetry, 0.1)))

    # --- le pas de la spirale
    if pitch is not None and angles:
        naked = dict(overrides)
        naked["free_zone"] = 0.95
        without = points_of(make(naked))
        spiral = points[len(without):] if without and len(without) < len(points) \
            else []
        if not spiral:
            print("  pas                           : ECHEC, spirale non isolee")
            return result

        # Le nombre de tours se deduit du pas comme le fait le node, et la
        # prediction est alors sans echelle : sur un rayon donne la spirale
        # couvre toujours la meme fraction, donc le pas moyen vaut cette
        # fraction divisee par le nombre de tours.
        free = overrides.get("free_zone", BASE["free_zone"])
        turns = max(2, int(round((0.97 - free) / pitch)))
        expected = (0.97 - free) / turns

        spol = polar(spiral, hub)
        step = 2 * math.pi / radii
        for name, want, growth in (("haut", 0.0, growth_top),
                                   ("bas", math.pi, growth_bottom)):
            # On vise un rayon nomme : chaque tour de spirale y depose un
            # sommet et un seul, donc les ecarts sont directement le pas. Le
            # regroupement est cale sur l'epaisseur du fil, pas sur le pas :
            # une tolerance large fusionnait une spire sur deux, et faisait
            # croire a un pas double.
            aim = min(angles, key=lambda a: abs(signed(a - want)))
            hits = sorted(r for r, a in spol
                          if abs(signed(a - aim)) < step * 0.3)
            loops = cluster(hits, extent * 0.006)
            if len(loops) < 4 or reach[name] < 1e-12:
                print("  pas (%-4s)                    : trop peu de tours (%d)"
                      % (name, len(loops)))
                continue
            gaps = [b - a for a, b in zip(loops, loops[1:])]
            mean = (sum(gaps) / len(gaps)) / reach[name]
            inner = sum(gaps[:2]) / 2.0
            outer = sum(gaps[-2:]) / 2.0
            opening = outer / inner if inner > 1e-15 else 0.0
            result["pas_" + name] = mean
            result["ouverture_" + name] = opening
            result["tours_" + name] = len(gaps)

            line = ("  pas (%-4s)                    : %2d tours (attendu %d), "
                    "%.2f %% du rayon local   attendu %.2f  %s"
                    % (name, len(gaps), turns, 100.0 * mean, 100.0 * expected,
                       verdict(mean, expected, expected * spread)))
            if growth is not None:
                # Ce que la loi predit : le pas vaut 1 + g*(t - milieu) fois le
                # pas moyen, avec le milieu au centre de la zone de capture. La
                # normalisation du node ne change pas ce rapport.
                middle = (free + 1.0) * 0.5
                lo = 1.0 + growth * (free - middle)
                hi = 1.0 + growth * (0.97 - middle)
                want_open = hi / lo if lo > 1e-9 else 0.0
                line += ("\n                                  bord / moyeu %.2f"
                         "   attendu %.2f  %s"
                         % (opening, want_open,
                            verdict(opening, want_open, want_open * 0.25)))
            print(line)

    return result


# --- le protocole -----------------------------------------------------------

probe("reference : 32 rayons, cadre a 24 ancrages", {},
      radii=32, asymmetry=1.4, angle_asymmetry=1.57, pitch=0.04,
      growth_top=0.0, growth_bottom=0.0)

probe("18 rayons", dict(radii=18), radii=18,
      asymmetry=1.4, angle_asymmetry=1.57)

probe("temoin : toile symetrique",
      dict(asymmetry=1.0, angle_asymmetry=1.0), radii=32,
      asymmetry=1.0, angle_asymmetry=1.0)

# La loi sectorielle du pas : il doit s'ouvrir vers le haut et se resserrer
# legerement vers le bas. C'est elle qui distingue une vraie toile d'une
# spirale d'Archimede, et c'est celle qu'il serait le plus facile de rater sans
# mesurer -- la premiere version du node moyennait la croissance sur le tour et
# rendait les deux moities identiques.
probe("pas sectoriel : ouvert en haut, plat en bas",
      dict(pitch_growth_top=0.9, pitch_growth_bottom=0.1), radii=32,
      pitch=0.04, growth_top=0.9, growth_bottom=0.1)

# L'invariance d'echelle. C'est la question qui a coute une demi-journee sur le
# tissu : un reglage en fraction doit rendre la meme toile a toute taille.
big = probe("la meme, mille fois plus grande", dict(radius=70.0),
            radii=32, asymmetry=1.4, angle_asymmetry=1.57, pitch=0.04)
small = probe("la meme, cent fois plus petite", dict(radius=0.0007),
              radii=32, asymmetry=1.4, angle_asymmetry=1.57, pitch=0.04)
if big and small:
    print("")
    print("--- invariance d'echelle, sur un facteur cent mille")
    for key in sorted(set(big) & set(small)):
        a, b = big[key], small[key]
        drift = abs(a - b) / max(1e-12, abs(a))
        print("  %-18s %12.6g  %12.6g   ecart %.1g   %s"
              % (key, a, b, drift, "ok" if drift < 1e-5 else "ECART"))

# Toutes les anomalies a la fois. Elles doivent changer la toile sans la
# casser : le compte de rayons tient, le pas reste dans ses bornes. Les fils
# d'amarrage restent coupes, parce qu'ils sortent du cadre et fausseraient
# l'etendue dont toutes les mesures se servent.
#
# La tolerance sur le pas est ouverte a moitie, et c'est voulu : on demande une
# variation de trente pour cent d'un tour a l'autre et onze demi-tours en
# epingle. Un echantillon pris dans une seule direction DOIT s'ecarter de la
# moyenne -- s'il ne s'en ecartait pas, le desordre ne servirait a rien.
probe("toutes les anomalies, sur le cadre de mesure",
      dict(pitch_variation=0.3, spiral_uturns=11,
           deviated_radii=3, y_radii=2, extra_radii=1, depth=0.015,
           radius_variation=0.4, pitch_growth_top=0.9,
           pitch_growth_bottom=0.1),
      radii=32, pitch=0.04, spread=0.5)

# Et la toile telle qu'elle sortira de la boite, sans un seul reglage. On ne
# mesure plus rien ici : on verifie qu'elle se construit et qu'elle a le poids
# qu'on attend.
print("")
print("--- la toile d'usine, aucun reglage touche")
node = ix.cmds.CreateObject("usine", "GeometryWeb", "Global", "project:/")
mesh = node.get_module().get_geometry()
if mesh is None:
    print("  ECHEC  aucune geometrie")
else:
    counts = {}
    for i in range(mesh.get_primitive_count()):
        name = mesh.get_primitive_shading_group_name(i)
        counts[name] = counts.get(name, 0) + 1
    print("  sommets %d, polygones %d"
          % (mesh.get_vertex_count(), mesh.get_polygon_count()))
    for name in sorted(counts):
        print("  %-10s %6d primitives" % (name, counts[name]))
    missing = [n for n in ("amarrage", "cadre", "rayon", "moyeu", "spirale")
               if n not in counts]
    print("  les cinq soies sont la        : %s"
          % ("oui" if not missing else "NON, il manque %s" % missing))

print("FIN" + "=" * 52)
