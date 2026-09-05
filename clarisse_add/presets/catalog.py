"""Catalogue de la bibliotheque de scenes ``.project``.

Le catalogue est un fichier JSON genere par ``tools/build_catalog.py`` : il
combine des metadonnees editoriales ecrites a la main (titre, description,
credit, categorie) et un inventaire extrait automatiquement de chaque fichier
par :mod:`clarisse_add.core.project_file` (classes presentes, parametres
exposes, fichiers externes references).

Le generer a l'avance evite d'analyser 12 Mo de ``.project`` a chaque ouverture
du Preset Browser, et permet de detecter hors ligne qu'un preset reclame une
texture absente.
"""

import io
import json
import os

from ..core import paths

_CATALOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "catalog.json")
_CACHE = None


class Parameter(object):
    """Un parametre expose par un preset, tel que declare dans le fichier."""

    __slots__ = ("name", "type", "group", "doc", "default", "minimum", "maximum", "presets", "owner")

    def __init__(self, data):
        self.name = data.get("name", "")
        self.type = data.get("type", "")
        self.group = data.get("group", "")
        self.doc = data.get("doc", "")
        self.default = data.get("default")
        self.minimum = data.get("minimum")
        self.maximum = data.get("maximum")
        self.presets = [tuple(pair) for pair in data.get("presets", [])]
        #: chemin relatif de l'objet porteur, dans la scene fusionnee
        self.owner = data.get("owner", "")

    @property
    def label(self):
        """Libelle lisible : la doc si elle existe, sinon le nom nettoye."""
        if self.doc:
            return self.doc
        name = self.name
        for prefix in ("OSL_", "CA_"):
            if name.startswith(prefix):
                name = name[len(prefix):]
        return name.replace("_", " ").strip().capitalize()

    @property
    def is_numeric(self):
        return self.type in ("double", "float", "long", "int")

    @property
    def is_integer(self):
        return self.type in ("long", "int")


class PresetEntry(object):
    """Une entree du catalogue."""

    def __init__(self, data):
        self.id = data["id"]
        self.title = data.get("title", data["id"])
        self.description = data.get("description", "")
        self.category = data.get("category", "Divers")
        self.credit = data.get("credit", "")
        self.shelf = bool(data.get("shelf", False))
        self.filename = data.get("filename", "")
        self.directory = data.get("directory", data["id"])
        self.object_count = data.get("object_count", 0)
        self.classes = data.get("classes", {})
        self.missing_files = data.get("missing_files", [])
        self.external_files = data.get("external_files", [])
        self.parameters = [Parameter(item) for item in data.get("parameters", [])]

    # -- acces disque ------------------------------------------------------

    @property
    def path(self):
        """Chemin absolu du ``.project``."""
        return paths.preset(self.directory, self.filename)

    def exists(self):
        return os.path.isfile(self.path)

    @property
    def parameter_groups(self):
        """Parametres regroupes par ``(objet, groupe)``, ordre stable."""
        groups = []
        index = {}
        for parameter in self.parameters:
            key = (parameter.owner, parameter.group)
            if key not in index:
                index[key] = []
                groups.append((key, index[key]))
            index[key].append(parameter)
        return groups

    def summary(self):
        """Une ligne d'inventaire, pour l'affichage dans le navigateur."""
        top = sorted(self.classes.items(), key=lambda item: -item[1])[:4]
        return ", ".join("%s x%d" % (name, count) for name, count in top)

    def __repr__(self):  # pragma: no cover - debug only
        return "<PresetEntry %s '%s'>" % (self.id, self.title)


def _load():
    global _CACHE
    if _CACHE is not None:
        return _CACHE
    if not os.path.isfile(_CATALOG_PATH):
        _CACHE = []
        return _CACHE
    with io.open(_CATALOG_PATH, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    _CACHE = [PresetEntry(item) for item in data.get("presets", [])]
    return _CACHE


def reload():
    """Vide le cache : utile apres avoir regenere le catalogue."""
    global _CACHE
    _CACHE = None
    return _load()


def entries():
    """Toutes les entrees du catalogue."""
    return list(_load())


def shelf_entries():
    """Les entrees qui meritent leur propre bouton de shelf."""
    return [entry for entry in _load() if entry.shelf]


def by_id(preset_id):
    for entry in _load():
        if entry.id == preset_id:
            return entry
    return None


def categories():
    """Categories presentes, triees."""
    found = sorted({entry.category for entry in _load()})
    return found
