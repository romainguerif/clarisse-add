"""Declaration de tous les boutons de l'addon.

C'est la source de verite unique : l'installeur genere ``shelf.cfg`` a partir
d'ici, les stubs d'entree sont generes a partir d'ici, et la documentation liste
les outils a partir d'ici.  Ajouter un outil = ajouter une ligne dans ce
fichier et ecrire le module correspondant.

Un ``Tool`` pointe vers un module qui expose ``run()``.  Le stub place dans
``shelf.cfg`` ne fait qu'appeler ``clarisse_add.bootstrap.launch(id, ix)``.
"""

from .presets import catalog

# Prefixe commun a toutes les categories du shelf : elles se retrouvent ainsi
# groupees dans la barre d'onglets, entre les categories natives de Clarisse.
PREFIX = "ClarisseAdd"

CATEGORY_MAIN = PREFIX
CATEGORY_SCATTER = PREFIX + " Scatter"
CATEGORY_LIGHTS = PREFIX + " Lights"
CATEGORY_LOOKDEV = PREFIX + " Look-dev"
CATEGORY_SCENE = PREFIX + " Scene"
CATEGORY_BUILD = PREFIX + " Build"
CATEGORY_PRESETS = PREFIX + " Presets"
CATEGORY_OPTICS = PREFIX + " Optique"
CATEGORY_KIT = PREFIX + " Survival Kit"


class Tool(object):
    """Un bouton du shelf.

    Attributs :
        id           identifiant stable, sert de nom de fichier pour le stub
        title        libelle affiche sur le bouton
        description  infobulle
        module       module Python expose ``run()``
        category     categorie du shelf
        icon         nom d'icone dans ``assets/icons`` (sans extension)
        payload      argument optionnel passe a ``run()`` (id de preset, ...)
    """

    __slots__ = ("id", "title", "description", "module", "category", "icon", "payload")

    def __init__(self, id, title, description, module, category,
                 icon=None, payload=None):
        self.id = id
        self.title = title
        self.description = description
        self.module = module
        self.category = category
        self.icon = icon or id.replace(".", "_")
        self.payload = payload

    def __repr__(self):  # pragma: no cover - debug only
        return "<Tool %s '%s'>" % (self.id, self.title)


# ---------------------------------------------------------------------------
# Socle
# ---------------------------------------------------------------------------

MAIN = [
    Tool(
        "main.preset_browser",
        "Preset Browser",
        "Parcourt la bibliotheque de scenes .project et les fusionne dans le "
        "contexte de votre choix, avec leurs parametres.",
        "clarisse_add.tools.preset_browser",
        CATEGORY_MAIN,
    ),
    Tool(
        "main.reload",
        "Reload ClarisseAdd",
        "Recharge tout le code de l'addon sans redemarrer Clarisse. "
        "Indispensable pendant le developpement d'un outil.",
        "clarisse_add.tools.reload_addon",
        CATEGORY_MAIN,
    ),
    Tool(
        "main.about",
        "A propos / Logs",
        "Version de l'addon, chemins utilises, et ouverture du fichier de log.",
        "clarisse_add.tools.about",
        CATEGORY_MAIN,
    ),
]

# ---------------------------------------------------------------------------
# Optique : les modules C++ de l'addon
# ---------------------------------------------------------------------------

OPTICS = [
    Tool(
        "optics.camera",
        "Camera Bokeh",
        "Cree une camera dont le diaphragme a une forme reelle. La profondeur "
        "de champ est calculee par le moteur en echantillonnant l'ouverture : "
        "l'occlusion est juste, aucune carte de profondeur n'est necessaire.",
        "clarisse_add.tools.optics",
        CATEGORY_OPTICS,
        payload="camera",
    ),
    Tool(
        "optics.filter",
        "Filtre Bokeh",
        "Pose le filtre Bokeh sur les layers selectionnes. Diaphragme a lames, "
        "aberrations optiques, et aberration chromatique -- que la camera ne "
        "peut pas faire, le moteur n'etant pas spectral.",
        "clarisse_add.tools.optics",
        CATEGORY_OPTICS,
        payload="filter",
    ),
    Tool(
        "optics.chroma",
        "Filtre Aberration Chromatique",
        "Pose le filtre d'aberration chromatique sur les layers selectionnes. "
        "A utiliser apres un rendu fait avec la camera Bokeh : elle ne peut pas "
        "la produire, le moteur etant RVB et non spectral.",
        "clarisse_add.tools.optics",
        CATEGORY_OPTICS,
        payload="chroma",
    ),
    Tool(
        "optics.load",
        "Charger les modules C++",
        "Declare les classes natives sans relancer Clarisse. Normalement "
        "inutile -- l'installeur pose un script de demarrage qui s'en charge -- "
        "mais indispensable apres une recompilation.",
        "clarisse_add.tools.load_native",
        CATEGORY_OPTICS,
    ),
]

# ---------------------------------------------------------------------------
# Scatter et distribution
# ---------------------------------------------------------------------------

SCATTER = [
    Tool(
        "scatter.distribute",
        "Distribute",
        "Repartit les objets selectionnes selon une ligne, un carre, un "
        "rectangle ou un cercle, en unites ou relativement a leur bounding box.",
        "clarisse_add.tools.distribute",
        CATEGORY_SCATTER,
    ),
    Tool(
        "scatter.shrinkwrap",
        "Shrink Wrap",
        "Cree un polygrid deplace qui epouse la selection, utile pour generer "
        "une surface de scatter a partir d'une geometrie complexe.",
        "clarisse_add.tools.shrinkwrap",
        CATEGORY_SCATTER,
    ),
    Tool(
        "scatter.light_scatterer",
        "Light Scatterer",
        "Instancie une lumiere sur chaque point d'un nuage de points Alembic, "
        "en mappant les proprietes du nuage sur la couleur et l'intensite.",
        "clarisse_add.tools.light_scatterer",
        CATEGORY_SCATTER,
    ),
]

# ---------------------------------------------------------------------------
# Lumieres
# ---------------------------------------------------------------------------

LIGHTS = [
    Tool(
        "lights.manager",
        "Light Manager",
        "Panneau central de toutes les lumieres de la scene : intensite, "
        "couleur, exposition, groupes, LPE, isolation, filtrage par nom.",
        "clarisse_add.tools.light_manager",
        CATEGORY_LIGHTS,
    ),
    Tool(
        "lights.sun_sky",
        "Sun & Sky",
        "Cree un systeme soleil + ciel physique (OSL Nishita), le soleil etant "
        "pilote par des expressions liees a la position dans le ciel.",
        "clarisse_add.tools.sun_sky",
        CATEGORY_LIGHTS,
    ),
    Tool(
        "lights.lpe_setup",
        "LPE Setup",
        "Genere un jeu complet de Light Path Expressions (diffuse, specular, "
        "transmission, SSS, coat, direct/indirect) sur l'image courante.",
        "clarisse_add.tools.lpe_setup",
        CATEGORY_LIGHTS,
    ),
]

# ---------------------------------------------------------------------------
# Look-dev
# ---------------------------------------------------------------------------

LOOKDEV = [
    Tool(
        "lookdev.studio",
        "Lookdev Studio",
        "Environnement de lookdev commutable : charge un studio HDRI parmi la "
        "bibliotheque et y place la selection.",
        "clarisse_add.tools.lookdev_studio",
        CATEGORY_LOOKDEV,
    ),
    Tool(
        "lookdev.material_per_shading_group",
        "Material per Shading Group",
        "Cree un materiau et un contexte par shading group de la geometrie "
        "selectionnee, et les assigne.",
        "clarisse_add.tools.material_per_shading_group",
        CATEGORY_LOOKDEV,
    ),
    Tool(
        "lookdev.gradient_random",
        "Gradient Random",
        "Remplit un TextureGradient de cles aleatoires entre deux couleurs, "
        "pour varier les instances d'un scatter.",
        "clarisse_add.tools.gradient_random",
        CATEGORY_LOOKDEV,
    ),
]

# ---------------------------------------------------------------------------
# Assemblage BUiLDER
# ---------------------------------------------------------------------------

BUILD = [
    Tool(
        "build.template",
        "Scene de depart",
        "Recree le contexte de demarrage de Clarisse iFX -- camera, lumiere, "
        "raytracer et image deja branchee -- pour partir d'une scene qui rend, "
        "y compris en BUiLDER. Chaine d'assemblage en option.",
        "clarisse_add.tools.build_template",
        CATEGORY_BUILD,
    ),
]


# ---------------------------------------------------------------------------
# Hygiene de scene
# ---------------------------------------------------------------------------

SCENE = [
    Tool(
        "scene.audit",
        "Scene Audit",
        "Rapport sur la scene : fichiers manquants, materiaux orphelins, "
        "contextes vides, textures non references, geometries sans materiau.",
        "clarisse_add.tools.scene_audit",
        CATEGORY_SCENE,
    ),
    Tool(
        "scene.relink",
        "Relink Files",
        "Remplace un prefixe de chemin par un autre sur tous les fichiers "
        "references, avec previsualisation avant application.",
        "clarisse_add.tools.relink",
        CATEGORY_SCENE,
    ),
    Tool(
        "scene.cleanup",
        "Cleanup",
        "Supprime les contextes vides et, au choix, les objets non references "
        "par le rendu.",
        "clarisse_add.tools.cleanup",
        CATEGORY_SCENE,
    ),
    Tool(
        "scene.rename",
        "Batch Rename",
        "Renomme la selection : prefixe, suffixe, recherche/remplacement, "
        "numerotation, casse.",
        "clarisse_add.tools.batch_rename",
        CATEGORY_SCENE,
    ),
]


# ---------------------------------------------------------------------------
# Presets : un bouton par scene-outil de la bibliotheque
# ---------------------------------------------------------------------------


def preset_tools():
    """Un ``Tool`` par preset marque ``shelf: true`` dans le catalogue.

    Les presets restent tous accessibles via le Preset Browser ; seuls les plus
    utilises meritent leur propre bouton, sinon la categorie devient illisible.
    """
    tools = []
    for entry in catalog.shelf_entries():
        tools.append(
            Tool(
                "preset." + entry.id,
                entry.title,
                entry.description,
                "clarisse_add.tools.preset_runner",
                CATEGORY_PRESETS,
                icon="preset_" + entry.id,
                payload=entry.id,
            )
        )
    return tools


# ---------------------------------------------------------------------------
# Clarisse Survival Kit
# ---------------------------------------------------------------------------

#: (id, titre, module CSK, description).  Les modules sont ceux du paquet
#: vendorise ``clarisse_add.vendor.clarisse_survival_kit``.
KIT_ENTRIES = [
    ("import_asset", "Import Asset", "import_asset",
     "Importe un asset Megascans ou un jeu de textures generique dans un "
     "nouveau contexte, geometries et materiaux assignes automatiquement."),
    ("mix", "Mix Surfaces", "mix",
     "Melange plusieurs surfaces selectionnees avec une surface de "
     "recouvrement (poussiere, neige)."),
    ("add_to_mix", "Add Surface(s) to Mix", "add_to_mix",
     "Ajoute les surfaces selectionnees a un mix existant."),
    ("triplanar", "Textures to Triplanar", "triplanar",
     "Convertit les textures selectionnees en projection triplanaire."),
    ("replace", "Replace Surface", "replace",
     "Remplace les surfaces selectionnees et met a jour le mapping."),
    ("simplify", "Toggle Surface Complexity", "simplify",
     "Bascule temporairement la surface en diffus simple sans displacement, "
     "pour alleger la scene pendant le travail."),
    ("scatter", "Decimated Point Cloud", "scatter",
     "Genere un nuage de points decime depuis l'objet selectionne, avec "
     "plusieurs selecteurs de masquage."),
    ("moisten", "Moisten Surface", "moisten",
     "Ajoute une couche humide sur la surface selectionnee."),
    ("blend", "Quick Blend", "blend",
     "Cree un blend ou multiblend adapte au type des elements selectionnes."),
    ("mask", "Mask Blend Nodes", "mask",
     "Ajoute des selecteurs aux noeuds de blend selectionnes."),
    ("tint", "Tint Surface", "tint",
     "Teinte le diffus d'une surface avec une couleur personnalisee."),
    ("blur", "Blur Textures", "blur",
     "Floute les textures selectionnees."),
    ("stream_toggle", "(Un)Stream Textures", "stream_toggle",
     "Bascule entre TextureMapFile et TextureStreamedMapFile."),
    ("converter", "Convert Textures", "converter",
     "Convertit les textures selectionnees vers un autre format."),
    ("reconvert", "Reconvert Textures", "reconvert",
     "Re-scanne les dossiers sources et reconvertit ce qui a change."),
    ("import_ms_library", "Import Megascans Library", "import_ms_library",
     "Importe tout ou partie de la bibliotheque Megascans."),
    ("ms_bridge_gui", "Megascans Bridge", "ms_bridge_gui",
     "Demarre l'ecoute du bridge Megascans (necessite le Command Port actif)."),
    ("terrain", "Setup Heightmap", "terrain",
     "Importe une heightmap et la deplace sur un polygrid, avec proxy basse "
     "definition optionnel."),
    ("aces", "Toggle ACES / Clarisse", "aces",
     "Corrige l'espace colorimetrique de toutes les textures apres un "
     "changement de configuration de color management."),
]


def kit_tools():
    return [
        Tool(
            "kit." + tool_id,
            title,
            description,
            "clarisse_add.tools.kit_runner",
            CATEGORY_KIT,
            icon="kit_" + module_name,
            payload=module_name,
        )
        for tool_id, title, module_name, description in KIT_ENTRIES
    ]


# ---------------------------------------------------------------------------


_ALL = None


def all_tools():
    """Tous les outils, dans l'ordre d'apparition dans le shelf.

    Le resultat est mis en cache : le manifeste ne change pas en cours de
    session, et ``by_id`` est appele a chaque clic sur un bouton.  Le cache
    garantit aussi que deux appels renvoient les *memes* objets ``Tool``, ce
    qui permet de les comparer par identite.  Le bouton "Reload" vide
    ``sys.modules``, donc le cache avec.
    """
    global _ALL
    if _ALL is None:
        tools = []
        tools.extend(MAIN)
        tools.extend(OPTICS)
        tools.extend(SCATTER)
        tools.extend(LIGHTS)
        tools.extend(LOOKDEV)
        tools.extend(SCENE)
        tools.extend(BUILD)
        tools.extend(preset_tools())
        tools.extend(kit_tools())
        _ALL = tools
    return list(_ALL)


def invalidate():
    """Oublie le cache, par exemple apres avoir regenere le catalogue."""
    global _ALL
    _ALL = None


def by_id(tool_id):
    """Retrouve un outil par son identifiant, ``None`` s'il n'existe pas."""
    for tool in all_tools():
        if tool.id == tool_id:
            return tool
    return None


def categories():
    """Les categories, dans l'ordre, sans doublon."""
    seen = []
    for tool in all_tools():
        if tool.category not in seen:
            seen.append(tool.category)
    return seen
