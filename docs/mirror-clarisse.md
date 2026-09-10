# Chantier Mirror — node `GeometryMirror`

> Plan valide (skill `architecte`) — 2026-09-10.
> Toute API citée est vérifiée dans le code existant du projet ou dans les
> headers du SDK (`J:\Clarisse-SDK`) avant d'être utilisée.

## 🎯 Objectif

Node Clarisse `GeometryMirror` (dérivé de `GeometryPolymesh`) qui reflète une
polymesh autour d'un plan (fixe ou piloté par un locator), soude les sommets
coïncidents, peut couper la géométrie au plan et décaler la copie.

Remplace la première version de `native/mirror/mirror.cpp` (écrite le
2026-09-10, non compilable — APIs inventées, clamp cassé).

## 🏗️ Architecture globale

Un seul module C++ (`GeometryMirrorModule`, dérivé de `ModulePolymesh`) :

1. **Lecture source** — `get_attribute("geometry")` →
   `get_module<ModuleGeometry>()` → `get_geometry(false)`, lecture séparée de
   la point cloud (positions + matrice objet→monde) et de la topologie
   (indices de polygones, tailles, shading groups, UV). Pattern exact :
   `face_select.cpp` lignes 248-284 + `curve_core.h` lignes 292-332.
2. **Plan de travail en espace monde** — centre et normale soit en attributs
   directs, soit extraits de la matrice globale du locator (position = centre,
   axe Y local = normale). Pattern identique : `SdfLocator` (`sdf.cpp`).
3. **Miroir** — réflexion standard par sommet + décalage `offset·n` de la
   copie.
4. **Clamp** (option) — clipping de chaque polygone par le demi-plan `(p−c)·n ≥ 0`
   (Sutherland-Hodgman par polygone, faces vides jetées).
5. **Weld** (option) — fusion des sommets dont la position quantisée
   (grille = `weld_threshold`) est identique, sur la liste fusionnée
   source + copie.
6. **Sortie** — un seul `PolyMesh` reconstruit via `PolyMesh::set(…)` (10
   paramètres, vélocités vides, shading group `"default"`), retourné par
   `create_resource(RESOURCE_ID_GEOMETRY)`.

## 📁 Fichiers

- `native/mirror/mirror.cpp` — **réécrit intégralement**
- `native/mirror/mirror.cid` — inchangé (UI complète : plan, locator, offset,
  clamp, weld, threshold)
- `docs/mirror-clarisse.md` — ce fichier, statut tenu à jour
- `docs/clarisse-add.md` — index à jour avec l'entrée `mirror`

## 🔧 APIs vérifiées (sources : headers SDK + code du projet)

| API | Source |
|---|---|
| `ModuleGeometry::get_geometry(false)` | `curve_core.h:301`, `face_select.cpp:250` |
| `CoreBaseObject::cast<PolyMesh>(geometry)` | `face_select.cpp:252` |
| `GeometryPointCloud::get_point_count()`, `get_position(i)` → `GMathVec3f` | `face_select.cpp:264-266` |
| `ModuleSceneItem::get_global_matrix()` → `GMathMatrix4x4d` | `face_select.cpp:260-262`, `curve_core.h:313-315`, `sdf.cpp` (locator) |
| `GMathMatrix4x4d::multiply(out, v, matrix)` | `face_select.cpp:272` |
| `PolyMesh::get_polygon_vertex_indices/count/shading_groups` | `face_select.cpp:257-258`, `poly_mesh.h:200-210` |
| `PolyMesh::get_uv_map_count()`, `get_uv_map_data()` | `poly_mesh.h:44-50` |
| `PolyMesh::set(vertices, velocities, polygon_indices, polygon_vertex_count, polygon_shading_groups, shading_group_names, uv_maps, normal_maps, color_maps, handle_degenerated_quad, progress_bar)` | `poly_mesh.h:162` (signature exacte), usage : `cloth_panel.cpp:854-859` |
| `create_resource(id, data)` override + delegation à `ModulePolymesh::create_resource` | `cloth_panel.cpp:576-584` |
| `set_resource_attrs(RESOURCE_ID_GEOMETRY, attrs)` pour dirty | `cloth_panel.cpp:573` |
| Footer `IX_MODULE_CLBK::declare_module/destroy_module` | `cloth_panel.cpp:1147-1158` |
| Helpers vecteurs `vsub`, `vdot`, `vnorm` | `curve_core.h` (usage ligne 354-355) |
| Conversion `GMathVec3d` → `GMathVec3f` par composante | `cloth_panel.cpp:819-824` |

## 📋 Étapes

1. **Échafaudage module** — skeleton `GeometryMirrorModule` (classe, helpers
   `read_*`, `set_resource_attrs`, `create_resource` délégué, footer
   `IX_MODULE_CLBK`). **Fait** : `python build.py mirror` compile.
2. **Lecture source** — `read_source()` sur le polymesh d'entrée en espace
   monde. **Fait** : compile, retourne 0 si géométrie vide.
3. **Miroir** — réflexion + offset des sommets, assemblage source + copie.
   **Fait** : compile, testé dans Clarisse (cube sur le plan → cube double).
4. **Clamp** — clipping demi-plan par polygone. **Fait** : compile, cube
   coupé → moitié exacte, pas de trou ni de dérive.
5. **Weld** — union-find sur clés quantisées (grille = `weld_threshold`).
   **Fait** : compile, maillage unique traversant (sélection : un seul
   sommet au centre de la jointure).
6. **UI wiring** — branchement des 8 attributs. **Fait** : chaque attribut
   déplace la géométrie dans Clarisse.
7. **Docs + git** — ce fichier + index à jour, commit, push.

## ✅ Critères de validation

- `python build.py mirror` → `build/mirror.dll`, zéro erreur
- Dans Clarisse (cube 1 m à l'origine) :
  - weld off → cube de 2 m, jointure à plat
  - weld on, threshold 0.001 → maillage unique, sélection d'arêtes traverse
  - clamp on → moitié de 1 m exacte
  - use_locator + locator déplacé → géométrie suit en direct
  - offset 0.1 → écart de 0.2 m entre les deux moitiés
- git : commit `mirror` + push `origin/main`

## ⚠️ Points à vérifier (décider AVANT l'étape concernée)

- **C++14 vs 17** : si le build impose C++14, pas de
  `std::unordered_map` sur clé composite → quantiser en triple d'entiers +
  table `CoreVector` + recherche linéaire par polygone. *Lever : lire les
  flags `cl` dans `build.py` (ligne ~71).*
- **Weld avec threshold = 0** : grille de taille 1 → fallback
  « equality exact » ou distance euclidienne `vdot(vsub(a,b),vsub(a,b)) < ε²`
  sur les voisins. *Décider à l'étape 5.*
- **Sens de la normale du locator** : `normal = mat * (0,1,0)` normalisée ;
  tester locator pivoté à 90°.
- **UV après weld** : par défaut on garde les UV de chaque côté, pas de
  moyenne — limiter documenté. *Valider avec une texture UV.*
- **Shading groups multiples** : on réutilise les mêmes noms pour la copie.
  *Valider sur un mesh importé à plusieurs groupes.*
- **Perf weld** : O(n²) par polygone en boucle linéaire. *Mesurer sur un
  gros asset ; si > 2 s passer à un grid hash.*

## Extension V2 (documentée, non planifiée)

- Sélection du côté clamped / inversion de normale
- Snap sur la topologie de la face d'origine
- Mirror multi-plans (groupe de plans)
