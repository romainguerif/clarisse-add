"""Les outils optiques de ClarisseAdd, reunis.

Le probleme que ces boutons resolvent n'est pas technique : c'est qu'une fois
poses dans les listes de Clarisse, nos noeuds ne se distinguent plus des
siens. Un filtre nomme "Bokeh" au milieu de "Defocus Blur" et "Gaussian Blur"
laisse un doute sur ce qu'on est en train de regler.

D'ou deux mesures. Les classes portent un `ui_name` explicite --
"Bokeh [ClarisseAdd]", "Camera Bokeh [ClarisseAdd]" -- et ces boutons les
posent depuis un seul endroit, avec des reglages de depart qui montrent
l'effet au lieu de le laisser a zero.
"""

from .. import native_modules
from ..core import log, ui
from ..core.compat import get_ix

FILTER_CLASS = "ImageFilterBokeh"
CAMERA_CLASS = "CameraBokeh"

# Des valeurs qui montrent quelque chose. A zero partout, un utilisateur pose
# le noeud, ne voit aucune difference, et conclut qu'il ne marche pas.
FILTER_DEFAULTS = (("radius", "18.0"), ("blades", "6"),
                   ("chromatic_aberration", "0.35"))
CAMERA_DEFAULTS = (("enable_dof", "1"), ("f_stop", "1.4"),
                   ("focus_distance", "10.0"), ("blades", "6"))


def _ensure_loaded(ix):
    """Declare les modules natifs s'ils ne le sont pas encore.

    Rend True si la classe demandee est disponible ensuite.
    """
    classes = ix.application.get_factory().get_classes()
    if classes.exists(FILTER_CLASS) and classes.exists(CAMERA_CLASS):
        return True
    native_modules.load()
    return bool(classes.exists(FILTER_CLASS))


def _missing_module_message():
    ui.message(
        "Les classes natives ne sont pas declarees.\n\n"
        "Elles se chargent au demarrage de Clarisse, par le script pose dans "
        "clarisse.env. Si vous venez de recompiler un module, il faut "
        "relancer Clarisse : une classe deja declaree n'est pas remplacee "
        "a chaud.\n\n"
        "Sinon, relancez l'installeur :\n"
        "    python install.py",
        "Modules natifs absents")


def add_filter(payload=None):
    """Pose le filtre Bokeh sur le ou les layers selectionnes."""
    ix = get_ix()
    if not _ensure_loaded(ix):
        _missing_module_message()
        return

    layers = [item for item in ix.selection
              if item is not None and item.is_kindof("Layer")]
    if not layers:
        ui.message(
            "Selectionnez d'abord un ou plusieurs layers.\n\n"
            "Un filtre d'image vit dans un layer, pas dans une image : il faut "
            "donc selectionner le layer lui-meme dans l'Explorer, pas l'image "
            "qui le contient.",
            "Aucun layer selectionne")
        return

    posed = []
    for layer in layers:
        module = layer.get_module()
        if module is None or not hasattr(module, "add_filter"):
            continue
        added = module.add_filter(FILTER_CLASS, "bokeh")
        if added is None:
            continue
        obj = added.get_object()
        for name, value in FILTER_DEFAULTS:
            if obj.get_attribute(name) is not None:
                ix.cmds.SetValues([str(obj) + "." + name], [value])
        posed.append(str(obj))

    log.info("bokeh : %d filtre(s) pose(s)" % len(posed))
    if not posed:
        ui.message("Aucun des layers selectionnes n'a accepte le filtre.",
                   "Rien pose")
        return
    ui.message(
        "%d filtre(s) pose(s) :\n    %s\n\n"
        "Reglages de depart : rayon 18, 6 lames, aberration chromatique 35 %%.\n"
        "Le flou ne se voit que sur des valeurs superieures a 1 en lineaire -- "
        "un rendu HDR, des speculaires, des lumieres."
        % (len(posed), "\n    ".join(posed)),
        "Bokeh pose")


def create_camera(payload=None):
    """Cree une camera Bokeh, mise au point active."""
    ix = get_ix()
    if not _ensure_loaded(ix):
        _missing_module_message()
        return

    camera = ix.cmds.CreateObject("bokeh_camera", CAMERA_CLASS, "Global",
                                  str(ix.application.get_current_context()))
    if camera is None:
        ui.message("La creation a echoue.", "Camera Bokeh")
        return

    for name, value in CAMERA_DEFAULTS:
        if camera.get_attribute(name) is not None:
            ix.cmds.SetValues([str(camera) + "." + name], [value])

    log.info("camera bokeh creee : %s" % camera)
    ui.message(
        "%s\n\n"
        "La profondeur de champ est calculee par le moteur : il echantillonne "
        "l'ouverture, l'occlusion est donc juste et aucune carte de profondeur "
        "n'est necessaire.\n\n"
        "Deux choses a savoir. Reglez Focus Distance sur ce qui doit etre net. "
        "Et montez l'echantillonnage du renderer -- Anti Aliasing Sample Count "
        "vaut 9 par defaut, ce qui donne neuf points de lentille par pixel et "
        "un bokeh crible de bruit." % camera,
        "Camera Bokeh creee")


def run(payload=None):
    """Point d'entree commun : le payload choisit l'outil.

    Deux boutons pour un module : leurs deux actions partagent le chargement
    des classes natives et les memes messages d'erreur, et les separer
    dupliquerait tout cela pour rien.
    """
    if payload == "camera":
        create_camera()
    else:
        add_filter()
