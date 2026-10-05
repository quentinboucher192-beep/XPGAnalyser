#!/usr/bin/env python3
# La planche du lot API 8 (PLANCHE_API_LOT8.png) : le dossier Simulation et le debogage ;
# renommer partout, les expressions impossibles et le fx ; glisser un fichier ; les themes ;
# chercher et les filtres retenus ; les dialogues dans la fenetre detachee ; l'explorateur
# de fichiers ; l'arbre du projet ; les bandeaux haut et bas ; les nouveautes de l'aide.
#   python3 planche-api-lot8.py <dossier des captures> [planche.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else src + "/PLANCHE_API_LOT8.png"
# (capture, legende, recadrage) : None = l'ecran entier, sinon (x0, y0, x1, y1)
PANE = (330, 44, 1920, 1080)
items = [("PNG_760_vue_ensemble_en_marche.png", "Simulation \u203a Vue d\u2019ensemble (F9)", PANE),
         ("PNG_763_debogage_arret.png", "Le d\u00e9bogage : arr\u00eat\u00e9 sur un point d\u2019arr\u00eat", PANE),
         ("PNG_764_marge_condition.png", "Un point d\u2019arr\u00eat et sa condition", None),
         ("PNG_769_continuer_cycle_garde.png", "Modifier en pause, Continuer : le cycle est gard\u00e9", PANE),
         ("PNG_770_forcages_le_programme_dirait.png", "For\u00e7ages : \u00ab le programme dirait \u00bb", PANE),
         ("PNG_775_expressions_suivent.png", "Renommer dans l\u2019IHM : les expressions suivent", None),
         ("PNG_777_compiler_expressions_impossibles.png", "Compiler : les expressions impossibles", PANE),
         ("PNG_778_fx_infobulle.png", "Le fx, bien visible, et son infobulle", PANE),
         ("PNG_779_glisser_classeur.png", "Glisser un classeur : ce qu\u2019on peut en faire", None),
         ("PNG_781_themes_famille_clairs.png", "43 th\u00e8mes, par famille", None),
         ("PNG_782_theme_editeur.png", "L\u2019\u00e9diteur de th\u00e8me", None),
         ("PNG_784_filtres_retenus.png", "Chercher : les filtres retenus", PANE),
         ("PNG_786_dialogue_fenetre_detachee.png", "Les dialogues dans la fen\u00eatre d\u00e9tach\u00e9e", None),
         ("PNG_790_explorateur_vignettes.png", "L\u2019explorateur de fichiers de l\u2019appli", None),
         ("PNG_809_arbre_filtre.png", "L\u2019arbre : le filtre, les pastilles, le rail", (0, 0, 640, 760)),
         ("PNG_802_cloche_notifications.png", "Le bandeau haut : la cloche", (700, 0, 1920, 600)),
         ("PNG_806_journal_des_messages.png", "La barre d\u2019\u00e9tat : le journal des messages", (0, 760, 1920, 1080)),
         ("PNG_794_aide_nouveautes_lot8.png", "Aide \u203a Nouveaut\u00e9s du lot 8", PANE)]
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
d.text((pad - 10, 14), "Lot API 8 : le dossier Simulation et le d\u00e9bogage, renommer partout, les th\u00e8mes, glisser un fichier, l\u2019explorateur, l\u2019arbre, les bandeaux",
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
