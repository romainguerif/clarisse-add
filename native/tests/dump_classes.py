# -*- coding: utf-8 -*-
"""Ecrit la liste des classes declarees et leur etat de licence, puis quitte.

Lance par clarisse.exe -flavor <ifx|builder> -startup_script. On ecrit dans un
fichier plutot que sur la sortie standard : une application graphique ne rend
pas sa console de facon fiable.
"""
import io
import os

app = ix.application
try:
    flavor = app.get_flavor()
except Exception:
    flavor = "?"

rows = []
classes = app.get_factory().get_classes().get_classes("")
for i in range(len(classes)):
    cls = classes[i]
    try:
        locked = "1" if cls.is_under_licensed() else "0"
    except Exception:
        locked = "?"
    rows.append("%s\t%s\t%s" % (cls.get_name(), cls.get_base_name(), locked))

path = os.path.join(r"J:\_WINDOWSTEMP\claude", "classes_flavor_%s.tsv" % flavor)
handle = io.open(path, "w", encoding="utf-8")
handle.write(u"# flavor=%s  total=%d\n" % (flavor, len(rows)))
handle.write(u"\n".join(sorted(rows)))
handle.close()

for method in ("quit", "exit", "close"):
    if hasattr(app, method):
        try:
            getattr(app, method)()
            break
        except Exception:
            pass
