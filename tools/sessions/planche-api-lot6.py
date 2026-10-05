#!/usr/bin/env python3
# La planche du lot API 6 (PLANCHE_API_LOT6.png) : la version en cours dans la
# barre du haut, Terminer / Livrer, la frise des versions qui defile ; le mode
# Modifier des macros (les cartes, l'essai, l'apercu) ; les neuf themes ; l'icone
# du projet ; les unites qui montrent leurs variables, les structures depliees
# des tables d'animation, coller depuis Excel.
#   python3 planche-api-lot6.py <dossier des captures> [planche.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else src + "/PLANCHE_API_LOT6.png"
# (capture, legende, recadrage) : None = l'ecran entier, sinon (x0, y0, x1, y1)
PANE = (330, 40, 1920, 1080)
items = [("PNG_677_menu_version_v4_en_cours.png", "La version que tu modifies, dans la barre", None),
         ("PNG_689_v4_livree_lock.png", "Terminer (FINISH), Livrer et verrouiller (LOCK)", None),
         ("PNG_692_versions_frise_defile.png", "Versions : la frise défile (44 versions)", PANE),
         ("PNG_705_editeur_macro_cartes.png", "Macros, mode Modifier : le code et les cartes", PANE),
         ("PNG_709_editeur_essai_f5.png", "Essayer (F5) : les remarques, leur ligne", PANE),
         ("PNG_708_editeur_apercu_formulaire.png", "L'aperçu du formulaire, en direct", PANE),
         ("PNG_696_galerie_nuit.png", "Neuf thèmes : la galerie", None),
         ("PNG_698_theme_papier.png", "Papier (et Nuit, Graphite, Solarisé...)", None),
         ("PNG_702_icone_mini_paint.png", "L'icône du projet : galerie et mini paint", None),
         ("PNG_682_unite_colle_excel.png", "Unités : leurs variables, coller d'Excel", PANE),
         ("PNG_684_table_instance_dfb_depliee.png", "Tables d'animation : les structures dépliées", PANE),
         ("PNG_679_variables_colle_excel.png", "Variables : coller depuis Excel", PANE)]
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
d.text((pad - 10, 14), "Lot API 6 : le mode Modifier des macros, l\u2019état et les versions, neuf thèmes, l\u2019icône du projet, coller depuis Excel",
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
