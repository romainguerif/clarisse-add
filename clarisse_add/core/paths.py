"""Les chemins de l'addon et ceux de Clarisse sur la machine hote.

Tout est deduit de l'emplacement du paquet : aucun chemin absolu n'est ecrit en
dur nulle part, et deplacer le dossier de l'addon ne casse rien tant qu'on
relance l'installeur (qui ne fait que reecrire ``shelf.cfg``).
"""

import os
import re
import sys

# <racine>/clarisse_add/core/paths.py  ->  <racine>
ADDON_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACKAGE_ROOT = os.path.join(ADDON_ROOT, "clarisse_add")

ASSETS_DIR = os.path.join(ADDON_ROOT, "assets")
ICONS_DIR = os.path.join(ASSETS_DIR, "icons")
PRESETS_DIR = os.path.join(ASSETS_DIR, "presets")
ENTRY_DIR = os.path.join(PACKAGE_ROOT, "entry")
VENDOR_DIR = os.path.join(PACKAGE_ROOT, "vendor")

#: Versions de Clarisse que l'addon sait cibler, de la plus recente a la plus
#: ancienne.  Sert a choisir le dossier de configuration utilisateur.
SUPPORTED_VERSIONS = ("5.5", "5.0", "4.0")


def normalize(path):
    """Chemin en slash avant, comme Clarisse les ecrit dans ses fichiers cfg."""
    return path.replace("\\", "/")


def icon(name):
    """Chemin d'une icone de l'addon, ``""`` si elle n'existe pas.

    Un ``icon_filename`` vide est valide dans ``shelf.cfg`` : Clarisse affiche
    alors le titre du bouton.  Mieux vaut ca qu'un chemin mort.
    """
    for extension in (".png", ".svg"):
        candidate = os.path.join(ICONS_DIR, name + extension)
        if os.path.isfile(candidate):
            return normalize(candidate)
    return ""


def preset(*parts):
    """Chemin d'un asset de preset (``.project``, vignette, ...)."""
    return normalize(os.path.join(PRESETS_DIR, *parts))


# ---------------------------------------------------------------------------
# Cote Clarisse
# ---------------------------------------------------------------------------


def user_config_root():
    """Dossier de configuration utilisateur de Clarisse, selon l'OS."""
    if sys.platform.startswith("win"):
        appdata = os.getenv("APPDATA")
        if not appdata:
            return None
        return os.path.join(appdata, "Isotropix", "Clarisse")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Preferences/Isotropix/Clarisse")
    return os.path.expanduser("~/.isotropix/clarisse")


_VERSION_DIR_RE = re.compile(r"^\d+\.\d+$")


def installed_config_versions():
    """Versions de Clarisse ayant un dossier de config, plus recentes d'abord.

    Attention : la presence du dossier ne prouve pas que l'application est
    installee (Clarisse laisse ses preferences derriere lui apres une
    desinstallation).  L'installeur le signale plutot que de le deviner.
    """
    root = user_config_root()
    if not root or not os.path.isdir(root):
        return []
    found = [name for name in os.listdir(root)
             if _VERSION_DIR_RE.match(name) and os.path.isdir(os.path.join(root, name))]
    found.sort(key=lambda name: [int(part) for part in name.split(".")], reverse=True)
    return found


#: Emplacements ou Clarisse s'installe par defaut, par plateforme.
_APPLICATION_ROOTS = {
    "win": ["C:/Program Files/Isotropix", "C:/Program Files (x86)/Isotropix"],
    "darwin": ["/Applications/Isotropix", "/Applications"],
    "linux": ["/opt/Isotropix", "/usr/local/Isotropix"],
}

# "Clarisse 5.0 SP14", "Clarisse iFX 4.0 SP11", "Clarisse 5.5"
_APPLICATION_DIR_RE = re.compile(r"Clarisse(?:\s+iFX)?\s+(\d+\.\d+)", re.IGNORECASE)


def _application_roots():
    if sys.platform.startswith("win"):
        return _APPLICATION_ROOTS["win"]
    if sys.platform == "darwin":
        return _APPLICATION_ROOTS["darwin"]
    return _APPLICATION_ROOTS["linux"]


def installed_applications():
    """``{version: dossier}`` des applications Clarisse reellement installees.

    A distinguer de :func:`installed_config_versions` : Clarisse laisse ses
    preferences derriere lui quand on le desinstalle, et une version d'essai
    ouverte une fois suffit a creer un dossier de configuration.  Installer le
    shelf dans la configuration d'une version absente ecrit un fichier que rien
    ne lira jamais.
    """
    found = {}
    for root in _application_roots():
        if not os.path.isdir(root):
            continue
        try:
            entries = os.listdir(root)
        except OSError:
            continue
        for name in entries:
            match = _APPLICATION_DIR_RE.match(name)
            if not match:
                continue
            directory = os.path.join(root, name)
            if os.path.isdir(directory):
                # Une version deja trouvee ailleurs n'est pas ecrasee : le
                # premier emplacement de la liste fait foi.
                found.setdefault(match.group(1), directory)
    return found


def shelf_config(version):
    """Chemin du ``shelf.cfg`` utilisateur pour une version donnee."""
    root = user_config_root()
    if not root:
        return None
    return os.path.join(root, version, "shelf.cfg")


def clarisse_env_file(version):
    """Chemin du ``clarisse.env`` utilisateur pour une version donnee."""
    root = user_config_root()
    if not root:
        return None
    return os.path.join(root, version, "clarisse.env")


def log_file():
    """Fichier de log de l'addon, a cote de la configuration Clarisse."""
    root = user_config_root()
    if root and os.path.isdir(root):
        return os.path.join(root, "clarisse_add.log")
    return os.path.join(ADDON_ROOT, "clarisse_add.log")
