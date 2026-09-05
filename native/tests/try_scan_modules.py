# -*- coding: utf-8 -*-
"""Peut-on charger un module sans toucher a la ligne de commande ?

Il n'existe aucune variable d'environnement pour les chemins de modules : la
liste exhaustive de celles que Clarisse reconnait est dans app_env.h, et
aucune ne concerne les modules. Le seul mecanisme documente est l'argument
-module_path, ce qui oblige a modifier le raccourci.

Mais AppObject::scan_modules(CoreVector<CoreString>) est exposee en Python
(framework.py). Si elle fonctionne depuis un script de demarrage, alors
CLARISSE_STARTUP_SCRIPT -- qui, elle, est une variable reconnue -- suffit a
charger nos modules, et le raccourci reste intact.

La reserve connue : les scripts de demarrage tournent apres l'initialisation
de l'application, donc apres le scan initial des modules. Une classe
enregistree tard peut ne pas se propager a l'interface. C'est precisement ce
que ce test mesure.
"""
import io

REPORT = r"J:\_WINDOWSTEMP\claude\scan_modules.log"
FOLDER = r"C:\Users\Anon\Desktop\ClarisseAdd\native\build"
CLASS = "ImageFilterBokeh"

lines = []


def say(text):
    lines.append(text)
    print(text)


app = ix.application
classes = app.get_factory().get_classes()

say("avant  : %s declaree = %s" % (CLASS, classes.exists(CLASS)))
say("methode scan_modules presente : %s" % hasattr(app, "scan_modules"))

if hasattr(app, "scan_modules"):
    # scan_modules veut un CoreVector<CoreString>, pas une liste Python : le
    # binding SWIG ne convertit pas. Le type est expose sous le nom
    # CoreStringVector (python3/base.py:2110).
    try:
        paths = ix.api.CoreStringVector()
        paths.add(ix.api.CoreString(FOLDER))
        say("CoreStringVector construit, %d entree(s)" % paths.get_count())
        app.scan_modules(paths)
        say("scan_modules : passe")
    except Exception as error:
        say("scan_modules a leve : %s" % error)

found = classes.exists(CLASS)
say("apres  : %s declaree = %s" % (CLASS, found))

if found:
    cls = classes.get(CLASS)
    say("  venue de   : %s" % cls.get_dso_filename())
    say("  classe base: %s" % cls.get_base_name())
    say("  categorie  : %s" % cls.get_category())

handle = io.open(REPORT, "w", encoding="utf-8")
handle.write(u"\n".join(lines))
handle.close()

for method in ("quit", "exit", "close"):
    if hasattr(app, method):
        try:
            getattr(app, method)()
            break
        except Exception:
            pass
