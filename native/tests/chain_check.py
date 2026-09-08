# -*- coding: utf-8 -*-
"""L'enchainement maillage -> selection -> panneau de tissu tient-il ?

C'est la question que le rendu ne repond pas vite : on veut savoir si le
panneau se limite bien aux faces choisies, et le compte de polygones le dit
sans ambiguite. Une grille de huit sur huit fait soixante-quatre faces ; si on
n'en selectionne que quatre, le panneau doit peser seize fois moins.
"""
from __future__ import print_function

print("")
print("DEBUT" + "=" * 50)

grid = ix.cmds.CreateObject("grille", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(grid) + ".size[0]", str(grid) + ".size[1]"], ["8", "8"])
ix.cmds.SetValues([str(grid) + ".spans[0]", str(grid) + ".spans[1]"], ["8", "8"])


def polygons(node):
    mesh = node.get_module().get_geometry()
    return mesh.get_polygon_count() if mesh is not None else -1


def cloth(name, source, **values):
    node = ix.cmds.CreateObject(name, "GeometryClothPanel", "Global", "project:/")
    ix.cmds.SetValues([str(node) + ".input_geometry"], [str(source)])
    # Peu de subdivisions : on compte des faces, on ne juge pas une forme.
    base = dict(resolution=4, levels=1, substeps=4, iterations=4)
    base.update(values)
    for key, value in base.items():
        if node.get_attribute(key) is None:
            print("  !! pas d'attribut %s" % key)
            continue
        ix.cmds.SetValues([str(node) + "." + key], [str(value)])
    return node


# Reference : tout le maillage.
full = cloth("tout", grid)
total = polygons(full)
print("panneau sur tout le maillage      : %d polygones" % total)

# Quatre faces designees a la main, comme le ferait l'outil de viewport.
select = ix.cmds.CreateObject("selection", "GeometrySelect", "Global", "project:/")
ix.cmds.SetValues([str(select) + ".input_geometry"], [str(grid)])
ix.cmds.SetValues([str(select) + ".list_mode"], ["1"])
faces = select.get_attribute("index")
faces.set_value_count(4)
for row, face in enumerate((0, 1, 8, 9)):
    faces.set_long(face, row)
print("selection                         : %d faces sur 64"
      % faces.get_value_count())

# Le point du jour : brancher la selection doit suffire, sans retaper son nom.
partial = cloth("partiel", select)
got = polygons(partial)
expected = total // 16
print("panneau sur la selection          : %d polygones, attendu %d  %s"
      % (got, expected, "ok" if got == expected else "ECHEC"))

# Et l'echappatoire doit rendre tout le maillage malgre la selection en amont.
override = cloth("etoile", select, shading_group="*")
got = polygons(override)
print("panneau avec etoile               : %d polygones, attendu %d  %s"
      % (got, total, "ok" if got == total else "ECHEC"))

# Un nom faux ne doit rien produire, pour que la faute de frappe se voie.
wrong = cloth("faute", select, shading_group="selectoin")
got = polygons(wrong)
print("panneau avec un nom faux          : %s  %s"
      % ("rien" if got <= 0 else "%d polygones" % got,
         "ok" if got <= 0 else "ECHEC"))

print("FIN" + "=" * 52)
