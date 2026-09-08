# -*- coding: utf-8 -*-
"""Le mode Coudes : la meme suite de points, en lisse puis en tuyauterie.

Un tuyau industriel n'est pas une spline : c'est une suite de segments droits
raccordes par des coudes de rayon constant. Le test met les deux cote a cote sur
les memes points de controle, avec deux rayons de coude, pour verifier que
l'arc est bien tangent aux deux branches et que le rayon se reduit tout seul
quand le segment est trop court pour le contenir.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\bend_scene.project"

# Un parcours anguleux, avec un segment volontairement court entre les deux
# derniers coins : c'est lui qui teste le bornage du rayon.
SHAPE = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (4.0, 2.5, 0.0),
         (7.0, 2.5, 0.0), (7.5, 2.5, -2.0), (10.0, 0.5, -2.0)]

VARIANTS = [(0, 0.0, 0.0, "lisse"),
            (1, 0.8, -3.0, "coudes r=0.8"),
            (1, 0.25, -6.0, "coudes r=0.25")]


def make(index, interpolation, bend, z, label):
    points = []
    for i, (x, y, dz) in enumerate(SHAPE):
        loc = ix.cmds.CreateObject("cp_%d_%d" % (index, i), "Locator",
                                   "Global", "project:/")
        ix.cmds.SetValues([str(loc) + ".translate"],
                          [str(x), str(y), str(dz + z)])
        points.append(loc)

    tube = ix.cmds.CreateObject("tube_%d" % index, "GeometryTube",
                                "Global", "project:/")
    attr = tube.get_attribute("control_points")
    attr.set_value_count(len(points))
    for i, loc in enumerate(points):
        attr.set_object(loc, i)

    ix.cmds.SetValues([str(tube) + ".radius"], ["0.22"])
    ix.cmds.SetValues([str(tube) + ".sides"], ["20"])
    ix.cmds.SetValues([str(tube) + ".steps"], ["10"])
    ix.cmds.SetValues([str(tube) + ".interpolation"], [str(interpolation)])
    ix.cmds.SetValues([str(tube) + ".bend_radius"], [str(bend)])

    mesh = tube.get_module().get_geometry()
    print("%-14s : %d sommets, %d polygones"
          % (label, mesh.get_vertex_count(), mesh.get_polygon_count()))
    return tube


tubes = [make(i, mode, bend, z, label)
         for i, (mode, bend, z, label) in enumerate(VARIANTS)]

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.45", "0.5", "0.55"])
for tube in tubes:
    ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-55.0", "35.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.4"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["5.0", "9.0", "11.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-40.0", "0.0", "0.0"])

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
