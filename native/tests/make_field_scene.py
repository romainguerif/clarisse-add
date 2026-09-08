# -*- coding: utf-8 -*-
"""Des cables en masse entre deux nuages de points.

Deux plans se faisant face, un nuage disperse sur chacun, et le node tend un
cable par paire. C'est l'echelle a laquelle poser des locators n'a plus de sens.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\field_scene.project"


def plane(name, x, count):
    grid = ix.cmds.CreateObject(name, "GeometryPolygrid", "Global", "project:/")
    ix.cmds.SetValues([str(grid) + ".translate"], [str(x), "0.0", "0.0"])
    ix.cmds.SetValues([str(grid) + ".rotate"], ["0.0", "0.0", "90.0"])
    ix.cmds.SetValues([str(grid) + ".size[0]", str(grid) + ".size[1]"], ["4", "4"])

    cloud = ix.cmds.CreateObject(name + "_points", "GeometryPointCloud",
                                 "Global", "project:/")
    ix.cmds.SetValues([str(cloud) + ".geometry"], [str(grid)])
    ix.cmds.SetValues([str(cloud) + ".point_count"], [str(count)])
    ix.cmds.SetValues([str(cloud) + ".distribution"], ["3"])   # blue noise
    print("%-14s : %d points" % (name, cloud.get_module().get_point_count()))
    return cloud


left = plane("depart", -4.0, 120)
right = plane("arrivee", 4.0, 120)

field = ix.cmds.CreateObject("faisceau", "GeometryCableField", "Global", "project:/")
ix.cmds.SetValues([str(field) + ".points_start"], [str(left)])
ix.cmds.SetValues([str(field) + ".points_end"], [str(right)])
ix.cmds.SetValues([str(field) + ".pairing"], ["2"])          # au hasard
ix.cmds.SetValues([str(field) + ".radius"], ["0.022"])
ix.cmds.SetValues([str(field) + ".radius_variation"], ["0.5"])
ix.cmds.SetValues([str(field) + ".sides"], ["6"])
ix.cmds.SetValues([str(field) + ".steps"], ["26"])
ix.cmds.SetValues([str(field) + ".slack"], ["0.12"])
ix.cmds.SetValues([str(field) + ".slack_variation"], ["0.7"])
ix.cmds.SetValues([str(field) + ".noise_amplitude"], ["0.18"])
ix.cmds.SetValues([str(field) + ".noise_frequency"], ["0.35"])
ix.cmds.SetValues([str(field) + ".noise_fade"], ["0.5"])

mesh = field.get_module().get_geometry()
print("champ          : %d sommets, %d polygones"
      % (mesh.get_vertex_count(), mesh.get_polygon_count()))

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.35", "0.3", "0.28"])
field.get_module().get_geometry()
ix.cmds.SetValues([str(field) + ".materials[0]"], [str(material)])

# Les plans ne servent qu'a porter les nuages : on les cache au rendu.
for name in ("depart", "arrivee"):
    item = ix.get_item("project:/" + name)
    if item is not None and item.get_attribute("unseen_by_camera") is not None:
        ix.cmds.SetValues([str(item) + ".unseen_by_camera"], ["1"])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-40.0", "35.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.5"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["0.0", "0.6", "9.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-6.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["720", "440"])
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
