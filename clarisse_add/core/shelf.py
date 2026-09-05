"""Installation de l'addon dans le shelf de Clarisse.

Le shelf utilisateur est un fichier ``shelf.cfg`` au meme format que les
``.project`` : on le relit donc avec :mod:`clarisse_add.core.project_file`, ce
qui donne les **plages de lignes** de chaque categorie, et on retouche le
fichier par tranches de lignes.

C'est volontairement different de ce que fait le Clarisse Survival Kit, qui
reecrit le fichier a coups d'expressions regulieres sur des niveaux
d'indentation precis.  Cette approche casse des qu'un bloc bouge, et c'est
exactement ce qui s'est produit sur cette machine : la moitie des boutons du kit
pointe vers un ``site-packages`` de Python 3.10 qui n'existe pas.  Ici, on
n'ecrit jamais que nos propres categories, et le reste du fichier ressort octet
pour octet identique.
"""

import io
import os
import shutil
import time

from . import log, paths
from . import project_file

#: Slot du shelf ou l'addon s'installe. Clarisse en propose huit (0-7) ;
#: le 0 est celui qui est affiche par defaut.
DEFAULT_SLOT = 0

_HEADER = "#Isotropix_Serial_Version 1.2"

_EMPTY_SHELF = """#Isotropix_Serial_Version 1.2

shelf {
    slot_selected 0
    category_selected "%s"
    show_toolbar yes
    style 0
    view_mode 0
    slot 0 {
    }
}
"""


# ---------------------------------------------------------------------------
# Stubs d'entree
# ---------------------------------------------------------------------------

_ENTRY_TEMPLATE = '''"""Point d'entree shelf : %(title)s

Fichier genere par clarisse_add.core.shelf -- ne pas editer a la main, il est
reecrit a chaque installation.  La logique est dans %(module)s.
"""

import os
import sys

_ADDON_ROOT = %(root)r
if _ADDON_ROOT not in sys.path:
    sys.path.insert(0, _ADDON_ROOT)

import clarisse_add.bootstrap as _bootstrap

_bootstrap.launch(%(tool_id)r, ix)
'''


def entry_filename(tool):
    """Chemin du stub lance par le shelf pour cet outil."""
    return os.path.join(paths.ENTRY_DIR, tool.id.replace(".", "_") + ".py")


def write_entry_scripts(tools):
    """(Re)genere un stub par outil et renvoie le nombre de fichiers ecrits.

    Clarisse execute le fichier designe par ``script_filename`` avec ``ix``
    dans ses globales ; le stub ne fait donc que rendre l'addon importable et
    deleguer.  Toute la logique reste dans le paquet, rechargeable a chaud.
    """
    if not os.path.isdir(paths.ENTRY_DIR):
        os.makedirs(paths.ENTRY_DIR)

    init_file = os.path.join(paths.ENTRY_DIR, "__init__.py")
    if not os.path.isfile(init_file):
        with io.open(init_file, "w", encoding="utf-8") as handle:
            handle.write(u"")

    written = 0
    for tool in tools:
        content = _ENTRY_TEMPLATE % {
            "title": tool.title,
            "module": tool.module,
            "root": paths.ADDON_ROOT,
            "tool_id": tool.id,
        }
        target = entry_filename(tool)
        existing = None
        if os.path.isfile(target):
            with io.open(target, "r", encoding="utf-8") as handle:
                existing = handle.read()
        if existing == content:
            continue
        with io.open(target, "w", encoding="utf-8") as handle:
            handle.write(content)
        written += 1
    return written


def prune_entry_scripts(tools):
    """Supprime les stubs d'outils qui n'existent plus dans le manifeste."""
    if not os.path.isdir(paths.ENTRY_DIR):
        return 0
    keep = {os.path.basename(entry_filename(tool)) for tool in tools}
    keep.add("__init__.py")
    removed = 0
    for name in os.listdir(paths.ENTRY_DIR):
        if not name.endswith(".py") or name in keep:
            continue
        try:
            os.remove(os.path.join(paths.ENTRY_DIR, name))
            removed += 1
        except OSError:
            log.warning("Stub non supprime : %s" % name)
    return removed


# ---------------------------------------------------------------------------
# Generation du bloc de configuration
# ---------------------------------------------------------------------------


def _quote(text):
    """Echappe une valeur pour le format Isotropix."""
    return '"%s"' % str(text).replace("\\", "\\\\").replace('"', '\\"')


def render_categories(tools, indent="        "):
    """Le texte des categories de l'addon, pret a etre insere dans un slot."""
    order = []
    grouped = {}
    for tool in tools:
        if tool.category not in grouped:
            grouped[tool.category] = []
            order.append(tool.category)
        grouped[tool.category].append(tool)

    item_indent = indent + "    "
    field_indent = item_indent + "    "

    lines = []
    for category in order:
        lines.append("%scategory %s {" % (indent, _quote(category)))
        for tool in grouped[category]:
            lines.append("%sshelf_item {" % item_indent)
            lines.append("%stitle %s" % (field_indent, _quote(tool.title)))
            description = " ".join(tool.description.split())
            lines.append("%sdescription %s" % (field_indent, _quote(description)))
            lines.append("%sscript_filename %s"
                         % (field_indent, _quote(paths.normalize(entry_filename(tool)))))
            icon = paths.icon(tool.icon)
            lines.append("%sicon_filename %s" % (field_indent, _quote(icon)))
            lines.append("%s}" % item_indent)
        lines.append("%s}" % indent)
    return lines


# ---------------------------------------------------------------------------
# Lecture / ecriture du shelf utilisateur
# ---------------------------------------------------------------------------


def _read_lines(path):
    with io.open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read().splitlines()


def _shelf_node(text):
    """Le noeud ``shelf`` du fichier, ou ``None`` si le format est inattendu."""
    parsed = project_file.parse_string(text)
    for child in parsed.root.children:
        if child.class_name == "shelf":
            return child
    return None


def _slot_node(shelf, slot=DEFAULT_SLOT):
    for child in shelf.children:
        if child.class_name == "slot" and str(child.label) == str(slot):
            return child
    return None


def find_categories(text, prefix):
    """Plages de lignes ``(debut, fin)`` des categories dont le nom commence
    par ``prefix``.  Les bornes sont 1-indexees et inclusives."""
    shelf = _shelf_node(text)
    if shelf is None:
        return []
    ranges = []
    for slot in shelf.children:
        if slot.class_name != "slot":
            continue
        for category in slot.children:
            if category.class_name != "category":
                continue
            if category.label and str(category.label).startswith(prefix):
                ranges.append((category.line, category.end_line))
    return ranges


def backup(path):
    """Copie horodatee du fichier, renvoyee sous forme de chemin."""
    stamp = time.strftime("%Y%m%d-%H%M%S")
    target = "%s.%s.bak" % (path, stamp)
    shutil.copyfile(path, target)
    return target


def install(tools, shelf_path, prefix, slot=DEFAULT_SLOT, select_category=None):
    """Ecrit les categories de l'addon dans ``shelf_path``.

    Les categories deja presentes sous ce prefixe sont remplacees ; tout le
    reste du fichier est conserve tel quel.  Renvoie un dictionnaire de
    compte-rendu.
    """
    report = {"shelf": shelf_path, "backup": None, "created": False,
              "replaced": 0, "categories": 0, "items": len(tools)}

    directory = os.path.dirname(shelf_path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)

    if not os.path.isfile(shelf_path):
        with io.open(shelf_path, "w", encoding="utf-8") as handle:
            handle.write(_EMPTY_SHELF % (select_category or prefix))
        report["created"] = True
    else:
        report["backup"] = backup(shelf_path)

    with io.open(shelf_path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    lines = text.splitlines()

    shelf = _shelf_node(text)
    if shelf is None:
        raise ValueError(
            "%s ne contient pas de bloc 'shelf' : fichier corrompu ou format "
            "inconnu. Sauvegarde conservee dans %s" % (shelf_path, report["backup"])
        )

    obsolete = find_categories(text, prefix)
    report["replaced"] = len(obsolete)
    dropped = set()
    for start, end in obsolete:
        dropped.update(range(start, end + 1))

    target_slot = _slot_node(shelf, slot)
    block = render_categories(tools)
    report["categories"] = sum(1 for line in block if line.lstrip().startswith("category "))

    output = []
    if target_slot is not None:
        # On insere juste avant l'accolade fermante du slot existant.
        insert_before = target_slot.end_line
        for number, line in enumerate(lines, start=1):
            if number in dropped:
                continue
            if number == insert_before:
                output.extend(block)
            output.append(line)
    else:
        # Pas de slot : on en cree un juste avant la fermeture du bloc shelf.
        insert_before = shelf.end_line
        for number, line in enumerate(lines, start=1):
            if number in dropped:
                continue
            if number == insert_before:
                output.append("    slot %d {" % slot)
                output.extend(block)
                output.append("    }")
            output.append(line)

    text_out = "\n".join(output)
    if select_category:
        text_out = _set_selected_category(text_out, select_category)
    if not text_out.endswith("\n"):
        text_out += "\n"

    with io.open(shelf_path, "w", encoding="utf-8") as handle:
        handle.write(text_out)

    log.info("Shelf mis a jour : %d boutons dans %d categories (%s)"
             % (report["items"], report["categories"], shelf_path))
    return report


def uninstall(shelf_path, prefix):
    """Retire les categories de l'addon du shelf. Renvoie le nombre retire."""
    if not os.path.isfile(shelf_path):
        return 0
    with io.open(shelf_path, "r", encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    obsolete = find_categories(text, prefix)
    if not obsolete:
        return 0
    backup(shelf_path)
    dropped = set()
    for start, end in obsolete:
        dropped.update(range(start, end + 1))
    lines = [line for number, line in enumerate(text.splitlines(), start=1)
             if number not in dropped]
    with io.open(shelf_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return len(obsolete)


def _set_selected_category(text, category):
    """Fait de ``category`` l'onglet actif au prochain demarrage."""
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if line.strip().startswith("category_selected "):
            indent = line[: len(line) - len(line.lstrip())]
            lines[index] = "%scategory_selected %s" % (indent, _quote(category))
            break
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Enregistrement a chaud, sans toucher au fichier
# ---------------------------------------------------------------------------


def register_runtime(ix, tools, slot=DEFAULT_SLOT):
    """Ajoute les boutons dans la session en cours, sans ecrire sur disque.

    ``AppShelf::add_item`` est expose par les bindings Python ; les elements
    ajoutes ici vivent le temps de la session.  C'est ce qui permet au bouton
    "Reload" de faire reapparaitre un outil qu'on vient d'ajouter au manifeste,
    sans redemarrer Clarisse ni reinstaller le shelf.

    Renvoie le nombre de boutons ajoutes, ou ``-1`` si l'API n'est pas
    disponible sur cette version.
    """
    try:
        shelf = ix.application.get_shelf()
    except Exception:
        log.debug("AppShelf indisponible : enregistrement a chaud ignore")
        return -1

    added = 0
    for tool in tools:
        try:
            ok = shelf.add_item(
                slot,
                tool.category,
                tool.title,
                " ".join(tool.description.split()),
                paths.normalize(entry_filename(tool)),
                paths.icon(tool.icon),
            )
        except Exception:
            log.exception("Ajout a chaud du bouton '%s'" % tool.title)
            continue
        if ok:
            added += 1
    return added
