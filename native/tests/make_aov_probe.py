# -*- coding: utf-8 -*-
"""Quels canaux un Layer 3D presente-t-il reellement a un filtre ?

Le filtre journalise la liste quand l'AOV demande est introuvable. On lui en
demande donc un qui n'existe pas, et il repond par l'inventaire -- c'est la
seule facon de connaitre les noms REELS, ceux du ImageMap, qui ne sont pas
forcement les libelles de l'interface.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\aov"
REPORT = r"J:\_WINDOWSTEMP\claude\aov_probe.log"

lines = []


def say(text):
    lines.append(text)
    print(text)


def create(name, cls):
    item = ix.cmds.CreateObject(name, cls, "Global", "project:/")
    if item is None:
        say("ECHEC %s (%s)" % (name, cls))
    return item


def setv(item, attribute, *values):
    ix.cmds.SetValues([str(item) + "." + attribute], [str(v) for v in values])


def setvec(item, attribute, *values):
    ix.cmds.SetValues(["%s.%s[%d]" % (str(item), attribute, i)
                       for i in range(len(values))],
                      [str(v) for v in values])


def first_of(item, attribute):
    attr = item.get_attribute(attribute)
    if attr is None or attr.get_value_count() == 0:
        return None
    return attr.get_object(0)


if not os.path.isdir(OUT):
    os.makedirs(OUT)

renderer = create("raytracer", "RendererRaytracer")
setv(renderer, "anti_aliasing_sample_count", 4)   # un banc d'essai, pas un rendu

camera = create("camera", "CameraPerspective")
setvec(camera, "translate", 0, 2, 14)

sun = create("sun", "LightPhysicalDistant")
setvec(sun, "rotate", -40, 25, 0)

for index in range(4):
    ball = create("ball_%d" % index, "GeometrySphere")
    setvec(ball, "translate", (index - 1.5) * 3.0, 0.0, -index * 8.0)
    setv(ball, "radius", 1.2)

image = create("aov_test", "Image")
setv(image, "resolution_mode", 1)
setvec(image, "resolution", 320, 240)
setv(image, "resolution_multiplier", 2)

ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
layer = first_of(image, "layers")
setv(layer, "active_camera", str(camera))
setv(layer, "renderer", str(renderer))

# `all` conserve tous les canaux au lieu de ne garder que RGBA.
setv(layer, "output_layer", -1)

# Les attributs qui pilotent les AOV, tels qu'ils existent reellement.
for name in ("selected_aov_list", "enabled_aov_list", "export_shading_aovs"):
    attr = layer.get_attribute(name)
    if attr is None:
        say("%-22s : absent" % name)
    else:
        say("%-22s : %d valeur(s), type %s"
            % (name, attr.get_value_count(), attr.get_type()))

# Activer l'AOV de profondeur. selected_aov_list et enabled_aov_list sont
# `hidden` -- l'editeur d'AOV les peuple -- mais cache ne veut pas dire en
# lecture seule. On les dimensionne puis on les remplit.
selected = layer.get_attribute("selected_aov_list")
enabled = layer.get_attribute("enabled_aov_list")
if selected is not None and enabled is not None:
    try:
        selected.set_value_count(1)
        enabled.set_value_count(1)
        selected.set_string("depth", 0)
        enabled.set_bool(True, 0)
        say("selected_aov_list      : %d -> '%s'"
            % (selected.get_value_count(), selected.get_string(0)))
        say("enabled_aov_list       : %d -> %s"
            % (enabled.get_value_count(), enabled.get_bool(0)))
    except Exception as error:
        say("activation directe a echoue : %s" % error)

# On demande un AOV inexistant : le filtre repondra par l'inventaire.
layer.get_module().add_filter("ImageFilterBokeh", "bokeh")
obj = first_of(layer, "filters")
setv(obj, "radius", 12.0)
setv(obj, "depth_aov", "depth")
setv(obj, "focus_distance", 26.0)
setv(obj, "focus_range", 3.0)
say("filtre                 : %s" % obj)

ix.application.save_project(os.path.join(OUT, "aov.project"))
say("image a rendre         : %s" % str(image))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
