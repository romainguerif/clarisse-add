"""Renommage en masse de la selection.

Prefixe, suffixe, recherche/remplacement, numerotation, casse : les cinq
operations couvrent la quasi-totalite des renommages de production.  Elles
s'appliquent dans cet ordre, et l'apercu montre le resultat final avant
validation -- il n'y a pas d'annulation pratique quand on s'est trompe sur
deux cents objets.
"""

import re

from ..core import log, scene, ui
from ..core.compat import get_ix

CASE_MODES = [
    ("Inchangee", "none"),
    ("minuscules", "lower"),
    ("MAJUSCULES", "upper"),
    ("Capitalisee", "title"),
]


def run(payload=None):
    ix = get_ix()

    items = scene.selection()
    if not items:
        ui.message("Selectionnez les elements a renommer, puis relancez l'outil.",
                   "Batch Rename")
        return False

    settings = ui.Form(
        "Batch Rename",
        [
            ui.Section("Remplacement"),
            ui.Text("search", "Rechercher", default="",
                    tooltip="Laisser vide pour ne rien remplacer."),
            ui.Text("replace", "Remplacer par", default=""),
            ui.Toggle("regex", "Expression reguliere", default=False),
            ui.Section("Affixes"),
            ui.Text("prefix", "Prefixe", default=""),
            ui.Text("suffix", "Suffixe", default=""),
            ui.Section("Numerotation"),
            ui.Toggle("numbering", "Numeroter", default=False),
            ui.Number("start", "Premier numero", default=1, minimum=0, maximum=100000,
                      integer=True),
            ui.Number("padding", "Chiffres", default=3, minimum=1, maximum=8,
                      integer=True),
            ui.Section("Casse"),
            ui.Choice("case", "Casse", CASE_MODES, default=0),
        ],
        note="%d element(s) selectionne(s)." % len(items),
        accept_label="Previsualiser",
    ).run()
    if settings is None:
        return False

    try:
        plan = _plan(items, settings)
    except re.error as error:
        ui.message("Expression reguliere invalide :\n%s" % error, "Batch Rename")
        return False

    changes = [(item, name) for item, name in plan if name != item.get_name()]
    if not changes:
        ui.message("Aucun nom ne change avec ces reglages.", "Batch Rename")
        return False

    preview = "\n".join("%s  ->  %s" % (item.get_name(), name)
                        for item, name in changes[:10])
    more = "\n(... et %d autres)" % (len(changes) - 10) if len(changes) > 10 else ""

    if not ui.confirm("%d element(s) renomme(s) :\n\n%s%s\n\nAppliquer ?"
                      % (len(changes), preview, more), "Batch Rename"):
        return False

    renamed = 0
    with scene.command_batch("ClarisseAdd - Batch Rename"):
        for item, name in changes:
            try:
                ix.cmds.RenameItem(str(item), name)
                renamed += 1
            except Exception:
                log.exception("Renommage de %s en '%s'" % (str(item), name))

    log.info("Batch Rename : %d element(s) renomme(s)" % renamed)
    ui.message("%d element(s) renomme(s)." % renamed, "Batch Rename")
    return True


def _plan(items, settings):
    """``[(item, nouveau nom)]``, dans l'ordre de la selection."""
    search = settings["search"]
    replace = settings["replace"]
    pattern = re.compile(search) if (search and settings["regex"]) else None

    plan = []
    number = int(settings["start"])
    for item in items:
        name = item.get_name()

        if search:
            if pattern is not None:
                name = pattern.sub(replace, name)
            else:
                name = name.replace(search, replace)

        name = (settings["prefix"] or "") + name + (settings["suffix"] or "")

        if settings["numbering"]:
            name += str(number).zfill(int(settings["padding"]))
            number += 1

        mode = settings["case"]
        if mode == "lower":
            name = name.lower()
        elif mode == "upper":
            name = name.upper()
        elif mode == "title":
            name = name.title()

        plan.append((item, _sanitize(name)))
    return plan


def _sanitize(name):
    """Clarisse refuse les noms vides et les caracteres de chemin."""
    cleaned = name.replace("/", "_").replace("\\", "_").replace(".", "_").strip()
    return cleaned or "item"
