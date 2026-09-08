# -*- coding: utf-8 -*-
"""Inventaire de ce que Clarisse possede deja qui touche au CSG.

Question posee : y a-t-il des booleens, un combiner, des implicites, des
volumes, du displacement, un sculpt ? On ne suppose rien, on lit le factory.

Lance par cnode -script. La sortie standard de cnode est noyee sous les
avertissements OCIO, donc on ecrit dans un fichier.

    cnode.exe empty.project -script csg_probe.py
"""
from __future__ import print_function

import io
import os

OUT = os.path.join(r"J:\_WINDOWSTEMP\claude", "csg_probe.txt")

app = ix.application
classes = app.get_factory().get_classes()
all_classes = classes.get_classes("")

lines = []


def say(text=""):
    lines.append(text)


# --- l'arbre d'heritage complet, pour savoir qui descend de quoi -------------

children = {}
info = {}
for i in range(len(all_classes)):
    cls = all_classes[i]
    name = cls.get_name()
    base = cls.get_base_name()
    try:
        locked = cls.is_under_licensed()
    except Exception:
        locked = None
    try:
        category = cls.get_category()
    except Exception:
        category = "?"
    info[name] = (base, category, locked)
    children.setdefault(base, []).append(name)

say("total de classes declarees : %d" % len(all_classes))
say("")


def tree(root, depth=0):
    if root in info:
        base, category, locked = info[root]
        say("%s%-34s  %-24s %s"
            % ("  " * depth, root, category or "-", "VERROUILLEE" if locked else ""))
    else:
        say("%s%s" % ("  " * depth, root))
    for kid in sorted(children.get(root, [])):
        tree(kid, depth + 1)


say("=" * 78)
say("ARBRE DES GEOMETRIES")
say("=" * 78)
tree("SceneObject")
say("")

say("=" * 78)
say("ARBRE DES DEFORMERS / DISPLACEMENTS")
say("=" * 78)
for root in ("Deformer", "Displacement", "Tool"):
    tree(root)
    say("")

# --- recherche par mot-cle, sur les noms de classes -------------------------

say("=" * 78)
say("RECHERCHE PAR MOT-CLE DANS LES NOMS DE CLASSES")
say("=" * 78)
KEYWORDS = ("bool", "csg", "combin", "implicit", "sdf", "distance", "level",
            "vdb", "volume", "sculpt", "carve", "subtract", "union",
            "intersect", "clip", "cut", "trim", "meta", "blob", "iso",
            "voxel", "grid", "solid", "shell", "remesh", "merge")
for key in KEYWORDS:
    hits = sorted(n for n in info if key in n.lower())
    say("%-12s %s" % (key, ", ".join(hits) if hits else "(rien)"))
say("")


# --- les attributs d'une classe, lus sur son prototype ----------------------

def dump_attributes(name):
    say("-" * 78)
    if not classes.exists(name):
        say("%s : LA CLASSE N'EXISTE PAS" % name)
        return
    cls = classes.get(name)
    base, category, locked = info.get(name, ("?", "?", None))
    say("%s   (base %s, categorie %s%s)"
        % (name, base, category or "-", ", VERROUILLEE" if locked else ""))
    try:
        say("  dso : %s" % cls.get_dso_filename())
    except Exception:
        pass
    proto = cls.get_proto()
    try:
        count = proto.get_attribute_count()
    except Exception:
        say("  (attributs illisibles)")
        return
    for i in range(count):
        attr = proto.get_attribute(i)
        try:
            attr_name = attr.get_name()
        except Exception:
            continue
        bits = []
        try:
            bits.append(attr.get_type_name(attr.get_type()))
        except Exception:
            pass
        try:
            if attr.is_hidden():
                bits.append("hidden")
        except Exception:
            pass
        try:
            if attr.get_value_count() > 1:
                bits.append("x%d" % attr.get_value_count())
        except Exception:
            pass
        value = ""
        try:
            value = attr.get_string()
        except Exception:
            pass
        group = ""
        try:
            group = attr.get_group()
        except Exception:
            pass
        say("    %-30s %-22s %-16s %s"
            % (attr_name, "/".join(bits), group or "", value))


say("=" * 78)
say("ATTRIBUTS DES CLASSES QUI COMPTENT")
say("=" * 78)
for name in ("SceneObjectCombiner", "GeometrySphere", "GeometryBox",
             "GeometryCylinder", "GeometryPolysphere", "GeometryVolume",
             "GeometryVolumeFile", "GeometryVolumeBox", "GeometryVolumeSurface",
             "GeometryPointVolume", "Displacement", "DeformerDisplacement",
             "Geometry", "GeometryPolymesh", "SceneObjectScatterer",
             "GeometryBundle", "SceneObjectTree", "Deformer"):
    dump_attributes(name)

handle = io.open(OUT, "w", encoding="utf-8")
handle.write(u"\n".join(lines))
handle.close()
print("csg_probe : ecrit %s (%d lignes)" % (OUT, len(lines)))
