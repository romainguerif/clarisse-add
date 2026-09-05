"""Fusionne un preset de la bibliotheque, puis expose ses reglages.

C'est ce qui transforme une scene ``.project`` en outil.  Une scene comme
``Wall Maker`` ou ``Window Box`` n'est pas un exemple a regarder : c'est un
montage parametrable, dont les reglages sont declares dans le fichier sous forme
d'attributs custom.  L'addon les lit (:mod:`clarisse_add.core.project_file`), les
met en cache dans le catalogue, et genere la fenetre de reglages a partir de la
declaration.

Rien n'est code en dur par preset : ajouter un ``.project`` porteur d'attributs
custom a la bibliotheque suffit a obtenir un outil avec son panneau.
"""

from ..core import log, scene, ui
from ..core.compat import get_ix
from ..presets import catalog


def run(payload=None):
    ix = get_ix()
    if not payload:
        log.error("preset_runner appele sans identifiant de preset")
        return False

    entry = catalog.by_id(payload)
    if entry is None:
        log.error("Preset inconnu : %s" % payload)
        return False
    if not entry.exists():
        log.error("Fichier du preset introuvable : %s" % entry.path)
        return False

    if entry.missing_files:
        details = "\n".join("  " + item for item in entry.missing_files[:6])
        if not ui.confirm(
            "%s reference %d fichier(s) absent(s) :\n%s\n\n"
            "La scene sera fusionnee mais ces elements resteront vides.\n"
            "Continuer ?" % (entry.title, len(entry.missing_files), details),
            entry.title,
        ):
            return False

    target = ui.pick_context("%s : contexte de destination" % entry.title)
    if target is None:
        return False

    with scene.command_batch("ClarisseAdd - %s" % entry.title):
        merged = scene.merge_project(entry.path, target)
    if merged is None:
        return False

    if not entry.parameters:
        log.info("%s fusionne dans %s" % (entry.title, str(target)))
        return True

    _edit_parameters(ix, entry, target)
    return True


# ---------------------------------------------------------------------------


def _edit_parameters(ix, entry, target):
    """Ouvre la fenetre de reglages generee, puis applique les valeurs."""
    resolved = _resolve_owners(ix, entry, target)
    if not resolved:
        log.warning(
            "%s : objets parametrables introuvables apres la fusion. "
            "Les reglages restent accessibles dans l'Attribute Editor."
            % entry.title
        )
        return

    fields, bindings = _build_fields(entry, resolved)
    if not fields:
        return

    values = ui.Form(
        "%s - reglages" % entry.title,
        fields,
        note="Applique sur %s" % str(target),
        accept_label="Appliquer",
    ).run()
    if values is None:
        return

    applied = 0
    with scene.command_batch("ClarisseAdd - %s (reglages)" % entry.title):
        for key, (obj, attribute_name, component) in bindings.items():
            if key not in values:
                continue
            if component is None:
                if scene.set_attribute(obj, attribute_name, values[key]):
                    applied += 1
            else:
                # Attribut multi-composantes : on ecrit composante par
                # composante pour ne pas ecraser celles que l'artiste n'a
                # pas touchees dans le formulaire.
                target_path = "%s.%s[%d]" % (str(obj), attribute_name, component)
                try:
                    ix.cmds.SetValues([target_path], [str(values[key])])
                    applied += 1
                except Exception:
                    log.exception("Ecriture de %s" % target_path)
    log.info("%s : %d reglages appliques" % (entry.title, applied))


def _resolve_owners(ix, entry, target):
    """``{chemin d'origine: objet}`` apres fusion.

    Le chemin note dans le catalogue est celui du fichier source
    (``scene/materials/window_box``).  Apres fusion il devient
    ``<contexte>/scene/materials/window_box`` -- sauf si Clarisse a du
    renommer un contexte pour eviter une collision.  On tente donc le chemin
    direct, puis on se rabat sur une recherche par nom d'objet dans le
    sous-arbre.
    """
    owners = []
    for parameter in entry.parameters:
        if parameter.owner and parameter.owner not in owners:
            owners.append(parameter.owner)

    base = str(target).rstrip("/")
    resolved = {}
    fallback_index = None

    for owner in owners:
        obj = ix.item_exists(base + "/" + owner)
        if obj is not None:
            resolved[owner] = obj
            continue
        if fallback_index is None:
            fallback_index = _index_by_name(target)
        leaf = owner.rsplit("/", 1)[-1]
        candidates = fallback_index.get(leaf, [])
        if len(candidates) == 1:
            resolved[owner] = candidates[0]
            log.debug("Objet %s retrouve en %s" % (owner, str(candidates[0])))
        elif candidates:
            log.warning(
                "%s : plusieurs objets nommes '%s' apres la fusion, reglages "
                "ignores pour celui-ci." % (entry.title, leaf)
            )
    return resolved


def _index_by_name(ctx):
    """``{nom court: [objets]}`` pour tout le sous-arbre d'un contexte."""
    index = {}
    for obj in scene.iter_objects(ctx):
        index.setdefault(obj.get_name(), []).append(obj)
    return index


def _build_fields(entry, resolved):
    """Traduit les parametres declares en champs de formulaire.

    Renvoie ``(champs, {cle: (objet, attribut, composante)})``.
    """
    fields = []
    bindings = {}
    current_section = None

    for (owner, group), parameters in entry.parameter_groups:
        obj = resolved.get(owner)
        if obj is None:
            continue
        section = " / ".join(part for part in (owner.rsplit("/", 1)[-1], group) if part)
        if section != current_section:
            fields.append(ui.Section(section))
            current_section = section

        for parameter in parameters:
            key_base = "%s::%s" % (owner, parameter.name)
            default = parameter.default

            if parameter.presets:
                labels = [(label, value) for label, value in parameter.presets]
                index = 0
                for position, (_label, value) in enumerate(labels):
                    if str(value) == str(default):
                        index = position
                        break
                fields.append(ui.Choice(key_base, parameter.label, labels,
                                        default=index, tooltip=parameter.doc))
                bindings[key_base] = (obj, parameter.name, None)
                continue

            if isinstance(default, list):
                # Vecteur (taille de brique, dimensions de mur...) : un champ
                # par composante, sinon l'artiste ne peut rien regler.
                for component, value in enumerate(default):
                    key = "%s[%d]" % (key_base, component)
                    fields.append(ui.Number(
                        key,
                        "%s [%d]" % (parameter.label, component),
                        default=_as_float(value),
                        minimum=parameter.minimum,
                        maximum=parameter.maximum,
                        integer=parameter.is_integer,
                        tooltip=parameter.doc,
                    ))
                    bindings[key] = (obj, parameter.name, component)
                continue

            if parameter.is_numeric:
                fields.append(ui.Number(
                    key_base, parameter.label,
                    default=_as_float(default),
                    minimum=parameter.minimum,
                    maximum=parameter.maximum,
                    integer=parameter.is_integer,
                    tooltip=parameter.doc,
                ))
            elif parameter.type == "bool":
                fields.append(ui.Toggle(key_base, parameter.label,
                                        default=bool(default), tooltip=parameter.doc))
            elif parameter.type in ("filename_open", "filename_save", "filename"):
                fields.append(ui.FilePath(key_base, parameter.label,
                                          default=default or "", tooltip=parameter.doc))
            else:
                fields.append(ui.Text(key_base, parameter.label,
                                      default=default if default is not None else "",
                                      tooltip=parameter.doc))
            bindings[key_base] = (obj, parameter.name, None)

    return fields, bindings


def _as_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0
