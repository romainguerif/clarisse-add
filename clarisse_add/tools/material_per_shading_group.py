"""Un materiau par shading group de la geometrie selectionnee.

Une geometrie importee depuis un DCC arrive avec ses shading groups, mais un
seul materiau par defaut : il faut ensuite en creer un par groupe a la main et
les assigner un par un.  Cet outil le fait en une fois.

Portage de ``MaterialPerShadingGroup.txt``, avec le choix de la classe de
materiau, un apercu du nombre de groupes trouves, et le traitement de toute la
selection au lieu du seul premier objet.
"""

from ..core import log, scene, ui
from ..core.compat import get_ix

MATERIAL_CLASSES = [
    "MaterialPhysicalStandard",
    "MaterialPhysicalDiffuse",
    "MaterialPhysicalAutodeskStandardSurface",
    "MaterialPhysicalDisneyPrincipled",
    "MaterialMatte",
]


def run(payload=None):
    ix = get_ix()

    geometries = [item for item in scene.selection()
                  if item.is_kindof("SceneObject") and _shading_group_count(item)]
    if not geometries:
        ui.message(
            "Selectionnez une ou plusieurs geometries possedant des shading "
            "groups, puis relancez l'outil.",
            "Material per Shading Group",
        )
        return False

    total = sum(_shading_group_count(item) for item in geometries)

    settings = ui.Form(
        "Material per Shading Group",
        [
            ui.Choice("material_class", "Classe de materiau", MATERIAL_CLASSES, default=0),
            ui.Text("suffix", "Suffixe", default="_mtl"),
            ui.Toggle("lowercase", "Forcer en minuscules", default=True),
            ui.Toggle("one_context_each", "Un contexte par shading group",
                      default=True,
                      tooltip="Decoche pour creer tous les materiaux a plat "
                              "dans le contexte de destination."),
            ui.Toggle("assign", "Assigner aux geometries", default=True),
        ],
        note="%d geometrie(s), %d shading group(s)." % (len(geometries), total),
        accept_label="Creer",
    ).run()
    if settings is None:
        return False

    target = ui.pick_context("Destination des materiaux")
    if target is None:
        return False
    if not scene.is_writable(target):
        return False

    created = 0
    with scene.command_batch("ClarisseAdd - Material per Shading Group"):
        for geometry in geometries:
            created += _process(ix, geometry, target, settings)

    log.info("%d materiaux crees dans %s" % (created, str(target)))
    ui.message("%d materiaux crees dans\n%s" % (created, str(target)),
               "Material per Shading Group")
    return True


def _shading_group_count(item):
    """Nombre de shading groups, 0 si l'objet n'en expose pas."""
    module = item.get_module()
    if module is None or not hasattr(module, "get_shading_group_count"):
        return 0
    try:
        return module.get_shading_group_count()
    except Exception:
        return 0


def _process(ix, geometry, target, settings):
    module = geometry.get_module()
    count = module.get_shading_group_count()
    created = 0

    for index in range(count):
        group_name = str(module.get_shading_group(index))
        material_name = group_name + (settings["suffix"] or "")
        if settings["lowercase"]:
            material_name = material_name.lower()

        if settings["one_context_each"]:
            # Un contexte par groupe : c'est ce qui permet d'y ranger ensuite
            # les textures du materiau sans tout melanger.
            context = ix.cmds.CreateContext(
                scene.unique_name(group_name, target), "Global", str(target)
            )
        else:
            context = target

        material = ix.cmds.CreateObject(
            scene.unique_name(material_name, context),
            settings["material_class"], "Global", str(context),
        )
        created += 1

        if settings["assign"]:
            try:
                module.assign_material(material.get_module(), index)
            except Exception:
                log.exception(
                    "Assignation du materiau '%s' au shading group %d de %s"
                    % (material_name, index, geometry.get_name())
                )
    return created
