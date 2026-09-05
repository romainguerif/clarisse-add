"""Construction de fenetres de reglages a partir d'une description.

L'API GUI de Clarisse est imperative et positionnee au pixel : chaque outil du
kit de survie reecrit deux cents lignes de ``GuiLabel(panel, 10, offset_y, ...)``
avec ses propres constantes d'espacement.  Resultat : chaque fenetre a une tete
differente, et ajouter un champ oblige a decaler tout ce qui suit a la main.

Ici on decrit le formulaire::

    fields = [
        Section("Geometrie"),
        Number("rows", "Rangees", default=6, minimum=1, maximum=200, integer=True),
        Number("height", "Hauteur", default=2.5, minimum=0.01, maximum=100.0),
        Choice("mode", "Mode", ["Brique", "Pierre"], default=0),
        Toggle("cap", "Chapeau", default=True),
    ]
    values = Form("Wall Maker", fields).run()

et la mise en page, la boucle modale et la recuperation des valeurs sont faites
une fois pour toutes.  ``run()`` renvoie ``None`` si l'utilisateur annule ou
ferme la fenetre — a distinguer d'un formulaire valide sans champ, qui renvoie
un dictionnaire vide.
"""

import os

from . import log
from .compat import get_ix

__all__ = [
    "Section",
    "Text",
    "Number",
    "Toggle",
    "Choice",
    "FilePath",
    "Form",
    "pick_context",
    "message",
    "confirm",
]

# Metrique commune a toutes les fenetres de l'addon.
ROW_HEIGHT = 22
ROW_SPACING = 28
MARGIN = 12
LABEL_WIDTH = 170
FIELD_WIDTH = 200
SECTION_SPACING = 34


# ---------------------------------------------------------------------------
# Description des champs
# ---------------------------------------------------------------------------


class _Field(object):
    """Base commune : un champ porte un identifiant, un libelle, une valeur."""

    is_section = False

    def __init__(self, key, label, default=None, tooltip=""):
        self.key = key
        self.label = label
        self.default = default
        self.tooltip = tooltip
        self.widget = None

    def height(self):
        return ROW_SPACING

    def build(self, ix, panel, y):  # pragma: no cover - GUI
        raise NotImplementedError

    def read(self, ix):  # pragma: no cover - GUI
        raise NotImplementedError

    def _label(self, ix, panel, y):
        widget = ix.api.GuiLabel(panel, MARGIN, y, LABEL_WIDTH, ROW_HEIGHT, self.label + " :")
        if self.tooltip:
            widget.set_tooltip(self.tooltip)
        return widget


class Section(_Field):
    """Un intertitre, pour aerer un formulaire long."""

    is_section = True

    def __init__(self, label):
        _Field.__init__(self, None, label)

    def height(self):
        return SECTION_SPACING

    def build(self, ix, panel, y):  # pragma: no cover - GUI
        self.widget = ix.api.GuiLabel(
            panel, MARGIN, y + 6, LABEL_WIDTH + FIELD_WIDTH, ROW_HEIGHT,
            "[ %s ]" % self.label.upper(),
        )
        self.widget.set_text_color(ix.api.GMathVec3uc(128, 128, 128))

    def read(self, ix):  # pragma: no cover - GUI
        return None


class Text(_Field):
    """Une ligne de texte libre."""

    def build(self, ix, panel, y):  # pragma: no cover - GUI
        self._label(ix, panel, y)
        self.widget = ix.api.GuiLineEdit(panel, MARGIN + LABEL_WIDTH, y, FIELD_WIDTH, ROW_HEIGHT)
        if self.default:
            self.widget.set_text(str(self.default))

    def read(self, ix):  # pragma: no cover - GUI
        return str(self.widget.get_text())


class Number(_Field):
    """Un nombre, avec slider quand une plage est donnee."""

    def __init__(self, key, label, default=0.0, minimum=None, maximum=None,
                 integer=False, increment=None, tooltip=""):
        _Field.__init__(self, key, label, default, tooltip)
        self.minimum = minimum
        self.maximum = maximum
        self.integer = integer
        self.increment = increment

    def build(self, ix, panel, y):  # pragma: no cover - GUI
        self._label(ix, panel, y)
        self.widget = ix.api.GuiNumberField(panel, MARGIN + LABEL_WIDTH, y, FIELD_WIDTH, "")
        if self.minimum is not None and self.maximum is not None:
            self.widget.set_slider_range(self.minimum, self.maximum)
            self.widget.enable_slider_range(True)
        increment = self.increment
        if increment is None:
            increment = 1 if self.integer else 0.1
        self.widget.set_increment(increment)
        if self.default is not None:
            self.widget.set_value(self.default)

    def read(self, ix):  # pragma: no cover - GUI
        value = self.widget.get_value()
        return int(round(value)) if self.integer else float(value)


class Toggle(_Field):
    """Une case a cocher."""

    def build(self, ix, panel, y):  # pragma: no cover - GUI
        self._label(ix, panel, y)
        self.widget = ix.api.GuiCheckbox(panel, MARGIN + LABEL_WIDTH, y, "")
        self.widget.set_value(bool(self.default))
        if self.tooltip:
            self.widget.set_tooltip(self.tooltip)

    def read(self, ix):  # pragma: no cover - GUI
        return bool(self.widget.get_value())


class Choice(_Field):
    """Une liste deroulante.

    ``items`` peut etre une liste de libelles, ou de couples
    ``(libelle, valeur)`` quand la valeur rendue differe de ce qui est affiche.
    """

    def __init__(self, key, label, items, default=0, tooltip=""):
        _Field.__init__(self, key, label, default, tooltip)
        self.items = []
        self.values = []
        for item in items:
            if isinstance(item, (tuple, list)) and len(item) == 2:
                self.items.append(str(item[0]))
                self.values.append(item[1])
            else:
                self.items.append(str(item))
                self.values.append(item)

    def build(self, ix, panel, y):  # pragma: no cover - GUI
        self._label(ix, panel, y)
        self.widget = ix.api.GuiListButton(panel, MARGIN + LABEL_WIDTH, y, FIELD_WIDTH, ROW_HEIGHT)
        for item in self.items:
            self.widget.add_item(item)
        index = self.default if isinstance(self.default, int) else 0
        if 0 <= index < len(self.items):
            self.widget.set_selected_item_by_index(index)
        if self.tooltip:
            self.widget.set_tooltip(self.tooltip)

    def read(self, ix):  # pragma: no cover - GUI
        index = self.widget.get_selected_item_index()
        if 0 <= index < len(self.values):
            return self.values[index]
        return None

    def read_index(self):  # pragma: no cover - GUI
        return self.widget.get_selected_item_index()


class FilePath(_Field):
    """Un chemin, avec un bouton de parcours."""

    def __init__(self, key, label, default="", directory=False, tooltip=""):
        _Field.__init__(self, key, label, default, tooltip)
        self.directory = directory
        self.button = None
        self._events = None

    def build(self, ix, panel, y):  # pragma: no cover - GUI
        self._label(ix, panel, y)
        edit_width = FIELD_WIDTH - 70
        self.widget = ix.api.GuiLineEdit(panel, MARGIN + LABEL_WIDTH, y, edit_width, ROW_HEIGHT)
        if self.default:
            self.widget.set_text(str(self.default))
        self.button = ix.api.GuiPushButton(
            panel, MARGIN + LABEL_WIDTH + edit_width + 6, y, 64, ROW_HEIGHT, "Parcourir"
        )

        field = self
        directory = self.directory

        class _Browse(ix.api.EventObject):
            def clicked(self, sender, evtid):
                if directory:
                    chosen = ix.api.GuiWidget.open_folder(ix.application, "", field.label)
                else:
                    chosen = ix.api.GuiWidget.open_file(ix.application, "", field.label)
                if chosen:
                    field.widget.set_text(str(chosen))

        # L'objet evenement doit survivre a `build` : Clarisse ne garde qu'une
        # reference faible cote C++, et un listener collecte ne repond plus.
        self._events = _Browse()
        self._events.connect(self.button, "EVT_ID_PUSH_BUTTON_CLICK", self._events.clicked)

    def read(self, ix):  # pragma: no cover - GUI
        return str(self.widget.get_text())


# ---------------------------------------------------------------------------
# Fenetre
# ---------------------------------------------------------------------------


class Form(object):
    """Une fenetre modale de reglages construite a partir de ``fields``."""

    def __init__(self, title, fields, width=None, accept_label="Valider",
                 cancel_label="Annuler", note=""):
        self.title = title
        self.fields = list(fields)
        self.width = width or (MARGIN * 2 + LABEL_WIDTH + FIELD_WIDTH + 12)
        self.accept_label = accept_label
        self.cancel_label = cancel_label
        self.note = note
        self.result = None

    def run(self):  # pragma: no cover - GUI
        """Affiche la fenetre et renvoie ``{cle: valeur}``, ou ``None``."""
        ix = get_ix()

        note_height = 26 if self.note else 0
        body_height = sum(field.height() for field in self.fields)
        height = MARGIN + note_height + body_height + ROW_SPACING + MARGIN + 10

        parent = ix.application.get_event_window()
        window = ix.api.GuiWindow(parent, 640, 320, self.width, height)
        window.set_title(self.title)
        panel = ix.api.GuiPanel(window, 0, 0, window.get_width(), window.get_height())
        panel.set_constraints(
            ix.api.GuiWidget.CONSTRAINT_LEFT, ix.api.GuiWidget.CONSTRAINT_TOP,
            ix.api.GuiWidget.CONSTRAINT_RIGHT, ix.api.GuiWidget.CONSTRAINT_BOTTOM,
        )

        y = MARGIN
        if self.note:
            hint = ix.api.GuiLabel(panel, MARGIN, y, self.width - MARGIN * 2, ROW_HEIGHT, self.note)
            hint.set_text_color(ix.api.GMathVec3uc(150, 150, 150))
            y += note_height

        for field in self.fields:
            field.build(ix, panel, y)
            y += field.height()

        y += 6
        button_width = 110
        cancel = ix.api.GuiPushButton(panel, MARGIN, y, button_width, ROW_HEIGHT, self.cancel_label)
        accept = ix.api.GuiPushButton(
            panel, self.width - MARGIN - button_width - 10, y, button_width, ROW_HEIGHT,
            self.accept_label,
        )

        form = self

        class _Events(ix.api.EventObject):
            def cancel(self, sender, evtid):
                form.result = None
                sender.get_window().hide()

            def accept(self, sender, evtid):
                try:
                    form.result = {
                        field.key: field.read(ix)
                        for field in form.fields
                        if not field.is_section and field.key
                    }
                except Exception:
                    log.exception("Lecture du formulaire '%s'" % form.title)
                    form.result = None
                sender.get_window().hide()

        events = _Events()
        events.connect(cancel, "EVT_ID_PUSH_BUTTON_CLICK", events.cancel)
        events.connect(accept, "EVT_ID_PUSH_BUTTON_CLICK", events.accept)

        window.show()
        while window.is_shown():
            ix.application.check_for_events()
        window.destroy()
        return self.result


# ---------------------------------------------------------------------------
# Dialogues courts
# ---------------------------------------------------------------------------


def pick_context(title="Choisir un contexte"):
    """Ouvre le selecteur de contexte natif et renvoie le contexte choisi."""
    ix = get_ix()
    chosen = ix.api.IOHelpers.pick_context(ix.application, title)
    if not chosen:
        return None
    return chosen


def message(text, title="ClarisseAdd"):
    """Affiche une information bloquante."""
    ix = get_ix()
    ix.application.message_box(text, title, ix.api.AppDialog.ok(), ix.api.AppDialog.STYLE_OK)


def confirm(text, title="ClarisseAdd"):
    """Demande une confirmation. ``True`` si l'utilisateur valide."""
    ix = get_ix()
    answer = ix.application.message_box(
        text, title, ix.api.AppDialog.yes(), ix.api.AppDialog.STYLE_YES_NO
    )
    return answer.is_yes()


def open_directory(path):
    """Ouvre un dossier dans l'explorateur du systeme (best effort)."""
    if not os.path.isdir(path):
        return False
    try:
        if os.name == "nt":
            os.startfile(path)  # noqa: S606 - chemin fourni par l'addon
        else:
            import subprocess
            opener = "open" if os.uname().sysname == "Darwin" else "xdg-open"
            subprocess.Popen([opener, path])
        return True
    except Exception:
        log.exception("Ouverture du dossier %s" % path)
        return False
