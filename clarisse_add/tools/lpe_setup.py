"""Genere un jeu de Light Path Expressions pret a compositer.

Creer une decomposition complete a la main demande une quinzaine d'objets, dont
chacun reclame une expression exacte : une parenthese de travers et l'AOV sort
noire sans rien signaler.  Les expressions utilisees ici viennent du fichier de
reference ``LPE_masterFile_vandam.project`` de la collection, complete par la
decomposition par composante habituelle.

L'outil lit aussi les labels LPE presents sur les lumieres de la scene
(attribut ``light_path_expression_label``) et peut generer, pour chacun, le
triplet direct / indirect / total : c'est ce qui permet de doser chaque lumiere
separement au compositing.
"""

from ..core import log, scene, ui
from ..core.compat import get_ix

#: (suffixe, expression, description).  ``%s`` est remplace par le label de
#: lumiere pour les expressions par groupe.
COMPONENT_LPES = [
    ("diffuse_direct", "C<RD>L", "Diffus, eclairage direct"),
    ("diffuse_indirect", "C<RD>[DSG]+L", "Diffus, rebonds indirects"),
    ("specular_direct", "C<RS>L", "Speculaire, eclairage direct"),
    ("specular_indirect", "C<RS>[DSG]+L", "Speculaire, rebonds indirects"),
    ("reflection", "C(S.*)|(G.*)L", "Reflexions, toutes profondeurs"),
    ("refraction", "CT.*L", "Refractions et transmission"),
    ("sss", "C<TD>.*L", "Diffusion sous-surfacique"),
    ("emission", "C.*O", "Emission des materiaux"),
]

GROUP_LPES = [
    ("direct", "CD<L.'%s'>", "Contribution directe de %s"),
    ("gi", "C<RD>[DS]+<L.'%s'>", "Contribution indirecte de %s"),
    ("full", "C.*<L.'%s'>", "Contribution totale de %s"),
]


def run(payload=None):
    ix = get_ix()

    labels = _light_labels(ix)
    note = ("%d label(s) LPE trouve(s) sur les lumieres : %s"
            % (len(labels), ", ".join(labels)) if labels
            else "Aucun label LPE sur les lumieres de la scene.")

    settings = ui.Form(
        "LPE Setup",
        [
            ui.Section("Contenu"),
            ui.Toggle("components", "Decomposition par composante", default=True,
                      tooltip="Diffus, speculaire, reflexion, refraction, SSS, "
                              "emission, en direct et indirect."),
            ui.Toggle("groups", "Un jeu par label de lumiere", default=bool(labels),
                      tooltip="Necessite que les lumieres portent un label LPE."),
            ui.Section("Sortie"),
            ui.Text("prefix", "Prefixe", default="LPE_"),
            ui.Text("context", "Contexte", default="lpe"),
            ui.Toggle("group_object", "Creer un Group qui les rassemble", default=True),
            ui.Toggle("assign", "Brancher sur l'image courante", default=True,
                      tooltip="Ecrit le Group dans l'attribut "
                              "light_path_expressions de l'image selectionnee "
                              "ou courante."),
        ],
        note=note,
        accept_label="Generer",
    ).run()
    if settings is None:
        return False

    if not settings["components"] and not settings["groups"]:
        ui.message("Rien a generer : cochez au moins une des deux options.",
                   "LPE Setup")
        return False

    parent = scene.context_from_selection()
    if not scene.is_writable(parent):
        return False

    with scene.command_batch("ClarisseAdd - LPE Setup"):
        created = _generate(ix, parent, labels, settings)

    if created is None:
        return False
    ui.message("%d Light Path Expressions creees." % created, "LPE Setup")
    return True


def _light_labels(ix):
    """Labels LPE distincts portes par les lumieres de la scene."""
    labels = []
    root = ix.application.get_factory().get_root()
    for obj in scene.iter_objects(root, kinds=("Light",)):
        attribute = obj.get_attribute("light_path_expression_label")
        if attribute is None:
            continue
        value = str(attribute.get_string()).strip()
        if value and value not in labels:
            labels.append(value)
    return labels


def _generate(ix, parent, labels, settings):
    ctx = scene.ensure_context(settings["context"] or "lpe", parent)
    if ctx is None:
        return None

    prefix = settings["prefix"] or ""
    definitions = []

    if settings["components"]:
        definitions.extend(COMPONENT_LPES)

    if settings["groups"]:
        if not labels:
            log.warning(
                "Aucun label LPE sur les lumieres : le jeu par groupe est "
                "ignore. Renseignez light_path_expression_label sur vos "
                "lumieres (le Light Manager le fait en une colonne)."
            )
        for label in labels:
            safe = _sanitize(label)
            for suffix, template, description in GROUP_LPES:
                definitions.append((
                    "%s_%s" % (safe, suffix),
                    template % label,
                    description % label,
                ))

    created = []
    for suffix, expression, description in definitions:
        name = scene.unique_name(prefix + suffix, ctx)
        try:
            lpe = ix.cmds.CreateObject(name, "LightPathExpression", "Global", str(ctx))
        except Exception:
            log.exception("Creation de la LPE '%s'" % name)
            continue
        ix.cmds.SetValues([str(lpe) + ".expression"], [expression])
        ix.cmds.SetValues([str(lpe) + ".output"], [name])
        created.append(lpe)
        log.debug("LPE %s = %s (%s)" % (name, expression, description))

    if not created:
        log.error("Aucune LPE creee.")
        return None

    group = None
    if settings["group_object"]:
        group = ix.cmds.CreateObject(
            scene.unique_name(prefix + "group", ctx), "Group", "Global", str(ctx)
        )
        ix.cmds.SetValues([str(group) + ".filter"], ["LightPathExpression"])
        # La regle "*" fait du groupe un filtre vivant : une LPE ajoutee plus
        # tard dans ce contexte y entre toute seule.
        ix.cmds.SetValues([str(group) + ".inclusion_rule"], ["*"])
        ix.cmds.AddValues([str(group) + ".inclusion_items"],
                          [str(item) for item in created])

    if settings["assign"] and group is not None:
        _assign_to_image(ix, group)

    log.info("%d LPE creees dans %s" % (len(created), str(ctx)))
    return len(created)


def _assign_to_image(ix, group):
    """Branche le groupe sur l'image selectionnee, sinon sur la premiere trouvee."""
    images = scene.selected_of_kind("Image")
    if not images:
        root = ix.application.get_factory().get_root()
        images = list(scene.iter_objects(root, kinds=("Image",)))
    if not images:
        log.warning("Aucune image dans la scene : le groupe de LPE n'est branche nulle part.")
        return False

    image = images[0]
    if image.get_attribute("light_path_expressions") is None:
        log.warning("%s n'expose pas d'attribut light_path_expressions." % str(image))
        return False
    ix.cmds.SetValues([str(image) + ".light_path_expressions"], [str(group)])
    log.info("Groupe de LPE branche sur %s" % str(image))
    return True


def _sanitize(label):
    """Rend un label utilisable comme nom d'objet Clarisse."""
    cleaned = []
    for character in label:
        cleaned.append(character if character.isalnum() or character == "_" else "_")
    return "".join(cleaned).strip("_") or "light"
