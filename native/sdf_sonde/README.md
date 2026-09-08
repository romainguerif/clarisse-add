# Les sondes du document `docs/sdf-clarisse.md`

Sources conservées pour que les mesures soient refaisables. **Ce n'est pas du
code de production** — aucun de ces fichiers n'est destiné à devenir un module
livré. Le nœud `GeometrySdf` du §9.1 du document est à écrire proprement.

## Ce qu'il y a ici

| Fichier | Ce qu'il répond |
|---|---|
| `sdf_sonde.cid` / `.cpp` | **Clarisse accepte-t-il un champ de distance résolu par sphere tracing, avec mélange doux ?** Oui. Le module compte ses itérations, ses évaluations, ses rayons plafonnés et **la taille des paquets que le moteur lui passe**, et écrit tout dans un fichier au déchargement de la DLL. |
| `build.py` | La chaîne `cmagen` → `cl` → `.dll`, recopiée de `native/build.py` mais **avec le dossier courant comme source et comme sortie**. C'est volontaire : la session principale compile en permanence dans `native/build` et le dossier reste verrouillé. |
| `make_scene.py` | La scène de mesure, paramétrée par variables d'environnement. Un seul nœud, 400 × 400, éclairage plat : on mesure un coût par rayon, pas une image. |
| `run.py` | Construit puis rend les sept configurations, et rapproche le temps annoncé par le raytracer des statistiques du module. |
| `sdf_cpu.cpp` | Le banc CPU hors Clarisse : scalaire contre AVX2, un thread contre vingt-quatre, élagage ou non, mélange dur ou doux ; plus la **norme du gradient** du champ mélangé et la **robustesse du facteur de pas**. |
| `sdf_gpu.cu` | Le banc CUDA : **la latence d'un lancement**, l'aller-retour complet en fonction du nombre de rayons, le sphere tracing plein écran, et l'évaluation d'une grille dense. |
| `gl_probe.cpp` | **Les symboles GLEW réexportés par `ix_glutils.lib` se lient-ils depuis du code à nous ?** Oui, `glDispatchCompute` compris. |

## Comment relancer

Les trois bancs sont indépendants et n'ont besoin de rien d'autre que d'un
compilateur.

```
# la sonde Clarisse — compiler ailleurs que dans native/build
python build.py          # depuis une copie de ce dossier, hors du depot
python run.py

# le banc CPU
cl /O2 /arch:AVX2 /EHsc /std:c++14 sdf_cpu.cpp
sdf_cpu.exe 64 1024      # 64 primitives, image 1024x1024

# le banc GPU
nvcc -O3 -arch=sm_86 -std=c++14 sdf_gpu.cu -o sdf_gpu.exe
sdf_gpu.exe 64 1024

# la sonde d'edition de liens
cl /MD /EHsc gl_probe.cpp /link J:\Clarisse-SDK\lib\ix_glutils.lib
```

## Deux choses à savoir avant de s'en servir

**Les chemins absolus.** `sdf_sonde.cpp` écrit ses statistiques dans
`J:\_WINDOWSTEMP\claude\sdf_sonde\stats.txt`, et `build.py` sort dans son propre
sous-dossier `build/`. C'est acceptable pour une sonde, pas pour un module.

**Le piège hérité de `csg_sonde`.** `GeometryMediumDescriptor::clear()` doit
poser `opacity = 1`, pas 0. Avec 0, le moteur lit « milieu totalement
transparent », un filtre d'intersection jette le fragment, et **rien ne
s'affiche — sans un message**. Voir `docs/csg-clarisse.md` §2.3.
