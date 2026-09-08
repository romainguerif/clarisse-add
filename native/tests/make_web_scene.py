# -*- coding: utf-8 -*-
"""Une toile orbitele, de face, telle qu'elle sort de la boite.

La sonde a verifie les nombres ; il reste a verifier ce que l'oeil sait mieux
faire qu'elle -- que la silhouette ne soit pas une rosace, que le desordre se
lise, que les fils d'amarrage partent quelque part.

Deux toiles cote a cote : celle d'usine, et une Zygiella avec son secteur libre
et son fil avertisseur. Fond noir et lumiere rasante, comme une photo de toile.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\web_scene.project"


def web(name, x, **values):
    node = ix.cmds.CreateObject(name, "GeometryWeb", "Global", "project:/")
    for key, value in values.items():
        if node.get_attribute(key) is None:
            print("  !! pas d'attribut %s" % key)
            continue
        ix.cmds.SetValues([str(node) + "." + key], [str(value)])
    ix.cmds.SetValues([str(node) + ".translate"], [str(x), "0.0", "0.0"])

    mesh = node.get_module().get_geometry()
    print("%-12s : %s" % (name, "%d sommets, %d polygones"
                          % (mesh.get_vertex_count(), mesh.get_polygon_count())
                          if mesh is not None else "RIEN"))
    return node


webs = [
    web("araneus", -0.085, radius=0.07, seed=3),
    web("zygiella", 0.085, radius=0.07, seed=11, radii=26,
        free_sector=38.0, asymmetry=1.25, spiral_pitch=0.035,
        anchors=4, aspect=0.78),
]

# Une soie est presque transparente et brille surtout de cote. On reste sur un
# blanc casse un peu speculaire : le but est de lire la geometrie, pas de faire
# joli.
material = ix.cmds.CreateObject("soie", "MaterialPhysicalStandard", "Global",
                                "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"],
                  ["0.82", "0.84", "0.88"])
for name, value in (("specular_1_roughness", "0.25"),
                    ("specular_1_fresnel_reflectivity", "0.06")):
    if material.get_attribute(name) is not None:
        ix.cmds.SetValues([str(material) + "." + name], [value])

# Cinq soies, donc cinq emplacements de matiere sur chaque toile.
for node in webs:
    slots = node.get_attribute("materials")
    for i in range(slots.get_value_count()):
        ix.cmds.SetValues([str(node) + ".materials[%d]" % i], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global",
                             "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-20.0", "25.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["4.5"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global",
                               "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.25"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["0.0", "0.0", "0.42"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global",
                                "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]",
                   str(image) + ".resolution[1]"], ["1400", "760"])
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
