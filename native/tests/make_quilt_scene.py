# -*- coding: utf-8 -*-
"""Le capitonnage, sur une grille et sur une sphere.

La sphere est le test qui compte : elle prouve que le node ne suppose ni un
plan, ni des quads, ni une grille reguliere. Chaque polygone du maillage devient
un coussin, quelle que soit sa forme et son orientation.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\quilt_scene.project"


def quilt(name, source, z, **values):
    node = ix.cmds.CreateObject(name, "GeometryQuilt", "Global", "project:/")
    ix.cmds.SetValues([str(node) + ".input_geometry"], [str(source)])
    for key, value in values.items():
        ix.cmds.SetValues([str(node) + "." + key], [str(value)])
    mesh = node.get_module().get_geometry()
    print("%-12s : %s" % (name,
          "%d sommets, %d polygones" % (mesh.get_vertex_count(),
                                        mesh.get_polygon_count())
          if mesh is not None else "RIEN"))
    return node


# Un matelas : grille reguliere de quads.
grid = ix.cmds.CreateObject("grille", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(grid) + ".size[0]", str(grid) + ".size[1]"], ["6", "6"])
ix.cmds.SetValues([str(grid) + ".spans[0]", str(grid) + ".spans[1]"], ["5", "5"])
ix.cmds.SetValues([str(grid) + ".translate"], ["-4.0", "0.0", "0.0"])
ix.cmds.SetValues([str(grid) + ".unseen_by_camera"], ["1"])

matelas = quilt("matelas", grid, 0.0, resolution=12, seam=0.13, seam_depth=0.06,
                slack=0.22, puff_relative=0.45, pressure=1.0,
                bend_stiffness=0.4, iterations=150, jitter=0.008)

# Une sphere : triangles et pentagones, orientations quelconques.
sphere = ix.cmds.CreateObject("sphere", "GeometryPolysphere", "Global", "project:/")
for name, value in (("radius", "2.4"), ("horizontal_span", "14"),
                    ("vertical_span", "10")):
    if sphere.get_attribute(name) is not None:
        ix.cmds.SetValues([str(sphere) + "." + name], [value])
ix.cmds.SetValues([str(sphere) + ".translate"], ["4.0", "0.0", "0.0"])
ix.cmds.SetValues([str(sphere) + ".unseen_by_camera"], ["1"])

boule = quilt("boule", sphere, 0.0, resolution=10, seam=0.16, seam_depth=0.03,
              slack=0.2, puff_relative=0.4, pressure=1.0,
              bend_stiffness=0.4, iterations=150, jitter=0.008)

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.55", "0.56", "0.6"])
for node in (matelas, boule):
    node.get_module().get_geometry()
    ix.cmds.SetValues([str(node) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-50.0", "30.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["3.0"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["0.45"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["0.0", "5.5", "11.0"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-26.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["760", "420"])
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
