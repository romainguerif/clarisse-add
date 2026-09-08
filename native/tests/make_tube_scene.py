# -*- coding: utf-8 -*-
"""Monte une scene minimale autour d'un GeometryTube et la sauve.

Le but est de voir une image : le compte de sommets et la boite englobante
disent que le maillage existe et qu'il a la bonne taille, ils ne disent rien de
l'orientation des faces. Un tube dont les normales pointent vers l'interieur a
exactement les memes comptes.

Rendu ensuite par cnode -image build://project/image -frames_list 1.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\tube_scene.project"

# La courbe : un S en trois dimensions, pour que le repere travaille.
positions = [(0.0, 0.0, 0.0), (2.0, 1.5, 0.0), (4.0, 0.0, 1.5), (6.0, 1.5, 0.0)]
for i, p in enumerate(positions):
    loc = ix.cmds.CreateObject("cp%d" % i, "Locator", "Global", "project:/")
    ix.cmds.SetValues([str(loc) + ".translate"],
                      [str(p[0]), str(p[1]), str(p[2])])

tube = ix.cmds.CreateObject("tube", "GeometryTube", "Global", "project:/")
ix.cmds.AddValues([str(tube) + ".control_points"],
                  ["project:/cp0", "project:/cp1", "project:/cp2", "project:/cp3"])
attr = tube.get_attribute("control_points")
if attr.get_value_count() == 0:
    attr.set_value_count(4)
    for i in range(4):
        attr.set_object(ix.get_item("project:/cp%d" % i), i)
ix.cmds.SetValues([str(tube) + ".radius"], ["0.25"])
ix.cmds.SetValues([str(tube) + ".sides"], ["24"])
ix.cmds.SetValues([str(tube) + ".steps"], ["16"])
print("tube : %d points, %d sommets"
      % (attr.get_value_count(), tube.get_module().get_geometry().get_vertex_count()))

# Un sol, pour que l'ombre portee dise quelque chose du volume.
ground = ix.cmds.CreateObject("ground", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(ground) + ".translate"], ["3.0", "-1.0", "0.0"])
for attribute, value in (("size", "30"), ("width", "30"), ("length", "30")):
    if ground.get_attribute(attribute) is not None:
        ix.cmds.SetValues([str(ground) + "." + attribute], [value])

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.55", "0.45", "0.4"])
ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])
try:
    ix.cmds.SetValues([str(ground) + ".materials[0]"], [str(material)])
except Exception as error:
    print("materiau du sol : %s" % error)

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-45.0", "35.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.35"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["3.0", "2.5", "13.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-6.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["480", "300"])
ix.cmds.SetValues([str(image) + ".resolution_multiplier"], ["2"])

# AddLayer, pas CreateObject : un layer est un objet EMBARQUE dans l'image.
# Un Layer3d cree a part dans le contexte puis ajoute a .layers ne rend rien,
# et ne signale rien. La commande renvoie None meme quand elle reussit : la
# seule preuve est de relire l'attribut layers.
ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
layers = image.get_attribute("layers")
layer = layers.get_object(0) if layers.get_value_count() > 0 else None
print("layer            : %s" % layer)
if layer is not None:
    ix.cmds.SetValues([str(layer) + ".active_camera"], [str(camera)])
    ix.cmds.SetValues([str(layer) + ".renderer"], [str(renderer)])

if not os.path.isdir(os.path.dirname(OUT)):
    os.makedirs(os.path.dirname(OUT))
ix.application.save_project(OUT)
print("scene ecrite : %s" % OUT)
