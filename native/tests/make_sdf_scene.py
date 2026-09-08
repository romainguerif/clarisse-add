# -*- coding: utf-8 -*-
"""Le champ de distance : trois formes, trois operations, un raccord arrondi.

La scene est faite pour qu'un seul coup d'oeil suffise a juger :

  - une capsule ajoutee a une boite avec un gros raccord -- c'est la signature
    qu'aucun booleen sur maillage ne sait faire, et si elle est la, le coeur
    fonctionne ;
  - un cylindre soustrait, arete franche -- le booleen dur, qui doit rester net
    au meme endroit ;
  - un tore ajoute sans raccord, pour verifier une primitive a deux rayons.

Un plan gris sous l'ensemble donne l'ombre portee : sans elle on ne voit pas si
la forme est posee ou flottante.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\sdf_scene.project"


def locator(name, position, rotation=None, scale=None):
    item = ix.cmds.CreateObject(name, "Locator", "Global", "project:/")
    ix.cmds.SetValues([str(item) + ".translate"],
                      [str(v) for v in position])
    if rotation is not None:
        ix.cmds.SetValues([str(item) + ".rotate"], [str(v) for v in rotation])
    if scale is not None:
        ix.cmds.SetValues([str(item) + ".scale"], [str(v) for v in scale])
    return item


field = ix.cmds.CreateObject("champ", "GeometrySdf", "Global", "project:/")
stack = field.get_attribute("item")

rows = [
    # (nom, position, rotation, forme, operation, raccord, taille, arrondi)
    ("boite",    (0.0, 0.0, 0.0),  None,            1, 0, 0.0,  (1.2, 0.8, 1.2), 0.05),
    ("capsule",  (1.4, 0.9, 0.0),  (0.0, 0.0, 25.0), 3, 0, 0.55, (0.55, 0.9, 1.0), 0.0),
    ("percage",  (0.0, 0.0, 0.0),  None,            2, 1, 0.0,  (0.45, 3.0, 1.0), 0.0),
    ("anneau",   (-1.5, 0.6, 0.0), (90.0, 0.0, 0.0), 5, 0, 0.0,  (0.7, 0.22, 1.0), 0.0),
]

# Chaque colonne d'une table est un attribut multiple a part entiere : les
# dimensionner toutes, sinon les ecritures suivantes tombent hors plage et sont
# ignorees en silence.
stack.set_value_count(len(rows))
for column, per_row in (("shape", 1), ("operation", 1), ("blend", 1),
                        ("rounding", 1), ("size_x", 1), ("size_y", 1),
                        ("size_z", 1), ("group", 1)):
    attr = field.get_attribute(column)
    if attr is not None:
        attr.set_value_count(len(rows) * per_row)
for index, (name, position, rotation, shape, operation, blend, size,
            rounding) in enumerate(rows):
    item = locator(name, position, rotation)
    stack.set_object(item, index)
    field.get_attribute("shape").set_long(shape, index)
    field.get_attribute("operation").set_long(operation, index)
    field.get_attribute("blend").set_double(blend, index)
    field.get_attribute("rounding").set_double(rounding, index)
    for axis, column in enumerate(("size_x", "size_y", "size_z")):
        field.get_attribute(column).set_double(size[axis], index)

geometry = field.get_module().get_geometry()
if geometry is None:
    print("champ : RIEN")
else:
    box = geometry.get_bbox()
    print("champ : %d primitive(s) Clarisse, boite (%.2f %.2f %.2f) a "
          "(%.2f %.2f %.2f)"
          % (geometry.get_primitive_count(),
             box.get_min()[0], box.get_min()[1], box.get_min()[2],
             box.get_max()[0], box.get_max()[1], box.get_max()[2]))

ground = ix.cmds.CreateObject("sol", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(ground) + ".size[0]", str(ground) + ".size[1]"],
                  ["20", "20"])
ix.cmds.SetValues([str(ground) + ".translate"], ["0.0", "-0.85", "0.0"])

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global",
                                "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"],
                  ["0.62", "0.30", "0.18"])
for name in ("specular_1_roughness", "specular_1_fresnel_reflectivity"):
    if material.get_attribute(name) is not None:
        ix.cmds.SetValues([str(material) + "." + name],
                          ["0.35" if "rough" in name else "0.05"])
ix.cmds.SetValues([str(field) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global",
                             "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-42.0", "38.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["2.6"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global",
                               "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.8"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["3.4", "3.0", "5.6"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-22.0", "28.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global",
                                "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]",
                   str(image) + ".resolution[1]"], ["900", "620"])
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
