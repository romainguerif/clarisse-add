"""Reecriture en masse des chemins de fichiers de la scene.

Le cas courant : une scene montee depuis ``U:/projects/...`` est reprise sur une
autre machine ou tout est sous ``J:/LIBRARY3D/...``.  Rien n'est casse dans la
scene, seul le prefixe a change, mais il y a parfois plusieurs centaines
d'attributs a corriger.

L'outil montre d'abord ce qu'il va faire (combien de chemins touches, combien
existeront reellement apres substitution), et n'ecrit qu'apres validation.  Il
propose comme ancien prefixe les dossiers les plus frequents de la scene, ce qui
evite de les retaper.
"""

import os

from ..core import files, log, scene, ui
from ..core.compat import get_ix


def run(payload=None):
    ix = get_ix()

    root = ix.application.get_factory().get_root()
    references = list(files.iter_file_references(root))
    if not references:
        ui.message("Aucun fichier reference dans cette scene.", "Relink Files")
        return False

    missing = [reference for reference in references if reference.exists() is False]
    prefixes = files.common_prefixes(missing or references)

    choices = [("(saisir a la main)", "")] + [(item, item) for item in prefixes]

    settings = ui.Form(
        "Relink Files",
        [
            ui.Section("Remplacement"),
            ui.Choice("suggested", "Prefixes de la scene", choices, default=1 if prefixes else 0,
                      tooltip="Dossiers les plus frequents parmi les chemins "
                              "de la scene, les plus profonds en premier."),
            ui.Text("old", "Ancien prefixe", default=prefixes[0] if prefixes else ""),
            ui.FilePath("new", "Nouveau prefixe", default="", directory=True),
            ui.Section("Portee"),
            ui.Toggle("only_missing", "Seulement les fichiers introuvables",
                      default=bool(missing),
                      tooltip="Laisse tranquilles les chemins qui resolvent deja."),
            ui.Toggle("case_insensitive", "Ignorer la casse", default=os.name == "nt"),
        ],
        note="%d chemin(s) au total, dont %d introuvable(s)."
             % (len(references), len(missing)),
        accept_label="Previsualiser",
    ).run()
    if settings is None:
        return False

    old = (settings["suggested"] or settings["old"] or "").strip()
    new = (settings["new"] or "").strip()
    if not old:
        ui.message("Indiquez l'ancien prefixe a remplacer.", "Relink Files")
        return False
    if not new:
        ui.message("Indiquez le nouveau prefixe.", "Relink Files")
        return False

    scope = missing if settings["only_missing"] else references
    changes = _plan(scope, old, new, settings["case_insensitive"])

    if not changes:
        ui.message(
            "Aucun chemin ne commence par :\n%s\n\n"
            "Verifiez le prefixe, ou decochez 'seulement les fichiers "
            "introuvables'." % old,
            "Relink Files",
        )
        return False

    resolved = sum(1 for _reference, value in changes if os.path.exists(value))
    preview = "\n".join(
        "%s\n  -> %s" % (_shorten(reference.value), _shorten(value))
        for reference, value in changes[:8]
    )
    more = "\n(... et %d autres)" % (len(changes) - 8) if len(changes) > 8 else ""

    if not ui.confirm(
        "%d chemin(s) seront reecrits.\n"
        "%d pointeront vers un fichier existant apres substitution.\n\n"
        "%s%s\n\nAppliquer ?" % (len(changes), resolved, preview, more),
        "Relink Files",
    ):
        return False

    applied = 0
    with scene.command_batch("ClarisseAdd - Relink"):
        for reference, value in changes:
            try:
                ix.cmds.SetValues([reference.target], [value])
                applied += 1
            except Exception:
                log.exception("Reecriture de %s" % reference.target)

    log.info("Relink : %d chemin(s) reecrits (%s -> %s)" % (applied, old, new))
    ui.message("%d chemin(s) reecrits." % applied, "Relink Files")
    return True


def _plan(references, old, new, case_insensitive):
    """Liste ``(reference, nouveau chemin)`` pour les chemins concernes."""
    old_normalized = old.replace("\\", "/").rstrip("/")
    new_normalized = new.replace("\\", "/").rstrip("/")
    needle = old_normalized.lower() if case_insensitive else old_normalized

    changes = []
    for reference in references:
        current = reference.value.replace("\\", "/")
        haystack = current.lower() if case_insensitive else current
        if not haystack.startswith(needle):
            continue
        replacement = new_normalized + current[len(old_normalized):]
        if replacement != reference.value:
            changes.append((reference, replacement))
    return changes


def _shorten(path, width=64):
    if len(path) <= width:
        return path
    return "..." + path[-(width - 3):]
