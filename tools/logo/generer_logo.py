#!/usr/bin/env python3
# =============================================================================
#  tools/logo/generer_logo.py - les fichiers du logo depuis son SVG (lot 12)
# -----------------------------------------------------------------------------
#  resources/logo/xpg_analyzer.svg est la seule source : ce script en tire
#  les PNG (16 a 512 pixels) et l'icone Windows multi-tailles (16 a 256), que
#  resources/windows/xpg_analyzer.rc.in lie a l'executable. L'application, elle,
#  embarque le SVG (src/app/Brand.cpp) : apres un changement du SVG, recopier
#  son texte dans logoSvg().
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
DOSSIER = os.path.normpath(os.path.join(ICI, '..', '..', 'resources', 'logo'))
SVG = os.path.join(DOSSIER, 'xpg_analyzer.svg')


def main():
    svg = open(SVG, 'rb').read()
    images = {}
    for s in (16, 24, 32, 48, 64, 128, 256, 512):
        png = cairosvg.svg2png(bytestring=svg, output_width=s, output_height=s)
        images[s] = Image.open(io.BytesIO(png)).convert('RGBA')
        images[s].save(os.path.join(DOSSIER, 'xpg_analyzer_%d.png' % s))
    images[256].save(os.path.join(DOSSIER, 'xpg_analyzer.ico'),
                     sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    print('logo : %s (8 PNG, 1 ICO)' % DOSSIER)


if __name__ == '__main__':
    main()
