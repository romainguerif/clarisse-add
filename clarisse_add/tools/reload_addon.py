"""Recharge l'addon sans redemarrer Clarisse.

Clarisse garde un interpreteur Python vivant pour toute la session : un module
importe une fois le reste, meme si son fichier change sur disque.  Sans ce
bouton, chaque modification d'un outil impose de relancer l'application, ce qui
prend plusieurs minutes sur une grosse scene.

Le rechargement regenere aussi les stubs du shelf et reenregistre les categories
a chaud : un outil ajoute au manifeste apparait donc immediatement.
"""

from ..core import log, shelf, ui
from ..core.compat import get_ix


def run(payload=None):
    ix = get_ix()
    from .. import bootstrap, manifest
    from ..presets import catalog

    catalog.reload()
    manifest.invalidate()
    tools = manifest.all_tools()

    written = shelf.write_entry_scripts(tools)
    pruned = shelf.prune_entry_scripts(tools)
    registered, already = shelf.register_runtime(ix, tools)
    forgotten = bootstrap.reload_addon()

    lines = [
        "%d modules recharges" % forgotten,
        "%d outils au manifeste" % len(tools),
        "%d stubs reecrits, %d obsoletes supprimes" % (written, pruned),
    ]
    if registered < 0:
        lines.append(
            "Enregistrement a chaud indisponible sur cette version : "
            "relancez install.py pour voir les nouveaux boutons."
        )
    elif registered:
        lines.append("%d nouveau(x) bouton(s) ajoute(s) au shelf, %d deja presents"
                     % (registered, already))
    else:
        lines.append("Shelf inchange : les %d boutons sont deja presents" % already)

    message = "\n".join(lines)
    log.info(message.replace("\n", " | "))
    ui.message(message, "ClarisseAdd rechargee")
    return True
