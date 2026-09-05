"""Acces au module ``ix`` de Clarisse depuis n'importe quel module de l'addon.

Clarisse n'expose pas ``ix`` comme un module importable : il l'injecte dans les
globales du script lance par le shelf.  Un module importe normalement ne le voit
donc pas.  Le kit de survie contourne ca en passant ``ix=ix`` dans les kwargs de
chaque fonction, ce qui contamine toutes les signatures.

Ici on le stocke une fois pour toutes au demarrage (:func:`set_ix`, appelee par
``clarisse_add.bootstrap``) et chaque module fait simplement ``ix = get_ix()``.

Le module reste importable hors de Clarisse : :func:`get_ix` leve alors une
:class:`ClarisseUnavailable` explicite, ce qui permet de tester les parties
pures (parser, catalogue) avec un pytest ordinaire.
"""

_IX = None


class ClarisseUnavailable(RuntimeError):
    """Le code appelant a besoin de l'API Clarisse, qui n'est pas disponible."""


def set_ix(ix_module):
    """Enregistre le module ``ix`` fourni par Clarisse."""
    global _IX
    _IX = ix_module
    return _IX


def get_ix(ix_local=None):
    """Renvoie le module ``ix``.

    ``ix_local`` permet de passer explicitement l'objet quand on l'a sous la
    main (dans un script de shelf, par exemple) ; il a la priorite et devient
    la reference enregistree.
    """
    if ix_local is not None:
        return set_ix(ix_local)
    if _IX is None:
        raise ClarisseUnavailable(
            "L'API Clarisse n'est pas disponible : ce code doit tourner dans "
            "Clarisse, ou appeler clarisse_add.core.compat.set_ix(ix) d'abord."
        )
    return _IX


def is_available():
    """``True`` si l'API Clarisse a ete enregistree."""
    return _IX is not None
