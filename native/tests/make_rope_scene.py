# -*- coding: utf-8 -*-
"""Trois tubes cote a cote : simple, corde a trois torons, cable a sept.

Un seul rendu pour trancher les trois cas -- le mode toron est la seule chose
qu'on ne peut pas verifier par des comptes de sommets, puisqu'il ne change que
la position des anneaux, pas leur nombre.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\rope_scene.project"

# La meme courbe en S, decalee en Z pour chaque variante.
SHAPE = [(0.0, 0.0), (2.0, 1.2), (4.0, 0.0), (6.0, 1.2)]
VARIANTS = [(1, 0.0, 0.0), (3, 0.5, -2.5), (7, 0.8, -5.0)]


def make_tube(index, strands, twist, z):
    points = []
    for i, (x, y) in enumerate(SHAPE):
        name = "cp_%d_%d" % (index, i)
        loc = ix.cmds.CreateObject(name, "Locator", "Global", "project:/")
        ix.cmds.SetValues([str(loc) + ".translate"], [str(x), str(y), str(z)])
        points.append(loc)

    tube = ix.cmds.CreateObject("tube_%d" % index, "GeometryTube", "Global", "project:/")
    attr = tube.get_attribute("control_points")
    attr.set_value_count(len(points))
    for i, loc in enumerate(points):
        attr.set_object(loc, i)

    ix.cmds.SetValues([str(tube) + ".radius"], ["0.4"])
    ix.cmds.SetValues([str(tube) + ".sides"], ["20"])
    ix.cmds.SetValues([str(tube) + ".steps"], ["24"])
    ix.cmds.SetValues([str(tube) + ".strands"], [str(strands)])
    ix.cmds.SetValues([str(tube) + ".twist"], [str(twist)])
    mesh = tube.get_module().get_geometry()
    print("torons=%d twist=%.1f : %d sommets, %d polygones"
          % (strands, twist, mesh.get_vertex_count(), mesh.get_polygon_count()))
    return tube


tubes = [make_tube(i, s, t, z) for i, (s, t, z) in enumerate(VARIANTS)]

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.62", "0.5", "0.38"])
for tube in tubes:
    ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

ground = ix.cmds.CreateObject("ground", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(ground) + ".translate"], ["3.0", "-1.2", "-2.5"])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-50.0", "30.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.3"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["3.0", "5.5", "9.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-30.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["640", "400"])
ix.cmds.SetValues([str(image) + ".resolution_multiplier"], ["2"])

ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
layers = image.get_attribute("layers")
layer = layers.get_object(0) if layers.get_value_count() > 0 else None
if layer is not None:
    ix.cmds.SetValues([str(layer) + ".active_camera"], [str(camera)])
    ix.cmds.SetValues([str(layer) + ".renderer"], [str(renderer)])

if not os.path.isdir(os.path.dirname(OUT)):
    os.makedirs(os.path.dirname(OUT))
ix.application.save_project(OUT)
print("scene ecrite : %s" % OUT)
