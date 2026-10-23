#!/bin/bash
# =============================================================================
#  outils/mingw/cross_mingw.sh - 1.11.3 : l'exe pour Windows, compile sous Linux
#  avec MinGW-w64 (comme l'exe des installateurs du lot 8 et de la 1.11.2). Il
#  faut : g++-mingw-w64-x86-64-posix, cmake, ninja. 1.12.0 : DEUX EXE, faits du
#  meme code - API-Studio.exe (l'automate) et IHM-Studio.exe (l'IHM).
#
#    outils/mingw/cross_mingw.sh            -> build-mingw/API-Studio.exe,
#                                              build-mingw/IHM-Studio.exe (+ SDL3.dll ;
#                                              1.12.30 : + pdfium.dll, s'il est dans third_party/pdfium)
#    JOBS=2 outils/mingw/cross_mingw.sh     -> moins de compilations a la fois
#
#  Le runtime C++ est lie dans l'exe (-static) : a cote, seulement SDL3.dll.
# =============================================================================
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
OUT=${OUT:-$ROOT/build-mingw}
JOBS=${JOBS:-4}
mkdir -p "$OUT"
# La bibliotheque d'import MinGW de SDL3.dll, depuis sa table d'exportation.
IMPLIB="$OUT/libSDL3.dll.a"
if [ ! -f "$IMPLIB" ]; then
    {
        echo "LIBRARY SDL3.dll"
        echo "EXPORTS"
        x86_64-w64-mingw32-objdump -p third_party/SDL3/lib/x64/SDL3.dll |
            awk '/\[Ordinal\/Name Pointer\] Table/{t=1; next} t { if ($0 ~ /^[ \t]*$/) exit; sub(/^[^]]*\][ \t]*/, ""); print }'
    } > "$OUT/SDL3.def"
    x86_64-w64-mingw32-dlltool -d "$OUT/SDL3.def" -l "$IMPLIB" -D SDL3.dll
fi
cmake -S . -B "$OUT" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/outils/mingw/toolchain-mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DXPG_BUILD_TESTS=OFF \
    -DSDL3_DIR="$ROOT/outils/mingw" \
    -DSDL3_MINGW_IMPLIB="$IMPLIB"
ninja -C "$OUT" -j "$JOBS" xpg_api xpg_ihm
cp -f third_party/SDL3/lib/x64/SDL3.dll "$OUT/SDL3.dll"
# 1.12.30 : PDFium, le moteur PDF de la visionneuse de documents (charge a la demande) : a cote des exe.
if [ -f third_party/pdfium/win-x64/pdfium.dll ]; then
    cp -f third_party/pdfium/win-x64/pdfium.dll "$OUT/pdfium.dll"
else
    echo "attention : third_party/pdfium/win-x64/pdfium.dll manque (python3 outils/pdfium/obtenir_pdfium.py) : la visionneuse dira que le lecteur PDF est absent"
fi
rm -f "$OUT/XpgAnalyzer.exe" "$OUT/xpg_analyzer.exe"     # la 1.11 : un seul exe
rm -f "$OUT/XpgAnalyzer-API.exe" "$OUT/XpgAnalyzer-IHM.exe"     # 1.12.0 a 1.12.19 : les anciens noms
for exe in API-Studio IHM-Studio; do
    x86_64-w64-mingw32-strip "$OUT/$exe.exe"
    echo "fait : $OUT/$exe.exe"
done
