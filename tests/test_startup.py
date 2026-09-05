"""Tests du crochet de demarrage ecrit dans ``clarisse.env``.

Le fichier ne nous appartient pas : il porte aussi les chemins Python de
Clarisse, et une seule ligne fausse empeche l'application de trouver son
interpreteur. Ces tests verifient donc surtout ce qu'on **ne** touche pas.
"""

import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from clarisse_add.core import startup  # noqa: E402

# Le fichier tel que Clarisse le livre : la variable existe et elle est vide.
LIVRE = u"""IX_PYTHON3HOME=C:\\Python37
IX_PYTHON3PATH=C:\\Python37\\Lib;C:\\Python37\\DLLs
IX_SHELF_CONFIG_FILE=$IX_PYTHON_API_PATH/shelves/shelf.cfg
CLARISSE_STARTUP_SCRIPT=
"""

HOOK = os.path.normpath("C:/addon/clarisse_add/startup.py")
AUTRE = os.path.normpath("D:/studio/pipeline_init.py")


def write(folder, text=LIVRE):
    path = os.path.join(str(folder), "clarisse.env")
    with io.open(path, "w", encoding="utf-8", newline="\r\n") as handle:
        handle.write(text)
    return path


def read(path):
    with io.open(path, encoding="utf-8") as handle:
        return handle.read()


def test_fichier_absent_ne_leve_pas(tmpdir):
    absent = os.path.join(str(tmpdir), "nulle-part", "clarisse.env")
    assert startup.scripts(absent) == []
    assert startup.is_enabled(absent, HOOK) is False


def test_variable_vide_ne_compte_aucun_script(tmpdir):
    path = write(tmpdir)
    assert startup.scripts(path) == []


def test_enable_remplace_la_ligne_existante(tmpdir):
    path = write(tmpdir)
    changed, saved = startup.enable(path, HOOK)

    assert changed is True
    assert saved is not None and os.path.isfile(saved)
    assert startup.is_enabled(path, HOOK)
    # La ligne est remplacee, pas ajoutee : une seule occurrence.
    assert read(path).count("CLARISSE_STARTUP_SCRIPT=") == 1


def test_enable_est_idempotent(tmpdir):
    path = write(tmpdir)
    startup.enable(path, HOOK)
    apres_un = read(path)

    changed, saved = startup.enable(path, HOOK)
    assert changed is False
    assert saved is None
    assert read(path) == apres_un


def test_enable_preserve_les_autres_variables(tmpdir):
    path = write(tmpdir)
    startup.enable(path, HOOK)
    contenu = read(path)

    for ligne in LIVRE.splitlines():
        if ligne.startswith("CLARISSE_STARTUP_SCRIPT"):
            continue
        assert ligne in contenu


def test_enable_ajoute_apres_un_script_existant(tmpdir):
    path = write(tmpdir, LIVRE.replace("CLARISSE_STARTUP_SCRIPT=",
                                       "CLARISSE_STARTUP_SCRIPT=" + AUTRE))
    startup.enable(path, HOOK)

    declares = [os.path.normpath(p) for p in startup.scripts(path)]
    assert declares == [AUTRE, HOOK]


def test_enable_ajoute_la_ligne_si_elle_manque(tmpdir):
    sans = u"IX_PYTHON3HOME=C:\\Python37\n"
    path = write(tmpdir, sans)
    startup.enable(path, HOOK)

    assert startup.is_enabled(path, HOOK)
    assert "IX_PYTHON3HOME" in read(path)


def test_disable_ne_retire_que_le_notre(tmpdir):
    path = write(tmpdir, LIVRE.replace("CLARISSE_STARTUP_SCRIPT=",
                                       "CLARISSE_STARTUP_SCRIPT=" + AUTRE))
    startup.enable(path, HOOK)
    changed, _ = startup.disable(path, HOOK)

    assert changed is True
    assert [os.path.normpath(p) for p in startup.scripts(path)] == [AUTRE]


def test_disable_sans_rien_a_faire(tmpdir):
    path = write(tmpdir)
    changed, saved = startup.disable(path, HOOK)
    assert changed is False
    assert saved is None


def test_le_lanceur_est_ecrit_et_importe_l_addon(tmpdir):
    """Clarisse execute ce fichier par PyRun_String : `__file__` n'y existe pas.

    Le lanceur porte donc la racine en dur -- c'est tout l'interet de le
    generer -- et il doit pointer sur un depot reel.
    """
    hook = startup.write_hook(str(tmpdir))

    assert os.path.isfile(hook)
    contenu = io.open(hook, encoding="utf-8").read()
    assert "clarisse_add" in contenu
    # La racine est inscrite en dur : c'est tout l'interet du lanceur.
    assert 'ROOT = r"%s"' % startup.addon_root() in contenu
    # Et elle n'est jamais deduite : aucune ligne de code ne lit __file__.
    code = [line for line in contenu.splitlines()
            if line.strip() and not line.strip().startswith("#")]
    assert not any("__file__" in line for line in code)
    assert os.path.isdir(startup.addon_root())
    assert os.path.isfile(os.path.join(startup.addon_root(),
                                       "clarisse_add", "startup.py"))

    # Il doit se compiler : une erreur de syntaxe ne se verrait qu'au
    # demarrage de Clarisse, la ou personne ne lit la console.
    compile(contenu, hook, "exec")


def test_remove_hook(tmpdir):
    startup.write_hook(str(tmpdir))
    assert startup.remove_hook(str(tmpdir)) is True
    assert startup.remove_hook(str(tmpdir)) is False
