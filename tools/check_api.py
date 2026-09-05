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
    os.path.join(ROOT, "clarisse_add", "scripts"),
    os.path.join(ROOT, "clarisse_add", "presets"),
    os.path.join(ROOT, "clarisse_add", "bootstrap.py"),
]

_CMD_RE = re.compile(r"\bix\.cmds\.(\w+)")
_API_RE = re.compile(r"\bix\.api\.(\w+)")
_APP_RE = re.compile(r"\bix\.application\.(\w+)")
# `ix.foo(` mais ni `ix.api`, ni `ix.cmds`, ni `ix.application`, ni `ix.selection`
_HELPER_RE = re.compile(r"\bix\.(?!api\b|cmds\b|application\b|selection\b)(\w+)\s*\(")

#: Emplacement du module Python expose sous le nom ``ix`` dans les scripts.
#: C'est `clarisse_helper.py`, pas le binding SWIG : `ix.set_current_context`
#: y vit, alors que `ix.application.set_current_context` n'existe pas.
DEFAULT_HELPER = os.environ.get(
    "CLARISSE_PYTHON_DIR",
    "C:/Program Files/Isotropix/Clarisse 5.0 SP14/Clarisse/python3",
)

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
    """Ce que le code appelle, par famille : ``{famille: {nom: {fichiers}}}``."""
    found = {"cmds": {}, "api": {}, "application": {}, "helper": {}}
    patterns = [("cmds", _CMD_RE), ("api", _API_RE),
                ("application", _APP_RE), ("helper", _HELPER_RE)]
    for path in iter_sources():
        with io.open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
        relative = os.path.relpath(path, ROOT)
        for family, pattern in patterns:
            for name in pattern.findall(text):
                found[family].setdefault(name, set()).add(relative)
    return found


def helper_names(python_dir):
    """Fonctions et variables exposees par ``clarisse_helper.py``.

    On lit le fichier plutot que la documentation : c'est litteralement le
    module que Clarisse expose sous le nom ``ix`` dans un script de shelf, donc
    la seule source qui dise la verite sur ce que ``ix.quelquechose`` accepte.
    """
    path = os.path.join(python_dir, "clarisse_helper.py")
    if not os.path.isfile(path):
        return None
    with io.open(path, "r", encoding="utf-8", errors="ignore") as handle:
        text = handle.read()
    names = set(re.findall(r"^def (\w+)\s*\(", text, re.M))
    names.update(re.findall(r"^(\w+)\s*=", text, re.M))
    return names


def app_member_names(sdk):
    """Methodes de ``ClarisseApp`` et de ses classes de base."""
    names = set()
    for page in ("class_clarisse_app-members.html", "class_app_object-members.html",
                 "class_gui_app-members.html", "class_of_app-members.html"):
        path = os.path.join(sdk, page)
        if not os.path.isfile(path):
            continue
        with io.open(path, "r", encoding="utf-8", errors="ignore") as handle:
            names.update(re.findall(r"\b(\w+)\s*\(", _strip(handle.read())))
    return names


#: Methodes d'objets Python courants, qu'on ne peut pas distinguer d'un appel
#: sur un objet Clarisse sans inference de type.  La liste est courte a dessein :
#: mieux vaut quelques lignes de bruit qu'un vrai probleme masque.
_STDLIB_METHODS = frozenset("""
    addHandler setFormatter setLevel finditer match search sub fullmatch
    total_seconds isoformat timestamp
    startswith endswith strip lstrip rstrip lower upper title capitalize
    zfill splitlines rsplit setdefault extend reverse isalnum isdigit
    encode decode
""".split())


#: Racines speciales deplacees entre Clarisse 4 et 5, d'apres la page
#: "New Special Roots" du SDK.  Un chemin reste sur l'ancienne forme ne produit
#: pas d'erreur a l'import : il leve un LookupError au moment ou l'outil est
#: lance, donc au clic, en pleine session.  Deux scripts de la collection
#: portaient encore `project://default`.
LEGACY_ROOTS = {
    "project://default": "default:/",
    "project://widgets": "widgets:/",
    "project://tools": "tools:/",
}


def legacy_root_paths():
    """``{ancienne racine: [emplacements]}`` encore presents dans le code."""
    found = {}
    for path in iter_sources():
        with io.open(path, "r", encoding="utf-8") as handle:
            lines = handle.read().splitlines()
        relative = os.path.relpath(path, ROOT)
        for number, line in enumerate(lines, start=1):
            code = line.split("#", 1)[0]
            for old in LEGACY_ROOTS:
                if old in code:
                    found.setdefault(old, []).append("%s:%d" % (relative, number))
    return found


def sdk_all_members(sdk):
    """Tous les noms de membres documentes, toutes classes confondues."""
    names = set()
    for page in os.listdir(sdk):
        if not page.endswith("-members.html"):
            continue
        try:
            with io.open(os.path.join(sdk, page), "r", encoding="utf-8",
                         errors="ignore") as handle:
                names.update(re.findall(r"\b([a-z_][a-z0-9_]{2,})\s*\(",
                                        _strip(handle.read())))
        except (OSError, IOError):
            continue
    return names


def object_method_calls():
    """Methodes appelees sur des objets, hors modules importes et hors ``ix.*``.

    Une methode sur un objet Clarisse (``prefs.item_exists(...)``) ne peut pas
    etre verifiee sans savoir de quel type est l'objet, et Python n'a pas cette
    information avant l'execution.  On s'en approche par elimination : on ecarte
    les appels dont le receveur est un module importe (Python les validera de
    lui-meme), ceux passant par ``ix`` (verifies par famille plus haut), et les
    methodes definies dans l'addon.  Ce qui reste vient, pour l'essentiel, de
    l'API Clarisse.

    C'est ce filet qui manquait quand ``prefs.is_item_exist`` -- nom valide en
    Clarisse 4, disparu en 5 -- est passe jusque dans le shelf.
    """
    import ast

    ours = set()
    trees = {}
    for path in iter_sources():
        with io.open(path, "r", encoding="utf-8") as handle:
            source = handle.read()
        tree = ast.parse(source)
        trees[path] = tree
        for node in ast.walk(tree):
            if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
                ours.add(node.name)

    calls = {}
    for path, tree in trees.items():
        relative = os.path.relpath(path, ROOT)
        skip = {"self", "cls", "ix"}
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    skip.add((alias.asname or alias.name).split(".")[0])
            elif isinstance(node, ast.ImportFrom):
                for alias in node.names:
                    skip.add(alias.asname or alias.name)

        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            if not isinstance(node.func, ast.Attribute):
                continue
            base = node.func.value
            while isinstance(base, ast.Attribute):
                base = base.value
            if isinstance(base, ast.Name) and base.id in skip:
                continue
            name = node.func.attr
            if len(name) < 3 or name in ours or name in _STDLIB_METHODS:
                continue
            calls.setdefault(name, []).append("%s:%d" % (relative, node.lineno))
    return calls


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
    parser.add_argument("--helper", default=DEFAULT_HELPER,
                        help="dossier python3/ de Clarisse, ou vit "
                             "clarisse_helper.py (defaut : %s)" % DEFAULT_HELPER)
    parser.add_argument("--strict", action="store_true",
                        help="fait echouer aussi sur les methodes d'objet "
                             "absentes du SDK (indicatives par defaut)")
    args = parser.parse_args(argv)

    found = collect()
    commands, classes = found["cmds"], found["api"]
    print("Code analyse : %s"
          % ", ".join("%d %s" % (len(found[family]), family)
                      for family in ("cmds", "api", "application", "helper")))

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

    # ix.application.* -- c'est la famille qui manquait, et celle qui a laisse
    # passer `set_current_context` : la methode n'existe pas sur ClarisseApp,
    # elle est sur le module `ix` lui-meme.
    app_members = app_member_names(args.sdk)
    if len(app_members) < 50:
        print("Extraction douteuse : %d membre(s) de ClarisseApp." % len(app_members))
        return 1
    for name in sorted(found["application"]):
        if name not in app_members:
            print("  INCONNU  ix.application.%-21s (%s)"
                  % (name, ", ".join(sorted(found["application"][name]))))
            problems += 1

    # ix.* -- les fonctions de clarisse_helper.py, le module reellement expose
    # sous le nom `ix` dans un script de shelf.
    helpers = helper_names(args.helper)
    if helpers is None:
        print("clarisse_helper.py introuvable dans %s : ix.* non verifie."
              % args.helper)
    else:
        print("Helper : %d nom(s) dans clarisse_helper.py" % len(helpers))
        for name in sorted(found["helper"]):
            if name not in helpers:
                print("  INCONNU  ix.%-33s (%s)"
                      % (name, ", ".join(sorted(found["helper"][name]))))
                problems += 1

    # Racines speciales de Clarisse 4 : bloquant, la correction est mecanique.
    legacy = legacy_root_paths()
    for old in sorted(legacy):
        print("  ANCIEN   %-28s -> %s  (%s)"
              % (old, LEGACY_ROOTS[old], ", ".join(legacy[old][:3])))
        problems += 1

    # Methodes appelees sur des objets : indicatif, pas bloquant sauf --strict.
    all_members = sdk_all_members(args.sdk)
    unknown_methods = {}
    for name, places in object_method_calls().items():
        if name not in all_members:
            unknown_methods[name] = places
    if unknown_methods:
        print("\nMethodes appelees sur un objet et absentes du SDK "
              "(%d, a verifier) :" % len(unknown_methods))
        for name in sorted(unknown_methods):
            print("  %-26s %s" % (name, ", ".join(unknown_methods[name][:3])))
        if args.strict:
            problems += len(unknown_methods)

    if problems:
        print("\n%d nom(s) introuvable(s) dans le SDK %s" % (problems, args.sdk))
        return 1

    print("Tous les noms d'API sont documentes dans le SDK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
