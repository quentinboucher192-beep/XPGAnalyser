#!/bin/bash
# =============================================================================
#  outils/web/construire_web.sh - 1.12.31 : le moteur de l'IHM compile en
#  WebAssembly, pour la page web EN MARCHE (IHM Studio > Projet > Exporter en
#  page web). La page embarque ce .wasm et le fait tourner : les memes vues, les
#  memes scripts, les memes alarmes - sur un navigateur.
# -----------------------------------------------------------------------------
#  Il faut la chaine wasm (Debian / Ubuntu : clang-18 lld-18 binaryen
#  wasi-libc libc++-18-dev-wasm32 libclang-rt-18-dev-wasm32) :
#    clang++ --target=wasm32-wasi (clang 18), wasm-ld, binaryen (wasm-opt),
#    la libc++ wasm32-wasi (/usr/lib/wasm32-wasi, en-tetes dans /usr/include).
#  Rien d'autre : ni Emscripten, ni Internet. Les objets restent dans
#  build-web/obj (refaits quand leur source ou un en-tete change).
#
#  Resultat (ignore par git, livre avec l'application, comme pdfium.dll) :
#    third_party/web/ihm-web.wasm       le moteur
#    third_party/web/ihm-web.wasm.gz    comprime (la page le decomprime avec
#                                       DecompressionStream, sinon le .wasm nu)
#
#  Sans la chaine, ou en cas d'echec, la page reste celle de la 1.12.30 (au
#  repos) : l'export le dit.
#
#  Variables : CLANGXX, WASMOPT, OUT, OBJDIR, JOBS (2 au plus : memoire comptee),
#  XPG_OPT (-O1), NOMS=1 (garde les noms des fonctions : des piles lisibles dans la
#  console du navigateur, pour chercher une erreur - un .wasm plus gros, pas a livrer).
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$(pwd)

CLANGXX=${CLANGXX:-clang++}
WASMOPT=${WASMOPT:-wasm-opt}
JOBS=${JOBS:-2}
OUTDIR=${OUT:-$ROOT/third_party/web}
OBJDIR=${OBJDIR:-$ROOT/build-web/obj}
SYSROOT_CXX=/usr/include/wasm32-wasi/c++/v1

# La chaine est-elle la ?
if ! command -v "$CLANGXX" >/dev/null 2>&1; then
    echo "construire_web : $CLANGXX absent - pas de chaine wasm, la page restera celle de la 1.12.30" >&2
    exit 3
fi
if [ ! -d "$SYSROOT_CXX" ]; then
    echo "construire_web : la libc++ wasm32-wasi manque ($SYSROOT_CXX) - pas de chaine wasm" >&2
    exit 3
fi

mkdir -p "$OBJDIR" "$OUTDIR"
COMPAT="$ROOT/outils/web/compat"
# __config_site sans _LIBCPP_HAS_NO_FILESYSTEM : <fstream> marche en memoire (WASI).
OV="$OBJDIR/ov"
mkdir -p "$OV"
sed '/_LIBCPP_HAS_NO_FILESYSTEM/d' "$SYSROOT_CXX/__config_site" > "$OV/__config_site"

INCLUDES="-isystem $COMPAT -isystem $OV -I$ROOT/src -I$ROOT/third_party"
CXXFLAGS="--target=wasm32-wasi -std=c++20 -DXPG_WEB=1 \
  ${XPG_OPT:--O1} -fno-strict-aliasing -w $INCLUDES"

# Les options de compilation ont change (une autre chaine, d'autres -D, -f...) : tous
# les objets se refont - un objet d'avant (sans RTTI, par exemple) casserait le moteur
# sans rien dire (un dynamic_cast qui saute dans le vide).
STAMP="$OBJDIR/options.txt"
if [ ! -f "$STAMP" ] || [ "$(cat "$STAMP")" != "$CLANGXX $CXXFLAGS" ]; then
    [ -d "$OBJDIR" ] && find "$OBJDIR" -maxdepth 1 \( -name '*.o' -o -name '*.o.d' -o -name '*.o.err' \) -delete
    printf '%s' "$CLANGXX $CXXFLAGS" > "$STAMP"
    echo "construire_web : options nouvelles, tout se refait"
fi

# ---- les fichiers du moteur (curated) ---------------------------------------
#  Les bibliotheques non graphiques (core, sim, domain, project, import, export,
#  xls, hmi), moins ce qu'une page ne peut pas faire (sockets, fils de fond,
#  PDFium, Media Foundation, serveur web, scan reseau) : src/web/ les remplace.
mapfile -t FILES < <(
    for d in core sim domain project import export xls hmi; do
        find "$ROOT/src/$d" -name '*.cpp'
    done
    find "$ROOT/src/web" -name '*.cpp'
)

# Ce qui ne va pas sur une page : les sockets (HmiNet, HmiNetInfo, HmiSpy, HmiIpScan,
# HmiModbusTool, HmiModbusCyclic : src/web/WebNetStub.cpp les remplace), les fils et
# le chien de garde (CrashGuard, SingleInstance, HmiScriptTask : src/web/WebScriptTask.cpp),
# le serveur web (HmiWeb), le courriel et les SMS (HmiNotify), PDFium (HmiPdf :
# src/web/WebStubs.cpp), le poste lance a part (HmiStandalone), l'import de projets
# d'autres logiciels (ProjectImporter), l'aide F1 et les didacticiels qui la lisent
# (HmiGuideText, HmiTutorialDeduce : la page n'a pas l'aide de l'editeur). HmiVideoMf y
# est (hors Windows : sans Media Foundation, le Motion-JPEG seul), HmiExternal aussi
# (sans disque : "illisible").
BLOCK='/(CrashGuard|SingleInstance|ProjectImporter|HmiNet|HmiNetInfo|HmiSpy|HmiIpScan|HmiModbusTool|HmiModbusCyclic|HmiWeb|HmiNotify|HmiPdf|HmiStandalone|HmiScriptTask|HmiGuideText|HmiTutorialDeduce)\.cpp$'

OBJS=()
SRCS=()
for f in "${FILES[@]}"; do
    [[ "$f" =~ $BLOCK ]] && continue
    SRCS+=("$f")
done

echo "construire_web : ${#SRCS[@]} fichiers a compiler (-j$JOBS)"

# Un objet a refaire : son .cpp ou l'un de ses en-tetes (le .d que clang ecrit) est plus recent.
# Un objet sans .d (d'avant) : ses dependances se relisent d'abord (le preprocesseur seul).
stale() {
    local src="$1" obj="$2"
    [ -f "$obj" ] || return 0
    if [ ! -f "$obj.d" ]; then
        $CLANGXX $CXXFLAGS -MM -MT "$obj" -MF "$obj.d" "$src" 2>/dev/null || return 0
    fi
    local dep
    for dep in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$obj.d"); do
        [ "$dep" -nt "$obj" ] && return 0
    done
    return 1
}
export -f stale

compile_one() {
    local src="$1"
    local obj="$OBJDIR/$(echo "$src" | sed "s#$ROOT/##; s#[/.]#_#g").o"
    if ! stale "$src" "$obj"; then echo "$obj"; return 0; fi
    if ! $CLANGXX $CXXFLAGS -MMD -MT "$obj" -MF "$obj.d" -c "$src" -o "$obj" 2> "$obj.err"; then
        echo "ECHEC $src" >&2
        sed 's/^/    /' "$obj.err" >&2
        return 1
    fi
    echo "$obj"
}
export -f compile_one
export CLANGXX CXXFLAGS OBJDIR ROOT

printf '%s\n' "${SRCS[@]}" | xargs -P "$JOBS" -I{} bash -c 'compile_one "$@"' _ {} > "$OBJDIR/objs.list" 2> "$OBJDIR/compile.err" || {
    echo "construire_web : des fichiers n'ont pas compile :" >&2
    cat "$OBJDIR/compile.err" >&2
    exit 1
}
mapfile -t OBJS < "$OBJDIR/objs.list"

# ---- le dessin : les MEMES peintres que l'appli -----------------------------
#  paintHmiView et sa famille (app/hmi), la police (platform/FontAtlas), le theme
#  et les formes (ui). Seulement ces fichiers-la de l'interface : le reste de
#  l'ecran (fenetres, panneaux, dialogues) n'est pas sur la page - elle a son
#  canevas (src/web/WebCanvas) a la place de HmiLiveCanvas.
PAINT=(
    platform/FontAtlas.cpp ui/Theme.cpp ui/Shapes.cpp
    app/hmi/HmiPainter.cpp app/hmi/HmiIcons.cpp app/hmi/HmiImages.cpp
    app/hmi/HmiControlsPainter.cpp app/hmi/HmiSynopticPainter.cpp app/hmi/HmiChartsPainter.cpp
    app/hmi/HmiDocPainter.cpp app/hmi/HmiLoginPainter.cpp app/hmi/HmiLot11Painter.cpp
    app/hmi/HmiLot13Painter.cpp app/hmi/HmiLot14Painter.cpp app/hmi/HmiNavPainter.cpp
    app/hmi/HmiPidPainter.cpp app/hmi/HmiSystemPainter.cpp
)
PSRCS=()
for f in "${PAINT[@]}"; do PSRCS+=("$ROOT/src/$f"); done
printf '%s\n' "${PSRCS[@]}" | xargs -P "$JOBS" -I{} bash -c 'compile_one "$@"' _ {} > "$OBJDIR/paint.list" 2> "$OBJDIR/paint.err" || {
    echo "construire_web : des peintres n'ont pas compile :" >&2
    cat "$OBJDIR/paint.err" >&2
    exit 1
}
mapfile -t -O "${#OBJS[@]}" OBJS < "$OBJDIR/paint.list"
echo "construire_web : ${#OBJS[@]} objets, edition des liens"

# ---- edition des liens ------------------------------------------------------
#  Un module WASI "reactor" (pas de main() qui se termine) : la page appelle ses
#  exports. On exporte ce que src/web/WebEngine declare (xpg_*), on garde la pile
#  et le tas assez grands pour un gros projet, on autorise la croissance memoire.
#  Sans -O : avec, clang passe lui-meme wasm-opt apres l'edition des liens, qui
#  retire les noms des fonctions - et Asyncify ne trouverait plus xpg_fiber_run.
LINKFLAGS="--target=wasm32-wasi -mexec-model=reactor \
  -Wl,--export-dynamic -Wl,--allow-undefined \
  -Wl,-z,stack-size=8388608 -Wl,--initial-memory=67108864 -Wl,--max-memory=1073741824 \
  -Wl,--export=malloc -Wl,--export=free -Wl,--export=__stack_pointer"
$CLANGXX $LINKFLAGS "${OBJS[@]}" -o "$OUTDIR/ihm-web.wasm" 2> "$OBJDIR/link.err" || {
    echo "construire_web : edition des liens en echec :" >&2
    cat "$OBJDIR/link.err" >&2
    exit 1
}

# ---- Asyncify : un script detache qui fait WAIT suspend sa pile --------------
#  Le moteur passe la main a un script detache et attend qu'il la rende (un WAIT,
#  sa tranche finie). Sur un seul fil, Asyncify suspend et reprend la pile C++.
if command -v "$WASMOPT" >/dev/null 2>&1; then
    "$WASMOPT" --asyncify \
        --pass-arg=asyncify-imports@env.xpg_host_sleep \
        --pass-arg=asyncify-removelist@xpg_fiber_run \
        -O2 ${NOMS:+-g} "$OUTDIR/ihm-web.wasm" -o "$OUTDIR/ihm-web.wasm" 2> "$OBJDIR/opt.err" || {
        echo "construire_web : wasm-opt (asyncify) a echoue - le .wasm nu reste (pas de WAIT dans un script detache)" >&2
        cat "$OBJDIR/opt.err" >&2
    }
else
    echo "construire_web : wasm-opt absent - pas d'Asyncify (un WAIT detache bloque au lieu de suspendre)" >&2
fi

gzip -9 -f -k "$OUTDIR/ihm-web.wasm"

# ---- ce que la page embarque avec le moteur ---------------------------------
#  L'hote JS, l'amorce, le gabarit de la page et la police : l'export d'IHM Studio
#  (app/HmiWebPage) les lit ici (ou dans web/ a cote de l'exe), avec le moteur.
cp -f outils/web/web_host.js outils/web/amorce.js outils/web/page_ihm.html "$OUTDIR/"
cp -f installateur/polices/Barlow-Regular.ttf "$OUTDIR/ihm-web.ttf"
cp -f installateur/polices/OFL-Barlow.txt "$OUTDIR/ihm-web.ttf.LICENCE.txt"
git -C "$ROOT" rev-parse --short HEAD 2>/dev/null > "$OUTDIR/VERSION.txt" || date +%F > "$OUTDIR/VERSION.txt"
ls -l "$OUTDIR" | sed 's/^/construire_web : /'
echo "construire_web : fait"
