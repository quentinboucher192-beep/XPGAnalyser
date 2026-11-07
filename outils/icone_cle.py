#!/usr/bin/env python3
# =============================================================================
#  outils/icone_cle.py - 1.35.1 : l'icone des programmes de l'EDITEUR (XPG-License-Manager-Admin.exe et la console
#  XPG-License-Admin.exe) : une cle blanche sur la tuile bleu-turquoise de l'installateur (l'etape Licence,
#  outils/installateur_images.py : tuile_licence), fond transparent.
# -----------------------------------------------------------------------------
#  python3 outils/icone_cle.py   ->  resources/logo/licences_admin.ico (16 a 256) et licences_admin_256.png
#  Il faut Pillow.
# =============================================================================
import os
from PIL import Image, ImageDraw

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SORTIE = os.path.join(RACINE, "resources", "logo")
BLEU_FONCE = (30, 78, 140)
TURQUOISE_FONCE = (18, 96, 90)
BLANC = (255, 255, 255, 255)
G = 1024   # dessine en grand, puis reduit (bords lisses)


def melange(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def icone():
    fond = Image.new("RGB", (G, G))
    px = fond.load()
    for x in range(G):
        c = melange(BLEU_FONCE, TURQUOISE_FONCE, x / (G - 1))
        for y in range(G):
            px[x, y] = c
    im = Image.new("RGBA", (G, G), (0, 0, 0, 0))
    masque = Image.new("L", (G, G))
    ImageDraw.Draw(masque).rounded_rectangle((0, 0, G - 1, G - 1), radius=int(G * 0.22), fill=255)
    im.paste(fond.convert("RGBA"), (0, 0), masque)
    cle = Image.new("RGBA", (G, G), (0, 0, 0, 0))
    d = ImageDraw.Draw(cle)
    # l'anneau (la tete de la cle), la tige et ses deux dents, a l'horizontale ; un peu plus epais qu'a l'ecran de
    # l'installateur (l'icone se voit aussi en 16 x 16)
    cx, cy, r = G * 0.33, G * 0.50, G * 0.19
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=BLANC)
    rt = r * 0.42
    d.ellipse((cx - rt, cy - rt, cx + rt, cy + rt), fill=(0, 0, 0, 0))
    e = G * 0.075
    d.rectangle((cx + r * 0.85, cy - e, G * 0.82, cy + e), fill=BLANC)
    d.rectangle((G * 0.62, cy, G * 0.70, cy + G * 0.17), fill=BLANC)
    d.rectangle((G * 0.74, cy, G * 0.82, cy + G * 0.12), fill=BLANC)
    cle = cle.rotate(-35, resample=Image.Resampling.BICUBIC, center=(G / 2, G / 2))
    # la cle ne deborde pas de la tuile
    im.alpha_composite(Image.composite(cle, Image.new("RGBA", (G, G), (0, 0, 0, 0)), masque))
    return im


def main():
    os.makedirs(SORTIE, exist_ok=True)
    grand = icone()
    tailles = [16, 24, 32, 48, 64, 128, 256]
    images = [grand.resize((t, t), Image.Resampling.LANCZOS) for t in tailles]
    ico = os.path.join(SORTIE, "licences_admin.ico")
    images[-1].save(ico, format="ICO", sizes=[(t, t) for t in tailles], append_images=images[:-1])
    images[-1].save(os.path.join(SORTIE, "licences_admin_256.png"))
    print("ecrit :", ico, "(" + ", ".join(str(t) for t in tailles) + ")")


if __name__ == "__main__":
    main()
