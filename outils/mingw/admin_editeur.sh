#!/bin/bash
# =============================================================================
#  outils/mingw/admin_editeur.sh - 1.30.0 : l'outil de l'EDITEUR pour les licences
#  (XPG-License-Admin.exe), construit avec MinGW et emballe A PART ; 1.33.0 : et son
#  application (XPG-License-Manager-Admin.exe, ses fenetres ; SDL3.dll a cote)
# -----------------------------------------------------------------------------
#  JAMAIS LIVRE A UN CLIENT : ni cross_mingw.sh ni package_mingw.py ne le copient
#  (le controle du paquet arrete la construction s'il trouve son nom ou sa marque
#  dans le dossier a livrer). Ce script fait un dossier et un zip pour l'EDITEUR
#  seul, hors de dist/ :
#
#    outils/mingw/admin_editeur.sh [DOSSIER]
#      -> DOSSIER/editeur-<version>/      XPG-License-Manager-Admin.exe (1.33.0 : l'application) et SDL3.dll
#                                         XPG-License-Admin.exe (la console : les scripts)
#                                         LISEZ-MOI-EDITEUR.txt (CRLF : le pas a pas)
#                                         registre.json (= tests/fixtures/licences/registre-exemple.json)
#                                         parametres-exemple.json
#                                         service-licences/ (1.35.0 : le kit de mise en ligne du service des
#                                           licences - service/ sans node_modules, deployer.bat/.sh/.mjs,
#                                           LISEZ-MOI-SERVICE.txt ; l'editeur y ajoute cle-publique.txt)
#                                         connexion-service.xpgsvc (1.47.0, s'il y en a une : la connexion au
#                                           service - le certificat signe par l'editeur, rien de secret ;
#                                           l'installateur la copie a cote du programme, l'Admin d'un futur
#                                           admin demarre connecte ; docs/ADMINS.md, par. 11)
#      -> DOSSIER/XPG-License-Admin-Setup-<version>-EDITEUR.exe  1.40.0 : L'INSTALLATEUR (un seul .exe, a la
#                                         place du zip : installateur/XPG-License-Admin.iss) - avec ISCC=...
#      -> DOSSIER/XPG-License-Admin-<version>-EDITEUR.zip   (seulement avec ZIP=1 : l'ancien paquet)
#    DOSSIER : par defaut <racine>/dist-editeur (jamais dans dist/).
#    ISCC=/chemin/ISCC.exe Inno Setup 6.3 ou plus (sous Wine hors de Windows) ; sans lui : le dossier seulement
#    XPG_VERSION=1.30.0   la version (par defaut : outils/config.ini, [Produit] version)
#    OUT=build-mingw      le dossier de construction MinGW (configure par cross_mingw.sh)
#    JOBS=2               les compilations a la fois
#    XPG_NINJA_LOCK=...   un verrou (flock) partage avec d'autres constructions
#    CONNEXION=F.xpgsvc   1.47.0 : la connexion au service a porter (par defaut ressources-licences/connexion-service.xpgsvc,
#                         ecrite par kit-studios ou Service en ligne > Exporter la connexion ; aucune : rien n'est porte)
#
#  Seules les cibles de l'editeur sont construites (ninja ... xpg_license_admin xpg_license_manager_admin).
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
    flock "$XPG_NINJA_LOCK" ninja -C "$OUT" -j "$JOBS" xpg_license_admin xpg_license_manager_admin
else
    ninja -C "$OUT" -j "$JOBS" xpg_license_admin xpg_license_manager_admin
fi
EXE="$OUT/XPG-License-Admin.exe"
[ -f "$EXE" ] || { echo "$EXE manque apres la construction" >&2; exit 1; }
grep -q "XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER" "$EXE" || { echo "$EXE ne porte pas la marque de l'outil Admin : ce n'est pas lui" >&2; exit 1; }
# (la sortie d'objdump lue en entier : avec pipefail, un grep -q qui s'arrete tot ferait echouer le tube)
SOUS_SYSTEME_EXE=$(x86_64-w64-mingw32-objdump -p "$EXE" | grep "Subsystem" || true)
if echo "$SOUS_SYSTEME_EXE" | grep -q "(Windows GUI)"; then
    echo "$EXE n'est pas un programme en console (-mconsole manque)" >&2
    exit 1
fi
# 1.33.0 : l'application (une fenetre : sous-systeme Windows GUI), elle aussi marquee.
APP="$OUT/XPG-License-Manager-Admin.exe"
[ -f "$APP" ] || { echo "$APP manque apres la construction" >&2; exit 1; }
grep -q "XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER" "$APP" || { echo "$APP ne porte pas la marque de l'outil Admin : ce n'est pas lui" >&2; exit 1; }
SOUS_SYSTEME_APP=$(x86_64-w64-mingw32-objdump -p "$APP" | grep "Subsystem" || true)
echo "$SOUS_SYSTEME_APP" | grep -q "(Windows GUI)" || { echo "$APP n'est pas une application a fenetre" >&2; exit 1; }
[ -f "$OUT/SDL3.dll" ] || { echo "$OUT/SDL3.dll manque (l'application en a besoin)" >&2; exit 1; }

# ---- 2. le dossier de l'editeur ----
NOM="editeur-$VERSION"
DEST="$SORTIE/$NOM"
rm -rf "$DEST"
mkdir -p "$DEST"
cp -f "$EXE" "$DEST/XPG-License-Admin.exe"
cp -f "$APP" "$DEST/XPG-License-Manager-Admin.exe"
cp -f "$OUT/SDL3.dll" "$DEST/SDL3.dll"
# 1.41.0 : la police de l'interface (Inter, deux graisses, licence SIL OFL)
mkdir -p "$DEST/resources/fonts"
cp -f resources/fonts/* "$DEST/resources/fonts/"
cp -f tests/fixtures/licences/registre-exemple.json "$DEST/registre.json"
cp -f tests/fixtures/licences/parametres-exemple.json "$DEST/parametres-exemple.json"
# 1.35.0 : le kit du service des licences (le Worker, sans ses essais ni node_modules ; les scripts de mise en ligne)
KIT="$DEST/service-licences"
mkdir -p "$KIT/service/src"
cp -f service/wrangler.toml service/package.json service/package-lock.json service/.gitignore "$KIT/service/"
cp -f service/src/*.js "$KIT/service/src/"
cp -f outils/service/deployer.bat outils/service/deployer.mjs outils/service/LISEZ-MOI-SERVICE.txt "$KIT/"
cp -f outils/service/deployer.sh "$KIT/deployer.sh" && chmod 755 "$KIT/deployer.sh"
# 1.47.0 : la connexion au service (le certificat du service signe par la cle de l'editeur : son adresse et sa cle ; rien de
# secret). L'installateur la copie a cote du programme ; l'Admin la verifie au premier lancement (ServiceConnection.hpp).
CONN=${CONNEXION:-$ROOT/ressources-licences/connexion-service.xpgsvc}
if [ -f "$CONN" ]; then
    grep -q "^XPG-LICENSING 1 service " "$CONN" || { echo "$CONN n'est pas une connexion au service (le bloc << service >> signe)" >&2; exit 1; }
    if grep -q -i -E "PRIVATE|secret|sk_live|sk_test|whsec_" "$CONN"; then echo "$CONN porte autre chose qu'un certificat public : refuse" >&2; exit 1; fi
    cp -f "$CONN" "$DEST/connexion-service.xpgsvc"
    echo "connexion au service : $(basename "$CONN") (portee par l'installateur)"
else
    echo "(pas de connexion au service a porter : ressources-licences/connexion-service.xpgsvc ou CONNEXION=... ; l'Admin la prendra a cote de l'installateur ou du Studio du PC)"
fi
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

# ---- 3. 1.40.0 : l'installateur (un seul .exe : l'application, la console, le service, les notes ; ses options) ----
SETUP="$SORTIE/XPG-License-Admin-Setup-$VERSION-EDITEUR.exe"
rm -f "$SETUP"
if [ -n "${ISCC:-}" ]; then
    [ -f "$ISCC" ] || { echo "ISCC introuvable : $ISCC" >&2; exit 2; }
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) W() { cygpath -w "$1"; }; LANCE=("$ISCC") ;;   # sous Windows
        *) W() { winepath -w "$1"; }; LANCE=(wine "$ISCC") ;;                 # ailleurs : sous Wine
    esac
    "${LANCE[@]}" /Q "/DVersion=$VERSION" "/DSource=$(W "$DEST")" "/DSortie=$(W "$SORTIE")" "$(W "$ROOT/installateur/XPG-License-Admin.iss")"
    [ -f "$SETUP" ] || { echo "l'installateur n'a pas ete fait ($SETUP)" >&2; exit 1; }
    grep -q "XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER" "$DEST/XPG-License-Manager-Admin.exe" || { echo "marque absente" >&2; exit 1; }
    echo "installateur de l'editeur : $SETUP ($(( $(stat -c %s "$SETUP") / 1024 )) Kio)"
else
    echo "(pas d'installateur : ISCC=/chemin/ISCC.exe pour le faire)"
fi
[ "${ZIP:-0}" = "1" ] || { echo "outil de l'editeur : $DEST"; exit 0; }

# ---- 4. le zip (un dossier editeur-<version> dedans) : seulement avec ZIP=1 ----
ZIP="$SORTIE/XPG-License-Admin-$VERSION-EDITEUR.zip"
rm -f "$ZIP"
python3 - "$SORTIE" "$NOM" "$ZIP" <<'PY'
import os, sys, zipfile
base, nom, archive = sys.argv[1:4]
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    racine = os.path.join(base, nom)
    for dossier, sous, fichiers in os.walk(racine):
        sous.sort()
        for name in sorted(fichiers):
            chemin = os.path.join(dossier, name)
            z.write(chemin, nom + "/" + os.path.relpath(chemin, racine).replace(os.sep, "/"))
with zipfile.ZipFile(archive) as z:
    for i in z.infolist():
        print(f"  {i.filename}  ({i.file_size} octets)")
PY
echo "outil de l'editeur : $DEST"
echo "zip de l'editeur   : $ZIP ($(( $(stat -c %s "$ZIP") / 1024 )) Kio)"
echo "A ne donner qu'a l'editeur : jamais dans l'installateur ni dans un zip client."
