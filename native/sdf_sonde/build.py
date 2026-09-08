# -*- coding: utf-8 -*-
"""Construit la sonde SDF hors de native/build.

La session principale compile en permanence dans native/build et le dossier
reste verrouille. On refait donc ici, a l'identique, ce que fait
native/build.py : cmagen puis cl, avec les memes bibliotheques et les memes
drapeaux, mais avec ce dossier-ci comme source et comme sortie.
"""
from __future__ import print_function

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLARISSE = r"C:\Program Files\Isotropix\Clarisse 5.0 SP14\Clarisse"
SDK = r"J:\Clarisse-SDK"
VCVARS = (r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
          r"\VC\Auxiliary\Build\vcvarsall.bat")

LIBS = ["ix_module", "ix_of", "ix_dso", "ix_core", "ix_gui", "ix_event",
        "ix_image", "ix_raytrace", "ix_poly", "ix_gmath", "ix_geometry",
        "ix_particle", "ix_resource", "ix_ctx", "ix_app", "ix_shader"]
SYSTEM_LIBS = ["opengl32.lib"]


def includes():
    paths = []
    for root in (os.path.join(SDK, "include"), os.path.join(SDK, "stubs")):
        paths.append(root)
        for base, folders, _ in os.walk(root):
            paths += [os.path.join(base, d) for d in sorted(folders)]
    return paths


def run(command, cwd=None):
    proc = subprocess.Popen(command, cwd=cwd, shell=isinstance(command, str),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = proc.communicate()[0].decode("mbcs", "replace")
    if output.strip():
        print(output.strip())
    return proc.returncode


def main():
    module = "sdf_sonde"
    out = os.path.join(HERE, "build")
    if not os.path.isdir(out):
        os.makedirs(out)

    print("[1/2] cmagen")
    code = run([os.path.join(CLARISSE, "cmagen.exe"), module + ".cid",
                "-module_path", os.path.join(CLARISSE, "module"),
                "-search_path", os.path.join(SDK, "cid")], cwd=HERE)
    if code:
        sys.exit("cmagen a echoue")

    inc = " ".join('/I"%s"' % p for p in includes() + [HERE])
    lib = " ".join('"%s"' % os.path.join(SDK, "lib", l + ".lib") for l in LIBS)
    lib += " " + " ".join(SYSTEM_LIBS)
    dll = os.path.join(out, module + ".dll")

    cl = ('cl /nologo /LD /MD /EHsc /std:c++14 /O2 /DNDEBUG /wd4267 /wd4244 '
          '%s "%s" /Fo:"%s\\\\" /Fe:"%s" /link /DLL %s'
          % (inc, os.path.join(HERE, module + ".cpp"), out, dll, lib))

    print("[2/2] compilation")
    code = run('call "%s" x64 >nul && %s' % (VCVARS, cl))
    if code:
        sys.exit("la compilation a echoue")
    print("OK : %s (%d octets)" % (dll, os.path.getsize(dll)))


if __name__ == "__main__":
    main()
