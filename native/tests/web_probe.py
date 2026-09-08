# -*- coding: utf-8 -*-
"""Que possede deja Clarisse pour poser et disperser des points ?

Trois questions, avant d'ecrire quoi que ce soit pour la toile d'araignee :
ou peut-on accrocher des fils, comment disperse-t-on des points, et existe-t-il
une texture de courbure -- c'est elle qui saurait trouver toute seule les aretes
saillantes, la ou une araignee accroche vraiment.
"""
from __future__ import print_function

print("")
print("DEBUT" + "=" * 50)

classes = ix.application.get_factory().get_classes()

WANTED = [
    ("TextureCurvature", "trouver les aretes saillantes"),
    ("TextureOcclusion", "trouver les recoins abrites"),
    ("TextureDistanceToObject", "s'eloigner d'un objet"),
    ("SceneObjectScatterer", "disperser des instances"),
    ("GeometryPointCloud", "un nuage de points brut"),
    ("GeometryPointArray", "des points explicites"),
    ("GeometryScatterer", "disperser de la geometrie"),
    ("GeometryFur", "des poils sur une surface"),
    ("GeometryCurve", "des courbes natives"),
    ("GeometryBundle", "un paquet de courbes"),
]
for name, why in WANTED:
    print("%-28s %-34s %s"
          % (name, why, "OUI" if classes.exists(name) else "non"))

print("")
print("--- ce qui ressemble a un nuage, un scatter ou une courbe ---")
found = []
for i in range(classes.get_count()):
    name = classes[i].get_name()
    low = name.lower()
    if ("scatter" in low or "pointcloud" in low or "curve" in low
            or "fur" in low or "hair" in low or "strand" in low):
        found.append(name)
for name in sorted(found):
    print("  " + name)

print("")
print("--- les textures qui sondent la geometrie ---")
probes = []
for i in range(classes.get_count()):
    name = classes[i].get_name()
    low = name.lower()
    if name.startswith("Texture") and (
            "curv" in low or "occl" in low or "dist" in low or "normal" in low
            or "position" in low or "ray" in low or "geom" in low):
        probes.append(name)
for name in sorted(probes):
    print("  " + name)

print("FIN" + "=" * 52)
