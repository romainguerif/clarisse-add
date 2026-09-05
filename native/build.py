# -*- coding: utf-8 -*-
"""Construit un module C++ pour Clarisse iFX 5.0 SP14.

    python build.py hello

Enchaine les trois etapes : cmagen produit le .cma a partir du .cid, cl compile,
link produit le .dll. Le resultat est depose dans `build/<module>.dll`.

Le SDK n'existe pas : Isotropix a ferme, l'installation ne livre que la
documentation. Les en-tetes et les bibliotheques d'import utilises ici sont
reconstruits dans J:/Clarisse-SDK (voir son RECONSTRUCTION.md).
"""
from __future__ import print_function

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

CLARISSE = r"C:\Program Files\Isotropix\Clarisse 5.0 SP14\Clarisse"
SDK = r"J:\Clarisse-SDK"

# Clarisse est bati avec vc141. Microsoft garantit la compatibilite binaire de
# VS 2015 a VS 2022 ; v142 est le toolset le plus proche installe ici. Le point
# qui compte vraiment est plus bas : /MD, comme Clarisse.
VCVARS = (r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
          r"\VC\Auxiliary\Build\vcvarsall.bat")

# Les cinq que la doc declare obligatoires, plus deux qu'elle oublie.
#
# ix_event : les en-tetes d'evenements declarent des identifiants globaux
# dont l'initialiseur statique appelle event_debug_collisions(). Le symbole
# est tire des qu'on inclut of_app.h, que le module se serve d'evenements
# ou non.
#
# ix_image : ImageCanvas et ImageProxy. Leurs accesseurs sont ecrits inline
# dans les en-tetes, mais la classe porte __declspec(dllimport) : MSVC va
# alors chercher le symbole dans la DLL au lieu d'inliner le corps. Tout
# module qui touche a des pixels en a besoin.
LIBS = ["ix_module", "ix_of", "ix_dso", "ix_core", "ix_gui", "ix_event",
        "ix_image"]


def includes():
    """Les en-tetes s'incluent a plat (<of_app.h>) alors qu'ils sont ranges en
    sous-dossiers. Il faut donc tous les sous-dossiers dans le chemin.

    `stubs/` vient apres `include/` : il porte des fichiers d'implementation
    vides, la ou l'original est perdu. Voir tools/stub_impl_files.py du SDK.
    """
    paths = []
    for root in (os.path.join(SDK, "include"), os.path.join(SDK, "stubs")):
        paths.append(root)
        for base, folders, _ in os.walk(root):
            paths += [os.path.join(base, d) for d in sorted(folders)]
    return paths


def run(command, cwd=None):
    line = command if isinstance(command, str) else " ".join(command)
    print("  $", line if len(line) < 200 else line[:120] + " [...] " + line[-60:])
    proc = subprocess.Popen(command, cwd=cwd, shell=isinstance(command, str),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.communicate()[0].decode("mbcs", "replace")
    if output.strip():
        print("    " + output.strip().replace("\n", "\n    "))
    return proc.returncode


def build(module):
    src = os.path.join(HERE, module)
    if not os.path.isdir(src):
        sys.exit("dossier introuvable : %s" % src)
    out = os.path.join(HERE, "build")
    if not os.path.isdir(out):
        os.makedirs(out)

    cids = [f for f in sorted(os.listdir(src)) if f.endswith(".cid")]
    cpps = [f for f in sorted(os.listdir(src)) if f.endswith(".cpp")]
    if not cpps:
        sys.exit("aucun .cpp dans %s" % src)

    print("[1/3] cmagen  (%d cid)" % len(cids))
    for cid in cids:
        code = run([os.path.join(CLARISSE, "cmagen.exe"), cid,
                    "-module_path", os.path.join(CLARISSE, "module")], cwd=src)
        if code:
            sys.exit("cmagen a echoue sur %s" % cid)

    inc = " ".join('/I"%s"' % p for p in includes() + [src])
    lib = " ".join('"%s"' % os.path.join(SDK, "lib", l + ".lib") for l in LIBS)
    sources = " ".join('"%s"' % os.path.join(src, c) for c in cpps)
    dll = os.path.join(out, module + ".dll")

    # /MD : meme CRT que Clarisse. Une allocation qui traverse deux CRT
    # differents corrompt le tas, et le plantage tombe loin de la cause.
    # /EHsc, /std:c++14 : ce que les en-tetes reclament.
    cl = ('cl /nologo /LD /MD /EHsc /std:c++14 /O2 /DNDEBUG '
          '/wd4267 /wd4244 %s %s /Fo:"%s\\\\" /Fe:"%s" /link /DLL %s'
          % (inc, sources, out, dll, lib))

    print("[2/3] compilation et edition de liens")
    code = run('call "%s" x64 >nul && %s' % (VCVARS, cl))
    if code:
        sys.exit("la compilation a echoue")

    print("[3/3] %s  (%d octets)" % (dll, os.path.getsize(dll)))
    return dll


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build(sys.argv[1])
