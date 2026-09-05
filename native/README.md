# Modules C++ pour Clarisse

Des classes C++ qui s'ajoutent a Clarisse au meme titre que les siennes, la ou
un script Python ne peut pas aller : cameras, filtres d'image, textures,
geometries.

Isotropix a ferme en 2023 et le SDK n'est pas livre avec l'installation : ni
en-tetes, ni bibliotheques. Ceux utilises ici sont reconstruits dans
`J:\Clarisse-SDK` a partir de la seule documentation restee sur le disque
(voir son `RECONSTRUCTION.md`).

## Les modules

| | |
|---|---|
| `bokeh_camera/` | **Camera Bokeh** — la profondeur de champ calculee par l'optique |
| `bokeh/` | **Filtre Bokeh** — le meme diaphragme, applique en 2D |
| `hello/` | module temoin, qui ne sert qu'a prouver que la chaine fonctionne |
| `common/` | la geometrie du diaphragme, partagee par les deux |

### Lequel utiliser

**La camera** echantillonne son diaphragme et lance de vrais rayons depuis de
vrais points de la lentille. L'occlusion derriere un objet net est donc juste,
il n'y a aucune carte de profondeur a fournir, et le flou se compose
correctement avec le flou de bouge et les volumes. C'est la verite optique.

Elle coute des echantillons. `Anti Aliasing Sample Count` vaut **9** par
defaut, soit neuf points de lentille par pixel : le bokeh ressort crible de
bruit. Une profondeur de champ propre en demande plusieurs dizaines.

**Le filtre** travaille sur une image finie. Il est instantane, se retouche
apres coup, et sert pour tout ce qui n'a pas ete rendu avec la camera. En
contrepartie il ne sait pas ce qu'il y a derriere un objet net -- limite de
tout post-traitement, sans exception.

Les deux partagent `common/aperture.h`, donc la forme du bokeh est identique.

### Ce que la camera ne peut pas faire

**L'aberration chromatique.** Elle demande que le rouge, le vert et le bleu
partent de points de lentille differents, donc trois rayons par echantillon
dont chacun ne contribuerait qu'a son canal. Un rayon de Clarisse revient avec
un triplet RVB complet et rien ne permet de le restreindre a un canal :
`RayGeneratorData` ne porte aucune longueur d'onde, et le moteur est RVB de
bout en bout. Les moteurs qui font cela a la camera sont spectraux.

Elle reste donc du ressort du filtre.

## Construire

```
python build.py bokeh_camera
python build.py bokeh
python tests/run.py hello        # verifie qu'un module se charge
```

`build.py` enchaine `cmagen`, la compilation et l'edition de liens.
`tests/run.py` charge le `.dll` dans **cnode** -- le moteur sans interface --
et controle en quelques secondes, sans ouvrir Clarisse.

**Apres une reconstruction, il faut relancer Clarisse.** Une classe deja
declaree n'est pas remplacee a chaud : `scan_modules` ajoute, il n'ecrase pas.

## Comment ca marche

Un module, c'est deux fichiers. Le **`.cid`** decrit la classe telle que
l'utilisateur la verra : nom, classe de base, attributs, plages, valeurs par
defaut. `cmagen.exe` le traduit en `.cma`, un en-tete C++ portant la
declaration encodee et deux fonctions, `declare_class` et `create_cma`. Le
**`.cpp`** inclut ce `.cma`, expose `on_register_module` que Clarisse appelle
au demarrage, et branche ses callbacks.

Le chargement est automatique : l'installeur de ClarisseAdd pose un script de
demarrage dans `clarisse.env`, qui appelle `AppObject::scan_modules` sur
`native/build`. Aucune variable d'environnement n'existe pour les modules --
la liste exhaustive de celles que Clarisse reconnait est dans `app_env.h` --
et `-module_path` en ligne de commande **remplace** le chemin par defaut au
lieu de s'y ajouter.

## Ce qui coute du temps quand on ne le sait pas

**L'exemple minimal de la documentation d'Isotropix plante Clarisse.** Son
`declare_module` oublie `set_object` ; `OfModule::is_protected()` et
`get_object_name()` dereferencent `m_object` sans le tester, et l'application
interroge le module des que l'objet rejoint son contexte. La violation d'acces
tombe dans `AppObjectImpl::on_object_factory_event`, une pile ou n'apparait
que du code d'Isotropix.

**Le module rendu doit correspondre a la classe de base du CID**, que Clarisse
caste sans verifier. Accessoirement la doc ecrit ces callbacks avec
`ModuleObject *` la ou le vrai typedef dit `OfModule *` (`of_class.h:35-36`).

**Clarisse execute les scripts de demarrage par `PyRun_String`** : `__file__`
n'y est pas defini. D'ou un lanceur genere a l'installation, avec la racine en
dur.

**`cmagen` deduit le nom de la bibliotheque du nom de la classe de base.**
`Camera` se resout depuis `camera.dll` ; `CameraPerspectiveAdvanced` non, elle
vit dans `cameras.dll`. La camera reprend donc les attributs de perspective
verbatim, extraits de la documentation de reference par
`J:\Clarisse-SDK\tools\extract_cid.py`.

**Certains attributs sont `read_only` dans le CID** -- `f_stop` et
`focus_distance` le sont. Ce n'est pas definitif : Clarisse leve le verrou
depuis le module quand la profondeur de champ est activee. Sans
`cb_on_attribute_change`, les deux reglages restent gris et la mise au point
ne repond a rien.

**`ix.cmds.AddLayer` renvoie `None` meme quand elle reussit**, et `SetValues`
sur un attribut a plusieurs composantes veut un chemin par composante. Les
deux font conclure a un echec sur des operations parfaitement valides.

## Le contrat de `CtxKernelFilter`

Aucune de ces informations n'est documentee : les pages Doxygen listent les
champs et laissent toutes les descriptions vides. Ce qui suit a ete releve en
instrumentant un filtre, puis recoupe en desassemblant `ix_module.dll`.

| | |
|---|---|
| `ctx.image` | la **source**, deja elargie de `kernel_radius` de chaque cote |
| `ctx.region` | la tuile a ecrire, **en coordonnees du proxy** : `{r, r, w, h}` |
| `ctx.channel_*` | la **destination**, sans marge, indexee `y * region.width + x` |
| `ctx.x0, y0` | la tuile en coordonnees image |
| bords | remplis en **CLAMP** par Clarisse : aucun traitement a ecrire |
| `pre_filter` | seul `source_image` y est valide ; lire `dest_image` plante |
| retour | `true` valide la tuile, `false` la jette |

La destination arrive pre-remplie avec la source : un filtre qui n'ecrit rien
ne casse rien.

## Compilation

Clarisse est bati avec vc141 ; on compile en v142, le plus proche installe
ici. Microsoft garantit la compatibilite binaire de VS 2015 a VS 2022, et
l'API de Clarisse ne fait pas traverser de STL a sa frontiere -- elle passe
ses propres `CoreString`, `CoreArray`, `CoreVector`. Le vrai risque est
ailleurs : compiler en `/MD`, comme Clarisse, et ne jamais faire traverser
d'allocation entre deux CRT differents.

Bibliotheques liees : les cinq que la documentation declare obligatoires --
`ix_module`, `ix_of`, `ix_dso`, `ix_core`, `ix_gui` -- plus `ix_event`, que la
doc oublie mais dont les initialiseurs statiques d'evenements ont besoin des
qu'on inclut `of_app.h`, `ix_image` pour `ImageCanvas` et `ImageProxy`, et
`ix_raytrace` pour le generateur de rayons de camera.
