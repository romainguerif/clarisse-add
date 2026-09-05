"""Rapport d'hygiene sur la scene courante.

Les scenes Clarisse pourrissent de facon previsible : des textures pointent
vers un disque qui n'est plus monte, des contextes restent vides apres un
nettoyage a moitie fait, des materiaux ne sont assignes a rien, des geometries
sortent en gris parce qu'elles n'ont jamais recu de materiau.  Aucun de ces
symptomes ne produit d'erreur : la scene s'ouvre, et le probleme se voit au
rendu, tard.

L'audit ne modifie rien.  Il liste, compte, et propose d'ecrire le rapport dans
un fichier texte pour le joindre a un ticket ou le comparer d'un jour a l'autre.
"""

import io
import time

from ..core import files, log, scene
from ..core.compat import get_ix


def run(payload=None):
    ix = get_ix()

    root = ix.application.get_factory().get_root()
    report = _audit(ix, root)
    text = _format(report)

    log.info("Audit : %d avertissement(s) sur %d objets"
             % (report["warning_count"], report["object_count"]))

    _show(ix, report, text)
    return True


# ---------------------------------------------------------------------------


def _audit(ix, root):
    report = {
        "object_count": 0,
        "context_count": 0,
        "missing_files": [],
        "unverifiable_files": [],
        "empty_contexts": [],
        "unassigned_materials": [],
        "geometries_without_material": [],
        "warning_count": 0,
    }

    contexts = [root] + scene.sub_contexts(root)
    report["context_count"] = len(contexts)

    for context in contexts:
        if context is not root and _is_empty(context):
            report["empty_contexts"].append(str(context))

    objects = list(scene.iter_objects(root))
    report["object_count"] = len(objects)

    for reference in files.iter_file_references(root):
        state = reference.exists()
        if state is False:
            report["missing_files"].append((reference.target, reference.value))
        elif state is None:
            report["unverifiable_files"].append((reference.target, reference.value))

    materials = [obj for obj in objects if obj.is_kindof("Material")]
    for material in materials:
        # `get_dependency_count` compte les attributs qui pointent vers cet
        # item : un materiau que rien ne reference ne sera jamais rendu.
        try:
            if material.get_dependency_count() == 0:
                report["unassigned_materials"].append(str(material))
        except Exception:
            # Toutes les classes n'exposent pas la dependance ; on ne bloque
            # pas l'audit pour autant.
            log.debug("Dependances indisponibles pour %s" % str(material))

    for obj in objects:
        if not obj.is_kindof("SceneObject"):
            continue
        attribute = obj.get_attribute("materials")
        if attribute is None:
            continue
        assigned = False
        for index in range(attribute.get_value_count()):
            if str(attribute.get_string(index)).strip():
                assigned = True
                break
        if not assigned:
            report["geometries_without_material"].append(str(obj))

    report["warning_count"] = (
        len(report["missing_files"])
        + len(report["empty_contexts"])
        + len(report["unassigned_materials"])
        + len(report["geometries_without_material"])
    )
    return report


def _is_empty(context):
    """Un contexte sans objet ni sous-contexte."""
    try:
        return context.get_object_count() == 0 and context.get_context_count() == 0
    except Exception:
        return False


def _format(report):
    lines = []
    lines.append("Audit de scene ClarisseAdd - %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
    lines.append("")
    lines.append("%d objets, %d contextes" % (report["object_count"], report["context_count"]))
    lines.append("")

    sections = [
        ("Fichiers introuvables", report["missing_files"], _format_file),
        ("Contextes vides", report["empty_contexts"], None),
        ("Materiaux non references", report["unassigned_materials"], None),
        ("Geometries sans materiau", report["geometries_without_material"], None),
        ("Chemins non verifiables (sequence ou variable)",
         report["unverifiable_files"], _format_file),
    ]

    for title, items, formatter in sections:
        lines.append("--- %s : %d" % (title, len(items)))
        if not items:
            lines.append("    (aucun)")
        for item in items:
            lines.append("    " + (formatter(item) if formatter else str(item)))
        lines.append("")

    return "\n".join(lines)


def _format_file(item):
    target, value = item
    return "%s\n        -> %s" % (target, value)


def _show(ix, report, text):  # pragma: no cover - GUI
    """Fenetre de resultat : resume, liste deroulante, export."""
    summary = [
        "%d objets, %d contextes" % (report["object_count"], report["context_count"]),
        "%d fichier(s) introuvable(s)" % len(report["missing_files"]),
        "%d contexte(s) vide(s)" % len(report["empty_contexts"]),
        "%d materiau(x) non reference(s)" % len(report["unassigned_materials"]),
        "%d geometrie(s) sans materiau" % len(report["geometries_without_material"]),
    ]

    width, height = 780, 520
    window = ix.api.GuiWindow(ix.application.get_event_window(), 400, 200, width, height)
    window.set_title("ClarisseAdd - Scene Audit")
    panel = ix.api.GuiPanel(window, 0, 0, width, height)
    panel.set_constraints(
        ix.api.GuiWidget.CONSTRAINT_LEFT, ix.api.GuiWidget.CONSTRAINT_TOP,
        ix.api.GuiWidget.CONSTRAINT_RIGHT, ix.api.GuiWidget.CONSTRAINT_BOTTOM,
    )

    for row, line in enumerate(summary):
        label = ix.api.GuiLabel(panel, 12, 12 + row * 20, width - 24, 20, line)
        if row == 0:
            label.set_text_color(ix.api.GMathVec3uc(150, 150, 150))

    listing = ix.api.GuiListView(panel, 12, 12 + len(summary) * 20 + 8,
                                 width - 24, height - len(summary) * 20 - 84)
    listing.set_mouse_over_selection(False)
    for line in text.splitlines():
        listing.add_item(line if line.strip() else " ")

    button_y = height - 44
    close_button = ix.api.GuiPushButton(panel, 12, button_y, 110, 24, "Fermer")
    select_button = ix.api.GuiPushButton(panel, 300, button_y, 210, 24,
                                         "Selectionner les fautifs")
    export_button = ix.api.GuiPushButton(panel, 520, button_y, 246, 24,
                                         "Exporter le rapport...")
    select_button.set_tooltip(
        "Selectionne dans la scene les objets dont un fichier est introuvable."
    )

    class _Events(ix.api.EventObject):
        def close(self, sender, evtid):
            sender.get_window().hide()

        def export(self, sender, evtid):
            path = ix.api.GuiWidget.save_file(
                ix.application, "", "Enregistrer le rapport d'audit"
            )
            if not path:
                return
            path = str(path)
            if not path.lower().endswith(".txt"):
                path += ".txt"
            try:
                with io.open(path, "w", encoding="utf-8") as handle:
                    handle.write(text)
                log.info("Rapport d'audit ecrit dans %s" % path)
            except (OSError, IOError):
                log.exception("Ecriture du rapport dans %s" % path)

        def select(self, sender, evtid):
            ix.selection.deselect_all()
            seen = set()
            for target, _value in report["missing_files"]:
                object_path = target.rsplit(".", 1)[0]
                if object_path in seen:
                    continue
                seen.add(object_path)
                item = ix.item_exists(object_path)
                if item is not None:
                    ix.selection.add(item)
            log.info("%d objet(s) fautif(s) selectionne(s)" % len(seen))

    events = _Events()
    events.connect(close_button, "EVT_ID_PUSH_BUTTON_CLICK", events.close)
    events.connect(export_button, "EVT_ID_PUSH_BUTTON_CLICK", events.export)
    events.connect(select_button, "EVT_ID_PUSH_BUTTON_CLICK", events.select)

    window.show()
    while window.is_shown():
        ix.application.check_for_events()
    window.destroy()
