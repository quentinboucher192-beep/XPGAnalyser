#!/usr/bin/env python3
# La planche du lot API 2 (PLANCHE_API_LOT2.png) : la barre du haut et ses menus,
# le tableau de bord de l'API, les onglets de l'API (Configuration, Taches,
# Sous-routines), les pastilles et la legende des macros, la simulation dans la
# barre.
#   python3 planche-api-lot2.py <dossier des captures> [planche.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else src + "/PLANCHE_API_LOT2.png"
# (capture, legende, recadrage) : None = l'ecran entier, sinon (x0, y0, x1, y1)
BAR = (0, 0, 1920, 330)
items = [("PNG_580_barre_arbre_tableau_de_bord.png", "Le projet s'ouvre sur le tableau de bord", None),
         ("PNG_581_menu_projet.png", "Le menu Projet", (0, 0, 760, 430)),
         ("PNG_582_menu_nouveau.png", "+ Nouveau : tout ce qui se crée", (300, 0, 1060, 430)),
         ("PNG_583_menu_affichage.png", "Affichage", (1160, 0, 1920, 430)),
         ("PNG_586_barre_annuler_modifie.png", "« modifié », Annuler et son infobulle", (0, 0, 960, 330)),
         ("PNG_587_configuration.png", "API · Configuration, dans son cadre", (330, 40, 1920, 1040)),
         ("PNG_590_taches.png", "API · Tâches : ce que MAST exécute", (330, 40, 1920, 1040)),
         ("PNG_591_sous_routines.png", "Sous-routines : même vide, un onglet", (330, 40, 1920, 1040)),
         ("PNG_592_tableau_de_bord_bibliotheque.png", "« À regarder » → la macro qui règle", (330, 40, 1920, 1040)),
         ("PNG_593_macros_pastilles_legende.png", "Macros : icônes, pastilles, légende", (330, 40, 1920, 1040)),
         ("PNG_594_macros_filtre_sections.png", "Un clic sur une pastille : le filtre", (330, 40, 1920, 1040)),
         ("PNG_597_barre_simulation_infobulle.png", "La simulation dans la barre : l'état, pourquoi", (380, 0, 1600, 330))]
cols = 4
tw, th = 466, 262
lh = 40
pad = 20
title_h = 60
rows = (len(items) + cols - 1) // cols
W = pad + cols * (tw + pad)
H = title_h + rows * (lh + th + 12) + pad
bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
img = Image.new("RGB", (W, H), (19, 22, 28))
d = ImageDraw.Draw(img)
d.text((pad - 10, 14), "Lot API 2 : la barre du haut, l'arbre de l'API en français, le tableau de bord, les onglets de l'API, les pastilles des macros",
       font=ImageFont.truetype(bold, 22), fill=(236, 239, 244))
lab = ImageFont.truetype(bold, 18)
for i, (name, label, box) in enumerate(items):
    r, c = divmod(i, cols)
    x = pad + c * (tw + pad) - 10
    y = title_h + r * (lh + th + 12)
    d.text((x, y + 8), label, font=lab, fill=(106, 164, 255))
    try:
        ex = Image.open(f"{src}/{name}").convert("RGB")
    except OSError:
        continue
    if box and ex.size == (1920, 1080):
        ex = ex.crop(box)
    scale = min(tw / ex.width, th / ex.height)
    ex = ex.resize((max(1, int(ex.width * scale)), max(1, int(ex.height * scale))), Image.LANCZOS)
    img.paste(ex, (x + (tw - ex.width) // 2, y + lh + (th - ex.height) // 2))
img.save(out)
print(out, img.size)
