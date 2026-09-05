"""Genere les icones manquantes du shelf.

Script de developpement, a lancer avec un Python disposant de Pillow :

    python tools/build_icons.py

Clarisse affiche le titre du bouton quand l'icone manque, ce qui est lisible
mais donne une barre d'outils tres large et sans reperes visuels.  Plutot que
de dessiner quarante icones a la main, on en genere une par outil : une pastille
de couleur propre a la categorie, avec deux ou trois lettres tirees du titre.
Ce n'est pas de l'illustration, c'est un repere de couleur et de forme -- ce qui
suffit a retrouver un bouton dans une categorie.

Les icones deja presentes (celles reprises du Survival Kit, du Light Manager,
du Lookdev Studio) ne sont jamais ecrasees.
"""

from __future__ import print_function

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - dependance de developpement
    print("Pillow est requis : python -m pip install Pillow")
    sys.exit(1)

from clarisse_add import manifest  # noqa: E402
from clarisse_add.core import paths  # noqa: E402

SIZE = 64          # Clarisse redimensionne ; on part large pour rester net
RADIUS = 12
MARGIN = 3

#: Une couleur par categorie, assez saturee pour rester lisible sur le fond
#: sombre de l'interface de Clarisse.
CATEGORY_COLORS = {
    manifest.CATEGORY_MAIN: (216, 122, 58),
    manifest.CATEGORY_SCATTER: (92, 158, 96),
    manifest.CATEGORY_LIGHTS: (218, 180, 62),
    manifest.CATEGORY_LOOKDEV: (98, 140, 208),
    manifest.CATEGORY_SCENE: (150, 110, 190),
    manifest.CATEGORY_PRESETS: (200, 92, 106),
    manifest.CATEGORY_KIT: (108, 168, 176),
}
FALLBACK_COLOR = (130, 130, 130)

FONT_CANDIDATES = [
    "C:/Windows/Fonts/seguisb.ttf",
    "C:/Windows/Fonts/segoeuib.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]

_WORD_RE = re.compile(r"[A-Za-z0-9]+")


def initials(title, maximum=3):
    """Deux ou trois lettres representatives du titre.

    Un titre en plusieurs mots donne ses initiales ("Light Manager" -> "LM") ;
    un mot seul donne ses premieres lettres ("Distribute" -> "Di").
    """
    words = _WORD_RE.findall(title)
    if not words:
        return "?"
    if len(words) == 1:
        return words[0][:2].capitalize()
    letters = "".join(word[0] for word in words if word[0].isalpha())
    return letters[:maximum].upper() or words[0][:2].capitalize()


def load_font(size):
    for candidate in FONT_CANDIDATES:
        if os.path.isfile(candidate):
            try:
                return ImageFont.truetype(candidate, size)
            except (OSError, IOError):
                continue
    return ImageFont.load_default()


def draw_icon(text, color):
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    box = [MARGIN, MARGIN, SIZE - MARGIN - 1, SIZE - MARGIN - 1]
    draw.rounded_rectangle(box, radius=RADIUS, fill=color + (235,))
    # Un liseré clair en haut donne du relief sans ombre portee, qui passerait
    # mal a la taille reelle du bouton.
    draw.rounded_rectangle(box, radius=RADIUS, outline=(255, 255, 255, 60), width=2)

    font_size = 30 if len(text) <= 2 else 23
    font = load_font(font_size)
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    position = ((SIZE - (right - left)) / 2 - left,
                (SIZE - (bottom - top)) / 2 - top - 1)
    draw.text(position, text, font=font, fill=(20, 20, 22, 255))
    return image


def main():
    if not os.path.isdir(paths.ICONS_DIR):
        os.makedirs(paths.ICONS_DIR)

    created = kept = 0
    for tool in manifest.all_tools():
        target = os.path.join(paths.ICONS_DIR, tool.icon + ".png")
        if os.path.isfile(target):
            kept += 1
            continue
        color = CATEGORY_COLORS.get(tool.category, FALLBACK_COLOR)
        draw_icon(initials(tool.title), color).save(target)
        created += 1

    print("%d icone(s) generee(s), %d conservee(s) dans %s"
          % (created, kept, paths.ICONS_DIR))
    return 0


if __name__ == "__main__":
    sys.exit(main())
