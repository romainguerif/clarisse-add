# -*- coding: utf-8 -*-
"""La scene qui repond a la question de la sonde CSG.

Trois nodes cote a cote -- union, intersection, difference -- et rien d'autre
qu'un eclairage plat. Le seul point a lire dans l'image est la silhouette : si
la difference montre un creux borde net, Clarisse a trace une surface qui n'a
jamais existe sous forme de points.

Petite resolution assumee : ce test tranche par oui ou par non, pas par la
qualite de l'image.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\csg_scene.project"

MODES = (("union", 0), ("intersection", 1), ("difference", 2))


def setv(node, **values):
    for name, value in values.items():
        ix.cmds.SetValues([str(node) + "." + name], [str(value)])


for index, (name, mode) in enumerate(MODES):
    node = ix.cmds.CreateObject(name, "GeometryCsgSonde", "Global", "project:/")
    setv(node, mode=mode, radius_a=1.0, radius_b=0.8, offset=0.75)
    ix.cmds.SetValues([str(node) + ".translate[0]"], [str((index - 1) * 3.0)])
    geo = node.get_module().get_geometry()
    print("%-13s primitives=%s  bbox=%s"
          % (name,
             geo.get_primitive_count() if geo is not None else "AUCUNE GEOMETRIE",
             geo.get_bbox() if geo is not None else "-"))

# Un temoin natif : la meme sphere implicite livree par Clarisse. Si elle rend
# et pas la notre, le probleme est dans notre GeometryObject ; si aucune des
# deux ne rend, il est dans la scene.
witness = ix.cmds.CreateObject("temoin", "GeometrySphere", "Global", "project:/")
setv(witness, radius=1.0)
ix.cmds.SetValues([str(witness) + ".translate[1]"], ["2.6"])

key = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(key) + ".rotate"], ["-35.0", "35.0", "0.0"])
ix.cmds.SetValues([str(key) + ".intensity"], ["3.0"])
fill = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(fill) + ".intensity"], ["0.4"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["0.0", "1.2", "9.5"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-5.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["480", "220"])

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
