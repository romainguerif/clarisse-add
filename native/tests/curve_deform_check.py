# -*- coding: utf-8 -*-
"""Le deformeur de courbe plie-t-il vraiment une geometrie ?

Une grille allongee sur X, une courbe en L, et on compare : boite englobante
non deformee contre boite englobante deformee, puis quelques sommets lus un a
un. On fait ensuite varier chaque reglage pour verifier que la geometrie se
reconstruit -- c'est le point que la documentation du SDK ne tranche pas.

Deformer est `embedded_only` : la node ne se cree pas dans un contexte, elle
se cree depuis l'attribut `deformers` de la geometrie.

Lance par cnode via -script.
"""
import math

CLASS = "DeformerCurve"
REPORT = r"J:\_WINDOWSTEMP\claude\curve_deform_check.txt"

# cnode entrelace sa barre de progression avec stdout et recoupe les lignes :
# le compte-rendu part donc aussi dans un fichier, seul endroit ou il reste
# lisible.
lines = []


def say(text):
    lines.append(text)
    print(text)


say("")
say("=" * 70)

classes = ix.application.get_factory().get_classes()
found = classes.exists(CLASS)
say("classe '%s' declaree : %s" % (CLASS, "OUI" if found else "NON"))

if found:
    cls = classes.get(CLASS)
    say("declaree par           : %s" % cls.get_dso_filename())
    say("classe de base         : %s" % cls.get_base_name())


def bbox_of(module, deformed):
    """La boite englobante, deformee ou non."""
    try:
        box = module.get_bbox(deformed)
    except Exception as error:
        return "illisible (%s)" % error
    lo = box.get_min()
    hi = box.get_max()
    return ("[%7.3f %7.3f %7.3f] -> [%7.3f %7.3f %7.3f]"
            % (lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))


def raw(module, deformed, indices):
    """Les positions brutes de quelques sommets."""
    cloud = module.get_geometry(deformed).get_point_cloud()
    out = []
    for i in indices:
        p = cloud.get_position(i)
        out.append((p[0], p[1], p[2]))
    return out


def points(module, deformed, indices):
    """Quelques positions de sommets, lues sur le nuage de points."""
    try:
        got = raw(module, deformed, indices)
    except Exception as error:
        return "nuage illisible (%s)" % error
    return "  ".join("%d:(%6.2f %6.2f %6.2f)" % (i, p[0], p[1], p[2])
                     for i, p in zip(indices, got))


def spacing(module, indices):
    """L'ecart entre sommets consecutifs d'une rangee, une fois deformee.

    C'est la mesure qui dit si la reparametrisation par longueur d'arc tient :
    des sommets regulierement espaces sur l'axe doivent le rester sur la
    courbe. Une abscisse uniforme sur une Catmull-Rom les tasserait dans le
    virage, et le rapport max/min le dirait tout de suite.
    """
    try:
        got = raw(module, True, indices)
    except Exception as error:
        return "illisible (%s)" % error
    steps = []
    for i in range(len(got) - 1):
        a, b = got[i], got[i + 1]
        steps.append(math.sqrt((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2
                               + (b[2] - a[2]) ** 2))
    total = sum(steps)
    return ("longueur %.4f, pas min %.4f max %.4f (rapport %.3f)"
            % (total, min(steps), max(steps), max(steps) / min(steps)))


deformer = None

if found:
    # --- la courbe : un L dans le plan XY, pour que le pli se voie -------
    positions = [(0.0, 0.0, 0.0), (3.0, 0.0, 0.0), (5.0, 2.0, 0.0), (5.0, 5.0, 0.0)]
    locators = []
    for i, p in enumerate(positions):
        loc = ix.cmds.CreateObject("cp%d" % i, "Locator", "Global", "project:/")
        ix.cmds.SetValues([str(loc) + ".translate"],
                          [str(p[0]), str(p[1]), str(p[2])])
        locators.append(loc)
    say("locators               : %d, le premier est %s"
        % (len(locators), str(locators[0])))

    # --- la geometrie : une grille allongee sur X ------------------------
    grid = ix.cmds.CreateObject("grid", "GeometryPolygrid", "Global", "project:/")
    ix.cmds.SetValues([str(grid) + ".size[0]", str(grid) + ".size[1]"], ["8.0", "1.0"])
    ix.cmds.SetValues([str(grid) + ".spans[0]", str(grid) + ".spans[1]"], ["32", "2"])
    geo_module = grid.get_module()
    say("grille                 : %s" % str(grid))
    say("sommets                : %d"
        % geo_module.get_geometry(False).get_vertex_count())
    say("bbox non deformee      : %s" % bbox_of(geo_module, False))

    # --- le deformeur, cree depuis l'attribut deformers ------------------
    # Deformer est embedded_only : pas de CreateObject dans un contexte, il
    # nait dans l'attribut de la geometrie.
    created = ix.cmds.AddValues([str(grid) + ".deformers"], [CLASS])
    say("AddValues rend         : %r" % (created,))

    slot = grid.get_attribute("deformers")
    say("deformers branches     : %d" % slot.get_value_count())
    deformer = slot.get_object(0) if slot.get_value_count() > 0 else None
    say("objet deformeur        : %s" % (str(deformer) if deformer else "AUCUN"))

if deformer is not None:
    say("module C++ attache     : %s"
        % ("OUI" if deformer.get_module() is not None else "NON"))
    names = ["control_points", "closed", "steps", "axis", "stretch",
             "weight", "local_deformation", "parent_matrix"]
    have = [n for n in names if deformer.get_attribute(n) is not None]
    say("attributs presents     : %s" % ", ".join(have))

    # Clarisse remplit-il parent_matrix, ou est-ce a nous de prendre la
    # matrice globale sur le module de la geometrie ?
    pm = deformer.get_attribute("parent_matrix")
    if pm is not None:
        row = [pm.get_double(k) for k in range(16)]
        say("parent_matrix          : %s ..." % " ".join("%.1f" % v for v in row[:8]))

    ix.cmds.AddValues([str(deformer) + ".control_points"],
                      [str(loc) for loc in locators])
    attr = deformer.get_attribute("control_points")
    if attr.get_value_count() == 0:
        attr.set_value_count(len(locators))
        for i, loc in enumerate(locators):
            attr.set_object(loc, i)
        say("branche par set_object : oui")
    say("points de controle     : %d" % attr.get_value_count())

    say("-" * 70)
    # La grille fait 33 x 3 sommets : la rangee 0 court sur X a z = +0.5.
    row0 = list(range(33))
    sample = [0, 16, 32, 33, 49, 65]
    say("bbox non deformee      : %s" % bbox_of(geo_module, False))
    say("bbox deformee          : %s" % bbox_of(geo_module, True))
    say("sommets au repos       : %s" % points(geo_module, False, sample))
    say("sommets deformes       : %s" % points(geo_module, True, sample))
    say("rangee z=+0.5 deformee : %s" % spacing(geo_module, row0))
    try:
        say("is_deformed            : %s" % geo_module.is_deformed())
    except Exception as error:
        say("is_deformed            : illisible (%s)" % error)

    # --- chaque reglage doit faire reconstruire --------------------------
    say("-" * 70)

    def probe(label):
        say("%-22s : %s" % (label, bbox_of(geo_module, True)))

    ix.cmds.SetValues([str(deformer) + ".stretch"], ["0"])
    probe("stretch off")
    say("%-22s : %s" % ("  sa rangee 0", spacing(geo_module, row0)))
    ix.cmds.SetValues([str(deformer) + ".stretch"], ["1"])
    probe("stretch on")

    ix.cmds.SetValues([str(deformer) + ".weight"], ["0.5"])
    probe("weight 50%")
    ix.cmds.SetValues([str(deformer) + ".weight"], ["0.0"])
    probe("weight 0% (= repos)")
    ix.cmds.SetValues([str(deformer) + ".weight"], ["1.0"])
    probe("weight 100%")

    ix.cmds.SetValues([str(deformer) + ".axis"], ["2"])
    probe("axis Z")
    ix.cmds.SetValues([str(deformer) + ".axis"], ["0"])
    probe("axis X")

    ix.cmds.SetValues([str(deformer) + ".steps"], ["24"])
    probe("steps 24")

    ix.cmds.SetValues([str(deformer) + ".closed"], ["1"])
    probe("closed")
    ix.cmds.SetValues([str(deformer) + ".closed"], ["0"])
    probe("ouverte")

    # Le point qui a coute cher sur le tube : un locator qui bouge doit
    # remonter jusqu'a la geometrie deformee.
    ix.cmds.SetValues([str(locators[3]) + ".translate"], ["5.0", "9.0", "0.0"])
    probe("cp3 monte a y=9")
    ix.cmds.SetValues([str(locators[3]) + ".translate"], ["5.0", "5.0", "0.0"])
    probe("cp3 revient a y=5")

    # local_deformation decide de l'espace ou la courbe est lue. Il faut
    # deplacer la grille SUR l'axe suivi pour que les deux modes different :
    # un decalage le long de la binormale se compense exactement au retour, et
    # ne prouverait rien.
    say("-" * 70)
    ix.cmds.SetValues([str(grid) + ".translate"], ["2.0", "0.0", "0.0"])
    probe("grille en x=2, local")
    say("  ses sommets          : %s" % points(geo_module, True, sample))
    ix.cmds.SetValues([str(deformer) + ".local_deformation"], ["0"])
    probe("grille en x=2, global")
    say("  ses sommets          : %s" % points(geo_module, True, sample))
    ix.cmds.SetValues([str(deformer) + ".local_deformation"], ["1"])
    ix.cmds.SetValues([str(grid) + ".translate"], ["0.0", "0.0", "0.0"])

if deformer is not None:
    # --- les entrees degenerees -----------------------------------------
    # Zero puis un seul point de controle : pre_deform doit rendre false et la
    # geometrie sortir intacte, plutot qu'un tas de sommets a l'origine.
    say("-" * 70)
    bare = ix.cmds.CreateObject("bare", "GeometryPolygrid", "Global", "project:/")
    ix.cmds.SetValues([str(bare) + ".size[0]", str(bare) + ".size[1]"], ["4.0", "2.0"])
    ix.cmds.AddValues([str(bare) + ".deformers"], [CLASS])
    bare_deformer = bare.get_attribute("deformers").get_object(0)
    bare_module = bare.get_module()
    say("sans point de controle : %s" % bbox_of(bare_module, True))
    ix.cmds.AddValues([str(bare_deformer) + ".control_points"], [str(locators[0])])
    say("un seul point          : %s" % bbox_of(bare_module, True))
    ix.cmds.AddValues([str(bare_deformer) + ".control_points"], [str(locators[1])])
    say("deux points (droite)   : %s" % bbox_of(bare_module, True))
    say("  au repos             : %s" % bbox_of(bare_module, False))

    # --- la plage parallele ---------------------------------------------
    # 99 sommets tiennent dans une seule plage : le premier essai ne prouvait
    # donc rien du parallelisme. Avec 160 801 sommets, cb_deform est appele
    # sur plusieurs plages disjointes. Une plage oubliee laisserait ses
    # sommets au repos, et la bbox le dirait tout de suite : elle repartirait
    # de x = -4 au lieu de x = 0.
    say("-" * 70)
    big = ix.cmds.CreateObject("big", "GeometryPolygrid", "Global", "project:/")
    ix.cmds.SetValues([str(big) + ".size[0]", str(big) + ".size[1]"], ["8.0", "8.0"])
    ix.cmds.SetValues([str(big) + ".spans[0]", str(big) + ".spans[1]"], ["400", "400"])
    ix.cmds.AddValues([str(big) + ".deformers"], [CLASS])
    big_deformer = big.get_attribute("deformers").get_object(0)
    ix.cmds.AddValues([str(big_deformer) + ".control_points"],
                      [str(loc) for loc in locators])
    big_module = big.get_module()
    say("grande grille          : %d sommets"
        % big_module.get_geometry(False).get_vertex_count())
    say("  bbox au repos        : %s" % bbox_of(big_module, False))
    say("  bbox deformee        : %s" % bbox_of(big_module, True))

    # Et un balayage complet, pour ne pas dependre du seul agregat.
    count = big_module.get_geometry(True).get_vertex_count()
    cloud = big_module.get_geometry(True).get_point_cloud()
    stalled = 0
    for i in range(0, count, 37):
        if cloud.get_position(i)[0] < -0.001:
            stalled += 1
    say("  sommets restes au repos (1 sur 37) : %d" % stalled)

say("=" * 70)

try:
    handle = open(REPORT, "w")
    handle.write("\n".join(lines) + "\n")
    handle.close()
    print("compte-rendu ecrit : %s" % REPORT)
except Exception as error:
    print("compte-rendu non ecrit : %s" % error)


