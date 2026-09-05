# Modules C++ pour Clarisse

Ce dossier contient de quoi ecrire, compiler et verifier des modules natifs
pour Clarisse iFX 5.0 SP14 -- des classes C++ qui s'ajoutent a l'application
au meme titre que ses classes d'origine, la ou un script Python ne peut pas
aller : filtres d'image, textures, deformeurs, geometries.

Isotropix a ferme en 2023. Le SDK n'est pas livre avec l'installation : ni
en-tetes, ni bibliotheques. Ceux utilises ici sont reconstruits dans
`J:\Clarisse-SDK` a partir de la seule documentation restee sur le disque
(voir son `RECONSTRUCTION.md`).

## Etat

**La chaine complete fonctionne, et c'est verifie.** Un module compile ici se
charge dans Clarisse, y declare sa classe, et ses objets s'instancient avec
leurs attributs. Le module temoin `hello/` ne sert qu'a le prouver, et
`tests/run.py` le reverifie en quelques secondes.

```
python build.py hello       # cmagen -> cl -> link -> build/hello.dll
python tests/run.py hello   # charge le .dll dans cnode et controle
```

## Comment ca marche

Un module, c'est deux fichiers.

Le **`.cid`** (Class Interface Definition) decrit la classe telle que
l'utilisateur la verra : son nom, sa classe de base, ses attributs, leurs
plages, leurs valeurs par defaut. `cmagen.exe`, livre avec Clarisse, le
traduit en un `.cma` -- un en-tete C++ qui porte la declaration encodee de la
classe et deux fonctions, `declare_class` et `create_cma`.

Le **`.cpp`** porte l'implementation. Il inclut le `.cma`, expose la fonction
`on_register_module` que Clarisse appelle au demarrage, et branche ses
callbacks.

`build.py` enchaine les trois etapes ; `tests/run.py` charge le resultat dans
`cnode` -- le moteur sans interface -- et verifie sans ouvrir de fenetre.

Les modules se chargent depuis n'importe quel dossier : c'est l'argument de
ligne de commande `-module_path` qui les designe. Il **remplace** le chemin par
defaut, il ne s'y ajoute pas, d'ou la reprise du `module/` d'origine :

```
clarisse.exe -module_path "<install>\Clarisse\module" "<ici>\native\build"
```

Il n'existe aucune variable d'environnement equivalente : la liste exhaustive
de celles que Clarisse reconnait est dans `app_env.h`, et aucune ne concerne
les modules.

## Deux pieges, tous deux verifies en plantant dessus

**La documentation du SDK donne un exemple minimal qui plante Clarisse.** Son
`declare_module` se contente de construire le module et de le rendre. Il
manque `set_object` :

```cpp
HelloModule *module = new HelloModule();
module->set_object(object);      // sans cette ligne, violation d'acces
return module;
```

`OfModule::is_protected()` et `get_object_name()` dereferencent `m_object`
sans le tester (`of_module.h:40-41`), et l'application interroge le module des
que l'objet rejoint son contexte. Le plantage tombe dans
`AppObjectImpl::on_object_factory_event`, tres loin de sa cause -- la pile
n'accuse que du code d'Isotropix.

**Le module rendu doit correspondre a la classe de base du CID.** Clarisse
caste sans verifier. Une classe qui derive de `ProjectItem` veut un module
derive de `ModuleProjectItem` ; un filtre d'image, un `ModuleKernelFilter`.
Un `ModuleObject` nu compile sans un mot et plante au meme endroit.

Accessoirement, la doc ecrit ces callbacks avec `ModuleObject *` la ou le vrai
typedef dit `OfModule *` (`of_class.h:35-36`) : elle a pris du retard sur le
code.

## Ce qu'il faut savoir avant d'ecrire un CID

Les `.cid` d'origine ne sont livres nulle part, mais leur texte integral est
dans la documentation de reference, une page par classe -- archivee dans
`J:\Clarisse-SDK\docs\reference\technical\`. C'est la source a lire avant
d'inventer quoi que ce soit. Elle contient aussi des mots-cles absents de la
grammaire officielle du SDK, `category` en tete.

La grammaire, elle, est dans `J:\Clarisse-SDK\docs\sdk\cid_intro.html`.

## Organisation

| | |
|---|---|
| `build.py` | cmagen, compilation, edition de liens |
| `hello/` | le module temoin : `hello.cid`, `hello.cpp` |
| `tests/run.py` | charge un module dans cnode et verifie |
| `tests/smoke.py` | ce que ce test execute cote Clarisse |
| `build/` | les `.dll` produits (non versionne) |

## Compilation

Clarisse est bati avec vc141 ; on compile en v142, le plus proche installe
ici. Microsoft garantit la compatibilite binaire de VS 2015 a VS 2022, et
l'API de Clarisse ne fait pas traverser de STL a sa frontiere -- elle passe ses
propres `CoreString`, `CoreArray`, `CoreVector`. Le vrai risque est ailleurs :
compiler en `/MD`, comme Clarisse, et ne jamais faire traverser d'allocation
entre deux CRT differents.

Bibliotheques liees : les cinq que la documentation declare obligatoires --
`ix_module`, `ix_of`, `ix_dso`, `ix_core`, `ix_gui` -- plus `ix_event`, que la
doc oublie mais dont les initialiseurs statiques d'evenements ont besoin des
qu'on inclut `of_app.h`.
