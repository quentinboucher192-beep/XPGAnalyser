#!/usr/bin/env python3
"""
Construit le classeur unique : E/S, equipements, reports, cartographie.

REPRIS DE ZERO COTE MISE EN PAGE. Les largeurs de colonnes de la version
precedente venaient d'une estimation a vue ; ici chaque colonne declare la
longueur du texte qu'elle doit contenir, et la largeur s'en deduit. Les hauteurs
de lignes sont fixees une fois par type de ligne plutot que laissees a Excel, qui
les ajuste au contenu et donne un tableau dont chaque ligne fait une taille
differente.

UN SEUL FICHIER. Les E/S et les reports vivaient dans deux classeurs qui
devaient s'ouvrir l'un l'autre pour tenir un lien a jour - trois cas a gerer, un
chemin a renseigner, et un etat incoherent possible si l'un des deux n'etait pas
accessible. Tout ce mecanisme disparait : un lien est maintenant une ligne dans
une feuille du meme classeur.

LES QUATRE FAMILLES D'E/S DANS UN SEUL ONGLET. Elles partagent la moitie de
leurs colonnes - carte, voie, adresse, tableau, index - et ce qui les separe
tient dans des blocs repliables. Quatre onglets obligeaient a chercher dans
lequel se trouvait une voie avant de pouvoir la regarder.
"""

import datetime
import os

from openpyxl import Workbook
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.utils import get_column_letter
from openpyxl.workbook.defined_name import DefinedName
from openpyxl.worksheet.datavalidation import DataValidation
from openpyxl.chart import BarChart, Reference, ScatterChart, Series
from openpyxl.worksheet.properties import PageSetupProperties

OUT = "automation.xlsx"
AFFAIRE = "2024_06_264"
CLIENT = "ALI"
INDICE = "A"

# ---------------------------------------------------------------------------
#  Les tokens de presentation. Un seul endroit, pour que deux onglets ne
#  puissent pas diverger sur la taille d'un titre.
# ---------------------------------------------------------------------------
FONT_NAME = "Calibri"

NAVY = "1F4E79"
SLATE = "44546A"
GREY_LINE = "D0D0D0"

# Les quatre familles, et leur couleur. Elle sert partout : la colonne Famille
# de l'onglet E/S, les cartes de l'onglet Cartes API, la legende. Une carte DI
# est bleue a tous les endroits ou on la voit.
FAMILY = {
    "DI": ("Entrees TOR",   "2E75B6", "DDEBF7"),
    "DO": ("Sorties TOR",   "548235", "E2EFDA"),
    "AI": ("Entrees ANA",   "BF8F00", "FFF2CC"),
    "AO": ("Sorties ANA",   "7030A0", "E9DFF5"),
}

F_TITLE = Font(name=FONT_NAME, size=15, bold=True, color="FFFFFF")
F_SUBTITLE = Font(name=FONT_NAME, size=9, color="D6DCE4")
F_LABEL = Font(name=FONT_NAME, size=9, bold=True)
F_HEAD = Font(name=FONT_NAME, size=9, bold=True, color="FFFFFF")
F_SUB = Font(name=FONT_NAME, size=7, color="D6DCE4")
F_DATA = Font(name=FONT_NAME, size=9)
F_CALC = Font(name=FONT_NAME, size=9, color="404040")
F_NOTE = Font(name=FONT_NAME, size=8, italic=True, color="808080")
F_GROUP = Font(name=FONT_NAME, size=8, bold=True, color="FFFFFF")

FILL_TITLE = PatternFill("solid", fgColor=NAVY)
FILL_HEAD = PatternFill("solid", fgColor=SLATE)
FILL_INPUT = PatternFill("solid", fgColor="FFFDE7")     # a saisir
FILL_LIST = PatternFill("solid", fgColor="EAF4EA")      # a choisir dans une liste
FILL_CALC = PatternFill("solid", fgColor="F4F4F4")      # calcule, ne pas toucher

SIDE = Side(style="thin", color=GREY_LINE)
BOX = Border(left=SIDE, right=SIDE, top=SIDE, bottom=SIDE)

# Hauteurs, fixees. Excel ajuste au contenu si on ne dit rien, et un tableau
# dont chaque ligne fait une taille differente se lit mal.
H_TITLE = 26
H_INFO = 16
H_GROUP = 15
H_HEAD = 30
H_SUB = 13
H_DATA = 15

ROW_TITLE = 1        # bandeau
ROW_INFO1 = 3        # affaire / client
ROW_INFO2 = 4        # indice / date
ROW_GROUP = 6        # bande des groupes
ROW_HEAD = 7         # titres de colonnes
ROW_SUB = 8          # sous-titres
ROW_FIRST = 9        # premiere ligne de donnees


def width_for(head, sub, sample):
    """La largeur d'une colonne, deduite de ce qu'elle doit contenir.

    Le titre passe sur deux lignes (l'en-tete fait 30 points de haut), donc sa
    moitie suffit ; le sous-titre et l'echantillon, eux, tiennent sur une ligne.
    Le +2 paie les bordures et la fleche du filtre.
    """
    longest = max(len(str(head)) / 2.0, len(str(sub or "")), len(str(sample or "")))
    return max(6.0, min(46.0, longest + 2.4))


class Col:
    """Une colonne : son groupe, son titre, son aide, et ce qu'on y met."""

    def __init__(self, group, head, sub="", kind="input", sample="", choices=None,
                 formula=None, width=None):
        self.group = group
        self.head = head
        self.sub = sub
        self.kind = kind          # input | list | calc | fixed
        self.sample = sample
        self.choices = choices
        self.formula = formula    # callable(row) -> str
        self.width = width or width_for(head, sub, sample)

    @property
    def fill(self):
        return {"input": FILL_INPUT, "list": FILL_LIST,
                "calc": FILL_CALC, "fixed": None}[self.kind]


def banner(ws, title, subtitle, width):
    """Le bandeau commun : titre, affaire, indice, date."""
    ws.row_dimensions[ROW_TITLE].height = H_TITLE
    ws.row_dimensions[ROW_TITLE + 1].height = H_INFO
    ws.merge_cells(start_row=ROW_TITLE, start_column=1,
                   end_row=ROW_TITLE, end_column=max(4, width))
    c = ws.cell(row=ROW_TITLE, column=1, value="  " + title)
    c.font = F_TITLE
    c.alignment = Alignment(vertical="center")
    for col in range(1, width + 1):
        ws.cell(row=ROW_TITLE, column=col).fill = FILL_TITLE

    ws.merge_cells(start_row=ROW_TITLE + 1, start_column=1,
                   end_row=ROW_TITLE + 1, end_column=max(4, width))
    c = ws.cell(row=ROW_TITLE + 1, column=1, value="  " + subtitle)
    c.font = F_SUBTITLE
    c.alignment = Alignment(vertical="center")
    for col in range(1, width + 1):
        ws.cell(row=ROW_TITLE + 1, column=col).fill = FILL_TITLE

    for row, pairs in ((ROW_INFO1, (("Affaire", AFFAIRE), ("Client", CLIENT))),
                       (ROW_INFO2, (("Indice", INDICE),
                                    ("Le", datetime.date.today().strftime("%d/%m/%Y"))))):
        ws.row_dimensions[row].height = H_INFO
        col = 1
        for label, value in pairs:
            lc = ws.cell(row=row, column=col, value=label)
            lc.font = F_LABEL
            vc = ws.cell(row=row, column=col + 1, value=value)
            vc.font = F_DATA
            vc.fill = FILL_INPUT
            vc.border = BOX
            col += 3


def headers(ws, cols, group_colors=None):
    """La bande de groupes, les titres, les sous-titres, les largeurs."""
    ws.row_dimensions[ROW_GROUP].height = H_GROUP
    ws.row_dimensions[ROW_HEAD].height = H_HEAD
    ws.row_dimensions[ROW_SUB].height = H_SUB

    palette = ["2E75B6", "548235", "BF8F00", "7030A0", "C55A11", "1F4E79", "7F7F7F"]
    seen = {}
    for i, col in enumerate(cols, start=1):
        ws.column_dimensions[get_column_letter(i)].width = col.width

        h = ws.cell(row=ROW_HEAD, column=i, value=col.head)
        h.font = F_HEAD
        h.fill = FILL_HEAD
        h.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
        h.border = BOX

        sub = col.sub
        if col.kind == "list" and sub and "liste" not in sub.lower():
            # La fleche d'une liste n'apparait que sur la cellule selectionnee.
            # C'est Excel, et aucune macro n'y change rien : le seul moyen de le
            # signaler est de l'ecrire.
            sub = sub + " \u25be"
        elif col.kind == "list" and not sub:
            sub = "\u25be liste"
        s = ws.cell(row=ROW_SUB, column=i, value=sub)
        s.font = F_SUB
        s.fill = FILL_HEAD
        s.alignment = Alignment(horizontal="center", vertical="center")
        s.border = BOX

        if col.group:
            seen.setdefault(col.group, []).append(i)

    colors = group_colors or {}
    for n, (group, columns) in enumerate(seen.items()):
        color = colors.get(group, palette[n % len(palette)])
        lo, hi = min(columns), max(columns)
        if hi > lo:
            ws.merge_cells(start_row=ROW_GROUP, start_column=lo,
                           end_row=ROW_GROUP, end_column=hi)
        g = ws.cell(row=ROW_GROUP, column=lo, value=group)
        g.font = F_GROUP
        g.alignment = Alignment(horizontal="center", vertical="center")
        for c in range(lo, hi + 1):
            ws.cell(row=ROW_GROUP, column=c).fill = PatternFill("solid", fgColor=color)

    ws.freeze_panes = ws.cell(row=ROW_FIRST, column=1)
    ws.auto_filter.ref = "A%d:%s%d" % (ROW_HEAD, get_column_letter(len(cols)), ROW_HEAD)


def body(ws, cols, rows):
    """Des lignes vides, formatees, pretes a recevoir."""
    for r in range(ROW_FIRST, ROW_FIRST + rows):
        ws.row_dimensions[r].height = H_DATA
        for i, col in enumerate(cols, start=1):
            c = ws.cell(row=r, column=i)
            c.border = BOX
            c.font = F_CALC if col.kind == "calc" else F_DATA
            if col.fill:
                c.fill = col.fill
            if col.formula:
                c.value = col.formula(r)
            if col.choices:
                pass   # pose en une fois plus bas, plus rapide


def validations(ws, cols, rows):
    last = ROW_FIRST + rows - 1
    for i, col in enumerate(cols, start=1):
        if not col.choices:
            continue
        letter = get_column_letter(i)
        dv = DataValidation(type="list", formula1=col.choices, allow_blank=True)
        ws.add_data_validation(dv)
        dv.add("%s%d:%s%d" % (letter, ROW_FIRST, letter, last))


def collapse(ws, cols, groups):
    """Replie les blocs cites, fermes a l'ouverture."""
    for group in groups:
        columns = [i for i, c in enumerate(cols, start=1) if c.group == group]
        if not columns:
            continue
        for i in columns:
            ws.column_dimensions[get_column_letter(i)].outline_level = 1
            ws.column_dimensions[get_column_letter(i)].hidden = True
    ws.sheet_properties.outlinePr.summaryRight = True


def print_setup(ws, last_col, last_row, title, wide=False):
    ws.page_setup.orientation = "landscape"
    ws.page_setup.paperSize = ws.PAPERSIZE_A3 if wide else ws.PAPERSIZE_A4
    ws.page_setup.fitToWidth = 1
    ws.page_setup.fitToHeight = 0
    ws.sheet_properties.pageSetUpPr = PageSetupProperties(fitToPage=True)
    ws.print_title_rows = "1:%d" % ROW_SUB
    ws.print_area = "A1:%s%d" % (get_column_letter(last_col), last_row)
    ws.print_options.horizontalCentered = True
    ws.page_margins.left = 0.35
    ws.page_margins.right = 0.35
    ws.page_margins.top = 0.45
    ws.page_margins.bottom = 0.5
    ws.oddFooter.left.text = "Affaire %s - %s" % (AFFAIRE, title)
    ws.oddFooter.left.size = 7
    ws.oddFooter.center.text = "&D"
    ws.oddFooter.center.size = 7
    ws.oddFooter.right.text = "page &P / &N"
    ws.oddFooter.right.size = 7


def note(ws, row, text, col=None, cols=None):
    """Une note explicative, ECRITE LOIN DES DONNEES.

    Elles etaient en colonne A, sous le bloc. LastRow remonte du bas avec
    End(xlUp) : il tombait sur la note et rendait SA ligne. Un equipement ajoute
    partait donc trois cents lignes plus bas, hors du bloc formate et hors du
    filtre - present dans le fichier, invisible a l'ecran.

    Elles vivent maintenant deux colonnes apres la derniere colonne de donnees,
    ou aucun End(xlUp) ne va les chercher.
    """
    # Colonne 70 par defaut : au-dela de toutes les colonnes de donnees de tous
    # les onglets - le plus large, la cartographie, en compte 65. Aucun
    # End(xlUp) sur une colonne de donnees ne peut plus tomber dessus.
    if col is None:
        col = cols + 2 if cols else 70
    c = ws.cell(row=row, column=col, value=text)
    c.font = F_NOTE
    c.alignment = Alignment(vertical="top")
    return c


def named(wb, name, sheet, ref):
    wb.defined_names.add(DefinedName(name, attr_text="'%s'!%s" % (sheet, ref)))


# ===========================================================================
#  1. Config
# ===========================================================================
CONFIG_FIELDS = [
    ("Dossier libs", "C:\\Affaires\\2024_06_264\\libs",
     "LE SEUL CHAMP INDISPENSABLE. La macro INIT lit ce dossier, ouvre chaque "
     ".ddt et .dfb, et remplit l'onglet Catalogue. Tout le reste en decoule."),
    ("Automate", "BMX P34 2020", "la reference CPU, reprise sur l'onglet Cartes API"),
    ("Tache", "MAST", "la tache des sections generees"),
    ("Periode (ms)", 20, "sa periode : les blocs en tirent leurs temporisations"),
    ("Zone MW debut", 1000, "premiere adresse mot utilisable pour les reports"),
    ("Zone MW fin", 1999, "derniere"),
    ("Zone MX debut", 0, "premiere adresse bit"),
    ("Zone MX fin", 511, "derniere"),
]


def sheet_config(wb):
    ws = wb.create_sheet("Config")
    banner(ws, "CONFIGURATION", "Le point de depart : renseignez le dossier libs, "
                                "puis lancez INIT.", 9)
    ws.column_dimensions["A"].width = 3
    ws.column_dimensions["B"].width = 20
    ws.column_dimensions["C"].width = 40
    ws.column_dimensions["D"].width = 2
    ws.column_dimensions["E"].width = 62
    ws.column_dimensions["F"].width = 2
    ws.column_dimensions["G"].width = 28

    r = ROW_GROUP
    for label, value, help_text in CONFIG_FIELDS:
        ws.row_dimensions[r].height = 22
        lc = ws.cell(row=r, column=2, value=label)
        lc.font = F_LABEL
        lc.alignment = Alignment(vertical="center")
        vc = ws.cell(row=r, column=3, value=value)
        vc.fill = FILL_INPUT
        vc.border = BOX
        vc.font = F_DATA
        vc.alignment = Alignment(vertical="center")
        hc = ws.cell(row=r, column=5, value=help_text)
        hc.font = F_NOTE
        hc.alignment = Alignment(wrap_text=True, vertical="center")
        r += 1

    named(wb, "CheminLibs", "Config", "$C$%d" % ROW_GROUP)
    named(wb, "MW_Debut", "Config", "$C$%d" % (ROW_GROUP + 4))
    named(wb, "MW_Fin", "Config", "$C$%d" % (ROW_GROUP + 5))
    named(wb, "MX_Debut", "Config", "$C$%d" % (ROW_GROUP + 6))
    named(wb, "MX_Fin", "Config", "$C$%d" % (ROW_GROUP + 7))

    # Le mode d'emploi, a cote des boutons que la macro posera en G.
    steps = [
        ("1", "INIT", "lit le dossier libs, remplit le Catalogue"),
        ("2", "Cartes API", "declarez vos cartes, les voies se creent"),
        ("3", "E/S", "double-clic sur une voie pour la regler"),
        ("4", "Ajouter equipement", "famille, voies, adresses de report"),
        ("5", "Tout recalculer", "reports, cartographie, ST"),
    ]
    r += 2
    ws.cell(row=r, column=2, value="MARCHE A SUIVRE").font = Font(
        name=FONT_NAME, size=11, bold=True, color=NAVY)
    r += 1
    for num, titre, quoi in steps:
        ws.row_dimensions[r].height = 18
        a = ws.cell(row=r, column=2, value=num + ".  " + titre)
        a.font = F_LABEL
        b = ws.cell(row=r, column=3, value=quoi)
        b.font = F_NOTE
        ws.merge_cells(start_row=r, start_column=3, end_row=r, end_column=5)
        r += 1

    r += 1
    note(ws, r, "Rien n'est obligatoire dans l'ordre : on peut declarer des voies sans "
                "equipement, un equipement sans report, un report sans voie. Les liens "
                "se font quand ils ont un sens.", col=2)
    ws.merge_cells(start_row=r, start_column=2, end_row=r + 1, end_column=5)
    ws.cell(row=r, column=2).alignment = Alignment(wrap_text=True, vertical="top")

    tableau_de_bord(wb, ws, r + 2)
    print_setup(ws, 7, r + 2, "Configuration")
    return ws


# ---------------------------------------------------------------------------
#  Le tableau de bord.
#
#  PAS UN GRAPHIQUE DECORATIF. Les six nombres qu'on veut voir en arrivant, et
#  un histogramme qui repond a la seule question qu'on se pose vraiment devant
#  une plage memoire : ou reste-t-il de la place.
#
#  Tout en FORMULES, aucune macro : un tableau de bord qu'il faut penser a
#  rafraichir est un tableau de bord qui ment la moitie du temps.
# ---------------------------------------------------------------------------
def tableau_de_bord(wb, ws, top):
    ws.cell(row=top, column=2, value="TABLEAU DE BORD").font = Font(
        name=FONT_NAME, size=11, bold=True, color=NAVY)

    lignes = [
        ("Cartes declarees", '=COUNTIF(\'Cartes API\'!$A$9:$A$48,"?*")'),
        ("Voies declarees", '=COUNTIF(ES!$B$9:$B$608,"?*")'),
        ("  dont entrees TOR", '=COUNTIF(ES!$A$9:$A$608,"DI")'),
        ("  dont sorties TOR", '=COUNTIF(ES!$A$9:$A$608,"DO")'),
        ("  dont entrees ANA", '=COUNTIF(ES!$A$9:$A$608,"AI")'),
        ("  dont sorties ANA", '=COUNTIF(ES!$A$9:$A$608,"AO")'),
        ("Voies cablees", '=COUNTIF(Cablage!$A$9:$A$608,"?*")'),
        ("Voies libres", '=COUNTIF(ES!$B$9:$B$608,"?*")-COUNTIF(Cablage!$A$9:$A$608,"?*")'),
        ("Equipements", '=COUNTIF(Equipements!$A$9:$A$308,"?*")'),
        ("Reports", '=COUNTIF(Liens!$D$9:$D$808,"?*")'),
        ("  informations (TM)", '=COUNTIF(Liens!$F$9:$F$808,"TM")'),
        ("  alarmes (TA)", '=COUNTIF(Liens!$F$9:$F$808,"TA")'),
        ("  commandes (TC)", '=COUNTIF(Liens!$F$9:$F$808,"TC")'),
        ("Mots %MW occupes", '=SUMPRODUCT((Liens!$G$9:$G$808="MW")*(Liens!$H$9:$H$808<>""))'),
        ("Bits %MX occupes", '=SUMPRODUCT((Liens!$G$9:$G$808="MX")*(Liens!$H$9:$H$808<>""))'),
        ("Conflits d'adresse", "=_ETAT!$B$6"),
    ]
    r = top + 1
    for label, formule in lignes:
        ws.row_dimensions[r].height = 15
        lc = ws.cell(row=r, column=2, value=label)
        lc.font = F_DATA if label.startswith("  ") else F_LABEL
        vc = ws.cell(row=r, column=3, value=formule)
        vc.font = F_CALC
        vc.fill = FILL_CALC
        vc.border = BOX
        vc.alignment = Alignment(horizontal="center")
        r += 1

    # Le conflit en rouge : c'est la seule ligne sur laquelle on veut buter.
    ws.cell(row=r - 1, column=3).font = Font(
        name=FONT_NAME, size=9, bold=True, color="C00000")

    # ---- l'histogramme d'occupation, par tranche de cent mots -------------
    base = top + 1
    ws.cell(row=base, column=12, value="Tranche").font = F_LABEL
    ws.cell(row=base, column=13, value="Occupes").font = F_LABEL
    # Les tranches partent de MW_Debut, pas de zero : un histogramme de mille
    # tranches vides suivi de la seule qui compte n'apprend rien.
    for i in range(20):
        off = i * 100
        ws.cell(row=base + 1 + i, column=12,
                value='=IF(MW_Debut+{off}>MW_Fin,"",(MW_Debut+{off})&"-"&'
                      'MIN(MW_Debut+{hi},MW_Fin))'.format(off=off, hi=off + 99))
        ws.cell(row=base + 1 + i, column=13,
                value='=IF(MW_Debut+{off}>MW_Fin,"",'
                      'SUMPRODUCT((Liens!$G$9:$G$808="MW")'
                      '*(Liens!$H$9:$H$808>=MW_Debut+{off})'
                      '*(Liens!$H$9:$H$808<=MIN(MW_Debut+{hi},MW_Fin))))'
                      .format(off=off, hi=off + 99))

    chart = BarChart()
    chart.type = "col"
    chart.title = "Occupation %MW par tranche de 100, depuis le debut de la plage"
    chart.y_axis.title = "mots occupes"
    chart.height = 7.5
    chart.width = 20
    chart.legend = None
    data = Reference(ws, min_col=13, min_row=base, max_row=base + 20)
    cats = Reference(ws, min_col=12, min_row=base + 1, max_row=base + 20)
    chart.add_data(data, titles_from_data=True)
    chart.set_categories(cats)
    ws.add_chart(chart, "E%d" % (top + 1))

    # Les deux colonnes de travail sont poussees loin a droite plutot que
    # masquees : masquer une colonne empeche Excel de la TRACER.
    ws.column_dimensions["L"].width = 9
    ws.column_dimensions["M"].width = 9
    note(ws, top, "Les chiffres sont des formules : ils ne peuvent pas etre perimes.",
         col=12)


# ===========================================================================
#  2. Cartes API
# ===========================================================================
CARD_COLS = [
    Col("Carte",    "Nom",       "libre",       "input", "DI_R0S4"),
    Col("Carte",    "Rack",      "0..7",        "list",  "0", '"0,1,2,3,4,5,6,7"'),
    Col("Carte",    "Module",    "0..15",       "input", "4"),
    Col("Carte",    "Famille",   "",            "list",  "DI", '"DI,DO,AI,AO"'),
    Col("Carte",    "Reference", "DDI 1602",    "input", "BMX DDI 1602"),
    Col("Carte",    "Voies",     "1..64",       "input", "16"),
    Col("Carte",    "Active",    "O/N",         "list",  "O", '"O,N"'),
    Col("Programme", "Tableau",  "nom ST",      "input", "CarteDI_R0S4"),
    Col("Programme", "Instance", "du DFB",      "input", "IO_DI_R0S4"),
    Col("Calcule",  "Prefixe",   "",            "calc",  "%I",
        formula=lambda r: '=IF(D{r}="","",IF(D{r}="DI","%I",IF(D{r}="DO","%Q",'
                          'IF(D{r}="AI","%IW","%QW"))))'.format(r=r)),
    Col("Calcule",  "Premiere",  "",            "calc",  "%I0.4.0",
        formula=lambda r: '=IF(OR(J{r}="",B{r}="",C{r}=""),"",J{r}&B{r}&"."&C{r}&".0")'
                          .format(r=r)),
    Col("Calcule",  "Derniere",  "",            "calc",  "%I0.4.15",
        formula=lambda r: '=IF(OR(J{r}="",F{r}=""),"",J{r}&B{r}&"."&C{r}&"."&(F{r}-1))'
                          .format(r=r)),
    Col("Calcule",  "Bloc",      "taille",      "calc",  "DFB_IO_DIG16",
        formula=lambda r: '=IF(F{r}="","",IF(OR(D{r}="DI",D{r}="DO"),"DFB_IO_DIG",'
                          '"DFB_IO_ANA")&TEXT(IF(F{r}<=4,4,IF(F{r}<=8,8,IF(F{r}<=16,16,'
                          'IF(F{r}<=32,32,IF(F{r}<=64,64,"?"))))),"00"))'.format(r=r)),
    Col("Calcule",  "Declarees", "voies creees", "calc", "16",
        formula=lambda r: '=IF(A{r}="","",COUNTIF(ES!$B:$B,A{r}))'.format(r=r)),
    Col("Calcule",  "Etat",      "",            "calc",  "complete",
        formula=lambda r: '=IF(A{r}="","",IF(N{r}=0,"aucune voie",'
                          'IF(N{r}<F{r},"il en manque "&(F{r}-N{r}),'
                          'IF(N{r}>F{r},"TROP : "&(N{r}-F{r}),"complete"))))'.format(r=r)),
    Col("Documentation", "Commentaire", "",     "input", "armoire A, porte gauche"),
]

CARD_EXAMPLES = [
    ("DI_R0S4", 0, 4, "DI", "BMX DDI 1602", 16, "O", "CarteDI_R0S4", "IO_DI_R0S4"),
    ("DO_R0S5", 0, 5, "DO", "BMX DRA 1605", 16, "O", "CarteDO_R0S5", "IO_DO_R0S5"),
    ("DI_R0S7", 0, 7, "DI", "BMX DDI 1602", 16, "O", "CarteDI_R0S7", "IO_DI_R0S7"),
    ("DO_R0S8", 0, 8, "DO", "BMX DRA 1605", 16, "O", "CarteDO_R0S8", "IO_DO_R0S8"),
    ("AI_R0S10", 0, 10, "AI", "BMX AMI 0810", 8, "O", "CarteAI_R0S10", "IO_AI_R0S10"),
    ("AI_R0S11", 0, 11, "AI", "BMX AMI 0810", 8, "O", "CarteAI_R0S11", "IO_AI_R0S11"),
]


def sheet_cards(wb):
    ws = wb.create_sheet("Cartes API")
    width = len(CARD_COLS)
    banner(ws, "CARTES DE L'AUTOMATE",
           "Declarez une carte, ses voies se creent dans l'onglet E/S. "
           "La vue du rack est en dessous du tableau.", width)
    headers(ws, CARD_COLS)
    rows = 40
    body(ws, CARD_COLS, rows)
    validations(ws, CARD_COLS, rows)

    for i, ex in enumerate(CARD_EXAMPLES):
        for j, v in enumerate(ex, start=1):
            ws.cell(row=ROW_FIRST + i, column=j, value=v)

    last = ROW_FIRST + rows - 1
    named(wb, "CartesNoms", "Cartes API", "$A$%d:$A$%d" % (ROW_FIRST, last))

    note(ws, last + 2,
         "Le bouton Creer les voies ajoute dans l'onglet E/S ce qui manque pour chaque "
         "carte active. Il n'efface jamais : une voie deja reglee reste telle quelle.")
    note(ws, last + 3,
         "La colonne Etat compare les voies declarees ici et celles qui existent "
         "vraiment. \"TROP\" veut dire qu'il reste des voies d'une carte reduite.")

    print_setup(ws, width, last + 3, "Cartes de l'automate", wide=True)
    return ws


def sheet_rack(wb):
    """La vue du rack, sur SON onglet.

    Elle etait sous le tableau des cartes, et les deux se disputaient les
    largeurs : une case de rack veut treize caracteres, une colonne "Rack" en
    veut cinq et une colonne "Reference" vingt. Le tableau perdait a chaque
    regeneration, sans que rien ne le dise.

    POURQUOI UNE VUE ET PAS SEULEMENT LE TABLEAU. Un tableau dit ce qu'on a
    saisi ; un rack dit ce qu'on a OUBLIE. Un emplacement vide entre deux cartes
    se voit en une seconde ici, et pas du tout dans une liste triee par nom.
    """
    ws = wb.create_sheet("Vue Rack")
    banner(ws, "VUE DES RACKS",
           "Une case par emplacement. Le bouton Rafraichir la vue la remplit "
           "depuis l'onglet Cartes API.", 18)

    ws.column_dimensions["A"].width = 10
    for slot in range(16):
        ws.column_dimensions[get_column_letter(2 + slot)].width = 14

    # Cinq lignes par emplacement : nom, famille, reference, voies, etat. C'est
    # le minimum pour repondre a "qu'est-ce qu'il y a la" sans aller voir
    # ailleurs.
    LIGNES = ("nom", "famille", "reference", "voies", "etat")
    start = ROW_FIRST
    for rack in range(8):
        base = start + rack * 7
        t = ws.cell(row=base, column=1, value="Rack %d" % rack)
        t.font = Font(name=FONT_NAME, size=10, bold=True, color="FFFFFF")
        t.fill = FILL_HEAD
        t.alignment = Alignment(horizontal="center", vertical="center")
        ws.row_dimensions[base].height = 16

        for slot in range(16):
            col = 2 + slot
            head = ws.cell(row=base, column=col, value=slot)
            head.font = Font(name=FONT_NAME, size=8, bold=True, color="FFFFFF")
            head.fill = FILL_HEAD
            head.alignment = Alignment(horizontal="center", vertical="center")
            head.border = BOX
            for k, quoi in enumerate(LIGNES, start=1):
                ws.row_dimensions[base + k].height = 13
                c = ws.cell(row=base + k, column=col)
                c.border = BOX
                c.font = Font(name=FONT_NAME, size=8)
                c.alignment = Alignment(horizontal="center", vertical="center")
        # la legende des cinq lignes, a gauche
        for k, quoi in enumerate(LIGNES, start=1):
            g = ws.cell(row=base + k, column=1, value=quoi)
            g.font = Font(name=FONT_NAME, size=7, italic=True, color="808080")
            g.alignment = Alignment(horizontal="right", vertical="center")

    last = start + 8 * 7
    # la legende des couleurs
    ws.cell(row=last + 1, column=1, value="Legende").font = F_LABEL
    for i, (code, (nom, fort, doux)) in enumerate(FAMILY.items()):
        c = ws.cell(row=last + 2 + i, column=1, value=code)
        c.fill = PatternFill("solid", fgColor=fort)
        c.font = Font(name=FONT_NAME, size=8, bold=True, color="FFFFFF")
        c.alignment = Alignment(horizontal="center")
        ws.cell(row=last + 2 + i, column=2, value=nom).font = F_NOTE

    note(ws, last + 7, "Un emplacement vide entre deux cartes se voit ici en une "
                       "seconde. Dans une liste triee par nom, pas du tout.")
    named(wb, "VueRack", "Vue Rack", "$A$%d" % start)
    print_setup(ws, 18, last + 7, "Vue des racks", wide=True)
    return ws


# ===========================================================================
#  3. E/S : les quatre familles dans un seul onglet.
#
#  Elles partagent la moitie de leurs colonnes. Ce qui les separe - les reglages
#  TOR d'un cote, l'echelle et les quatre seuils de l'autre - vit dans des blocs
#  REPLIABLES, fermes a l'ouverture. On voit donc par defaut les dix colonnes qui
#  valent pour tout le monde, et on ouvre le bloc dont on a besoin.
# ===========================================================================
def es_columns():
    cols = [
        Col("Voie", "Famille", "DI/DO/AI/AO", "list", "DI", '"DI,DO,AI,AO"'),
        Col("Voie", "Carte", "", "list", "DI_R0S4", "=CartesNoms"),
        Col("Voie", "Voie", "0..63", "input", "0"),
        Col("Voie", "Designation", "ce que c'est", "input",
            "Presence 24V armoire A", width=34),
        Col("Voie", "Repere", "schema", "input", "S12"),
        Col("Voie", "Adresse", "", "calc", "%I0.4.0",
            formula=lambda r: '=IFERROR(IF(OR($B{r}="",$C{r}=""),"",'
                              'INDEX(\'Cartes API\'!$J:$J,MATCH($B{r},\'Cartes API\'!$A:$A,0))'
                              '&INDEX(\'Cartes API\'!$B:$B,MATCH($B{r},\'Cartes API\'!$A:$A,0))'
                              '&"."&INDEX(\'Cartes API\'!$C:$C,MATCH($B{r},\'Cartes API\'!$A:$A,0))'
                              '&"."&$C{r}),"")'.format(r=r)),
        Col("Voie", "Tableau", "", "calc", "CarteDI_R0S4",
            formula=lambda r: '=IFERROR(IF($B{r}="","",INDEX(\'Cartes API\'!$H:$H,'
                              'MATCH($B{r},\'Cartes API\'!$A:$A,0))),"")'.format(r=r)),
        Col("Voie", "Index", "dans le tableau", "input", "0"),
        Col("Voie", "Acces ST", "", "calc", "CarteDI_R0S4[0]",
            formula=lambda r: '=IF(OR($G{r}="",$H{r}=""),"",$G{r}&"["&$H{r}&"]")'.format(r=r)),
        Col("Voie", "Equipement", "lien", "calc", "Pompe gavage A",
            formula=lambda r: '=IFERROR(INDEX(Cablage!$A:$A,MATCH($I{r},Cablage!$F:$F,0)),"")'
                              .format(r=r)),
    ]
    for name, sub, sample in (("Inv", "O/N", "N"), ("DebounceMs", "ms", "20"),
                              ("AlarmEn", "O/N", "N"), ("AlarmState", "0/1", "1"),
                              ("AlarmDelayMs", "ms", "0"), ("Sev", "1..4", "2")):
        cols.append(Col("Reglages TOR", name, sub, "input", sample))
    for name, sub, sample in (("Unite", "", "bars"), ("RawMin", "pts", "0"),
                              ("RawMax", "pts", "10000"), ("EngMin", "", "0"),
                              ("EngMax", "", "16"), ("Clamp", "O/N", "O"),
                              ("DeadBand", "", "0"), ("FiltN", "0..8", "1"),
                              ("RateMax", "u/s", "0")):
        cols.append(Col("Echelle ANA", name, sub, "input", sample))
    for k in range(4):
        for name, sub, sample in (("En", "O/N", "N"), ("Dir", "H/B", "H"),
                                  ("Val", "", "0"), ("Hyst", "", "0"),
                                  ("DelayMs", "ms", "0"), ("Sev", "1..4", "2")):
            cols.append(Col("Seuil %d" % k, "S%d_%s" % (k, name), sub, "input", sample))
    cols.append(Col("Documentation", "Commentaire", "", "input", "", width=30))
    return cols


def sheet_es(wb):
    cols = es_columns()
    ws = wb.create_sheet("ES")
    width = len(cols)
    banner(ws, "ENTREES / SORTIES",
           "Les quatre familles ensemble. Filtrez sur Famille pour n'en voir qu'une ; "
           "double-cliquez une ligne pour la regler.", width)
    group_colors = {"Reglages TOR": FAMILY["DI"][1], "Echelle ANA": FAMILY["AI"][1],
                    "Seuil 0": "7030A0", "Seuil 1": "7030A0",
                    "Seuil 2": "7030A0", "Seuil 3": "7030A0"}
    headers(ws, cols, group_colors)
    rows = 600
    body(ws, cols, rows)
    validations(ws, cols, rows)
    collapse(ws, cols, ["Reglages TOR", "Echelle ANA",
                        "Seuil 0", "Seuil 1", "Seuil 2", "Seuil 3"])

    last = ROW_FIRST + rows - 1
    named(wb, "ES_Familles", "ES", "$A$%d:$A$%d" % (ROW_FIRST, last))
    named(wb, "ES_Acces", "ES", "$I$%d:$I$%d" % (ROW_FIRST, last))

    note(ws, last + 2,
         "Une couleur par famille dans la colonne A : les quatre parties se voient "
         "sans avoir a filtrer, et le filtre les separe quand on veut travailler sur "
         "une seule.")
    note(ws, last + 3,
         "Les blocs Reglages TOR, Echelle ANA et Seuils sont replies : ouvrez avec le "
         "+ en haut celui dont vous avez besoin. Une voie TOR laisse les colonnes ANA "
         "vides, et reciproquement.")
    note(ws, last + 4,
         "La colonne Equipement se remplit toute seule quand une voie est cablee "
         "depuis l'assistant d'equipement. Elle reste vide si la voie ne sert a rien "
         "d'autre qu'a etre lue - ce qui est parfaitement admis.")

    print_setup(ws, width, last, "Entrees / sorties", wide=True)
    return ws


# ===========================================================================
#  4. Catalogue, Equipements, Cablage, Liens
# ===========================================================================
CATALOGUE_COLS = [
    Col("Origine", "Famille", "DDT ou DFB", "calc", "ST_EQ_Valve2"),
    Col("Origine", "Genre", "ddt/dfb", "calc", "ddt"),
    Col("Origine", "Fichier", "dans libs", "calc", "Equipment\\ST_EQ_Valve2.ddt", width=30),
    Col("Parametre", "Nom", "", "calc", "TravelElapsed"),
    Col("Parametre", "Type", "", "calc", "ARRAY[0..1] OF BOOL"),
    Col("Parametre", "Portee", "", "calc", "Member"),
    Col("Parametre", "Defaut", "", "calc", "5000"),
    Col("Choix", "Reportable", "O/N", "list", "O", '"O,N"'),
    Col("Choix", "Type report", "TM/TA/TC", "list", "TM", '"TM,TA,TC"'),
    Col("Choix", "Cablage E/S", "DI/DO/AI/AO", "list", "DI", '"DI,DO,AI,AO"'),
    Col("Documentation", "Commentaire", "", "calc",
        "position atteinte, -1 tant qu'aucun retour", width=44),
]


def sheet_catalogue(wb):
    ws = wb.create_sheet("Catalogue")
    width = len(CATALOGUE_COLS)
    banner(ws, "CATALOGUE DES DDT ET DFB",
           "Ecrit par INIT depuis le dossier libs. Seules les trois colonnes "
           "vertes sont a vous - et INIT ne les ecrase jamais.", width)
    headers(ws, CATALOGUE_COLS)
    rows = 900
    body(ws, CATALOGUE_COLS, rows)
    validations(ws, CATALOGUE_COLS, rows)

    last = ROW_FIRST + rows - 1
    named(wb, "CatFamilles", "Catalogue", "$A$%d:$A$%d" % (ROW_FIRST, last))
    named(wb, "CatParams", "Catalogue", "$D$%d:$D$%d" % (ROW_FIRST, last))

    ws.merge_cells(start_row=ROW_INFO1, start_column=7, end_row=ROW_INFO2, end_column=11)
    how = ws.cell(row=ROW_INFO1, column=7,
                  value="D'OU CA VIENT : INIT parcourt libs\\ et ses sous-dossiers, "
                        "ouvre chaque .ddt et .dfb - ce sont des fichiers texte - et "
                        "ecrit ici une ligne par parametre.")
    how.font = F_NOTE
    how.alignment = Alignment(wrap_text=True, vertical="center")

    note(ws, last + 2, "Reportable dit si ce parametre merite de remonter a l'IHM. "
                       "Type report dit comment : TM information, TA alarme, TC commande.")
    note(ws, last + 3, "Cablage E/S dit si ce parametre se branche sur une voie physique, "
                       "et dans quel sens. Vide veut dire qu'il n'en vient pas : un defaut "
                       "calcule par le bloc, une consigne qui vient de l'IHM.")
    print_setup(ws, width, last, "Catalogue")
    return ws


EQUIP_COLS = [
    Col("Identification", "Nom", "libre", "input", "Pompe gavage A", width=24),
    Col("Identification", "Famille", "", "list", "ST_EQ_Pump", "=CatFamilles"),
    Col("Identification", "Repere", "P1", "input", "P1"),
    Col("Identification", "Actif", "O/N", "list", "O", '"O,N"'),
    Col("Programme", "Forme", "", "list", "Tableau", '"Tableau,Variable seule"'),
    Col("Programme", "Variable", "nom ST", "input", "Pompes"),
    Col("Programme", "Index", "si tableau", "input", "0"),
    Col("Programme", "Instance", "du DFB", "input", "IO_Pompes"),
    # UNE DDT EST UN ELEMENT DE TABLEAU, OU UNE VARIABLE SEULE. Jamais les deux :
    # Pompes[0] et Pompes sont deux choses differentes, et l'ancienne formule
    # rendait une chaine vide des que l'index manquait - une variable seule
    # n'avait donc aucun acces.
    Col("Programme", "Acces ST", "", "calc", "Pompes[0]",
        formula=lambda r: '=IF(F{r}="","",IF(OR(E{r}="Variable seule",G{r}=""),'
                          'F{r},F{r}&"["&G{r}&"]"))'.format(r=r)),
    Col("Bilan", "Voies", "cablees", "calc", "3",
        formula=lambda r: '=IF(A{r}="","",COUNTIF(Cablage!$A:$A,A{r}))'.format(r=r)),
    Col("Bilan", "Reports", "", "calc", "5",
        formula=lambda r: '=IF(A{r}="","",COUNTIF(Liens!$A:$A,A{r}))'.format(r=r)),
    # Ce qui occupe deja cet index dans ce tableau, s'il y a conflit. Vide quand
    # tout va bien : une colonne de controle qui affiche "ok" partout n'apprend
    # rien et fatigue l'oeil.
    Col("Bilan", "Conflit", "index pris", "calc", "",
        formula=lambda r: '=IF(OR(A{r}="",E{r}="Variable seule",F{r}="",G{r}=""),"",'
                          'IF(COUNTIFS($F$9:$F$308,F{r},$G$9:$G$308,G{r},$A$9:$A$308,'
                          '"<>"&A{r})>0,"index deja pris",""))'.format(r=r)),
    Col("Documentation", "Commentaire", "", "input", "", width=30),
]


def sheet_equipements(wb):
    ws = wb.create_sheet("Equipements")
    width = len(EQUIP_COLS)
    banner(ws, "EQUIPEMENTS",
           "Double-cliquez une ligne pour rouvrir l'assistant et changer ses voies "
           "ou ses adresses.", width)
    headers(ws, EQUIP_COLS)
    rows = 300
    body(ws, EQUIP_COLS, rows)
    validations(ws, EQUIP_COLS, rows)
    last = ROW_FIRST + rows - 1
    named(wb, "EqNoms", "Equipements", "$A$%d:$A$%d" % (ROW_FIRST, last))
    note(ws, last + 2, "Les deux colonnes de bilan comptent ce qui est reellement cable "
                       "et reporte. Un equipement a zero des deux n'est pas une erreur : "
                       "c'est un equipement declare dont on n'a pas encore decide.")
    print_setup(ws, width, last, "Equipements")
    return ws


CABLAGE_COLS = [
    Col("Equipement", "Equipement", "", "calc", "Pompe gavage A", width=24),
    Col("Equipement", "Parametre", "", "calc", "Fbk"),
    Col("Equipement", "Cible", "", "calc", "Pompes[0].Fbk",
        formula=lambda r: '=IFERROR(IF(OR(A{r}="",B{r}=""),"",'
                          'INDEX(Equipements!$H:$H,MATCH(A{r},Equipements!$A:$A,0))'
                          '&"."&B{r}),"")'.format(r=r)),
    Col("Voie", "Sens", "DI/DO/AI/AO", "calc", "DI"),
    Col("Voie", "Ligne ES", "", "calc", "42"),
    Col("Voie", "Acces voie", "", "calc", "CarteDI_R0S4[3]", width=22),
    Col("Voie", "Attribut", "", "calc", "Val"),
    Col("Voie", "Source", "", "calc", "CarteDI_R0S4[3].Val", width=24),
    Col("Voie", "Adresse", "", "calc", "%I0.4.3"),
    Col("Voie", "Designation", "", "calc", "Retour marche P1", width=30),
]


INDICE_COLS = [
    Col("Indice", "Indice", "A, B, C...", "input", "A"),
    Col("Indice", "Date", "", "input", "12/09/2026"),
    Col("Indice", "Par", "", "input", "QM"),
    Col("Indice", "Objet de la modification", "", "input",
        "ajout de la pompe de gavage B", width=46),
]


def sheet_indices(wb):
    """Les indices a la main, et le journal ecrit par les macros.

    LES DEUX, PARCE QU'ILS NE DISENT PAS LA MEME CHOSE. L'indice dit ce qu'on a
    voulu faire et pourquoi ; le journal dit ce qui a ete fait et quand. Sur une
    affaire qui vit deux ans, c'est le second qui repond a "depuis quand cette
    adresse est-elle la", et le premier a "pourquoi".
    """
    ws = wb.create_sheet("Indices")
    width = len(INDICE_COLS)
    banner(ws, "INDICES ET JOURNAL",
           "Le haut se remplit a la main. Le bas s'ecrit tout seul.", width + 4)
    headers(ws, INDICE_COLS)
    rows = 40
    body(ws, INDICE_COLS, rows)

    ws.cell(row=ROW_FIRST, column=1, value=INDICE)
    ws.cell(row=ROW_FIRST, column=2,
            value=datetime.date.today().strftime("%d/%m/%Y"))
    ws.cell(row=ROW_FIRST, column=4, value="creation du classeur")

    # Le journal, plus bas, avec ses propres titres.
    top = ROW_FIRST + rows + 2
    ws.cell(row=top, column=1, value="JOURNAL DES OPERATIONS").font = Font(
        name=FONT_NAME, size=11, bold=True, color=NAVY)
    for i, (titre, larg) in enumerate(
            (("Quand", 17), ("Quoi", 24), ("Detail", 60), ("Par", 12)), start=1):
        c = ws.cell(row=top + 1, column=i, value=titre)
        c.font = F_HEAD
        c.fill = FILL_HEAD
        c.alignment = Alignment(horizontal="center", vertical="center")
        c.border = BOX
        if i > width:
            ws.column_dimensions[get_column_letter(i)].width = larg
    ws.row_dimensions[top + 1].height = 20

    named(wb, "JournalDebut", "Indices", "$A$%d" % (top + 2))
    note(ws, top - 1, "Le journal est ecrit par les macros : ajout et suppression "
                      "d'equipement, attribution d'adresses, recalcul. Il ne se "
                      "saisit pas.")
    print_setup(ws, width, top + 200, "Indices et journal")
    return ws


def sheet_cablage(wb):
    ws = wb.create_sheet("Cablage")
    width = len(CABLAGE_COLS)
    banner(ws, "CABLAGE DES ENTREES / SORTIES",
           "Ce que chaque voie alimente. Ecrit par l'assistant ; une ligne se retire "
           "avec le bouton Enlever le lien.", width)
    headers(ws, CABLAGE_COLS)
    rows = 600
    body(ws, CABLAGE_COLS, rows)
    last = ROW_FIRST + rows - 1
    note(ws, last + 2, "Le cablage n'est PAS un report : une voie peut alimenter un "
                       "equipement sans jamais remonter a l'IHM, et un report peut "
                       "porter sur une valeur calculee qui ne vient d'aucune voie.")
    print_setup(ws, width, last, "Cablage", wide=True)
    return ws


LIENS_COLS = [
    Col("Source", "Equipement", "", "list", "Pompe gavage A", "=EqNoms", width=24),
    Col("Source", "Parametre", "", "list", "Fault", "=CatParams"),
    Col("Source", "Chemin", "", "calc", "Pompes[0].Fault",
        formula=lambda r: '=IFERROR(IF(OR(A{r}="",B{r}=""),"",'
                          'INDEX(Equipements!$H:$H,MATCH(A{r},Equipements!$A:$A,0))'
                          '&"."&B{r}),"")'.format(r=r), width=22),
    Col("Report", "Designation", "ce que l'operateur lit", "input",
        "Pompe gavage A - defaut", width=32),
    Col("Report", "Sens", "R/W", "list", "R", '"R,W,RW"'),
    Col("Report", "Type", "TM/TA/TC", "list", "TA", '"TM,TA,TC"'),
    Col("Adresse", "Zone", "MW/MX", "list", "MW", '"MW,MX"'),
    Col("Adresse", "Numero", "", "input", "1000"),
    Col("Adresse", "Bit", "0..15", "input", "1"),
    Col("Adresse", "Adr API", "", "calc", "%MW1000",
        formula=lambda r: '=IF(OR(G{r}="",H{r}=""),"","%"&G{r}&H{r})'.format(r=r)),
    Col("Echelle", "Unite", "", "input", "bars"),
    Col("Echelle", "Mini", "", "input", "0"),
    Col("Echelle", "Maxi", "", "input", "16"),
    Col("Echelle", "Resolution", "", "input", "0.1"),
    Col("Controle", "Occupation", "", "calc", "mot entier",
        formula=lambda r: '=IF(H{r}="","",IF(I{r}="","mot entier","bit "&I{r}))'.format(r=r)),
    Col("Documentation", "Commentaire", "", "input", "", width=26),
]


def sheet_liens(wb):
    ws = wb.create_sheet("Liens")
    width = len(LIENS_COLS)
    banner(ws, "REPORTS VERS L'IHM",
           "Une ligne par report. Les trois onglets de reports en sont derives : "
           "une adresse s'ecrit ici et nulle part ailleurs.", width)
    headers(ws, LIENS_COLS)
    rows = 800
    body(ws, LIENS_COLS, rows)
    validations(ws, LIENS_COLS, rows)
    last = ROW_FIRST + rows - 1
    note(ws, last + 2, "Le tri entre les trois onglets de reports se fait tout seul : "
                       "%MX en tout-ou-rien, %MW avec un bit en bits de mots, %MW seul "
                       "en analogique.")
    print_setup(ws, width, last, "Reports", wide=True)
    return ws


# ===========================================================================
#  5. Les onglets derives : reports, cartographie, ST
# ===========================================================================
def report_sheet(wb, name, titre, cols, rows=400):
    ws = wb.create_sheet(name)
    width = len(cols)
    banner(ws, titre, "Onglet DERIVE de la feuille Liens : ne rien saisir ici.", width)
    headers(ws, cols)
    body(ws, cols, rows)
    last = ROW_FIRST + rows - 1
    note(ws, last + 2, "Le bouton Tout recalculer le remplit depuis la feuille Liens.")
    print_setup(ws, width, last, titre, wide=True)
    return ws


def dig_cols():
    return [
        Col("", "Designation", "", "calc", "Pompe gavage A - defaut", width=34),
        Col("Adr API", "Adr API", "IHM", "calc", "%MX12"),
        Col("Adr API", "Mode", "", "calc", ""),
        Col("Adr API", "Reset", "", "calc", ""),
        Col("Type", "TM", "", "calc", "X"),
        Col("Type", "TA", "", "calc", "X"),
        Col("Type", "TC", "", "calc", "X"),
        Col("Conformite API", "Animation", "", "calc", ""),
        Col("Conformite API", "Adressage", "", "calc", ""),
        Col("Conformite IHM", "Animation", "", "calc", ""),
        Col("Conformite IHM", "Alarme", "", "calc", ""),
        Col("", "Commentaire", "", "calc", "", width=28),
    ]


def bits_cols():
    return [
        Col("", "Designation", "", "calc", "Pompe gavage A - defaut", width=34),
        Col("Adr API", "MW", "", "calc", "%MW1000"),
        Col("Adr API", "MX", "bit", "calc", "1"),
        Col("Type", "TM", "", "calc", "X"),
        Col("Type", "TA", "", "calc", "X"),
        Col("Type", "TC", "", "calc", "X"),
        Col("", "RTN", "", "calc", ""),
        Col("Conformite API", "Animation", "", "calc", ""),
        Col("Conformite API", "Adressage", "", "calc", ""),
        Col("Conformite IHM", "Animation", "", "calc", ""),
        Col("Conformite IHM", "Alarme", "", "calc", ""),
        Col("", "Commentaire", "", "calc", "", width=28),
    ]


def ana_cols():
    return [
        Col("", "Designation", "", "calc", "Cuve tampon - niveau", width=34),
        Col("Adr API", "Adr API", "IHM", "calc", "%MW1010"),
        Col("Type", "TM", "", "calc", "X"),
        Col("Type", "TC", "", "calc", "X"),
        Col("Echelle", "Unite", "", "calc", "bars"),
        Col("Echelle", "mini", "", "calc", "0"),
        Col("Echelle", "maxi", "", "calc", "16"),
        Col("Echelle", "resolution", "", "calc", "0.1"),
        Col("Conformite API", "Animation", "", "calc", ""),
        Col("Conformite API", "Adressage", "", "calc", ""),
        Col("Conformite IHM", "Animation", "", "calc", ""),
        Col("", "Commentaire", "", "calc", "", width=28),
    ]


def memory_sheet(wb, name, zone, lines=64, per_row=64):
    """La cartographie, ANCREE SUR LA PLAGE DE CONFIG.

    Elle partait de zero, toujours. Quelqu'un qui travaille de 1000 a 1999
    regardait donc mille cases qui ne le concernaient pas, et les siennes
    n'etaient nulle part.

    Les en-tetes de ligne sont des FORMULES : changez la plage dans Config, la
    cartographie suit, et les lignes au-dela de la fin s'effacent d'elles-memes.
    """
    ws = wb.create_sheet(name)
    banner(ws, "CARTOGRAPHIE MEMOIRE %" + zone,
           "Une case par adresse, a partir de la plage declaree dans Config. "
           "Bleu : mot entier. Vert : des bits. Rouge : conflit.", per_row + 1)
    ws.column_dimensions["A"].width = 11
    for c in range(2, per_row + 2):
        ws.column_dimensions[get_column_letter(c)].width = 2.9

    for line in range(lines):
        r = ROW_FIRST + line
        ws.row_dimensions[r].height = 13
        h = ws.cell(row=r, column=1,
                    value='=IF({zone}_Debut+{off}>{zone}_Fin,"","%{zone}"&({zone}_Debut+{off}))'
                          .format(zone=zone, off=line * per_row))
        h.font = Font(name=FONT_NAME, size=8, bold=True, color="FFFFFF")
        h.fill = FILL_HEAD
        h.border = BOX
        h.alignment = Alignment(horizontal="center", vertical="center")
        for k in range(per_row):
            c = ws.cell(row=r, column=2 + k)
            c.border = BOX
            c.font = Font(name=FONT_NAME, size=7)
            c.alignment = Alignment(horizontal="center", vertical="center")

    last = ROW_FIRST + lines
    note(ws, last + 1, "Un chevauchement ne se voit pas en lisant une liste d'adresses. "
                       "Ici, si.")
    print_setup(ws, per_row + 1, last, "Cartographie %" + zone, wide=True)
    return ws


# ===========================================================================
def main():
    wb = Workbook()
    wb.remove(wb.active)

    sheet_config(wb)
    sheet_cards(wb)
    sheet_rack(wb)
    sheet_es(wb)
    sheet_catalogue(wb)
    sheet_equipements(wb)
    sheet_cablage(wb)
    sheet_liens(wb)
    sheet_indices(wb)

    report_sheet(wb, "Reports TOR", "REPORTS - TOUT OU RIEN (%MX)", dig_cols())
    report_sheet(wb, "Reports bits", "REPORTS - BITS DE MOTS (%MW)", bits_cols(), rows=600)
    report_sheet(wb, "Reports ANA", "REPORTS - ANALOGIQUE (%MW)", ana_cols())

    memory_sheet(wb, "Memoire MW", "MW")
    memory_sheet(wb, "Memoire MX", "MX")

    st = wb.create_sheet("ST genere")
    st.column_dimensions["A"].width = 120
    st.cell(row=1, column=1,
            value="(* Ecrit par le bouton Generer le ST. *)").font = F_NOTE
    st.sheet_state = "hidden"

    wb.create_sheet("_ASSISTANT").sheet_state = "hidden"

    etat = wb.create_sheet("_ETAT")
    for i, (k, v) in enumerate([("VERSION", 2), ("CAT_DATE", ""), ("VOIES_DATE", ""),
                                ("REPORTS_DATE", ""), ("MEM_DATE", ""),
                                ("CONFLITS", 0)], start=1):
        etat.cell(row=i, column=1, value=k)
        etat.cell(row=i, column=2, value=v)
    etat.sheet_state = "hidden"

    wb.save(OUT)
    print("%s : %d onglets" % (OUT, len(wb.sheetnames)))
    for n in wb.sheetnames:
        print("   ", n)


if __name__ == "__main__":
    main()
