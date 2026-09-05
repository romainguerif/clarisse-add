"""Suppression des contextes vides et des objets que rien ne reference.

Deux nettoyages distincts, volontairement separes :

* **contextes vides** : sans risque, on les supprime de bas en haut pour qu'un
  parent devenu vide en cours de route parte lui aussi ;
* **objets orphelins** : textures et materiaux qu'aucun attribut ne reference.
  C'est plus delicat -- un objet peut n'etre reference par rien et servir quand
  meme (un materiau qu'on s'apprete a assigner). L'option est donc decochee par
  defaut, limitee aux textures et materiaux, et toujours confirmee avec la liste
  sous les yeux.

Rien n'est supprime sans validation explicite.
"""

from ..core import log, scene, ui
from ..core.compat import get_ix

#: Classes considerees comme sures a supprimer quand rien ne les reference.
ORPHAN_KINDS = ("Texture", "Material")


def run(payload=None):
    ix = get_ix()

    settings = ui.Form(
        "Cleanup",
        [
            ui.Section("Portee"),
            ui.Toggle("selection_only", "Limiter au contexte selectionne", default=False),
            ui.Section("Nettoyages"),
            ui.Toggle("empty_contexts", "Supprimer les contextes vides", default=True),
            ui.Toggle("orphans", "Supprimer les textures et materiaux orphelins",
                      default=False,
                      tooltip="Un objet qu'aucun attribut ne reference. "
                              "Verifiez la liste avant de valider."),
        ],
        accept_label="Analyser",
    ).run()
    if settings is None:
        return False

    if settings["selection_only"]:
        root = scene.context_from_selection()
    else:
        root = ix.application.get_factory().get_root()
    if root is None:
        return False

    empty = _empty_contexts(root) if settings["empty_contexts"] else []
    orphans = _orphans(root) if settings["orphans"] else []

    if not empty and not orphans:
        ui.message("Rien a nettoyer dans %s." % str(root), "Cleanup")
        return True

    lines = []
    if empty:
        lines.append("%d contexte(s) vide(s) :" % len(empty))
        lines.extend("  " + str(item) for item in empty[:6])
        if len(empty) > 6:
            lines.append("  (... et %d autres)" % (len(empty) - 6))
    if orphans:
        lines.append("%d objet(s) orphelin(s) :" % len(orphans))
        lines.extend("  " + str(item) for item in orphans[:6])
        if len(orphans) > 6:
            lines.append("  (... et %d autres)" % (len(orphans) - 6))

    if not ui.confirm("\n".join(lines) + "\n\nSupprimer ?", "Cleanup"):
        return False

    removed = 0
    with scene.command_batch("ClarisseAdd - Cleanup"):
        if orphans:
            removed += _delete(ix, orphans)
        if empty:
            # Du plus profond au plus superficiel : sinon supprimer un parent
            # invalide les references vers ses enfants encore dans la liste.
            ordered = sorted(empty, key=lambda item: str(item).count("/"), reverse=True)
            removed += _delete(ix, ordered)

    log.info("Cleanup : %d element(s) supprime(s)" % removed)
    ui.message("%d element(s) supprime(s)." % removed, "Cleanup")
    return True


def _empty_contexts(root):
    """Contextes sans objet ni sous-contexte, y compris ceux qui le deviennent.

    On itere jusqu'a stabilite : vider un sous-contexte peut rendre son parent
    vide a son tour, et l'artiste attend que la branche entiere disparaisse.
    """
    doomed = []
    known = set()
    while True:
        found = []
        for context in scene.sub_contexts(root):
            path = str(context)
            if path in known:
                continue
            objects = context.get_object_count()
            children = [child for child in scene.sub_contexts(context, max_depth=1)
                        if str(child) not in known]
            if objects == 0 and not children:
                found.append(context)
                known.add(path)
        if not found:
            break
        doomed.extend(found)
    return doomed


def _orphans(root):
    """Textures et materiaux qu'aucun attribut ne reference."""
    orphans = []
    for obj in scene.iter_objects(root, kinds=ORPHAN_KINDS):
        try:
            if obj.get_dependency_count() == 0:
                orphans.append(obj)
        except Exception:
            log.debug("Dependances indisponibles pour %s" % str(obj))
    return orphans


def _delete(ix, items):
    if not items:
        return 0
    paths = [str(item) for item in items]
    try:
        ix.cmds.DeleteItems(paths)
        return len(paths)
    except Exception:
        log.exception("Suppression de %d element(s)" % len(paths))
        return 0
