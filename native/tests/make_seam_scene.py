# -*- coding: utf-8 -*-
"""Banc d'essai des coutures : un projet, quatre reglages, aucun lancer de rayon.

Le filtre s'applique a un layer, et un LayerFile suffit a le nourrir. Monter une
scene 3D pour eprouver un filtre d'image ajouterait un moteur de rendu, un
echantillonnage et un bruit -- trois choses qui coutent cher et qui n'ont rien a
voir avec ce qu'on cherche. Ici le rendu est une convolution et rien d'autre :
il dure quelques secondes.

Les quatre reglages ne sont pas des variantes esthetiques, ce sont quatre
soupcons :

- vignettage : le noyau est bati au centre de la TUILE, donc il change par
  marches sur la grille des tuiles ;
- aberration chromatique en dose NEGATIVE : le decalage par defaut (0.6, 1, 1)
  fait alors grandir le rayon du rouge, jusqu'au-dela de la marge que
  pre_filter a reservee autour de la tuile ;
- douceur : la jupe du fondu s'etend jusqu'a (1 + douceur) fois le rayon, la
  aussi au-dela de la marge ;
- lames creusees a trois lames : la forme elle-meme sort du disque circonscrit.

Un temoin sans filtre sert de reference : c'est la seule facon de dire qu'un
ecart vient du filtre et pas de la chaine d'affichage.
"""
import io
import os

OUT = r"J:\_WINDOWSTEMP\claude\seams"
REPORT = r"J:\_WINDOWSTEMP\claude\seams\scene.log"
SOURCE = r"J:\_WINDOWSTEMP\claude\seam_src.hdr"
SIZE = 512

# Rayon 120 : c'est une "valeur elevee" au sens ou Romain l'entend, et c'est
# la que tout se voit. Une boule couvre alors quatre tuiles de large, donc une
# discontinuite d'une tuile a l'autre traverse la boule au lieu de se cacher a
# son bord.
BASE = {
    "radius": 120.0, "blades": 0, "roundness": 0.0, "softness": 0.0,
    "anamorphism": 0.0, "gain": 0.0, "threshold": 1.0,
    "optical_vignetting": 0.0, "spherical_aberration": 0.0,
    "chromatic_aberration": 0.0, "preserve_exposure": "no",
}

VARIANTS = [
    ("t0_temoin", None),
    ("t1_vignettage", {"optical_vignetting": 1.0}),
    ("t2_chroma_negatif", {"chromatic_aberration": -0.5}),
    ("t3_douceur", {"softness": 0.5}),
    ("t4_lames_creusees", {"blades": 3, "roundness": -0.5}),
    # Le reglage tel qu'on l'utiliserait vraiment : une optique marquee, tous
    # les defauts ensemble.
    ("t5_optique_marquee", {"blades": 6, "optical_vignetting": 0.8,
                            "chromatic_aberration": -0.4, "softness": 0.3}),
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

say("source : %s (existe : %s)" % (SOURCE, os.path.isfile(SOURCE)))

for name, overrides in VARIANTS:
    image = ix.cmds.CreateObject(name, "Image", "Global", "project:/")
    # `resolution` est declare `read_only` dans le CID de Image : c'est le
    # passage en "User-defined" qui doit lever le verrou, et SetValues ne le
    # leve pas. Passer par l'attribut, verrou baisse a la main, marche --
    # "read-only n'est pas forcement definitif", comme pour la subdivision des
    # Polymesh. Sans cela l'image reste a la resolution des preferences du
    # projet, 1920x1080 ici, et la source est etiree.
    ix.cmds.SetValues([str(image) + ".resolution_mode"], ["1"])
    res = image.get_attribute("resolution")
    res.set_read_only(False)
    res.set_long(SIZE, 0)
    res.set_long(SIZE, 1)
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
        say("%-20s %s" % (name, ", ".join("%s=%s" % (k, overrides[k])
                                          for k in sorted(overrides))))
    else:
        say("%-20s aucun filtre" % name)

    res = image.get_attribute("resolution")
    say("   %s  %sx%s" % (str(image), res.get_long(0), res.get_long(1)))

ix.application.save_project(os.path.join(OUT, "seams.project"))
say("projet : %s" % os.path.join(OUT, "seams.project"))

io.open(REPORT, "w", encoding="utf-8").write(u"\n".join(lines))
