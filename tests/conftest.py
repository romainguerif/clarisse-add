"""Isolation des tests : aucun test n'ecrit dans le vrai journal de l'addon.

Sans ca, chaque ``pytest`` laissait une vingtaine de lignes ``Shelf mis a jour
... pytest-of-Anon ...`` dans ``%APPDATA%/Isotropix/Clarisse/clarisse_add.log``,
melees aux vraies traces de Clarisse -- ce qui rend le journal illisible au
moment precis ou l'on en a besoin, apres un crash.
"""

import logging
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)


def _drop_handlers():
    logger = logging.getLogger("clarisse_add")
    for handler in list(logger.handlers):
        try:
            handler.close()
        except Exception:
            pass
        logger.removeHandler(handler)


@pytest.fixture(autouse=True)
def isolated_log(tmp_path, monkeypatch):
    """Redirige le journal vers un fichier temporaire, pour chaque test.

    Le chemin passe par une variable d'environnement et non par un monkeypatch
    de ``paths.log_file`` : les tests de rechargement purgent ``sys.modules``,
    et un attribut patche sur un module qui n'existe plus ne protege rien.  Le
    logger Python nomme ``clarisse_add`` est lui global au processus ; on vide
    ses handlers avant et apres, sinon le premier fichier ouvert reste accroche
    pour toute la session.
    """
    monkeypatch.setenv("CLARISSE_ADD_LOG", str(tmp_path / "clarisse_add.log"))
    _drop_handlers()
    for name, module in list(sys.modules.items()):
        if name == "clarisse_add.core.log" and hasattr(module, "_logger"):
            module._logger = None
    yield
    _drop_handlers()
    for name, module in list(sys.modules.items()):
        if name == "clarisse_add.core.log" and hasattr(module, "_logger"):
            module._logger = None
