# -*- coding: utf-8 -*-
"""Charge les modules C++ de ClarisseAdd dans une Clarisse deja lancee.

Clarisse ne decouvre ses modules qu'au demarrage, en balayant les dossiers
donnes par l'argument de ligne de commande ``-module_path``. Il n'existe
aucune variable d'environnement equivalente : la liste exhaustive de celles
que Clarisse reconnait est dans ``app_env.h``, et aucune ne concerne les
modules. Suivre la voie officielle obligerait donc a modifier le raccourci de
lancement -- et a relister le dossier ``module`` d'origine, puisque
``-module_path`` remplace le defaut au lieu de s'y ajouter.

``AppObject::scan_modules`` offre une sortie : elle balaye des dossiers
supplementaires a chaud. Elle est exposee en Python, et elle fonctionne --
verifie, la classe apparait bien et son ``get_dso_filename`` pointe sur notre
bibliotheque.

Deux details qui coutent du temps si on les ignore :

- ``scan_modules`` veut un ``CoreVector<CoreString>``, pas une liste Python.
  Le binding SWIG ne convertit rien et l'erreur ne dit pas quel type fournir.
  Le type se nomme ``CoreStringVector``.
- Balayer deux fois le meme dossier redeclare les memes classes. On saute donc
  les dossiers dont toutes les classes sont deja la.
"""
from __future__ import absolute_import

import os

from .core.compat import get_ix

# Ou vivent les bibliotheques construites par native/build.py.
BUILD_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "native", "build")


def available(folder=None):
    """Les bibliotheques presentes dans le dossier de construction."""
    folder = folder or BUILD_DIR
    if not os.path.isdir(folder):
        return []
    return sorted(name for name in os.listdir(folder) if name.endswith(".dll"))


def load(folder=None):
    """Balaye le dossier et rend (charges, deja_presents).

    Rend ``(-1, 0)`` si le dossier n'existe pas ou si Clarisse ne propose pas
    ``scan_modules`` -- plutot que de lever, pour qu'un demarrage reste sans
    consequence sur une machine ou rien n'est compile.
    """
    ix = get_ix()
    folder = folder or BUILD_DIR
    if not os.path.isdir(folder):
        return (-1, 0)

    app = ix.application
    if not hasattr(app, "scan_modules"):
        return (-1, 0)

    classes = app.get_factory().get_classes()
    before = _declared(classes)

    paths = ix.api.CoreStringVector()
    paths.add(ix.api.CoreString(folder))
    app.scan_modules(paths)

    after = _declared(classes)
    return (len(after - before), len(before & after))


def _declared(classes):
    """Les noms de classes actuellement declarees."""
    names = set()
    listing = classes.get_classes("")
    for index in range(len(listing)):
        names.add(listing[index].get_name())
    return names
