#!/usr/bin/env python3
# =============================================================================
#  outils/web/essais.py - 1.12.31 : les essais de la page web EN MARCHE
# -----------------------------------------------------------------------------
#  essais.py moteur   le moteur wasm de l'IHM sous Node (sans navigateur) :
#                     cycles, valeurs, script cyclique, consigne, alarme, et le
#                     WAIT d'un script detache (Asyncify)
#  essais.py page     la page en marche dans Chromium (Playwright) : aucune erreur
#                     JS, la vue se dessine et change, un clic de navigation, une
#                     popup, une consigne saisie, un curseur, le WAIT d'un script
#                     detache, une alarme, la documentation du projet
#
#  Un essai qui a besoin de Node, de Playwright ou du moteur wasm
#  (third_party/web/ihm-web.wasm, fait par construire_web.sh) se SAUTE
#  proprement la ou il manque : code de sortie 77 (ctest : SKIP_RETURN_CODE).
# =============================================================================
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

RACINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAUTE = 77


def saute(pourquoi):
    print("essai saute : " + pourquoi)
    sys.exit(SAUTE)


def node():
    for c in (os.environ.get("XPG_NODE"), shutil.which("node"), "/opt/node22/bin/node"):
        if c and os.path.exists(c):
            return c
    return None


def moteur_wasm():
    p = os.environ.get("XPG_WEB_WASM") or os.path.join(RACINE, "third_party", "web", "ihm-web.wasm")
    return p if os.path.exists(p) else None


def archive_essai(dossier_tmp):
    """Le projet d'essai (tests/fixtures/web/essai, du texte) en archive .xihm."""
    src = os.path.join(RACINE, "tests", "fixtures", "web", "essai")
    out = os.path.join(dossier_tmp, "essai.xihm")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for base, _, fichiers in os.walk(src):
            for f in sorted(fichiers):
                plein = os.path.join(base, f)
                z.write(plein, os.path.relpath(plein, src).replace(os.sep, "/"))
    return out


def essai_moteur():
    n = node()
    if not n:
        saute("Node absent")
    w = moteur_wasm()
    if not w:
        saute("moteur wasm absent (outils/web/construire_web.sh)")
    with tempfile.TemporaryDirectory() as tmp:
        xihm = archive_essai(tmp)
        r = subprocess.run([n, "--no-warnings", os.path.join(RACINE, "outils", "web", "essai_node.mjs"), w, xihm])
        return r.returncode


def essai_page():
    n = node()
    if not n:
        saute("Node absent")
    if not os.path.exists(os.path.join(RACINE, "third_party", "web", "ihm-web.wasm.gz")):
        saute("moteur wasm compresse absent (outils/web/construire_web.sh)")
    script = os.path.join(RACINE, "outils", "web", "essai_page.mjs")
    if not os.path.exists(script):
        saute("essai de la page absent")
    r = subprocess.run([n, "--no-warnings", script, RACINE])
    return r.returncode


if __name__ == "__main__":
    quoi = sys.argv[1] if len(sys.argv) > 1 else "moteur"
    if quoi == "moteur":
        sys.exit(essai_moteur())
    if quoi == "page":
        sys.exit(essai_page())
    print("usage : essais.py moteur|page")
    sys.exit(2)
