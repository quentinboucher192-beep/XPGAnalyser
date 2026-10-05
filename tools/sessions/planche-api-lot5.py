#!/usr/bin/env python3
# La planche du lot API 5 (PLANCHE_API_LOT5.png) : les types derives (renommer,
# le refus d'une suppression), les blocs DFB (le bloc dessine, ses instances),
# les unites (sections, parametres), les variables (lues par l'IHM, ajouter a une
# table), les sous-routines, le plan memoire en trois zones (%M, %MW, %KW : la
# taille, les bornes, le pourcentage, les adresses indexees dynamiques).
#   python3 planche-api-lot5.py <dossier des captures> [planche.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else src + "/PLANCHE_API_LOT5.png"
# (capture, legende, recadrage) : None = l'ecran entier, sinon (x0, y0, x1, y1)
PANE = (330, 40, 1920, 1080)
items = [("PNG_636_types_derives.png", "Types dérivés : du projet, de la bibliothèque", PANE),
         ("PNG_640_type_renomme.png", "Renommer un type : les variables suivent", PANE),
         ("PNG_643_supprimer_type_refuse.png", "Supprimer : le refus dit pourquoi", PANE),
         ("PNG_648_bloc_grafcetengine.png", "Blocs DFB : le bloc dessiné, ses instances", PANE),
         ("PNG_650_unite_gestion_armoires.png", "Unités : sections, paramètres ⇄ variables", PANE),
         ("PNG_654_variables_lues_ihm.png", "Variables : lues par l'IHM", PANE),
         ("PNG_655_ajouter_table_menu.png", "Ajouter à une table d'animation", PANE),
         ("PNG_661_sous_routine_creee.png", "Sous-routines : + Sous-routine", PANE),
         ("PNG_665_plan_memoire_trois_zones.png", "Plan mémoire : %M, %MW, %KW, le %", PANE),
         ("PNG_669_plan_memoire_bornes.png", "Bornes : %MW0[index] dynamique, hors zone", PANE),
         ("PNG_644_nouveau_type_dialogue.png", "Les dialogues de création en français", PANE),
         ("PNG_673_historique_lot5.png", "Chaque geste est une commande", PANE)]
cols = 3
tw, th = 620, 372
lh = 40
pad = 20
title_h = 60
rows = (len(items) + cols - 1) // cols
W = pad + cols * (tw + pad)
H = title_h + rows * (lh + th + 12) + pad
bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
img = Image.new("RGB", (W, H), (19, 22, 28))
d = ImageDraw.Draw(img)
d.text((pad - 10, 14), "Lot API 5 : Types dérivés, Blocs DFB, Unités, Variables, Sous-routines, le plan mémoire en trois zones",
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
