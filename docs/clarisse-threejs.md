# Clarisse comme interface d'auteur pour three.js

Document de réflexion, issu d'une discussion du 2026-09-07. **Aucun code n'a été
écrit, aucune décision n'engage.**

C'est un projet **distinct de ClarisseAdd** — autre cible, autre durée de vie,
autre public. Il est rangé ici pour ne pas se perdre ; il aura son dépôt quand
il commencera.

---

## 1. D'où vient l'idée

### Le constat de marché

Romain : quasiment plus aucune commande de rendu image, presque tout en temps
réel, principalement **Unity**. Conviction que le web — WebGL, WebGPU — est
porteur, et que la consommation part vers le navigateur et le mobile.

Analyse honnête de ce constat, y compris là où je le tempère :

**Ce qui est solide.** WebGPU est arrivé sur les trois navigateurs. Ce n'est
plus une promesse. Le plafond monte réellement : compute shaders, buffers de
stockage, coût par draw call en baisse, ce qui débloque le rendu piloté GPU et
un vrai éclairage clustered.

**Ce que je tempère.** La contrainte qui limite la 3D web n'a jamais été la
puissance du shader — c'est **le poids de téléchargement et la mémoire**, et
WebGPU n'y change rien. L'écart avec le natif ne va donc pas se refermer, il va
se déplacer. Ce qui distingue une bonne scène web d'une mauvaise, ce n'est pas
le nombre de lumières, c'est le budget. Gérer un budget de look, c'est
exactement un métier de look-dev.

**Sur le marché lui-même.** L'agence « expérience web 3D » promet d'exploser
depuis dix ans : réelle, mais en dents de scie, dépendante des budgets
marketing, sous pression sur les prix. L'argent durable est plutôt dans le
**produit** que dans l'expérience — configurateurs, viewers embarqués,
e-commerce, jumeaux numériques.

**Sur les postes fixes.** Ils ne disparaissent pas, ils se spécialisent. C'est
la consommation qui a migré, pas la création. Ça ne change pas la conclusion.

### Le trou dans le marché

> *« de toute façon, ça n'existe pas vraiment des softs pour faire du lighting
> web »*

C'est le point de départ réel du projet, et il est juste. La raison pour
laquelle ça n'existe pas est instructive : **les gens qui savent éclairer et les
gens qui livrent de la 3D web sont deux populations qui ne se croisent
presque pas.** Les premiers viennent du VFX et ne touchent pas au navigateur ;
les seconds sont des développeurs front qui règlent des lumières en tapant des
nombres et en rechargeant la page. Personne dans l'intersection pour construire
l'outil.

Romain y est. C'est le meilleur argument du projet, bien meilleur que n'importe
quelle considération technique.

### Trois projets qu'il fallait séparer

L'idée est arrivée mélangée à deux autres. Le tri :

| Objectif | Coût | Verdict |
|---|---|---|
| Aller plus vite dans Clarisse — **moteur GPU** | énorme | le romantique. Il accélère la partie du travail qui rétrécit, et ne donne pas une ligne de web |
| Faire **sortir** le travail vers le web | modéré | **c'est ici** |
| Mieux **construire les mondes** — câbles, courbes procédurales | faible | oui, et ça sert des deux côtés de la migration |

Sur le moteur GPU : techniquement `Renderer` est dérivable, mais écrire un path
tracer GPU se compte en années d'équipe, et en intégrer un existant demande de
traduire toute la scène Clarisse vers la représentation de l'autre moteur —
instanciation massive comprise, c'est-à-dire précisément ce que Clarisse a de
plus singulier et de plus dur à traduire. Abandonné.

### Le cadre stratégique

Clarisse est mort — pas mourant. Pas de correctifs, pas de support d'OS futur.
Conséquence directe sur ce qui vaut le coup d'être construit :

> **Tout ce qui fait sortir la valeur de Clarisse vaut mieux que tout ce qui
> l'y enferme.**

`tropix` (`.project` → USD) est de loin la chose la plus stratégique déjà
écrite, parce que c'est la porte de sortie. Un moteur GPU aurait été la plus
enfermante.

---

## 2. Le principe : miroir, pas traduction

Le cœur du projet, et c'est l'intuition de Romain, pas la mienne.

**On ne convertit pas les objets Clarisse vers three.js. On fabrique des nodes
Clarisse qui *sont* les objets three.js**, avec exactement leurs paramètres,
dans exactement leurs unités.

Ce que ça dissout, et c'est spectaculaire :

- **Les conversions d'unités.** Un node `DirectionalLight` dont `intensity` est
  en lux parce que c'est ce qu'attend three.js n'a rien à convertir. La
  correspondance est exacte **par construction**, pas par calibration. Le
  casse-tête candela / lux / watts disparaît.
- **Le tone mapping.** Il ne pose problème que si l'on veut que Clarisse
  *montre juste*. Si le navigateur est la vérité, il n'y a plus d'écart à
  corriger.
- **La dérive.** Une traduction fuit toujours — il y a toujours un paramètre
  sans équivalent. Un miroir, non.

Corollaire à assumer : **le miroir oblige à choisir sa cible.** Une traduction
pourrait viser three.js et Unity ; un miroir non, leurs matériaux ne s'alignent
pas.

---

## 3. Ce que ça transforme

Le projet n'est pas « faire exporter Clarisse vers le web ». C'est faire de
**Clarisse une interface d'auteur pour three.js**.

```
   CLARISSE                     RELAIS                   NAVIGATEUR
   ────────                     ──────                   ──────────
   graphe de scene       ──▶   petit service     ──▶    le VRAI runtime
   nodes en miroir              externe                  three.js
   gizmos, scatter              WebSocket                = la sortie,
   editeur d'attributs                                     pas un apercu
        │                                                      │
        └──────────  transformations, lumieres, ───────────────┘
                     parametres : en direct

   maillages : pousses a la demande, pas en continu
```

Le partage :

- **Clarisse** apporte ce qu'il fait mieux qu'aucun éditeur web : un graphe de
  scène solide, un node graph, des gizmos, un éditeur d'attributs, de la mise en
  place, et surtout de l'instanciation à une échelle inatteignable dans un
  navigateur.
- **Le navigateur** est le moteur de rendu. Pas un aperçu : la sortie réelle,
  tout le temps.

Conséquence sur les matériaux : **Clarisse n'a pas besoin de savoir rendre un
`MeshPhysicalMaterial`.** Il a besoin de le *tenir* — ses paramètres, son
graphe, ses connexions. Le viewport de Clarisse devient un éditeur, pas un
aperçu.

### Séparer le lourd du léger

Le principe qui rend le direct faisable, appliqué à deux niveaux :

| Lourd, rare → poussé à la demande | Léger, constant → en direct |
|---|---|
| topologie des maillages | transformations |
| textures, HDRI | lumières (position, intensité, couleur, cône) |
| | paramètres de matériaux |
| | tone mapping, exposition |

Le rig d'éclairage complet tient en quelques centaines d'octets : on peut le
pousser à 60 Hz sans y penser. **On ne synchronise jamais la géométrie en
continu** — on pousse les maillages quand ils sont sales, les transformations
tout le temps. Résultat : déplacer un objet est instantané sans qu'un seul
sommet ne traverse le fil.

---

## 4. Les décisions prises

**La cible est three.js**, pas Unity. Raison : Unity **a** un éditeur, le manque
y est bien plus petit. three.js n'en a pas, et c'est là qu'on est seul.
*(Nuance à garder en tête : les commandes actuelles sont en Unity. On sert donc
d'abord la conviction, pas le carnet de commandes. Voir les questions ouvertes.)*

**Le chemin node (`three/webgpu`), pas le renderer classique.** Cet
embranchement s'est dissous en regardant de près : le `WebGPURenderer` a un
**repli WebGL2**, donc on n'abandonne pas le parc actuel en construisant pour le
futur. Deux raisons de partir là :

1. On ne perd rien aujourd'hui.
2. `MeshPhysicalNodeMaterial` porte les mêmes paramètres que la version
   classique — donc **ajouter le graphe TSL plus tard sera une extension, pas
   une réécriture.** En partant du renderer classique, l'ambition du graphe
   serait un second projet.

C'est aussi la direction que prend three.js.

**Pas de serveur dans Clarisse.** Son Python est en 3.7, et faire du réseau dans
un thread au milieu d'un DCC plante à la fermeture et se débogue pendant trois
jours. Clarisse pousse bêtement vers un **relais externe**, qui parle WebSocket
au navigateur. Clarisse reste bête et sûr ; le relais se redémarre sans rien
perdre.

**Le format est l'actif, pas le plugin.** Un « rig » défini proprement en JSON
rend les deux bouts interchangeables : Clarisse n'est que le premier client côté
auteur. Et comme tout est en miroir, ce format **est** à peu près le modèle
d'objet de three.js — donc l'export est presque gratuit et portable par nature.
C'est aussi ce qui sort du piège du logiciel mort : l'investissement ne meurt
pas avec Clarisse.

---

## 5. L'inventaire du miroir

Compté, parce que « mettre three.js en miroir » sonne énorme et ne l'est pas
pour une première version utilisable.

| Famille | Nodes | Contenu |
|---|---|---|
| **Lumières** | 6 | `Ambient`, `Hemisphere`, `Directional`, `Point`, `Spot`, `RectArea`. Plus les réglages d'ombre par lumière : taille de map, bias, normalBias, radius, frustum pour la directionnelle. |
| **Environnement** | 1 | HDRI, intensité, rotation, arrière-plan, brouillard. |
| **Matériaux** | 3 | `Physical` (le morceau : ~60 paramètres — base, transmission, clearcoat, sheen, iridescence, anisotropie, et tous les slots de textures avec leurs transformations d'UV), `Standard`, `Basic`. |
| **Rendu** | 1 | tone mapping (dont **AgX** — Romain travaille déjà en AgX côté Clarisse, l'alignement est gratuit), exposition, espace de sortie, type d'ombres. |
| **Caméra** | 1–2 | perspective, éventuellement orthographique. |

**Une douzaine de nodes** pour une v1 qui fait vraiment le travail. Deux à trois
fois le volume du bokeh, et beaucoup moins de difficulté : aucun algorithme,
que de la structure.

**TSL** est l'extension d'après, et elle est grande. Principe à tenir jusqu'au
bout : **ne pas traduire un graphe Clarisse arbitraire vers TSL** — c'est la
version qui fuit. Fabriquer des nodes Clarisse qui *sont* les nœuds TSL.
L'artiste construit dans le vocabulaire de la cible, et l'export est une
transcription, pas une interprétation.

*(Les faits three.js cités ici — repli WebGL2, unités physiques des lumières,
AgX, TSL — sont à revérifier contre la version courante au démarrage du
projet.)*

---

## 6. Les deux choses que personne n'a

**L'aperçu sur téléphone, en direct.** Le runtime tourne dans un navigateur, sur
le réseau. Donc on ouvre la même URL sur son téléphone et **il s'éclaire en
direct** pendant qu'on tire la lumière dans Clarisse. Ce qui est joli sur un
écran calibré est souvent illisible sur un OLED en plein soleil ; là, on le voit
pendant qu'on règle. Ça sort gratuitement de l'architecture, et c'est ce qu'il
faudrait montrer en premier à quelqu'un.

**Le scatter de Clarisse vers `InstancedMesh`.** Clarisse est probablement le
meilleur outil au monde pour semer des millions d'instances, et `InstancedMesh`
encaisse très bien côté three.js. Semer une forêt dans Clarisse et la voir
tourner dans un navigateur, avec les outils de dispersion de Clarisse pour la
régler — personne ne l'a. C'est peut-être plus différenciant que l'éclairage, et
ça découle de la même architecture.

---

## 7. Ce qui est difficile, honnêtement

- **Le miroir oblige à choisir la cible.** Servir aussi Unity demanderait un
  second jeu de nodes.
- **Le volume.** Une douzaine de nodes pour la v1, puis TSL. Ça se compte en
  mois, pas en week-ends.
- **Le viewport de Clarisse sera faux.** C'est le prix assumé du miroir : on ne
  travaille pas sans le navigateur ouvert à côté. Un mappage approximatif vers
  le matériau physique de Clarisse pourrait adoucir ça — confort, pas priorité.
- **Clarisse est mort.** Plus on construit dessus, plus l'exposition grandit. Le
  format d'échange est la contre-mesure, et le fait qu'il soit calqué sur three.js
  le rend portable par construction.

---

## 8. Ce qu'on ne fait pas tout de suite

- **Le bake de lightmaps** — plus tard, et probablement jamais côté Clarisse.
- **Traduire des graphes Clarisse arbitraires** — jamais. C'est le contraire du
  principe.
- **Unity** — pas maintenant. Le même flux nourrirait un adaptateur le jour où
  les commandes trancheront.

---

## 9. Par où commencer

La version la plus bête possible, et j'insiste sur bête. Mais avec le principe
du miroir, **ce n'est plus un jetable : c'est la première tranche verticale du
vrai produit.**

**Une lumière et un objet.** Un node `ThreeDirectionalLight` en miroir, la
géométrie poussée à la demande, la transformation en direct, une page three.js
en face. Au départ, même pas de socket : Clarisse écrit un JSON toutes les
100 ms, la page le relit. Une centaine de lignes.

Parce que la question à laquelle il faut répondre d'abord n'est pas « est-ce
faisable » — ça l'est — mais **« est-ce que le geste est bon ? »** Est-ce que
tirer une lumière dans Clarisse en regardant le navigateur donne envie de
continuer, ou est-ce que le va-et-vient entre deux fenêtres est pénible ? Ça se
sait en une journée, et aucune quantité de conception ne le dira.

Si le geste tient, tout le reste — le socket, le relais, les unités, le format
propre, les onze autres nodes — n'est que de la surface à couvrir, sans
inconnue.

---

## 10. Questions ouvertes

1. **Les livrables Unity actuels** : est-ce que Romain **pose les lumières** dans
   Unity, ou livre-t-il des assets et c'est leur développeur qui éclaire ? Si
   c'est lui qui éclaire, l'outil a un débouché client immédiat et pas seulement
   une valeur de conviction — et l'ordre three.js / Unity mérite d'être
   rediscuté.
2. **Le sens de la caméra.** Elle devrait probablement remonter du navigateur
   vers Clarisse plutôt que l'inverse : on cadre dans la cible, où le FOV et le
   ratio sont vrais, et on éclaire depuis Clarisse. À trancher à l'usage.
3. **Le scatter au-delà d'`InstancedMesh`.** Que devient un semis de plusieurs
   millions d'instances qu'un navigateur ne peut pas tenir ? Décimation,
   culling, LOD — question réelle, et c'est aussi là que l'outil peut se
   distinguer.
4. **Le nom et le dépôt.** Ce projet n'appartient pas à ClarisseAdd.
