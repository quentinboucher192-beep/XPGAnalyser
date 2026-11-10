# Les graphiques des planches 00-07 (SVG ou HTML), dessines a partir de donnees d'essai (rien de reel).
# G["nom"]() rend le graphique ; V["nom"] une valeur. prep.py remplace <!--G:nom--> et <!--V:nom-->.
# La methode : un axe par graphique, traits fins, grille discrete, legende des 2 series, etiquettes directes rares,
# une couleur par entite (les admins : extra.css), la comparaison en gris.
import html

G = {}
V = {}

# ------------------------------------------------------------------ les couleurs (extra.css) ----
AD = {"C": "#6d4aff", "L": "#0b7fa3", "H": "#ea580c", "N": "#db2777"}
ADN = {"C": "Camille Essai", "L": "Léa Essai", "H": "Hugo Essai", "N": "Nina Essai"}
ORDER = ["C", "L", "H", "N"]
CMP = "#a3adbf"     # la periode d'avant
BLUE = "#2a78d6"
GRID = "#eef1f5"
BASE = "#d5dce6"
AXT = "#94a3b8"
INK = "#0f172a"
INK2 = "#334155"
MUT = "#64748b"
SEQ = ["#f1f4f8", "#cde2fb", "#9ec5f4", "#6da7ec", "#3987e5", "#256abf", "#184f95"]
CRIT = [("Info", "#94a3b8", "i"), ("Moyenne", "#2563eb", "m"), ("Haute", "#d97706", "h"), ("Critique", "#dc2626", "c")]
RED = "#dc2626"
FONT = 'font-family="Inter, system-ui, sans-serif"'

TYPES = ["Créer et signer une licence", "Prolonger une licence", "Ajouter une option", "Révoquer une licence", "Retirer un PC",
         "Modifier les offres", "Créer, modifier une option", "Annonces", "Accepter, refuser une demande", "Revoir le code",
         "Supprimer un client", "Exporter les clients", "Écrire aux clients", "Boutique : livrer, revoir"]
TSHORT = ["Créer et signer", "Prolonger", "Ajouter une option", "Révoquer", "Retirer un PC", "Offres", "Options", "Annonces",
          "Demandes", "Revoir le code", "Supprimer un client", "Exporter les clients", "Écrire aux clients", "Boutique"]

# ------------------------------------------------------------------ les donnees d'essai ----
# La periode : les 30 derniers jours (lun. 12/10 -> mar. 10/11/2026) ; la periode d'avant : 12/09 -> 11/10.
DAYS = ["12/10", "13/10", "14/10", "15/10", "16/10", "17/10", "18/10", "19/10", "20/10", "21/10", "22/10", "23/10", "24/10", "25/10",
        "26/10", "27/10", "28/10", "29/10", "30/10", "31/10", "01/11", "02/11", "03/11", "04/11", "05/11", "06/11", "07/11", "08/11",
        "09/11", "10/11"]
WEEKEND = {5, 6, 12, 13, 19, 20, 26, 27}
# Les actions par type et par admin (meme ordre que TYPES) ; Camille a en plus ses actions reservees (14).
MAT = {
    "C": [8, 6, 3, 3, 2, 4, 3, 2, 9, 4, 1, 3, 6, 12],
    "L": [19, 9, 7, 2, 0, 0, 0, 0, 34, 6, 2, 2, 14, 22],
    "H": [6, 5, 2, 1, 1, 0, 0, 0, 21, 3, 0, 0, 4, 5],
    "N": [4, 2, 1, 0, 0, 0, 0, 0, 8, 2, 0, 0, 1, 0],
}
PREV_T = [40, 29, 10, 4, 5, 2, 1, 3, 85, 15, 2, 4, 24, 33]
RESERVED_C = 14
# les droits : None = pas le droit
RIGHTS = {
    "C": [1] * 14,
    "L": [1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1],   # Prolonger retire le 02/11 ; Exporter ajoute le 05/11
    "H": [1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1],
    "N": [0] * 14,
}
ACT = {"C": 80, "L": 117, "H": 48, "N": 18}
ACT_P = {"C": 70, "L": 96, "H": 57, "N": 44}
LIC = {"C": 20, "L": 41, "H": 11, "N": 4}
LIC_P = {"C": 17, "L": 34, "H": 14, "N": 6}
DEM = {"C": 9, "L": 34, "H": 21, "N": 8}
DEM_P = {"C": 12, "L": 30, "H": 26, "N": 17}
EUR = {"C": 2760, "L": 4870, "H": 1060, "N": 0}
EUR_P = {"C": 2310, "L": 3720, "H": 1390, "N": 0}

# Les actions par jour (30 jours)
DAILY = {
    "L": [6, 5, 4, 6, 3, 0, 0, 0, 0, 7, 5, 4, 0, 2, 5, 4, 6, 5, 19, 0, 0, 6, 5, 4, 6, 3, 0, 0, 7, 5],
    "C": [4, 3, 3, 4, 3, 1, 0, 2, 4, 3, 3, 3, 0, 0, 4, 4, 5, 2, 3, 1, 0, 6, 4, 4, 4, 2, 0, 1, 5, 2],
    "H": [2, 3, 0, 2, 4, 0, 0, 3, 2, 1, 3, 2, 0, 0, 2, 3, 1, 4, 2, 0, 0, 2, 3, 1, 2, 1, 0, 4, 0, 1],
    "N": [2, 1, 2, 2, 1, 0, 0, 2, 1, 2, 1, 1, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
}
DAILY_P = {"L": [5, 4, 5, 4, 3, 0, 0, 5, 5, 4, 4, 3, 0, 0, 5, 5, 4, 5, 4, 0, 0, 5, 5, 4, 4, 3, 0, 0, 5, 5]}
for k in ORDER:
    assert sum(DAILY[k]) == ACT[k], (k, sum(DAILY[k]))
assert sum(DAILY_P["L"]) == ACT_P["L"], sum(DAILY_P["L"])
for k in ORDER:
    assert sum(MAT[k]) + (RESERVED_C if k == "C" else 0) == ACT[k], k
assert sum(PREV_T) + 10 == sum(ACT_P.values())
TOTAL_DAY = [sum(DAILY[k][i] for k in ORDER) for i in range(30)]

V.update({
    "act": sum(ACT.values()), "act_p": sum(ACT_P.values()),
    "lic_adm": sum(LIC[k] for k in "LHN"), "lic_toi": LIC["C"], "lic": sum(LIC.values()), "lic_p": sum(LIC_P.values()),
    "dem": sum(DEM.values()), "eur": "8 690 €",
})


def pct(a, b):
    return round((a - b) * 100.0 / b) if b else 0


def fr(n):
    s = "{:,}".format(n).replace(",", " ")
    return s


# ------------------------------------------------------------------ le moteur (SVG) ----
def colpath(x, w, ytop, ybase, r=4.0):
    """Une colonne : le bout arrondi (4 px), la base carree. ytop < ybase : vers le haut ; ytop > ybase : vers le bas."""
    if abs(ybase - ytop) < 0.5:
        return ""
    if ytop < ybase:
        r = min(r, w / 2, ybase - ytop)
        return ("M%.1f,%.1fV%.1fQ%.1f,%.1f %.1f,%.1fH%.1fQ%.1f,%.1f %.1f,%.1fV%.1fZ"
                % (x, ybase, ytop + r, x, ytop, x + r, ytop, x + w - r, x + w, ytop, x + w, ytop + r, ybase))
    r = min(r, w / 2, ytop - ybase)
    return ("M%.1f,%.1fV%.1fQ%.1f,%.1f %.1f,%.1fH%.1fQ%.1f,%.1f %.1f,%.1fV%.1fZ"
            % (x, ybase, ytop - r, x, ytop, x + r, ytop, x + w - r, x + w, ytop, x + w, ytop - r, ybase))


def txt(x, y, s, size=11, fill=AXT, anchor="start", weight=500, extra=""):
    return ('<text x="%.1f" y="%.1f" font-size="%s" fill="%s" text-anchor="%s" font-weight="%s" %s %s>%s</text>'
            % (x, y, size, fill, anchor, weight, FONT, extra, html.escape(s)))


class Plot:
    def __init__(s, w, h, n, ymax, ticks, fmt=None, pl=30, pr=8, pt=8, pb=20, band=True, ymin=0):
        s.w, s.h, s.n, s.ymax, s.ymin, s.ticks = w, h, n, ymax, ymin, ticks
        s.fmt = fmt or (lambda v: fr(int(v)) if float(v).is_integer() else str(v))
        s.x0, s.x1, s.y0, s.y1, s.band = pl, w - pr, h - pb, pt, band
        s.p = []

    def bw(s):
        return (s.x1 - s.x0) / s.n

    def X(s, i):
        if s.band:
            return s.x0 + (i + 0.5) * s.bw()
        return s.x0 + i * (s.x1 - s.x0) / (s.n - 1)

    def Y(s, v):
        return s.y0 - (v - s.ymin) / float(s.ymax - s.ymin) * (s.y0 - s.y1)

    def grid(s):
        for t in s.ticks:
            y = s.Y(t)
            col = BASE if t == 0 else GRID
            s.p.append('<line x1="%.1f" x2="%.1f" y1="%.1f" y2="%.1f" stroke="%s" stroke-width="1" shape-rendering="crispEdges"/>' % (s.x0, s.x1, y, y, col))
            s.p.append(txt(s.x0 - 6, y + 3.5, s.fmt(t), anchor="end"))
        return s

    def xlab(s, labels, dy=14):
        for i, t in labels:
            a = "middle"
            x = s.X(i)
            if not s.band and i == 0:
                a = "start"
            if not s.band and i == s.n - 1:
                a = "end"
            s.p.append(txt(x, s.y0 + dy, t, anchor=a))
        return s

    def cols(s, vals, color, cap=24, frac=0.62, opacity=1.0, colors=None):
        w = min(cap, s.bw() * frac)
        for i, v in enumerate(vals):
            if v:
                c = colors[i] if colors else color
                s.p.append('<path d="%s" fill="%s"%s/>' % (colpath(s.X(i) - w / 2, w, s.Y(v), s.Y(0)), c,
                                                          ' opacity="%.2f"' % opacity if opacity < 1 else ""))
        return s

    def stack(s, series, cap=24, frac=0.62):
        """series : [(valeurs, couleur)], de la base vers le haut ; 2 px de surface entre les morceaux."""
        w = min(cap, s.bw() * frac)
        for i in range(s.n):
            base = 0
            parts = [(vals[i], c) for vals, c in series if vals[i]]
            for j, (v, c) in enumerate(parts):
                yb = s.Y(base) - (2 if j else 0)
                yt = s.Y(base + v)
                top = j == len(parts) - 1
                x = s.X(i) - w / 2
                if top:
                    s.p.append('<path d="%s" fill="%s"/>' % (colpath(x, w, yt, yb), c))
                elif yb - yt > 0.5:
                    s.p.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s"/>' % (x, yt, w, yb - yt, c))
                base += v
        return s

    def line(s, vals, color, width=2.0, area=False, dot=True, ring=True, label=None, label_dx=8):
        pts = [(s.X(i), s.Y(v)) for i, v in enumerate(vals) if v is not None]
        d = "M" + " L".join("%.1f,%.1f" % p for p in pts)
        if area:
            s.p.append('<path d="%s L%.1f,%.1f L%.1f,%.1f Z" fill="%s" opacity=".10"/>' % (d, pts[-1][0], s.Y(0), pts[0][0], s.Y(0), color))
        s.p.append('<path d="%s" fill="none" stroke="%s" stroke-width="%s" stroke-linejoin="round" stroke-linecap="round"/>' % (d, color, width))
        if dot:
            x, y = pts[-1]
            s.p.append('<circle cx="%.1f" cy="%.1f" r="4" fill="%s" stroke="#fff" stroke-width="2"/>' % (x, y, color))
        if label:
            x, y = pts[-1]
            s.p.append(txt(x + label_dx, y + 4, label, fill=INK, weight=600))
        return s

    def mark(s, i, v, text, color=INK, dy=-8, anchor="middle", weight=600, size=11):
        s.p.append(txt(s.X(i), s.Y(v) + dy, text, fill=color, anchor=anchor, weight=weight, size=size))
        return s

    def raw(s, svg):
        s.p.append(svg)
        return s

    def svg(s):
        return ('<svg width="%d" height="%d" viewBox="0 0 %d %d" xmlns="http://www.w3.org/2000/svg">%s</svg>'
                % (s.w, s.h, s.w, s.h, "".join(s.p)))


def spark(vals, color, w=120, h=34, prev=None, dot=True, ymax=None):
    m = ymax or max(max(vals), max(prev) if prev else 0)
    n = len(vals)

    def pts(v):
        return " L".join("%.1f,%.1f" % (3 + i * (w - 8) / (n - 1), h - 4 - x / float(m) * (h - 10)) for i, x in enumerate(v))
    o = ['<svg width="%d" height="%d" viewBox="0 0 %d %d">' % (w, h, w, h)]
    if prev:
        o.append('<path d="M%s" fill="none" stroke="%s" stroke-width="1.5" stroke-linejoin="round"/>' % (pts(prev), CMP))
    d = "M" + pts(vals)
    o.append('<path d="%s L%.1f,%.1f L3,%.1f Z" fill="%s" opacity=".10"/>' % (d, w - 5, h - 4, h - 4, color))
    o.append('<path d="%s" fill="none" stroke="%s" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>' % (d, color))
    if dot:
        x, y = 3 + (n - 1) * (w - 8) / (n - 1), h - 4 - vals[-1] / float(m) * (h - 10)
        o.append('<circle cx="%.1f" cy="%.1f" r="3.5" fill="%s" stroke="#fff" stroke-width="2"/>' % (x, y, color))
    o.append("</svg>")
    return "".join(o)


def hbars(rows, color, maxv, cols="150px 1fr 34px", extra=None):
    """rows : (libelle, valeur, note|None, off) ; une seule serie, une seule teinte, le bout arrondi."""
    o = ['<div class="hb" style="grid-template-columns:%s">' % cols]
    for lab, v, note, off in rows:
        o.append('<div class="l%s">%s</div>' % (" off" if off else "", html.escape(lab)))
        if v:
            o.append('<div class="tr"><i style="width:%.1f%%;background:%s"></i></div>' % (v * 100.0 / maxv, color))
        else:
            o.append('<div class="tr nt">%s</div>' % (note or ""))
        o.append('<div class="n%s">%s</div>' % (" off" if off and not v else "", "—" if off and not v else fr(v)))
    o.append("</div>")
    return "".join(o)


# ------------------------------------------------------------------ 01 : la vue d'ensemble ----
def g_kpi_actions():
    return spark(TOTAL_DAY, BLUE, w=96, h=30)


def g_kpi_eur():
    weekly = [1520, 1980, 2310, 2880]
    prev = [1640, 2010, 1720, 2050]
    return spark(weekly, BLUE, w=80, h=30, prev=prev)


def g_spark_lea():
    """La courbe de Lea sur 30 jours (depliee dans la liste) + la periode d'avant en gris."""
    p = Plot(250, 96, 30, 22, [0, 10, 20], pl=22, pr=6, pt=10, pb=18, band=False)
    p.grid().xlab([(0, "12/10"), (14, "26/10"), (29, "10/11")])
    p.line(DAILY_P["L"], CMP, width=1.5, dot=False)
    p.line(DAILY["L"], AD["L"], area=True)
    p.mark(18, 19, "19 · le 30/10", dy=-1, anchor="end", size=10.5)
    return p.svg()


def g_rank_spark(k):
    def f():
        return spark(DAILY[k], AD[k], w=78, h=24, dot=True, ymax=19)
    return f


# ------------------------------------------------------------------ 02 : la fiche (vue d'ensemble) ----
def g_lea_days():
    p = Plot(574, 246, 30, 20, [0, 5, 10, 15, 20], pl=26, pr=4, pt=12, pb=20, band=True)
    p.grid()
    # les fins de semaine : une bande tres claire
    for i in sorted(WEEKEND):
        x = p.X(i) - p.bw() / 2
        p.p.insert(0, '<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#f8fafc"/>' % (x, p.y1, p.bw(), p.y0 - p.y1))
    p.cols(DAILY["L"], AD["L"], frac=0.6)
    p.line(DAILY_P["L"], CMP, width=1.5, dot=False)
    p.xlab([(0, "lun. 12/10"), (7, "19/10"), (14, "26/10"), (21, "02/11"), (29, "auj.")])
    p.mark(18, 19, "19 actions · 3,4× sa moyenne", dy=-6, anchor="end", size=10.5, color=INK2)
    return p.svg()


def g_lea_types():
    rows = []
    for i, t in enumerate(TYPES):
        v = MAT["L"][i]
        has = RIGHTS["L"][i]
        note = None
        off = False
        if i == 1:
            note = None
        if not has and not v:
            off = True
            note = '<span class="cap" style="font-size:11px">pas ce droit</span>'
        elif has and not v:
            note = '<span class="warn2" style="font-size:11px;display:inline-flex;gap:4px;align-items:center"><i data-i="alert" style="width:12px;height:12px"></i>jamais (90 j)</span>'
        lab = t
        rows.append((lab, v, note, off))
    out = hbars(rows, AD["L"], 34, cols="182px 1fr 26px")
    return out


def g_lea_crit():
    vals = [71, 33, 11, 2]
    tot = sum(vals)
    o = ['<div class="seg2" style="height:12px">']
    for (lab, col, _), v in zip(CRIT, vals):
        o.append('<i style="width:%.2f%%;background:%s"></i>' % (v * 100.0 / tot, col))
    o.append('</div><div style="display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-top:10px">')
    for (lab, col, cls), v in zip(CRIT, vals):
        o.append('<div><span class="cr %s">%s</span><div style="font:650 16px Inter;margin-top:4px" class="nofig">%d <span class="cap" style="font-weight:500">%d %%</span></div></div>'
                 % (cls, lab, v, round(v * 100.0 / tot)))
    o.append("</div>")
    return "".join(o)


# ------------------------------------------------------------------ 03 : activite et securite ----
HOURS = list(range(24))
WD = ["lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."]


def heat_lea():
    tot = {0: 24, 1: 19, 2: 21, 3: 22, 4: 29, 5: 0, 6: 2}
    wts = {7: 0.3, 8: 1.4, 9: 3.6, 10: 4.4, 11: 3.8, 12: 1.0, 13: 1.2, 14: 3.6, 15: 3.8, 16: 3.0, 17: 1.6, 18: 0.4}
    m = [[0] * 24 for _ in range(7)]
    for d in range(5):
        t = tot[d] - (1 if d == 2 else 0)
        sw = sum(wts.values())
        raw = {h: t * w / sw for h, w in wts.items()}
        fl = {h: int(v) for h, v in raw.items()}
        rest = t - sum(fl.values())
        for h in sorted(raw, key=lambda h: raw[h] - fl[h], reverse=True)[:rest]:
            fl[h] += 1
        for h, v in fl.items():
            m[d][h] = v
    m[2][21] = 1   # mer. 21 h : inhabituel
    m[6][22] = 2   # dim. 22 h : inhabituel
    assert sum(map(sum, m)) == 117
    return m


HEAT = heat_lea()


def g_heat():
    cw, ch, gap = 21, 20, 2
    x0, y0 = 38, 20
    o = []
    # la bande des heures habituelles (8 h 40 -> 17 h 50)
    hx = x0 + 8.67 * (cw + gap)
    hx2 = x0 + 17.83 * (cw + gap)
    o.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="6" fill="none" stroke="%s" stroke-width="1.5"/>' % (hx - 3, y0 - 4, hx2 - hx + 4, 7 * (ch + gap) + 4, "#b9c6d8"))
    o.append(txt((hx + hx2) / 2, y0 - 9, "ses heures habituelles : 8 h 40 → 17 h 50", fill=MUT, anchor="middle", size=10.5))
    mx = max(max(r) for r in HEAT)
    for d in range(7):
        o.append(txt(x0 - 8, y0 + d * (ch + gap) + 15, WD[d], anchor="end", size=10.5))
        for h in range(24):
            v = HEAT[d][h]
            if v == 0:
                c = SEQ[0]
            else:
                c = SEQ[1 + min(5, int((v - 1) * 6 / mx))]
            x = x0 + h * (cw + gap)
            y = y0 + d * (ch + gap)
            o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="3" fill="%s"/>' % (x, y, cw, ch, c))
            if (d, h) in ((2, 21), (6, 22)):
                o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="4" fill="none" stroke="%s" stroke-width="2"/>' % (x - 2, y - 2, cw + 4, ch + 4, RED))
    for h in range(0, 24, 3):
        o.append(txt(x0 + h * (cw + gap) + cw / 2, y0 + 7 * (ch + gap) + 12, "%d h" % h, anchor="middle", size=10.5))
    # l'echelle
    lx, ly = x0, y0 + 7 * (ch + gap) + 28
    o.append(txt(lx, ly + 9, "actions par heure :", size=10.5, fill=MUT))
    for i, c in enumerate(SEQ):
        o.append('<rect x="%d" y="%d" width="18" height="10" rx="2" fill="%s"/>' % (lx + 98 + i * 20, ly, c))
    o.append(txt(lx + 98 - 4, ly + 21, "0", size=10, anchor="start"))
    o.append(txt(lx + 98 + 7 * 20 - 2, ly + 21, str(mx), size=10, anchor="end"))
    o.append('<rect x="%d" y="%d" width="14" height="10" rx="3" fill="none" stroke="%s" stroke-width="2"/>' % (lx + 270, ly, RED))
    o.append(txt(lx + 290, ly + 9, "heure inhabituelle pour elle (signalée)", size=10.5, fill=INK2))
    w = x0 + 24 * (cw + gap) + 4
    return '<svg width="%d" height="%d" viewBox="0 0 %d %d">%s</svg>' % (w, ly + 26, w, ly + 26, "".join(o))


def g_streak():
    """Les 30 jours : un carre par jour (actif ou non), la serie en cours."""
    o = []
    cw, gap = 26, 4
    for i, v in enumerate(DAILY["L"]):
        x = i % 15 * (cw + gap)
        y = i // 15 * 46
        c = SEQ[0] if v == 0 else (SEQ[2] if v < 5 else (SEQ[4] if v < 10 else SEQ[6]))
        o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="4" fill="%s"/>' % (x, y, cw, 22, c))
        if i >= 21:
            o.append('<rect x="%d" y="%d" width="%d" height="3" rx="1.5" fill="%s"/>' % (x, y + 25, cw, AD["L"]))
        if i % 15 in (0, 7, 14) or i == 29:
            o.append(txt(x + cw / 2, y + 39, DAYS[i] if i != 29 else "auj.", anchor="middle", size=10))
    w = 15 * (cw + gap)
    return '<svg width="%d" height="%d" viewBox="0 0 %d %d">%s</svg>' % (w, 92, w, 92, "".join(o))


# ------------------------------------------------------------------ 04 : ce qu'elle produit ----
WEEKS = ["17/08", "24/08", "31/08", "07/09", "14/09", "21/09", "28/09", "05/10", "12/10", "19/10", "26/10", "02/11"]
W_LIC_L = [0, 0, 0, 0, 3, 6, 7, 8, 9, 6, 15, 11]
W_LIC_O = [14, 12, 15, 13, 11, 9, 10, 9, 8, 9, 10, 8]
W_DEM_L = [0, 0, 0, 0, 2, 5, 7, 8, 9, 4, 11, 10]
W_DEM_O = [16, 18, 15, 17, 14, 12, 10, 11, 9, 10, 8, 9]
W_EUR_L = [0, 0, 0, 0, 380, 610, 980, 1130, 1240, 690, 1650, 1290]
W_EUR_O = [1820, 2140, 1690, 1960, 1710, 1480, 1220, 1310, 940, 1080, 870, 1150]


def emph(wl, wo, ymax, ticks, fmt=None, w=360, h=128, unit="", lab=True):
    p = Plot(w, h, 12, ymax, ticks, fmt=fmt, pl=34 if ymax > 100 else 24, pr=4, pt=8, pb=18, band=True)
    p.grid()
    # avant son arrivee (14/09) : une bande claire
    p.p.insert(0, '<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#f8fafc"/>' % (p.x0, p.y1, 4 * p.bw(), p.y0 - p.y1))
    p.stack([(wl, AD["L"]), (wo, "#d5dce6")], frac=0.58)
    p.xlab([(0, "17/08"), (4, "14/09"), (8, "12/10"), (11, "02/11")] if w > 300 else [(0, "17/08"), (4, "14/09"), (11, "02/11")])
    if lab:
        p.p.append(txt(p.x0 + 4, p.y1 + 10, "pas encore admin", size=10, fill=AXT))
    return p.svg()


def g_w_lic():
    return emph(W_LIC_L, W_LIC_O, 30, [0, 10, 20, 30], w=384, h=150)


def g_w_dem():
    return emph(W_DEM_L, W_DEM_O, 25, [0, 10, 20], w=212, h=158, lab=False)


def g_w_eur():
    return emph(W_EUR_L, W_EUR_O, 3000, [0, 1000, 2000, 3000], fmt=lambda v: "%d k€" % (v // 1000) if v else "0", w=384, h=158)


def g_delay_hist():
    bins = ["< ½ h", "½–1 h", "1–2 h", "2–4 h", "4–8 h", "8–24 h", "1–2 j", "> 2 j"]
    vals = [9, 8, 6, 5, 3, 2, 0, 1]
    assert sum(vals) == 34
    p = Plot(374, 162, 8, 10, [0, 5, 10], pl=22, pr=4, pt=24, pb=18, band=True)
    p.grid()
    p.cols(vals, AD["L"], frac=0.62)
    for i, b in enumerate(bins):
        p.p.append(txt(p.X(i), p.y0 + 13, b, anchor="middle", size=10))
    # la mediane (1 h 10) tombe dans « 1-2 h », la moyenne (4 h 05) dans « 4-8 h », le plus long (2 j 6 h) dans « > 2 j »
    for i, lab, v, anc, top in ((2, "médiane 1 h 10", 6, "end", p.y1 - 6), (4, "moyenne 4 h 05", 3, "start", p.y1 - 6), (7, "max 2 j 6 h", 1, "end", p.Y(5))):
        x = p.X(i)
        p.p.append('<line x1="%.1f" x2="%.1f" y1="%.1f" y2="%.1f" stroke="%s" stroke-width="1"/>' % (x, x, p.Y(v) - 3, top, INK2))
        p.p.append(txt(x + (4 if anc == "end" else -4), top - 3, lab, anchor=anc, fill=INK, weight=600, size=10.5))
    return p.svg()


# ------------------------------------------------------------------ 05 : qui fait quoi ----
def g_share():
    """La part de chacun par type d'action : une barre a 100 % par type, les admins dans l'ordre fixe."""
    o = ['<div class="shr">']
    o.append('<div class="shh"><span>Type d\'action</span><span>Part de chacun</span><span style="text-align:right">Actions</span><span style="text-align:right">vs avant</span><span>En tête</span></div>')
    tot_all = [sum(MAT[k][i] for k in ORDER) for i in range(14)]
    allrow = [sum(MAT[k]) for k in ORDER]
    rows = [("Toutes (hors réservées)", allrow, sum(allrow), sum(PREV_T), True)]
    for i, t in enumerate(TYPES):
        rows.append((t, [MAT[k][i] for k in ORDER], tot_all[i], PREV_T[i], False))
    for lab, vals, tot, prev, first in rows:
        o.append('<div class="shrow%s">' % (" first" if first else ""))
        o.append('<span class="l">%s</span><span class="seg2" style="height:%dpx">' % (html.escape(lab), 12 if first else 10))
        for k, v in zip(ORDER, vals):
            if v:
                o.append('<i style="width:%.2f%%;background:%s"></i>' % (v * 100.0 / tot, AD[k]))
        o.append('</span><span class="n">%d</span>' % tot)
        d = tot - prev
        if prev >= 10:
            p = pct(tot, prev)
            cls = "neu" if p == 0 else ("up" if p > 0 else "dn")
            o.append('<span class="t %s">%s</span>' % (cls, ("%+d %%" % p).replace("-", "−") if p else "="))
        else:
            cls = "neu" if d == 0 else ("up" if d > 0 else "dn")
            o.append('<span class="t %s">%s</span>' % (cls, ("%+d" % d).replace("-", "−") if d else "="))
        lead = max(ORDER, key=lambda k: vals[ORDER.index(k)])
        lv = vals[ORDER.index(lead)]
        if len([v for v in vals if v]) == 1:
            o.append('<span class="ld"><i class="av3 c%s" style="width:16px;height:16px;font-size:9px;border-radius:5px">%s</i>%s seule%s</span>'
                     % (lead, ADN[lead][0], ADN[lead].split()[0], "" if lead in "LN" else "".replace("", "")))
        else:
            o.append('<span class="ld"><i class="av3 c%s" style="width:16px;height:16px;font-size:9px;border-radius:5px">%s</i>%s %d %%</span>'
                     % (lead, ADN[lead][0], ADN[lead].split()[0], round(lv * 100.0 / tot)))
        o.append("</div>")
    o.append("</div>")
    return "".join(o)


def dumbbell(title, cur, prev, maxv, ticks, fmt=None, w=410, sub=""):
    fmt = fmt or (lambda v: fr(v))
    rowh = 22
    x0, x1 = 58, w - 74
    h = 22 + rowh * 4 + 20
    o = [txt(0, 12, title, size=12, fill=INK, weight=650)]
    if sub:
        o.append(txt(w, 12, sub, size=10.5, fill=MUT, anchor="end"))

    def X(v):
        return x0 + v * (x1 - x0) / float(maxv)
    for t in ticks:
        o.append('<line x1="%.1f" x2="%.1f" y1="20" y2="%d" stroke="%s" shape-rendering="crispEdges"/>' % (X(t), X(t), 20 + rowh * 4, BASE if t == 0 else GRID))
        o.append(txt(X(t), 20 + rowh * 4 + 13, fmt(t), anchor="middle", size=10))
    for j, k in enumerate(ORDER):
        y = 20 + rowh * j + rowh / 2
        o.append(txt(x0 - 10, y + 4, ADN[k].split()[0], anchor="end", size=11.5, fill=INK2, weight=550))
        a, b = X(prev[k]), X(cur[k])
        o.append('<line x1="%.1f" x2="%.1f" y1="%.1f" y2="%.1f" stroke="#cbd3de" stroke-width="2" stroke-linecap="round"/>' % (a, b, y, y))
        o.append('<circle cx="%.1f" cy="%.1f" r="4.5" fill="%s" stroke="#fff" stroke-width="2"/>' % (a, y, CMP))
        o.append('<circle cx="%.1f" cy="%.1f" r="5" fill="%s" stroke="#fff" stroke-width="2"/>' % (b, y, AD[k]))
        p = pct(cur[k], prev[k]) if prev[k] else 0
        lab = fmt(cur[k]) + ("  " + ("%+d %%" % p).replace("-", "−") if prev[k] else "")
        o.append(txt(max(a, b) + 10, y + 4, lab, size=11, fill=INK, weight=600))
    return '<svg width="%d" height="%d" viewBox="0 0 %d %d">%s</svg>' % (w, h, w, h, "".join(o))


def g_db_act():
    return dumbbell("Actions", ACT, ACT_P, 120, [0, 40, 80, 120], w=250, sub="263 · −1 %")


def g_db_lic():
    return dumbbell("Licences signées", LIC, LIC_P, 45, [0, 15, 30, 45], w=250, sub="76 · +7 %")


def g_db_dem():
    return dumbbell("Demandes traitées", DEM, DEM_P, 45, [0, 15, 30, 45], w=250, sub="72 · −15 %")


def g_db_eur():
    return dumbbell("Montant encaissé", EUR, EUR_P, 6000, [0, 2000, 4000, 6000], w=250, sub="8 690 € · +17 %",
                    fmt=lambda v: ("%s €" % fr(v)) if v % 1000 else ("%d k€" % (v // 1000) if v else "0"))


G.update({
    "kpi_actions": g_kpi_actions, "kpi_eur": g_kpi_eur, "spark_lea": g_spark_lea,
    "rk_C": g_rank_spark("C"), "rk_L": g_rank_spark("L"), "rk_H": g_rank_spark("H"), "rk_N": g_rank_spark("N"),
    "lea_days": g_lea_days, "lea_types": g_lea_types, "lea_crit": g_lea_crit,
    "heat": g_heat, "streak": g_streak,
    "w_lic": g_w_lic, "w_dem": g_w_dem, "w_eur": g_w_eur, "delay_hist": g_delay_hist,
    "share": g_share, "db_act": g_db_act, "db_lic": g_db_lic, "db_dem": g_db_dem, "db_eur": g_db_eur,
})


# ------------------------------------------------------------------ la fiche : l'en-tete commun (02, 03, 04) ----
ANCRES = ["Vue d'ensemble", "Activité", "Licences et clients", "Demandes", "Boutique", "Sécurité", "Droits"]


def fiche_head(on):
    tabs = "".join('<span class="%s">%s</span>' % ("on" if a in on else "", a) for a in ANCRES)
    return ('''<div class="fh">
  <div class="big cL">L</div>
  <div class="grow"><div class="row gap8"><span class="nm2">Léa Essai</span><span class="pill p-gray pl">Admin</span><span class="pill p-green pl"><span class="d"></span>Active</span>
   <span class="pill p-violet pl">Admin LV2 · 2 écarts</span></div>
   <div class="muted" style="margin-top:3px;font-size:12.5px">lea@exemple.fr · PC-COMMERCIAL-02 · admin depuis le 14/09/2026, ajoutée par Camille Essai · <span class="ok2">en ligne</span>, vue il y a 2 min à Paris, FR</div></div>
  <span class="btn sm"><i data-i="scroll" class="sm"></i>Son journal</span><span class="btn sm"><i data-i="lock" class="sm"></i>Ses droits…</span><span class="btn sm"><i data-i="mail" class="sm"></i>Lui écrire…</span><span class="btn sm" style="padding:0 8px"><i data-i="more" class="sm"></i></span>
 </div>
 <div class="row" style="margin-top:12px;align-items:flex-end;gap:16px;border-bottom:1px solid var(--line)">
  <div class="tabs" style="border-bottom:0">%s</div>
  <div class="per" style="margin-left:auto;padding-bottom:7px"><div class="seg"><span>7 j</span><span class="on">30 j</span><span>90 j</span><span>12 mois</span><span>Tout</span><span><i data-i="calendar"></i>Dates…</span></div>
   <div class="cmp">comparé à <b>la période d'avant</b><i data-i="down"></i></div></div>
 </div>''' % tabs)


W_ACT_L = [0, 0, 0, 0, 9, 18, 22, 25, 24, 18, 39, 24]
def fiche_head_compact(on):
    """L'en-tete reduit, quand on descend dans la fiche (il reste en haut)."""
    tabs = "".join('<span class="%s">%s</span>' % ("on" if a in on else "", a) for a in ANCRES)
    return ('<div class="row" style="gap:14px;align-items:center;border-bottom:1px solid var(--line);margin-top:-6px">'
            '<div class="row gap8" style="padding-bottom:9px"><span class="av3 cL" style="width:30px;height:30px;border-radius:9px;font-size:12px">L</span>'
            '<b style="font:700 16px Inter;white-space:nowrap">Léa Essai</b><span class="pill p-green pl"><span class="d"></span>Active</span></div>'
            '<div class="tabs" style="border-bottom:0;margin-left:10px;gap:18px">%s</div>'
            '<div class="per" style="margin-left:auto;padding-bottom:7px"><div class="seg"><span>7 j</span><span class="on">30 j</span><span>90 j</span>'
            '<span>12 mois</span><span>Tout</span><span><i data-i="calendar"></i>Dates…</span></div></div></div>' % tabs)


G.update({
    "fh_vue": lambda: fiche_head(["Vue d'ensemble"]),
    "fh_act": lambda: fiche_head_compact(["Activité"]),
    "fh_lic": lambda: fiche_head_compact(["Licences et clients"]),
    "sp_act": lambda: spark(W_ACT_L, AD["L"], w=70, h=26),
    "sp_lic": lambda: spark(W_LIC_L, AD["L"], w=70, h=26),
    "sp_dem": lambda: spark(W_DEM_L, AD["L"], w=70, h=26),
    "sp_eur": lambda: spark(W_EUR_L, AD["L"], w=70, h=26),
})


def g_lea_share():
    rows = [("Actions", 117, 263, "1re sur 4"), ("Licences signées", 41, 76, "1re"), ("Demandes traitées", 34, 72, "1re"),
            ("Montant encaissé", 4870, 8690, "1re"), ("Délai de réponse", None, None, "2e plus rapide")]
    o = ['<div style="display:grid;grid-template-columns:132px 1fr 46px 86px;align-items:center;column-gap:12px;row-gap:12px">']
    for lab, a, b, rk in rows:
        o.append('<div style="font-size:12.5px;color:var(--ink2)">%s</div>' % lab)
        if a is None:
            # le delai : un point par admin sur une echelle de temps (plus a gauche = plus rapide)
            pts = {"C": 2.08, "L": 4.08, "H": 9.67, "N": 5.5}
            svg = ['<svg width="100%%" height="18" viewBox="0 0 300 18" preserveAspectRatio="none">']
            svg.append('<line x1="0" x2="300" y1="9" y2="9" stroke="%s"/>' % GRID)
            o.append('<div style="position:relative;height:18px"><div style="position:absolute;left:0;right:0;top:8px;height:2px;background:#eef1f5;border-radius:1px"></div>%s</div>'
                     % "".join('<i style="position:absolute;left:calc(%.1f%% - 6px);top:3px;width:12px;height:12px;border-radius:50%%;background:%s;border:2px solid #fff;%s"></i>'
                               % (v / 10.0 * 100, AD[k], "" if k == "L" else "opacity:.35") for k, v in pts.items()))
            o.append('<div class="nofig" style="font:650 13px Inter">4 h 05</div><div class="cap">%s</div>' % rk)
            continue
        p = a * 100.0 / b
        o.append('<div class="seg2" style="height:10px"><i style="width:%.1f%%;background:%s"></i><i style="width:%.1f%%;background:#dfe5ee"></i></div>' % (p, AD["L"], 100 - p))
        o.append('<div class="nofig" style="font:650 13px Inter">%d %%</div><div class="cap">%s</div>' % (round(p), rk))
    o.append("</div>")
    return "".join(o)


G["lea_share"] = g_lea_share


# ------------------------------------------------------------------ 03 : ses jours ----
def g_streak1():
    """Les 30 jours sur une ligne : un carre par jour (son nombre d'actions), la serie en cours soulignee."""
    o = []
    cw, gap = 16, 3
    mx = max(DAILY["L"])
    for i, v in enumerate(DAILY["L"]):
        x = i * (cw + gap)
        c = SEQ[0] if v == 0 else SEQ[1 + min(5, int((v - 1) * 6 / mx * 1.6))]
        o.append('<rect x="%d" y="0" width="%d" height="%d" rx="3" fill="%s"/>' % (x, cw, cw, c))
        if i in (13,):
            o.append('<rect x="%d" y="-2" width="%d" height="%d" rx="4" fill="none" stroke="%s" stroke-width="2"/>' % (x - 2, cw + 4, cw + 4, RED))
        if i in (0, 7, 14, 21, 29):
            o.append(txt(x + cw / 2, 30, DAYS[i] if i != 29 else "auj.", anchor="middle", size=10))
    x0 = 21 * (cw + gap)
    o.append('<rect x="%d" y="%d" width="%d" height="3" rx="1.5" fill="%s"/>' % (x0, cw + 3, 9 * (cw + gap) - gap, AD["L"]))
    w = 30 * (cw + gap)
    return '<svg width="%d" height="36" viewBox="0 -3 %d 36">%s</svg>' % (w, w, "".join(o))


# debut et fin de chaque jour actif (heures decimales) ; None : pas d'action
RANGE = [(8.7, 17.6), (8.5, 18.0), (9.1, 17.2), (8.6, 17.9), (8.9, 16.4), None, None, None, None, (9.0, 21.05), (8.8, 17.5), (8.6, 17.1), None, (22.2, 22.4),
         (8.7, 17.8), (8.9, 17.4), (8.4, 18.1), (8.6, 17.7), (8.2, 19.7), None, None, (8.7, 17.9), (8.8, 17.6), (8.6, 17.3), (8.5, 18.0), (9.0, 16.2), None, None,
         (8.6, 17.8), (8.4, 11.7)]


def g_range():
    p = Plot(596, 128, 30, 24, [6, 12, 18, 24], fmt=lambda v: "%d h" % v, pl=34, pr=4, pt=6, pb=18, band=True, ymin=6)
    # la bande habituelle
    p.p.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#f1f6fd"/>' % (p.x0, p.Y(17.83), p.x1 - p.x0, p.Y(8.67) - p.Y(17.83)))
    p.grid()
    w = 7
    for i, r in enumerate(RANGE):
        if not r:
            continue
        a, b = r
        odd = b > 20 or a > 20
        c = RED if odd else AD["L"]
        x = p.X(i) - w / 2
        h = max(4, p.Y(a) - p.Y(b))
        p.p.append('<rect x="%.1f" y="%.1f" width="%d" height="%.1f" rx="3.5" fill="%s"/>' % (x, p.Y(b), w, h, c))
    p.xlab([(0, "12/10"), (7, "19/10"), (14, "26/10"), (21, "02/11"), (29, "auj.")])
    p.p.append(txt(p.X(13) + 8, p.Y(22.3) + 4, "dim. 25/10, 22 h 14", fill=INK, weight=600, size=10.5))
    p.p.append(txt(p.X(9) + 8, p.Y(21.05) + 4, "21 h 03", fill=INK, weight=600, size=10.5))
    return p.svg()


G.update({"streak1": g_streak1, "range": g_range})


def g_lic_offre():
    return hbars([("PERSONAL", 15, None, False), ("PROFESSIONAL", 20, None, False), ("ENTERPRISE", 6, None, False)], AD["L"], 20, cols="96px 1fr 22px")


def g_top_clients():
    rows = [("Bureau d'études Exemple", 5, 1470), ("Client d'essai", 4, 1190), ("Studio Exemple", 3, 780), ("Laboratoire d'essai", 3, 560), ("Usine Exemple Nord", 2, 390)]
    o = ['<div class="hb" style="grid-template-columns:150px 1fr 58px 52px">']
    for n, lic, eur in rows:
        o.append('<div class="l">%s</div><div class="tr"><i style="width:%.1f%%;background:%s"></i></div><div class="n" style="text-align:right">%s €</div><div class="cap" style="white-space:nowrap">%d lic.</div>'
                 % (html.escape(n), eur * 100.0 / 1470, AD["L"], fr(eur), lic))
    o.append("</div>")
    return "".join(o)


def g_articles():
    return hbars([("PERSONAL 1 an", 9, None, False), ("PROFESSIONAL 1 an", 6, None, False), ("ENTERPRISE 1 an", 1, None, False),
                  ("Exports avancés", 4, None, False), ("Travail à plusieurs", 2, None, False)], AD["L"], 9, cols="128px 1fr 14px")


G.update({"lic_offre": g_lic_offre, "top_clients": g_top_clients, "articles": g_articles})
