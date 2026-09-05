# -*- coding: utf-8 -*-
"""Ce que Clarisse fait au demarrage pour le compte de l'addon.

Declare les modules C++ -- le filtre Bokeh, entre autres -- pour qu'ils soient
la des l'ouverture, sans bouton a cliquer ni ligne de commande a retenir.

Ce code tourne avant tout le reste, dans une application qui n'est pas encore
prete. Il ne doit donc jamais lever : une exception ici, et c'est le demarrage
de Clarisse qui trebuche. D'ou le try/except large, qui va contre l'usage mais
se justifie -- un module natif absent ne vaut pas de casser la session.

En revanche il ne doit pas echouer en silence. Un ``print`` au demarrage
n'arrive nulle part de lisible : la console de Clarisse n'existe pas encore.
Tout part donc dans le journal de l'addon.

Ce module n'est pas appele directement par Clarisse : c'est le lanceur genere
a l'installation qui l'importe, parce que Clarisse execute ses scripts de
demarrage par ``PyRun_String`` et que ``__file__`` n'y est pas defini.
"""
from __future__ import absolute_import

import os


def record(message):
    """Journalise, par le journal de l'addon s'il repond, par /tmp sinon."""
    try:
        from .core import log
        log.info(u"demarrage : %s" % message)
        return
    except Exception:
        pass
    try:
        import io
        import tempfile
        fallback = os.path.join(tempfile.gettempdir(), "clarisse_add_startup.log")
        with io.open(fallback, "a", encoding="utf-8") as handle:
            handle.write(u"%s\n" % message)
    except Exception:
        pass


def run(ix):
    """Point d'entree appele par le lanceur genere."""
    import traceback
    try:
        from .core.compat import set_ix
        from . import native_modules

        set_ix(ix)

        libraries = native_modules.available()
        if not libraries:
            record(u"aucun module compile dans %s" % native_modules.BUILD_DIR)
            return 0

        loaded, already = native_modules.load()
        if loaded < 0:
            record(u"scan_modules indisponible dans cette version")
            return 0

        record(u"%d classe(s) native(s) declaree(s) depuis %s"
               % (loaded, ", ".join(libraries)))
        return loaded
    except Exception as error:      # noqa: BLE001 -- voir la note en tete
        record(u"abandon : %s\n%s" % (error, traceback.format_exc()))
        return 0
