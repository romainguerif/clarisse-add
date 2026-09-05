"""Non-regression des corrections issues du double-check.

Trois defauts trouves en relisant tout le code apres le premier lancement dans
Clarisse, chacun avec le symptome qu'il produisait :

* ``register_runtime`` reajoutait les 43 boutons a chaque Reload.  Les elements
  crees par ``AppShelf::add_item`` ne sont pas volatils : Clarisse les ecrit
  dans ``shelf.cfg`` en quittant, et le fichier en comptait 86 au redemarrage.
* La jointure de chemins ``str(ctx) + "/" + nom`` produisait ``project:///nom``
  sur la racine, et un ``rstrip("/")`` naif ``project:/nom`` : aucun des deux
  n'est un chemin Clarisse valide.
* ``str(True)`` envoyait ``"True"`` a ``SetValues``, que Clarisse ne lit pas
  comme un booleen.
"""

import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from clarisse_add import manifest  # noqa: E402
from clarisse_add.core import compat, scene, shelf  # noqa: E402


# ---------------------------------------------------------------------------
# register_runtime : pas de doublons
# ---------------------------------------------------------------------------


class _Item(object):
    def __init__(self, title):
        self._title = title

    def get_title(self):
        return self._title


class _Vector(object):
    """Imite un ``CoreVector`` : ``get_count()`` et indexation."""

    def __init__(self, items):
        self._items = list(items)

    def get_count(self):
        return len(self._items)

    def __getitem__(self, index):
        return self._items[index]


class FakeShelf(object):
    """Un ``AppShelf`` minimal : memorise ce qu'on lui ajoute, par categorie."""

    def __init__(self, preloaded=None):
        self.categories = {}
        for category, titles in (preloaded or {}).items():
            self.categories[category] = [_Item(title) for title in titles]
        self.add_calls = 0

    def get_items(self, slot, category):
        if category not in self.categories:
            return None
        return _Vector(self.categories[category])

    def add_item(self, slot, category, title, description, script, icon):
        self.add_calls += 1
        self.categories.setdefault(category, []).append(_Item(title))
        return True

    def count(self):
        return sum(len(items) for items in self.categories.values())


class FakeApp(object):
    def __init__(self, shelf_obj):
        self._shelf = shelf_obj

    def get_shelf(self):
        return self._shelf


class FakeIx(object):
    def __init__(self, shelf_obj):
        self.application = FakeApp(shelf_obj)


TOOLS = manifest.all_tools()


def test_register_runtime_adds_everything_to_an_empty_shelf():
    fake = FakeShelf()
    added, skipped = shelf.register_runtime(FakeIx(fake), TOOLS)
    assert (added, skipped) == (len(TOOLS), 0)
    assert fake.count() == len(TOOLS)


def test_register_runtime_is_idempotent():
    """Le scenario du bug : Reload apres une installation complete."""
    fake = FakeShelf()
    shelf.register_runtime(FakeIx(fake), TOOLS)
    before = fake.count()

    added, skipped = shelf.register_runtime(FakeIx(fake), TOOLS)

    assert added == 0
    assert skipped == len(TOOLS)
    assert fake.count() == before, "un second appel ne doit rien ajouter"


def test_register_runtime_skips_what_shelf_cfg_already_loaded():
    """Au demarrage, Clarisse a deja charge shelf.cfg : tout est present."""
    preloaded = {}
    for tool in TOOLS:
        preloaded.setdefault(tool.category, []).append(tool.title)
    fake = FakeShelf(preloaded)

    added, skipped = shelf.register_runtime(FakeIx(fake), TOOLS)

    assert added == 0
    assert skipped == len(TOOLS)
    assert fake.add_calls == 0


def test_register_runtime_adds_only_the_missing_tool():
    preloaded = {}
    for tool in TOOLS[1:]:
        preloaded.setdefault(tool.category, []).append(tool.title)
    fake = FakeShelf(preloaded)

    added, skipped = shelf.register_runtime(FakeIx(fake), TOOLS)

    assert added == 1
    assert skipped == len(TOOLS) - 1
    assert TOOLS[0].title in [item.get_title()
                              for item in fake.categories[TOOLS[0].category]]


def test_register_runtime_reports_missing_api():
    class NoShelfApp(object):
        def get_shelf(self):
            raise AttributeError("get_shelf")

    class NoShelfIx(object):
        application = NoShelfApp()

    assert shelf.register_runtime(NoShelfIx(), TOOLS) == (-1, 0)


# ---------------------------------------------------------------------------
# child_path : jointure sur la racine
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("root", ["project:/", "project://", "default:/", "build://project"])
def test_child_path_from_a_root(root):
    result = scene.child_path(root, "scene")
    if root == "build://project":
        assert result == "build://project/scene"
    else:
        scheme = root.split(":")[0]
        assert result == "%s://scene" % scheme


def test_child_path_from_a_nested_context():
    assert scene.child_path("project://scene", "box") == "project://scene/box"
    assert scene.child_path("project://scene/", "box") == "project://scene/box"


def test_child_path_accepts_a_relative_owner_path():
    """preset_runner joint le chemin d'origine d'un objet, avec ses slashs."""
    assert scene.child_path("project://lookdev", "scene/materials/wb") == \
        "project://lookdev/scene/materials/wb"


def test_child_path_never_produces_a_triple_slash():
    for root in ("project:/", "project://", "project://a", "project://a/"):
        assert ":///" not in scene.child_path(root, "x")


# ---------------------------------------------------------------------------
# set_attribute : serialisation
# ---------------------------------------------------------------------------


class _Attr(object):
    pass


class _Obj(object):
    def __init__(self, path):
        self._path = path

    def __str__(self):
        return self._path

    def get_attribute(self, name):
        return _Attr() if name != "absent" else None


class _Cmds(object):
    def __init__(self):
        self.calls = []

    def SetValues(self, targets, values):
        self.calls.append((list(targets), list(values)))


@pytest.fixture
def fake_ix(monkeypatch):
    ix = type("Ix", (), {})()
    ix.cmds = _Cmds()
    monkeypatch.setattr(compat, "_IX", ix)
    return ix


def test_set_attribute_serializes_booleans(fake_ix):
    obj = _Obj("project://scene/box")
    assert scene.set_attribute(obj, "enable", True) is True
    assert scene.set_attribute(obj, "enable", False) is True
    assert fake_ix.cmds.calls == [
        (["project://scene/box.enable"], ["1"]),
        (["project://scene/box.enable"], ["0"]),
    ]


def test_set_attribute_serializes_numbers_and_vectors(fake_ix):
    obj = _Obj("project://scene/box")
    scene.set_attribute(obj, "size", 2.5)
    scene.set_attribute(obj, "translate", [1, 2.0, 3])
    assert fake_ix.cmds.calls == [
        (["project://scene/box.size"], ["2.5"]),
        (["project://scene/box.translate"], ["1", "2.0", "3"]),
    ]


def test_set_attribute_ignores_a_missing_attribute(fake_ix):
    obj = _Obj("project://scene/box")
    assert scene.set_attribute(obj, "absent", 1) is False
    assert fake_ix.cmds.calls == []
