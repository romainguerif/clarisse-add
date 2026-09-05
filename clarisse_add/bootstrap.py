"""Point d'entree unique de l'addon, appele par les stubs du shelf.

Un bouton du shelf execute un fichier de ``clarisse_add/entry/`` qui se resume a
``bootstrap.launch("scatter.distribute", ix)``.  Tout ce qui suit se passe ici :

* enregistrer le module ``ix`` fourni par Clarisse ;
* rendre le paquet vendorise importable (le Survival Kit s'importe en
  ``clarisse_survival_kit.*`` en absolu) ;
* importer le module de l'outil et appeler son ``run()`` ;
* attraper toute exception, la journaliser avec sa pile et la montrer a
  l'artiste plutot que de laisser le bouton ne rien faire.
"""

import importlib
import os
import sys

from .core import log, paths

#: Contrat des modules d'outils : ils exposent ``run(payload=None)``.
ENTRY_POINT = "run"

_vendor_ready = False


def ensure_paths():
    """Rend importables le paquet de l'addon et ses dependances vendorisees."""
    global _vendor_ready
    if paths.ADDON_ROOT not in sys.path:
        sys.path.insert(0, paths.ADDON_ROOT)
    if not _vendor_ready:
        # Le Survival Kit s'importe en absolu (``from clarisse_survival_kit.x
        # import *``).  On place donc notre copie en tete de sys.path : elle
        # prend le pas sur une eventuelle installation site-packages, y compris
        # cassee, ce qui evite de dependre de l'etat du Python de la machine.
        if paths.VENDOR_DIR not in sys.path:
            sys.path.insert(0, paths.VENDOR_DIR)
        _vendor_ready = True


def launch(tool_id, ix_module, payload=None):
    """Execute l'outil ``tool_id``. Ne leve jamais : journalise et signale."""
    # Import local, volontairement : un `from .core.compat import set_ix` en
    # tete de fichier figerait la reference sur l'exemplaire de `compat` charge
    # au premier clic.  Apres un rechargement, `launch` ecrirait alors `_IX`
    # dans cette copie-la pendant que l'outil, importe a neuf, en lirait une
    # autre -- et tous les outils tomberaient sur ClarisseUnavailable.
    from .core.compat import set_ix

    set_ix(ix_module)
    ensure_paths()

    from . import manifest

    tool = manifest.by_id(tool_id)
    if tool is None:
        log.error("Outil inconnu : %s. Relancez l'installation du shelf." % tool_id)
        return None

    if payload is None:
        payload = tool.payload

    try:
        module = importlib.import_module(tool.module)
    except Exception:
        log.exception("Import du module %s (outil '%s')" % (tool.module, tool.title))
        return None

    entry = getattr(module, ENTRY_POINT, None)
    if entry is None:
        log.error("%s n'expose pas de fonction %s()" % (tool.module, ENTRY_POINT))
        return None

    log.debug("Lancement de %s (payload=%r)" % (tool_id, payload))
    try:
        return entry(payload)
    except Exception:
        log.exception("Execution de l'outil '%s'" % tool.title)
        return None


def run_script_file(filename, ix_module, extra_globals=None):
    """Execute un script Clarisse ordinaire, avec ``ix`` dans ses globales.

    Les scripts du Survival Kit appellent leur fonction GUI a la derniere ligne
    du fichier : les importer ne les lancerait qu'une fois, Python gardant le
    module en cache.  On les execute donc comme Clarisse le ferait lui-meme, ce
    qui les rend rejouables et evite d'avoir a les modifier.
    """
    from .core.compat import set_ix  # voir launch() : resolution paresseuse

    set_ix(ix_module)
    ensure_paths()

    if not os.path.isfile(filename):
        log.error("Script introuvable : %s" % filename)
        return False

    namespace = {
        "__name__": "__main__",
        "__file__": filename,
        "ix": ix_module,
    }
    if extra_globals:
        namespace.update(extra_globals)

    directory = os.path.dirname(filename)
    inserted = False
    if directory and directory not in sys.path:
        sys.path.insert(0, directory)
        inserted = True
    try:
        with open(filename, "rb") as handle:
            source = handle.read()
        code = compile(source, filename, "exec")
        exec(code, namespace)  # noqa: S102 - script d'outil, fourni par l'addon
        return True
    except Exception:
        log.exception("Execution de %s" % os.path.basename(filename))
        return False
    finally:
        if inserted:
            try:
                sys.path.remove(directory)
            except ValueError:
                pass


def reload_addon():
    """Purge les modules de l'addon de ``sys.modules``.

    Clarisse garde l'interpreteur Python vivant pour toute la session : sans
    ca, modifier un outil oblige a redemarrer l'application.  On ne touche pas
    au paquet vendorise, qui n'a pas de raison de changer en cours de session.

    Renvoie le nombre de modules oublies.
    """
    prefix = "clarisse_add"
    doomed = [name for name in sys.modules
              if name == prefix or name.startswith(prefix + ".")]
    # `bootstrap` est purge comme les autres, y compris pendant qu'on y tourne.
    # Le retirer de sys.modules ne detruit pas le module : la pile d'appel en
    # garde une reference, la fonction va donc jusqu'au bout normalement, et le
    # prochain clic en importe un exemplaire neuf depuis le stub.
    #
    # L'epargner serait pire : il conserverait des references vers les modules
    # `core` d'avant la purge, et l'addon se retrouverait avec deux exemplaires
    # de `core.compat` -- celui ou `launch` ecrit `_IX`, et celui, vierge, que
    # lisent les outils fraichement importes.
    for name in doomed:
        del sys.modules[name]
    return len(doomed)
