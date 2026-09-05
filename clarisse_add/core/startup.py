# -*- coding: utf-8 -*-
"""Branche ClarisseAdd sur le demarrage de Clarisse.

Clarisse ne decouvre ses modules C++ qu'au lancement, en balayant les dossiers
donnes par ``-module_path``. Aucune variable d'environnement ne permet d'en
ajouter : ``app_env.h`` donne la liste exhaustive de celles qu'il reconnait, et
aucune ne concerne les modules. Il resterait donc a modifier le raccourci de
lancement -- en relistant le dossier ``module`` d'origine, que ``-module_path``
remplace au lieu de completer -- ou a cliquer un bouton a chaque session.

``CLARISSE_STARTUP_SCRIPT``, elle, est bien reconnue, et le script qu'elle
designe peut appeler ``AppObject::scan_modules``. Un reglage pose une fois, et
les classes natives sont la a chaque lancement.

Le format de ``clarisse.env`` est une ligne ``CLE=VALEUR`` par variable. La
valeur de ``CLARISSE_STARTUP_SCRIPT`` accepte plusieurs chemins separes par
``;`` : on ajoute le notre a ce qui s'y trouve deja, au lieu de l'ecraser.
"""
from __future__ import absolute_import

import io
import os
import shutil
import time

VARIABLE = "CLARISSE_STARTUP_SCRIPT"
SEPARATOR = ";"


HOOK_NAME = "clarisse_add_startup.py"

# Clarisse execute ses scripts de demarrage par PyRun_String : `__file__` n'y
# est pas defini, et un script pose dans le paquet ne saurait pas se situer.
# Le lanceur est donc genere, avec la racine de l'addon inscrite dedans.
HOOK_TEMPLATE = u"""# -*- coding: utf-8 -*-
# Genere par ClarisseAdd -- ne pas modifier, l'installeur le reecrit.
#
# Clarisse execute ce fichier au demarrage (CLARISSE_STARTUP_SCRIPT), par
# PyRun_String et non comme un fichier : `__file__` n'y existe pas, d'ou la
# racine en dur ci-dessous.
import sys

ROOT = r"%(root)s"
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

try:
    from clarisse_add import startup as _startup
    _startup.run(ix)          # noqa: F821 -- `ix` est fourni par Clarisse
except Exception as _error:   # le demarrage de Clarisse ne doit pas trebucher
    print("ClarisseAdd : demarrage abandonne (%%s)" %% _error)
"""


def addon_root():
    """La racine du depot, deduite de l'emplacement de ce module."""
    return os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))


def hook_script(config_dir):
    """Le lanceur, pose a cote du ``clarisse.env`` qu'il accompagne."""
    return os.path.join(config_dir, HOOK_NAME)


def write_hook(config_dir, root=None):
    """Ecrit le lanceur. Rend son chemin."""
    target = hook_script(config_dir)
    folder = os.path.dirname(target)
    if folder and not os.path.isdir(folder):
        os.makedirs(folder)
    body = HOOK_TEMPLATE % {"root": root or addon_root()}
    with io.open(target, "w", encoding="utf-8") as handle:
        handle.write(body)
    return target


def remove_hook(config_dir):
    """Supprime le lanceur. Rend True s'il existait."""
    target = hook_script(config_dir)
    if os.path.isfile(target):
        os.remove(target)
        return True
    return False


def read(env_file):
    """Les lignes du fichier, telles quelles. Rend [] s'il n'existe pas."""
    if not os.path.isfile(env_file):
        return []
    with io.open(env_file, encoding="utf-8", errors="replace") as handle:
        return handle.read().splitlines()


def _split(value):
    """Les chemins d'une valeur multiple, vides retires."""
    return [part.strip() for part in value.split(SEPARATOR) if part.strip()]


def scripts(env_file):
    """Les scripts de demarrage actuellement declares."""
    for line in read(env_file):
        if line.startswith(VARIABLE + "="):
            return _split(line[len(VARIABLE) + 1:])
    return []


def is_enabled(env_file, script):
    script = os.path.normcase(os.path.normpath(script))
    return any(os.path.normcase(os.path.normpath(path)) == script
               for path in scripts(env_file))


def _rewrite(env_file, wanted):
    """Ecrit la variable avec la liste voulue, en preservant le reste.

    La ligne est remplacee sur place quand elle existe -- meme vide, ce qui est
    le cas par defaut -- et ajoutee a la fin sinon. Les autres lignes ne sont
    pas touchees : ce fichier porte aussi les chemins Python de Clarisse, et
    les reecrire serait prendre un risque pour rien.
    """
    lines = read(env_file)
    value = SEPARATOR.join(wanted)
    replaced = False
    for index, line in enumerate(lines):
        if line.startswith(VARIABLE + "="):
            lines[index] = "%s=%s" % (VARIABLE, value)
            replaced = True
            break
    if not replaced:
        lines.append("%s=%s" % (VARIABLE, value))

    folder = os.path.dirname(env_file)
    if folder and not os.path.isdir(folder):
        os.makedirs(folder)
    with io.open(env_file, "w", encoding="utf-8", newline="\r\n") as handle:
        handle.write(u"\n".join(lines) + u"\n")


def backup(env_file):
    """Copie horodatee a cote du fichier. Rend son chemin, ou None."""
    if not os.path.isfile(env_file):
        return None
    stamp = time.strftime("%Y%m%d-%H%M%S")
    target = "%s.%s.bak" % (env_file, stamp)
    shutil.copy2(env_file, target)
    return target


def enable(env_file, script):
    """Ajoute le lanceur aux scripts de demarrage.

    Rend (change, sauvegarde). ``change`` est faux si rien n'etait a faire.
    """
    script = os.path.normpath(script)
    if is_enabled(env_file, script):
        return (False, None)
    saved = backup(env_file)
    _rewrite(env_file, scripts(env_file) + [script])
    return (True, saved)


def disable(env_file, script):
    """Retire le lanceur. Rend (change, sauvegarde)."""
    script = os.path.normcase(os.path.normpath(script))
    current = scripts(env_file)
    kept = [path for path in current
            if os.path.normcase(os.path.normpath(path)) != script]
    if len(kept) == len(current):
        return (False, None)
    saved = backup(env_file)
    _rewrite(env_file, kept)
    return (True, saved)
