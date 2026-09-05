"""Monte un squelette de build BUiLDER : le graphe de base, deja cable.

Un build part toujours de la meme forme -- faire entrer les assets, les editer
sans les abimer, assembler, definir le rendu, ecrire.  Ces six nodes se posent
a la main en dix minutes, a chaque plan, et on se trompe une fois sur trois sur
le sens des branchements.

L'outil les pose d'un coup :

    Read Project ---> Edit ---> Merge ---> Render Scene ---> Image Node Render
                                              |                      |
                                           AOV Set          Image Node Write

Deux familles d'objets, a ne pas confondre -- c'est la source d'erreur
principale quand on scripte BUiLDER :

* les nodes d'assemblage de scene (Read Project, Edit, Merge, Override,
  Isolate, Prune) sont des **contextes** portant un moteur, crees par
  ``ix.cmds.CreateCustomContext(nom, moteur, chemin)`` ;
* les autres (Render Scene, AOV Set, Rule Set, Switch, Extract, les Image
  Node et les Process) sont des **objets** ordinaires, crees par
  ``ix.cmds.CreateObject``.

Les noms de moteurs viennent des modules livres avec Clarisse
(``module/scene_assembly_*.dll``), pas d'une supposition.
"""

from ..core import log, scene, ui
from ..core.compat import get_ix

#: Racine des builds. En iFX le vizroot y est fixe ; en BUiLDER c'est la
#: racine ou vivent les nodes d'assemblage.
BUILD_ROOT = "build://project"

#: Moteurs de contexte, releves dans module/scene_assembly_*.dll.
ENGINE_READ_PROJECT = "SceneAssemblyReadProject"
ENGINE_READ_ABC = "SceneAssemblyReadAbc"
ENGINE_EDIT = "SceneAssemblyEdit"
ENGINE_MERGE = "SceneAssemblyMerge"
ENGINE_OVERRIDE = "SceneAssemblyOverride"

#: Attribut d'entree commun aux nodes d'assemblage (confirme sur
#: SceneAssemblyExtract dans la documentation de reference).
INPUT_ATTR = "input_context"


def run(payload=None):
    ix = get_ix()

    if ix.item_exists(BUILD_ROOT) is None:
        ui.message(
            "La racine %s est introuvable.\n\n"
            "Lancez Clarisse en saveur BUiLDER : le raccourci "
            "\"Clarisse BUiLDER 5.0 SP14\" du menu Demarrer, ou retirez "
            "l'argument -flavor ifx de votre raccourci." % BUILD_ROOT,
            "Nouveau build",
        )
        return False

    settings = ui.Form(
        "Nouveau build",
        [
            ui.Section("Identite"),
            ui.Text("name", "Nom du plan", default="sh010",
                    tooltip="Sert de prefixe a tous les nodes crees."),
            ui.Section("Source"),
            ui.FilePath("project", "Projet a faire entrer", default="",
                        tooltip="Un .project existant. Laissez vide pour "
                                "creer le Read Project sans fichier."),
            ui.Section("Rendu"),
            ui.Toggle("aov_set", "Ajouter un AOV Set", default=True,
                      tooltip="A remplir ensuite avec le bouton LPE Setup."),
            ui.Toggle("comp", "Ajouter le rendu et l'ecriture d'image",
                      default=True),
            ui.Section("Diagnostic"),
            ui.Toggle("introspect", "Journaliser les attributs de chaque node",
                      default=True,
                      tooltip="Ecrit dans le journal la liste reelle des "
                              "attributs de chaque node cree. Laissez coche "
                              "tant que l'outil n'est pas stabilise."),
        ],
        note="Les nodes seront crees dans %s" % BUILD_ROOT,
        accept_label="Construire",
    ).run()
    if settings is None:
        return False

    name = _sanitize(settings["name"]) or "build"

    with scene.command_batch("ClarisseAdd - Nouveau build"):
        report = _build(ix, name, settings)

    _report(report, settings)
    return True


# ---------------------------------------------------------------------------


def _build(ix, name, settings):
    """Cree le graphe et renvoie un compte-rendu de ce qui a reussi."""
    report = {"created": [], "wired": [], "failed": [], "attrs": {}}
    made = {}

    def context(node_name, engine):
        """Un node d'assemblage : contexte + moteur."""
        full = "%s_%s" % (name, node_name)
        try:
            item = ix.cmds.CreateCustomContext(full, engine, BUILD_ROOT)
        except Exception as error:
            report["failed"].append("%s (moteur %s) : %s"
                                    % (full, engine, _short(error)))
            log.exception("CreateCustomContext %s / %s" % (full, engine))
            return None
        if item is None:
            report["failed"].append("%s : moteur '%s' inconnu" % (full, engine))
            return None
        report["created"].append("%s  [%s]" % (full, engine))
        made[node_name] = item
        return item

    def obj(node_name, class_name):
        """Un node ordinaire."""
        full = "%s_%s" % (name, node_name)
        try:
            item = ix.cmds.CreateObject(full, class_name, "Global", BUILD_ROOT)
        except Exception as error:
            report["failed"].append("%s (classe %s) : %s"
                                    % (full, class_name, _short(error)))
            log.exception("CreateObject %s / %s" % (full, class_name))
            return None
        if item is None:
            report["failed"].append("%s : classe '%s' indisponible "
                                    "(licence BUiLDER ?)" % (full, class_name))
            return None
        report["created"].append("%s  [%s]" % (full, class_name))
        made[node_name] = item
        return item

    def wire(target, attribute, value, label):
        """Branche un attribut, et dit clairement s'il n'existe pas."""
        if target is None:
            return False
        if target.get_attribute(attribute) is None:
            report["failed"].append("%s : pas d'attribut '%s'"
                                    % (str(target), attribute))
            return False
        try:
            ix.cmds.SetValues([str(target) + "." + attribute], [str(value)])
        except Exception as error:
            report["failed"].append("%s.%s : %s"
                                    % (str(target), attribute, _short(error)))
            return False
        report["wired"].append(label)
        return True

    # -- la chaine d'assemblage -------------------------------------------

    read = context("read", ENGINE_READ_PROJECT)
    project = (settings.get("project") or "").strip()
    if read is not None and project:
        # Le nom de l'attribut de fichier n'est pas documente pour ce moteur :
        # on essaie les candidats plausibles et on signale lequel a pris.
        for candidate in ("filename", "file", "filenames", "project_filename"):
            if read.get_attribute(candidate) is not None:
                wire(read, candidate, project, "read.%s" % candidate)
                break
        else:
            report["failed"].append(
                "Read Project : aucun attribut de fichier reconnu "
                "(voir le journal pour la liste reelle)")

    edit = context("edit", ENGINE_EDIT)
    if read is not None:
        wire(edit, INPUT_ATTR, str(read), "edit <- read")

    merge = context("merge", ENGINE_MERGE)
    if edit is not None and merge is not None:
        # Le Merge accepte plusieurs entrees : son attribut est une liste.
        for candidate in ("dependencies", INPUT_ATTR, "inputs"):
            if merge.get_attribute(candidate) is not None:
                try:
                    ix.cmds.AddValues([str(merge) + "." + candidate], [str(edit)])
                    report["wired"].append("merge <- edit (%s)" % candidate)
                except Exception:
                    wire(merge, candidate, str(edit), "merge <- edit")
                break
        else:
            report["failed"].append("Merge : aucun attribut d'entree reconnu")

    # -- le rendu ----------------------------------------------------------

    render_scene = obj("render_scene", "RenderScene")
    source = merge if merge is not None else edit
    if source is not None and render_scene is not None:
        for candidate in (INPUT_ATTR, "scene", "context"):
            if render_scene.get_attribute(candidate) is not None:
                wire(render_scene, candidate, str(source),
                     "render_scene <- %s" % source.get_name())
                break

    if settings.get("aov_set"):
        aov = obj("aovs", "AovSet")
        if aov is not None and render_scene is not None:
            for candidate in ("aov_set", "aovs", "aov_sets"):
                if render_scene.get_attribute(candidate) is not None:
                    wire(render_scene, candidate, str(aov), "render_scene <- aovs")
                    break

    # -- l'image -----------------------------------------------------------

    if settings.get("comp"):
        image_render = obj("render", "ImageNodeRender")
        if image_render is not None and render_scene is not None:
            wire(image_render, "scene", str(render_scene), "render <- render_scene")
        write = obj("write", "ImageNodeWrite")
        if write is not None and image_render is not None:
            for candidate in ("input", INPUT_ATTR, "image"):
                if write.get_attribute(candidate) is not None:
                    wire(write, candidate, str(image_render), "write <- render")
                    break

    # -- introspection -----------------------------------------------------

    if settings.get("introspect"):
        for node_name, item in made.items():
            report["attrs"][node_name] = _attributes(item)

    return report


def _attributes(item):
    """Noms et types des attributs d'un objet, pour le journal."""
    found = []
    try:
        count = item.get_attribute_count()
    except Exception:
        return found
    for index in range(count):
        attr = item.get_attribute(index)
        if attr is None:
            continue
        try:
            found.append("%s:%s" % (str(attr.get_name()), str(attr.get_type_name())))
        except Exception:
            found.append(str(attr.get_name()))
    return found


def _report(report, settings):
    lines = ["%d node(s) cree(s)" % len(report["created"])]
    lines.extend("  " + item for item in report["created"])
    if report["wired"]:
        lines.append("")
        lines.append("%d branchement(s)" % len(report["wired"]))
        lines.extend("  " + item for item in report["wired"])
    if report["failed"]:
        lines.append("")
        lines.append("%d point(s) a verifier" % len(report["failed"]))
        lines.extend("  " + item for item in report["failed"])

    message = "\n".join(lines)
    log.info("Nouveau build : %d nodes, %d branchements, %d echecs"
             % (len(report["created"]), len(report["wired"]), len(report["failed"])))

    if settings.get("introspect") and report["attrs"]:
        log.debug("--- attributs reels des nodes crees ---")
        for node_name, attrs in sorted(report["attrs"].items()):
            log.debug("%s : %s" % (node_name, ", ".join(attrs)))
        message += ("\n\nLes attributs reels de chaque node sont dans le "
                    "journal :\n%s" % _log_path())

    if report["failed"]:
        message += ("\n\nPosez le vizroot sur un node (touche V) pour voir "
                    "ou en est l'assemblage.")
    ui.message(message, "Nouveau build")


def _log_path():
    from ..core import paths
    return paths.log_file()


def _short(error):
    text = str(error).strip().splitlines()
    return text[-1][:120] if text else error.__class__.__name__


def _sanitize(name):
    cleaned = "".join(c if (c.isalnum() or c == "_") else "_" for c in (name or ""))
    return cleaned.strip("_")

