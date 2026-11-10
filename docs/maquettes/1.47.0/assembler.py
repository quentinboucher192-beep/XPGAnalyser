# Une seule page HTML autonome : les planches de la maquette 1.47.0 (styles, scripts, icones, polices dedans)
# python3 assembler.py <dossier> maquette-1.47.0.html   (apres python3 prep.py)
# Chaque planche va dans un <iframe srcdoc> : ses scripts y tournent (le globe de la planche 11 tourne toujours).
import glob, html, os, re, sys

M = sys.argv[1]
OUT = sys.argv[2]
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
         '<link href="https://fonts.googleapis.com/css2?family=Inter:opsz,wght@14..32,400..800&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet">'
         '<style>@font-face{font-family:"Inter Display";src:local("Inter Display"),local("Inter");}</style>')

TITRES = {
    "00": "La liste des informations proposées",
    "01": "Administrateurs — la vue d'ensemble (admin+)",
    "02": "La fiche d'un admin — vue d'ensemble",
    "03": "La fiche — activité et sécurité",
    "04": "La fiche — licences, clients, demandes, boutique",
    "05": "Qui fait quoi — et toi, l'admin+",
    "06": "Le clic droit — la règle et 6 menus",
    "07": "Le dépliage partout",
    "08": "Statistiques — la vue générale",
    "09": "Statistiques — les mises à jour",
    "10": "La fiche d'un client et ses courbes",
    "11": "Le globe",
}


def local(name):
    p = os.path.join(M, name)
    assert os.path.isfile(p) and os.path.dirname(os.path.abspath(p)) == os.path.abspath(M), name
    return open(p, encoding="utf-8").read()


def board(name):
    s = open(os.path.join(M, name), encoding="utf-8").read()
    # les feuilles de style et les scripts du dossier, mis dedans (</ echappe dans les scripts)
    s = re.sub(r'<link rel="stylesheet" href="([\w.-]+\.css)">', lambda m: "<style>%s</style>" % local(m.group(1)), s)
    s = re.sub(r'<script src="([\w.-]+\.js)"></script>', lambda m: "<script>%s</script>" % local(m.group(1)).replace("</", "<\\/"), s)
    s = s.replace("<head>", "<head>" + FONTS, 1)
    rest = re.findall(r'(?:href|src)="(?!https://fonts|#|data:)[^"]+"', s)
    assert not rest, (name, rest[:5])
    assert "/home/" not in s and "/tmp/" not in s, name
    return s


pages = sorted(f for f in os.listdir(M) if re.match(r"^\d\d-.*\.html$", f))
nums = [f[:2] for f in pages]
manque = [n for n in TITRES if n not in nums]
if manque:
    print("ATTENTION : planches absentes :", ", ".join(manque))
PLANCHES = [(f, TITRES.get(f[:2], f[3:-5]), 1000) for f in pages]

nav = "".join('<a href="#p%s"><b>%s</b>%s</a>' % (f[:2], f[:2], html.escape(t)) for f, t, _ in PLANCHES)
cartes = ""
for f, t, h in PLANCHES:
    cartes += ('<section id="p%s"><h2><span>%s</span>%s</h2><div class="cadre" data-h="%d"><iframe title="%s" width="1600" height="%d" '
               'loading="lazy" srcdoc="%s"></iframe></div></section>\n') % (f[:2], f[:2], html.escape(t), h, html.escape(t), h, html.escape(board(f), quote=True))
page = """<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Maquette XPG 1.47</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:opsz,wght@14..32,400..800&display=swap" rel="stylesheet">
<style>
:root{--bg:#f4f6fa;--ink:#0f172a;--muted:#64748b;--line:#e3e8ef;--card:#fff;--acc:#6d4aff}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#0b1020;--ink:#e5e9f2;--muted:#94a3b8;--line:#1f2a44;--card:#111a2e}}
:root[data-theme="dark"]{--bg:#0b1020;--ink:#e5e9f2;--muted:#94a3b8;--line:#1f2a44;--card:#111a2e}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 Inter,system-ui,sans-serif}
header{max-width:1240px;margin:0 auto;padding:40px 16px 8px}
header .o{font:700 12px Inter;letter-spacing:.1em;text-transform:uppercase;color:var(--acc)}
header h1{font:750 34px/1.15 Inter;letter-spacing:-.02em;margin:8px 0 10px}
header p{color:var(--muted);max-width:860px;margin:0}
nav{position:sticky;top:0;z-index:5;background:color-mix(in srgb,var(--bg) 88%,transparent);backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
nav div{max-width:1240px;margin:0 auto;padding:10px 16px;display:flex;gap:6px;overflow-x:auto}
nav a{flex:none;display:flex;gap:8px;align-items:center;padding:6px 12px;border-radius:99px;border:1px solid var(--line);background:var(--card);color:var(--ink);text-decoration:none;font-size:13px;font-weight:550;white-space:nowrap}
nav a b{color:var(--acc);font-weight:700}
main{max-width:1240px;margin:0 auto;padding:16px 16px 60px}
section{margin-top:34px;scroll-margin-top:70px}
section h2{display:flex;gap:12px;align-items:baseline;font:700 20px Inter;margin:0 0 12px}
section h2 span{color:var(--acc);font-size:14px}
.cadre{position:relative;width:100%;overflow:hidden;border-radius:14px;border:1px solid var(--line);box-shadow:0 10px 30px rgba(15,23,42,.10);background:#fff}
.cadre iframe{position:absolute;left:0;top:0;border:0;transform-origin:0 0;display:block}
footer{max-width:1240px;margin:0 auto;padding:0 16px 40px;color:var(--muted);font-size:13px}
</style></head><body>
<header><div class="o">Proposition · version 1.47</div><h1>Les statistiques des admins, des courbes partout, et le globe</h1>
<p>Beaucoup plus de chiffres sur chaque admin et sur l'admin+ (sa fiche, son activité, ce qu'il produit, sa sécurité), une vue d'ensemble, les lignes qui se déplient et le clic droit dans le même ordre partout ; puis les courbes (ventes, mises à jour, PC, licences, par client, par admin) et le globe qui tourne. Les noms et les chiffres sont des exemples d'essai.</p></header>
<nav><div>__NAV__</div></nav>
<main>__CARTES__</main>
<footer>Maquette — rien de ce qui est dessiné ici n'est encore dans les logiciels. À valider (ou à corriger) avant de le construire.</footer>
<script>
function ajuster(){document.querySelectorAll('.cadre').forEach(function(c){var f=c.querySelector('iframe'),h=+c.dataset.h,s=Math.min(1,c.clientWidth/1600);f.style.transform='scale('+s+')';c.style.height=Math.round(h*s)+'px';});}
window.addEventListener('resize',ajuster);ajuster();
</script>
</body></html>"""
page = page.replace("__NAV__", nav).replace("__CARTES__", cartes)
open(OUT, "w", encoding="utf-8").write(page)
print(OUT, len(PLANCHES), "planches,", len(page.encode()) // 1024, "Kio")
