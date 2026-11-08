#!/usr/bin/env python3
# =============================================================================
#  outils/installateur_admin_images.py - 1.40.0 : les images de l'installateur de
#  l'outil de l'editeur (installateur/XPG-License-Admin.iss), faites depuis son icone
#  (resources/logo/licences_admin_256.png) aux couleurs de l'installateur des Studios
#  (la colonne marine #0D1F3A, le turquoise #2DC4B6). Bitmaps 24 bits (Inno Setup),
#  a 1x et a 2x :
#    installateur/admin/logo[-2x].bmp     l'icone dans la colonne des etapes (56 px)
#    installateur/admin/petite[-2x].bmp   l'icone de l'en-tete des pages (55 px, fond blanc)
#    installateur/admin/grande[-2x].bmp   l'image de l'accueil et de la fin (164 x 314)
#  Relancer apres un changement de l'icone : python3 outils/installateur_admin_images.py
# =============================================================================
import os
from PIL import Image, ImageDraw

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SORTIE = os.path.join(RACINE, "installateur", "admin")
ICONE = os.path.join(RACINE, "resources", "logo", "licences_admin_256.png")
MARINE = (13, 31, 58)
TURQUOISE = (45, 196, 182)
BLANC = (255, 255, 255)


def icone(taille, fond):
    src = Image.open(ICONE).convert("RGBA").resize((taille, taille), Image.LANCZOS)
    img = Image.new("RGB", (taille, taille), fond)
    img.paste(src, (0, 0), src)
    return img


def enregistrer(img, nom):
    img.convert("RGB").save(os.path.join(SORTIE, nom), "BMP")


def grande(echelle):
    l, h = 164 * echelle, 314 * echelle
    img = Image.new("RGB", (l, h), MARINE)
    d = ImageDraw.Draw(img)
    # un leger degrade vers le bas
    for y in range(h):
        t = y / (h - 1)
        c = tuple(int(MARINE[i] * (1 - 0.35 * t)) for i in range(3))
        d.line([(0, y), (l, y)], fill=c)
    # des pistes de circuit (le decor des Studios), en turquoise attenue
    piste = tuple(int(MARINE[i] * 0.55 + TURQUOISE[i] * 0.45) for i in range(3))
    e = echelle
    chemins = [
        [(0, 250), (40, 250), (62, 228), (110, 228), (130, 208), (164, 208)],
        [(0, 280), (70, 280), (88, 262), (140, 262), (164, 238)],
        [(24, 314), (24, 298), (44, 278)],
    ]
    for c in chemins:
        d.line([(x * e, y * e) for x, y in c], fill=piste, width=max(1, e))
    for x, y in [(62, 228), (130, 208), (88, 262), (140, 262), (44, 278)]:
        r = 3 * e
        d.ellipse([x * e - r, y * e - r, x * e + r, y * e + r], fill=TURQUOISE)
    ic = icone(72 * e, MARINE)
    img.paste(ic, ((l - ic.width) // 2, 56 * e))
    return img


os.makedirs(SORTIE, exist_ok=True)
for e, suffixe in [(1, ""), (2, "-2x")]:
    enregistrer(icone(56 * e, MARINE), "logo" + suffixe + ".bmp")
    enregistrer(icone(55 * e, BLANC), "petite" + suffixe + ".bmp")
    enregistrer(grande(e), "grande" + suffixe + ".bmp")
print("images de l'installateur de l'Admin :", ", ".join(sorted(os.listdir(SORTIE))))
