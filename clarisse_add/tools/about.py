"""Etat de l'addon : version, chemins, inventaire, acces au journal.

C'est le premier endroit ou regarder quand un bouton ne fait rien : le fichier
de log contient la pile complete de la derniere erreur.
"""

import os

from .. import __version__
from ..core import log, paths, ui
from ..core.compat import get_ix


def run(payload=None):
    ix = get_ix()
    from .. import manifest
    from ..presets import catalog

    tools = manifest.all_tools()
    presets = catalog.entries()
    missing = [entry for entry in presets if entry.missing_files]
    absent = [entry for entry in presets if not entry.exists()]

    lines = [
        "ClarisseAdd %s" % __version__,
        "",
        "%d outils dans %d categories" % (len(tools), len(manifest.categories())),
        "%d presets dans la bibliotheque" % len(presets),
    ]
    if absent:
        lines.append("  %d preset(s) dont le .project est introuvable" % len(absent))
    if missing:
        lines.append("  %d preset(s) referencant un fichier absent" % len(missing))
    lines.extend([
        "",
        "Addon      : %s" % paths.ADDON_ROOT,
        "Presets    : %s" % paths.PRESETS_DIR,
        "Journal    : %s" % paths.log_file(),
    ])

    versions = paths.installed_config_versions()
    if versions:
        lines.append("Config     : %s" % ", ".join(versions))

    log.info("A propos consulte (version %s)" % __version__)

    fields = [
        ui.Section("Etat"),
        ui.Text("log", "Journal", default=paths.log_file()),
        ui.Toggle("open_log", "Ouvrir le dossier du journal", default=False),
        ui.Toggle("open_addon", "Ouvrir le dossier de l'addon", default=False),
    ]
    ui.message("\n".join(lines), "ClarisseAdd")

    result = ui.Form(
        "ClarisseAdd - dossiers",
        fields,
        note="Cochez ce que vous voulez ouvrir dans l'explorateur.",
        accept_label="Ouvrir",
    ).run()
    if not result:
        return True
    if result.get("open_log"):
        ui.open_directory(os.path.dirname(paths.log_file()))
    if result.get("open_addon"):
        ui.open_directory(paths.ADDON_ROOT)
    del ix
    return True
