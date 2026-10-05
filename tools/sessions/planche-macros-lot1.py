#!/usr/bin/env python3
# La planche du lot macros 1 (PLANCHE_MACROS_LOT1.png) : l'onglet Macros (dossiers,
# fiche), le formulaire (le bouton ..., Ctrl+V, l'apercu, Appliquer), ranger (un
# modele, un dossier, la corbeille) et les onglets de l'aide.
#   python3 planche-macros-lot1.py <dossier des captures> [planche.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else src + "/PLANCHE_MACROS_LOT1.png"
items = [("PNG_550_macros_onglet.png", "L'onglet Macros : les dossiers"),
         ("PNG_551_macros_fiche.png", "La fiche d'une macro"),
         ("PNG_553_formulaire_bouton_parcourir.png", "Le formulaire : le bouton …"),
         ("PNG_554_formulaire_ctrl_v.png", "Ctrl+V d'un fichier copié"),
         ("PNG_556_apercu.png", "L'aperçu : ce qui sera fait"),
         ("PNG_557_applique.png", "Appliqué : un seul Ctrl+Z"),
         ("PNG_559_nouvelle_macro_modele.png", "Nouvelle macro, depuis un modèle"),
         ("PNG_562_glisse_dans_un_dossier.png", "Glisser dans un dossier"),
         ("PNG_564_corbeille.png", "La corbeille, Restaurer"),
         ("PNG_566_aide_macros.png", "L'aide : l'onglet Macros"),
         ("PNG_568_aide_blocs.png", "L'aide : Blocs DFB / DDT"),
         ("PNG_569_aide_generale.png", "L'aide : les nouveautés")]
whole = {"PNG_566_aide_macros.png", "PNG_568_aide_blocs.png", "PNG_569_aide_generale.png"}
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
d.text((pad - 10, 14), "Lot macros 1 : l'onglet Macros, son formulaire (…, Ctrl+V, glisser), ranger en dossiers, les onglets de l'aide",
       font=ImageFont.truetype(bold, 22), fill=(236, 239, 244))
lab = ImageFont.truetype(bold, 18)
for i, (name, label) in enumerate(items):
    r, c = divmod(i, cols)
    x = pad + c * (tw + pad) - 10
    y = title_h + r * (lh + th + 12)
    d.text((x, y + 8), label, font=lab, fill=(106, 164, 255))
    try:
        ex = Image.open(f"{src}/{name}").convert("RGB")
    except OSError:
        continue
    if ex.size == (1920, 1080) and name not in whole:   # sans l'arbre du projet ni la barre d'etat
        ex = ex.crop((330, 40, 1920, 1040))
    scale = min(tw / ex.width, th / ex.height) if ex.width / ex.height < tw / th else tw / ex.width
    ex = ex.resize((max(1, int(ex.width * scale)), max(1, int(ex.height * scale))), Image.LANCZOS)
    ex = ex.crop((0, 0, min(ex.width, tw), min(ex.height, th)))
    img.paste(ex, (x + (tw - ex.width) // 2, y + lh))
img.save(out)
print(out, img.size)
