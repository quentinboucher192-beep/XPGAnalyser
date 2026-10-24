#!/usr/bin/env python3
# =============================================================================
#  outils/web/faire_page.py - 1.12.31 : une page web EN MARCHE pour les essais
# -----------------------------------------------------------------------------
#  Remplit outils/web/page_ihm.html (le meme gabarit que l'export d'IHM Studio,
#  app/HmiWebPage.cpp) avec : le moteur wasm compresse, la police, l'archive d'un
#  projet (.xihm), une documentation minimale, l'hote JS et l'amorce. Sert aux
#  essais de la page (Playwright) sans passer par l'application.
#
#    faire_page.py <archive.xihm> <page.html> [titre]
# =============================================================================
import base64
import hashlib
import html
import io
import os
import re
import sys
import zipfile

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(os.path.dirname(ICI))


def lire(p, mode="r"):
    with open(p, mode) as f:
        return f.read()


def documentation(archive):
    """Une documentation minimale du projet (l'export d'IHM Studio fait la vraie) : ses alarmes,
    ses variables et ses scripts, lus dans ihm/ihm.txt de l'archive."""
    try:
        with zipfile.ZipFile(io.BytesIO(archive)) as z:
            texte = z.read("ihm/ihm.txt").decode("utf-8", "replace")
    except (KeyError, zipfile.BadZipFile):
        texte = ""
    attr = lambda ligne, cle: html.escape((re.search(cle + r'="([^"]*)"', ligne) or [None, ""])[1])
    lignes = texte.splitlines()
    def tableau(debut, cles):
        rangs = ["<tr>" + "".join("<td>%s</td>" % attr(l, c) for c in cles) + "</tr>" for l in lignes if l.startswith(debut + " ")]
        return "<table>%s</table>" % "".join(rangs) if rangs else "<p>Aucune.</p>"
    return ('<!doctype html><html lang="fr"><head><meta charset="utf-8"><title>Documentation</title></head><body>'
            '<section id="alarmes"><h2>Alarmes</h2>' + tableau("alarme", ["nom", "condition", "message"]) + '</section>'
            '<section id="variables"><h2>Variables IHM</h2>' + tableau("variable", ["nom", "type", "initiale"]) + '</section>'
            '<section id="scripts"><h2>Scripts et fonctions</h2>' + tableau("script", ["nom", "evenement"]) + '</section>'
            '<section id="fiche"><h2>Fiche du projet</h2><p>La fiche du projet.</p></section></body></html>')


def main():
    if len(sys.argv) < 3:
        print("usage : faire_page.py <archive.xihm> <page.html> [titre]")
        return 2
    archive = lire(sys.argv[1], "rb")
    titre = sys.argv[3] if len(sys.argv) > 3 else "Essai web"
    wasm_gz = os.environ.get("XPG_WEB_WASM_GZ") or os.path.join(RACINE, "third_party", "web", "ihm-web.wasm.gz")
    if not os.path.exists(wasm_gz):
        print("moteur absent : " + wasm_gz)
        return 3
    moteur = base64.b64encode(lire(wasm_gz, "rb")).decode()
    police = base64.b64encode(lire(os.path.join(RACINE, "installateur", "polices", "Barlow-Regular.ttf"), "rb")).decode()
    nom = os.path.basename(sys.argv[1])
    bloc = ('<script type="application/x-xpg-archive" id="xpg-archive" data-genre="ihm" data-nom="%s" data-octets="%d" '
            'data-sha256="%s">\n%s\n</script>\n') % (nom, len(archive), hashlib.sha256(archive).hexdigest(),
                                                       base64.b64encode(archive).decode())
    doc = documentation(archive)
    bloc_doc = '<script type="application/x-xpg-doc" id="xpg-doc">%s</script>' % base64.b64encode(doc.encode("utf-8")).decode()
    remplace = {
        "{{APPLICATION}}": "IHM Studio (essai)",
        "{{TITRE}}": html.escape(titre),
        "{{ARCHIVE_MARKER}}": '<meta name="xpg-archive" content="ihm">',
        "{{DOC_BLOCK}}": bloc_doc,
        "{{DOWNLOAD_BUTTON}}": '<button type="button" id="dl">Télécharger le projet</button>',
        "{{MOTEUR_GZIP}}": "1",
        "{{MOTEUR}}": moteur,
        "{{POLICE}}": police,
        "{{ARCHIVE_BLOCK}}": bloc,
        "{{HOTE}}": lire(os.path.join(ICI, "web_host.js")),
        "{{AMORCE}}": lire(os.path.join(ICI, "amorce.js")),
    }
    page = lire(os.path.join(ICI, "page_ihm.html"))
    for k, v in remplace.items():
        page = page.replace(k, v)
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(page)
    print("page ecrite : %s (%d octets)" % (sys.argv[2], len(page.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
