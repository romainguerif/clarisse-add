"""Lecteur du format ``.project`` d'Isotropix Clarisse iFX.

Le format est un arbre textuel simple, sans virgules ni deux-points::

    #Isotropix_Serial_Version 1.2
    #Isotropix_Clarisse_Version 4
    Context "scene" {
        CameraPerspective {
            name "camera"
            translate 28 21 28
            objects "project://scene/box" "project://scene/sphere"
            embedded_objects {
                IntegratorPathtracer { name "..." }
            }
        }
    }

Chaque bloc est ``<ClassName> { ... }`` (ou ``Context "nom" { ... }``), chaque
ligne restante est ``<attribut> <valeur> [valeur ...]``.  Les lignes qui
commencent par ``#`` sont des metadonnees (``#created``, ``#version``, ...) et
non des commentaires : on les conserve.

Ce module ne depend pas de ``ix`` : il tourne aussi bien dans Clarisse qu'en
Python standard, ce qui permet d'indexer une bibliotheque de presets hors ligne.

Compatible Python 3.7 (la version embarquee par Clarisse 5.0 SP14).
"""

import io
import os
import re

__all__ = [
    "ProjectNode",
    "CustomAttribute",
    "ProjectFile",
    "parse",
    "parse_string",
    "ParseError",
]


class ParseError(Exception):
    """Le fichier n'est pas un ``.project`` Clarisse exploitable."""


# ---------------------------------------------------------------------------
# Tokenizer
# ---------------------------------------------------------------------------

# Une valeur est soit une chaine entre guillemets (avec echappements), soit une
# accolade, soit une suite de caracteres non blancs.
_TOKEN_RE = re.compile(
    r'"((?:[^"\\]|\\.)*)"'   # 1: chaine entre guillemets
    r'|(\{|\})'              # 2: accolade
    r'|([^\s"{}]+)'          # 3: mot nu
)

_UNESCAPE_RE = re.compile(r"\\(.)")


def _unescape(text):
    return _UNESCAPE_RE.sub(r"\1", text)


class _Token(object):
    __slots__ = ("kind", "value", "line")

    # kind: "string" | "word" | "open" | "close" | "newline"
    def __init__(self, kind, value, line):
        self.kind = kind
        self.value = value
        self.line = line

    def __repr__(self):  # pragma: no cover - debug only
        return "<%s %r L%d>" % (self.kind, self.value, self.line)


def _tokenize(text):
    """Decoupe le texte en tokens, en gardant les fins de ligne significatives."""
    tokens = []
    append = tokens.append
    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        for match in _TOKEN_RE.finditer(line):
            quoted, brace, word = match.group(1), match.group(2), match.group(3)
            if quoted is not None:
                append(_Token("string", _unescape(quoted), lineno))
            elif brace is not None:
                append(_Token("open" if brace == "{" else "close", brace, lineno))
            else:
                append(_Token("word", word, lineno))
        append(_Token("newline", "\n", lineno))
    return tokens


# ---------------------------------------------------------------------------
# AST
# ---------------------------------------------------------------------------


class ProjectNode(object):
    """Un bloc du fichier : un objet Clarisse, un Context, ou la racine.

    Attributs :
        class_name  nom de classe Clarisse ("CameraPerspective", "Context", ...)
        label       le litteral qui suivait la classe, s'il y en avait un
                    (``Context "scene"`` -> "scene"), ``None`` sinon
        attributes  dict {nom: [valeurs]} des attributs simples
        children    liste des ProjectNode enfants
        parent      le noeud parent, ``None`` pour la racine
        line        numero de ligne de l'accolade ouvrante
        end_line    numero de ligne de l'accolade fermante
    """

    __slots__ = ("class_name", "label", "attributes", "children", "parent",
                 "line", "end_line")

    def __init__(self, class_name, label=None, parent=None, line=0):
        self.class_name = class_name
        self.label = label
        self.attributes = {}
        self.children = []
        self.parent = parent
        #: ligne de l'accolade ouvrante, et de la fermante (1-indexees).
        #: Elles permettent de retoucher un fichier par tranches de lignes
        #: sans avoir a le re-serialiser -- donc sans risquer d'abimer ce
        #: qu'on n'a pas ecrit soi-meme.
        self.line = line
        self.end_line = line

    # -- acces -------------------------------------------------------------

    @property
    def name(self):
        """Nom de l'objet : l'attribut ``name``, sinon le label du bloc."""
        values = self.attributes.get("name")
        if values:
            return values[0]
        return self.label

    @property
    def path(self):
        """Chemin facon Clarisse, ``project://scene/camera``."""
        parts = []
        node = self
        while node is not None and node.parent is not None:
            if node.name:
                parts.append(node.name)
            node = node.parent
        return "project://" + "/".join(reversed(parts))

    def get(self, attribute, default=None):
        """Premiere valeur de l'attribut, ou ``default``."""
        values = self.attributes.get(attribute)
        if not values:
            return default
        return values[0]

    def get_all(self, attribute):
        """Toutes les valeurs de l'attribut (liste vide si absent)."""
        return list(self.attributes.get(attribute, ()))

    def get_float(self, attribute, index=0, default=None):
        values = self.attributes.get(attribute)
        if not values or index >= len(values):
            return default
        try:
            return float(values[index])
        except (TypeError, ValueError):
            return default

    def get_bool(self, attribute, default=None):
        value = self.get(attribute)
        if value is None:
            return default
        return str(value).lower() in ("yes", "true", "1", "on")

    # -- parcours ----------------------------------------------------------

    def walk(self):
        """Itere sur ce noeud puis, en profondeur, sur tous ses descendants."""
        yield self
        for child in self.children:
            for node in child.walk():
                yield node

    def find(self, class_name=None, name=None, recursive=True):
        """Tous les noeuds correspondant a la classe et/ou au nom donnes."""
        source = self.walk() if recursive else iter(self.children)
        found = []
        for node in source:
            if node is self:
                continue
            if class_name is not None and node.class_name != class_name:
                continue
            if name is not None and node.name != name:
                continue
            found.append(node)
        return found

    def find_one(self, class_name=None, name=None, recursive=True):
        matches = self.find(class_name=class_name, name=name, recursive=recursive)
        return matches[0] if matches else None

    # -- attributs personnalises -------------------------------------------

    def custom_attributes(self):
        """Les attributs custom declares sur cet objet.

        Dans le fichier ils se presentent ainsi::

            custom_attributes {
                attribute_group "input" {
                    double "OSL_roomDepth" {
                        doc "roomDepth"
                        numeric_range yes 0.1 100
                        value 1.5
                    }
                }
            }

        Ce sont les *parametres* d'un setup : c'est ce qu'un artiste manipule
        une fois la scene mergee.  On les remonte a plat, en gardant le groupe.
        """
        block = None
        for child in self.children:
            if child.class_name == "custom_attributes":
                block = child
                break
        if block is None:
            return []

        declared = []
        for group in block.children:
            if group.class_name == "attribute_group":
                group_name = group.label or "custom"
                members = group.children
            else:
                # Attribut declare directement, hors groupe.
                group_name = ""
                members = [group]
            for member in members:
                declared.append(CustomAttribute.from_node(member, group_name))
        return declared

    def __repr__(self):  # pragma: no cover - debug only
        return "<ProjectNode %s %r children=%d attrs=%d>" % (
            self.class_name,
            self.name,
            len(self.children),
            len(self.attributes),
        )


class CustomAttribute(object):
    """Un attribut custom declare sur un objet du ``.project``.

    C'est la brique qui permet de transformer une scene-outil en panneau de
    reglages : le type, la plage et la valeur par defaut suffisent a generer
    un widget.
    """

    __slots__ = ("name", "type", "group", "doc", "values", "minimum", "maximum", "presets")

    def __init__(self, name, type_name, group="", doc="", values=None,
                 minimum=None, maximum=None, presets=None):
        self.name = name
        self.type = type_name
        self.group = group
        self.doc = doc
        self.values = values or []
        self.minimum = minimum
        self.maximum = maximum
        self.presets = presets or []

    @classmethod
    def from_node(cls, node, group=""):
        # `ui_range yes 0.1 100` -> (0.1, 100). `numeric_range` est la borne
        # dure, `ui_range` celle du slider ; on prefere l'affichage.
        minimum = maximum = None
        for key in ("ui_range", "numeric_range"):
            bounds = node.get_all(key)
            if len(bounds) >= 3 and bounds[0].lower() in ("yes", "true"):
                try:
                    minimum, maximum = float(bounds[1]), float(bounds[2])
                except ValueError:
                    minimum = maximum = None
                if minimum is not None:
                    break

        # `preset "label" "valeur"`, potentiellement repete : le parser a
        # concatene les paires, on les regroupe deux par deux.
        raw_presets = node.get_all("preset")
        presets = [
            (raw_presets[i], raw_presets[i + 1])
            for i in range(0, len(raw_presets) - 1, 2)
        ]

        return cls(
            name=node.label or node.get("name") or "",
            type_name=node.class_name,
            group=group,
            doc=node.get("doc", "") or "",
            values=node.get_all("value"),
            minimum=minimum,
            maximum=maximum,
            presets=presets,
        )

    @property
    def is_numeric(self):
        return self.type in ("double", "long", "float", "int")

    def default(self):
        """Valeur par defaut, convertie selon le type quand c'est possible."""
        if not self.values:
            return None
        if self.type in ("double", "float"):
            try:
                return [float(v) for v in self.values] if len(self.values) > 1 else float(self.values[0])
            except ValueError:
                return self.values[0]
        if self.type in ("long", "int"):
            try:
                return int(float(self.values[0]))
            except ValueError:
                return self.values[0]
        if self.type == "bool":
            return str(self.values[0]).lower() in ("yes", "true", "1")
        return self.values[0] if len(self.values) == 1 else self.values

    def __repr__(self):  # pragma: no cover - debug only
        return "<CustomAttribute %s %s=%r>" % (self.type, self.name, self.default())


class ProjectFile(object):
    """Un fichier ``.project`` analyse."""

    def __init__(self, root, path=None, headers=None):
        self.root = root
        self.path = path
        self.headers = headers or {}

    # -- metadonnees -------------------------------------------------------

    @property
    def clarisse_version(self):
        return self.headers.get("Isotropix_Clarisse_Version")

    @property
    def project_version(self):
        return self.headers.get("Isotropix_Clarisse_Project_Version")

    # -- inventaire --------------------------------------------------------

    def class_histogram(self, skip_embedded=True):
        """``{classe: nombre}`` pour tout le fichier.

        ``skip_embedded`` ignore les objets planques sous ``embedded_objects``
        (les reglages internes du renderer, presents dans toutes les scenes et
        qui noient l'inventaire reel).
        """
        counts = {}
        for node in self.iter_objects(skip_embedded=skip_embedded):
            counts[node.class_name] = counts.get(node.class_name, 0) + 1
        return counts

    def objects(self, class_name=None, skip_embedded=True):
        return [
            node
            for node in self.iter_objects(skip_embedded=skip_embedded)
            if class_name is None or node.class_name == class_name
        ]

    def contexts(self, skip_embedded=True):
        return self.objects("Context", skip_embedded=skip_embedded)

    def iter_objects(self, skip_embedded=True):
        """Parcours en profondeur des objets de la scene, racine exclue.

        Un ``.project`` ne contient pas que la scene : il embarque aussi la
        disposition des fenetres, sous un bloc ``#preferences``.  Sans filtre,
        l'inventaire d'une scene de trois cubes remonte deux cents ``tab``,
        ``split_v`` et ``viewport_widget``.  On saute donc ces sous-arbres,
        ainsi que les declarations d'attributs custom (ce sont des parametres,
        pas des objets) et, sur demande, les objets internes du renderer.
        """
        stack = list(reversed(self.root.children))
        while stack:
            node = stack.pop()
            class_name = node.class_name
            if class_name.startswith("#"):
                continue
            if class_name == "custom_attributes":
                continue
            if skip_embedded and class_name == "embedded_objects":
                continue
            yield node
            stack.extend(reversed(node.children))

    def parameterized_objects(self, skip_embedded=True):
        """Les objets porteurs d'attributs custom, avec leurs parametres.

        Renvoie une liste de ``(node, [CustomAttribute, ...])``.  C'est ce qui
        distingue une scene-outil (WindowBox, les noises OSL, Wall Maker) d'une
        simple scene d'exemple : la premiere expose des reglages.
        """
        found = []
        for node in self.iter_objects(skip_embedded=skip_embedded):
            attributes = node.custom_attributes()
            if attributes:
                found.append((node, attributes))
        return found

    def external_files(self):
        """Chemins de fichiers references (textures, caches, archives).

        Utile pour reperer une scene qui ne se merge pas telle quelle parce
        qu'elle pointe vers des assets absents.
        """
        found = []
        for node in self.iter_objects(skip_embedded=False):
            for attribute in ("filename", "filenames", "file", "path"):
                for value in node.get_all(attribute):
                    if value and value != "<empty>":
                        found.append((node.path, attribute, value))
        return found


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

_HEADER_RE = re.compile(r"^#(?P<key>[A-Za-z_]\w*)\s+(?P<value>.*)$")


def parse_string(text, path=None):
    """Analyse le contenu d'un ``.project`` et renvoie un :class:`ProjectFile`."""
    headers = {}
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if not stripped.startswith("#"):
            break
        match = _HEADER_RE.match(stripped)
        if match:
            headers[match.group("key")] = match.group("value").strip()

    tokens = _tokenize(text)
    root = ProjectNode("<root>", label=path)
    node = root
    # Tokens de la ligne courante, accumules jusqu'au newline ou a l'accolade.
    pending = []

    def flush_attribute():
        """La ligne courante n'ouvrait pas de bloc : c'est un attribut."""
        if not pending:
            return
        key = pending[0].value
        values = [token.value for token in pending[1:]]
        # Un attribut repete (rare, mais possible sur les listes) est concatene
        # plutot qu'ecrase : on ne perd jamais de donnee.
        if key in node.attributes:
            node.attributes[key].extend(values)
        else:
            node.attributes[key] = values
        del pending[:]

    for token in tokens:
        kind = token.kind
        if kind == "open":
            # `pending` porte l'en-tete du bloc : ClassName, ClassName "label",
            # ou un conteneur sans classe comme `embedded_objects`.
            if not pending:
                raise ParseError("Accolade ouvrante sans en-tete, ligne %d" % token.line)
            class_name = pending[0].value
            label = pending[1].value if len(pending) > 1 else None
            child = ProjectNode(class_name, label=label, parent=node, line=token.line)
            node.children.append(child)
            node = child
            del pending[:]
        elif kind == "close":
            flush_attribute()
            if node.parent is None:
                raise ParseError("Accolade fermante en trop, ligne %d" % token.line)
            node.end_line = token.line
            node = node.parent
        elif kind == "newline":
            flush_attribute()
        else:
            pending.append(token)

    flush_attribute()
    if node is not root:
        raise ParseError("Accolade non fermee (bloc %s)" % node.class_name)
    return ProjectFile(root, path=path, headers=headers)


def parse(path):
    """Analyse le fichier ``.project`` situe a ``path``."""
    with io.open(path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    if "#Isotropix" not in text[:400]:
        raise ParseError("%s ne ressemble pas a un .project Clarisse" % os.path.basename(path))
    return parse_string(text, path=path)
