#!/bin/bash
# =============================================================================
#  outils/mingw/cross_mingw.sh - 1.11.3 : XpgAnalyzer.exe pour Windows, compile
#  sous Linux avec MinGW-w64 (comme l'exe des installateurs du lot 8 et de la
#  1.11.2). Il faut : g++-mingw-w64-x86-64-posix, cmake, ninja.
#
#    outils/mingw/cross_mingw.sh            -> build-mingw/XpgAnalyzer.exe (+ SDL3.dll)
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
ninja -C "$OUT" -j "$JOBS" xpg_analyzer
cp -f "$OUT/xpg_analyzer.exe" "$OUT/XpgAnalyzer.exe"
cp -f third_party/SDL3/lib/x64/SDL3.dll "$OUT/SDL3.dll"
x86_64-w64-mingw32-strip "$OUT/XpgAnalyzer.exe"
echo "fait : $OUT/XpgAnalyzer.exe"
