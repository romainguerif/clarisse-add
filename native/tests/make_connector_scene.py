# -*- coding: utf-8 -*-
"""Un cable complet : gaine raccourcie, connecteurs aux deux bouts, colliers.

C'est l'assemblage qui compte plus que chaque piece : le meme jeu de locators
nourrit trois nodes -- le tube pour la gaine, un nuage en mode Extremites pour
les connecteurs, un autre en mode Espacement pour les colliers -- et deux
Scatterers natifs posent les objets. Rien n'est ecrit pour l'instanciation.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\connector_scene.project"

SHAPE = [(0.0, 3.0, 0.0), (3.5, 1.2, 0.6), (7.0, 2.6, -0.4), (10.0, 1.0, 0.0)]

locators = []
for i, (x, y, z) in enumerate(SHAPE):
    loc = ix.cmds.CreateObject("cp%d" % i, "Locator", "Global", "project:/")
    ix.cmds.SetValues([str(loc) + ".translate"], [str(x), str(y), str(z)])
    locators.append(loc)


def wire(node):
    for i, loc in enumerate(locators):
        ix.cmds.AddTableRow(str(node) + ".control_points")
        ix.cmds.SetValues([str(node) + ".point[%d]" % i], [str(loc)])


def wire_list(node):
    attr = node.get_attribute("control_points")
    attr.set_value_count(len(locators))
    for i, loc in enumerate(locators):
        attr.set_object(loc, i)


# La gaine, raccourcie des deux cotes pour laisser place aux connecteurs.
tube = ix.cmds.CreateObject("gaine", "GeometryTube", "Global", "project:/")
wire(tube)
ix.cmds.SetValues([str(tube) + ".radius"], ["0.12"])
ix.cmds.SetValues([str(tube) + ".sides"], ["18"])
ix.cmds.SetValues([str(tube) + ".steps"], ["24"])
ix.cmds.SetValues([str(tube) + ".trim_start"], ["0.25"])
ix.cmds.SetValues([str(tube) + ".trim_end"], ["0.25"])
print("gaine   : %d sommets" % tube.get_module().get_geometry().get_vertex_count())

# Les deux extremites, orientees vers l'exterieur.
ends = ix.cmds.CreateObject("extremites", "GeometryCurvePoints", "Global", "project:/")
wire_list(ends)
ix.cmds.SetValues([str(ends) + ".mode"], ["2"])
ix.cmds.SetValues([str(ends) + ".normal_source"], ["0"])
# Meme valeur que le trim du tube : l'embout se pose exactement la ou la gaine
# s'arrete, et son corps la recouvre.
ix.cmds.SetValues([str(ends) + ".end_offset"], ["0.25"])
print("bouts   : %d points" % ends.get_module().get_point_count())

# Des colliers repartis le long.
clamps = ix.cmds.CreateObject("colliers", "GeometryCurvePoints", "Global", "project:/")
wire_list(clamps)
ix.cmds.SetValues([str(clamps) + ".mode"], ["1"])
ix.cmds.SetValues([str(clamps) + ".spacing"], ["1.6"])
ix.cmds.SetValues([str(clamps) + ".offset"], ["0.4"])
ix.cmds.SetValues([str(clamps) + ".normal_source"], ["0"])
print("colliers: %d points" % clamps.get_module().get_point_count())

# Les objets a instancier : un connecteur trapu, un collier plat.
plug = ix.cmds.CreateObject("connecteur", "GeometryPolycylinder", "Global", "project:/")
for name, value in (("height", "0.5"), ("radius", "0.2")):
    if plug.get_attribute(name) is not None:
        ix.cmds.SetValues([str(plug) + "." + name], [value])

clamp = ix.cmds.CreateObject("collier", "GeometryPolycylinder", "Global", "project:/")
for name, value in (("height", "0.12"), ("radius", "0.19")):
    if clamp.get_attribute(name) is not None:
        ix.cmds.SetValues([str(clamp) + "." + name], [value])


def scatter(name, support, geo):
    node = ix.cmds.CreateObject(name, "SceneObjectScatterer", "Global", "project:/")
    ix.cmds.SetValues([str(node) + ".geometry_support"], [str(support)])
    ix.cmds.SetValues([str(node) + ".use_support_normals"], ["1"])
    ix.cmds.AddTableRow(str(node) + ".geometries")
    ix.cmds.SetValues([str(node) + ".geometry[0]"], [str(geo)])
    return node


scatter("pose_bouts", ends, plug)
scatter("pose_colliers", clamps, clamp)

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.28", "0.28", "0.3"])
tube.get_module().get_geometry()
ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-45.0", "25.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.45"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["5.0", "2.2", "12.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-3.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["640", "360"])
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
