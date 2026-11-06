#!/bin/bash
# =============================================================================
#  outils/mingw/admin_editeur.sh - 1.30.0 : l'outil de l'EDITEUR pour les licences
#  (XPG-License-Admin.exe), construit avec MinGW et emballe A PART
# -----------------------------------------------------------------------------
#  JAMAIS LIVRE A UN CLIENT : ni cross_mingw.sh ni package_mingw.py ne le copient
#  (le controle du paquet arrete la construction s'il trouve son nom ou sa marque
#  dans le dossier a livrer). Ce script fait un dossier et un zip pour l'EDITEUR
#  seul, hors de dist/ :
#
#    outils/mingw/admin_editeur.sh [DOSSIER]
#      -> DOSSIER/editeur-<version>/      XPG-License-Admin.exe
#                                         LISEZ-MOI-EDITEUR.txt (CRLF : le pas a pas)
#                                         registre.json (= tests/fixtures/licences/registre-exemple.json)
#                                         parametres-exemple.json
#      -> DOSSIER/XPG-License-Admin-<version>-EDITEUR.zip
#    DOSSIER : par defaut <racine>/dist-editeur (jamais dans dist/).
#    XPG_VERSION=1.30.0   la version (par defaut : outils/config.ini, [Produit] version)
#    OUT=build-mingw      le dossier de construction MinGW (configure par cross_mingw.sh)
#    JOBS=2               les compilations a la fois
#    XPG_NINJA_LOCK=...   un verrou (flock) partage avec d'autres constructions
#
#  Seule la cible xpg_license_admin est construite (ninja ... xpg_license_admin).
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
OUT=${OUT:-$ROOT/build-mingw}
JOBS=${JOBS:-2}
SORTIE=${1:-$ROOT/dist-editeur}
VERSION=${XPG_VERSION:-$(sed -n '/^\[Produit\]/,/^\[/{s/^version[[:space:]]*=[[:space:]]*\([0-9.]*\).*/\1/p}' outils/config.ini | head -1)}
if [ -z "$VERSION" ]; then
    echo "la version est introuvable (outils/config.ini, [Produit] version ; ou XPG_VERSION=...)" >&2
    exit 2
fi
mkdir -p "$SORTIE"
SORTIE=$(cd "$SORTIE" && pwd)
case "$SORTIE/" in
    "$ROOT/dist/"*) echo "refuse : $SORTIE est dans dist/ (la livraison des clients) ; donne un autre dossier" >&2; exit 2 ;;
esac
if [ ! -f "$OUT/build.ninja" ]; then
    echo "$OUT n'est pas configure : lance d'abord outils/mingw/cross_mingw.sh" >&2
    exit 2
fi

# ---- 1. construire l'outil (cette cible seulement) ----
if [ -n "${XPG_NINJA_LOCK:-}" ]; then
    flock "$XPG_NINJA_LOCK" ninja -C "$OUT" -j "$JOBS" xpg_license_admin
else
    ninja -C "$OUT" -j "$JOBS" xpg_license_admin
fi
EXE="$OUT/XPG-License-Admin.exe"
[ -f "$EXE" ] || { echo "$EXE manque apres la construction" >&2; exit 1; }
grep -q "XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER" "$EXE" || { echo "$EXE ne porte pas la marque de l'outil Admin : ce n'est pas lui" >&2; exit 1; }
if x86_64-w64-mingw32-objdump -p "$EXE" | grep -q "Subsystem.*(Windows GUI)"; then
    echo "$EXE n'est pas un programme en console (-mconsole manque)" >&2
    exit 1
fi

# ---- 2. le dossier de l'editeur ----
NOM="editeur-$VERSION"
DEST="$SORTIE/$NOM"
rm -rf "$DEST"
mkdir -p "$DEST"
cp -f "$EXE" "$DEST/XPG-License-Admin.exe"
cp -f tests/fixtures/licences/registre-exemple.json "$DEST/registre.json"
cp -f tests/fixtures/licences/parametres-exemple.json "$DEST/parametres-exemple.json"
# Le pas a pas : UTF-8 avec BOM, fins de ligne CRLF (le Bloc-notes), la version mise.
python3 - "$ROOT/outils/mingw/LISEZ-MOI-EDITEUR.txt" "$DEST/LISEZ-MOI-EDITEUR.txt" "$VERSION" <<'PY'
import sys
src, dst, version = sys.argv[1:4]
with open(src, encoding="utf-8-sig") as f:
    text = f.read().replace("{VERSION}", version)
text = text.replace("\r\n", "\n").replace("\n", "\r\n")
with open(dst, "w", encoding="utf-8-sig", newline="") as f:
    f.write(text)
PY

# ---- 3. le zip (un dossier editeur-<version> dedans) ----
ZIP="$SORTIE/XPG-License-Admin-$VERSION-EDITEUR.zip"
rm -f "$ZIP"
python3 - "$SORTIE" "$NOM" "$ZIP" <<'PY'
import os, sys, zipfile
base, nom, archive = sys.argv[1:4]
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name in sorted(os.listdir(os.path.join(base, nom))):
        z.write(os.path.join(base, nom, name), nom + "/" + name)
with zipfile.ZipFile(archive) as z:
    for i in z.infolist():
        print(f"  {i.filename}  ({i.file_size} octets)")
PY
echo "outil de l'editeur : $DEST"
echo "zip de l'editeur   : $ZIP ($(( $(stat -c %s "$ZIP") / 1024 )) Kio)"
echo "A ne donner qu'a l'editeur : jamais dans l'installateur ni dans un zip client."
