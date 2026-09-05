# -*- coding: utf-8 -*-
"""Les modes de reglage et l'objectif reel, en une scene.

Les quatre modes autres que l'image ne floutent rien : ils montrent ce que le
filtre a compris. Ils se verifient donc a l'oeil et non a la mesure -- si la
visualisation du point met du rouge sur la mauvaise sphere, le probleme est
dans la lecture de la profondeur et pas dans le flou.

L'objectif reel, lui, se verifie au journal : il annonce le rayon maximal
qu'il a calcule, et cette valeur doit suivre l'ouverture. A f/1.4 le flou doit
etre nettement plus fort qu'a f/16, sans qu'aucun rayon ne soit saisi.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\modes"
REPORT = r"J:\_WINDOWSTEMP\claude\modes.log"

# (nom, output_type, objectif reel, f_stop)
VARIANTS = [
    ("00_image",        0, False, 0.0),
    ("01_bloom",        1, False, 0.0),
    ("02_point",        2, False, 0.0),
    ("03_noyau",        3, False, 0.0),
    ("04_mattes",       4, False, 0.0),
    ("05_lens_f1_4",    0, True,  1.4),
    ("06_lens_f16",     0, True, 16.0),
]

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

# Quatre spheres qui s'eloignent : la camera est a z = 14, elles a 0, -8, -16
# et -24, soit des profondeurs de 14, 22, 30 et 38 sur l'axe de visee.
for index in range(4):
    ball = create("ball_%d" % index, "GeometrySphere")
    setvec(ball, "translate", (index - 1.5) * 3.4, 0.0, -index * 8.0)
    setv(ball, "radius", 1.4)

# Une petite sphere tres brillante : sans elle, le mode bloom ne montrerait
# rien, la scene n'ayant aucune valeur au-dessus du seuil.
lamp = create("lamp", "GeometrySphere")
setvec(lamp, "translate", 4.5, 2.6, -12.0)
setv(lamp, "radius", 0.35)
glow = create("glow", "MaterialPhysicalDiffuse")
setvec(glow, "front_color", 40.0, 34.0, 26.0)
setv(lamp, "override_material", str(glow))

for name, mode, real_lens, f_stop in VARIANTS:
    image = create("img_" + name, "Image")
    setv(image, "resolution_mode", 1)
    setvec(image, "resolution", 640, 400)

    ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
    layer = first_of(image, "layers")
    setv(layer, "active_camera", str(camera))
    setv(layer, "renderer", str(renderer))
    setv(layer, "output_layer", -1)

    selected = layer.get_attribute("selected_aov_list")
    enabled = layer.get_attribute("enabled_aov_list")
    selected.set_value_count(1)
    enabled.set_value_count(1)
    selected.set_string("depth", 0)
    enabled.set_bool(True, 0)

    layer.get_module().add_filter("ImageFilterBokeh", "bokeh")
    obj = first_of(layer, "filters")
    setv(obj, "depth_aov", "depth")
    setv(obj, "focus_object", "project:/ball_1")
    setv(obj, "output_type", mode)
    setv(obj, "radius", 24.0)
    setv(obj, "blades", 6)
    setv(obj, "roundness", 0.15)
    setv(obj, "focus_range", 2.0)

    if mode == 1:
        # Le bloom n'existe que si la reprise est active.
        setv(obj, "threshold", 1.0)
        setv(obj, "gain", 3.0)

    if real_lens:
        setv(obj, "real_world_lens", 1)
        setv(obj, "focal_length", 50.0)
        setv(obj, "f_stop", f_stop)
        setv(obj, "world_scale", 2)      # metres
        setv(obj, "film_format", 1)      # Super 35

    ix.application.save_project(os.path.join(OUT, name + ".project"))
    say("%s\t%s" % (name, str(image)))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
