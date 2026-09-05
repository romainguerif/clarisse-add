"""Declare les classes C++ de ClarisseAdd dans la session en cours.

Clarisse ne balaye ses modules qu'au demarrage, et seulement les dossiers
donnes par l'argument de ligne de commande ``-module_path``. Aucune variable
d'environnement ne permet d'en ajouter : la liste exhaustive de celles que
Clarisse reconnait est dans ``app_env.h``, et aucune ne concerne les modules.

Sans ce bouton, se servir d'un module compile ici imposerait donc de relancer
Clarisse avec une ligne de commande particuliere -- en n'oubliant pas de
relister le dossier ``module`` d'origine, que ``-module_path`` remplace au
lieu de completer.

``AppObject::scan_modules`` accepte des dossiers supplementaires a chaud. Elle
est appelable a tout moment, et rappeler ce bouton ne redeclare rien.
"""

from .. import native_modules
from ..core import log, ui
from ..core.compat import get_ix


def run(payload=None):
    get_ix()  # leve ClarisseUnavailable si l'addon n'est pas initialise

    libraries = native_modules.available()
    if not libraries:
        ui.message(
            "Le dossier native/build est vide.\n\n"
            "Les modules se construisent depuis un terminal :\n"
            "    python native/build.py bokeh\n\n"
            "Il faut Visual Studio et le SDK reconstruit dans J:\\Clarisse-SDK.",
            "Aucun module compile")
        return

    loaded, already = native_modules.load()

    if loaded < 0:
        ui.message(
            "Cette version de Clarisse n'expose pas scan_modules.\n\n"
            "Relancez alors avec :\n"
            "    clarisse.exe -module_path \"<install>\\Clarisse\\module\" "
            "\"<addon>\\native\\build\"",
            "Chargement impossible")
        return

    log.info("modules natifs : %d classe(s) declaree(s) depuis %s"
             % (loaded, ", ".join(libraries)))

    if loaded == 0:
        ui.message(
            "Les %d bibliotheque(s) de native/build sont deja declarees.\n\n"
            "%s" % (len(libraries), ", ".join(libraries)),
            "Deja charges")
        return

    ui.message(
        "%d nouvelle(s) classe(s) declaree(s) depuis :\n    %s\n\n"
        "Le filtre Bokeh apparait ensuite dans la liste des filtres d'un "
        "layer, groupe Filters de l'Attribute Editor."
        % (loaded, "\n    ".join(libraries)),
        "Modules charges")
