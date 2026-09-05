"""Coherence entre le manifeste, le catalogue et ce qui est reellement sur le disque.

Ces tests attrapent la classe d'erreur la plus penible de l'addon : un bouton
qui apparait dans le shelf et ne fait rien, parce que le module qu'il designe
n'existe pas ou n'expose pas ``run()``.  Rien de tout cela ne se voit avant le
clic, dans Clarisse, en pleine session.
"""

import importlib
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from clarisse_add import manifest  # noqa: E402
from clarisse_add.core import paths, shelf  # noqa: E402
from clarisse_add.presets import catalog  # noqa: E402

TOOLS = manifest.all_tools()


def test_tools_exist():
    assert TOOLS


def test_tool_ids_are_unique():
    identifiers = [tool.id for tool in TOOLS]
    duplicates = {item for item in identifiers if identifiers.count(item) > 1}
    assert not duplicates, "identifiants en double : %s" % duplicates


def test_titles_are_unique_within_a_category():
    seen = set()
    for tool in TOOLS:
        key = (tool.category, tool.title)
        assert key not in seen, "deux boutons '%s' dans %s" % (tool.title, tool.category)
        seen.add(key)


@pytest.mark.parametrize("tool", TOOLS, ids=lambda tool: tool.id)
def test_tool_module_is_importable_and_runnable(tool):
    """Le module existe et expose ``run()``.

    L'import ne doit pas exiger Clarisse : les modules d'outils n'appellent
    ``get_ix()`` que dans ``run()``, jamais au chargement.
    """
    module = importlib.import_module(tool.module)
    entry = getattr(module, "run", None)
    assert callable(entry), "%s n'expose pas run()" % tool.module


@pytest.mark.parametrize("tool", TOOLS, ids=lambda tool: tool.id)
def test_tool_has_a_description(tool):
    assert len(tool.description) > 20, "description trop courte pour %s" % tool.id


def test_by_id_finds_every_tool():
    for tool in TOOLS:
        assert manifest.by_id(tool.id) is tool
    assert manifest.by_id("n.existe.pas") is None


def test_entry_scripts_are_generated(tmp_path, monkeypatch):
    monkeypatch.setattr(paths, "ENTRY_DIR", str(tmp_path))
    monkeypatch.setattr(shelf.paths, "ENTRY_DIR", str(tmp_path))

    shelf.write_entry_scripts(TOOLS)
    produced = {name for name in os.listdir(str(tmp_path)) if name.endswith(".py")}
    assert "__init__.py" in produced
    for tool in TOOLS:
        assert os.path.basename(shelf.entry_filename(tool)) in produced

    # Ecrire deux fois ne doit rien reecrire : les stubs sont stables.
    assert shelf.write_entry_scripts(TOOLS) == 0


def test_prune_removes_orphan_entries(tmp_path, monkeypatch):
    monkeypatch.setattr(paths, "ENTRY_DIR", str(tmp_path))
    monkeypatch.setattr(shelf.paths, "ENTRY_DIR", str(tmp_path))

    shelf.write_entry_scripts(TOOLS)
    orphan = tmp_path / "outil_supprime.py"
    orphan.write_text("# obsolete", encoding="utf-8")

    assert shelf.prune_entry_scripts(TOOLS) == 1
    assert not orphan.exists()


# ---------------------------------------------------------------------------
# Catalogue de presets
# ---------------------------------------------------------------------------

ENTRIES = catalog.entries()


def test_catalog_is_not_empty():
    assert ENTRIES, "catalog.json est vide : lancez tools/build_catalog.py"


@pytest.mark.parametrize("entry", ENTRIES, ids=lambda entry: entry.id)
def test_preset_file_is_present(entry):
    assert entry.exists(), "%s introuvable" % entry.path


@pytest.mark.parametrize("entry", ENTRIES, ids=lambda entry: entry.id)
def test_preset_has_editorial_metadata(entry):
    assert entry.title
    assert entry.category
    assert len(entry.description) > 30, "description trop courte pour %s" % entry.id


def test_shelf_presets_have_a_matching_tool():
    tool_payloads = {tool.payload for tool in manifest.preset_tools()}
    for entry in catalog.shelf_entries():
        assert entry.id in tool_payloads


def test_parameters_carry_an_owner():
    """Sans objet porteur, un parametre ne peut pas etre reapplique."""
    for entry in ENTRIES:
        for parameter in entry.parameters:
            assert parameter.owner, "%s: parametre %s sans owner" % (entry.id, parameter.name)


def test_parameter_labels_are_readable():
    for entry in ENTRIES:
        for parameter in entry.parameters:
            assert parameter.label
            assert not parameter.label.startswith("OSL_")
