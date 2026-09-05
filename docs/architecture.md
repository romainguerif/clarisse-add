# Comment ClarisseAdd s'accroche à Clarisse

## Le problème

Clarisse n'a pas de notion d'*addon*. Il a un shelf, dont chaque bouton est un
chemin vers un fichier `.py` que l'application exécute avec `ix` injecté dans les
globales. Il n'y a ni manifeste, ni installation, ni rechargement.

Trois mécanismes permettent d'y greffer du code :

| Mécanisme | Variable | Ce que ça vaut |
|---|---|---|
| Shelf utilisateur | `%APPDATA%/Isotropix/Clarisse/<v>/shelf.cfg` | persistant, mais un seul fichier partagé avec l'artiste |
| Shelf de remplacement | `IX_SHELF_CONFIG_FILE` | remplace le shelf natif — inutilisable |
| Enregistrement à chaud | `ix.application.get_shelf().add_item()` | volatile, disparaît à la fermeture |

ClarisseAdd utilise le **premier** pour l'installation, et le **troisième** pour
le rechargement en cours de session.

## Le chemin d'un clic

```
   [bouton du shelf]
          │  script_filename → clarisse_add/entry/scatter_shrinkwrap.py
          ▼
   stub généré (7 lignes)
          │  sys.path += ADDON_ROOT
          │  bootstrap.launch("scatter.shrinkwrap", ix)
          ▼
   bootstrap.launch
          │  compat.set_ix(ix)          ← ix devient accessible partout
          │  sys.path += vendor/         ← le Survival Kit s'importe en absolu
          │  manifest.by_id(...)         ← retrouve le Tool
          │  importlib.import_module(...)
          ▼
   tools/shrinkwrap.py :: run(payload)
          │  toute exception est attrapée ici,
          │  journalisée avec sa pile, et signalée
          ▼
   core/{scene,ui,files} → ix.cmds.*
```

Les stubs sont générés depuis le manifeste plutôt qu'écrits à la main : c'est ce
qui garantit qu'un bouton du shelf et un module de l'addon ne peuvent pas
diverger. `install.py` les régénère et supprime ceux dont l'outil a disparu.

### Pourquoi un stub, et pas le module directement

Clarisse exécute le fichier désigné, il ne l'importe pas. Sans indirection, il
faudrait que chaque outil soit un script autonome — c'est exactement ce que fait
le Survival Kit, et c'est pourquoi ses vingt-six fichiers dupliquent le même
préambule.

## `ix` n'est pas un module

Clarisse injecte `ix` dans les globales du script exécuté. Un module importé
normalement ne le voit pas. Le Survival Kit contourne ça en passant `ix=ix` dans
les kwargs de chaque fonction — d'où des signatures comme
`def moisten_surface(ctx, height_blend=True, ..., **kwargs)` avec un
`ix = get_ix(kwargs.get("ix"))` en première ligne de chacune.

Ici, `bootstrap.launch` l'enregistre une fois dans `core.compat`, et chaque
module fait `ix = get_ix()` dans son `run()`. Conséquence utile : les modules
s'importent **sans Clarisse**, ce qui permet de tester le manifeste, le parser et
le catalogue avec un `pytest` ordinaire — 186 tests, aucun n'a besoin de
l'application.

`get_ix()` lève `ClarisseUnavailable` si personne n'a appelé `set_ix()` : l'appel
hors contexte échoue clairement au lieu de produire un `NameError` obscur.

## Écriture du `shelf.cfg`

Le `shelf.cfg` est **au même format que les `.project`** : même en-tête
`#Isotropix_Serial_Version`, même syntaxe `Bloc { attribut valeur }`. Le parser
de `core/project_file.py` le lit donc directement, et chaque nœud porte ses
numéros de ligne d'ouverture et de fermeture.

L'installation en découle :

1. parser le fichier existant ;
2. relever les plages de lignes des catégories commençant par `ClarisseAdd` ;
3. reconstruire le fichier ligne à ligne, en sautant ces plages et en insérant le
   bloc généré avant l'accolade fermante du slot.

Tout ce qui n'appartient pas à l'addon ressort **octet pour octet identique**. Un
test le vérifie : hors des catégories de l'addon, une installation ne modifie
qu'une seule ligne, `category_selected`.

C'est la différence de fond avec le Survival Kit, qui cherche ses blocs par
expressions régulières calées sur des niveaux d'indentation
(`r"\s{12}shelf_item {(.*?)(?<! ) {12}(?! )}"`). Ça marche tant que personne n'a
touché au fichier.

## Rechargement à chaud

Clarisse garde un interpréteur Python vivant pour toute la session : un module
importé une fois le reste. Sans rechargement, modifier un outil impose de
redémarrer l'application — plusieurs minutes sur une grosse scène.

Le bouton **Reload** :

1. `catalog.reload()` + `manifest.invalidate()` — relit le catalogue ;
2. `shelf.write_entry_scripts()` — régénère les stubs des outils ajoutés ;
3. `shelf.register_runtime()` — réenregistre les boutons via `AppShelf.add_item()`,
   sans toucher au disque ;
4. `bootstrap.reload_addon()` — purge `sys.modules` de tout `clarisse_add.*`.

`bootstrap` s'exclut lui-même de la purge : on est en train d'y tourner, et le
recharger sous ses propres pieds laisserait deux copies du module en mémoire.

`register_runtime` renvoie `-1` si `AppShelf` n'est pas exposé sur la version
installée — le rechargement du code fonctionne quand même, seuls les *nouveaux*
boutons demandent alors une réinstallation.

## Undo

Les commandes Clarisse ne sont pas empilées dans l'historique quand elles
viennent d'un script, sauf à l'activer. Un outil qui crée quarante nœuds sans
précaution laisse quarante entrées dans l'historique : quarante Ctrl+Z.

`scene.command_batch(label)` enveloppe le tout en un seul undo, et ferme le batch
même si le bloc lève — sinon l'historique de Clarisse reste ouvert et le reste de
la session devient imprévisible :

```python
with scene.command_batch("ClarisseAdd - Shrink Wrap"):
    ...
```

## Journalisation

Deux destinations simultanées : la console de Clarisse (ce que l'artiste voit) et
un fichier à côté de la configuration (ce qui survit au crash).

`log.exception(contexte)` journalise la pile complète et affiche la dernière
ligne de l'erreur avec le chemin du fichier de log. C'est ce qui manquait le plus
aux outils d'origine : Clarisse avale les exceptions levées dans un callback GUI,
et sans ça un bouton cassé se contente de ne rien faire.

## Compatibilité

Clarisse 5.0 SP14 embarque **Python 3.7**. Le code de l'addon s'y tient : pas
d'opérateur morse, pas de f-strings `=`, pas de `match`. Les scripts de
développement (`tools/`) tournent avec n'importe quel Python 3 puisqu'ils
n'entrent jamais dans Clarisse.
