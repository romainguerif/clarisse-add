# -*- coding: utf-8 -*-
"""Scene 3D de profondeur de champ : des spheres a des distances differentes.

Sert deux buts. D'abord decouvrir si un Layer 3D expose ses AOV au filtre :
l'attribut `output_layer` du Layer promet de conserver tous les canaux quand
il vaut `all`. Si un canal de profondeur apparait, le rayon du bokeh peut
varier par pixel, et on tient la vraie mise au point.

Ensuite servir de banc d'essai visuel : sans objets a des profondeurs
differentes, un flou de mise au point ne se juge pas.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\dof.project"
REPORT = r"J:\_WINDOWSTEMP\claude\dof_scene.log"

lines = []


def say(text):
    lines.append(text)
    print(text)


def create(name, cls):
    item = ix.cmds.CreateObject(name, cls, "Global", "project:/")
    if item is None:
        say("ECHEC creation %s (%s)" % (name, cls))
    return item


def setv(item, attribute, *values):
    ix.cmds.SetValues([str(item) + "." + attribute], [str(v) for v in values])


def setvec(item, attribute, *values):
    paths = ["%s.%s[%d]" % (str(item), attribute, i) for i in range(len(values))]
    ix.cmds.SetValues(paths, [str(v) for v in values])


def first_of(item, attribute):
    attr = item.get_attribute(attribute)
    if attr is None or attr.get_value_count() == 0:
        return None
    return attr.get_object(0)


# -- la scene ----------------------------------------------------------------
camera = create("camera", "CameraPerspective")
setvec(camera, "translate", 0, 6, 30)
setvec(camera, "rotate", -8, 0, 0)

light = create("sun", "LightPhysicalDistant")
setvec(light, "rotate", -50, 35, 0)
if light.get_attribute("intensity") is not None:
    setv(light, "intensity", 4)

sun = create("key", "LightPhysicalSphere")
setvec(sun, "translate", -12, 14, 14)
if sun.get_attribute("intensity") is not None:
    setv(sun, "intensity", 600)

ground = create("ground", "GeometryPolygrid")
setvec(ground, "translate", 0, -2, 0)
for attribute in ("size", "length", "width"):
    if ground.get_attribute(attribute) is not None:
        setv(ground, attribute, 200)

# Une rangee de spheres qui s'eloigne : c'est la seule facon de voir une mise
# au point. Les plus proches et les plus lointaines doivent flouter, celles du
# milieu rester nettes.
for index in range(9):
    depth = -34.0 + index * 9.0
    sphere = create("ball_%d" % index, "GeometrySphere")
    setvec(sphere, "translate", (index - 4) * 3.2, 0.0, depth)
    if sphere.get_attribute("radius") is not None:
        setv(sphere, "radius", 1.4)

# -- l'image -----------------------------------------------------------------
image = create("dof", "Image")
setv(image, "resolution_mode", 1)
setvec(image, "resolution", 960, 540)
setv(image, "resolution_multiplier", 2)

ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
layer = first_of(image, "layers")
say("layer            : %s" % layer)

if layer is not None:
    setv(layer, "layer_3d.active_camera", str(camera))
    # `all` conserve tous les canaux AOV au lieu de ne garder que RGBA.
    # C'est la piste : si un canal de profondeur survit jusqu'au filtre, le
    # rayon peut varier par pixel.
    setv(layer, "output_layer", -1)
    say("output_layer     : %s" % layer.get_attribute("output_layer").get_string())

    names = [layer.get_attribute(i).get_name()
             for i in range(layer.get_attribute_count())]
    say("attributs layer  : %s" % ", ".join(n for n in names if "aov" in n.lower()
                                            or "channel" in n.lower()))

    layer.get_module().add_filter("ImageFilterBokeh", "bokeh")
    obj = first_of(layer, "filters")
    if obj is not None:
        setv(obj, "radius", 18.0)
        setv(obj, "blades", 6)
        say("filtre           : %s" % obj)

ix.application.save_project(OUT)
say("projet           : %s" % OUT)
say("image a rendre   : %s" % str(image))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
