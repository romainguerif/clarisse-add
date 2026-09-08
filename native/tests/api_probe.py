# -*- coding: utf-8 -*-
"""Quels noms l'API Python de Clarisse expose-t-elle pour lire les groupes ?"""
from __future__ import print_function

print("")
print("DEBUT" + "=" * 40)

grid = ix.cmds.CreateObject("grille", "GeometryPolygrid", "Global", "project:/")
mesh = grid.get_module().get_geometry()
print("type du maillage : %s" % type(mesh).__name__)

names = [n for n in dir(mesh) if "shading" in n or "polygon" in n]
print("accesseurs : %s" % ", ".join(sorted(names)))

arrays = [n for n in dir(ix.api) if "Array" in n and
          ("Uns" in n or "Int" in n or "Long" in n)]
print("tableaux : %s" % ", ".join(sorted(arrays)[:20]))

print("FIN" + "=" * 42)
