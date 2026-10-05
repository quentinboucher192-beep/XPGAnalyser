#!/usr/bin/env python3
# La planche des lots API 3 et 4 (PLANCHE_API_LOT3_4.png) : les tables d'animation
# (les lignes API et IHM, + Table, le choix des variables, glisser depuis l'arbre,
# la simulation, ecrire / forcer, l'historique), la Configuration (importer le
# .XHW, les racks et les voies employees, voies et adresses, reseau, plan
# memoire), les Taches, l'Ordre d'execution.
#   python3 planche-api-lot34.py <dossier des captures> [planche.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else src + "/PLANCHE_API_LOT3_4.png"
# (capture, legende, recadrage) : None = l'ecran entier, sinon (x0, y0, x1, y1)
PANE = (330, 40, 1920, 1080)
items = [("PNG_600_tables_purge.png", "Tables d'animation : la table, ses lignes", PANE),
         ("PNG_602_nouvelle_table_renommer.png", "+ Table : renommer sur place", PANE),
         ("PNG_604_choix_variables_ihm.png", "+ Variable IHM : filtrer, cocher", (330, 40, 1920, 1080)),
         ("PNG_606_glisser_depuis_arbre.png", "Glisser une variable depuis l'arbre", (0, 40, 1920, 1080)),
         ("PNG_608_simulation_valeurs_courbe.png", "En marche : les valeurs, la courbe", PANE),
         ("PNG_609_ecrire_forcer.png", "Écrire, forcer (la simulation)", PANE),
         ("PNG_610_historique.png", "Chaque geste est une commande", (330, 40, 1920, 1080)),
         ("PNG_622_module_choisi.png", "Racks : les voies que le code emploie", PANE),
         ("PNG_623_voies_adresses.png", "Voies et adresses : face aux modules", PANE),
         ("PNG_626_plan_memoire.png", "Plan mémoire : un carré par mot", PANE),
         ("PNG_629_taches_fast.png", "Tâches : chien de garde, + FAST", PANE),
         ("PNG_632_ordre_place_apres.png", "Ordre : vérifier, placer après, glisser", PANE)]
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
d.text((pad - 10, 14), "Lots API 3 et 4 : les tables d'animation avec l'IHM, la Configuration, les Tâches, l'Ordre d'exécution",
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
