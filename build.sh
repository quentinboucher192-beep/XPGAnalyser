#!/bin/bash
# 1.11.17 (refonte des scripts, lot 0) : PLUS TENU A JOUR. Il ne construit ni
# src/hmi ni src/help, ni l'application, ni les essais IHM : la reference est
# CMake (cmake -S . -B build-linux -G Ninja ; ninja -C build-linux ; ctest), et
# sous Windows outils\build.bat (XpgAnalyzer.vcxproj, que verifie
# outils/verifier_vcxproj.py). Garde pour l'histoire des premiers lots.
#
# Build sans CMake (le conteneur n'a pas cmake). Reproduit les cibles du
# CMakeLists : xpg_core, xpg_import, xpg_ui, puis les tests.
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p $OUT/obj
FLAGS="-std=c++20 -Wall -Wextra -Wshadow -Wnon-virtual-dtor -O1 -g -Isrc"
JOBS=${JOBS:-8}

# Piège nº 5, rencontré ici pour de vrai: ce script ne comparait que la date du
# .cpp. Ajouter un membre a TableView a laissé deux unités avec des sizeof
# différents pour la même classe, et layout_test est mort sur une corruption de
# tas dans malloc - pas une erreur de compilation, pas une pile lisible.
# La parade est celle de tout Makefile sérieux: -MMD génère la liste des
# en-têtes inclus, et on recompile dès que l'un d'eux bouge.
FLAGS="$FLAGS -MMD -MP"

newer_than_obj() {   # $1 source, $2 objet
    [ ! -f "$2" ] && return 0
    [ "$1" -nt "$2" ] && return 0
    local dep="${2%.o}.d"
    [ ! -f "$dep" ] && return 0
    local h
    for h in $(sed -e 's/^.*://' -e 's/\\//g' "$dep"); do
        [ -f "$h" ] && [ "$h" -nt "$2" ] && return 0
    done
    return 1
}

compile() {
    local src="$1"
    local obj="$OUT/obj/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        g++ $FLAGS -c "$src" -o "$obj" || return 1
    fi
    echo "$obj"
}

CORE=$(find src/core -name '*.cpp' ! -name 'compile_check.cpp')
IMPORT=$(find src/domain src/import src/export src/project src/sim -name '*.cpp')
UI=$(find src/ui src/menu -name '*.cpp')
# src/platform n'est pas compile en entier : SdlRenderer et SdlEventPump ont
# besoin de SDL3, absent des machines de test. FontAtlas, lui, ne rasterise que
# dans de la memoire - c'est precisement pourquoi il a ete ecrit sans SDL.
PLATFORM=src/platform/FontAtlas.cpp
XLS=src/xls/Workbook.cpp

# miniz est compile A PART, et avec MINIZ_NO_DEFLATE_APIS.
#
# Sans ce drapeau il ne compile pas en C++ : s_tdefl_num_probes y est defini
# deux fois, ce que le C tolere - definitions tentatives - et que le C++ refuse.
# Le drapeau retire la partie COMPRESSION, dont un lecteur de .xlsx n'a aucun
# besoin : il ne sait plus qu'ouvrir, ce qui est exactement ce qu'on veut.
MINIZ_O=$OUT/obj/third_party_miniz.o
mkdir -p $OUT/obj
if [ ! -f $MINIZ_O ] || [ third_party/miniz.c -nt $MINIZ_O ]; then
    echo "  cc   third_party/miniz.c"
    g++ -x c++ -std=c++20 -O2 -DMINIZ_NO_DEFLATE_APIS -c third_party/miniz.c -o $MINIZ_O
fi
APP=$(find src/app -name '*.cpp')

ALL="$CORE $IMPORT $UI $PLATFORM $XLS $APP $(ls tests/*_test.cpp)"

# compilation parallèle
pids=""
for s in $ALL; do
    obj="$OUT/obj/$(echo "$s" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    if newer_than_obj "$s" "$obj"; then
        g++ $FLAGS -c "$s" -o "$obj" &
        pids="$pids $!"
        while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
    fi
done
wait

objs() { for s in $@; do echo -n "$OUT/obj/$(echo "$s" | tr '/' '_' | sed 's/\.cpp$/.o/') "; done; }

CORE_O=$(objs $CORE)
IMPORT_O=$(objs $IMPORT)
UI_O=$(objs $UI)
VM_O=$OUT/obj/src_app_ViewModels.o
SET_O=$OUT/obj/src_app_Settings.o

link() { # name libs...
    local t=$1; shift
    g++ $FLAGS -o $OUT/${t}_test $OUT/obj/tests_${t}_test.o "$@" -lpthread
}

link menu               $UI_O $CORE_O
link syntax             $UI_O $CORE_O
link fold               $UI_O $CORE_O
link layout             $UI_O $CORE_O
link popup              $UI_O $CORE_O
link import             $IMPORT_O $CORE_O
link hardware           $IMPORT_O $CORE_O
link roundtrip          $IMPORT_O $CORE_O
link project            $IMPORT_O $CORE_O
link edit               $IMPORT_O $CORE_O
link library            $IMPORT_O $CORE_O
link simulation         $IMPORT_O $CORE_O $OUT/obj/src_app_SimulationHost.o
link sharedlib          $IMPORT_O $CORE_O
link parser_robustness  $IMPORT_O $CORE_O
link delete             $IMPORT_O $CORE_O
FONT_O=$OUT/obj/src_platform_FontAtlas.o
link font               $FONT_O
link corner
link savemark
link xls                $OUT/obj/src_xls_Workbook.o $OUT/obj/src_project_Table.o $OUT/obj/src_import_XmlReader.o $MINIZ_O
link theme              $OUT/obj/src_ui_Theme.o
link table              $IMPORT_O $CORE_O
link macro              $IMPORT_O $CORE_O
link xref               $IMPORT_O $CORE_O
link equipment          $IMPORT_O $CORE_O
link grafcet            $IMPORT_O $UI_O $OUT/obj/src_app_GrafcetView.o $OUT/obj/src_app_GrafcetPanels.o $CORE_O
link editor             $IMPORT_O $UI_O $CORE_O
link viewmodel          $IMPORT_O $UI_O $VM_O $CORE_O
link settings           $SET_O $CORE_O
g++ $FLAGS -o $OUT/core_test src/core/compile_check.cpp $CORE_O -lpthread
echo "build ok"
