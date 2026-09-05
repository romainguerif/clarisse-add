# Le format `.project` de Clarisse

Notes de rétro-ingénierie établies en lisant les 23 scènes de
`assets/presets/`, le `shelf.cfg` utilisateur et la documentation SDK hors ligne.
Elles décrivent ce que `clarisse_add/core/project_file.py` sait lire — pas
nécessairement tout le format.

## Forme générale

Texte, UTF-8, arbre d'accolades. Ni virgules, ni deux-points, ni guillemets
obligatoires.

```
#Isotropix_Serial_Version 1.2

#Isotropix_Clarisse_Version 4
#Isotropix_Clarisse_Project_Version 0.94
Context "scene" {
    #created 1455809270
    CameraPerspective {
        name "camera"
        #version 0.9
        translate 28 21 28
        rotate -27.938 45 0.0
        field_of_view 25
        objects "project://scene/box" "project://scene/sphere"
    }
}
```

Règles :

- un **bloc** est `NomDeClasse { … }`, éventuellement `NomDeClasse "label" { … }` ;
- une **ligne d'attribut** est `nom valeur [valeur …]` — les valeurs multiples
  sont les composantes d'un vecteur, ou les éléments d'une liste ;
- une valeur est soit une chaîne entre guillemets (échappement par `\`), soit un
  mot nu (nombre, `yes`/`no`, `<empty>`) ;
- un attribut peut n'avoir aucune valeur : `private` seul vaut « vrai » ;
- l'accolade ouvrante est **toujours sur la ligne de l'en-tête** ;
- les lignes commençant par `#` **à l'intérieur d'un bloc** sont des métadonnées
  (`#created`, `#version`, `#modified`), pas des commentaires ;
- les lignes `#Clé valeur` **en tête de fichier** sont l'en-tête de version.

Le même format sert au `shelf.cfg`, aux préférences et aux fichiers `.build`.

## Ce qui n'est pas la scène

Un `.project` contient **aussi la disposition des fenêtres**, sous un bloc
`#preferences` de premier niveau. Sans filtre, l'inventaire d'une scène de trois
cubes remonte deux cents `tab`, `split_v`, `viewport_widget` et `local_selection`.

Mesuré sur la bibliothèque : `Cactus.project` passe de **246 blocs à 87 objets**
une fois `#preferences` écarté, `Desert_v2` de 901 à 707.

`iter_objects()` saute donc :

- tout bloc dont le nom commence par `#` ;
- les blocs `custom_attributes` (ce sont des paramètres, pas des objets) ;
- sur demande, `embedded_objects` — les réglages internes du renderer, présents
  dans toute scène, qui ajoutent une trentaine d'objets identiques partout.

Restent des blocs en minuscules (`input1`, `color`, `value[]`, `normal_input`) :
ce sont des sous-structures d'attributs texturés, pas des objets. Le catalogue ne
garde donc que les classes commençant par une majuscule.

## Attributs custom — le cœur du sujet

C'est ce qui distingue une scène d'exemple d'une scène-outil : les paramètres
exposés à l'artiste sont **déclarés dans le fichier**.

```
custom_attributes {
    attribute_group "input" {
        filename_open "OSL_filename" {
            doc "filename"
            value "$PDIR/WBX_Office_Day.png"
        }
        double "OSL_roomDepth" {
            doc "roomDepth"
            texturable yes
            animatable yes
            numeric_range yes 0.100000001490116 100
            ui_range yes 0.100000001490116 100
            shading_variable yes
            value 1.5
        }
        long "OSL_enableCurtains" {
            doc "enableCurtains"
            value 0
        }
        string "OSL_output_name" {
            doc "Select shader output to fill texture RGB color."
            preset "outRGB" "outRGB"
            value "outRGB"
        }
    }
}
```

Ce qu'on en tire, dans `CustomAttribute` :

| Champ du fichier | Devient | Sert à |
|---|---|---|
| nom du bloc | `type` | choisir le widget |
| label du bloc | `name` | écrire la valeur avec `SetValues` |
| `doc` | `label` | libellé lisible du champ |
| `value` | `default` | valeur initiale (liste si vecteur) |
| `ui_range` / `numeric_range` | `minimum`, `maximum` | bornes du slider |
| `preset "libellé" "valeur"` | `presets` | liste déroulante |
| `attribute_group "x"` | `group` | section du formulaire |

`ui_range` est préféré à `numeric_range` : c'est la plage d'affichage, la seconde
étant la borne dure du type.

Types rencontrés dans la bibliothèque : `double`, `long`, `string`, `bool`,
`filename_open`, `color`.

Cette lecture suffit à générer une fenêtre de réglages complète sans écrire une
ligne de code spécifique au preset — voir `tools/preset_runner.py`.

## Chemins de fichiers

Trois formes coexistent :

- **absolue** — `U:/projects/Black_Cauldron/geo/Bricks/Brick_Flat.obj`. C'est ce
  qu'on trouve dans les scènes récupérées, et ça pointe vers la machine de
  quelqu'un d'autre.
- **relative à `$PDIR`** — le dossier du `.project`. La seule forme portable.
- **motifs** — `###`, `$F4`, `<UDIM>`, `*`. Ils désignent une séquence : le
  chemin ne peut pas être testé tel quel, et `FileReference.exists()` renvoie
  `None` plutôt que `False` pour ne pas les déclarer manquants à tort.

`build_catalog.py` résout `$PDIR` et signale ce qui manque réellement ; les deux
presets restants pointent vers des HDRI de scène de démonstration, pas vers
l'outil lui-même.

## Ce que le parser ne fait pas

- **Il ne réécrit pas de `.project`.** L'écriture perdrait la distinction entre
  valeur citée et mot nu, ce qui suffirait à corrompre un fichier. Les
  modifications de `shelf.cfg` se font par tranches de lignes, en s'appuyant sur
  les bornes `line` / `end_line` de chaque nœud.
- **Il ne résout pas les références.** `"project://scene/box"` reste une chaîne :
  résoudre demanderait de construire l'index de la scène, dont on n'a pas besoin
  ici.
- **Il n'interprète pas les expressions** SeExpr des attributs.

Pour une conversion complète `.project` → USD, voir
[tropix](https://github.com/romainguerif/tropix), qui fait un vrai parser
récursif descendant avec AST typé.

## Vérification

`tests/test_project_file.py` valide le parser sur les 23 fichiers réels de
`assets/presets/` — jamais sur des extraits inventés. Un format propriétaire ne
se devine pas : chaque règle notée ici vient d'un fichier existant.
