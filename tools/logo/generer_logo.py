#!/usr/bin/env python3
# =============================================================================
#  tools/logo/generer_logo.py - les fichiers des logos depuis leurs SVG
# -----------------------------------------------------------------------------
#  1.12.20 : deux logos de la meme famille (le cadre arrondi, les traits clairs,
#  le point jaune) - un par application :
#
#    resources/logo/api_studio.svg   API Studio : un barreau de schema a contacts
#    resources/logo/ihm_studio.svg   IHM Studio : un ecran de supervision
#
#  Les SVG sont la seule source : ce script en tire, pour chacun, les PNG
#  (16 a 512 pixels) et l'icone Windows multi-tailles (16 a 256) que
#  resources/windows/xpg_analyzer.rc.in lie a son executable ; puis l'icone de
#  l'installateur (resources/logo/studio.ico : les deux logos en biais) et les
#  images de son assistant (installateur/images : les deux logos).
#  L'application, elle, embarque les SVG (src/app/Brand.cpp) : apres un
#  changement d'un SVG, recopier son texte dans logoSvg().
#
#    python3 tools/logo/generer_logo.py            (depuis la racine du depot)
#
#  Il faut cairosvg et Pillow (pip install cairosvg pillow).
# =============================================================================
import io
import os
import sys

try:
    import cairosvg
    from PIL import Image
except ImportError as e:
    sys.exit("il faut cairosvg et Pillow : pip install cairosvg pillow (%s)" % e)

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.normpath(os.path.join(ICI, '..', '..'))
DOSSIER = os.path.join(RACINE, 'resources', 'logo')
IMAGES = os.path.join(RACINE, 'installateur', 'images')
LOGOS = ('api_studio', 'ihm_studio')
TAILLES = (16, 24, 32, 48, 64, 128, 256, 512)


def rendre(svg, cote):
    png = cairosvg.svg2png(bytestring=svg, output_width=cote, output_height=cote)
    return Image.open(io.BytesIO(png)).convert('RGBA')


def degrade(largeur, hauteur, haut, bas):
    # Le fond de l'assistant : un degrade vertical (comme avant).
    im = Image.new('RGB', (largeur, hauteur))
    px = im.load()
    for y in range(hauteur):
        t = y / max(1, hauteur - 1)
        c = tuple(round(haut[i] + (bas[i] - haut[i]) * t) for i in range(3))
        for x in range(largeur):
            px[x, y] = c
    return im


def images_installateur(svgs):
    # La grande image (a gauche de l'accueil et de la fin) : les deux logos l'un
    # sous l'autre ; la petite (en haut a droite des pages) : les deux, en biais.
    for suffixe, echelle in (('', 1), ('-2x', 2)):
        l, h = 164 * echelle, 314 * echelle
        grande = degrade(l, h, (0x0E, 0x3B, 0x6E), (0x17, 0x6B, 0xBF)).convert('RGBA')
        cote = 96 * echelle
        x = (l - cote) // 2
        grande.alpha_composite(rendre(svgs['api_studio'], cote), (x, 52 * echelle))
        grande.alpha_composite(rendre(svgs['ihm_studio'], cote), (x, 166 * echelle))
        grande.convert('RGB').save(os.path.join(IMAGES, 'grande%s.bmp' % suffixe))
        c = 55 * echelle
        petite = Image.new('RGBA', (c, c), (255, 255, 255, 255))
        cote = 34 * echelle
        petite.alpha_composite(rendre(svgs['api_studio'], cote), (0, 0))
        petite.alpha_composite(rendre(svgs['ihm_studio'], cote), (c - cote, c - cote))
        petite.convert('RGB').save(os.path.join(IMAGES, 'petite%s.bmp' % suffixe))


def deux_logos(svgs, cote):
    # Les deux logos en biais, dans un carre transparent (l'icone de l'installateur).
    im = Image.new('RGBA', (cote, cote), (0, 0, 0, 0))
    c = max(1, round(cote * 0.66))
    im.alpha_composite(rendre(svgs['api_studio'], c), (0, 0))
    im.alpha_composite(rendre(svgs['ihm_studio'], c), (cote - c, cote - c))
    return im


def main():
    svgs = {}
    for nom in LOGOS:
        svg = open(os.path.join(DOSSIER, nom + '.svg'), 'rb').read()
        svgs[nom] = svg
        images = {s: rendre(svg, s) for s in TAILLES}
        for s, im in images.items():
            im.save(os.path.join(DOSSIER, '%s_%d.png' % (nom, s)))
        images[256].save(os.path.join(DOSSIER, nom + '.ico'), sizes=[(s, s) for s in TAILLES if s <= 256])
    deux_logos(svgs, 256).save(os.path.join(DOSSIER, 'studio.ico'), sizes=[(s, s) for s in TAILLES if s <= 256])
    images_installateur(svgs)
    print('logos : %s (%d PNG, %d ICO) ; installateur : studio.ico et %s' % (DOSSIER, len(LOGOS) * len(TAILLES), len(LOGOS) + 1, IMAGES))


if __name__ == '__main__':
    main()
