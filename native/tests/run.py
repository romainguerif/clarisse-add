# -*- coding: utf-8 -*-
"""Verifie qu'un module compile se charge vraiment dans Clarisse.

    python tests/run.py hello

Passe par cnode -- le moteur sans interface -- plutot que par l'application
graphique : le test tourne sans ouvrir de fenetre, en quelques secondes, et sa
sortie se lit dans un terminal. `-module_path` remplace le chemin par defaut,
d'ou la reprise du dossier `module/` d'origine en premier.

Sort 0 si tout est vert, 1 sinon. Aucune sortie inutile : ce test doit pouvoir
tourner en boucle.
"""
from __future__ import print_function

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)
CLARISSE = r"C:\Program Files\Isotropix\Clarisse 5.0 SP14\Clarisse"

# Ce que la sortie du script doit contenir pour que le module soit declare bon.
EXPECTED = [
    ("classe .* declaree : OUI", "la classe est enregistree"),
    ("declaree par .*%s", "elle vient bien de notre .dll"),
    ("Locator natif cree .*: OUI", "temoin de controle"),
    ("instanciation .*: OUI", "un objet se cree"),
    (r"module C\+\+ attache .*: OUI", "son module C++ est en place"),
]

# Le bruit de demarrage de Clarisse : configuration couleur, licence, memoire.
# Rien de tout cela ne concerne le test.
NOISE = re.compile(r"OpenColorIO|ColorSpace|color space|OCIO|BuiltinTransform"
                   r"|at your own risk|WARNING")


def main(module):
    dll = os.path.join(NATIVE, "build", module + ".dll")
    if not os.path.isfile(dll):
        sys.exit("%s n'existe pas -- lancer d'abord : python build.py %s"
                 % (dll, module))

    command = [os.path.join(CLARISSE, "cnode.exe"),
               os.path.join(HERE, "empty.project"),
               "-module_path", os.path.join(CLARISSE, "module"),
               os.path.join(NATIVE, "build"),
               "-script", os.path.join(HERE, "smoke.py")]

    proc = subprocess.Popen(command, cwd=CLARISSE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    raw = proc.communicate()[0].decode("mbcs", "replace")

    # cnode habille chaque ligne d'un horodatage et d'un compteur memoire, et
    # coupe les lignes longues. On recolle le tout avant de chercher.
    flat = re.sub(r"\d\d:\d\d:\d\d\s+\d+/\d+MB\s*", "", raw)
    flat = re.sub(r"\s*\n\s*", " ", flat)

    ok = True
    for pattern, why in EXPECTED:
        if not re.search(pattern % module if "%s" in pattern else pattern, flat):
            print("  ECHEC  %s" % why)
            ok = False
        else:
            print("  ok     %s" % why)

    if "EXCEPTION_ACCESS_VIOLATION" in flat:
        print("  ECHEC  Clarisse a plante")
        ok = False
    if proc.returncode != 0:
        print("  ECHEC  cnode est sorti avec le code %d" % proc.returncode)
        ok = False

    if not ok:
        print("")
        print("Sortie complete de cnode, sans le bruit de demarrage :")
        for line in raw.splitlines():
            if line.strip() and not NOISE.search(line):
                print("    " + line.rstrip())
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "hello"))
