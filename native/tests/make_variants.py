# -*- coding: utf-8 -*-
"""Ecrit un projet par variante de reglage, pour comparer les rendus.

Un seul lancement de cnode suffit a produire tous les projets ; les rendus se
font ensuite un par un. Rendre visible chaque parametre isolement est le seul
moyen de savoir qu'il fait ce que sa documentation promet -- et de voir tout
de suite quand il ne le fait pas.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\variants"
REPORT = r"J:\_WINDOWSTEMP\claude\variants.log"
SOURCE = os.path.join(r"J:\_WINDOWSTEMP\claude", "bokeh_src.hdr")

BASE = {"radius": 26.0, "blades": 0, "rotation": 0.0, "roundness": 0.0,
        "anamorphism": 0.0, "threshold": 1.0, "gain": 1.0,
        "optical_vignetting": 0.0, "spherical_aberration": 0.0,
        "chromatic_aberration": 0.0}

VARIANTS = [
    ("00_source", None),
    ("01_disque", {}),
    ("02_six_lames", {"blades": 6}),
    ("03_lames_bombees", {"blades": 6, "roundness": 0.6}),
    ("04_lames_concaves", {"blades": 6, "roundness": -0.7}),
    ("05_rotation", {"blades": 5, "rotation": 0.6}),
    ("06_bulle_de_savon", {"spherical_aberration": 0.9}),
    ("07_bokeh_cremeux", {"spherical_aberration": -0.9}),
    ("08_oeil_de_chat", {"optical_vignetting": 0.7}),
    ("09_aberration_chroma", {"chromatic_aberration": 0.8}),
    ("10_anamorphique", {"anamorphism": 0.6}),
    ("11_gain_artistique", {"blades": 6, "gain": 6.0, "threshold": 1.0}),
]

lines = []


def say(text):
    lines.append(text)
    print(text)


def first_of(item, attribute):
    attr = item.get_attribute(attribute)
    if attr is None or attr.get_value_count() == 0:
        return None
    return attr.get_object(0)


if not os.path.isdir(OUT):
    os.makedirs(OUT)

# Chaque image porte le nom de sa variante. Reutiliser un nom unique en
# comptant sur DeleteItems ne marche pas : la suppression echoue en silence,
# les images s'empilent en bokeh_test1, bokeh_test2..., et le rendu vise
# toujours la premiere -- celle sans filtre. Les projets se suivent donc en
# grossissant, ce qui n'a aucune importance pour un banc d'essai.
for name, overrides in VARIANTS:
    image = ix.cmds.CreateObject(name, "Image", "Global", "project:/")
    ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
    ix.cmds.SetValues([str(image) + ".resolution_multiplier"], ["2"])

    ix.cmds.AddLayer(str(image) + ".layers", "LayerFile")
    layer = first_of(image, "layers")
    ix.cmds.SetValues([str(layer) + ".filename"], [SOURCE])

    if overrides is not None:
        layer.get_module().add_filter("ImageFilterBokeh", "bokeh")
        obj = first_of(layer, "filters")
        settings = dict(BASE)
        settings.update(overrides)
        for key in sorted(settings):
            ix.cmds.SetValues([str(obj) + "." + key], [str(settings[key])])

    path = os.path.join(OUT, name + ".project")
    ix.application.save_project(path)
    say("%s	%s" % (name, str(image)))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
