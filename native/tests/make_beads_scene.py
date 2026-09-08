# -*- coding: utf-8 -*-
"""Les perles sur un fil : quatre reglages cote a cote.

Quatre brins tendus, vus de tres pres. De haut en bas : le fil nu, un chapelet
regulier, le meme avec de l'irregularite, et des gouttes resserrees sur un cable
a trois torons -- ou l'on verifie que les perles ne s'alignent pas d'un brin a
l'autre, ce qui trahirait le procede.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\beads_scene.project"


def strand(name, y, **values):
    a = ix.cmds.CreateObject(name + "_a", "Locator", "Global", "project:/")
    b = ix.cmds.CreateObject(name + "_b", "Locator", "Global", "project:/")
    ix.cmds.SetValues([str(a) + ".translate"], ["-2.6", str(y), "0.0"])
    ix.cmds.SetValues([str(b) + ".translate"], ["2.6", str(y), "0.0"])

    tube = ix.cmds.CreateObject(name, "GeometryTube", "Global", "project:/")
    points = tube.get_attribute("point")
    points.set_value_count(2)
    points.set_object(a, 0)
    points.set_object(b, 1)

    base = dict(radius=0.012, sides=10, steps=220, strands=1)
    base.update(values)
    for key, value in base.items():
        if tube.get_attribute(key) is None:
            print("  !! pas d'attribut %s" % key)
            continue
        ix.cmds.SetValues([str(tube) + "." + key], [str(value)])

    mesh = tube.get_module().get_geometry()
    print("%-14s : %s" % (name,
          "%d sommets" % mesh.get_vertex_count() if mesh is not None else "RIEN"))
    return tube


tubes = [
    strand("nu", 0.42),
    strand("regulier", 0.14, bead_spacing=0.16, bead_size=5.0,
           bead_variation=0.0),
    strand("irregulier", -0.14, bead_spacing=0.16, bead_size=5.0,
           bead_variation=0.6),
    strand("cable", -0.42, bead_spacing=0.11, bead_size=3.2,
           bead_sharpness=1.4, bead_variation=0.5, strands=3,
           strand_gap=0.15, twist=1.5, radius=0.02),
]

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global",
                                "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"],
                  ["0.72", "0.74", "0.78"])
for name, value in (("specular_1_roughness", "0.18"),
                    ("specular_1_fresnel_reflectivity", "0.08")):
    if material.get_attribute(name) is not None:
        ix.cmds.SetValues([str(material) + "." + name], [value])
for tube in tubes:
    ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global",
                             "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-35.0", "40.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.2"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global",
                               "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.5"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["0.0", "0.0", "2.3"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global",
                                "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]",
                   str(image) + ".resolution[1]"], ["1100", "560"])
ix.cmds.SetValues([str(image) + ".resolution_multiplier"], ["1"])

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
