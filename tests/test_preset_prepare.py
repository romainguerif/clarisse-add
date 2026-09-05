"""Non-regression du premier essai de Wall Maker.

Le mur sortait vide : ``$PDIR`` designe le dossier du projet *courant* de
Clarisse, pas celui du fichier fusionne.  Dans une scene non sauvegardee,
``$PDIR/geo/Bricks/Brick_Flat.obj`` devenait ``/geo/Bricks/Brick_Flat.obj``.

Deux defauts de lecture du format se cachaient derriere : ``distance[3]`` et
``long[2]`` n'etaient pas reconnus comme numeriques, et les valeurs par defaut
etaient reecrites telles quelles dans la scene a la validation du formulaire.
"""

import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from clarisse_add.core import paths  # noqa: E402
from clarisse_add.core.project_file import CustomAttribute, parse_string  # noqa: E402
from clarisse_add.presets import catalog  # noqa: E402
from clarisse_add.tools import preset_runner  # noqa: E402

PROJECT_WITH_PDIR = b"""#Isotropix_Serial_Version 1.2

Context "scene" {
    GeometryPolyfile {
        name "brick"
        filename "$PDIR/geo/Bricks/Brick_Flat.obj"
    }
    TextureMapFile {
        name "decal"
        filename "$PDIR/decal.png"
    }
}
"""

PROJECT_WITHOUT_PDIR = b"""#Isotropix_Serial_Version 1.2

Context "scene" {
    GeometryBox {
        name "box"
    }
}
"""


@pytest.fixture
def library(tmp_path, monkeypatch):
    """Une bibliotheque de presets temporaire, et un cache d'addon temporaire."""
    presets = tmp_path / "presets"
    (presets / "wall").mkdir(parents=True)
    (presets / "wall" / "Wall.project").write_bytes(PROJECT_WITH_PDIR)
    (presets / "plain").mkdir()
    (presets / "plain" / "Plain.project").write_bytes(PROJECT_WITHOUT_PDIR)
    monkeypatch.setattr(paths, "PRESETS_DIR", str(presets))
    monkeypatch.setattr(paths, "ADDON_ROOT", str(tmp_path / "addon"))
    return presets


def entry(slug, filename):
    return catalog.PresetEntry({"id": slug, "directory": slug, "filename": filename})


def test_prepared_path_expands_pdir_to_the_preset_directory(library):
    wall = entry("wall", "Wall.project")
    prepared = wall.prepared_path()

    assert prepared != wall.path
    assert prepared.endswith(".cache/presets/wall.project")
    data = open(prepared, "rb").read()
    assert b"$PDIR" not in data
    expected = paths.normalize(str(library / "wall")).encode("utf-8")
    assert data.count(expected) == 2
    assert expected + b"/geo/Bricks/Brick_Flat.obj" in data


def test_prepared_path_uses_forward_slashes_only(library):
    data = open(entry("wall", "Wall.project").prepared_path(), "rb").read()
    line = [l for l in data.splitlines() if b"Brick_Flat" in l][0]
    assert b"\\" not in line


def test_prepared_path_preserves_everything_else(library):
    """Seul le jeton change : ni encodage, ni fins de ligne, ni structure."""
    data = open(entry("wall", "Wall.project").prepared_path(), "rb").read()
    directory = paths.normalize(str(library / "wall")).encode("utf-8")
    assert data.replace(directory, b"$PDIR") == PROJECT_WITH_PDIR
    parse_string(data.decode("utf-8"))  # toujours un .project valide


def test_prepared_path_returns_the_original_when_pdir_is_absent(library):
    plain = entry("plain", "Plain.project")
    assert plain.prepared_path() == plain.path
    assert not os.path.exists(os.path.join(str(library.parent / "addon"), ".cache"))


def test_prepared_path_is_rewritten_each_time(library):
    wall = entry("wall", "Wall.project")
    first = wall.prepared_path()
    open(first, "wb").write(b"corrompu")
    second = wall.prepared_path()
    assert first == second
    assert b"Brick_Flat" in open(second, "rb").read()


def test_library_presets_using_pdir_are_the_expected_ones():
    """Garde-fou sur la vraie bibliotheque : ce qui reference $PDIR."""
    using = sorted(e.id for e in catalog.entries() if e.uses_pdir())
    assert "wall_maker" in using
    assert "window_box" in using
    assert "cactus" not in using


# ---------------------------------------------------------------------------
# Types a arite : distance[3], long[2]
# ---------------------------------------------------------------------------

WALL_CONTROL = """#Isotropix_Serial_Version 1.2

Context "Setup" {
    Locator {
        name "Control"
        custom_attributes {
            distance[3] "Brick_Size" {
                doc "Brick Size"
                value 2.04 0.66 1
            }
            long[2] "Wall_Size" {
                value 58 20
            }
            double "Rows_Offset" {
                value 1.02
            }
            string "Label" {
                value "mur"
            }
        }
    }
}
"""


def test_arity_types_are_numeric():
    control = parse_string(WALL_CONTROL).root.find_one("Locator")
    attributes = {a.name: a for a in control.custom_attributes()}

    brick = attributes["Brick_Size"]
    assert brick.type == "distance[3]"
    assert brick.base_type == "distance"
    assert brick.is_numeric and not brick.is_integer
    assert brick.default() == [2.04, 0.66, 1.0]

    wall = attributes["Wall_Size"]
    assert wall.base_type == "long"
    assert wall.is_numeric and wall.is_integer
    assert wall.default() == [58, 20]

    assert attributes["Rows_Offset"].default() == 1.02
    assert attributes["Label"].is_numeric is False
    assert attributes["Label"].default() == "mur"


def test_catalog_parameter_mirrors_the_type_logic():
    wall = catalog.Parameter({"name": "Wall_Size", "type": "long[2]", "default": [58, 20]})
    assert wall.is_numeric and wall.is_integer
    brick = catalog.Parameter({"name": "Brick_Size", "type": "distance[3]"})
    assert brick.is_numeric and not brick.is_integer


def test_real_wall_maker_exposes_numeric_parameters():
    wall = catalog.by_id("wall_maker")
    if wall is None:
        pytest.skip("bibliotheque absente")
    by_name = {p.name: p for p in wall.parameters}
    assert by_name["Brick_Size"].is_numeric
    assert by_name["Wall_Size"].is_integer
    assert isinstance(by_name["Brick_Size"].default, list)
    assert len(by_name["Brick_Size"].default) == 3


# ---------------------------------------------------------------------------
# N'ecrire que ce qui a change
# ---------------------------------------------------------------------------


def test_changed_detects_real_differences():
    changed = preset_runner._changed
    assert changed(2.04, 2.04) is False
    assert changed(2.0400000001, 2.04) is False
    assert changed(3.0, 2.04) is True
    assert changed(58, 58) is False
    assert changed(58.0, 58) is False
    assert changed(True, True) is False
    assert changed(False, True) is True
    assert changed("outRGB", "outRGB") is False
    assert changed("outA", "outRGB") is True
    assert changed(1.0, None) is True


def test_serialize_writes_integers_without_decimal():
    serialize = preset_runner._serialize
    assert serialize(58.0) == "58"
    assert serialize(2.04) == "2.04"
    assert serialize(True) == "1"
    assert serialize(False) == "0"
    assert serialize("texte") == "texte"
