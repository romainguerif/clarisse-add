# -*- coding: utf-8 -*-
"""Une courbe, des points le long, et le Scatterer natif qui instancie dessus.

C'est le test qui compte pour GeometryCurvePoints : le node ne sert a rien si le
Scatterer de Clarisse ne l'accepte pas comme support. Le rendu montre du meme
coup si les instances sont orientees comme prevu.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\scatter_scene.project"

SHAPE = [(0.0, 0.0, 0.0), (3.0, 1.0, -1.0), (6.0, 0.0, 1.0), (9.0, 1.5, 0.0)]

points = []
for i, (x, y, z) in enumerate(SHAPE):
    loc = ix.cmds.CreateObject("cp%d" % i, "Locator", "Global", "project:/")
    ix.cmds.SetValues([str(loc) + ".translate"], [str(x), str(y), str(z)])
    points.append(loc)


def wire_curve(node):
    attr = node.get_attribute("control_points")
    attr.set_value_count(len(points))
    for i, loc in enumerate(points):
        attr.set_object(loc, i)


# Le tube sert de temoin visuel : il montre ou passe la courbe.
tube = ix.cmds.CreateObject("tube", "GeometryTube", "Global", "project:/")
wire_curve(tube)
ix.cmds.SetValues([str(tube) + ".radius"], ["0.08"])
ix.cmds.SetValues([str(tube) + ".sides"], ["12"])
ix.cmds.SetValues([str(tube) + ".steps"], ["20"])

cloud = ix.cmds.CreateObject("points", "GeometryCurvePoints", "Global", "project:/")
wire_curve(cloud)
ix.cmds.SetValues([str(cloud) + ".count"], ["14"])
ix.cmds.SetValues([str(cloud) + ".normal_source"], ["1"])   # Normale
print("nuage : %d points" % cloud.get_module().get_point_count())

# Ce qu'on instancie : un cylindre debout, pour qu'on voie l'orientation.
post = ix.cmds.CreateObject("post", "GeometryPolycylinder", "Global", "project:/")
for name, value in (("height", "0.9"), ("radius", "0.06")):
    if post.get_attribute(name) is not None:
        ix.cmds.SetValues([str(post) + "." + name], [value])

scatterer = ix.cmds.CreateObject("scatter", "SceneObjectScatterer", "Global", "project:/")
ix.cmds.SetValues([str(scatterer) + ".geometry_support"], [str(cloud)])
# use_support_normals fait porter aux instances la normale du point, qui est ce
# que notre node ecrit selon normal_source.
ix.cmds.SetValues([str(scatterer) + ".use_support_normals"], ["1"])

# Une table CID se remplit en deux temps : AddTableRow cree la ligne, puis on
# ecrit dans les colonnes, qui sont des attributs indexes a part entiere. Ecrire
# directement dans une colonne sans avoir ajoute la ligne ne signale rien et ne
# fait rien -- le scatterer reste vide et le rendu ne montre aucune instance.
ix.cmds.AddTableRow(str(scatterer) + ".geometries")
ix.cmds.SetValues([str(scatterer) + ".geometry[0]"], [str(post)])
column = scatterer.get_attribute("geometry")
print("table geometries : %d ligne(s), objet = %s"
      % (column.get_value_count(), column.get_object(0)))

# Forcer la construction : les shading groups n'existent pas avant, et
# l'assignation de materiau echoue alors avec un message qu'on lit mal.
tube.get_module().get_geometry()

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.55", "0.5", "0.45"])
ix.cmds.SetValues([str(tube) + ".materials[0]"], [str(material)])

ground = ix.cmds.CreateObject("ground", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(ground) + ".translate"], ["4.5", "-0.6", "0.0"])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-50.0", "25.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.35"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["4.5", "4.0", "11.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-18.0", "0.0", "0.0"])

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
