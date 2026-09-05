"""Execution des stubs du shelf, comme Clarisse le ferait.

C'est le seul maillon que les autres tests ne couvrent pas : ils verifient que
les modules d'outils s'importent et exposent ``run()``, mais pas que le fichier
reellement designe par ``script_filename`` fasse le lien.  Or c'est precisement
la ou tout casse en production -- un stub perime, un chemin d'addon deplace, un
identifiant qui ne correspond plus au manifeste -- et le symptome est un bouton
qui ne fait rien, sans message.

On execute donc chaque stub dans un espace de noms neuf, avec un faux ``ix``,
en interceptant ``launch`` juste avant qu'il n'appelle l'outil.
"""

import io
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from clarisse_add import bootstrap, manifest  # noqa: E402
from clarisse_add.core import shelf  # noqa: E402

TOOLS = manifest.all_tools()


class FakeIx(object):
    """Un ``ix`` inerte : il suffit a traverser le stub jusqu'a ``launch``."""


@pytest.fixture(autouse=True)
def stubs_written():
    shelf.write_entry_scripts(TOOLS)


@pytest.mark.parametrize("tool", TOOLS, ids=lambda tool: tool.id)
def test_stub_reaches_launch_with_the_right_id(tool, monkeypatch):
    path = shelf.entry_filename(tool)
    assert os.path.isfile(path), "stub manquant pour %s" % tool.id

    seen = {}

    def spy(tool_id, ix_module, payload=None):
        seen["id"] = tool_id
        seen["ix"] = ix_module
        return "intercepte"

    monkeypatch.setattr(bootstrap, "launch", spy)

    with io.open(path, "r", encoding="utf-8") as handle:
        source = handle.read()
    namespace = {"__name__": "__main__", "__file__": path, "ix": FakeIx()}
    exec(compile(source, path, "exec"), namespace)

    assert seen.get("id") == tool.id
    assert isinstance(seen.get("ix"), FakeIx)


def test_stub_makes_the_addon_importable(monkeypatch, tmp_path):
    """Le stub doit ajouter la racine de l'addon a sys.path lui-meme.

    Clarisse execute le fichier depuis un interpreteur qui ne connait pas
    l'addon : sans cette ligne, le premier import echoue.
    """
    tool = TOOLS[0]
    with io.open(shelf.entry_filename(tool), "r", encoding="utf-8") as handle:
        source = handle.read()

    from clarisse_add.core import paths
    assert repr(paths.ADDON_ROOT) in source
    assert "sys.path.insert" in source


def test_launch_reports_an_unknown_tool_without_raising(monkeypatch):
    """Un stub perime ne doit pas remonter une exception dans Clarisse."""
    messages = []
    from clarisse_add.core import log
    monkeypatch.setattr(log, "error", lambda message: messages.append(message))

    assert bootstrap.launch("outil.supprime", FakeIx()) is None
    assert messages and "outil.supprime" in messages[0]


def test_launch_catches_tool_failures(monkeypatch):
    """Une exception dans run() est journalisee, pas propagee."""
    reported = []
    from clarisse_add.core import log
    monkeypatch.setattr(log, "exception", lambda context: reported.append(context))

    tool = TOOLS[0]
    import importlib
    module = importlib.import_module(tool.module)
    monkeypatch.setattr(module, "run", _boom)

    assert bootstrap.launch(tool.id, FakeIx()) is None
    assert reported and tool.title in reported[0]


def _boom(payload=None):
    raise RuntimeError("echec simule")


def test_run_script_file_reports_a_missing_script(monkeypatch, tmp_path):
    messages = []
    from clarisse_add.core import log
    monkeypatch.setattr(log, "error", lambda message: messages.append(message))

    assert bootstrap.run_script_file(str(tmp_path / "absent.py"), FakeIx()) is False
    assert messages


def test_run_script_file_executes_with_ix_in_globals(tmp_path):
    """Les scripts repris tels quels attendent ``ix`` dans leurs globales."""
    script = tmp_path / "essai.py"
    script.write_text("resultat = ix.marqueur\n", encoding="utf-8")

    fake = FakeIx()
    fake.marqueur = 42
    assert bootstrap.run_script_file(str(script), fake) is True
