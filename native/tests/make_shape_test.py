# -*- coding: utf-8 -*-
"""Test minimal de la FORME du diaphragme.

Un seul point tres brillant, tres loin de la mise au point, sur fond noir. La
tache qu'il produit est, litteralement, l'image de l'ouverture : c'est le seul
montage ou la forme se lit sans ambiguite. Tout le reste -- sol, rangee de
spheres, eclairage -- ne ferait que masquer ce qu'on cherche a voir.
"""
import io
import math
import os

OUT = r"J:\_WINDOWSTEMP\claude\shape"
REPORT = r"J:\_WINDOWSTEMP\claude\shape.log"

BASE = {"enable_dof": "1", "f_stop": "1.0", "focus_distance": "80",
        "enable_bokeh": "1", "blades": "0", "blade_rotation": "0.0",
        "blade_curvature": "0.0", "anamorphism": "0.0",
        "optical_vignetting": "0.0", "spherical_aberration": "0.0",
        "aperture_swirl": "0.0"}

VARIANTS = [
    ("01_disque", {}),
    ("02_six_lames", {"blades": "6"}),
    ("03_cinq_lames", {"blades": "5", "blade_rotation": "0.3"}),
    ("04_lames_bombees", {"blades": "6", "blade_curvature": "0.7"}),
    ("05_lames_concaves", {"blades": "6", "blade_curvature": "-0.8"}),
    ("06_bulle_de_savon", {"spherical_aberration": "0.95"}),
    ("07_cremeux", {"spherical_aberration": "-0.95"}),
    ("08_anamorphique", {"anamorphism": "0.6"}),
    ("09_oeil_de_chat", {"optical_vignetting": "0.8"}),
    ("10_tourbillon", {"blades": "6", "aperture_swirl": "1.0"}),
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
# Le defaut est 9 echantillons par pixel, donc 9 points de lentille : la tache
# de bokeh sort criblee de bruit et sa forme devient illisible. Un flou de mise
# au point coute des echantillons, il n'y a pas d'echappatoire.
setv(renderer, "anti_aliasing_sample_count", 400)

# Materiau matte : couleur constante, sans ombrage ni lumiere necessaire.
# C'est ce qui garantit un point d'une luminance connue.
glow = create("glow", "MaterialMatte")
setvec(glow, "color", 200, 190, 160)
say("glow color : %s" % glow.get_attribute("color").get_string())

# Cinq pastilles reparties dans le cadre : une au centre, quatre vers les
# bords, pour que le vignettage optique et le tourbillon soient visibles.
# Les pastilles sont juste devant l'objectif et la mise au point est a
# l'infini : c'est la configuration ou le cercle de confusion est maximal, donc
# celle ou la forme de l'ouverture se lit le mieux. Elles restent dans le champ
# -- a 1,2 m d'un 50 mm, la demi-largeur vaut 43 cm.
places = [(0.0, 0.0), (-0.30, 0.17), (0.30, 0.17), (-0.30, -0.17), (0.30, -0.17)]
for index, (px, py) in enumerate(places):
    dot = create("dot_%d" % index, "GeometrySphere")
    setvec(dot, "translate", px, py, 6.8)
    setv(dot, "radius", 0.004)
    attr = dot.get_attribute("override_material")
    if attr is None:
        say("dot : pas d'attribut 'materials'")
    else:
        setv(dot, "override_material", str(glow))
        say("dot_%d materials : %s" % (index, attr.get_string()))

for name, overrides in VARIANTS:
    camera = create("cam_" + name, "CameraBokeh")
    setvec(camera, "translate", 0, 0, 8)
    setv(camera, "focal_length", 0.05)

    settings = dict(BASE)
    settings.update(overrides)
    for key in sorted(settings):
        setv(camera, key, settings[key])

    image = create("img_" + name, "Image")
    setv(image, "resolution_mode", 1)
    setvec(image, "resolution", 700, 700)
    setv(image, "resolution_multiplier", 2)

    ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
    layer = first_of(image, "layers")
    setv(layer, "active_camera", str(camera))
    setv(layer, "renderer", str(renderer))

    ix.application.save_project(os.path.join(OUT, name + ".project"))
    say("%s\t%s" % (name, str(image)))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
