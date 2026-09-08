# -*- coding: utf-8 -*-
"""Bruit sur la courbe, et faisceau de torons qui ondulent sans se traverser.

Quatre variantes sur la meme courbe :
  - lisse, pour comparer ;
  - la meme avec du bruit, qui doit onduler sans changer de longueur ni bouger
    a ses extremites ;
  - six torons serres, sans jeu ;
  - six torons avec du jeu et de l'ondulation propre a chacun. C'est celui qui
    compte : les brins doivent se croiser visuellement sans jamais se penetrer,
    et la garantie est geometrique -- l'amplitude est bornee par la marge que le
    jeu libere, pas verifiee apres coup.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\bundle_scene.project"

SHAPE = [(0.0, 0.0), (3.0, 1.4), (6.0, -0.6), (9.0, 0.8)]

# (torons, bruit courbe, jeu, bruit toron, z, etiquette)
VARIANTS = [(1, 0.0,  0.0,  0.0, 0.0,  "lisse"),
            (1, 0.18, 0.0,  0.0, -2.2, "bruit sur la courbe"),
            (6, 0.0,  0.0,  0.0, -4.4, "6 torons serres"),
            (6, 0.06, 0.35, 1.0, -6.6, "6 torons avec jeu et ondulation")]


def make(index, strands, noise, gap, strand_noise, z, label):
    tube = ix.cmds.CreateObject("faisceau_%d" % index, "GeometryTube",
                                "Global", "project:/")
    for i, (x, y) in enumerate(SHAPE):
        loc = ix.cmds.CreateObject("cp_%d_%d" % (index, i), "Locator",
                                   "Global", "project:/")
        ix.cmds.SetValues([str(loc) + ".translate"], [str(x), str(y), str(z)])
        ix.cmds.AddTableRow(str(tube) + ".control_points")
        ix.cmds.SetValues([str(tube) + ".point[%d]" % i], [str(loc)])

    ix.cmds.SetValues([str(tube) + ".radius"], ["0.32"])
    ix.cmds.SetValues([str(tube) + ".sides"], ["14"])
    ix.cmds.SetValues([str(tube) + ".steps"], ["48"])
    ix.cmds.SetValues([str(tube) + ".strands"], [str(strands)])
    ix.cmds.SetValues([str(tube) + ".twist"], ["0.35"])
    ix.cmds.SetValues([str(tube) + ".noise_amplitude"], [str(noise)])
    ix.cmds.SetValues([str(tube) + ".noise_frequency"], ["0.45"])
    ix.cmds.SetValues([str(tube) + ".noise_seed"], [str(index * 17)])
    ix.cmds.SetValues([str(tube) + ".strand_gap"], [str(gap)])
    ix.cmds.SetValues([str(tube) + ".strand_noise"], [str(strand_noise)])
    ix.cmds.SetValues([str(tube) + ".strand_noise_frequency"], ["1.8"])

    mesh = tube.get_module().get_geometry()
    box = tube.get_module().get_bbox()
    print("%-32s : %d sommets, bbox x [%.3f %.3f]"
          % (label, mesh.get_vertex_count(), box.get_min()[0], box.get_max()[0]))
    return tube


tubes = [make(i, st, n, g, sn, z, label)
         for i, (st, n, g, sn, z, label) in enumerate(VARIANTS)]

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.5", "0.42", "0.32"])
for tube in tubes:
    tube.get_module().get_geometry()
    ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-52.0", "28.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.4"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["4.5", "4.5", "11.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-25.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["720", "420"])
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
