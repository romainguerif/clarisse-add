"""Navigateur de la bibliotheque de scenes ``.project``.

Le probleme que resout cet outil n'est pas technique : les vingt-trois scenes
existaient deja sur le disque, mais personne ne se souvient de ce qu'il y a
dedans ni ou elles sont, donc elles ne servent jamais.  Le navigateur les liste
par categorie, affiche ce que contient chacune (inventaire extrait du fichier
lui-meme) et la fusionne dans le contexte choisi.

Un preset porteur d'attributs custom enchaine ensuite sur sa fenetre de
reglages, via :mod:`clarisse_add.tools.preset_runner`.
"""

from ..core import log, ui
from ..core.compat import get_ix
from ..presets import catalog

WINDOW_WIDTH = 720
WINDOW_HEIGHT = 520

ALL_CATEGORIES = "Toutes les categories"


def run(payload=None):
    ix = get_ix()
    entries = catalog.entries()
    if not entries:
        ui.message(
            "Le catalogue est vide.\n\n"
            "Relancez 'python tools/build_catalog.py' depuis le dossier de "
            "l'addon pour l'indexer.",
            "Preset Browser",
        )
        return False

    Browser(ix, entries).show()
    return True


class Browser(object):
    """Fenetre du navigateur. L'etat vit dans l'instance, pas dans des globales."""

    def __init__(self, ix, entries):
        self.ix = ix
        self.entries = entries
        self.visible = []
        self.window = None
        self.list_view = None
        self.detail_labels = []
        self.category_button = None
        self.search_field = None

    # -- construction ------------------------------------------------------

    def show(self):  # pragma: no cover - GUI
        ix = self.ix
        parent = ix.application.get_event_window()
        self.window = ix.api.GuiWindow(parent, 420, 220, WINDOW_WIDTH, WINDOW_HEIGHT)
        self.window.set_title("ClarisseAdd - Preset Browser")

        panel = ix.api.GuiPanel(self.window, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT)
        panel.set_constraints(
            ix.api.GuiWidget.CONSTRAINT_LEFT, ix.api.GuiWidget.CONSTRAINT_TOP,
            ix.api.GuiWidget.CONSTRAINT_RIGHT, ix.api.GuiWidget.CONSTRAINT_BOTTOM,
        )

        ix.api.GuiLabel(panel, 12, 12, 80, 22, "Categorie :")
        self.category_button = ix.api.GuiListButton(panel, 96, 12, 200, 22)
        self.category_button.add_item(ALL_CATEGORIES)
        for category in catalog.categories():
            self.category_button.add_item(category)
        self.category_button.set_selected_item_by_index(0)

        ix.api.GuiLabel(panel, 312, 12, 60, 22, "Filtre :")
        self.search_field = ix.api.GuiLineEdit(panel, 372, 12, 230, 22)
        search_button = ix.api.GuiPushButton(panel, 610, 12, 96, 22, "Filtrer")

        self.list_view = ix.api.GuiListView(panel, 12, 46, 320, WINDOW_HEIGHT - 110)
        self.list_view.set_mouse_over_selection(False)

        detail_x = 344
        detail_width = WINDOW_WIDTH - detail_x - 12
        self.detail_labels = []
        for row in range(11):
            label = ix.api.GuiLabel(panel, detail_x, 46 + row * 20, detail_width, 20, "")
            if row == 0:
                label.set_text_color(ix.api.GMathVec3uc(220, 220, 220))
            elif row in (1, 2):
                label.set_text_color(ix.api.GMathVec3uc(150, 150, 150))
            self.detail_labels.append(label)

        button_y = WINDOW_HEIGHT - 54
        close_button = ix.api.GuiPushButton(panel, 12, button_y, 110, 24, "Fermer")
        merge_here = ix.api.GuiPushButton(panel, 300, button_y, 190, 24, "Fusionner ici")
        merge_into = ix.api.GuiPushButton(panel, 500, button_y, 206, 24, "Fusionner dans...")
        merge_here.set_tooltip("Fusionne dans le contexte courant de l'application.")
        merge_into.set_tooltip("Ouvre le selecteur de contexte avant de fusionner.")

        browser = self

        class _Events(ix.api.EventObject):
            def refresh(self, sender, evtid):
                browser.refresh()

            def selected(self, sender, evtid):
                browser.update_details()

            def close(self, sender, evtid):
                sender.get_window().hide()

            def merge_here(self, sender, evtid):
                browser.merge(pick_target=False)

            def merge_into(self, sender, evtid):
                browser.merge(pick_target=True)

        events = _Events()
        events.connect(self.category_button, "EVT_ID_LIST_BUTTON_SELECT", events.refresh)
        events.connect(search_button, "EVT_ID_PUSH_BUTTON_CLICK", events.refresh)
        events.connect(self.search_field, "EVT_ID_LINE_EDIT_CHANGED", events.refresh)
        events.connect(self.list_view, "EVT_ID_LIST_VIEW_SELECT", events.selected)
        events.connect(close_button, "EVT_ID_PUSH_BUTTON_CLICK", events.close)
        events.connect(merge_here, "EVT_ID_PUSH_BUTTON_CLICK", events.merge_here)
        events.connect(merge_into, "EVT_ID_PUSH_BUTTON_CLICK", events.merge_into)

        self.refresh()
        self.window.show()
        while self.window.is_shown():
            ix.application.check_for_events()
        self.window.destroy()

    # -- contenu -----------------------------------------------------------

    def refresh(self):  # pragma: no cover - GUI
        """Reconstruit la liste selon la categorie et le filtre courants."""
        category = str(self.category_button.get_selected_item_name())
        needle = str(self.search_field.get_text()).strip().lower()

        self.visible = []
        for entry in self.entries:
            if category != ALL_CATEGORIES and entry.category != category:
                continue
            if needle:
                haystack = " ".join([
                    entry.title, entry.description, entry.id, entry.credit,
                    " ".join(entry.classes),
                ]).lower()
                if needle not in haystack:
                    continue
            self.visible.append(entry)

        self.list_view.remove_all_items()
        for entry in self.visible:
            marks = []
            if entry.parameters:
                marks.append("%d reglages" % len(entry.parameters))
            if entry.missing_files:
                marks.append("fichiers absents")
            if not entry.exists():
                marks.append("INTROUVABLE")
            suffix = "  (%s)" % ", ".join(marks) if marks else ""
            self.list_view.add_item(entry.title + suffix)

        if self.visible:
            self.list_view.set_selected_index(0)
        self.update_details()

    def selected_entry(self):
        index = self.list_view.get_selected_index()
        if 0 <= index < len(self.visible):
            return self.visible[index]
        return None

    def update_details(self):  # pragma: no cover - GUI
        entry = self.selected_entry()
        rows = []
        if entry is None:
            rows = ["Aucun preset selectionne."]
        else:
            rows.append(entry.title)
            rows.append("%s%s" % (entry.category,
                                  "  -  %s" % entry.credit if entry.credit else ""))
            rows.append("")
            rows.extend(_wrap(entry.description, 58, 4))
            rows.append("")
            rows.append("%d objets : %s" % (entry.object_count, entry.summary()))
            if entry.parameters:
                rows.append("%d reglages exposes apres fusion" % len(entry.parameters))
            if entry.missing_files:
                rows.append("Fichiers absents : %d" % len(entry.missing_files))
                rows.extend("  " + _shorten(item, 54) for item in entry.missing_files[:2])
            if not entry.exists():
                rows.append("Fichier .project introuvable sur le disque.")

        for index, label in enumerate(self.detail_labels):
            # GuiLabel n'a pas de set_text : son texte est son "label".
            label.set_label(rows[index] if index < len(rows) else "")

    # -- action ------------------------------------------------------------

    def merge(self, pick_target):  # pragma: no cover - GUI
        from . import preset_runner

        entry = self.selected_entry()
        if entry is None:
            return
        if not entry.exists():
            ui.message("Le fichier de ce preset est introuvable :\n%s" % entry.path,
                       entry.title)
            return

        log.debug("Preset Browser : fusion de %s" % entry.id)
        if pick_target or entry.parameters:
            # preset_runner demande lui-meme le contexte, et enchaine sur les
            # reglages quand le preset en expose.
            preset_runner.run(entry.id)
        else:
            from ..core import scene
            with scene.command_batch("ClarisseAdd - %s" % entry.title):
                scene.merge_project(entry.path, scene.current_context())


# ---------------------------------------------------------------------------


def _wrap(text, width, max_lines):
    """Decoupe un texte en lignes courtes : l'API GUI n'a pas de retour auto."""
    words = text.split()
    lines, current = [], ""
    for word in words:
        candidate = (current + " " + word).strip()
        if len(candidate) > width and current:
            lines.append(current)
            current = word
            if len(lines) == max_lines:
                break
        else:
            current = candidate
    if current and len(lines) < max_lines:
        lines.append(current)
    while len(lines) < max_lines:
        lines.append("")
    return lines[:max_lines]


def _shorten(text, width):
    if len(text) <= width:
        return text
    return "..." + text[-(width - 3):]
