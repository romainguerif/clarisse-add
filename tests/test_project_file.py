"""Tests du lecteur de fichiers ``.project``.

Ils tournent sans Clarisse : le parser est volontairement independant de ``ix``.
Les cas non triviaux sont valides sur les vrais fichiers de ``assets/presets``,
jamais sur des extraits inventes -- un format proprietaire ne se devine pas.
"""

import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from clarisse_add.core.project_file import (  # noqa: E402
    ParseError, parse, parse_string,
)

PRESETS = os.path.join(ROOT, "assets", "presets")

SAMPLE = """#Isotropix_Serial_Version 1.2

#Isotropix_Clarisse_Version 4
Context "scene" {
    #created 1455809270
    CameraPerspective {
        name "camera"
        translate 28 21 28
        field_of_view 25
        objects "project://scene/box" "project://scene/sphere"
    }
    GeometryBox {
        name "box"
        private yes
        custom_attributes {
            attribute_group "input" {
                double "size" {
                    doc "Taille de la boite"
                    ui_range yes 0.1 100
                    value 2.5
                }
                long "mode" {
                    doc "Mode"
                    preset "Rapide" "0"
                    preset "Precis" "1"
                    value 1
                }
            }
        }
    }
}
"""


def test_headers():
    project = parse_string(SAMPLE)
    assert project.headers["Isotropix_Serial_Version"] == "1.2"
    assert project.clarisse_version == "4"


def test_tree_structure():
    project = parse_string(SAMPLE)
    contexts = project.contexts()
    assert len(contexts) == 1
    assert contexts[0].name == "scene"

    camera = project.root.find_one("CameraPerspective")
    assert camera is not None
    assert camera.name == "camera"
    assert camera.path == "project://scene/camera"


def test_attribute_values():
    camera = parse_string(SAMPLE).root.find_one("CameraPerspective")
    assert camera.get_all("translate") == ["28", "21", "28"]
    assert camera.get_float("translate", 1) == 21.0
    assert camera.get_float("field_of_view") == 25.0
    # Les valeurs entre guillemets sont bien dequotees.
    assert camera.get_all("objects") == [
        "project://scene/box", "project://scene/sphere",
    ]


def test_bare_keyword_attribute():
    """`private yes` est un attribut, pas un en-tete de bloc."""
    box = parse_string(SAMPLE).root.find_one("GeometryBox")
    assert box.get_bool("private") is True


def test_custom_attributes():
    box = parse_string(SAMPLE).root.find_one("GeometryBox")
    attributes = box.custom_attributes()
    assert [a.name for a in attributes] == ["size", "mode"]

    size = attributes[0]
    assert size.type == "double"
    assert size.group == "input"
    assert size.doc == "Taille de la boite"
    assert size.default() == 2.5
    assert (size.minimum, size.maximum) == (0.1, 100.0)
    assert size.is_numeric

    mode = attributes[1]
    assert mode.default() == 1
    assert mode.presets == [("Rapide", "0"), ("Precis", "1")]


def test_custom_attributes_are_not_objects():
    """Les declarations d'attributs ne doivent pas polluer l'inventaire."""
    project = parse_string(SAMPLE)
    histogram = project.class_histogram()
    assert "double" not in histogram
    assert "attribute_group" not in histogram
    assert histogram["GeometryBox"] == 1


def test_parameterized_objects():
    project = parse_string(SAMPLE)
    parameterized = project.parameterized_objects()
    assert len(parameterized) == 1
    node, attributes = parameterized[0]
    assert node.class_name == "GeometryBox"
    assert len(attributes) == 2


def test_line_ranges():
    """Les bornes de bloc servent a retoucher un fichier sans le reecrire."""
    project = parse_string(SAMPLE)
    context = project.contexts()[0]
    assert context.line < context.end_line
    lines = SAMPLE.splitlines()
    assert lines[context.line - 1].strip().startswith('Context "scene"')
    assert lines[context.end_line - 1].strip() == "}"


def test_unbalanced_braces():
    with pytest.raises(ParseError):
        parse_string('Context "a" {\n    GeometryBox {\n        name "b"\n    }\n')


def test_rejects_non_project_file(tmp_path):
    target = tmp_path / "notes.txt"
    target.write_text("ceci n'est pas un projet", encoding="utf-8")
    with pytest.raises(ParseError):
        parse(str(target))


# ---------------------------------------------------------------------------
# Sur les vrais fichiers de la bibliotheque
# ---------------------------------------------------------------------------


def real_projects():
    if not os.path.isdir(PRESETS):
        return []
    found = []
    for slug in sorted(os.listdir(PRESETS)):
        directory = os.path.join(PRESETS, slug)
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            if name.endswith(".project"):
                found.append(os.path.join(directory, name))
                break
    return found


@pytest.mark.parametrize("path", real_projects(), ids=lambda p: os.path.basename(p))
def test_real_project_parses(path):
    project = parse(path)
    assert project.root.children, "%s n'a produit aucun bloc" % path
    # Toute scene contient au moins un objet de classe Clarisse.
    classes = [name for name in project.class_histogram() if name[:1].isupper()]
    assert classes


def test_window_box_exposes_its_parameters():
    """Le preset le plus parametre de la bibliotheque, en garde-fou."""
    path = os.path.join(PRESETS, "window_box", "WindowBox.project")
    if not os.path.isfile(path):
        pytest.skip("bibliotheque de presets absente")
    project = parse(path)
    parameterized = project.parameterized_objects()
    assert parameterized
    names = [attribute.name
             for _node, attributes in parameterized
             for attribute in attributes]
    assert "OSL_roomDepth" in names
