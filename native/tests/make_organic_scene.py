# -*- coding: utf-8 -*-
"""Le faisceau organique, tel que Romain l'a decrit.

Trois brins serres qui s'enroulent tres lentement -- presque lineairement -- et
par-dessus un cable qui tourne autour du faisceau en s'en ecartant par endroits,
la ou il pend.

Les deux nodes partagent les memes points de controle : c'est ce qui fait que le
second suit le premier sans qu'on ait rien a synchroniser.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\organic_scene.project"

SHAPE = [(0.0, 0.0, 0.0), (4.0, 0.9, 0.4), (8.0, -0.5, -0.3), (12.0, 0.6, 0.0)]

locators = []
for i, (x, y, z) in enumerate(SHAPE):
    loc = ix.cmds.CreateObject("cp%d" % i, "Locator", "Global", "project:/")
    ix.cmds.SetValues([str(loc) + ".translate"], [str(x), str(y), str(z)])
    locators.append(loc)


def wire(node):
    for i, loc in enumerate(locators):
        ix.cmds.AddTableRow(str(node) + ".control_points")
        ix.cmds.SetValues([str(node) + ".point[%d]" % i], [str(loc)])


def setv(node, **values):
    for name, value in values.items():
        ix.cmds.SetValues([str(node) + "." + name], [str(value)])


# Le faisceau : trois brins serres, enroulement tres lent, et un ecartement
# entre les points de serrage.
bundle = ix.cmds.CreateObject("faisceau", "GeometryTube", "Global", "project:/")
wire(bundle)
setv(bundle, radius=0.28, sides=16, steps=90, strands=3,
     twist=0.08,                    # tres lent : un tour tous les douze metres
     strand_gap=0.25, strand_noise=0.6, strand_noise_frequency=0.7,
     strand_spread=0.55, strand_spread_frequency=0.22,
     noise_amplitude=0.09, noise_frequency=0.25, noise_fade=0.6)
print("faisceau : %d sommets" % bundle.get_module().get_geometry().get_vertex_count())

# Le cable qui tourne autour : memes points, mais enroule, et il tombe la ou il
# s'ecarte.
around = ix.cmds.CreateObject("cable_enroule", "GeometryTube", "Global", "project:/")
wire(around)
setv(around, radius=0.07, sides=12, steps=220,
     noise_amplitude=0.09, noise_frequency=0.25, noise_fade=0.6,
     wrap_radius=0.42, wrap_turns=0.22, wrap_variation=0.55,
     wrap_variation_frequency=0.16, wrap_sag=0.55, wrap_seed=3)
print("enroule  : %d sommets" % around.get_module().get_geometry().get_vertex_count())

dark = ix.cmds.CreateObject("gaine", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(dark) + ".diffuse_front_color"], ["0.13", "0.13", "0.14"])
red = ix.cmds.CreateObject("rouge", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(red) + ".diffuse_front_color"], ["0.42", "0.07", "0.06"])

bundle.get_module().get_geometry()
around.get_module().get_geometry()
ix.cmds.SetValues([str(bundle) + ".materials[0]"], [str(dark)])
ix.cmds.SetValues([str(around) + ".materials[0]"], [str(red)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-42.0", "30.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.2"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.5"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["6.0", "1.4", "8.5"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-4.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["720", "400"])
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
