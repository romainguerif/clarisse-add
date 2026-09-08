# -*- coding: utf-8 -*-
"""Le rayon de coude par point : trois fois la meme equerre, trois reglages.

Le compte de sommets ne dit rien ici -- le nombre de points d'arc est fixe,
seul leur placement change -- et la boite englobante non plus, puisque ce sont
les extremites qui la dictent. C'est donc une image qui tranche.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\perbend_scene.project"

SHAPE = [(0.0, 0.0), (4.0, 0.0), (4.0, 3.0), (8.0, 3.0)]

# (rayon global, override sur le coin 1, decalage en Z, etiquette)
VARIANTS = [(0.2, None, 0.0, "global 0.2"),
            (1.4, None, -3.0, "global 1.4"),
            (0.2, 1.4, -6.0, "0.2 sauf coin 1 a 1.4")]


def make(index, bend_global, bend_corner, z, label):
    tube = ix.cmds.CreateObject("tube_%d" % index, "GeometryTube",
                                "Global", "project:/")
    for i, (x, y) in enumerate(SHAPE):
        loc = ix.cmds.CreateObject("cp_%d_%d" % (index, i), "Locator",
                                   "Global", "project:/")
        ix.cmds.SetValues([str(loc) + ".translate"], [str(x), str(y), str(z)])
        ix.cmds.AddTableRow(str(tube) + ".control_points")
        ix.cmds.SetValues([str(tube) + ".point[%d]" % i], [str(loc)])

    ix.cmds.SetValues([str(tube) + ".interpolation"], ["1"])
    ix.cmds.SetValues([str(tube) + ".bend_radius"], [str(bend_global)])
    ix.cmds.SetValues([str(tube) + ".radius"], ["0.18"])
    ix.cmds.SetValues([str(tube) + ".sides"], ["20"])
    ix.cmds.SetValues([str(tube) + ".steps"], ["12"])
    if bend_corner is not None:
        ix.cmds.SetValues([str(tube) + ".bend[1]"], [str(bend_corner)])

    mesh = tube.get_module().get_geometry()
    print("%-24s : %d sommets" % (label, mesh.get_vertex_count()))
    return tube


tubes = [make(i, g, c, z, label)
         for i, (g, c, z, label) in enumerate(VARIANTS)]

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.5", "0.55", "0.6"])
for tube in tubes:
    tube.get_module().get_geometry()
    ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-60.0", "20.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.4"])

# Vue de dessus : l'equerre est plane, c'est de face qu'on lit le coude.
camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["4.0", "1.5", "16.0"])
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
