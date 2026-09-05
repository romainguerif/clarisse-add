"""Tests du choix de version fait par l'installeur.

Clarisse laisse ses preferences derriere lui quand on le desinstalle, et une
version d'essai ouverte une seule fois suffit a creer un dossier de
configuration.  Se caler sur le numero de version le plus eleve trouve dans
``%APPDATA%`` revient donc, tot ou tard, a ecrire un shelf que rien ne lira.

C'est exactement le cas sur la machine de developpement : une configuration 5.5
traine alors que seul Clarisse 5.0 SP14 est installe.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import install  # noqa: E402
from clarisse_add.core import paths  # noqa: E402


def test_prefers_a_version_that_is_actually_installed():
    applications = {"5.0": "C:/Program Files/Isotropix/Clarisse 5.0 SP14"}
    configs = ["5.5", "5.0"]
    assert install.choose_version(applications, configs) == "5.0"


def test_prefers_the_most_recent_when_several_are_installed():
    applications = {"5.0": "/a", "5.5": "/b"}
    assert install.choose_version(applications, ["5.5", "5.0"]) == "5.5"


def test_falls_back_to_the_application_when_no_config_exists_yet():
    """Premiere installation : la config naitra au premier lancement."""
    assert install.choose_version({"5.0": "/a"}, []) == "5.0"


def test_falls_back_to_the_config_when_no_application_is_found():
    """Installation hors des emplacements standards : on ne bloque pas."""
    assert install.choose_version({}, ["5.0"]) == "5.0"


def test_returns_none_when_there_is_nothing_to_target():
    assert install.choose_version({}, []) is None


def test_application_directory_names_are_recognised(tmp_path, monkeypatch):
    """Isotropix a nomme ses dossiers de plusieurs facons selon les versions."""
    root = tmp_path / "Isotropix"
    root.mkdir()
    for name in ("Clarisse 5.0 SP14", "Clarisse iFX 4.0 SP11", "Clarisse 5.5",
                 "Autre chose", "Isotropix Ilise"):
        (root / name).mkdir()

    monkeypatch.setattr(paths, "_application_roots", lambda: [str(root)])
    found = paths.installed_applications()

    assert sorted(found) == ["4.0", "5.0", "5.5"]
    assert found["5.0"].endswith("Clarisse 5.0 SP14")


def test_missing_application_root_is_not_an_error(monkeypatch):
    monkeypatch.setattr(paths, "_application_roots", lambda: ["/n/existe/pas"])
    assert paths.installed_applications() == {}
