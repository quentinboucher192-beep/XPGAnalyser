#!/usr/bin/env python3
# La planche du lot API 7 (PLANCHE_API_LOT7.png) : l'accueil ; la simulation et les
# statistiques dans l'API ; le menu des onglets, les groupes, la mosaique, une
# fenetre detachee ; deballer a toute profondeur ; renommer ; importer un MAST ;
# chercher ; le clic droit de l'arbre ; le didacticiel de l'API.
#   python3 planche-api-lot7.py <dossier des captures> [planche.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else src + "/PLANCHE_API_LOT7.png"
# (capture, legende, recadrage) : None = l'ecran entier, sinon (x0, y0, x1, y1)
PANE = (330, 40, 1920, 1080)
items = [("PNG_720_accueil.png", "L\u2019accueil refait : logos, projets, détail", None),
         ("PNG_724_simulation_onglet.png", "API \u203a Simulation (F9)", PANE),
         ("PNG_726_statistiques.png", "API \u203a Statistiques (Ctrl+5)", PANE),
         ("PNG_729_groupes_cote_a_cote.png", "Les onglets côte à côte", None),
         ("PNG_730_mosaique.png", "La mosaïque automatique", None),
         ("PNG_732_fenetre_detachee.png", "Un onglet dans sa fenêtre", None),
         ("PNG_728_menu_onglet.png", "Clic droit sur un onglet", PANE),
         ("PNG_735_dfb_choix_section.png", "Blocs DFB : déplier, puis quelle section ouvrir", PANE),
         ("PNG_736_unites_dossiers_portee.png", "Unités : les variables par portée", PANE),
         ("PNG_743_renommer_tout.png", "Renommer : tout ce qui change, API et IHM", None),
         ("PNG_745_import_mast_recap.png", "Importer un .XPG : le récapitulatif", None),
         ("PNG_747_glisser_xpg_xhw.png", "Glisser un export n\u2019importe où", None),
         ("PNG_738_filtre_colonne.png", "Chercher : les filtres par colonne", PANE),
         ("PNG_740_aller_a_partout.png", "Aller à\u2026 : partout", None),
         ("PNG_741_clic_droit_unite.png", "Le clic droit de l\u2019arbre, par endroit", None),
         ("PNG_751_ihm_variables_systeme_dossiers.png", "IHM : les variables système en dossiers", PANE),
         ("PNG_752_exporter_csv_parcourir.png", "Le bouton \u2026 des imports et exports", None),
         ("PNG_748_didacticiel_api.png", "Le didacticiel de l\u2019API", PANE)]
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
d.text((pad - 10, 14), "Lot API 7 : la simulation et les statistiques dans l\u2019API, l\u2019accueil, le multi-fenêtre, renommer, importer un MAST, chercher partout",
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
