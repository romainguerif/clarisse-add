"""Genere une surface qui epouse la selection.

Un polygrid est place au-dessus de la bounding box de la selection, puis
deplace vers le bas par une texture d'occlusion dont les occluders sont les
objets selectionnes : la grille "retombe" sur la geometrie.  C'est la maniere
la plus rapide d'obtenir une surface de scatter propre au-dessus d'un decor
complexe, sans avoir a modeliser quoi que ce soit.

Un second montage, dans un sous-contexte ``Baking``, permet de cuire cette
hauteur en carte de displacement : materiau matte, camera, image en UV bake.

Portage du script ``113_ShrinkWrap.py`` de la collection.  Trois differences
avec l'original :

* le contexte est cree la ou l'artiste travaille, avec un nom unique, au lieu
  d'un ``ShrinkWrap`` fixe a la racine du projet qui empeche de lancer l'outil
  deux fois ;
* la resolution de grille et le rayon d'occlusion sont demandes plutot que
  codes en dur a 300 spans ;
* le montage de baking est optionnel : il cree une dizaine d'objets dont on n'a
  pas toujours besoin.
"""

from ..core import log, scene, ui
from ..core.compat import get_ix

DEFAULT_SPANS = 300


def run(payload=None):
    ix = get_ix()

    selected = scene.selection()
    if not selected:
        ui.message(
            "Selectionnez d'abord les objets a envelopper, puis relancez "
            "l'outil.",
            "Shrink Wrap",
        )
        return False

    settings = ui.Form(
        "Shrink Wrap",
        [
            ui.Section("Grille"),
            ui.Number("spans_x", "Subdivisions X", default=DEFAULT_SPANS,
                      minimum=4, maximum=2000, integer=True,
                      tooltip="Plus de subdivisions = plus de detail, mais une "
                              "grille plus lourde a deformer."),
            ui.Number("spans_z", "Subdivisions Z", default=DEFAULT_SPANS,
                      minimum=4, maximum=2000, integer=True),
            ui.Section("Occlusion"),
            ui.Number("radius_scale", "Rayon (x hauteur)", default=1.5,
                      minimum=0.1, maximum=10.0,
                      tooltip="Rayon de la recherche d'occlusion, exprime en "
                              "multiples de la hauteur de la bounding box."),
            ui.Number("quality", "Qualite", default=1, minimum=1, maximum=32,
                      integer=True),
            ui.Section("Sortie"),
            ui.Text("name", "Nom du contexte", default="ShrinkWrap"),
            ui.Toggle("baking", "Creer le montage de baking", default=False,
                      tooltip="Ajoute materiau, camera, raytracer et image en "
                              "UV bake pour cuire la hauteur en texture."),
        ],
        note="%d objet(s) selectionne(s)." % len(selected),
        accept_label="Creer",
    ).run()
    if settings is None:
        return False

    parent = scene.context_from_selection()
    if not scene.is_writable(parent):
        return False

    with scene.command_batch("ClarisseAdd - Shrink Wrap"):
        result = _build(ix, selected, parent, settings)
    return result is not None


def _build(ix, selected, parent, settings):
    ctx = scene.ensure_context(settings["name"] or "ShrinkWrap", parent)
    if ctx is None:
        return None

    combiner = ix.cmds.CreateObject("occluders_combiner", "SceneObjectCombiner", str(ctx))
    group = ix.cmds.CreateObject("occluders", "Group", str(ctx))
    for item in selected:
        full_name = item.get_full_name()
        ix.cmds.AddValues([str(combiner) + ".objects"], [full_name])
        ix.cmds.AddValues([str(group) + ".inclusion_items"], [full_name])

    # La bounding box n'est disponible qu'une fois le combiner evalue.
    ix.application.check_for_events()
    bbox = combiner.get_module().get_bbox()
    minimum, maximum = bbox[0], bbox[1]

    center = [(minimum[axis] + maximum[axis]) * 0.5 for axis in range(3)]
    width = maximum[0] - minimum[0]
    height = maximum[1] - minimum[1]
    depth = maximum[2] - minimum[2]

    if width <= 0 or depth <= 0:
        log.error(
            "La selection a une bounding box plate ou vide (%.3f x %.3f) : "
            "rien a envelopper." % (width, depth)
        )
        return None
    if height <= 0:
        # Une selection parfaitement plane laisserait un rayon d'occlusion nul,
        # donc une grille qui ne descend jamais.
        height = max(width, depth) * 0.1
        log.warning("Selection plate : hauteur d'occlusion estimee a %.3f" % height)

    radius = height * float(settings["radius_scale"])

    plane = ix.cmds.CreateObject("shrink_plane", "GeometryPolygrid", str(ctx))
    _place_grid(ix, plane, center, height, width, depth, settings)

    occlusion = ix.cmds.CreateObject("occlusion", "TextureOcclusion", str(ctx))
    ix.cmds.SetCurveKeyType([str(occlusion) + ".distance_falloff"], [0, 1, 0, 0, 2, 0])
    ix.cmds.SetValues([str(occlusion) + ".occlusion_mode"], ["2"])
    ix.cmds.SetValues([str(occlusion) + ".angle"], ["0.0"])
    ix.cmds.SetValues([str(occlusion) + ".quality"], [str(settings["quality"])])
    ix.cmds.SetValues([str(occlusion) + ".radius"], [str(radius)])
    ix.cmds.SetValues([str(occlusion) + ".color"],
                      [str(-radius), str(-radius), str(-radius)])
    ix.cmds.SetValues([str(occlusion) + ".occluders"], [str(group)])

    ix.cmds.AddValues([str(plane) + ".deformers"], ["DeformerDisplacement"])
    ix.cmds.SetValues([str(plane) + ".displacement.texture"], [str(occlusion)])
    ix.cmds.SetValues([str(plane) + ".displacement.local_deformation"], ["0"])
    ix.cmds.SetValues([str(plane) + ".displacement.displacement_axis"], ["1"])
    ix.cmds.CenterObjectsPivots([str(plane)], False)

    if settings.get("baking"):
        _build_baking(ix, ctx, occlusion, center, height, width, depth, radius, settings)

    ix.selection.deselect_all()
    ix.selection.add(plane)
    log.info("Shrink Wrap cree dans %s" % str(ctx))
    return ctx


def _place_grid(ix, grid, center, height, width, depth, settings):
    """Positionne et dimensionne un polygrid au-dessus de la bounding box."""
    ix.cmds.SetValues(
        [str(grid) + ".translate"],
        [str(center[0]), str(center[1] + height), str(center[2])],
    )
    ix.cmds.SetValues([str(grid) + ".spans[0]"], [str(settings["spans_x"])])
    ix.cmds.SetValues([str(grid) + ".spans[1]"], [str(settings["spans_z"])])
    # Le retrait d'un millieme evite que le bord de la grille coincide
    # exactement avec celui de la bounding box, ce qui produit un liseré
    # d'occlusion indefinie sur tout le pourtour.
    ix.cmds.SetValues([str(grid) + ".size[0]"], [str(width - 0.001)])
    ix.cmds.SetValues([str(grid) + ".size[1]"], [str(depth - 0.001)])


def _build_baking(ix, parent_ctx, occlusion, center, height, width, depth, radius, settings):
    """Montage de cuisson de la hauteur en carte de displacement."""
    ctx = ix.cmds.CreateContext("Baking", "Global", str(parent_ctx))

    material = ix.cmds.CreateObject("bake_mat", "MaterialMatte", "Global", str(ctx))
    rescale = ix.cmds.CreateObject("rescale", "TextureRescale", "Global", str(ctx))
    multiply = ix.cmds.CreateObject("multiply", "TextureMultiply", "Global", str(ctx))

    ix.cmds.SetTexture([str(material) + ".color"], str(rescale))
    ix.cmds.SetTexture([str(rescale) + ".input"], str(multiply))
    ix.cmds.SetTexture([str(multiply) + ".input1"], str(occlusion))
    ix.cmds.SetValues([str(multiply) + ".input2"], ["-1", "-1", "-1"])

    # L'occlusion sort en valeurs negatives (elle pousse la grille vers le bas) :
    # on la remet dans [0,1] en inversant les bornes de sortie.
    ix.cmds.SetValues([str(rescale) + ".output_min"], ["1", "1", "1"])
    ix.cmds.SetValues([str(rescale) + ".output_max"], ["0", "0", "0"])
    ix.cmds.SetValues([str(rescale) + ".input_max"],
                      [str(radius), str(radius), str(radius)])

    bake_plane = ix.cmds.CreateObject("bake_plane", "GeometryPolygrid", "Global", str(ctx))
    _place_grid(ix, bake_plane, center, height, width, depth, settings)
    ix.cmds.SetValues([str(bake_plane) + ".materials[0]"], [str(material)])

    raytracer = ix.cmds.CreateObject("raytracer", "RendererRaytracer", "Global", str(ctx))
    ix.cmds.SetValues([str(raytracer) + ".anti_aliasing_sample_count"], ["6"])
    ix.cmds.SetValues([str(raytracer) + ".anti_aliasing_filter"], ["3"])

    camera = ix.cmds.CreateObject("perspective", "CameraPerspective", "Global", str(ctx))

    image = ix.cmds.CreateObject("baked_render", "Image", "Global", str(ctx))
    ix.cmds.SetValues([str(image) + ".resolution[0]"], ["1024"])
    ix.cmds.SetValues([str(image) + ".resolution[1]"], ["1024"])
    ix.cmds.SetValues([str(image) + ".resolution_multiplier"], ["2"])
    ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
    ix.cmds.SetValues([str(image) + ".layer_3d.enable_uv_bake"], ["1"])
    ix.cmds.SetValues([str(image) + ".layer_3d.uv_bake_geometry"], [str(bake_plane)])
    ix.cmds.SetValues([str(image) + ".layer_3d.renderer"], [str(raytracer)])
    ix.cmds.SetValues([str(image) + ".layer_3d.active_camera"], [str(camera)])
    return ctx
