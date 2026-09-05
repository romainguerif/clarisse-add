# Mesurer la silhouette d'un premier plan flou

Un banc d'essai pour un seul defaut du filtre `ImageFilterBokeh`, et un nombre
qui dit s'il est repare.

## Le defaut

Le filtre va chercher, pour chaque pixel d'arrivee, dans un disque dont le
rayon vient de la profondeur de **ce** pixel. Les pixels de fond qui bordent un
premier plan flou sont nets : leur rayon vaut zero, ils sont recopies tels
quels, et rien du premier plan ne vient s'y deposer. La silhouette reste donc
franche alors qu'une vraie optique la ferait se dissoudre sur le decor, et
laisserait voir a travers son bord sur environ deux fois le rayon de flou.

C'est un defaut asymetrique, et c'est ce qui rend la mesure moins evidente
qu'il n'y parait : **l'interieur** de la silhouette, lui, est bien adouci meme
sans correction, parce qu'un pixel de premier plan a un grand rayon et ramene
donc du fond dans son disque. Seul le **cote fond** reste dur.

## Les fichiers

| | |
|---|---|
| `make_foreground_test.py` | construit la scene et ecrit un `.project` par variante |
| `measure_edge.py` | lit les EXR rendus et sort les largeurs de transition |

## La scene

Deux surfaces frontales, a profondeur constante, et rien d'autre :

- un mur au fond, a 80 unites, qui remplit le cadre, avec deux bandes de blocs
  tres contrastes en haut et en bas -- elles servent a **voir** sur l'image que
  le fond est net, et sont placees loin de la ligne de mesure ;
- une boite opaque devant, a 19 unites, qui occupe le tiers central du cadre.

La mise au point est faite sur le mur via `focus_object`, jamais par une
distance saisie a la main. Le rayon vaut 30 px, donc la transition attendue
d'un filtre correct est d'environ **60 px**.

Les deux surfaces sont des **emetteurs**, pas des surfaces diffuses. Une
surface diffuse rend son albedo multiplie par ce qu'elle recoit : dans un
premier essai, le mur clair renvoyait assez d'indirect sur la boite pour
ramener un contraste voulu de 6:1 a 1,7:1. Un emetteur rend exactement sa
valeur. Les deux plateaux valent donc `0.4` et `4.0` par construction, ce qui
donne 10:1 et, accessoirement, un controle de bout en bout : si `measure_edge`
ne relit pas exactement ces deux nombres, c'est la chaine de lecture des EXR
qui est en cause, pas le filtre.

## Ce que la mesure sort

Sur une ligne horizontale passant par le milieu de l'objet, le profil de
luminance est normalise entre le plateau de premier plan (0) et celui du fond
(1), puis on releve trois choses par bord :

| | |
|---|---|
| `10-90` | distance entre les passages a 90% et a 10%. C'est la largeur de transition demandee. |
| `debord` | de combien la transition depasse le bord geometrique **du cote du fond net**. |
| `penetration` | de combien elle mord **vers l'interieur** de l'objet. |

Le bord geometrique ne vient pas de la couleur mais de l'AOV de profondeur du
rendu **sans filtre** : la geometrie est la meme dans toutes les variantes,
donc toutes sont mesurees a la meme regle, et une variante dont le filtre
abimerait sa propre profondeur reste mesurable.

**`debord` est le nombre qui tranche.** `10-90` bouge deja sans correction,
parce que l'interieur de la silhouette est adouci de toute facon ; `debord`,
lui, ne peut monter que si le premier plan flou se depose reellement sur le
fond net. C'est exactement ce qu'on cherche a obtenir.

## Etat mesure

Rendu a 960x540, `radius` 30, quatre echantillons, le 5 septembre :

| variante | `10-90` G/D | `debord` G/D | `penetration` G/D |
|---|---|---|---|
| `slices_00_nofilter` | 1.47 / 0.80 | 1.10 / 0.40 | 0.37 / 0.40 |
| `slices_01` | 11.62 / 10.54 | 1.29 / 0.31 | 10.33 / 10.24 |
| `slices_10` | 9.10 / 10.23 | 9.73 / 9.85 | -0.63 / 0.38 |

Ce qu'il faut y lire :

- Le **plancher** de la mesure est donne par `slices_00_nofilter` : environ
  1 px, la largeur d'une marche franche a quatre echantillons. Une valeur de
  cet ordre veut dire « bord dur ».
- `slices_01` sort a environ **11 px** de `10-90`, pas a 1 px. Ce n'est pas une
  anomalie : c'est l'adoucissement interieur decrit plus haut. Son `debord`
  reste a **0,3-1,3 px**, c'est-a-dire au niveau du rendu sans filtre : la
  silhouette est bien restee dure du cote du fond. C'est le defaut, mesure.
- `slices_10` fait passer `debord` de 1 px a **environ 10 px**. La correction
  agit donc deja. Mais 10 px pour un rayon de 30, avec une `penetration`
  retombee a zero, veut dire que la transition s'est **deplacee** vers le fond
  plutot qu'**elargie** de part et d'autre.

## Ce qu'on attend d'une correction aboutie

Le premier plan est a rayon maximal sur toute sa surface, donc son bord devrait
s'etaler symetriquement autour de la silhouette :

- `debord` proche du **rayon**, soit environ **30 px**, des deux cotes ;
- `penetration` du meme ordre ;
- `10-90` de l'ordre du **diametre**, soit environ **60 px** ;
- `slices_01` doit rester ou il est -- c'est le temoin, il ne doit pas bouger.

Tant que `debord` reste sous 5 px, la silhouette est dure et la correction ne
fait pas son travail, quel que soit le reste.

## Lancer

Construire les scenes (quelques secondes, aucun rendu) :

```
cd "C:/Program Files/Isotropix/Clarisse 5.0 SP14/Clarisse"
./cnode.exe "C:/Users/Anon/Desktop/ClarisseAdd/native/tests/empty.project" \
  -module_path "C:/Program Files/Isotropix/Clarisse 5.0 SP14/Clarisse/module" \
                "C:/Users/Anon/Desktop/ClarisseAdd/native/build" \
  -script "C:/Users/Anon/Desktop/ClarisseAdd/native/tests/make_foreground_test.py"
```

Le compte rendu part dans `J:\_WINDOWSTEMP\claude\fg.log`, parce que la sortie
de cnode est trop bruyante pour qu'on y lise quoi que ce soit.

Rendre, environ deux secondes par variante :

```
cd "C:/Program Files/Isotropix/Clarisse 5.0 SP14/Clarisse"
for %V in (slices_00_nofilter slices_01 slices_10) do ./cnode.exe ^
  "J:/_WINDOWSTEMP/claude/fg/%V.project" ^
  -module_path "C:/Program Files/Isotropix/Clarisse 5.0 SP14/Clarisse/module" ^
               "C:/Users/Anon/Desktop/ClarisseAdd/native/build" ^
  -image "build://project/img_%V" -frames_list 1 ^
  -output "J:/_WINDOWSTEMP/claude/fg/%V.exr"
```

`-frames_list 1` est **obligatoire** : sans lui l'image est simplement sautee.

Mesurer :

```
python native/tests/measure_edge.py
```

## Ce qui a coute du temps

**cnode ignore l'attribut `resolution` de l'Image.** Il rend
`resolution_preset` multiplie par `resolution_multiplier`, quel que soit
`resolution_mode`. Le preset par defaut est 1920x1080 et le multiplicateur 1
vaut la moitie : toutes les images sortent en **960x540**, et demander 640x400
n'y change rien. `SetValues` sur `resolution` echoue d'ailleurs en silence --
il faut ecrire l'attribut directement, comme pour les listes d'AOV. Le
placement de la scene ne depend que du champ de la camera, donc la boite
occupe le tiers central quelle que soit la largeur finale, et `measure_edge`
lit la taille dans le rendu plutot que dans le manifeste.

**`field_of_view` d'une `CameraPerspective` est le champ VERTICAL.** Une boite
dimensionnee comme si c'etait l'horizontal ressort trop etroite d'exactement le
rapport hauteur/largeur -- une erreur qui ne se voit pas a la lecture du code.

**`%[fx:maxima]` ne distingue pas les canaux separes.** Les TIFF sortis de
`magick -separate` trainent un canal alpha qui contient encore la profondeur,
si bien que fx rend la meme valeur pour les cinq fichiers. Les maxima sont donc
recalcules sur les valeurs lues.

**`magick ... txt:-` conserve bien les valeurs HDR**, contrairement a ce que
laisse croire son entete `65535` : le champ entre parentheses est la valeur
multipliee par ce quantum, et il depasse allegrement -- 38.0 ressort en
2490330. C'est un entier, donc sans ambiguite de formatage.

**Avec `corrective_slices` a 10, le filtre ecrit dans l'AOV de profondeur**,
qui ressort a 251.10 la ou le rendu brut donne 79.90 -- un facteur tres proche
de pi. Le filtre n'est cense toucher que RGBA. `measure_edge` le signale et
prend le bord geometrique dans le rendu de reference, mais ce n'est qu'un
contournement : c'est un bug a corriger dans le filtre.
