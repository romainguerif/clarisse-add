# -*- coding: utf-8 -*-
"""Verifie clarisse_add.native_modules dans une vraie Clarisse."""
import io
import os
import sys

REPORT = r"J:\_WINDOWSTEMP\claude\native_modules.log"
ROOT = r"C:\Users\Anon\Desktop\ClarisseAdd"

lines = []


def say(text):
    lines.append(text)
    print(text)


if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

try:
    from clarisse_add.core.compat import set_ix
    from clarisse_add import native_modules

    set_ix(ix)
    say("modules disponibles : %s" % ", ".join(native_modules.available()))

    classes = ix.application.get_factory().get_classes()
    say("avant  : ImageFilterBokeh = %s" % bool(classes.exists("ImageFilterBokeh")))

    loaded, already = native_modules.load()
    say("load() : %d nouvelle(s) classe(s), %d deja presente(s)" % (loaded, already))

    found = bool(classes.exists("ImageFilterBokeh"))
    say("apres  : ImageFilterBokeh = %s" % found)
    if found:
        cls = classes.get("ImageFilterBokeh")
        say("  bibliotheque : %s" % cls.get_dso_filename())

    # Deuxieme appel : il ne doit rien redeclarer.
    loaded2, already2 = native_modules.load()
    say("2e load(): %d nouvelle(s), %d deja presente(s)" % (loaded2, already2))
except Exception as error:
    import traceback
    say("ECHEC : %s" % error)
    say(traceback.format_exc())

handle = io.open(REPORT, "w", encoding="utf-8")
handle.write(u"\n".join(lines))
handle.close()

for method in ("quit", "exit", "close"):
    if hasattr(ix.application, method):
        try:
            getattr(ix.application, method)()
            break
        except Exception:
            pass
