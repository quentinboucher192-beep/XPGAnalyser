# Les planches de la maquette 1.42 : src-NN-*.html -> NN-*.html
# <!--SIDE:cle--> : la barre laterale de l'Admin (cle = l'entree active)
# <!--CAL:AAAA-MM:debut:fin:aujourdhui--> : un mois du mini calendrier (debut/fin : la periode choisie ; 0 = rien)
import calendar, glob, os, re, sys

D = os.path.dirname(os.path.abspath(__file__))
MOIS = ["", "Janvier", "Février", "Mars", "Avril", "Mai", "Juin", "Juillet", "Août", "Septembre", "Octobre", "Novembre", "Décembre"]

NAV = [
    ("Pilotage", [("dash", "dash", "Tableau de bord", ""), ("licences", "key", "Licences", "128"), ("clients", "users", "Clients", "54"),
                  ("demandes", "inbox", "Demandes en ligne", "!3"), ("activations", "laptop", "Activations", "342")]),
    ("Catalogue", [("offres", "layers", "Offres", "7"), ("options", "puzzle", "Options", "4"), ("fonct", "blocks", "Fonctionnalités", "21"),
                   ("flottantes", "network", "Licences flottantes", "")]),
    ("Vente en ligne", [("vente", "cart", "Boutique", "~bientôt")]),
    ("Sécurité", [("revoc", "ban", "Révocations", ""), ("service", "cloud", "Service en ligne", ""), ("histo", "history", "Historique", ""),
                  ("secu", "sliders", "Paramètres de sécurité", "")]),
]


def side(active):
    h = ['<aside class="side">',
         '<div class="brand"><div class="logo"><i data-i="key"></i></div><div><b>License Manager</b><span>Admin · éditeur</span></div></div>']
    for g, items in NAV:
        h.append('<div class="navg"><div class="over">%s</div>' % g)
        for k, ic, lab, n in items:
            cls = "nav" + (" on" if k == active else "") + (" dim" if n.startswith("~") else "")
            badge = ""
            if n.startswith("!"):
                badge = '<span class="n warn">%s</span>' % n[1:]
            elif n.startswith("~"):
                badge = '<span class="n">%s</span>' % n[1:]
            elif n:
                badge = '<span class="n">%s</span>' % n
            h.append('<div class="%s"><i data-i="%s"></i>%s%s</div>' % (cls, ic, lab, badge))
        h.append('</div>')
    h.append('<div class="sidefoot"><div class="svc"><div class="t"><span class="live" style="gap:0"><i></i></span>Service en ligne</div>'
             '<div class="s">Connecté · relevé il y a 4 s · v1.42.0</div></div><div class="ver"><span>Dossier du poste</span><span>1.42.0</span></div></div>')
    h.append('</aside>')
    return "\n".join(h)


def cal(spec):
    ym, a, b, t = spec.split(":")
    y, m = map(int, ym.split("-"))
    a, b, t = int(a), int(b), int(t)
    h = ['<div class="hd"><span class="nv"><i data-i="left"></i></span><b>%s %d</b><span class="nv"><i data-i="right"></i></span></div><div class="g">' % (MOIS[m], y)]
    for w in "LMMJVSD":
        h.append('<div class="wd">%s</div>' % w)
    for week in calendar.Calendar(0).monthdatescalendar(y, m):
        for d in week:
            c = ["d"]
            if d.month != m:
                c.append("o")
            else:
                n = d.day
                if t and n < t:
                    c.append("p")
                if a and b and a < n < b:
                    c.append("in")
                if a and n == a:
                    c.append("s")
                if b and n == b:
                    c.append("e")
                if t and n == t:
                    c.append("t")
            h.append('<div class="%s"><span>%d</span></div>' % (" ".join(c), d.day))
    h.append('</div>')
    return "".join(h)


for src in sorted(glob.glob(os.path.join(D, "src-*.html"))):
    s = open(src, encoding="utf-8").read()
    s = re.sub(r"<!--SIDE:(\w+)-->", lambda mm: side(mm.group(1)), s)
    s = re.sub(r"<!--LICNAV:(\w+)-->", lambda mm: re.sub(r'class="ni@(\w+)"', lambda k: 'class="ni on"' if k.group(1) == mm.group(1) else 'class="ni"', open(os.path.join(D, "licnav.part"), encoding="utf-8").read()), s)
    s = re.sub(r"<!--CAL:([0-9:-]+)-->", lambda mm: cal(mm.group(1)), s)
    out = os.path.join(D, os.path.basename(src)[4:])
    open(out, "w", encoding="utf-8").write(s)
    print("ok", os.path.basename(out))
