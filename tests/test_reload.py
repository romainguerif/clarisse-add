"""Non-regression : le rechargement ne doit pas dedoubler ``core.compat``.

Historique du bug, parce qu'il est instructif et facile a reintroduire.

``reload_addon()`` purgeait ``sys.modules`` de tous les modules de l'addon
*sauf* ``bootstrap``, au motif qu'on tourne dedans au moment de la purge.  Mais
``bootstrap`` importait ``set_ix`` en tete de fichier : il gardait donc une
reference vers l'exemplaire de ``core.compat`` charge au premier clic.

Au clic suivant :

* ``launch()`` appelait l'ancien ``set_ix``, qui ecrivait ``_IX`` dans l'ancien
  ``compat`` ;
* ``importlib.import_module(tool.module)`` importait l'outil a neuf, lequel
  faisait ``from ..core.compat import get_ix`` -- resolu vers un ``compat``
  tout neuf, puisque l'ancien avait ete purge, avec ``_IX`` a ``None``.

Deux exemplaires du meme module : l'un ou l'on ecrit, l'autre ou l'on lit.  Tous
les outils tombaient sur ``ClarisseUnavailable`` apres un simple Reload, y
compris le bouton Reload lui-meme -- donc sans moyen de s'en sortir autrement
qu'en redemarrant Clarisse.

Deux garde-fous depuis : ``launch()`` resout ``set_ix`` a chaque appel, et la
purge n'epargne plus ``bootstrap``.
"""

import importlib
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)


class FakeIx(object):
    """Un ``ix`` inerte, reconnaissable par identite."""


@pytest.fixture(autouse=True)
def clean_modules():
    """Chaque test part et repart d'un ``sys.modules`` sans l'addon."""
    _purge()
    yield
    _purge()


def _purge():
    for name in [n for n in sys.modules
                 if n == "clarisse_add" or n.startswith("clarisse_add.")]:
        del sys.modules[name]


def _first_tool_id():
    manifest = importlib.import_module("clarisse_add.manifest")
    return manifest.all_tools()[0].id


def test_reload_purges_bootstrap_too():
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    bootstrap.reload_addon()
    assert "clarisse_add.bootstrap" not in sys.modules, (
        "epargner bootstrap lui laisse des references vers l'ancien core"
    )


def test_reload_purges_the_whole_package():
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    importlib.import_module("clarisse_add.core.compat")
    importlib.import_module("clarisse_add.manifest")

    forgotten = bootstrap.reload_addon()

    assert forgotten >= 3
    remaining = [n for n in sys.modules
                 if n == "clarisse_add" or n.startswith("clarisse_add.")]
    assert remaining == []


def test_ix_reaches_the_tool_after_a_reload(monkeypatch):
    """Le scenario complet : clic, Reload, clic. C'est le bug d'origine."""
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    tool_id = _first_tool_id()

    # Premier clic.
    first = FakeIx()
    seen = {}

    def spy(payload=None):
        from clarisse_add.core.compat import get_ix
        seen["ix"] = get_ix()
        return True

    manifest = importlib.import_module("clarisse_add.manifest")
    module = importlib.import_module(manifest.by_id(tool_id).module)
    monkeypatch.setattr(module, "run", spy)
    bootstrap.launch(tool_id, first)
    assert seen["ix"] is first

    # Le bouton Reload.
    bootstrap.reload_addon()

    # Second clic : le stub reimporte bootstrap, comme le ferait Clarisse.
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    second = FakeIx()
    seen.clear()
    manifest = importlib.import_module("clarisse_add.manifest")
    module = importlib.import_module(manifest.by_id(tool_id).module)
    monkeypatch.setattr(module, "run", spy)

    bootstrap.launch(tool_id, second)

    assert "ix" in seen, (
        "l'outil n'a pas vu ix apres le rechargement : core.compat est dedouble"
    )
    assert seen["ix"] is second


def test_only_one_compat_module_after_a_reload():
    """Formulation directe : set_ix et get_ix doivent viser le meme module."""
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    bootstrap.reload_addon()

    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    fake = FakeIx()
    bootstrap.launch("outil.inexistant", fake)  # suffit a appeler set_ix

    compat = importlib.import_module("clarisse_add.core.compat")
    assert compat.is_available()
    assert compat.get_ix() is fake


def test_run_script_file_also_registers_ix_after_a_reload(tmp_path):
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    bootstrap.reload_addon()
    bootstrap = importlib.import_module("clarisse_add.bootstrap")

    script = tmp_path / "essai.py"
    script.write_text("marqueur = ix.valeur\n", encoding="utf-8")

    fake = FakeIx()
    fake.valeur = 7
    assert bootstrap.run_script_file(str(script), fake) is True

    compat = importlib.import_module("clarisse_add.core.compat")
    assert compat.get_ix() is fake


def test_vendor_path_is_restored_after_a_reload():
    """``_vendor_ready`` est une globale de module : la purge la remet a zero."""
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    paths = importlib.import_module("clarisse_add.core.paths")
    bootstrap.ensure_paths()
    assert paths.VENDOR_DIR in sys.path

    while paths.VENDOR_DIR in sys.path:
        sys.path.remove(paths.VENDOR_DIR)

    bootstrap.reload_addon()
    bootstrap = importlib.import_module("clarisse_add.bootstrap")
    bootstrap.ensure_paths()

    paths = importlib.import_module("clarisse_add.core.paths")
    assert paths.VENDOR_DIR in sys.path
