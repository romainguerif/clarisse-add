# -*- coding: utf-8 -*-
"""Le mode cable : quatre portees, quatre mous, plus un cas denivele.

Ce qu'on verifie a l'oeil : que la courbe pend, que le mou se lit comme une
fraction et non comme une longueur absolue, et surtout que sur une portee dont
les deux appuis ne sont pas a la meme hauteur, le point bas se decale vers
l'appui le plus bas au lieu de rester au milieu. C'est ce dernier point qui
distingue une vraie chainette d'une parabole posee entre deux clous.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\catenary_scene.project"

# (mou, hauteur du second appui, decalage Z, etiquette)
VARIANTS = [(0.0, 4.0, 0.0, "tendu"),
            (0.02, 4.0, -2.0, "mou 2%"),
            (0.10, 4.0, -4.0, "mou 10%"),
            (0.30, 4.0, -6.0, "mou 30%"),
            (0.10, 1.5, -8.0, "mou 10%, appuis inegaux")]


def make(index, slack, right_height, z, label):
    tube = ix.cmds.CreateObject("cable_%d" % index, "GeometryTube",
                                "Global", "project:/")
    for i, (x, y) in enumerate(((0.0, 4.0), (10.0, right_height))):
        loc = ix.cmds.CreateObject("cp_%d_%d" % (index, i), "Locator",
                                   "Global", "project:/")
        ix.cmds.SetValues([str(loc) + ".translate"], [str(x), str(y), str(z)])
        ix.cmds.AddTableRow(str(tube) + ".control_points")
        ix.cmds.SetValues([str(tube) + ".point[%d]" % i], [str(loc)])

    ix.cmds.SetValues([str(tube) + ".interpolation"], ["2"])
    ix.cmds.SetValues([str(tube) + ".slack"], [str(slack)])
    ix.cmds.SetValues([str(tube) + ".steps"], ["40"])
    ix.cmds.SetValues([str(tube) + ".radius"], ["0.06"])
    ix.cmds.SetValues([str(tube) + ".sides"], ["10"])

    box = tube.get_module().get_bbox()
    mesh = tube.get_module().get_geometry()
    print("%-26s : %d sommets, point bas y=%.3f"
          % (label, mesh.get_vertex_count(), box.get_min()[1]))
    return tube


tubes = [make(i, s, h, z, label)
         for i, (s, h, z, label) in enumerate(VARIANTS)]

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.2", "0.2", "0.22"])
for tube in tubes:
    tube.get_module().get_geometry()
    ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-40.0", "20.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.5"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["5.0", "2.5", "16.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["0.0", "0.0", "0.0"])

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
