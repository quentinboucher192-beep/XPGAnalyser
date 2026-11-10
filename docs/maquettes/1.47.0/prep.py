# Les planches de la maquette 1.47 : src-NN-*.html -> NN-*.html
# <!--SIDE:cle-->   : la barre laterale de l'Admin (cle = l'entree active)
# <!--G:nom-->      : un graphique (SVG ou HTML), dessine par graphes.py a partir de ses donnees d'essai
# <!--V:nom-->      : une valeur calculee par graphes.py (un total, une moyenne : les memes que les graphiques)
import glob, os, re, sys

D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
import graphes  # noqa: E402

NAV = [
    ("Pilotage", [("dash", "dash", "Tableau de bord", ""), ("stats", "chart", "Statistiques", "+nouveau"),
                  ("licences", "key", "Licences", "128"), ("clients", "users", "Clients", "54"),
                  ("demandes", "inbox", "Demandes en ligne", "!5"), ("activations", "laptop", "Activations", "342")]),
    ("Catalogue", [("offres", "layers", "Offres", "4"), ("options", "puzzle", "Options", "3"), ("fonct", "blocks", "Fonctionnalités", "21"),
                   ("flottantes", "network", "Licences flottantes", "")]),
    ("Vente en ligne", [("boutique", "cart", "Boutique", ""), ("commandes", "receipt", "Commandes", "!2")]),
    ("Sécurité", [("revoc", "ban", "Révocations", ""), ("admins", "shuser", "Administrateurs", "!1"), ("journal", "scroll", "Journal des admins", "!3"),
                  ("versions", "box", "Versions", ""), ("service", "cloud", "Service en ligne", ""), ("histo", "history", "Historique", ""),
                  ("secu", "sliders", "Paramètres de sécurité", "")]),
]


def side(active):
    h = ['<aside class="side">',
         '<div class="brand"><div class="logo"><i data-i="key"></i></div><div><b>License Manager</b><span>Camille Essai · admin+</span></div></div>']
    for g, items in NAV:
        h.append('<div class="navg"><div class="over">%s</div>' % g)
        for k, ic, lab, n in items:
            cls = "nav" + (" on" if k == active else "")
            badge = ""
            if n.startswith("!"):
                badge = '<span class="n warn">%s</span>' % n[1:]
            elif n.startswith("+"):
                badge = '<span class="n new">%s</span>' % n[1:]
            elif n:
                badge = '<span class="n">%s</span>' % n
            h.append('<div class="%s"><i data-i="%s"></i>%s%s</div>' % (cls, ic, lab, badge))
        h.append('</div>')
    h.append('<div class="sidefoot"><div class="svc"><div class="t"><span class="live" style="gap:0"><i></i></span>Service en ligne</div>'
             '<div class="s">Connecté · relevé il y a 4 s</div></div><div class="ver"><span>Dossier du poste</span><span>1.47.0</span></div></div>')
    h.append('</aside>')
    return "\n".join(h)


def build(only=()):
    for src in sorted(glob.glob(os.path.join(D, "src-*.html"))):
        name = os.path.basename(src)[4:]
        if only and not any(name.startswith(o) for o in only):
            continue
        s = open(src, encoding="utf-8").read()
        s = re.sub(r"<!--SIDE:(\w+)-->", lambda mm: side(mm.group(1)), s)
        s = re.sub(r"<!--G:([\w-]+)-->", lambda mm: graphes.G[mm.group(1)](), s)
        s = re.sub(r"<!--V:([\w-]+)-->", lambda mm: str(graphes.V[mm.group(1)]), s)
        left = re.findall(r"<!--[A-Z]+:[^>]*-->", s)
        assert not left, (name, left)
        open(os.path.join(D, name), "w", encoding="utf-8").write(s)
        print("ok", name)


if __name__ == "__main__":
    build(sys.argv[1:])
