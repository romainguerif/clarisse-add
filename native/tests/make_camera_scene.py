# -*- coding: utf-8 -*-
"""Scene de profondeur de champ, rendue par la camera Bokeh.

Une rangee de spheres qui s'eloigne, des points lumineux tres brillants au
fond, et la mise au point posee au milieu. C'est le seul montage ou une
profondeur de champ se juge : il faut du net et du flou dans la meme image, et
des hautes lumieres pour voir la forme du diaphragme.

Chaque variante est une image distincte : `DeleteItems` echoue en silence, les
objets s'empilent, et le rendu viserait toujours le premier.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\camera"
REPORT = r"J:\_WINDOWSTEMP\claude\camera_scene.log"

BASE = {"enable_dof": "1", "f_stop": "1.2", "focus_distance": "26",
        "enable_bokeh": "1", "blades": "0", "blade_rotation": "0.0",
        "blade_curvature": "0.0", "anamorphism": "0.0",
        "optical_vignetting": "0.0", "spherical_aberration": "0.0",
        "aperture_swirl": "0.0"}

VARIANTS = [
    ("00_sans_dof", {"enable_dof": "0"}),
    ("01_disque", {}),
    ("02_six_lames", {"blades": "6"}),
    ("03_lames_bombees", {"blades": "6", "blade_curvature": "0.6"}),
    ("04_lames_concaves", {"blades": "6", "blade_curvature": "-0.7"}),
    ("05_bulle_de_savon", {"spherical_aberration": "0.9"}),
    ("06_cremeux", {"spherical_aberration": "-0.9"}),
    ("07_oeil_de_chat", {"optical_vignetting": "0.75"}),
    ("08_anamorphique", {"anamorphism": "0.55"}),
    ("09_tourbillon", {"blades": "6", "aperture_swirl": "0.8"}),
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

# -- la scene, commune a toutes les variantes --------------------------------
renderer = create("raytracer", "RendererRaytracer")

sun = create("sun", "LightPhysicalDistant")
setvec(sun, "rotate", -42, 30, 0)

ground = create("ground", "GeometryPolygrid")
setvec(ground, "translate", 0, -2.2, 0)
for attribute in ("size", "length", "width"):
    if ground.get_attribute(attribute) is not None:
        setv(ground, attribute, 400)

# La rangee : du plus proche au plus lointain, la mise au point est au milieu.
for index in range(11):
    sphere = create("ball_%d" % index, "GeometrySphere")
    setvec(sphere, "translate", (index - 5) * 2.6, 0.0, 6.0 - index * 4.0)
    if sphere.get_attribute("radius") is not None:
        setv(sphere, "radius", 1.1)

# Des pastilles tres brillantes au fond : ce sont elles qui dessinent le
# diaphragme. Une lumiere n'est pas visible par la camera ; il faut de la
# geometrie. Un materiau matte donne une couleur constante, sans ombrage --
# a 80 en lineaire, chaque pastille devient une boule de bokeh franche.
glow = create("glow", "MaterialMatte")
if glow is not None:
    setvec(glow, "color", 80, 74, 62)

import math as _math
for index in range(22):
    a = index * 2.399963
    dot = create("dot_%d" % index, "GeometrySphere")
    setvec(dot, "translate",
           _math.cos(a) * (5.0 + index * 0.85),
           4.0 + _math.sin(a) * (3.2 + index * 0.35),
           -62.0 - (index % 5) * 5.0)
    if dot.get_attribute("radius") is not None:
        setv(dot, "radius", 0.13)
    if glow is not None and dot.get_attribute("materials") is not None:
        setv(dot, "materials", str(glow))

# -- une camera et une image par variante ------------------------------------
for name, overrides in VARIANTS:
    camera = create("cam_" + name, "CameraBokeh")
    if camera is None:
        continue
    setvec(camera, "translate", 0, 4.5, 26)
    setv(camera, "focal_length", 0.075)
    setvec(camera, "rotate", -6, 0, 0)

    settings = dict(BASE)
    settings.update(overrides)
    for key in sorted(settings):
        if camera.get_attribute(key) is None:
            say("  %s : pas d'attribut '%s'" % (name, key))
            continue
        setv(camera, key, settings[key])

    image = create("img_" + name, "Image")
    setv(image, "resolution_mode", 1)
    setvec(image, "resolution", 960, 540)
    setv(image, "resolution_multiplier", 2)

    ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
    layer = first_of(image, "layers")
    if layer is not None:
        # active_camera et renderer sont declares sur LayerScene, sans prefixe.
        # Le chemin `layer_3d.active_camera` n'existe pas -- il ne signale rien
        # et l'image sort vide.
        setv(layer, "active_camera", str(camera))
        setv(layer, "renderer", str(renderer))
        say("  %s : camera=%s renderer=%s"
            % (name,
               layer.get_attribute("active_camera").get_string(),
               layer.get_attribute("renderer").get_string()))

    path = os.path.join(OUT, name + ".project")
    ix.application.save_project(path)
    say("%s\t%s" % (name, str(image)))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
