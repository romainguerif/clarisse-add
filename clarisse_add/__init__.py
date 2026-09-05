"""ClarisseAdd - boite a outils unifiee pour Isotropix Clarisse iFX.

Regroupe dans un seul shelf des outils jusqu'ici eparpilles : le Clarisse
Survival Kit, une collection de scripts communautaires, et une bibliotheque de
scenes ``.project`` parametrables.

Point d'entree : :mod:`clarisse_add.bootstrap`, appele par les stubs generes
dans ``clarisse_add/entry/``.  Le catalogue des outils est dans
:mod:`clarisse_add.manifest`.
"""

__version__ = "0.1.0"
__author__ = "Romain Guerif"
__license__ = "GPL-3.0-or-later"

#: Version de Clarisse contre laquelle l'addon est developpe et teste.
TARGET_CLARISSE = "5.0 SP14"
