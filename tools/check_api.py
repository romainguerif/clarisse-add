"""Verifie les noms d'API Clarisse utilises par l'addon contre la doc du SDK.

    python tools/check_api.py [--sdk J:/Clarisse-SDK/docs/sdk]

Clarisse ne peut pas etre pilote hors interface sans licence CNode : impossible
donc d'executer le code de l'addon en dehors de l'application.  Or une faute de
frappe dans ``ix.cmds.RenameItems`` ou ``ix.api.GuiListview`` ne se voit qu'au
clic, en pleine session, sous forme de bouton qui ne fait rien.

Ce script comble le trou par une verification statique : il releve tous les
``ix.cmds.X`` et ``ix.api.Y`` du code, et les confronte a la documentation
Doxygen hors ligne livree avec Clarisse.  Ce n'est pas une preuve que le code
marche -- les arguments ne sont pas verifies -- mais un nom inexistant est
attrape avant d'arriver dans le shelf.

Le chemin du SDK peut aussi venir de la variable ``CLARISSE_SDK_DOCS``.  Sans
SDK accessible, le script s'arrete proprement : il est optionnel, et n'est pas
appele par la suite de tests.
"""

from __future__ import print_function

import argparse
import html
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

DEFAULT_SDK = os.environ.get("CLARISSE_SDK_DOCS", "J:/Clarisse-SDK/docs/sdk")

#: Dossiers analyses. Le code vendorise est exclu : il n'est pas de notre
#: ressort, et il vise aussi Clarisse 4.
SOURCES = [
    os.path.join(ROOT, "clarisse_add", "core"),
    os.path.join(ROOT, "clarisse_add", "tools"),
    os.path.join(ROOT, "clarisse_add", "bootstrap.py"),
]

_CMD_RE = re.compile(r"\bix\.cmds\.(\w+)")
_API_RE = re.compile(r"\bix\.api\.(\w+)")

#: Noms qui ne sont pas des classes mais des constantes ou des sous-modules,
#: et qu'on retrouve donc autrement dans la documentation.
_API_MEMBERS = re.compile(r"^(OfAttr|AppDialog|GuiWidget|GuiPushButton|AppPreferences)$")


def iter_sources():
    for source in SOURCES:
        if os.path.isfile(source):
            yield source
        elif os.path.isdir(source):
            for name in sorted(os.listdir(source)):
                if name.endswith(".py"):
                    yield os.path.join(source, name)


def collect():
    """``(commandes, classes)`` referencees par le code de l'addon."""
    commands, classes = {}, {}
    for path in iter_sources():
        with io.open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
        relative = os.path.relpath(path, ROOT)
        for name in _CMD_RE.findall(text):
            commands.setdefault(name, set()).add(relative)
        for name in _API_RE.findall(text):
            classes.setdefault(name, set()).add(relative)
    return commands, classes


def sdk_command_names(sdk):
    path = os.path.join(sdk, "namespacecmds.html")
    if not os.path.isfile(path):
        return None
    with io.open(path, "r", encoding="utf-8", errors="ignore") as handle:
        text = handle.read()
    return set(re.findall(r"def (\w+)\s*\(", _strip(text)))


def sdk_class_names(sdk):
    """Les classes documentees, lues depuis l'index Doxygen des fichiers."""
    names = set()
    # Doxygen genere un `class_gui_list_view.html` par classe `GuiListView`.
    for name in os.listdir(sdk):
        match = re.match(r"^class_([a-z0-9_]+)\.html$", name)
        if match:
            names.add(match.group(1).replace("_", ""))
    return names


def sdk_mentions(sdk, wanted):
    """Les noms de ``wanted`` qui apparaissent quelque part dans le SDK.

    Second filet, pour les identifiants qui n'ont pas de page de classe :
    ``GMathVec3uc`` et ``OfObjectArray`` sont des typedefs, Doxygen ne leur
    genere donc pas de ``class_*.html``, alors qu'ils sont bien exposes en
    Python.  Sans cette passe, le verificateur crierait au loup sur du code
    parfaitement valide -- et un verificateur qui donne de faux positifs finit
    par ne plus etre lance du tout.
    """
    if not wanted:
        return set()
    pattern = re.compile(r"\b(%s)\b" % "|".join(re.escape(name) for name in wanted))
    found = set()
    for name in os.listdir(sdk):
        if not name.endswith(".html"):
            continue
        try:
            with io.open(os.path.join(sdk, name), "r", encoding="utf-8",
                         errors="ignore") as handle:
                found.update(pattern.findall(handle.read()))
        except (OSError, IOError):
            continue
        if found == wanted:
            break
    return found


def _strip(text):
    """Texte brut d'une page Doxygen.

    Le passage par ``html.unescape`` n'est pas cosmetique : Doxygen ecrit les
    signatures avec des espaces insecables (``def&#160;RenameItem&#160;(``).
    Sans decodage, aucune signature ne correspond au motif, l'ensemble des
    commandes connues ressort vide, et la verification passe en silence sans
    rien verifier du tout.
    """
    text = re.sub(r"<script.*?</script>", " ", text, flags=re.S)
    text = re.sub(r"<[^>]+>", " ", text)
    text = html.unescape(text)
    text = text.replace(u"\xa0", " ")
    return " ".join(text.split())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sdk", default=DEFAULT_SDK,
                        help="dossier docs/sdk du SDK Clarisse (defaut : %s)" % DEFAULT_SDK)
    args = parser.parse_args(argv)

    commands, classes = collect()
    print("Code analyse : %d commande(s), %d classe(s) referencees"
          % (len(commands), len(classes)))

    if not os.path.isdir(args.sdk):
        print("SDK introuvable dans %s : verification impossible." % args.sdk)
        print("Indiquez --sdk ou definissez CLARISSE_SDK_DOCS.")
        return 0

    known_commands = sdk_command_names(args.sdk)
    known_classes = sdk_class_names(args.sdk)
    if known_commands is None:
        print("namespacecmds.html absent du SDK : commandes non verifiees.")
        return 1

    # Garde-fou : si l'extraction rate, l'ensemble ressort vide ou minuscule et
    # tout passe pour valide. Un verificateur qui ne verifie rien en silence est
    # pire que pas de verificateur -- c'est exactement ce qui s'est produit ici,
    # les espaces insecables de Doxygen faisant echouer chaque signature.
    if len(known_commands) < 100:
        print("Extraction douteuse : %d commande(s) seulement lues dans "
              "namespacecmds.html (attendu : environ 200). Le format de la "
              "documentation a change." % len(known_commands))
        return 1
    if len(known_classes) < 100:
        print("Extraction douteuse : %d classe(s) seulement dans le SDK."
              % len(known_classes))
        return 1

    print("SDK : %d commandes, %d classes documentees"
          % (len(known_commands), len(known_classes)))

    problems = 0

    for name in sorted(commands):
        if name not in known_commands:
            print("  INCONNU  ix.cmds.%-28s (%s)"
                  % (name, ", ".join(sorted(commands[name]))))
            problems += 1

    undocumented = {name for name in classes
                    if not _API_MEMBERS.match(name) and name.lower() not in known_classes}
    mentioned = sdk_mentions(args.sdk, undocumented)
    for name in sorted(undocumented):
        if name in mentioned:
            print("  typedef  ix.api.%-29s (pas de page de classe, mais documente)"
                  % name)
            continue
        print("  INCONNU  ix.api.%-29s (%s)"
              % (name, ", ".join(sorted(classes[name]))))
        problems += 1

    if problems:
        print("\n%d nom(s) introuvable(s) dans le SDK %s" % (problems, args.sdk))
        return 1

    print("Tous les noms d'API sont documentes dans le SDK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
