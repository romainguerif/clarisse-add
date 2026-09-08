# -*- coding: utf-8 -*-
"""Le capitonnage, sur une grille et sur une sphere.

La sphere est le test qui compte pour la topologie : elle prouve que le node ne
suppose ni un plan, ni des quads, ni une grille reguliere. Le matelas est le
test qui compte pour la qualite : c'est lui qu'on compare aux references, donc
c'est lui qui remplit le cadre.
"""
import os

OUT = r"J:\_WINDOWSTEMP\claude\quilt_scene.project"


def quilt(name, source, **values):
    node = ix.cmds.CreateObject(name, "GeometryClothPanel", "Global", "project:/")
    ix.cmds.SetValues([str(node) + ".input_geometry"], [str(source)])
    gy = values.pop("gravity_y", None)
    if gy is not None:
        ix.cmds.SetValues([str(node) + ".gravity[1]"], [str(gy)])
    for key, value in values.items():
        if node.get_attribute(key) is None:
            print("  !! pas d'attribut %s" % key)
            continue
        ix.cmds.SetValues([str(node) + "." + key], [str(value)])
    mesh = node.get_module().get_geometry()
    print("%-12s : %s" % (name,
          "%d sommets, %d polygones" % (mesh.get_vertex_count(),
                                        mesh.get_polygon_count())
          if mesh is not None else "RIEN"))
    return node


# Un matelas : grille reguliere de quads, six unites de cote, coussins de 1.2.
grid = ix.cmds.CreateObject("grille", "GeometryPolygrid", "Global", "project:/")
ix.cmds.SetValues([str(grid) + ".size[0]", str(grid) + ".size[1]"], ["6", "6"])
ix.cmds.SetValues([str(grid) + ".spans[0]", str(grid) + ".spans[1]"], ["5", "5"])
ix.cmds.SetValues([str(grid) + ".translate"], ["-4.0", "0.0", "0.0"])
ix.cmds.SetValues([str(grid) + ".unseen_by_camera"], ["1"])

matelas = quilt("matelas", grid,
                resolution=96, levels=5, substeps=160, iterations=8,
                damping=0.02,
                pressure=4.0, pin_rings=2,
                seam=0.055, seam_depth=0.03,
                seam_gather=0.30, seam_wrinkle_scale=0.055,
                wrinkles=0.12, wrinkle_reach=2.0, corner_gather=0.0,
                stretch=0.02, compression=500.0, shear_stiffness=4.0,
                crease_stretch=1.0, wrinkle_variation=0.45,
                wrinkle_scale=0.045, wrinkle_size=0.0,
                gravity_strength=0.0,
                jitter=0.002, smoothing=0, smoothing_amount=0.2)

# Une sphere : triangles et pentagones, orientations quelconques. Ce qu'on
# regarde ici, ce sont les aretes partagees : le contour est enfonce le long des
# normales aux sommets, donc deux coussins voisins doivent tomber d'accord.
sphere = ix.cmds.CreateObject("sphere", "GeometryPolysphere", "Global", "project:/")
for name, value in (("radius", "0.9"), ("horizontal_span", "12"),
                    ("vertical_span", "8")):
    if sphere.get_attribute(name) is not None:
        ix.cmds.SetValues([str(sphere) + "." + name], [value])
ix.cmds.SetValues([str(sphere) + ".translate"], ["-0.6", "0.0", "-3.2"])
ix.cmds.SetValues([str(sphere) + ".unseen_by_camera"], ["1"])

boule = quilt("boule", sphere,
              resolution=28, levels=3, substeps=100, iterations=8,
              damping=0.02,
              pressure=4.0, pin_rings=2,
              seam=0.065, seam_depth=0.03,
              seam_gather=0.28, seam_wrinkle_scale=0.06,
              wrinkles=0.12, wrinkle_reach=2.0,
              stretch=0.02, compression=500.0, shear_stiffness=4.0,
              crease_stretch=1.0, wrinkle_variation=0.45, wrinkle_scale=0.08,
              gravity_strength=0.0,
              jitter=0.002, smoothing=0, smoothing_amount=0.2)

material = ix.cmds.CreateObject("mat", "MaterialPhysicalStandard", "Global", "project:/")
ix.cmds.SetValues([str(material) + ".diffuse_front_color"], ["0.5", "0.51", "0.55"])
for name, values in (("specular_1_roughness", ["0.42"]),
                     ("specular_1_fresnel_reflectivity", ["0.05"]),
                     ("diffuse_back_color", ["0.45", "0.46", "0.5"]),
                     ("diffuse_back_strength", ["1.0"])):
    if material.get_attribute(name) is not None:
        ix.cmds.SetValues([str(material) + "." + name], values)
for node in (matelas, boule):
    node.get_module().get_geometry()
    ix.cmds.SetValues([str(node) + ".materials[0]"], [str(material)])

light = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(light) + ".rotate"], ["-38.0", "35.0", "0.0"])
ix.cmds.SetValues([str(light) + ".intensity"], ["2.2"])
ambient = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(ambient) + ".intensity"], ["1.1"])

# Cadrage serre : deux coussins et demi dans la largeur. C'est a cette echelle
# que sont les references, donc c'est a cette echelle qu'il faut juger.
camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["-4.3", "1.9", "1.9"])
ix.cmds.SetValues([str(camera) + ".rotate"], ["-40.0", "0.0", "0.0"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["1280", "720"])
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
