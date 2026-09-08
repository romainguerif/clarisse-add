# -*- coding: utf-8 -*-
"""Le node de selection fait-il ce qu'il dit ?

On compte les faces qui atterrissent dans le shading group produit. C'est une
verification par les nombres, pas par le rendu : chaque critere a un resultat
attendu qu'on peut ecrire a l'avance, et un critere qui se trompe se voit tout
de suite.
"""
from __future__ import print_function

print("")
print("DEBUT" + "=" * 60)

classes = ix.application.get_factory().get_classes()
print("classe GeometrySelect declaree : %s"
      % ("OUI" if classes.exists("GeometrySelect") else "NON"))

grid = ix.cmds.CreateObject("grille", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(grid) + ".size[0]", str(grid) + ".size[1]"], ["8", "8"])
ix.cmds.SetValues([str(grid) + ".spans[0]", str(grid) + ".spans[1]"], ["8", "8"])

sphere = ix.cmds.CreateObject("sphere", "GeometryPolysphere", "Global", "project:/")

counter = [0]


def group_counts(node):
    """Nombre de faces par shading group du maillage produit."""
    mesh = node.get_module().get_geometry()
    if mesh is None:
        return None, None
    names = mesh.get_shading_group_names()
    groups = ix.api.UIntArray()
    mesh.get_polygon_shading_groups(groups)
    counts = {}
    for i in range(groups.get_count()):
        counts[groups[i]] = counts.get(groups[i], 0) + 1
    labels = [names[i] for i in range(names.get_count())]
    return labels, counts


def select(label, source, expected=None, **values):
    counter[0] += 1
    node = ix.cmds.CreateObject("sel%d" % counter[0], "GeometrySelect",
                                "Global", "project:/")
    ix.cmds.SetValues([str(node) + ".input_geometry"], [str(source)])
    for key, value in values.items():
        if node.get_attribute(key) is None:
            print("  !! pas d'attribut %s" % key)
            continue
        if isinstance(value, (list, tuple)):
            ix.cmds.SetValues([str(node) + "." + key + "[%d]" % i
                               for i in range(len(value))],
                              [str(v) for v in value])
        else:
            ix.cmds.SetValues([str(node) + "." + key], [str(value)])

    labels, counts = group_counts(node)
    if labels is None:
        print("%-34s : RIEN" % label)
        return node

    name = values.get("group_name", "selection")
    index = labels.index(name) if name in labels else -1
    got = counts.get(index, 0) if index >= 0 else 0
    verdict = ""
    if expected is not None:
        verdict = "  attendu %s  %s" % (expected,
                                        "ok" if got == expected else "ECHEC")
    print("%-34s : %4d faces sur %d%s"
          % (label, got, sum(counts.values()), verdict))
    return node


# --- les criteres, un par un -------------------------------------------------
# Sans critere ni texture, tout passe : c'est la valeur par defaut la plus
# previsible, et elle sert de temoin.
select("aucun critere", grid, expected=64)

select("moitie au hasard", grid, random_ratio=0.5)
select("un quart au hasard", grid, random_ratio=0.25)

# Sur une grille horizontale, toutes les faces regardent en haut : le critere de
# normale doit tout prendre vers le haut, et rien vers le bas.
select("normale vers le haut", grid, expected=64,
       use_normal=1, direction=[0, 1, 0], angle=30)
select("normale vers le bas", grid, expected=0,
       use_normal=1, direction=[0, -1, 0], angle=30)

# Sur une sphere, une demi-sphere environ regarde vers le haut a 90 degres.
select("sphere, hemisphere haut", sphere,
       use_normal=1, direction=[0, 1, 0], angle=90)

# --- la composition ----------------------------------------------------------
first = select("base : un quart", grid, group_name="base", random_ratio=0.25,
               seed=1)
select("base, elargie d'un anneau", first, group_name="base",
       source_group="base", mode=1, random_ratio=0.0, grow=1)
select("base, moins tout", first, group_name="base", expected=0,
       source_group="base", mode=3)

print("FIN" + "=" * 62)
