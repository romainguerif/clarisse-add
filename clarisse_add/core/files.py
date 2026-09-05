"""Recherche des fichiers references par une scene.

Textures, caches Alembic, archives, scripts OSL, HDRI : tout cela vit dans des
attributs de type ``TYPE_FILE``.  Les parcourir permet a la fois de dire ce qui
manque (Scene Audit) et de reecrire les chemins en masse (Relink), sans que
chaque outil ait a redecouvrir ou sont les fichiers.

Clarisse accepte dans ces attributs des motifs de sequence (``###``, ``$F4``,
``<UDIM>``) et des variables (``$PDIR``).  Un chemin qui en contient ne peut pas
etre teste tel quel : on le signale comme non verifiable plutot que de le
declarer manquant a tort.
"""

import os
import re

from .compat import get_ix

#: Motifs qui rendent un chemin non testable directement.
_PATTERN_RE = re.compile(r"(#+|\$\w+|<UDIM>|<udim>|%0\d+d|\*|\?)")


class FileReference(object):
    """Un attribut de fichier, son objet porteur et sa valeur."""

    __slots__ = ("obj", "attribute", "attribute_name", "index", "value")

    def __init__(self, obj, attribute, attribute_name, index, value):
        self.obj = obj
        self.attribute = attribute
        self.attribute_name = attribute_name
        self.index = index
        self.value = value

    @property
    def target(self):
        """Cible utilisable avec ``ix.cmds.SetValues``."""
        if self.index is None:
            return "%s.%s" % (str(self.obj), self.attribute_name)
        return "%s.%s[%d]" % (str(self.obj), self.attribute_name, self.index)

    def has_pattern(self):
        return bool(_PATTERN_RE.search(self.value))

    def exists(self):
        """``True`` si le fichier est la, ``None`` si on ne peut pas savoir."""
        if not self.value:
            return None
        if self.has_pattern():
            return None
        return os.path.exists(self.value)

    def __repr__(self):  # pragma: no cover - debug only
        return "<FileReference %s = %r>" % (self.target, self.value)


def iter_file_references(root=None):
    """Itere sur toutes les references de fichier de la scene.

    ``root`` est un contexte ; par defaut, la racine du projet.
    """
    ix = get_ix()
    from . import scene

    if root is None:
        root = ix.application.get_factory().get_root()

    file_type = ix.api.OfAttr.TYPE_FILE

    for obj in scene.iter_objects(root):
        count = obj.get_attribute_count()
        for index in range(count):
            attribute = obj.get_attribute(index)
            if attribute is None or attribute.get_type() != file_type:
                continue
            name = str(attribute.get_name())
            value_count = attribute.get_value_count()
            if value_count <= 1:
                value = str(attribute.get_string())
                if value:
                    yield FileReference(obj, attribute, name, None, value)
            else:
                for position in range(value_count):
                    value = str(attribute.get_string(position))
                    if value:
                        yield FileReference(obj, attribute, name, position, value)


def missing_files(root=None):
    """Les references dont le fichier est absent du disque."""
    return [reference for reference in iter_file_references(root)
            if reference.exists() is False]


def common_prefixes(references, limit=12):
    """Les dossiers les plus frequents parmi les references.

    Sert a proposer un ancien prefixe plausible dans Relink plutot que de
    laisser l'artiste le retaper.
    """
    counts = {}
    for reference in references:
        directory = os.path.dirname(reference.value.replace("\\", "/"))
        while directory and directory not in ("/", ""):
            counts[directory] = counts.get(directory, 0) + 1
            parent = os.path.dirname(directory)
            if parent == directory:
                break
            directory = parent
    ranked = sorted(counts.items(), key=lambda item: (-item[1], -len(item[0])))
    return [directory for directory, _count in ranked[:limit]]
