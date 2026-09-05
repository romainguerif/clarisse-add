"""Genere ``clarisse_add/presets/catalog.json`` depuis ``assets/presets``.

Script de developpement : il tourne avec n'importe quel Python 3, hors de
Clarisse.  A relancer apres avoir ajoute un ``.project`` dans la bibliotheque.

    python tools/build_catalog.py

Les metadonnees editoriales (titre, description, credit, categorie, presence
dans le shelf) sont ecrites a la main dans :data:`METADATA` ci-dessous ; tout le
reste est extrait du fichier lui-meme.
"""

from __future__ import print_function

import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from clarisse_add.core.project_file import parse, ParseError  # noqa: E402

PRESETS_DIR = os.path.join(ROOT, "assets", "presets")
OUTPUT = os.path.join(ROOT, "clarisse_add", "presets", "catalog.json")


# ---------------------------------------------------------------------------
# Metadonnees ecrites a la main
# ---------------------------------------------------------------------------

#: slug -> (titre, categorie, credit, dans_le_shelf, description)
METADATA = {
    "wall_maker": (
        "Wall Maker", "Geometrie", "Isotropix / communaute", True,
        "Mur de briques procedural : la geometrie des briques est instanciee "
        "sur une grille, avec decalage de rang et variation aleatoire.",
    ),
    "window_box": (
        "Window Box", "Shading", "jiWindowBox (Julien Ignace) - portage OSL", True,
        "Interieur factice vu au travers d'une fenetre, en une seule texture "
        "OSL : profondeur de piece, arriere-plan, rideaux, sans geometrie.",
    ),
    "osl_noise_collection": (
        "OSL Noise Collection", "Shading", "communaute Clarisse", True,
        "Neuf textures OSL de bruit (Perlin, Worley, Voronoi, flow, fractal) "
        "pretes a brancher, chacune avec ses parametres exposes.",
    ),
    "micro_scratch": (
        "Micro Scratch", "Shading", "communaute Clarisse", True,
        "Rayures fines procedurales pour la rugosite et le speculaire, avec "
        "densite, orientation et contraste reglables.",
    ),
    "tilable_maker": (
        "Tileable Maker", "Shading", "communaute Clarisse", True,
        "Assemble une texture repetable a partir d'une source non repetable, "
        "par recouvrement et fondu des bords.",
    ),
    "chain_fence_scatterer": (
        "Chain Fence Scatterer", "Scatter", "communaute Clarisse", True,
        "Grillage genere par scatter : le maillon est instancie le long d'une "
        "grille de points, avec variation d'orientation.",
    ),
    "face_to_camera_scatterer": (
        "Face To Camera Scatterer", "Scatter", "communaute Clarisse", True,
        "Scatter dont chaque instance s'oriente vers la camera : billboards, "
        "cartes de vegetation, particules.",
    ),
    "radial_gradient": (
        "Radial Gradient", "Shading", "communaute Clarisse", True,
        "Degrade radial procedural construit sur un bruit cellulaire, "
        "utilisable comme masque ou comme selecteur.",
    ),
    "scatterer_lean_out": (
        "Scatterer Lean Out", "Scatter", "communaute Clarisse", False,
        "Scatter dont les instances s'inclinent vers l'exterieur en fonction "
        "d'une occlusion, pour eviter les interpenetrations.",
    ),
    "instance_color": (
        "Instance Color (attribut custom)", "Scatter", "communaute Clarisse", False,
        "Colorisation par instance pilotee par un attribut custom lu dans le "
        "materiau : chaque copie recoit sa propre teinte.",
    ),
    "menger_sponge": (
        "Menger Sponge", "Geometrie", "communaute Clarisse", False,
        "Eponge de Menger construite par douze niveaux de scatter imbriques. "
        "Bon test de charge pour l'instanciation.",
    ),
    "points_circle": (
        "Points on Circle", "Scatter", "communaute Clarisse", False,
        "Nuage de points dispose en cercle, rayon et nombre de points "
        "parametrables.",
    ),
    "road_decimate": (
        "Road Decimate", "Geometrie", "communaute Clarisse", False,
        "Decimation d'une bande de route par nuage de points : garde la "
        "densite la ou la courbure l'exige.",
    ),
    "procedural_grass": (
        "Procedural Grass", "Shading", "communaute Clarisse", False,
        "Herbe entierement procedurale : dix bruits fractals melanges pour la "
        "couleur, la longueur et la variation par touffe.",
    ),
    "mix_normals": (
        "Mix Normals", "Shading", "communaute Clarisse", False,
        "Melange correct de deux normal maps (pas une simple moyenne), pour "
        "superposer un detail sur une normale de base.",
    ),
    "rgb_displacement": (
        "RGB Displacement Material", "Shading", "Demian Hernandez", False,
        "Displacement pilote par les trois canaux d'une seule texture : trois "
        "hauteurs independantes dans un fichier.",
    ),
    "lpe_master": (
        "LPE Master File", "Rendu", "vandam", False,
        "Seize Light Path Expressions pretes a l'emploi, couvrant la "
        "decomposition complete diffuse / speculaire / transmission / SSS.",
    ),
    "decals": (
        "Decals", "Shading", "communaute Clarisse", False,
        "Systeme de decals projetes par Scope, avec materiau en couches.",
    ),
    "decal_multi_material": (
        "Decal Multi Material", "Shading", "communaute Clarisse", False,
        "Variante multi-materiaux du systeme de decals : plusieurs decals "
        "empiles sur une meme surface.",
    ),
    "decal_perso": (
        "Decal (variante)", "Shading", "communaute Clarisse", False,
        "Decal sur materiau Disney Principled, avec reorientation des "
        "normales.",
    ),
    "cactus": (
        "Cactus", "Geometrie", "communaute Clarisse", False,
        "Cactus procedural : cinq degrades pilotent la forme, les epines et la "
        "coloration.",
    ),
    "ocean": (
        "Ocean", "Shading", "Demian Hernandez", False,
        "Surface d'ocean procedurale : sept bruits melanges pour la houle, "
        "l'ecume et la variation de rugosite.",
    ),
    "desert": (
        "Desert", "Environnement", "Demian Hernandez", False,
        "Environnement desertique complet : dunes, dispersion de rochers et "
        "shading en trente et un contextes. Scene lourde.",
    ),
}

DEFAULT_METADATA = ("", "Divers", "", False, "")


# ---------------------------------------------------------------------------


def describe(directory, filename):
    """Analyse un ``.project`` et renvoie son entree de catalogue."""
    slug = os.path.basename(directory)
    full_path = os.path.join(directory, filename)
    title, category, credit, shelf, description = METADATA.get(slug, DEFAULT_METADATA)
    if not title:
        title = os.path.splitext(filename)[0].replace("_", " ")

    entry = {
        "id": slug,
        "title": title,
        "description": description,
        "category": category,
        "credit": credit,
        "shelf": shelf,
        "directory": slug,
        "filename": filename,
    }

    try:
        project = parse(full_path)
    except ParseError as error:
        print("  ! %s : %s" % (slug, error))
        entry["object_count"] = 0
        entry["classes"] = {}
        entry["parameters"] = []
        entry["external_files"] = []
        entry["missing_files"] = []
        return entry

    histogram = project.class_histogram()
    entry["object_count"] = sum(histogram.values())
    # On ne garde que les vraies classes Clarisse : les blocs en minuscules
    # sont des sous-structures d'attribut (input1, color, value[]), pas des
    # objets, et polluent l'inventaire affiche a l'artiste.
    entry["classes"] = {
        name: count for name, count in histogram.items()
        if name[:1].isupper()
    }

    parameters = []
    for node, attributes in project.parameterized_objects():
        owner = node.path.replace("project://", "")
        for attribute in attributes:
            parameters.append({
                "owner": owner,
                "name": attribute.name,
                "type": attribute.type,
                "group": attribute.group,
                "doc": attribute.doc,
                "default": attribute.default(),
                "minimum": attribute.minimum,
                "maximum": attribute.maximum,
                "presets": [list(pair) for pair in attribute.presets],
            })
    entry["parameters"] = parameters

    # Les chemins relatifs a $PDIR sont resolus a cote du .project : ce sont
    # ceux qu'on peut verifier. Les chemins absolus pointent vers la machine
    # d'origine et sont signales tels quels.
    external, missing = [], []
    for _owner, _attribute, value in project.external_files():
        if value in external:
            continue
        external.append(value)
        resolved = value.replace("$PDIR", directory)
        if "$" in resolved or resolved.startswith(("project:", "//")):
            continue
        if not os.path.isabs(resolved):
            resolved = os.path.join(directory, resolved)
        if not os.path.exists(resolved):
            missing.append(value)
    entry["external_files"] = external
    entry["missing_files"] = missing
    return entry


def main():
    if not os.path.isdir(PRESETS_DIR):
        print("Dossier de presets introuvable : %s" % PRESETS_DIR)
        return 1

    presets = []
    for slug in sorted(os.listdir(PRESETS_DIR)):
        directory = os.path.join(PRESETS_DIR, slug)
        if not os.path.isdir(directory):
            continue
        project_files = sorted(
            name for name in os.listdir(directory) if name.endswith(".project")
        )
        if not project_files:
            print("  - %s : aucun .project, ignore" % slug)
            continue
        if len(project_files) > 1:
            print("  - %s : %d .project, on prend %s"
                  % (slug, len(project_files), project_files[0]))
        entry = describe(directory, project_files[0])
        presets.append(entry)
        flag = " [shelf]" if entry["shelf"] else ""
        warn = " MANQUANTS:%d" % len(entry["missing_files"]) if entry["missing_files"] else ""
        print("  + %-26s %3d objets, %2d parametres%s%s"
              % (slug, entry["object_count"], len(entry["parameters"]), flag, warn))

    payload = {"version": 1, "presets": presets}
    with io.open(OUTPUT, "w", encoding="utf-8") as handle:
        handle.write(json.dumps(payload, indent=2, ensure_ascii=False, sort_keys=False))
    print("\n%d presets ecrits dans %s" % (len(presets), os.path.relpath(OUTPUT, ROOT)))

    unknown = sorted(set(entry["id"] for entry in presets) - set(METADATA))
    if unknown:
        print("Sans metadonnees editoriales : %s" % ", ".join(unknown))
    return 0


if __name__ == "__main__":
    sys.exit(main())
