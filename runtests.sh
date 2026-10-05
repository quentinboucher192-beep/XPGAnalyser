#!/bin/bash
cd "$(dirname "$0")"
X=tests/fixtures/MAST.XPG
H=tests/fixtures/CONFIG.XHW
L=resources/schneider_library.txt
pass=0; fail=0
run() {
    name=$1; shift
    if out=$("$@" 2>&1); then
        echo "  ok    $name"; pass=$((pass+1))
    else
        echo "  FAIL  $name"; echo "$out" | tail -20; fail=$((fail+1))
    fi
}
run core       build/core_test
run menu       build/menu_test
run syntax     build/syntax_test
run fold       build/fold_test
run layout     build/layout_test
run popup      build/popup_test
run settings   build/settings_test
run edit       build/edit_test
run editor     build/editor_test
run simulation build/simulation_test
run sharedlib  build/sharedlib_test
run library    build/library_test $L
run import     build/import_test $X
run viewmodel  build/viewmodel_test $X
run roundtrip  build/roundtrip_test $X
run project    build/project_test $X
run robustness build/parser_robustness_test $X
run delete     build/delete_test $X
run grafcet    build/grafcet_test $X
run font       build/font_test
run corner     build/corner_test
run savemark   build/savemark_test
run xls        build/xls_test /mnt/user-data/uploads/config_es.xlsm
run theme      build/theme_test
run table      build/table_test /tmp/excel_export.csv
run macro      build/macro_test
run xref       build/xref_test $X
run equipment  build/equipment_test
run hardware   build/hardware_test $X $H
echo "-- $pass reussis, $fail echoues"
[ $fail -eq 0 ]
