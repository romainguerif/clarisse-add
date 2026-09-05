# -*- coding: utf-8 -*-
"""Construit la scene minimale qui declenche le filtre de bokeh, et la sauve.

Pas de scene 3D : un Layer File suffit. Le filtre s'applique a un layer, et
peu importe d'ou viennent ses pixels. Monter une camera, une lumiere et une
sphere juste pour faire tourner une sonde, ce serait ajouter cinq sources de
panne a un test qui n'en demande aucune.

Deux pieges verifies ici :

- `ix.cmds.AddLayer` renvoie `None` **meme quand elle reussit**. Se fier a son
  retour fait conclure a un echec sur une operation parfaitement valide. La
  seule preuve, c'est de relire l'attribut `layers` de l'image.
- `SetValues` sur un attribut a plusieurs composantes veut un chemin par
  composante -- `resolution[0]`, `resolution[1]`. Passer deux valeurs a un
  chemin unique n'ecrit rien, sans rien signaler.

A lancer en cnode ou en clarisse.exe, peu importe : rien ici n'est propre a
une saveur.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\bokeh_test.project"
REPORT = r"J:\_WINDOWSTEMP\claude\bokeh_scene.log"
SOURCE = os.path.join(r"J:\_WINDOWSTEMP\claude", "bokeh_src.hdr")
NAME = "bokeh_test"
SIZE = 512

_lines = []


def say(text):
    _lines.append(text)
    print(text)


def first_of(item, attribute):
    """Le premier objet d'un attribut tableau, relu plutot que suppose."""
    attr = item.get_attribute(attribute)
    if attr is None:
        say("pas d'attribut '%s' sur %s" % (attribute, item.get_name()))
        return None
    count = attr.get_value_count()
    say("%-15s : %d entree(s)" % (attribute, count))
    if count == 0:
        return None
    try:
        return attr.get_object(0)
    except Exception as error:
        say("get_object(0) a leve : %s" % error)
        return None


image = ix.cmds.CreateObject(NAME, "Image", "Global", "project:/")
say("image           : %s" % image)

ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
ix.cmds.SetValues([str(image) + ".resolution[0]", str(image) + ".resolution[1]"],
                  [str(SIZE), str(SIZE)])
# 100 %. Par defaut le multiplicateur vaut 50 % et l'image est rendue a la
# moitie de sa taille : commode en production, deroutant pour une mesure.
ix.cmds.SetValues([str(image) + ".resolution_multiplier"], ["2"])

res = image.get_attribute("resolution")
say("resolution      : %s x %s   multiplicateur=%s"
    % (res.get_long(0), res.get_long(1),
       image.get_attribute("resolution_multiplier").get_string()))

ix.cmds.AddLayer(str(image) + ".layers", "LayerFile")
layer = first_of(image, "layers")
say("layer           : %s" % layer)

if layer is None:
    say("ECHEC : aucun layer dans l'image apres AddLayer")
else:
    # Une image de test avec de vraies hautes lumieres : six points a 60, deux
    # aplats gris, fond noir. Un aplat uniforme ne prouverait rien -- il donne
    # la meme valeur des deux cotes du filtre, quoi qu'on fasse.
    ix.cmds.SetValues([str(layer) + ".filename"], [SOURCE])
    say("filename        : %s" % layer.get_attribute("filename").get_string())
    say("le fichier existe : %s" % os.path.isfile(SOURCE))

    module = layer.get_module()
    try:
        added = module.add_filter("ImageFilterBokeh", "bokeh")
    except Exception as error:
        added = None
        say("add_filter a leve : %s" % error)
    say("add_filter rend : %s" % (added is not None))

    obj = first_of(layer, "filters")
    say("objet filtre    : %s" % obj)
    if obj is not None:
        for name, value in (("radius", "20.0"), ("blades", "6"),
                            ("threshold", "1.0"), ("gain", "4.0")):
            ix.cmds.SetValues([str(obj) + "." + name], [value])
            say("  %-12s = %s" % (name, obj.get_attribute(name).get_string()))
        say("radius relu     : %s" % obj.get_attribute("radius").get_double())

folder = os.path.dirname(OUT)
if not os.path.isdir(folder):
    os.makedirs(folder)
ix.application.save_project(OUT)
say("projet sauve    : %s" % OUT)

handle = io.open(REPORT, "w", encoding="utf-8")
handle.write(u"\n".join(_lines))
handle.close()
