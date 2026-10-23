#!/usr/bin/env python3
# =============================================================================
#  outils/installateur_images.py - les images de l'installateur (1.12.27, 1.12.28)
# -----------------------------------------------------------------------------
#  Fabrique installateur/images/ui/*.bmp, chaque image en 1x et en 2x (pour les
#  ecrans agrandis) :
#   - 1.12.27 : le logo et le decor de la colonne de gauche, l'icone de chaque
#     etape, le degrade de l'en-tete, les logos des cartes de l'accueil, et
#     l'animation de la copie ;
#   - 1.12.28, le mouvement : des CALQUES et des BANDES D'IMAGES que
#     l'installateur assemble lui-meme a chaque instant (rien n'est une video) :
#       . l'accueil : le fond (le reseau), les deux logos et leur ombre, les points
#         du lien, l'eclat qui passe, la lueur des noeuds et des impulsions ;
#       . la fin : le fond, le cercle, la coche qui se dessine (une bande), les
#         etincelles (une bande par couleur) ;
#       . la colonne : les marques des etapes (a venir ; l'arrivee, la pulsation
#         et la coche qui se dessine : des bandes), la pastille de l'etape en cours ;
#       . la copie : la barre (son degrade, l'eclat qui la parcourt) et les images
#         du diaporama.
#     Les calques ont un canal alpha : BMP 32 bits, alpha premultiplie (celui
#     qu'attend AlphaBlend de Windows). Une bande = les images l'une sous l'autre.
#     Les reperes (noeuds du reseau, pistes du decor, etincelles...) et le nombre
#     d'images de chaque bande vont dans images/ui/habillage.ini, que
#     l'installateur lit a la compilation : changer une image ici suffit.
#
#  L'animation de la copie vient d'un GIF : installateur/images/animation.gif.
#  Pour en changer, remplace ce fichier par le tien (n'importe quel GIF anime)
#  et relance ce script : il le decoupe en une bande d'images (animation.bmp,
#  animation-2x.bmp) et ecrit animation.ini (nombre d'images, delai, taille).
#  --gif-par-defaut refait le GIF livre.
#
#  Les BMP sont gardes dans le depot : construire l'installateur n'a besoin ni
#  de Python ni de Pillow. Ce script ne sert qu'a les refaire.
#
#    python3 outils/installateur_images.py [--gif-par-defaut] [--icones ...]
#
#  Les icones des etapes et celles du diaporama viennent de la police Material
#  Icons (Apache 2.0) : --icones /chemin/MaterialIcons-Regular.ttf (sans elle,
#  elles gardent leur version du depot).
# =============================================================================
import argparse
import configparser
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageSequence

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
CARTE = (243, 246, 250)        # le fond des cartes de l'accueil et du diaporama
PISTE = (227, 233, 242)        # le cable de l'animation

SS = 4  # le sur-echantillonnage des petites images (bords lisses)

# 1.12.28 : les reperes ecrits dans habillage.ini (section -> cle -> valeur)
REPERES = {}


def repere(section, cle, valeur):
    REPERES.setdefault(section, {})[cle] = str(valeur)


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


# ------------------------------------------------ 1.12.28 : calques et bandes ----
def reduire(im, taille):
    """Une image RGBA reduite proprement (l'alpha premultiplie le temps de la reduction)."""
    return im.convert("RGBa").resize(taille, Image.Resampling.LANCZOS).convert("RGBA")


def enregistrer_alpha(im, nom):
    """BMP 32 bits, alpha premultiplie (AlphaBlend de Windows, AC_SRC_ALPHA)."""
    os.makedirs(UI, exist_ok=True)
    p = im.convert("RGBA").convert("RGBa")
    Image.frombytes("RGBA", p.size, p.tobytes()).save(os.path.join(UI, nom + ".bmp"))


def bande(images, fond=None):
    """Les images l'une sous l'autre (la bande que l'installateur lit image par image)."""
    l, h = images[0].size
    mode = images[0].mode
    b = Image.new(mode, (l, h * len(images)), fond if fond is not None else ((0, 0, 0, 0) if mode == "RGBA" else BLANC))
    for i, im in enumerate(images):
        assert im.size == (l, h)
        b.paste(im, (0, i * h))
    return b


def deux_tailles_alpha(nom, fabrique, l, h, n=1):
    """fabrique(echelle) -> une image RGBA (n = 1) ou une liste de n images (une bande) ;
    enregistre nom.bmp et nom-2x.bmp (32 bits) ; le nombre d'images va dans habillage.ini."""
    for e, suffixe in ((1, ""), (2, "-2x")):
        r = fabrique(e)
        images = r if isinstance(r, list) else [r]
        assert len(images) == n, (nom, len(images))
        for im in images:
            assert im.size == (l * e, h * e), (nom, im.size)
        enregistrer_alpha(bande(images), nom + suffixe)
    repere("Bandes", nom, f"{l},{h},{n}")


def disque(d, cx, cy, r, couleur):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=couleur)


def polyligne_partielle(pts, f):
    """Les premiers f (0..1) de la longueur d'une ligne brisee."""
    longueurs = [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    reste = f * sum(longueurs)
    r = [pts[0]]
    for i, lg in enumerate(longueurs):
        if reste >= lg:
            r.append(pts[i + 1])
            reste -= lg
        else:
            t = reste / lg if lg else 0
            r.append((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t))
            break
    return r


def trait(d, pts, couleur, w):
    """Une ligne brisee aux bouts ronds."""
    if len(pts) >= 2:
        d.line(pts, fill=couleur, width=int(w), joint="curve")
    for p in (pts[0], pts[-1]):
        disque(d, p[0], p[1], w / 2, couleur)


def sortie_rebond(t, c1=1.70158):
    u = t - 1
    return 1 + (c1 + 1) * u * u * u + c1 * u * u


def douce(t):
    return (1 - math.cos(math.pi * t)) / 2


# ------------------------------------------------------------ logo de colonne ----
def logo_colonne(e):
    t = 40 * e
    im = Image.new("RGBA", (88 * e, 40 * e), MARINE + (255,))
    im.alpha_composite(logo("api_studio", t), (0, 0))
    im.alpha_composite(logo("ihm_studio", t), (48 * e, 0))
    return im.convert("RGB")


def logo_carte(nom, taille=40):
    def f(e):
        return sur_fond(logo(nom, taille * e), CARTE)
    return f


# ------------------------------------------------------ le decor de la colonne ----
# Des pistes de circuit, discretes, en bas de la colonne (unites 1x, dans 200 x 150) ;
# 1.12.28 : des impulsions les parcourent (l'installateur lit les pistes dans habillage.ini).
PISTES = [
    [(0, 120), (40, 120), (60, 100), (120, 100), (140, 80), (200, 80)],
    [(0, 136), (70, 136), (86, 120), (150, 120), (170, 140), (200, 140)],
    [(20, 150), (20, 112), (34, 98), (34, 60)],
    [(96, 150), (96, 128), (110, 114), (176, 114)],
    [(150, 60), (150, 92), (166, 108), (200, 108)],
    [(56, 40), (56, 78), (74, 96)],
]


# 1.12.28 : seul le bas du decor se voit (DECOR_HAUT a 150, sous les etapes) ; il s'efface vers le haut
# et n'a plus de bord : les etapes, au-dessus, ne le coupent pas.
DECOR_HAUT = 54


def decor(e):
    l, h = 200 * e, 150 * e
    g = SS
    im = Image.new("RGB", (l * g, h * g), MARINE)
    d = ImageDraw.Draw(im)
    u = e * g
    for i, p in enumerate(PISTES):
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
        a = int(255 * fondu_decor(y / h))
        for x in range(l):
            pm[x, y] = a
    return Image.composite(im, fond, masque).crop((0, DECOR_HAUT * e, l, h))


def fondu_decor(v):
    """0 en haut de la partie visible (DECOR_HAUT), 1 quarante points plus bas."""
    return min(1.0, max(0.0, (v * 150 - DECOR_HAUT) / 40))


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


def reseau_points(l, h, e, graine):
    """Les noeuds (x, y, rayon) et les liens (i, j) du reseau : le meme tirage en 1x et en 2x."""
    r = random.Random(graine)
    pts = [(r.uniform(0.02, 0.62) * l, r.uniform(0.08, 0.92) * h) for _ in range(16)]
    liens = [(i, j) for i in range(len(pts)) for j in range(i + 1, len(pts)) if math.dist(pts[i], pts[j]) < 0.22 * l]
    rayons = [r.choice((2.2, 3.0, 4.0)) * e for _ in pts]
    return pts, liens, rayons


def reseau(d, l, h, e, graine):
    """Des noeuds relies (un reseau d'equipements), tres discrets."""
    pts, liens, rayons = reseau_points(l, h, e, graine)
    for i, j in liens:
        d.line([pts[i], pts[j]], fill=(255, 255, 255, 34), width=max(1, int(1.2 * e)))
    for (x, y), rr in zip(pts, rayons):
        d.ellipse((x - rr, y - rr, x + rr, y + rr), fill=(255, 255, 255, 70))


def reperes_reseau(section, graine):
    pts, liens, rayons = reseau_points(560, 150, 1, graine)
    repere(section, "noeuds", ";".join(f"{round(x)},{round(y)},{round(r * 10)}" for (x, y), r in zip(pts, rayons)))
    repere(section, "liens", ";".join(f"{i},{j}" for i, j in liens))


def ombre(rgba, rayon, opacite=110):
    a = rgba.getchannel("A")
    o = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    o.putalpha(a.point(lambda v: v * opacite // 255))
    return o.filter(ImageFilter.GaussianBlur(rayon))


def fond_reseau(graine):
    def f(e):
        l, h = 560 * e, 150 * e
        im = fond_bandeau(l, h).convert("RGBA")
        calque = Image.new("RGBA", (l, h), (0, 0, 0, 0))
        reseau(ImageDraw.Draw(calque), l, h, e, graine)
        im.alpha_composite(calque)
        return im.convert("RGB")
    return f


# ------------------------------------------- 1.12.28 : l'accueil, en calques ----
# Les deux logos (92 px, comme le bandeau de la 1.12.27), chacun avec son ombre, dans un
# carre de 132 ; les trois points du lien ; l'eclat (une bande de lumiere oblique).
LOGO_HEROS, MARGE_HEROS = 92, 20
LOGOS_HEROS = (("api_studio", 324, 26), ("ihm_studio", 436, 32))
POINTS_HEROS = ((419, 75, BLEU), (426, 75, melange(BLEU, TURQUOISE, 0.5)), (433, 75, TURQUOISE))


def heros_logo(nom):
    def f(e):
        t, m = LOGO_HEROS * e, MARGE_HEROS * e
        c = 2 * m + t
        lg = logo(nom, t)
        porteur = Image.new("RGBA", (c, c), (0, 0, 0, 0))
        porteur.alpha_composite(lg, (m, m + 5 * e))
        im = ombre(porteur, 7 * e)
        im.alpha_composite(lg, (m, m))
        return im
    return f


def heros_points(e):
    images = []
    for (_, _, c) in POINTS_HEROS:
        g = 10 * e * SS
        im = Image.new("RGBA", (g, g), (0, 0, 0, 0))
        disque(ImageDraw.Draw(im), g / 2, g / 2, 3 * e * SS, c + (255,))
        images.append(reduire(im, (10 * e, 10 * e)))
    return images


def eclat(e):
    """Une bande de lumiere oblique (20 degres), douce : elle passe sur le bandeau."""
    l, h = 90 * e, 150 * e
    im = Image.new("RGBA", (l, h), (0, 0, 0, 0))
    px = im.load()
    pente = math.tan(math.radians(20))
    for y in range(h):
        x0 = l / 2 + (y - h / 2) * pente
        for x in range(l):
            dd = (x - x0) / (13 * e)
            a = 0.30 * math.exp(-dd * dd)
            # plus discrete en haut et en bas
            v = y / h
            a *= min(1.0, v / 0.12, (1 - v) / 0.12)
            px[x, y] = (255, 255, 255, int(255 * a))
    return im


def lueur(couleur, coeur=BLANC):
    """Une lueur ronde et douce (le coeur plus clair) : les noeuds qui scintillent, les impulsions."""
    def f(e):
        t = 32 * e
        im = Image.new("RGBA", (t, t), (0, 0, 0, 0))
        px = im.load()
        s = 6.5 * e
        for y in range(t):
            for x in range(t):
                r = math.dist((x + 0.5, y + 0.5), (t / 2, t / 2))
                a = math.exp(-(r / s) ** 2)
                c = melange(couleur, coeur, math.exp(-(r / (s * 0.45)) ** 2))
                px[x, y] = c + (int(255 * a),)
        return im
    return f


def reperes_accueil():
    repere("Accueil", "logos", ";".join(f"{x - MARGE_HEROS},{y - MARGE_HEROS}" for (_, x, y) in LOGOS_HEROS))
    repere("Accueil", "points", ";".join(f"{x},{y}" for (x, y, _) in POINTS_HEROS))
    reperes_reseau("Accueil", 7)


# ----------------------------------------------- 1.12.28 : la fin, en calques ----
# Le cercle de la coche (centre 400,75), la coche qui se dessine, les etincelles.
CENTRE_SUCCES = (400, 75)
BOITE_COCHE = (370, 52, 62, 50)          # x, y, largeur, hauteur (1x)
ETINCELLES = ((324, 28, 7, 0), (468, 34, 5, 1), (490, 108, 8, 0), (310, 118, 5, 1), (440, 128, 4, 0))   # x, y, taille, 0 ambre / 1 blanc


def succes_disque(e):
    g = SS
    t = 104 * e
    c = Image.new("RGBA", (t * g, t * g), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    d.ellipse((0, 0, t * g - 1, t * g - 1), fill=(255, 255, 255, 40))
    m = 12 * e * g
    d.ellipse((m, m, t * g - m, t * g - m), fill=TURQUOISE + (255,))
    c = reduire(c, (t, t))
    b = 150 * e
    im = Image.new("RGBA", (b, b), (0, 0, 0, 0))
    x = (b - t) // 2
    porteur = Image.new("RGBA", (b, b), (0, 0, 0, 0))
    porteur.alpha_composite(c, (x, x + 5 * e))
    im.alpha_composite(ombre(porteur, 8 * e, 90))
    im.alpha_composite(c, (x, x))
    return im


def succes_coche(e):
    """La coche blanche, dessinee peu a peu : 10 images."""
    bx, by, bl, bh = BOITE_COCHE
    cx, cy = CENTRE_SUCCES
    # les points de la 1.12.27 (dans le carre de 104 du cercle), ramenes dans la boite de la coche
    x0, y0 = cx - 52, cy - 52
    pts = [(x0 + 104 * 0.32 - bx, y0 + 104 * 0.52 - by), (x0 + 104 * 0.45 - bx, y0 + 104 * 0.65 - by), (x0 + 104 * 0.70 - bx, y0 + 104 * 0.38 - by)]
    images = []
    g = SS * e
    for k in range(10):
        f = douce((k + 1) / 10)
        im = Image.new("RGBA", (bl * g, bh * g), (0, 0, 0, 0))
        trait(ImageDraw.Draw(im), [(x * g, y * g) for x, y in polyligne_partielle(pts, f)], BLANC + (255,), 9 * g)
        images.append(reduire(im, (bl * e, bh * e)))
    return images


def etincelle(couleur):
    """Une etoile a quatre branches, de la plus petite a la plus grande : 8 images."""
    def f(e):
        images = []
        g = SS * e
        t = 22
        for k in range(8):
            s = 8 * (0.25 + 0.75 * k / 7) * g
            sx = sy = t * g / 2
            im = Image.new("RGBA", (t * g, t * g), (0, 0, 0, 0))
            ImageDraw.Draw(im).polygon([(sx, sy - s), (sx + s * 0.28, sy - s * 0.28), (sx + s, sy), (sx + s * 0.28, sy + s * 0.28),
                                        (sx, sy + s), (sx - s * 0.28, sy + s * 0.28), (sx - s, sy), (sx - s * 0.28, sy - s * 0.28)],
                                       fill=couleur + (255,))
            images.append(reduire(im, (t * e, t * e)))
        return images
    return f


def reperes_fin():
    repere("Fin", "disque", f"{CENTRE_SUCCES[0] - 75},{CENTRE_SUCCES[1] - 75}")
    repere("Fin", "coche", f"{BOITE_COCHE[0]},{BOITE_COCHE[1]}")
    repere("Fin", "centre", f"{CENTRE_SUCCES[0]},{CENTRE_SUCCES[1]}")
    repere("Fin", "etincelles", ";".join(f"{x},{y},{s},{c}" for (x, y, s, c) in ETINCELLES))
    reperes_reseau("Fin", 11)


# --------------------------------------- 1.12.28 : la colonne, en calques ----
# Les marques des etapes (28 x 28, le centre au milieu ; le disque fait 8,8 de rayon comme
# les marques de la 1.12.27) : a venir, l'arrivee (le disque ambre qui grossit), la pulsation
# (un anneau qui s'eloigne), la coche (l'ambre devient turquoise, la coche se dessine).
RAIL = 28
RAYON_MARQUE = 8.8


def marque_image(e, dessin):
    g = RAIL * e * SS
    im = Image.new("RGBA", (g, g), (0, 0, 0, 0))
    dessin(ImageDraw.Draw(im), g / 2, e * SS)
    return reduire(im, (RAIL * e, RAIL * e))


def rail_avenir(e):
    def f(d, c, u):
        r = RAYON_MARQUE * u
        d.ellipse((c - r, c - r, c + r, c + r), outline=GRIS_ETEINT + (255,), width=int(1.6 * u))
    return marque_image(e, f)


def rail_arrivee(e):
    images = []
    for k in range(10):
        t = k / 9

        def f(d, c, u, t=t):
            # l'anneau a venir s'efface, le disque ambre grossit (un peu trop, puis revient)
            if t < 0.5:
                r = RAYON_MARQUE * u
                a = int(255 * (1 - t / 0.5))
                d.ellipse((c - r, c - r, c + r, c + r), outline=GRIS_ETEINT + (a,), width=int(1.6 * u))
            s = max(0.0, sortie_rebond(t, 2.4))
            if s > 0:
                disque(d, c, c, RAYON_MARQUE * u * s, AMBRE + (255,))
            rp = 3.4 * u * max(0.0, min(1.0, (t - 0.45) / 0.55))
            if rp > 0:
                disque(d, c, c, rp, MARINE + (255,))
        images.append(marque_image(e, f))
    return images


def rail_pulse(e):
    images = []
    for k in range(24):
        t = k / 24

        def f(d, c, u, t=t):
            r = (RAYON_MARQUE + 0.6 + 4.2 * t) * u
            a = int(255 * 0.75 * (1 - t) ** 1.4)
            if a > 0:
                d.ellipse((c - r, c - r, c + r, c + r), outline=AMBRE + (a,), width=max(1, int(1.5 * u)))
            disque(d, c, c, RAYON_MARQUE * u, AMBRE + (255,))
            disque(d, c, c, 3.4 * u, MARINE + (255,))
        images.append(marque_image(e, f))
    return images


def rail_coche(e):
    images = []
    pts = [(RAIL / 2 - 10 + 20 * 0.30, RAIL / 2 - 10 + 20 * 0.52), (RAIL / 2 - 10 + 20 * 0.44, RAIL / 2 - 10 + 20 * 0.66),
           (RAIL / 2 - 10 + 20 * 0.71, RAIL / 2 - 10 + 20 * 0.37)]
    for k in range(12):
        def f(d, c, u, k=k):
            if k < 4:
                disque(d, c, c, RAYON_MARQUE * u, melange(AMBRE, TURQUOISE, k / 3) + (255,))
                rp = 3.4 * u * (1 - k / 3)
                if rp > 0.2:
                    disque(d, c, c, rp, MARINE + (255,))
            else:
                disque(d, c, c, RAYON_MARQUE * u, TURQUOISE + (255,))
                fr = douce((k - 3) / 8)
                trait(d, [(x * u, y * u) for x, y in polyligne_partielle(pts, fr)], MARINE + (255,), 2.2 * u)
        images.append(marque_image(e, f))
    return images


def rail_anneau(e):
    """Pendant la copie, l'etape Installation : son anneau se remplit (25 images, de 0 a 100 %)."""
    images = []
    for k in range(25):
        def f(d, c, u, k=k):
            disque(d, c, c, RAYON_MARQUE * u, AMBRE + (255,))
            disque(d, c, c, 3.4 * u, MARINE + (255,))
            r = 12.4 * u
            w = max(1, int(1.8 * u))
            d.ellipse((c - r, c - r, c + r, c + r), outline=AMBRE + (60,), width=w)
            if k > 0:
                d.arc((c - r, c - r, c + r, c + r), -90, -90 + 360 * k / 24, fill=AMBRE + (255,), width=w)
        images.append(marque_image(e, f))
    return images


def rail_pastille(e):
    """La pastille de l'etape en cours : elle glisse d'une etape a l'autre."""
    l, h = 176 * e, 28 * e
    g = SS
    im = Image.new("RGBA", (l * g, h * g), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, l * g - 1, h * g - 1), radius=9 * e * g, fill=(255, 255, 255, 22), outline=(255, 255, 255, 30), width=max(1, e * g))
    im = reduire(im, (l, h))
    # plus claire a gauche (du cote de la marque), qui s'efface vers la droite
    masque = Image.new("L", (l, h))
    pm = masque.load()
    for x in range(l):
        v = int(255 * (1 - 0.65 * (x / (l - 1)) ** 1.5))
        for y in range(h):
            pm[x, y] = v
    a = ImageChops.multiply(im.getchannel("A"), masque)
    im.putalpha(a)
    return im


# ---------------------------------------------- 1.12.28 : la barre de la copie ----
def barre_degrade(e):
    l, h = 480 * e, 10 * e
    im = degrade_horizontal(l, h, BLEU, TURQUOISE)
    # un reflet en haut : la barre a du relief
    reflet = Image.new("L", (l, h))
    pm = reflet.load()
    for y in range(h):
        v = int(255 * 0.16 * max(0.0, 1 - y / (h * 0.55)))
        for x in range(l):
            pm[x, y] = v
    return Image.composite(Image.new("RGB", (l, h), BLANC), im, reflet)


def barre_eclat(e):
    l, h = 64 * e, 10 * e
    im = Image.new("RGBA", (l, h), (0, 0, 0, 0))
    px = im.load()
    for x in range(l):
        dd = (x - l / 2) / (l * 0.22)
        a = 0.55 * math.exp(-dd * dd)
        for y in range(h):
            px[x, y] = (255, 255, 255, int(255 * a))
    return im


# ---------------------------------------------- 1.12.28 : le diaporama de la copie ----
# Une image par diapositive (56 x 56, sur le fond des cartes) ; les textes sont dans le
# [Code] de l'installateur (DiaposTitres, DiaposTextes), dessines a la taille de l'ecran.
DIAPOS_ICONES = {"diapo-versions": "history", "diapo-donnees": "verified_user", "diapo-aide": "menu_book", "diapo-maintenance": "build"}


def tuile_icone(police, code, taille, fond_page):
    def f(e):
        t = taille * e
        g = t * SS
        im = Image.new("RGBA", (g, g), fond_page + (255,))
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
    return tuile_icone(police, code, 44, BLANC)


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
        lueur_ = Image.new("RGBA", (L, H), (0, 0, 0, 0))
        dl = ImageDraw.Draw(lueur_)
        for p in range(3):
            u = ((k / n) + p / 3) % 1.0
            cx = x0 + 8 * g + u * (x1 - x0 - 16 * g)
            c = melange(BLEU, TURQUOISE, u)
            a = int(255 * min(1.0, u / 0.12, (1 - u) / 0.12))
            dl.rounded_rectangle((cx - 20 * g, y - 10 * g, cx + 20 * g, y + 10 * g), radius=10 * g, fill=c + (a // 3,))
        lueur_ = lueur_.filter(ImageFilter.GaussianBlur(7 * g))
        im.alpha_composite(lueur_)
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
        bande_ = Image.new("RGB", (l, h * len(cadres)), BLANC)
        for i, c in enumerate(cadres):
            bande_.paste(c if c.size == (l, h) else c.resize((l, h), Image.Resampling.LANCZOS), (0, i * h))
        enregistrer(bande_, "animation" + suffixe)
    delai = max(30, int(sum(delais) / len(delais)))
    ini = configparser.ConfigParser()
    ini["Animation"] = {"images": str(len(cadres)), "delai": str(delai), "largeur": str(l1), "hauteur": str(h1)}
    with open(os.path.join(UI, "animation.ini"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("; Ecrit par outils/installateur_images.py a partir de images/animation.gif : ne pas modifier.\n")
        ini.write(f)
    print(f"animation : {len(cadres)} images de {l1}x{h1} (2x : {l2}x{h2}), {delai} ms")


def ecrire_reperes():
    # toutes les bandes sur une ligne (nom:largeur,hauteur,images;...) : l'installateur la lit d'un coup
    bandes = REPERES.get("Bandes", {})
    repere("Bandes", "liste", ";".join(f"{k}:{v}" for k, v in bandes.items() if k != "liste"))
    ini = configparser.ConfigParser()
    ini.optionxform = str
    for section in sorted(REPERES):
        ini[section] = REPERES[section]
    with open(os.path.join(UI, "habillage.ini"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("; Ecrit par outils/installateur_images.py : ne pas modifier. Les reperes des calques (unites 1x)\n")
        f.write("; et la taille de chaque bande d'images (largeur, hauteur, nombre d'images), lus a la compilation.\n")
        ini.write(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gif-par-defaut", action="store_true", help="refaire images/animation.gif (le GIF livre)")
    ap.add_argument("--icones", default="", help="MaterialIcons-Regular.ttf (sinon : les icones du depot restent)")
    a = ap.parse_args()

    # 1.12.27 (les marques des etapes et les bandeaux d'une piece sont remplaces par les calques)
    for ancien in ("marque-fait", "marque-encours", "marque-avenir", "accueil", "fin"):
        for suffixe in ("", "-2x"):
            if os.path.exists(os.path.join(UI, ancien + suffixe + ".bmp")):
                os.remove(os.path.join(UI, ancien + suffixe + ".bmp"))
    deux_tailles("logo", logo_colonne, 88, 40)
    deux_tailles("logo-api", logo_carte("api_studio"), 40, 40)
    deux_tailles("logo-ihm", logo_carte("ihm_studio"), 40, 40)
    deux_tailles("decor", decor, 200, 150 - DECOR_HAUT)
    deux_tailles("degrade", lambda e: degrade_horizontal(512 * e, 4 * e, BLEU, TURQUOISE), 512, 4)
    repere("Decor", "haut", DECOR_HAUT)
    repere("Decor", "hauteur", 150 - DECOR_HAUT)
    repere("Decor", "pistes", "|".join(" ".join(f"{x},{y}" for x, y in p) for p in PISTES))

    # 1.12.28 : l'accueil
    deux_tailles("accueil-fond", fond_reseau(7), 560, 150)
    deux_tailles_alpha("heros-api", heros_logo("api_studio"), LOGO_HEROS + 2 * MARGE_HEROS, LOGO_HEROS + 2 * MARGE_HEROS)
    deux_tailles_alpha("heros-ihm", heros_logo("ihm_studio"), LOGO_HEROS + 2 * MARGE_HEROS, LOGO_HEROS + 2 * MARGE_HEROS)
    deux_tailles_alpha("heros-points", heros_points, 10, 10, 3)
    deux_tailles_alpha("eclat", eclat, 90, 150)
    deux_tailles_alpha("lueur", lueur((200, 230, 255)), 32, 32)
    deux_tailles_alpha("lueur-turquoise", lueur(TURQUOISE, (210, 255, 248)), 32, 32)
    reperes_accueil()
    # la fin
    deux_tailles("fin-fond", fond_reseau(11), 560, 150)
    deux_tailles_alpha("succes-disque", succes_disque, 150, 150)
    deux_tailles_alpha("succes-coche", succes_coche, BOITE_COCHE[2], BOITE_COCHE[3], 10)
    deux_tailles_alpha("etincelle-ambre", etincelle(AMBRE), 22, 22, 8)
    deux_tailles_alpha("etincelle-blanche", etincelle(BLANC), 22, 22, 8)
    reperes_fin()
    # la colonne
    deux_tailles_alpha("rail-avenir", rail_avenir, RAIL, RAIL)
    deux_tailles_alpha("rail-arrivee", rail_arrivee, RAIL, RAIL, 10)
    deux_tailles_alpha("rail-pulse", rail_pulse, RAIL, RAIL, 24)
    deux_tailles_alpha("rail-coche", rail_coche, RAIL, RAIL, 12)
    deux_tailles_alpha("rail-anneau", rail_anneau, RAIL, RAIL, 25)
    deux_tailles_alpha("rail-pastille", rail_pastille, 176, 28)
    # la copie
    deux_tailles("barre-degrade", barre_degrade, 480, 10)
    deux_tailles_alpha("barre-eclat", barre_eclat, 64, 10)
    deux_tailles("diapo-api", logo_carte("api_studio", 56), 56, 56)
    deux_tailles("diapo-ihm", logo_carte("ihm_studio", 56), 56, 56)
    if a.icones:
        codes = {}
        with open(os.path.splitext(a.icones)[0] + ".codepoints", encoding="ascii") as f:
            for ligne in f:
                n, c = ligne.split()
                codes[n] = int(c, 16)
        for etape, nom in ICONES.items():
            deux_tailles("icone-" + etape, icone(a.icones, codes[nom]), 44, 44)
        for diapo, nom in DIAPOS_ICONES.items():
            deux_tailles(diapo, tuile_icone(a.icones, codes[nom], 56, CARTE), 56, 56)
    gif = os.path.join(INST, "images", "animation.gif")
    if a.gif_par_defaut or not os.path.exists(gif):
        gif_par_defaut(gif)
    decouper_gif(gif)
    ecrire_reperes()
    print("images :", ", ".join(sorted(f for f in os.listdir(UI) if f.endswith(".bmp") and "-2x" not in f)))


if __name__ == "__main__":
    sys.exit(main())
