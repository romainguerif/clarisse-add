"""Tests de l'ecriture du ``shelf.cfg``.

Le point critique n'est pas que l'addon s'installe : c'est qu'il n'abime rien.
Un ``shelf.cfg`` contient les boutons que l'artiste a ajoutes lui-meme depuis
des annees, et il n'y en a qu'un exemplaire.  Ces tests verifient que
l'installation est idempotente, qu'elle ne touche que ses propres categories, et
qu'elle laisse un fichier toujours relisible.
"""

import io
import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from clarisse_add import manifest  # noqa: E402
from clarisse_add.core import project_file, shelf  # noqa: E402

EXISTING = """#Isotropix_Serial_Version 1.2

shelf {
    slot_selected 0
    category_selected "Perso"
    show_toolbar yes
    style 0
    view_mode 0
    slot 0 {
        category "Perso" {
            shelf_item {
                title "Mon script"
                description "Un bouton ajoute a la main"
                script_filename "C:/scripts/mon_script.py"
                icon_filename ""
            }
        }
    }
}
"""


@pytest.fixture
def shelf_file(tmp_path):
    target = tmp_path / "shelf.cfg"
    target.write_text(EXISTING, encoding="utf-8")
    return str(target)


def read(path):
    with io.open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def categories(path):
    parsed = project_file.parse_string(read(path))
    found = {}
    for node in parsed.iter_objects(skip_embedded=False):
        if node.class_name == "category":
            found[str(node.label)] = len(node.children)
    return found


def test_install_adds_categories(shelf_file):
    tools = manifest.all_tools()
    report = shelf.install(tools, shelf_file, manifest.PREFIX,
                           select_category=manifest.CATEGORY_MAIN)

    assert report["items"] == len(tools)
    assert report["categories"] == len(manifest.categories())

    found = categories(shelf_file)
    for category in manifest.categories():
        assert category in found


def test_install_preserves_user_categories(shelf_file):
    shelf.install(manifest.all_tools(), shelf_file, manifest.PREFIX)
    found = categories(shelf_file)
    assert found["Perso"] == 1
    assert 'script_filename "C:/scripts/mon_script.py"' in read(shelf_file)


def test_install_is_idempotent(shelf_file):
    tools = manifest.all_tools()
    shelf.install(tools, shelf_file, manifest.PREFIX)
    first = read(shelf_file)
    report = shelf.install(tools, shelf_file, manifest.PREFIX)
    second = read(shelf_file)

    assert report["replaced"] == len(manifest.categories())
    assert first == second, "une seconde installation doit rendre le meme fichier"


def test_install_only_touches_one_line_besides_its_own(shelf_file):
    """Hors ses categories, seul `category_selected` change."""
    before = read(shelf_file).splitlines()
    shelf.install(manifest.all_tools(), shelf_file, manifest.PREFIX,
                  select_category=manifest.CATEGORY_MAIN)
    after = read(shelf_file).splitlines()

    removed = [line for line in before if line not in after]
    assert removed == ['    category_selected "Perso"']


def test_uninstall_restores_the_file(shelf_file):
    original = read(shelf_file)
    shelf.install(manifest.all_tools(), shelf_file, manifest.PREFIX)
    removed = shelf.uninstall(shelf_file, manifest.PREFIX)

    assert removed == len(manifest.categories())
    assert categories(shelf_file) == {"Perso": 1}
    # `category_selected` reste sur ClarisseAdd : desinstaller ne devine pas
    # quel onglet l'artiste veut retrouver. Le reste doit etre identique.
    assert read(shelf_file).replace('"ClarisseAdd"', '"Perso"') == original


def test_install_creates_missing_file(tmp_path):
    target = str(tmp_path / "nouveau" / "shelf.cfg")
    report = shelf.install(manifest.all_tools(), target, manifest.PREFIX,
                           select_category=manifest.CATEGORY_MAIN)
    assert report["created"] is True
    assert os.path.isfile(target)
    assert set(categories(target)) == set(manifest.categories())


def test_install_backs_up(shelf_file):
    report = shelf.install(manifest.all_tools(), shelf_file, manifest.PREFIX)
    assert report["backup"] and os.path.isfile(report["backup"])
    assert read(report["backup"]) == EXISTING


def test_rejects_corrupt_file(tmp_path):
    target = tmp_path / "shelf.cfg"
    target.write_text("#Isotropix_Serial_Version 1.2\n\nautre_chose {\n}\n",
                      encoding="utf-8")
    with pytest.raises(ValueError):
        shelf.install(manifest.all_tools(), str(target), manifest.PREFIX)


def test_quoting_escapes_quotes():
    assert shelf._quote('a"b') == '"a\\"b"'
    assert shelf._quote("C:\\x") == '"C:\\\\x"'


def test_rendered_block_is_parsable():
    """Le bloc genere doit se relire, sinon Clarisse jette tout le fichier."""
    block = "\n".join(shelf.render_categories(manifest.all_tools()))
    wrapped = "#Isotropix_Serial_Version 1.2\n\nshelf {\n    slot 0 {\n%s\n    }\n}\n" % block
    parsed = project_file.parse_string(wrapped)
    assert parsed.root.children[0].class_name == "shelf"
