# -*- coding: utf-8 -*-
"""Construit une scene de mesure pour la sonde SDF.

Un seul node, parametre par les variables d'environnement, pour que chaque
rendu produise une ligne de statistiques propre. Trois configurations nous
interessent :

    sphere tracing + une primitive Clarisse par blob   (le BVH elague)
    sphere tracing + une seule primitive globale       (aucun elagage)
    intersection analytique, union dure                (le temoin csg_sonde)

La resolution est volontairement petite : on mesure un cout par rayon, pas une
image.
"""
import os

OUT = os.environ.get("SDF_OUT", r"J:\_WINDOWSTEMP\claude\sdf_sonde\scene.project")
MODE = int(os.environ.get("SDF_MODE", "0"))
SPLIT = os.environ.get("SDF_SPLIT", "1") == "1"
BLEND = float(os.environ.get("SDF_BLEND", "0.25"))
BLOBS = int(os.environ.get("SDF_BLOBS", "64"))
STEP = float(os.environ.get("SDF_STEP", "1.0"))
MAXIT = int(os.environ.get("SDF_MAXIT", "128"))

node = ix.cmds.CreateObject("sdf", "GeometrySdfSonde", "Global", "project:/")
ix.cmds.SetValues([str(node) + ".blob_count"], [str(BLOBS)])
ix.cmds.SetValues([str(node) + ".blend"], [str(BLEND)])
ix.cmds.SetValues([str(node) + ".mode"], [str(MODE)])
ix.cmds.SetValues([str(node) + ".split_primitives"], ["1" if SPLIT else "0"])
ix.cmds.SetValues([str(node) + ".step_scale"], [str(STEP)])
ix.cmds.SetValues([str(node) + ".max_iterations"], [str(MAXIT)])

geo = node.get_module().get_geometry()
log = open(r"J:\_WINDOWSTEMP\claude\sdf_sonde\build_log.txt", "a")
if geo is None:
    log.write("AUCUNE GEOMETRIE -- le module n'a rien rendu\n")
else:
    log.write("mode=%d split=%s blend=%.2f blobs=%d : primitives=%s bbox=%s\n"
              % (MODE, SPLIT, BLEND, BLOBS,
                 geo.get_primitive_count(), geo.get_bbox()))
log.close()

key = ix.cmds.CreateObject("key", "LightPhysicalDistant", "Global", "project:/")
ix.cmds.SetValues([str(key) + ".rotate"], ["-35.0", "35.0", "0.0"])
ix.cmds.SetValues([str(key) + ".intensity"], ["3.0"])
fill = ix.cmds.CreateObject("fill", "LightPhysicalAmbient", "Global", "project:/")
ix.cmds.SetValues([str(fill) + ".intensity"], ["0.4"])

camera = ix.cmds.CreateObject("cam", "CameraPerspective", "Global", "project:/")
ix.cmds.SetValues([str(camera) + ".translate"], ["0.0", "0.0", "6.5"])

renderer = ix.cmds.CreateObject("renderer", "RendererRaytracer", "Global", "project:/")

image = ix.cmds.CreateObject("image", "Image", "Global", "project:/")
ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  ["400", "400"])

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
