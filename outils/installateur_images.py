#!/usr/bin/env python3
# =============================================================================
#  outils/installateur_images.py - 1.12.27 : les images de l'installateur
# -----------------------------------------------------------------------------
#  Fabrique installateur/images/ui/*.bmp (chaque image en 1x et en 2x, pour les
#  ecrans a 200 %) : le bandeau d'accueil, celui de la fin, le logo et les
#  marques des etapes de la colonne de gauche, l'icone de chaque etape, le
#  degrade, et l'animation de la copie.
#
#  L'animation vient d'un GIF : installateur/images/animation.gif. Pour en
#  changer, remplace ce fichier par le tien (n'importe quel GIF anime) et
#  relance ce script : il le decoupe en une bande d'images (animation.bmp,
#  animation-2x.bmp) et ecrit animation.ini (nombre d'images, delai, taille),
#  que l'installateur lit a la compilation. --gif-par-defaut refait le GIF livre.
#
#  Les BMP sont gardes dans le depot : construire l'installateur n'a besoin ni
#  de Python ni de Pillow. Ce script ne sert qu'a les refaire.
#
#    python3 outils/installateur_images.py [--gif-par-defaut]
#
#  Les icones des etapes viennent de la police Material Icons (Apache 2.0) :
#  --icones /chemin/MaterialIcons-Regular.ttf (sans elle, les icones gardent
#  leur version du depot).
# =============================================================================
import argparse
import configparser
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageSequence

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INST = os.path.join(ROOT, "installateur")
UI = os.path.join(INST, "images", "ui")
LOGOS = os.path.join(ROOT, "resources", "logo")

# La charte : les couleurs des deux logos (API : bleu, IHM : turquoise, le jaune des deux).
MARINE = (13, 31, 58)          # la colonne de gauche
MARINE_CLAIR = (24, 52, 92)
BLEU = (59, 130, 246)
BLEU_FONCE = (30, 78, 140)
TURQUOISE = (45, 196, 182)
TURQUOISE_FONCE = (18, 96, 90)
AMBRE = (245, 197, 66)
GRIS_ETEINT = (74, 96, 128)
BLANC = (255, 255, 255)
CARTE = (243, 246, 250)        # le fond des cartes de l'accueil
PISTE = (227, 233, 242)        # le cable de l'animation

SS = 4  # le sur-echantillonnage des petites images (bords lisses)


def melange(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def logo(nom, taille):
    im = Image.open(os.path.join(LOGOS, nom + "_512.png")).convert("RGBA")
    return im.resize((taille, taille), Image.Resampling.LANCZOS)


def sur_fond(rgba, fond):
    base = Image.new("RGBA", rgba.size, fond + (255,))
    base.alpha_composite(rgba)
    return base.convert("RGB")


def enregistrer(im, nom):
    os.makedirs(UI, exist_ok=True)
    im.convert("RGB").save(os.path.join(UI, nom + ".bmp"))


def deux_tailles(nom, fabrique, l, h):
    """fabrique(echelle) -> image (l*echelle, h*echelle) ; enregistre nom.bmp et nom-2x.bmp."""
    for e, suffixe in ((1, ""), (2, "-2x")):
        im = fabrique(e)
        assert im.size == (l * e, h * e), (nom, im.size)
        enregistrer(im, nom + suffixe)


def degrade_horizontal(l, h, a, b):
    im = Image.new("RGB", (l, h))
    px = im.load()
    for x in range(l):
        c = melange(a, b, x / max(1, l - 1))
        for y in range(h):
            px[x, y] = c
    return im


# ---------------------------------------------------------------- les marques ----
def marque(etat, e):
    t = 20 * e
    g = t * SS
    im = Image.new("RGB", (g, g), MARINE)
    d = ImageDraw.Draw(im)
    m = int(1.2 * e * SS)
    if etat == "fait":
        d.ellipse((m, m, g - m, g - m), fill=TURQUOISE)
        w = int(2.2 * e * SS)
        pts = [(g * 0.30, g * 0.52), (g * 0.44, g * 0.66), (g * 0.71, g * 0.37)]
        d.line(pts, fill=MARINE, width=w, joint="curve")
        for p in (pts[0], pts[-1]):
            d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=MARINE)
    elif etat == "encours":
        d.ellipse((m, m, g - m, g - m), fill=AMBRE)
        r = g * 0.17
        d.ellipse((g / 2 - r, g / 2 - r, g / 2 + r, g / 2 + r), fill=MARINE)
    else:
        w = int(1.6 * e * SS)
        d.ellipse((m, m, g - m, g - m), outline=GRIS_ETEINT, width=w)
    return im.resize((t, t), Image.Resampling.LANCZOS)


# ------------------------------------------------------------ logo de colonne ----
def logo_colonne(e):
    t = 40 * e
    im = Image.new("RGBA", (88 * e, 40 * e), MARINE + (255,))
    im.alpha_composite(logo("api_studio", t), (0, 0))
    im.alpha_composite(logo("ihm_studio", t), (48 * e, 0))
    return im.convert("RGB")


def logo_carte(nom):
    def f(e):
        return sur_fond(logo(nom, 40 * e), CARTE)
    return f


# ------------------------------------------------------ le decor de la colonne ----
def decor(e):
    """Des pistes de circuit, discretes, en bas de la colonne."""
    l, h = 200 * e, 150 * e
    g = SS
    im = Image.new("RGB", (l * g, h * g), MARINE)
    d = ImageDraw.Draw(im)
    u = e * g
    pistes = [
        [(0, 120), (40, 120), (60, 100), (120, 100), (140, 80), (200, 80)],
        [(0, 136), (70, 136), (86, 120), (150, 120), (170, 140), (200, 140)],
        [(20, 150), (20, 112), (34, 98), (34, 60)],
        [(96, 150), (96, 128), (110, 114), (176, 114)],
        [(150, 60), (150, 92), (166, 108), (200, 108)],
        [(56, 40), (56, 78), (74, 96)],
    ]
    for i, p in enumerate(pistes):
        c = melange(MARINE, TURQUOISE if i % 2 else BLEU, 0.28)
        d.line([(x * u, y * u) for x, y in p], fill=c, width=int(1.6 * u), joint="curve")
        for (x, y) in (p[0], p[-1]):
            r = 3.2 * u
            d.ellipse((x * u - r, y * u - r, x * u + r, y * u + r), outline=c, width=int(1.4 * u), fill=MARINE)
    # un fondu vers le haut : le decor sort de la colonne sans bord
    im = im.resize((l, h), Image.Resampling.LANCZOS)
    fond = Image.new("RGB", (l, h), MARINE)
    masque = Image.new("L", (l, h))
    pm = masque.load()
    for y in range(h):
        a = int(255 * min(1.0, max(0.0, (y / h - 0.05) / 0.55)))
        for x in range(l):
            pm[x, y] = a
    return Image.composite(im, fond, masque)


# ------------------------------------------------------------- les bandeaux ----
def fond_bandeau(l, h):
    im = degrade_horizontal(l, h, (19, 49, 92), (16, 98, 92))
    # un voile plus clair en haut a gauche
    voile = Image.new("L", (l, h))
    d = ImageDraw.Draw(voile)
    d.ellipse((-l * 0.3, -h * 1.4, l * 0.55, h * 0.9), fill=70)
    voile = voile.filter(ImageFilter.GaussianBlur(h * 0.35))
    im = Image.composite(Image.new("RGB", (l, h), (60, 120, 200)), im, voile)
    return im


def reseau(d, l, h, e, graine):
    """Des noeuds relies (un reseau d'equipements), tres discrets."""
    import random
    r = random.Random(graine)
    pts = [(r.uniform(0.02, 0.62) * l, r.uniform(0.08, 0.92) * h) for _ in range(16)]
    for i, a in enumerate(pts):
        for b in pts[i + 1:]:
            if math.dist(a, b) < 0.22 * l:
                d.line([a, b], fill=(255, 255, 255, 34), width=max(1, int(1.2 * e)))
    for (x, y) in pts:
        rr = r.choice((2.2, 3.0, 4.0)) * e
        d.ellipse((x - rr, y - rr, x + rr, y + rr), fill=(255, 255, 255, 70))


def ombre(rgba, rayon, opacite=110):
    a = rgba.getchannel("A")
    o = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    o.putalpha(a.point(lambda v: v * opacite // 255))
    return o.filter(ImageFilter.GaussianBlur(rayon))


def bandeau_accueil(e):
    l, h = 560 * e, 150 * e
    im = fond_bandeau(l, h).convert("RGBA")
    calque = Image.new("RGBA", (l, h), (0, 0, 0, 0))
    reseau(ImageDraw.Draw(calque), l, h, e, 7)
    im.alpha_composite(calque)
    t = 92 * e
    for nom, x, y in (("api_studio", l - 236 * e, 26 * e), ("ihm_studio", l - 124 * e, 32 * e)):
        lg = logo(nom, t)
        marge = 16 * e
        cadre = Image.new("RGBA", (t + 2 * marge, t + 2 * marge), (0, 0, 0, 0))
        cadre.alpha_composite(lg, (marge, marge))
        im.alpha_composite(ombre(cadre, 7 * e), (x - marge, y - marge + 5 * e))
        im.alpha_composite(lg, (x, y))
    # le lien entre les deux : trois points
    d = ImageDraw.Draw(im)
    for i, c in enumerate((BLEU, melange(BLEU, TURQUOISE, 0.5), TURQUOISE)):
        x = l - 142 * e + i * 10 * e
        y = 26 * e + t // 2 + 3 * e
        rr = 3 * e
        d.ellipse((x - rr, y - rr, x + rr, y + rr), fill=c + (255,))
    return im.convert("RGB")


def bandeau_fin(e):
    l, h = 560 * e, 150 * e
    im = fond_bandeau(l, h).convert("RGBA")
    calque = Image.new("RGBA", (l, h), (0, 0, 0, 0))
    reseau(ImageDraw.Draw(calque), l, h, e, 11)
    im.alpha_composite(calque)
    # la coche, dans un cercle, au centre-droit
    g = SS
    t = 104 * e
    c = Image.new("RGBA", (t * g, t * g), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    d.ellipse((0, 0, t * g - 1, t * g - 1), fill=(255, 255, 255, 40))
    m = 12 * e * g
    d.ellipse((m, m, t * g - m, t * g - m), fill=TURQUOISE + (255,))
    w = int(9 * e * g)
    T = t * g
    pts = [(T * 0.32, T * 0.52), (T * 0.45, T * 0.65), (T * 0.70, T * 0.38)]
    d.line(pts, fill=BLANC + (255,), width=w, joint="curve")
    for p in (pts[0], pts[-1]):
        d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=BLANC + (255,))
    c = c.resize((t, t), Image.Resampling.LANCZOS)
    x, y = l - 212 * e, (h - t) // 2
    im.alpha_composite(ombre(c, 8 * e, 90), (x, y + 5 * e))
    im.alpha_composite(c, (x, y))
    # des etincelles
    d = ImageDraw.Draw(im)
    for (sx, sy, s, col) in ((l - 236 * e, 28 * e, 7, AMBRE), (l - 92 * e, 34 * e, 5, BLANC), (l - 70 * e, 108 * e, 8, AMBRE),
                             (l - 250 * e, 118 * e, 5, BLANC), (l - 120 * e, 128 * e, 4, AMBRE)):
        s = s * e
        d.polygon([(sx, sy - s), (sx + s * 0.28, sy - s * 0.28), (sx + s, sy), (sx + s * 0.28, sy + s * 0.28),
                   (sx, sy + s), (sx - s * 0.28, sy + s * 0.28), (sx - s, sy), (sx - s * 0.28, sy - s * 0.28)], fill=col + (255,))
    return im.convert("RGB")


# ------------------------------------------------------- les icones d'etapes ----
ICONES = {   # l'etape -> le nom de l'icone Material
    "bienvenue": "waving_hand",
    "emplacement": "folder_open",
    "donnees": "storage",
    "bibliotheque": "library_books",
    "raccourcis": "dashboard_customize",
    "recapitulatif": "checklist",
    "installation": "install_desktop",
    "termine": "task_alt",
}


def icone(police, code):
    def f(e):
        t = 44 * e
        g = t * SS
        im = Image.new("RGBA", (g, g), BLANC + (255,))
        fond = degrade_horizontal(g, g, BLEU_FONCE, TURQUOISE_FONCE).convert("RGBA")
        masque = Image.new("L", (g, g))
        ImageDraw.Draw(masque).rounded_rectangle((0, 0, g - 1, g - 1), radius=int(g * 0.26), fill=255)
        im.paste(fond, (0, 0), masque)
        fnt = ImageFont.truetype(police, int(g * 0.58))
        d = ImageDraw.Draw(im)
        ch = chr(code)
        bb = d.textbbox((0, 0), ch, font=fnt)
        d.text(((g - (bb[2] - bb[0])) / 2 - bb[0], (g - (bb[3] - bb[1])) / 2 - bb[1]), ch, font=fnt, fill=BLANC + (255,))
        return im.resize((t, t), Image.Resampling.LANCZOS).convert("RGB")
    return f


# ------------------------------------------------------------- l'animation ----
def gif_par_defaut(chemin):
    """Les donnees qui vont de l'automate (API Studio) a l'ecran (IHM Studio) : 24 images."""
    l, h, n = 720, 240, 24
    g = 2
    L, H = l * g, h * g
    t = 112 * g
    api = logo("api_studio", t)
    ihm = logo("ihm_studio", t)
    x0, x1 = 48 * g + t + 18 * g, l * g - 48 * g - t - 18 * g
    y = H // 2
    images = []
    for k in range(n):
        im = Image.new("RGBA", (L, H), BLANC + (255,))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((x0, y - 5 * g, x1, y + 5 * g), radius=5 * g, fill=PISTE + (255,))
        lueur = Image.new("RGBA", (L, H), (0, 0, 0, 0))
        dl = ImageDraw.Draw(lueur)
        for p in range(3):
            u = ((k / n) + p / 3) % 1.0
            cx = x0 + 8 * g + u * (x1 - x0 - 16 * g)
            c = melange(BLEU, TURQUOISE, u)
            a = int(255 * min(1.0, u / 0.12, (1 - u) / 0.12))
            dl.rounded_rectangle((cx - 20 * g, y - 10 * g, cx + 20 * g, y + 10 * g), radius=10 * g, fill=c + (a // 3,))
        lueur = lueur.filter(ImageFilter.GaussianBlur(7 * g))
        im.alpha_composite(lueur)
        d = ImageDraw.Draw(im)
        for p in range(3):
            u = ((k / n) + p / 3) % 1.0
            cx = x0 + 8 * g + u * (x1 - x0 - 16 * g)
            c = melange(BLEU, TURQUOISE, u)
            a = int(255 * min(1.0, u / 0.12, (1 - u) / 0.12))
            d.rounded_rectangle((cx - 13 * g, y - 6 * g, cx + 13 * g, y + 6 * g), radius=6 * g, fill=c + (a,))
        # l'ecran recoit : un halo qui bat au rythme des arrivees
        battement = (k % (n // 3)) / (n // 3)
        halo = Image.new("RGBA", (L, H), (0, 0, 0, 0))
        dh = ImageDraw.Draw(halo)
        cx, cy = L - 48 * g - t // 2, y
        rr = t // 2 + int(battement * 16 * g)
        dh.rounded_rectangle((cx - rr, cy - rr, cx + rr, cy + rr), radius=int(rr * 0.3),
                             outline=TURQUOISE + (int(150 * (1 - battement)),), width=3 * g)
        im.alpha_composite(halo)
        im.alpha_composite(api, (48 * g, y - t // 2))
        im.alpha_composite(ihm, (L - 48 * g - t, y - t // 2))
        images.append(im.convert("RGB").resize((l, h), Image.Resampling.LANCZOS))
    # une seule palette pour toutes les images : pas de scintillement des couleurs
    planche = Image.new("RGB", (l, h * n))
    for i, im in enumerate(images):
        planche.paste(im, (0, i * h))
    pal = planche.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    # le fond : la couleur la plus frequente redevient un blanc pur (la mediane le grisait un peu,
    # et le cadre de l'animation se voyait sur la page blanche)
    fond = max(pal.getcolors(256 * 256))[1]
    p = pal.getpalette()
    p[fond * 3:fond * 3 + 3] = [255, 255, 255]
    pal.putpalette(p)
    cadres = [im.quantize(palette=pal, dither=Image.Dither.NONE) for im in images]
    cadres[0].save(chemin, save_all=True, append_images=cadres[1:], duration=55, loop=0, optimize=False, disposal=1)


def decouper_gif(chemin):
    gif = Image.open(chemin)
    cadres, delais = [], []
    for c in ImageSequence.Iterator(gif):
        # un blanc presque pur (la palette du GIF l'arrondit) devient pur : la page de l'assistant
        # est blanche, l'animation s'y fond sans cadre
        c = c.convert("RGB")
        r, v, b = c.split()
        blanc = Image.eval(r, lambda x: 255 if x >= 249 else 0)
        for canal in (v, b):
            blanc = Image.composite(blanc, Image.new("L", c.size, 0), Image.eval(canal, lambda x: 255 if x >= 249 else 0))
        c.paste((255, 255, 255), (0, 0), blanc)
        cadres.append(c)
        delais.append(c.info.get("duration", gif.info.get("duration", 60)) or 60)
    l2, h2 = cadres[0].size
    l1, h1 = max(1, l2 // 2), max(1, h2 // 2)
    for (l, h, suffixe) in ((l1, h1, ""), (l2, h2, "-2x")):
        bande = Image.new("RGB", (l, h * len(cadres)), BLANC)
        for i, c in enumerate(cadres):
            bande.paste(c if c.size == (l, h) else c.resize((l, h), Image.Resampling.LANCZOS), (0, i * h))
        enregistrer(bande, "animation" + suffixe)
    delai = max(30, int(sum(delais) / len(delais)))
    ini = configparser.ConfigParser()
    ini["Animation"] = {"images": str(len(cadres)), "delai": str(delai), "largeur": str(l1), "hauteur": str(h1)}
    with open(os.path.join(UI, "animation.ini"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("; Ecrit par outils/installateur_images.py a partir de images/animation.gif : ne pas modifier.\n")
        ini.write(f)
    print(f"animation : {len(cadres)} images de {l1}x{h1} (2x : {l2}x{h2}), {delai} ms")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gif-par-defaut", action="store_true", help="refaire images/animation.gif (le GIF livre)")
    ap.add_argument("--icones", default="", help="MaterialIcons-Regular.ttf (sinon : les icones du depot restent)")
    a = ap.parse_args()

    for etat in ("fait", "encours", "avenir"):
        deux_tailles("marque-" + etat, lambda e, s=etat: marque(s, e), 20, 20)
    deux_tailles("logo", logo_colonne, 88, 40)
    deux_tailles("logo-api", logo_carte("api_studio"), 40, 40)
    deux_tailles("logo-ihm", logo_carte("ihm_studio"), 40, 40)
    deux_tailles("decor", decor, 200, 150)
    deux_tailles("accueil", bandeau_accueil, 560, 150)
    deux_tailles("fin", bandeau_fin, 560, 150)
    deux_tailles("degrade", lambda e: degrade_horizontal(512 * e, 4 * e, BLEU, TURQUOISE), 512, 4)
    if a.icones:
        codes = {}
        with open(os.path.splitext(a.icones)[0] + ".codepoints", encoding="ascii") as f:
            for ligne in f:
                n, c = ligne.split()
                codes[n] = int(c, 16)
        for etape, nom in ICONES.items():
            deux_tailles("icone-" + etape, icone(a.icones, codes[nom]), 44, 44)
    gif = os.path.join(INST, "images", "animation.gif")
    if a.gif_par_defaut or not os.path.exists(gif):
        gif_par_defaut(gif)
    decouper_gif(gif)
    print("images :", ", ".join(sorted(f for f in os.listdir(UI) if f.endswith(".bmp") and "-2x" not in f)))


if __name__ == "__main__":
    sys.exit(main())
