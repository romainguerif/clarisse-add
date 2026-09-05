# -*- coding: utf-8 -*-
"""Preuve que la profondeur pilote le rayon : deux mises au point, une scene.

Quatre spheres qui s'eloignent. En placant la mise au point sur la premiere
puis sur la derniere, ce qui est net doit changer de place. Si les deux images
sont identiques, la profondeur n'est pas lue -- c'est le seul test qui le dit
sans ambiguite.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\focus"
REPORT = r"J:\_WINDOWSTEMP\claude\focus.log"

# La camera est a z = 14 ; les spheres a 0, -8, -16, -24. Leurs distances
# valent donc 14, 22, 30 et 38.
# "none" : aucun filtre. None : filtre sans profondeur. Un entier : viser la
# sphere de cet index, sans jamais saisir de distance -- c'est tout l'interet.
VARIANTS = [("00_aucun_filtre", "none"), ("01_flou_uniforme", None),
            ("02_vise_sphere_0", 0), ("03_vise_sphere_3", 3)]

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
setv(renderer, "anti_aliasing_sample_count", 4)

camera = create("camera", "CameraPerspective")
setvec(camera, "translate", 0, 1.5, 14)

sun = create("sun", "LightPhysicalDistant")
setvec(sun, "rotate", -40, 25, 0)

# Des damiers clairs et sombres alternes : la nettete se lit mieux sur un
# motif contraste que sur une sphere lisse.
for index in range(4):
    ball = create("ball_%d" % index, "GeometrySphere")
    setvec(ball, "translate", (index - 1.5) * 3.4, 0.0, -index * 8.0)
    setv(ball, "radius", 1.4)

for name, focus in VARIANTS:
    image = create("img_" + name, "Image")
    setv(image, "resolution_mode", 1)
    setvec(image, "resolution", 640, 400)
    setv(image, "resolution_multiplier", 2)

    ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
    layer = first_of(image, "layers")
    setv(layer, "active_camera", str(camera))
    setv(layer, "renderer", str(renderer))
    setv(layer, "output_layer", -1)

    # Activer l'AOV de profondeur. Ces deux listes sont `hidden` -- l'editeur
    # d'AOV les peuple d'ordinaire -- mais cache ne veut pas dire en lecture
    # seule.
    selected = layer.get_attribute("selected_aov_list")
    enabled = layer.get_attribute("enabled_aov_list")
    selected.set_value_count(1)
    enabled.set_value_count(1)
    selected.set_string("depth", 0)
    enabled.set_bool(True, 0)

    if focus == "none":
        say("%s	%s" % (name, str(image)))
        ix.application.save_project(os.path.join(OUT, name + ".project"))
        continue

    layer.get_module().add_filter("ImageFilterBokeh", "bokeh")
    obj = first_of(layer, "filters")
    setv(obj, "radius", 22.0)
    setv(obj, "blades", 6)
    if focus is not None:
        setv(obj, "depth_aov", "depth")
        setv(obj, "focus_range", 3.0)
        setv(obj, "focus_object", "project:/ball_%d" % focus)
        say("  %s vise ball_%d -> focus_object='%s'"
            % (name, focus, obj.get_attribute("focus_object").get_string()))

    ix.application.save_project(os.path.join(OUT, name + ".project"))
    say("%s\t%s" % (name, str(image)))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
