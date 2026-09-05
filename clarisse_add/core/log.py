"""Journalisation de l'addon.

Deux destinations, toujours les deux a la fois :

* la console de Clarisse (``ix.log_info`` / ``log_warning`` / ``log_error``),
  qui est ce que l'artiste voit ;
* un fichier a cote de la configuration Clarisse, qui survit au crash et permet
  de comprendre apres coup pourquoi un bouton n'a rien fait.

Aucun outil ne doit echouer en silence : c'est la regle qui manquait le plus au
kit de survie, ou une exception dans un callback GUI disparait sans trace.
"""

import logging
import os
import traceback

from . import paths
from .compat import is_available, get_ix

_LOGGER_NAME = "clarisse_add"
_logger = None


def _build_logger():
    logger = logging.getLogger(_LOGGER_NAME)
    if logger.handlers:
        return logger
    logger.setLevel(logging.DEBUG)
    logger.propagate = False
    try:
        target = paths.log_file()
        directory = os.path.dirname(target)
        if directory and not os.path.isdir(directory):
            os.makedirs(directory)
        handler = logging.FileHandler(target, encoding="utf-8")
        handler.setFormatter(
            logging.Formatter("%(asctime)s %(levelname)-7s %(message)s", "%Y-%m-%d %H:%M:%S")
        )
        logger.addHandler(handler)
    except (OSError, IOError):
        # Un disque plein ou un dossier en lecture seule ne doit pas empecher
        # l'addon de tourner : on se rabat sur la seule console Clarisse.
        logger.addHandler(logging.NullHandler())
    return logger


def logger():
    global _logger
    if _logger is None:
        _logger = _build_logger()
    return _logger


def _to_clarisse(level, message):
    if not is_available():
        return
    try:
        ix = get_ix()
    except Exception:
        return
    try:
        if level == "error":
            ix.log_warning("[ClarisseAdd] " + message)
        elif level == "warning":
            ix.log_warning("[ClarisseAdd] " + message)
        else:
            ix.log_info("[ClarisseAdd] " + message)
    except Exception:
        # ix.log_error interrompt le script dans Clarisse ; on ne s'en sert
        # jamais, et on avale toute defaillance du logger lui-meme.
        pass


def debug(message):
    logger().debug(message)


def info(message):
    logger().info(message)
    _to_clarisse("info", message)


def warning(message):
    logger().warning(message)
    _to_clarisse("warning", message)


def error(message, exc_info=False):
    logger().error(message, exc_info=exc_info)
    _to_clarisse("error", message)


def exception(context):
    """Journalise l'exception courante, avec sa pile, sans la relancer.

    A appeler dans un ``except`` autour d'un callback GUI : Clarisse avale les
    exceptions des callbacks, et sans ca l'outil semble simplement ne rien
    faire.
    """
    trace = traceback.format_exc()
    logger().error("%s\n%s", context, trace)
    first_line = trace.strip().splitlines()[-1] if trace.strip() else "erreur inconnue"
    _to_clarisse("error", "%s : %s (details dans %s)" % (context, first_line, paths.log_file()))
