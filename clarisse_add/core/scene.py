"""Operations de scene mutualisees par les outils.

Toutes les fonctions prennent ``ix`` implicitement via
:func:`clarisse_add.core.compat.get_ix`, et refusent d'ecrire dans un contexte
verrouille plutot que de laisser Clarisse echouer a moitie.
"""

import contextlib
import os

from . import log
from .compat import get_ix

__all__ = [
    "current_context",
    "context_from_selection",
    "ensure_context",
    "unique_name",
    "is_writable",
    "selection",
    "selected_of_kind",
    "iter_objects",
    "sub_contexts",
    "command_batch",
    "merge_project",
    "import_project",
    "set_attribute",
    "get_attribute",
]


# ---------------------------------------------------------------------------
# Contextes
# ---------------------------------------------------------------------------


def current_context():
    """Le contexte courant de l'application."""
    ix = get_ix()
    return ix.application.get_current_context()


def context_from_selection(fallback_to_current=True):
    """Le contexte de l'element selectionne, sinon le contexte courant.

    C'est le comportement attendu par un artiste : selectionner un objet et
    lancer un outil doit construire a cote de cet objet, pas a la racine.
    """
    ix = get_ix()
    sel = ix.selection
    if sel.get_count():
        item = sel[0]
        if item.is_context():
            return item
        parent = item.get_context()
        if parent:
            return parent
    if fallback_to_current:
        return current_context()
    return None


def is_writable(ctx, quiet=False):
    """``True`` si on peut ecrire dans ce contexte."""
    ix = get_ix()
    if ctx is None:
        return False
    if (not ctx.is_editable()) or ctx.is_content_locked() or ctx.is_remote():
        if not quiet:
            log.warning("Contexte verrouille, ecriture impossible : %s" % str(ctx))
        return False
    del ix
    return True


def ensure_context(name, parent=None, unique=True):
    """Cree (ou retrouve) un sous-contexte et le renvoie.

    Avec ``unique``, un contexte homonyme existant n'est pas reutilise : on
    cree ``name1``, ``name2``, etc.  C'est ce qu'on veut pour un outil qu'on
    relance plusieurs fois sur la meme scene.
    """
    ix = get_ix()
    parent = parent or current_context()
    if not is_writable(parent):
        return None
    if unique:
        name = unique_name(name, parent)
    else:
        existing = ix.item_exists(str(parent) + "/" + name)
        if existing:
            return existing
    return ix.cmds.CreateContext(name, "Global", str(parent))


def unique_name(base, ctx):
    """``base`` s'il est libre dans ``ctx``, sinon ``base1``, ``base2``, ..."""
    ix = get_ix()
    prefix = str(ctx).rstrip("/") + "/"
    if not ix.item_exists(prefix + base):
        return base
    index = 1
    while ix.item_exists(prefix + base + str(index)):
        index += 1
    return base + str(index)


def sub_contexts(ctx, max_depth=0, _depth=0):
    """Tous les sous-contextes, recursivement (``max_depth=0`` = illimite)."""
    found = []
    _depth += 1
    for index in range(ctx.get_context_count()):
        child = ctx.get_context(index)
        found.append(child)
        if max_depth == 0 or _depth < max_depth:
            found.extend(sub_contexts(child, max_depth=max_depth, _depth=_depth))
    return found


def iter_objects(ctx, kinds=(), max_depth=0):
    """Itere sur les objets de ``ctx``, filtres par classe.

    ``kinds`` accepte des noms de classe Clarisse ; le test utilise
    ``is_kindof``, donc ``("MaterialPhysical",)`` attrape toutes les variantes.
    """
    ix = get_ix()
    contexts = [ctx]
    if max_depth == 0 or max_depth > 1:
        contexts.extend(sub_contexts(ctx, max_depth=max_depth))
    for context in contexts:
        count = context.get_object_count()
        if not count:
            continue
        objects = ix.api.OfObjectArray(count)
        context.get_all_objects(objects, ix.api.CoreBitFieldHelper(), False)
        for index in range(count):
            obj = objects[index]
            if not kinds:
                yield obj
                continue
            for kind in kinds:
                if obj.is_kindof(kind):
                    yield obj
                    break


# ---------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------


def selection():
    """La selection courante, sous forme de liste Python."""
    ix = get_ix()
    sel = ix.selection
    return [sel[index] for index in range(sel.get_count())]


def selected_of_kind(*kinds):
    """Les elements selectionnes dont la classe derive d'un des ``kinds``."""
    return [item for item in selection()
            if any(item.is_kindof(kind) for kind in kinds)]


# ---------------------------------------------------------------------------
# Commandes
# ---------------------------------------------------------------------------


@contextlib.contextmanager
def command_batch(label):
    """Regroupe toutes les commandes du bloc en un seul undo.

    Sans ca, un outil qui cree quarante noeuds laisse quarante entrees dans
    l'historique, et l'artiste doit faire quarante Ctrl+Z.  Le batch est ferme
    meme si le bloc leve, sinon l'historique de Clarisse reste ouvert et le
    reste de la session devient imprevisible.
    """
    ix = get_ix()
    ix.begin_command_batch(label)
    try:
        yield
    finally:
        ix.end_command_batch()


def set_attribute(obj, name, value):
    """Ecrit un attribut via une commande (donc annulable), sans planter.

    Renvoie ``True`` si l'attribut existait et a ete ecrit.
    """
    ix = get_ix()
    attribute = obj.get_attribute(name) if hasattr(obj, "get_attribute") else None
    if attribute is None:
        log.debug("Attribut absent, ignore : %s.%s" % (str(obj), name))
        return False
    target = str(obj) + "." + name
    values = value if isinstance(value, (list, tuple)) else [value]
    ix.cmds.SetValues([target], [str(item) for item in values])
    return True


def get_attribute(obj, name, default=None):
    """Lit un attribut sans supposer qu'il existe."""
    if not hasattr(obj, "get_attribute"):
        return default
    attribute = obj.get_attribute(name)
    if attribute is None:
        return default
    return attribute


# ---------------------------------------------------------------------------
# Import de projets
# ---------------------------------------------------------------------------


def merge_project(filename, target_context=None):
    """Fusionne un ``.project`` dans un contexte, et renvoie ce contexte.

    ``ix.cmds.MergeProject`` importe dans le contexte *courant* de
    l'application ; on le deplace donc temporairement, puis on le restaure —
    y compris si l'import echoue, sans quoi l'artiste se retrouve a travailler
    dans un contexte qu'il n'a pas choisi.
    """
    ix = get_ix()
    if not os.path.isfile(filename):
        log.error("Fichier introuvable : %s" % filename)
        return None

    target = target_context or current_context()
    if not is_writable(target):
        return None

    previous = ix.application.get_current_context()
    try:
        ix.application.set_current_context(target)
        ix.cmds.MergeProject([filename])
    except Exception:
        log.exception("Echec du merge de %s" % os.path.basename(filename))
        return None
    finally:
        try:
            ix.application.set_current_context(previous)
        except Exception:
            log.exception("Impossible de restaurer le contexte courant")
    log.info("Merge : %s -> %s" % (os.path.basename(filename), str(target)))
    return target


def import_project(filename, target_context=None, as_root=False):
    """Importe un ``.project`` comme sous-contexte du contexte cible."""
    ix = get_ix()
    if not os.path.isfile(filename):
        log.error("Fichier introuvable : %s" % filename)
        return None

    target = target_context or current_context()
    if not is_writable(target):
        return None

    previous = ix.application.get_current_context()
    try:
        ix.application.set_current_context(target)
        ix.cmds.ImportProject([filename], as_root)
    except Exception:
        log.exception("Echec de l'import de %s" % os.path.basename(filename))
        return None
    finally:
        try:
            ix.application.set_current_context(previous)
        except Exception:
            log.exception("Impossible de restaurer le contexte courant")
    log.info("Import : %s -> %s" % (os.path.basename(filename), str(target)))
    return target
