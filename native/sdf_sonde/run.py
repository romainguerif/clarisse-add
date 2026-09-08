# -*- coding: utf-8 -*-
"""Construit les scenes puis les rend, une configuration a la fois.

Chaque rendu ecrit une ligne dans stats.txt au dechargement de la DLL, et on
mesure ici le temps de mur. Le rapprochement des deux donne le cout par rayon
que le moteur paie reellement.
"""
from __future__ import print_function

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLARISSE = r"C:\Program Files\Isotropix\Clarisse 5.0 SP14\Clarisse"
CNODE = os.path.join(CLARISSE, "cnode.exe")
MODULES = os.path.join(CLARISSE, "module")
BUILD = os.path.join(HERE, "build")
STATS = os.path.join(HERE, "stats.txt")

# nom, mode, split, blend, blobs, pas, plafond
CONFIGS = [
    ("analytique-union-dure",      1, True,  0.00, 64, 1.0, 128),
    ("tracing-decoupe-dur",        0, True,  0.00, 64, 1.0, 128),
    ("tracing-decoupe-doux",       0, True,  0.25, 64, 1.0, 128),
    ("tracing-monolithe-doux",     0, False, 0.25, 64, 1.0, 128),
    ("tracing-decoupe-doux-pas05", 0, True,  0.25, 64, 0.5, 128),
    # Le mode d'echec : a plafond bas la surface disparait par plaques.
    ("plafond-24",                 0, False, 0.25, 64, 1.0, 24),
    ("plafond-12",                 0, False, 0.25, 64, 1.0, 12),
]


def render(name, mode, split, blend, blobs, step, maxit):
    project = os.path.join(HERE, "scene_%s.project" % name)
    env = dict(os.environ)
    env.update({
        "SDF_OUT": project, "SDF_MODE": str(mode),
        "SDF_SPLIT": "1" if split else "0", "SDF_BLEND": str(blend),
        "SDF_BLOBS": str(blobs), "SDF_STEP": str(step),
        "SDF_MAXIT": str(maxit),
    })

    empty = os.path.join(HERE, "empty.project")
    if not os.path.isfile(empty):
        open(empty, "w").write("#version 5.0 SP14\n")

    build = [CNODE, empty, "-module_path", MODULES, BUILD,
             "-script", os.path.join(HERE, "make_scene.py")]
    p = subprocess.Popen(build, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    out = p.communicate()[0].decode("mbcs", "replace")
    if not os.path.isfile(project):
        print("  ECHEC de construction :", name)
        print(out[-2000:])
        return None

    before = os.path.getsize(STATS) if os.path.isfile(STATS) else 0
    cmd = [CNODE, project, "-module_path", MODULES, BUILD,
           "-image", "build://project/image", "-frames_list", "1",
           "-output", os.path.join(HERE, "out_%s.exr" % name)]
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.communicate()[0].decode("mbcs", "replace")
    dt = time.time() - t0

    line = ""
    if os.path.isfile(STATS):
        with open(STATS) as f:
            f.seek(before)
            line = f.read().strip()
    if not line:
        print("  pas de statistiques pour", name)
        print(out[-2500:])

    # Le temps de mur inclut ~2 s de demarrage de cnode. Le seul chiffre
    # comparable est celui que le raytracer annonce lui-meme.
    render = None
    for chunk in out.replace("\n", " ").split("rendered in "):
        head = chunk.split(" s")[0].strip()
        try:
            render = float(head)
        except ValueError:
            pass
    print("%-30s mur %6.2f s | raytrace %7.3f s | %s"
          % (name, dt, render if render else -1.0, line))
    return dt


if __name__ == "__main__":
    if os.path.isfile(STATS):
        os.remove(STATS)
    log = os.path.join(HERE, "build_log.txt")
    if os.path.isfile(log):
        os.remove(log)
    for c in CONFIGS:
        render(*c)
    print("\n--- journal de construction ---")
    if os.path.isfile(log):
        print(open(log).read())
