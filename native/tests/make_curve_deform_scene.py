# -*- coding: utf-8 -*-
"""Monte une scene minimale autour d'un DeformerCurve et la sauve.

Les comptes de sommets et la boite englobante disent que la deformation a eu
lieu et qu'elle tombe au bon endroit ; ils ne disent rien de ce que le moteur
en fait au moment du rendu -- ni des normales recalculees sur le maillage plie,
ni de la tesselation. D'ou cette scene, rendue ensuite par
`cnode -image build://project/image -frames_list 1`.

Le sujet est une planche allongee sur X, tordue le long d'une courbe qui monte
en spirale : elle a des virages dans les trois dimensions, donc le repere a
torsion minimale y travaille vraiment.
"""
import math
import os

OUT = r"J:\_WINDOWSTEMP\claude\curve_deform_scene.project"

# Une helice d'un tour et demi : virages dans les trois dimensions.
positions = []
for i in range(7):
    a = 1.6 * math.pi * i / 6.0
    positions.append((2.2 * math.cos(a) - 2.2, 0.55 * i, 2.2 * math.sin(a)))

locators = []
for i, p in enumerate(positions):
    loc = ix.cmds.CreateObject("cp%d" % i, "Locator", "Global", "project:/")
    ix.cmds.SetValues([str(loc) + ".translate"],
                      [str(p[0]), str(p[1]), str(p[2])])
    locators.append(loc)

# La planche : longue sur X, large sur Z, assez dense pour que le pli soit
# lisse plutot que facette.
board = ix.cmds.CreateObject("board", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(board) + ".size[0]", str(board) + ".size[1]"], ["10.0", "1.2"])
ix.cmds.SetValues([str(board) + ".spans[0]", str(board) + ".spans[1]"], ["240", "8"])

# Deformer est embedded_only : il nait dans l'attribut, jamais dans le
# contexte. AddValues rend None -- l'objet cree se relit sur l'attribut, et il
# s'appelle "<geometrie>.curve".
ix.cmds.AddValues([str(board) + ".deformers"], ["DeformerCurve"])
deformer = board.get_attribute("deformers").get_object(0)
ix.cmds.AddValues([str(deformer) + ".control_points"],
                  [str(loc) for loc in locators])
ix.cmds.SetValues([str(deformer) + ".steps"], ["16"])

module = board.get_module()
print("planche  : %d sommets, %d points de controle"
      % (module.get_geometry(False).get_vertex_count(),
         deformer.get_attribute("control_points").get_value_count()))
for label, deformed in (("au repos", False), ("deformee", True)):
    box = module.get_bbox(deformed)
    lo, hi = box.get_min(), box.get_max()
    print("bbox %-8s : [%.2f %.2f %.2f] -> [%.2f %.2f %.2f]"
          % (label, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))

ground = ix.cmds.CreateObject("ground", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(ground) + ".translate"], ["0.0", "-1.5", "0.0"])
ix.cmds.SetValues([str(ground) + ".size[0]", str(ground) + ".size[1]"],
                  ["40.0", "40.0"])

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.55", "0.45", "0.4"])
for item in (board, ground):
    ix.cmds.SetValues([str(item) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-45.0", "35.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.35"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["-2.2", "3.2", "7.5"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-10.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["480", "320"])

# AddLayer, pas CreateObject : un layer est embarque dans l'image. La commande
# rend None meme quand elle reussit, la seule preuve est de relire l'attribut.
ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
layers = image.get_attribute("layers")
layer = layers.get_object(0) if layers.get_value_count() > 0 else None
print("layer    : %s" % layer)
if layer is not None:
    ix.cmds.SetValues([str(layer) + ".active_camera"], [str(camera)])
    ix.cmds.SetValues([str(layer) + ".renderer"], [str(renderer)])

if not os.path.isdir(os.path.dirname(OUT)):
    os.makedirs(os.path.dirname(OUT))
ix.application.save_project(OUT)
print("scene ecrite : %s" % OUT)
