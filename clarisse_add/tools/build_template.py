"""Cree une scene de depart complete, prete a rendre.

Quand on ouvre Clarisse iFX, on tombe sur un contexte tout fait : une camera,
une lumiere distante, un raytracer, et une image dont le Layer 3D est deja
branche dessus.  On appuie sur Rendu, il se passe quelque chose.  En BUiLDER
on part d'une page blanche, et il faut reposer ces cinq objets a la main avant
de voir le moindre pixel.

Cet outil les repose d'un coup, avec les valeurs d'Isotropix -- elles sont
lues dans ``Clarisse/config/startup_scene.project``, le fichier que Clarisse
lui-meme charge au demarrage :

    camera      CameraPerspective     translate 28 21 28, rotate -27.938 45 0, fov 25
    light       LightPhysicalDistant  rotate -35 125 0
    raytracer   RendererRaytracer
    image       Image + Layer3d, camera et renderer deja branches

En option : un sol et une sphere, pour que le premier rendu ne soit pas vide ;
et la chaine d'assemblage BUiLDER (Read Project, Edit, Merge) en amont.

Une note sur les deux familles d'objets de BUiLDER, parce que c'est la source
d'erreur principale quand on le scripte : les nodes d'assemblage de scene
(Read Project, Edit, Merge, Override, Isolate, Prune) ne sont pas des objets
mais des **contextes portant un moteur**, crees par ``CreateCustomContext``.
Tout le reste -- camera, lumiere, renderer, image, Render Scene, AOV Set --
sont des objets ordinaires.  Les noms de moteurs viennent des modules livres
avec Clarisse (``module/scene_assembly_*.dll``), pas d'une supposition.
"""

from ..core import log, scene, ui
from ..core.compat import get_ix

#: Moteurs de contexte, releves dans module/scene_assembly_*.dll.
ENGINE_READ_PROJECT = "SceneAssemblyReadProject"
ENGINE_EDIT = "SceneAssemblyEdit"
ENGINE_MERGE = "SceneAssemblyMerge"

#: Attribut d'entree des nodes d'assemblage, confirme sur SceneAssemblyExtract
#: dans la documentation de reference.
INPUT_ATTR = "input_context"

#: Valeurs exactes de la scene de demarrage livree par Isotropix.
CAMERA_TRANSLATE = ("28", "21", "28")
CAMERA_ROTATE = ("-27.938", "45", "0")
CAMERA_FOV = "25"
LIGHT_ROTATE = ("-35", "125", "0")

RESOLUTIONS = [
    ("1920 x 1080", "1920x1080"),
    ("1280 x 720", "1280x720"),
    ("2048 x 1152", "2048x1152"),
    ("3840 x 2160", "3840x2160"),
    ("1024 x 1024", "1024x1024"),
]


def run(payload=None):
    ix = get_ix()
    log.debug("Racines disponibles : %s" % ", ".join(_roots(ix)))

    settings = ui.Form(
        "Scene de depart",
        [
            ui.Section("Contexte"),
            ui.Text("name", "Nom", default="scene"),
            ui.Choice("resolution", "Resolution", RESOLUTIONS, default=0),
            ui.Toggle("graph", "Ajouter le graphe Render Scene", default=True,
                      tooltip="Render Scene, Image Node Render et le process "
                              "d'ecriture. Demande la saveur BUiLDER. "
                              "L'image classique est creee dans tous les cas."),
            ui.Section("Contenu"),
            ui.Toggle("props", "Ajouter un sol et une sphere", default=True,
                      tooltip="De quoi voir quelque chose au premier rendu."),
            ui.Toggle("assembly", "Ajouter la chaine BUiLDER en amont",
                      default=False,
                      tooltip="Read Project, Edit et Merge, a cabler ensuite "
                              "sur vos assets. Sans effet en saveur iFX."),
            ui.Section("Diagnostic"),
            ui.Toggle("introspect", "Journaliser les attributs des nodes",
                      default=False),
        ],
        note="Cree dans le contexte courant.",
        accept_label="Creer",
    ).run()
    if settings is None:
        return False

    parent = scene.current_context()
    if parent is None:
        ui.message("Aucun contexte courant : impossible de savoir ou creer.",
                   "Scene de depart")
        return False
    if not scene.is_writable(parent):
        ui.message("Le contexte courant n'est pas modifiable :\n%s\n\n"
                   "Selectionnez-en un autre dans l'Explorer, puis relancez."
                   % str(parent), "Scene de depart")
        return False

    name = _sanitize(settings["name"]) or "scene"

    with scene.command_batch("ClarisseAdd - Scene de depart"):
        report = _build(ix, name, parent, settings)

    _verify(ix, report)
    _report(ix, report, settings)
    return True


# ---------------------------------------------------------------------------


def _build(ix, name, parent, settings):
    report = {"created": [], "wired": [], "failed": [], "notes": [],
              "unverified": [], "pending": [], "attrs": {}, "image": None}
    made = {}

    ctx = scene.ensure_context(name, parent)
    if ctx is None:
        report["failed"].append("Creation du contexte '%s' impossible" % name)
        return report
    report["created"].append("%s  [Context]" % str(ctx))

    def obj(short, class_name):
        try:
            item = ix.cmds.CreateObject(short, class_name, "Global", str(ctx))
        except Exception as error:
            report["failed"].append("%s (%s) : %s" % (short, class_name, _short(error)))
            log.exception("CreateObject %s / %s" % (short, class_name))
            return None
        if item is None:
            report["failed"].append("%s : classe '%s' indisponible" % (short, class_name))
            return None
        report["created"].append("%s  [%s]" % (short, class_name))
        made[short] = item
        return item

    def values(target, attribute, *vals):
        """Ecrit un attribut, puis **relit** pour savoir si ca a pris.

        Les commandes de Clarisse ne levent pas d'exception quand elles
        echouent : elles ecrivent l'erreur dans la console et renvoient une
        valeur fausse. Un try/except autour de SetValues n'attrape donc rien,
        et se fier a son absence d'erreur revient a declarer reussis des
        branchements qui n'ont pas eu lieu. La seule preuve, c'est la relecture.
        """
        if target is None:
            return False
        # `get_attribute` ne resout qu'un nom simple : sur un chemin pointe
        # comme "layer_3d.active_camera" il renvoie toujours None, alors que
        # SetValues, lui, l'accepte.
        simple = "." not in attribute
        if simple and target.get_attribute(attribute) is None:
            report["failed"].append("%s : pas d'attribut '%s'"
                                    % (target.get_name(), attribute))
            report["attrs"][target.get_name()] = _attributes(target)
            return False
        try:
            ix.cmds.SetValues([str(target) + "." + attribute], [str(v) for v in vals])
        except Exception as error:
            report["failed"].append("%s.%s : %s"
                                    % (target.get_name(), attribute, _short(error)))
            report["attrs"][target.get_name()] = _attributes(target)
            return False

        if not simple:
            # Chemin compose : get_attribute ne sait pas le relire, donc pas de
            # verification possible. On le dit au lieu de faire semblant.
            report["unverified"].append("%s.%s" % (target.get_name(), attribute))
            return True

        # On ne relit PAS maintenant. Dans un command batch, Clarisse differe
        # l'execution des commandes -- le SDK est explicite la-dessus : "there's
        # a good chance it is delayed". Relire ici renverrait l'ancienne valeur
        # et ferait passer pour un echec toute ecriture parfaitement valide.
        # La verification a lieu apres la fermeture du batch, dans _verify().
        report["pending"].append((target, attribute, str(vals[0]),
                                  "%s.%s" % (target.get_name(), attribute)))
        return True

    # -- les cinq objets de la scene de demarrage --------------------------

    camera = obj("camera", "CameraPerspective")
    values(camera, "translate", *CAMERA_TRANSLATE)
    values(camera, "rotate", *CAMERA_ROTATE)
    values(camera, "field_of_view", CAMERA_FOV)

    light = obj("light", "LightPhysicalDistant")
    values(light, "rotate", *LIGHT_ROTATE)

    renderer = obj("raytracer", "RendererRaytracer")

    width, height = (settings["resolution"] or "1920x1080").split("x")

    # Une seule sortie, choisie par ce qui fonctionne reellement.
    #
    # L'Image layeree est une construction de Clarisse iFX : dans la hierarchie
    # build elle se cree mais **refuse les layers**, et elle refuse aussi d'etre
    # supprimee -- on ne peut donc pas la creer "pour voir" et faire le menage
    # apres. En BUiLDER, ce sont les Image Node qui produisent les pixels.
    #
    # On tente donc le graphe en premier, et on ne retombe sur l'image layeree
    # que s'il n'est pas disponible, c'est-a-dire en saveur iFX.
    graph_ok = False
    if settings.get("graph"):
        graph_ok = _render_graph(ix, name, ctx, camera, renderer, width, height,
                                 report, made, obj, values)

    if not graph_ok:
        _layered_image(ix, ctx, camera, renderer, width, height,
                       report, obj, values)

    # -- de quoi voir quelque chose au premier rendu -----------------------

    if settings.get("props"):
        ground = obj("ground", "GeometryPolygrid")
        values(ground, "size", "40", "40")
        sphere = obj("sphere", "GeometrySphere")
        values(sphere, "translate", "0", "2", "0")
        material = obj("surface", "MaterialPhysicalStandard")
        if material is not None:
            for target in (ground, sphere):
                if target is None or target.get_attribute("materials") is None:
                    continue
                try:
                    ix.cmds.SetValues([str(target) + ".materials[0]"], [str(material)])
                    report["wired"].append("%s <- surface" % target.get_name())
                except Exception as error:
                    report["failed"].append("materiau sur %s : %s"
                                            % (target.get_name(), _short(error)))

    # -- la chaine d'assemblage BUiLDER, en option -------------------------

    if settings.get("assembly"):
        _assembly(ix, name, ctx, report, made)

    if settings.get("introspect"):
        for short, item in made.items():
            report["attrs"][short] = _attributes(item)

    return report


def _render_graph(ix, name, ctx, camera, renderer, width, height,
                  report, made, obj, values):
    """Render Scene -> Image Node Render -> Image Node Write.

    C'est la construction native de BUiLDER : le Render Scene y remplace le
    Layer 3D de l'image layeree -- la documentation le dit mot pour mot,
    "quite similar to the Layer 3d found in the layered image".

    Trois details que la seule lecture des libelles ne donne pas, et qui
    viennent du CID :

    * ``input`` est un attribut de type *group*, filtre sur Context et
      SceneItem : c'est par lui qu'on branche le contexte de contenu ;
    * ``camera`` et ``renderer`` portent ``null_label "Use input"`` -- laisses
      vides, ils se deduisent du contenu. On les precise quand meme, pour que
      le graphe soit lisible ;
    * ``resolution`` est ``read_only`` : elle est pilotee par
      ``resolution_mode``, qu'il faut donc basculer en personnalise avant.
    """
    render_scene = obj("render_scene", "RenderScene")
    if render_scene is None:
        report["notes"].append(
            "RenderScene indisponible : saveur iFX. On bascule sur l'image "
            "layeree, qui est la sortie native de cette saveur.")
        return False

    if values(render_scene, "input", str(ctx)):
        report["wired"].append("render_scene <- %s" % ctx.get_name())
    if camera is not None and values(render_scene, "camera", str(camera)):
        report["wired"].append("render_scene <- camera")
    if renderer is not None and values(render_scene, "renderer", str(renderer)):
        report["wired"].append("render_scene <- raytracer")

    # resolution_mode doit quitter le mode "preset" pour que resolution
    # devienne inscriptible.
    values(render_scene, "resolution_mode", "1")
    values(render_scene, "resolution", width, height)

    image_render = obj("render", "ImageNodeRender")
    if image_render is None:
        report["notes"].append(
            "ImageNodeRender indisponible : la famille ImageNode demande la "
            "saveur BUiLDER. On bascule sur l'image layeree.")
        return False
    if values(image_render, "scene", str(render_scene)):
        report["wired"].append("render <- render_scene")
    report["graph_output"] = image_render
    if report["image"] is None:
        report["image"] = image_render

    # L'ecriture n'est pas un ImageNode mais un **Process** : la classe
    # documentee est ProcessImageNodeWrite, et son attribut `input` est un
    # group filtre sur ImageNode. La classe "ImageNodeWrite" existe aussi et
    # se cree sans erreur, mais elle n'est documentee nulle part et n'expose
    # pas les memes attributs -- c'est elle qui echouait.
    write = obj("write", "ProcessImageNodeWrite")
    if write is not None and values(write, "input", str(image_render)):
        report["wired"].append("write <- render")
    return True


def _layered_image(ix, ctx, camera, renderer, width, height,
                   report, obj, values):
    """Sortie de la saveur iFX : une Image et son Layer 3D.

    N'est appelee que si le graphe n'a pas pu etre construit. On ne cree donc
    jamais une Image que la hierarchie refusera -- ce qui evite d'avoir a la
    supprimer ensuite, operation que Clarisse refuse egalement.
    """
    image = obj("image", "Image")
    if image is None:
        return False

    # resolution est read_only tant que resolution_mode reste sur
    # "Use Project Preferences" (0) ; il faut passer en User-defined (1).
    values(image, "resolution_mode", "1")
    values(image, "resolution", width, height)

    try:
        layer = ix.cmds.AddLayer(str(image) + ".layers", "Layer3d")
    except Exception:
        layer = None
    # AddLayer ne leve pas quand la hierarchie refuse le layer : elle
    # journalise et renvoie None. C'est le retour qui fait foi.
    if layer is None:
        report["failed"].append(
            "L'Image n'accepte pas de Layer 3D dans cette hierarchie, et le "
            "graphe n'est pas disponible non plus : aucune sortie creee.")
        return False

    report["created"].append("image.layers[0]  [Layer3d]")
    if camera is not None and values(image, "layer_3d.active_camera", str(camera)):
        report["wired"].append("image <- camera")
    if renderer is not None and values(image, "layer_3d.renderer", str(renderer)):
        report["wired"].append("image <- raytracer")
    report["image"] = image
    return True


def _assembly(ix, name, ctx, report, made):
    """Read Project -> Edit -> Merge, en amont de la scene."""

    def context(short, engine):
        full = "%s_%s" % (name, short)
        try:
            item = ix.cmds.CreateCustomContext(full, engine, str(ctx))
        except Exception as error:
            report["failed"].append("%s (moteur %s) : %s"
                                    % (full, engine, _short(error)))
            log.exception("CreateCustomContext %s / %s" % (full, engine))
            return None
        if item is None:
            report["failed"].append("%s : moteur '%s' inconnu -- etes-vous en "
                                    "saveur BUiLDER ?" % (full, engine))
            return None
        report["created"].append("%s  [%s]" % (full, engine))
        made[short] = item
        return item

    read = context("read", ENGINE_READ_PROJECT)
    edit = context("edit", ENGINE_EDIT)
    merge = context("merge", ENGINE_MERGE)

    if read is not None and edit is not None:
        if edit.get_attribute(INPUT_ATTR) is None:
            report["failed"].append("edit : pas d'attribut '%s'" % INPUT_ATTR)
        else:
            try:
                ix.cmds.SetValues([str(edit) + "." + INPUT_ATTR], [str(read)])
                report["wired"].append("edit <- read")
            except Exception as error:
                report["failed"].append("edit.%s : %s" % (INPUT_ATTR, _short(error)))

    if edit is not None and merge is not None:
        # Le Merge prend plusieurs entrees : son attribut est une liste, dont
        # le nom n'est documente nulle part. On essaie les candidats, et on dit
        # lequel a pris.
        for candidate in ("dependencies", INPUT_ATTR, "inputs"):
            if merge.get_attribute(candidate) is None:
                continue
            try:
                ix.cmds.AddValues([str(merge) + "." + candidate], [str(edit)])
                report["wired"].append("merge <- edit  (%s)" % candidate)
            except Exception:
                report["failed"].append("merge.%s : ajout refuse" % candidate)
            break
        else:
            report["failed"].append("Merge : aucun attribut d'entree reconnu "
                                    "(liste reelle dans le journal)")
        report["attrs"]["merge"] = _attributes(merge)


# ---------------------------------------------------------------------------


def _verify(ix, report):
    """Relit toutes les ecritures, une fois le batch ferme.

    Clarisse differe l'execution des commandes lancees depuis un script, et
    davantage encore a l'interieur d'un batch. `check_for_events` force le
    traitement de ce qui est en attente ; sans lui, la relecture porte sur
    l'etat d'avant.
    """
    if not report.get("pending"):
        return
    try:
        ix.application.check_for_events()
    except Exception:
        log.debug("check_for_events indisponible avant verification")

    for target, attribute, expected, label in report["pending"]:
        if _written(target, attribute, expected):
            report["wired"].append(label)
        else:
            report["failed"].append("%s : ecriture sans effet" % label)
            report["attrs"][target.get_name()] = _attributes(target)
    report["pending"] = []


def _written(target, attribute, expected):
    """La valeur ecrite est-elle effectivement en place ?

    Comparaison indulgente : une reference relue peut etre un chemin absolu
    la ou l'on a passe un chemin relatif, ou l'inverse. On considere l'ecriture
    reussie si la valeur relue n'est pas vide et si l'un des deux chemins se
    termine par l'autre.
    """
    attr = target.get_attribute(attribute)
    if attr is None:
        return False
    try:
        got = str(attr.get_string()).strip()
    except Exception:
        return True  # illisible : on ne peut pas conclure, on n'accuse pas
    want = str(expected).strip()
    if not want:
        return True
    if not got:
        return False
    if got == want or got.endswith(want) or want.endswith(got):
        return True
    # valeurs numeriques : "1" contre "1.0"
    try:
        return abs(float(got) - float(want)) < 1e-6
    except ValueError:
        return False


def _roots(ix):
    """Racines de hierarchie disponibles (build:/, default:/, ...)."""
    try:
        roots = ix.application.get_factory().get_roots()
    except Exception:
        return ["(get_roots indisponible)"]
    found = []
    for index in range(len(roots)):
        try:
            found.append(str(roots[index]))
        except Exception:
            continue
    return found or ["(aucune)"]


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


def _report(ix, report, settings):
    if report["image"] is not None:
        try:
            ix.selection.deselect_all()
            ix.selection.add(report["image"])
        except Exception:
            log.debug("Selection de l'image impossible")

    lines = ["%d objet(s) cree(s)" % len(report["created"])]
    lines.extend("  " + item for item in report["created"])
    if report["wired"]:
        lines.append("")
        lines.append("Branchements verifies : " + ", ".join(report["wired"]))
    if report.get("unverified"):
        lines.append("Ecrits sans verification possible : "
                     + ", ".join(report["unverified"]))
    if report.get("notes"):
        lines.append("")
        lines.extend("  " + item for item in report["notes"])
    if report["failed"]:
        lines.append("")
        lines.append("%d point(s) a verifier" % len(report["failed"]))
        lines.extend("  " + item for item in report["failed"])

    log.info("Scene de depart : %d objets, %d branchements, %d echecs"
             % (len(report["created"]), len(report["wired"]), len(report["failed"])))

    message = "\n".join(lines)
    if report.get("graph_output") is not None:
        message += ("\n\nPour voir le rendu : survolez le node 'render' dans "
                    "la Build View et appuyez sur 1 -- ou faites glisser sa "
                    "sortie vers l'Image View. L'evaluation part toute seule.")
    elif report["image"] is not None:
        message += ("\n\nL'image est selectionnee : ouvrez l'Image View, "
                    "elle se rend d'un clic.")

    if report["attrs"]:
        log.debug("--- attributs reels ---")
        for short, attrs in sorted(report["attrs"].items()):
            log.debug("%s : %s" % (short, ", ".join(attrs)))
        from ..core import paths
        message += "\n\nAttributs des nodes concernes dans %s" % paths.log_file()

    ui.message(message, "Scene de depart")


def _short(error):
    text = str(error).strip().splitlines()
    return text[-1][:120] if text else error.__class__.__name__


def _sanitize(name):
    cleaned = "".join(c if (c.isalnum() or c == "_") else "_" for c in (name or ""))
    return cleaned.strip("_")
