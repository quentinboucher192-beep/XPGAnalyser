# Une seule page HTML autonome : les planches de la maquette 1.43.0 (styles, icones, images, polices dedans)
import base64, html, os, re, sys
M = sys.argv[1]
OUT = sys.argv[2]
POL = "/home/user/XPGAnalyser/installateur/polices/"
def data(path, mime):
    return "data:%s;base64,%s" % (mime, base64.b64encode(open(path, "rb").read()).decode())
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
         '<link href="https://fonts.googleapis.com/css2?family=Inter:opsz,wght@14..32,400..800&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet">'
         '<style>@font-face{font-family:"Inter Display";src:local("Inter Display"),local("Inter");}</style>')
def board(name):
    s = open(os.path.join(M, name), encoding="utf-8").read()
    for css in ("ds.css", "admin.css", "studio.css", "extra.css", "licwin.css"):
        s = s.replace('<link rel="stylesheet" href="%s">' % css, "<style>%s</style>" % open(os.path.join(M, css), encoding="utf-8").read())
    s = s.replace('<script src="icons.js"></script>', "<script>%s</script>" % open(os.path.join(M, "icons.js"), encoding="utf-8").read())
    s = s.replace("<head>", "<head>" + FONTS, 1)
    for f in ("Regular", "Medium", "SemiBold", "Bold"):
        s = s.replace("url(/home/user/XPGAnalyser/installateur/polices/Barlow-%s.ttf)" % f, "url(%s)" % data(POL + "Barlow-%s.ttf" % f, "font/ttf"))
    s = re.sub(r'(url\(|src=")(v/[0-9a-z-]+\.png|avant-[a-z]+\.png)', lambda m: m.group(1) + data(os.path.join(M, m.group(2)), "image/png"), s)
    assert "/home/user" not in s and 'href="' not in s.replace('href="https://fonts', "").replace('href="#', ""), name
    return s
PLANCHES = [
    ("00-admins-principe.html", "Plusieurs admins — le principe", 1000),
    ("01-admin-admins.html", "Admin — la page Administrateurs", 1000),
    ("02-admin-ajouter.html", "Admin — ajouter un admin (l'empreinte)", 1000),
    ("03-admin-journal.html", "Admin — le journal des admins", 1000),
    ("04-admin-criticite.html", "Admin — la criticité des actions", 1000),
    ("05-studio-notif-fichier.html", "Studio — une notification avec un fichier", 1000),
    ("06-studio-mise-a-jour.html", "Studios et Admin — la mise à jour", 1000),
    ("07-admin-versions.html", "Admin — publier une version", 1000),
]
nav = "".join('<a href="#p%d"><b>%02d</b>%s</a>' % (i, i, html.escape(t)) for i, (_, t, _) in enumerate(PLANCHES))
cartes = ""
for i, (f, t, h) in enumerate(PLANCHES):
    cartes += ('<section id="p%d"><h2><span>%02d</span>%s</h2><div class="cadre" data-h="%d"><iframe title="%s" width="1600" height="%d" '
               'loading="lazy" srcdoc="%s"></iframe></div></section>\n') % (i, i, html.escape(t), h, html.escape(t), h, html.escape(board(f), quote=True))
page = """<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Maquette XPG 1.43</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:opsz,wght@14..32,400..800&display=swap" rel="stylesheet">
<style>
:root{--bg:#f4f6fa;--ink:#0f172a;--muted:#64748b;--line:#e3e8ef;--card:#fff;--acc:#6d4aff}
@media (prefers-color-scheme: dark){:root{--bg:#0b1020;--ink:#e5e9f2;--muted:#94a3b8;--line:#1f2a44;--card:#111a2e}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 Inter,system-ui,sans-serif}
header{max-width:1240px;margin:0 auto;padding:40px 16px 8px}
header .o{font:700 12px Inter;letter-spacing:.1em;text-transform:uppercase;color:var(--acc)}
header h1{font:750 34px/1.15 Inter;letter-spacing:-.02em;margin:8px 0 10px}
header p{color:var(--muted);max-width:820px;margin:0}
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
<header><div class="o">Proposition · version 1.43</div><h1>Plusieurs admins, le journal, et les mises à jour</h1>
<p>L'Admin sur plusieurs PC : chaque admin a sa clé, l'admin+ garde la clé de l'éditeur, ajoute et retire les admins, lit le journal de leurs actions (trié, avec une criticité). Des fichiers dans les notifications, et les mises à jour qui arrivent seules, avec les 2 installateurs pour les admins. Les noms et les chiffres sont des exemples d'essai.</p></header>
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
print(OUT, len(page.encode()) // 1024, "Kio")
