cmake_minimum_required(VERSION 3.24)
project(xpg_analyzer VERSION 1.11.21 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# C++23 is requested, C++20 is the floor. The one feature that needs C++23 is
# std::expected; core/Result.hpp falls back to an API-compatible implementation
# when the toolchain does not provide it, so the project builds either way.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(XPG_WITH_SDL           "Build the graphical front end"   ON)
option(XPG_BUILD_TESTS        "Build the test executables"      ON)
option(XPG_WARNINGS_AS_ERRORS "Treat warnings as errors"        OFF)
# Lot 15 : l'espion Modbus sans la capture reseau (Npcap / libpcap / AF_PACKET) :
# la trace et le relais restent. Npcap n'est jamais lie : il est charge s'il est
# installe ; cette option retire aussi le code qui le charge.
option(XPG_SANS_NPCAP         "Build the Modbus spy without network capture" OFF)
if(XPG_SANS_NPCAP)
    add_compile_definitions(XPG_SANS_NPCAP)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    if(XPG_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor)
    if(XPG_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
#  SOURCE LISTS ARE GLOBBED, DELIBERATELY.
#
#  The usual advice is to list sources by hand, because a plain GLOB is only
#  evaluated at configure time and silently misses files added afterwards.
#  CONFIGURE_DEPENDS removes exactly that failure: CMake re-checks the glob on
#  every build and re-runs configure when the set of files changes.
#
#  The trade is worth taking here because of how this project is delivered. It
#  arrives as an archive unpacked over an existing tree, and unzip preserves the
#  archive's timestamps. A CMakeLists.txt that lands with a date older than the
#  CMake cache is not re-read, the hand-written list stays as it was, and the
#  link fails on the symbols of a file that was never compiled. That has now
#  happened twice, which is twice more than a build system should get to fail
#  for a reason unrelated to the code.
#
#  The cost is a directory scan per build. On 32 files it is unmeasurable.
#
#  C'EST AUSSI POURQUOI IL N'Y A PLUS AUCUN target_sources() NOMMANT UN FICHIER
#  A LA MAIN ici : les cinq fichiers de l'aide, les trois du lecteur .xlsm et
#  les nouveaux de src/help/ sont ramasses par le glob de leur dossier. Un
#  fichier ajoute a la main EN PLUS du glob se compile deux fois et se lie deux
#  fois - "multiple definition of" sur un fichier qu'on vient d'ajouter, ce qui
#  ressemble a tout sauf a sa cause.
# ---------------------------------------------------------------------------
function(xpg_sources out_var)
    set(patterns "")
    foreach(dir IN LISTS ARGN)
        list(APPEND patterns "${CMAKE_CURRENT_SOURCE_DIR}/src/${dir}/*.cpp")
    endforeach()
    file(GLOB_RECURSE found CONFIGURE_DEPENDS ${patterns})
    # compile_check.cpp is a standalone dialect probe, not part of any library.
    list(FILTER found EXCLUDE REGEX "compile_check\\.cpp$")
    # The SDL backend is added separately, only when SDL is wanted.
    list(FILTER found EXCLUDE REGEX "platform/Sdl.*\\.cpp$")
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

# --- core: no dependencies at all, not even on the platform layer ------------
xpg_sources(CORE_SOURCES core)
add_library(xpg_core STATIC ${CORE_SOURCES})
target_include_directories(xpg_core PUBLIC src)
target_link_libraries(xpg_core PUBLIC Threads::Threads)

# --- xls: lire un classeur, et rien d'autre ----------------------------------
#
#  SON SEUL LIEN EST xpg_core, ET C'EST LE POINT.
#
#  La version precedente le faisait dependre de xpg_import, alors que
#  project/Macro.cpp appelle xls::MacroXls : les deux bibliotheques se
#  reclamaient l'une l'autre. Un cycle entre deux bibliotheques statiques ne se
#  plante pas a la configuration, il se plante au lien, une fois sur deux selon
#  l'ordre que l'editeur de liens a choisi - c'est-a-dire chez celui qui
#  compile, pas chez celui qui a ecrit la ligne.
#
#  Le sens reel est simple : xls ne connait ni projet ni modele, il rend des
#  chaines et des nombres. C'est xpg_import qui a besoin de lui.
#
#  miniz n'est ajoute que s'il est la. XlsmSource.cpp porte sa propre
#  decompression (une quarantaine de lignes de Huffman), donc l'arbre se
#  configure aussi sans third_party/.
xpg_sources(XLS_SOURCES xls)
add_library(xpg_xls STATIC ${XLS_SOURCES})
target_link_libraries(xpg_xls PUBLIC xpg_core)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz.c")
    target_sources(xpg_xls PRIVATE third_party/miniz.c)
    # 1.8.0 : la compression aussi (l'export lisible : le classeur et le PDF
    # compresses). XPG_HAVE_MINIZ le dit a qui s'en sert (export/DocKit.cpp).
    target_compile_definitions(xpg_xls PUBLIC XPG_HAVE_MINIZ=1)
    target_include_directories(xpg_xls PUBLIC third_party)
endif()

# --- domain, import, export, project: the model and everything that reads or
#     writes it. This is the library CI exercises. -------------------------
xpg_sources(IMPORT_SOURCES domain import export project sim)
add_library(xpg_import STATIC ${IMPORT_SOURCES})
target_link_libraries(xpg_import PUBLIC xpg_core xpg_xls)

# --- ui + menu: depend on core; the platform seam is gfx::IRenderer ----------
xpg_sources(UI_SOURCES ui menu)
add_library(xpg_ui STATIC ${UI_SOURCES})
# 1.11 (T2) : ui/HelpDocument.cpp lit project:: (formats(), sectionFor) : xpg_import
# (domain import export project sim, qui n'inclut rien de ui/). Sans elle, un essai
# qui tire ui::buildHelp() par xpg_help ne se lie pas (l'ordre des .a).
target_link_libraries(xpg_ui PUBLIC xpg_core xpg_import)
# ---- Lot API 8 : l'explorateur de fichiers - SHGetKnownFolderPath (le Bureau,
# les Documents, les Telechargements) : ui/FileExplorerModel.cpp ----
if(WIN32)
    target_link_libraries(xpg_ui PUBLIC shell32 ole32)
endif()

# --- help: chercher, relier, diagnostiquer, essayer, exporter ----------------
#
#  Une bibliotheque a part, et pas un morceau de app/, parce qu'elle ne sait
#  pas qu'il existe un ecran : elle rend des donnees - un classement, une
#  cible, un article, du HTML - et c'est LibraryHelpPage qui les affiche. C'est
#  ce qui permet a help_test et helpsession_test de tourner sans serveur X,
#  donc en CI, donc a chaque commit.
#
#  Elle depend de xpg_ui pour la seule structure ui::HelpArticle : les pages de
#  diagnostic se mettent en page avec le meme widget que l'aide d'un bloc, et
#  il n'y a pas deux mises en page a tenir.
xpg_sources(HELP_SOURCES help)
add_library(xpg_help STATIC ${HELP_SOURCES})
target_link_libraries(xpg_help PUBLIC xpg_import xpg_ui)

# --- hmi: les vues IHM - modele, fichiers, commandes, expressions, controles ---
#
#  Sans ecran, comme xpg_import dont il reprend l'interpreteur ST pour evaluer
#  les expressions des vues : hmi_test tourne donc en CI. Ce qui dessine et ce
#  qui se clique (l'editeur de vues) est dans src/app/hmi, avec l'application.
xpg_sources(HMI_SOURCES hmi)
add_library(xpg_hmi STATIC ${HMI_SOURCES})
target_link_libraries(xpg_hmi PUBLIC xpg_import)
# Lot 14 : sous Windows, les sockets (Modbus TCP, le serveur de demonstration)
# et le registre (le poste d'exploitation qui demarre avec la session). Ceux
# qui lient xpg_hmi (l'application, les essais) les recoivent aussi.
if(WIN32)
    target_link_libraries(xpg_hmi PUBLIC ws2_32 advapi32)
    # Lot 15 : les ports du PC et le ping (iphlpapi), changer l'adresse d'un port
    # avec l'autorisation administrateur (shell32).
    target_link_libraries(xpg_hmi PUBLIC iphlpapi shell32)
else()
    # Lot 15 : libpcap, chargee a la demande (dlopen) pour la capture.
    target_link_libraries(xpg_hmi PUBLIC ${CMAKE_DL_LIBS})
endif()

# --- application -------------------------------------------------------------
xpg_sources(APP_SOURCES app)
add_executable(xpg_analyzer ${APP_SOURCES} src/main.cpp)
target_link_libraries(xpg_analyzer PRIVATE xpg_ui xpg_import xpg_help xpg_hmi)

# --- lot 12 : le logo de l'executable Windows ---------------------------------
#
#  resources/logo/xpg_analyzer.ico (fait depuis le SVG, 16 a 256 pixels) entre
#  dans xpg_analyzer.exe par une ressource : l'Explorateur, la barre des taches
#  et Alt+Tab le montrent. Le .rc est configure pour porter le chemin ABSOLU de
#  l'icone : rc.exe et windres cherchent un nom relatif dans leur dossier de
#  travail, qui est celui de la construction. La fenetre, elle, prend le logo
#  partout (app/Brand.cpp, SDL_SetWindowIcon).
if(WIN32)
    enable_language(RC)
    set(XPG_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/logo/xpg_analyzer.ico")
    set(XPG_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/xpg_analyzer.manifest")   # 1.11.3 : MinGW
    configure_file(resources/windows/xpg_analyzer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/xpg_analyzer.rc @ONLY)
    target_sources(xpg_analyzer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/xpg_analyzer.rc)
endif()

if(XPG_WITH_SDL)
    find_package(SDL3 CONFIG REQUIRED)
    file(GLOB SDL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Sdl*.cpp")
    # SdlRenderer dessine le texte avec FontAtlas : sans lui, l'executable ne
    # se lie pas ("undefined reference to gfx::FontAtlas").
    target_sources(xpg_analyzer PRIVATE ${SDL_SOURCES} src/platform/FontAtlas.cpp)
    target_link_libraries(xpg_analyzer PRIVATE SDL3::SDL3)
    target_compile_definitions(xpg_analyzer PRIVATE XPG_WITH_SDL=1)
endif()

# ---------------------------------------------------------------------------
#  tests - all headless, so CI needs no display server
# ---------------------------------------------------------------------------
if(XPG_BUILD_TESTS)
    enable_testing()

    set(XPG_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/MAST.XPG" CACHE FILEPATH "Le projet d'essai MAST.XPG")
    set(XHW_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/CONFIG.XHW" CACHE FILEPATH "La configuration materielle d'essai CONFIG.XHW")
    set(XPG_LIBS    ${CMAKE_CURRENT_SOURCE_DIR}/libs)
    set(XLSM_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

    # 1.11 (I111, decision 16 du 03/10) : DES SOURCES D'ESSAI CITEES ICI NE
    # SONT PAS DANS LE DEPOT (core_test, menu, import, l'aide, le classeur...).
    # Avec les reglages par defaut (XPG_BUILD_TESTS=ON), cmake s'arretait sur
    # "Cannot find source file" : les sources livrees ne se configuraient pas.
    # Maintenant un essai dont une source manque est saute, avec un message :
    # xpg_add_test_executable ne cree pas sa cible, et les xpg_target_* et
    # xpg_add_test qui la nomment ne font rien. Un essai dont un fichier
    # d'entree manque (un chemin absolu en argument : MAST.XPG, CONFIG.XHW...)
    # n'est pas enregistre, avec un message, comme ceux de libs/ plus bas : un
    # rouge qui ne dit rien sur le code. XPG_FIXTURE et XHW_FIXTURE designent
    # les fixtures ailleurs : cmake -DXPG_FIXTURE=D:/essais/MAST.XPG
    # Une source qui revient dans tests/ est reprise toute seule.
    function(xpg_add_test_executable name)
        set(absentes "")
        foreach(src IN LISTS ARGN)
            cmake_path(ABSOLUTE_PATH src BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE chemin)
            if(NOT EXISTS "${chemin}")
                list(APPEND absentes "${src}")
            endif()
        endforeach()
        if(absentes)
            list(JOIN absentes ", " liste)
            message(STATUS "essai saute : ${name} (source absente : ${liste})")
            set_property(GLOBAL APPEND PROPERTY XPG_ESSAIS_SAUTES ${name})
            return()
        endif()
        add_executable(${name} ${ARGN})
    endfunction()
    function(xpg_target_link_libraries name)
        if(TARGET ${name})
            target_link_libraries(${name} ${ARGN})
        endif()
    endfunction()
    function(xpg_target_sources name)
        if(TARGET ${name})
            target_sources(${name} ${ARGN})
        endif()
    endfunction()
    function(xpg_target_link_options name)
        if(TARGET ${name})
            target_link_options(${name} ${ARGN})
        endif()
    endfunction()
    function(xpg_add_test)
        cmake_parse_arguments(T "" "NAME" "COMMAND" ${ARGN})
        list(GET T_COMMAND 0 exe)
        if(NOT TARGET ${exe})
            return()   # deja dit par xpg_add_test_executable
        endif()
        set(absents "")
        foreach(arg IN LISTS T_COMMAND)
            if(IS_ABSOLUTE "${arg}" AND NOT EXISTS "${arg}")
                list(APPEND absents "${arg}")
            endif()
        endforeach()
        if(absents)
            list(JOIN absents ", " liste)
            message(STATUS "essai non enregistre : ${T_NAME} (fichier d'entree absent : ${liste})")
            set_property(GLOBAL APPEND PROPERTY XPG_ESSAIS_NON_ENREGISTRES ${T_NAME})
            return()
        endif()
        add_test(NAME ${T_NAME} COMMAND ${T_COMMAND})
    endfunction()

    # name : libraries : arguments
    #   core       Result under both dialects, Signal, EventBus, CommandStack
    #   menu       navigation, modality, dialog results, deferred mutation
    #   import     end-to-end on a real export
    #   hardware   the .XHW, and where each figure came from
    #   roundtrip  import, write, re-import, compare - the writer is not trusted
    #   project    folder format, states, lock, export
    #   edit       creating entities and taking every one of them back
    #   robustness malformed input must diagnose, never overrun
    #   syntax     the tokeniser on ST and C++
    #   fold       folding derived from indentation
    #   layout     no overlapping widgets, no text outside its widget
    #   settings   workspace persistence, ordering and locale
    xpg_add_test_executable(core_test src/core/compile_check.cpp)
    xpg_target_link_libraries(core_test PRIVATE xpg_core)

    #  delete     removing from the middle, and putting it back byte for byte
    #  popup      the context menu: geometry, dismissal, event routing
    #  sharedlib  publish, import, notify - it existed and was never registered
    foreach(t menu import hardware roundtrip project edit editor library simulation
              sharedlib delete grafcet table macro xref equipment font theme corner parser_robustness syntax fold layout popup)
        xpg_add_test_executable(${t}_test tests/${t}_test.cpp)
    endforeach()

    xpg_target_link_libraries(menu_test              PRIVATE xpg_ui)
    xpg_target_link_libraries(import_test            PRIVATE xpg_import)
    xpg_target_link_libraries(hardware_test          PRIVATE xpg_import)
    xpg_target_link_libraries(roundtrip_test         PRIVATE xpg_import)
    xpg_target_link_libraries(project_test           PRIVATE xpg_import)
    xpg_target_link_libraries(edit_test              PRIVATE xpg_import)
    xpg_target_link_libraries(editor_test            PRIVATE xpg_import xpg_ui)
    xpg_target_link_libraries(library_test           PRIVATE xpg_import)
    xpg_target_link_libraries(sharedlib_test         PRIVATE xpg_import)
    xpg_target_link_libraries(delete_test            PRIVATE xpg_import)
    xpg_target_link_libraries(grafcet_test           PRIVATE xpg_import xpg_ui)
    xpg_target_link_libraries(table_test             PRIVATE xpg_import)
    xpg_target_link_libraries(macro_test             PRIVATE xpg_import)
    xpg_target_link_libraries(xref_test              PRIVATE xpg_import)
    xpg_target_link_libraries(equipment_test         PRIVATE xpg_import)
    # font_test ne depend que de FontAtlas, qui n'utilise pas SDL : c'est pour
    # cela qu'il a ete ecrit ainsi, et c'est ce qui le rend testable partout.
    xpg_target_sources(font_test PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/FontAtlas.cpp)
    xpg_target_link_libraries(theme_test             PRIVATE xpg_ui)
    xpg_target_link_libraries(popup_test             PRIVATE xpg_ui)
    xpg_target_link_libraries(simulation_test        PRIVATE xpg_import)
    xpg_target_link_libraries(parser_robustness_test PRIVATE xpg_import)
    xpg_target_link_libraries(syntax_test            PRIVATE xpg_ui)
    xpg_target_link_libraries(fold_test              PRIVATE xpg_ui)
    xpg_target_link_libraries(layout_test            PRIVATE xpg_ui)

    # Lot 16 : ViewModels.cpp montre les types et les dossiers des variables IHM
    # (hmi::types, dans xpg_hmi) : les trois tests qui le compilent lient xpg_hmi.
    xpg_add_test_executable(viewmodel_test tests/viewmodel_test.cpp src/app/ViewModels.cpp)
    xpg_target_link_libraries(viewmodel_test PRIVATE xpg_hmi xpg_import xpg_ui)

    xpg_add_test_executable(settings_test tests/settings_test.cpp src/app/Settings.cpp)
    xpg_target_link_libraries(settings_test PRIVATE xpg_core)

    # 1.8.0 : les dossiers de l'application (installation.ini, XPGAnalyser.ini) -
    # lire, ecrire, les jetons, la resolution, changer (copie sans ecraser).
    xpg_add_test_executable(dossiers_test tests/dossiers_test.cpp src/app/Dossiers.cpp)
    xpg_target_link_libraries(dossiers_test PRIVATE xpg_core)

    # -- l'aide : le fichier, l'article, l'editeur -----------------------------
    #
    #  libcatalog / libhelp / macrohelp  ne touchent qu'au texte d'un fichier de
    #  bibliotheque. helparticle ne touche qu'a la mise en page, avec une mesure
    #  de police fausse mais connue : huit pixels par caractere. Aucun n'a besoin
    #  d'ecran, et c'est ce qui fait qu'ils tournent en CI.
    xpg_add_test_executable(libcatalog_test      tests/libcatalog_test.cpp)
    xpg_add_test_executable(libhelp_test         tests/libhelp_test.cpp)
    xpg_add_test_executable(macrohelp_parse_test tests/macrohelp_parse_test.cpp)
    xpg_add_test_executable(helparticle_test     tests/helparticle_test.cpp)
    xpg_target_link_libraries(libcatalog_test      PRIVATE xpg_import)
    xpg_target_link_libraries(libhelp_test         PRIVATE xpg_import)
    xpg_target_link_libraries(macrohelp_parse_test PRIVATE xpg_import)
    xpg_target_link_libraries(helparticle_test     PRIVATE xpg_ui)

    # L'ecran d'aide lui-meme. Les sources de app/ ne sont dans aucune
    # bibliotheque - elles ne servent qu'a l'executable - donc un test qui en a
    # besoin les nomme, comme viewmodel_test le fait deja.
    # Lot macros 1 : la page se sert aussi de HelpChrome (les boutons, la barre
    # des onglets), du banc d'essai (HelpBench) et du module help/ - sans eux,
    # le test ne se liait plus depuis longtemps.
    xpg_add_test_executable(libraryhelppage_test tests/libraryhelppage_test.cpp
                   src/app/LibraryHelpModels.cpp
                   src/app/screens/LibraryHelpScreen.cpp
                   src/app/screens/HelpChrome.cpp
                   src/app/screens/HelpBench.cpp)
    xpg_target_link_libraries(libraryhelppage_test PRIVATE xpg_help xpg_import xpg_ui)

    # -- le module help/ : recherche, renvois, F1, codes, Essayer, export ------
    xpg_add_test_executable(help_test        tests/help_test.cpp)
    xpg_add_test_executable(helpsession_test tests/helpsession_test.cpp)
    xpg_target_link_libraries(help_test        PRIVATE xpg_help)
    xpg_target_link_libraries(helpsession_test PRIVATE xpg_help)
    # 1.11 (T1) : le moteur des tutoriels, sans ecran - le format .tuto, la
    # compilation par variante, l'automate du lecteur sur une scene factice.
    # Ne depend que de la bibliotheque standard : se compile aussi seul,
    #   g++ -std=c++20 -Isrc src/help/Tutorial*.cpp tests/tutorial_test.cpp
    xpg_add_test_executable(tutorial_test tests/tutorial_test.cpp)
    xpg_target_link_libraries(tutorial_test PRIVATE xpg_help)
    xpg_add_test(NAME tutorial COMMAND tutorial_test ${CMAKE_CURRENT_SOURCE_DIR}/tools/tutoriels)

    # -- le classeur -----------------------------------------------------------
    xpg_add_test_executable(xlsm_test        tests/xlsm_test.cpp)
    xpg_add_test_executable(macroxls_test    tests/macroxls_test.cpp)
    xpg_add_test_executable(macronatives_test tests/macronatives_test.cpp)
    xpg_target_link_libraries(xlsm_test        PRIVATE xpg_xls)
    xpg_target_link_libraries(macroxls_test    PRIVATE xpg_xls)
    # macronatives passe par project/Macro.cpp : c'est l'hote de macro qu'il
    # verifie, pas le lecteur.
    xpg_target_link_libraries(macronatives_test PRIVATE xpg_import)

    # -- lot macros 1 : le formulaire (MacroSpec), les dossiers, la memoire, la
    #    session, et le Ctrl+Z d'une macro qui importe (LibImport) ---------------
    xpg_add_test_executable(macros2_test tests/macros2_test.cpp)
    xpg_target_link_libraries(macros2_test PRIVATE xpg_import xpg_xls)

    # -- lot API 2 : la barre du haut (chaque ancien bouton y a sa place), les
    #    pages gardees des onglets, le tableau de bord de l'API (ApiChecks) et
    #    les pastilles des macros (ce qu'elles touchent) -----------------------
    # ---- Lot API 8 : bandeau haut - TopBar.cpp s'appuie sur TopBarLot8.cpp ----
    xpg_add_test_executable(api2_test tests/api2_test.cpp src/app/TopBar.cpp src/app/TopBarLot8.cpp)
    xpg_target_link_libraries(api2_test PRIVATE xpg_import xpg_ui)
    # ---- Lot API 8 : bandeau haut (trois zones, la liste d'Annuler, la cloche, les taches de fond) ----
    xpg_add_test_executable(topbar8_test tests/topbar8_test.cpp src/app/TopBar.cpp src/app/TopBarLot8.cpp src/app/BackgroundTasks.cpp)
    xpg_target_link_libraries(topbar8_test PRIVATE xpg_ui)
    # ---- fin Lot API 8 : bandeau haut ----

    # -- lots API 3 et 4 : les tables d'animation (lignes API et IHM, fichier,
    #    export, commandes), l'ordre d'execution en entrees (les unites en bloc),
    #    les taches, les voies et adresses, le plan memoire, le materiel --------
    xpg_add_test_executable(api34_test tests/api34_test.cpp)
    xpg_target_link_libraries(api34_test PRIVATE xpg_import)

    # -- lot API 5 : qui se sert de quoi (types derives, DFB, variables,
    #    sous-routines), renommer et ce qui suit, l'en-tete du projet en dossier
    xpg_add_test_executable(typeusage_test tests/typeusage_test.cpp)
    xpg_target_link_libraries(typeusage_test PRIVATE xpg_import)
    # Lot API 6 : ecrire les lignes "#!" ; l'etat et les versions ; les themes ;
    # l'icone du projet.
    xpg_add_test_executable(macrospec_write_test tests/macrospec_write_test.cpp)
    xpg_target_link_libraries(macrospec_write_test PRIVATE xpg_import)
    xpg_add_test_executable(versionstate_test tests/versionstate_test.cpp)
    xpg_target_link_libraries(versionstate_test PRIVATE xpg_hmi)
    # (theme_test existe deja - les trois themes d'avant ; celui-ci verifie
    # les neuf du catalogue, leurs contrastes et leurs noms.)
    xpg_add_test_executable(themecatalog_test tests/themecatalog_test.cpp)
    xpg_target_link_libraries(themecatalog_test PRIVATE xpg_ui)
    xpg_add_test_executable(projecticon_test tests/projecticon_test.cpp)
    xpg_target_link_libraries(projecticon_test PRIVATE xpg_import)
    # 1.8.0 : l'export lisible, le comparateur de sections, les icones au choix.
    xpg_add_test_executable(lisible_test tests/lisible_test.cpp)
    xpg_target_link_libraries(lisible_test PRIVATE xpg_import)
    # Lot API 7 : deplier sans limite (paquets, plusieurs dimensions).
    xpg_add_test_executable(membertree_test tests/membertree_test.cpp)
    xpg_target_link_libraries(membertree_test PRIVATE xpg_import)
    # Lot 7 : renommer en voyant tout ce qui suit - le plan (API et IHM), les noms
    # refuses, UNE commande faite puis defaite. RenameDialog.cpp : la moitie IHM
    # du plan (xpg_import ne lie pas l'IHM) ; rien n'y est affiche.
    xpg_add_test_executable(renameplan_test tests/renameplan_test.cpp src/app/RenameDialog.cpp)
    xpg_target_link_libraries(renameplan_test PRIVATE xpg_hmi xpg_import xpg_ui)
    # Lot 7 : un .XPG dans le projet ouvert (nouveau MAST) - le plan (garde,
    # change, supprime, nouveau, liens IHM perdus), la fusion, Ctrl+Z / Ctrl+Y.
    # 1.11 (R111) : l'avis des conditions d'activation manquantes (la cloche, les reglages)
    xpg_add_test_executable(mastimport_test tests/mastimport_test.cpp src/app/ConditionsNotice.cpp
                   src/app/Settings.cpp src/app/BackgroundTasks.cpp)
    xpg_target_link_libraries(mastimport_test PRIVATE xpg_import)
    # Lot API 7 : les statistiques du projet ouvert (API > Statistiques).
    xpg_add_test_executable(projectstats_test tests/projectstats_test.cpp)
    xpg_target_link_libraries(projectstats_test PRIVATE xpg_import)
    # ---- Lot API 8 : Simulation > Debogage (les phrases, la pile, les temps, la marge de l'editeur) ----
    xpg_add_test_executable(simdebugui_test tests/simdebugui_test.cpp src/app/SimDebugText.cpp)
    xpg_target_link_libraries(simdebugui_test PRIVATE xpg_import xpg_ui)
    # ---- Lot API 8 : le moteur de simulation (modification en ligne, points d'arret,
    # pas a pas par section, qui a ecrit, temps par section) - le runtime et l'hote ----
    xpg_add_test_executable(simdebug_test tests/simdebug_test.cpp src/app/SimulationHost.cpp src/app/SimulationHostDebug.cpp)
    xpg_target_link_libraries(simdebug_test PRIVATE xpg_import)
    # ---- Lot API 8 : les dialogues dans la fenetre detachee (la fenetre de chaque
    # dialogue, la route des evenements, chacun a sa taille, le retour dans la
    # principale quand sa fenetre se ferme) - la pile seule, sans ecran ----
    xpg_add_test_executable(dialogwindow_test tests/dialogwindow_test.cpp)
    xpg_target_link_libraries(dialogwindow_test PRIVATE xpg_ui)
    # ---- Lot API 8 : les exports qui demandent ou (la fin) - le moteur de l'IHM
    # (la question pendant un geste, jamais seul ; la reponse), l'option relue,
    # ExportTarget.hpp, le dialogue du poste (StationExportDialog, un vrai
    # MenuManager) ----
    xpg_add_test_executable(exportask_test tests/exportask_test.cpp src/app/screens/StationExportDialog.cpp)
    xpg_target_link_libraries(exportask_test PRIVATE xpg_hmi xpg_ui)
    # ---- Lot API 8 : themes (le fichier .xpgtheme, les contrastes, deriver de
    # l'accent, les themes de l'utilisateur dans un dossier temporaire) ----
    xpg_add_test_executable(themefile_test tests/themefile_test.cpp)
    xpg_target_link_libraries(themefile_test PRIVATE xpg_ui)
    # ---- Lot API 8 : Centre de simulation (le bandeau, les cartes, la chaine,
    # l'attention, la frise ; le journal : ses filtres, ses cles "aller a") - sans ecran ----
    xpg_add_test_executable(simstatus_test tests/simstatus_test.cpp src/app/SimStatus.cpp src/app/SimJournal.cpp)
    xpg_target_link_libraries(simstatus_test PRIVATE xpg_core)
    # ---- Lot API 8 : glisser n'importe quel fichier (le plan : le genre, la case par
    # defaut, un classeur lu par son contenu, les macros de libs/ qui le lisent) - sans ecran ----
    xpg_add_test_executable(dropfiles_test tests/dropfiles_test.cpp src/app/DropFilesPlan.cpp)
    xpg_target_link_libraries(dropfiles_test PRIVATE xpg_hmi xpg_import)
    # ---- Lot API 8 : renommer dans l'IHM par tous les chemins (les references suivent) - sans ecran ----
    xpg_add_test_executable(rename_hmi_paths_test tests/rename_hmi_paths_test.cpp src/app/RenameDialog.cpp)
    xpg_target_link_libraries(rename_hmi_paths_test PRIVATE xpg_hmi xpg_import xpg_ui)
    # ---- Lot API 8 : renommer - RenameDialog.cpp appelle l'aide (F1 : lot8::helpAnchorFor,
    # TutorialsLot8.cpp) : les essais qui le compilent la lient aussi ----
    xpg_target_sources(renameplan_test PRIVATE src/app/TutorialsLot8.cpp)
    xpg_target_sources(rename_hmi_paths_test PRIVATE src/app/TutorialsLot8.cpp)
    # ---- Lot API 8 : les filtres retenus d'une seance a l'autre (le crochet, une colonne
    # renommee, Tout effacer, deux projets) et la recherche des volets qui n'en avaient pas - sans ecran ----
    xpg_add_test_executable(filtermemory_test tests/filtermemory_test.cpp src/app/ApiListKit.cpp)
    xpg_target_link_libraries(filtermemory_test PRIVATE xpg_import xpg_ui)
    # ---- Lot API 8 : finitions (Ctrl+F : le champ de recherche montre de l'onglet ; Echap l'efface) ----
    xpg_add_test_executable(searchfocus_test tests/searchfocus_test.cpp src/app/SearchFocus.cpp src/app/ApiListKit.cpp)
    xpg_target_link_libraries(searchfocus_test PRIVATE xpg_import xpg_ui)
    # ---- fin Lot API 8 : finitions ----
    # ---- Lot API 8 : le bandeau bas (le journal des messages, la rangee des pastilles) - sans ecran ----
    xpg_add_test_executable(statusstrip_test tests/statusstrip_test.cpp src/app/StatusStrip.cpp)
    xpg_target_link_libraries(statusstrip_test PRIVATE xpg_ui)
    # ---- fin Lot API 8 : le bandeau bas ----

    # -- l'ordre d'execution ---------------------------------------------------
    xpg_add_test_executable(executionorder_test tests/executionorder_test.cpp)
    xpg_add_test_executable(simorder_test       tests/simorder_test.cpp)
    xpg_target_link_libraries(executionorder_test PRIVATE xpg_import)
    xpg_target_link_libraries(simorder_test       PRIVATE xpg_import)

    xpg_add_test_executable(treednd_test tests/treednd_test.cpp src/app/ViewModels.cpp)
    xpg_target_link_libraries(treednd_test PRIVATE xpg_hmi xpg_import xpg_ui)

    # Le bout a bout : le menu contextuel et le glisser-deposer appliquent-ils
    # VRAIMENT une commande ? C'est ce test qui a trouve que le gestionnaire de
    # depot etait connecte puis deconnecte dans la foulee - la Connection etait
    # jetee, le depot ne faisait rien, et silencieusement.
    xpg_add_test_executable(execorder_wiring_test tests/execorder_wiring_test.cpp
                   src/app/ViewModels.cpp)
    xpg_target_link_libraries(execorder_wiring_test PRIVATE xpg_hmi xpg_import xpg_ui)

    # L'IHM. hmi_test : le modele, le disque, l'annulation, les commandes
    # d'edition, les expressions (index calcules compris) et le simulateur.
    # hmi_editor_test : l'editeur de vues pilote par de vrais evenements souris
    # et clavier, sans ecran - on verifie le document ET la pile d'annulation.
    xpg_add_test_executable(hmi_test tests/hmi_test.cpp)
    xpg_target_link_libraries(hmi_test PRIVATE xpg_hmi)
    # ---- Lot API 8 : les expressions impossibles (Compiler, Generer) ----
    xpg_add_test_executable(hmicheck_expr_test tests/hmicheck_expr_test.cpp)
    xpg_target_link_libraries(hmicheck_expr_test PRIVATE xpg_hmi)
    # ---- 1.11.13 : la generation incrementale de l'IHM (cache, dependances, etats, artefacts) ----
    xpg_add_test_executable(hmi_build_test tests/hmi_build_test.cpp src/app/hmi/HmiBuild.cpp)
    xpg_target_link_libraries(hmi_build_test PRIVATE xpg_hmi xpg_import)
    # ---- 1.11.14 : IHM_LOG et la Console (niveaux, verification, source et ligne) ----
    xpg_add_test_executable(hmi_log_test tests/hmi_log_test.cpp src/app/hmi/HmiConsole.cpp)
    xpg_target_link_libraries(hmi_log_test PRIVATE xpg_hmi)
    # ---- 1.11.15 : la remanence de simulation (instantane, retour par identifiant stable, conversions) ----
    xpg_add_test_executable(hmi_simdata_test tests/hmi_simdata_test.cpp)
    xpg_target_link_libraries(hmi_simdata_test PRIVATE xpg_hmi)
    xpg_add_test_executable(hmi_retain_test tests/hmi_retain_test.cpp)        # 1.11.16 : la remanence d'exploitation
    xpg_target_link_libraries(hmi_retain_test PRIVATE xpg_hmi)
    xpg_add_test_executable(hmi_trace_test tests/hmi_trace_test.cpp)          # 1.11.17 : la trace de reference (refonte, lot 0)
    xpg_target_link_libraries(hmi_trace_test PRIVATE xpg_hmi)
    xpg_add_test_executable(hmi_migrate_test tests/hmi_migrate_test.cpp)      # 1.11.18 : la migration des blocs VAR (refonte, lot 4)
    xpg_target_link_libraries(hmi_migrate_test PRIVATE xpg_hmi)
    xpg_add_test_executable(hmi_decl_test tests/hmi_decl_test.cpp)            # 1.11.18 : les declarations lues sans perte (refonte, lot 2)
    xpg_target_link_libraries(hmi_decl_test PRIVATE xpg_hmi xpg_ui)   # xpg_ui : hmitree (l'arbre du projet)
    xpg_add_test_executable(hmi_types_test tests/hmi_types_test.cpp)          # 1.11.19 : le registre des types (refonte, lot 6)
    xpg_target_link_libraries(hmi_types_test PRIVATE xpg_hmi)
    xpg_add_test_executable(hmi_overload_test tests/hmi_overload_test.cpp)    # 1.11.20 : les signatures et les surcharges
    xpg_target_link_libraries(hmi_overload_test PRIVATE xpg_hmi)
    # ---- Lot API 8 : didacticiels et aide (les huit parcours, leurs pages de l'aide, le menu Aide) ----
    xpg_add_test_executable(lot8tutorials_test tests/lot8tutorials_test.cpp src/app/TopBar.cpp src/app/TopBarLot8.cpp src/app/TutorialsLot8.cpp)   # Lot API 8 : bandeau haut (+ TopBarLot8.cpp)
    # ---- Lot API 8 : tests - xpg_ui AVANT xpg_import : ui::buildHelp (ui/HelpDocument.cpp) appelle
    # project::formats, sectionFor... ; dans l'autre ordre, le lien statique ne les trouve pas ----
    xpg_target_link_libraries(lot8tutorials_test PRIVATE xpg_ui xpg_import)
    # 1.11 (I111, decision 16) : TopBar.cpp demande au centre des nouveautes si
    # les reperes sont masques (noveltyCenter().marksHidden()) et lit le registre
    # (help::news, dans xpg_help). Le vrai NoveltyCenter.cpp tire l'appli entiere
    # (HelpCenterScreen, MainAnalysisScreen, Settings...) : les trois essais qui
    # compilent TopBar.cpp prennent tests/noveltycenter_stub.cpp et xpg_help.
    # Sans eux, ils ne se liaient plus.
    foreach(t api2_test topbar8_test lot8tutorials_test)
        xpg_target_sources(${t} PRIVATE tests/noveltycenter_stub.cpp)
        xpg_target_link_libraries(${t} PRIVATE xpg_help)
    endforeach()
    # ---- Lot API 8 : l'explorateur de fichiers de l'appli (les familles, lister, trier,
    # filtrer, chercher, le fil d'Ariane, le nom a enregistrer, la memoire) - sans ecran ----
    # Lot API 8, 2e partie : l'apercu (src/app/FilePreview.cpp : hmi::decodeImage, xls::Workbook)
    xpg_add_test_executable(fileexplorer_test tests/fileexplorer_test.cpp src/app/FilePreview.cpp)
    xpg_target_link_libraries(fileexplorer_test PRIVATE xpg_ui xpg_hmi)
    # ---- fin Lot API 8 : l'explorateur de fichiers ----
    file(GLOB HMI_EDITOR_SOURCES CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/src/app/hmi/*.cpp)
    # Lot 13 : Capture.cpp pour le JPEG des vignettes du dossier de l'IHM.
    # Lot 15 : Recovery.cpp, la reprise apres un arret brutal.
    # Lot 19 : HistoryPanel.cpp, le tiroir de l'historique.
    # Lot 20 : TablePaste.cpp (coller depuis Excel dans les tableaux des volets),
    # GoToPanel.cpp (Aller a...).
    xpg_add_test_executable(hmi_editor_test tests/hmi_editor_test.cpp ${HMI_EDITOR_SOURCES}
                   src/app/ViewModels.cpp src/app/Brand.cpp src/app/Capture.cpp src/app/Recovery.cpp
                   src/app/HistoryPanel.cpp src/app/TablePaste.cpp src/app/GoToPanel.cpp
                   src/app/Dossiers.cpp)   # 1.8.0 : Capture.cpp demande le dossier des captures
    # 1.11 (I111, decision 16) : l'editeur et ses essais appellent le centre
    # d'aide (help::center, news, notes, keys, f1, report, Tutorial) : xpg_help,
    # comme l'appli et la construction des essais (outils/hmitests).
    xpg_target_link_libraries(hmi_editor_test PRIVATE xpg_hmi xpg_ui xpg_help)
    # 1.10.2 (CR) : l'historique interne (core/CallTrail), le rapport de plantage et le
    # chien de garde (core/CrashGuard) - le plantage et le blocage se jouent dans un fils.
    xpg_add_test_executable(crashtrail_test tests/crashtrail_test.cpp)
    xpg_target_link_libraries(crashtrail_test PRIVATE xpg_core)
    if(NOT WIN32)
        xpg_target_link_options(crashtrail_test PRIVATE -rdynamic)
    endif()
    xpg_add_test(NAME crashtrail COMMAND crashtrail_test)

    xpg_add_test(NAME core       COMMAND core_test)
    xpg_add_test(NAME menu       COMMAND menu_test)
    xpg_add_test(NAME syntax     COMMAND syntax_test)
    xpg_add_test(NAME fold       COMMAND fold_test)
    xpg_add_test(NAME layout     COMMAND layout_test)
    xpg_add_test(NAME popup      COMMAND popup_test)
    xpg_add_test(NAME sharedlib  COMMAND sharedlib_test)
    xpg_add_test(NAME settings   COMMAND settings_test)
    xpg_add_test(NAME dossiers   COMMAND dossiers_test)   # 1.8.0
    xpg_add_test(NAME edit       COMMAND edit_test)
    xpg_add_test(NAME editor     COMMAND editor_test)
    xpg_add_test(NAME simulation COMMAND simulation_test)
    xpg_add_test(NAME library    COMMAND library_test ${CMAKE_CURRENT_SOURCE_DIR}/resources/schneider_library.txt)
    xpg_add_test(NAME import     COMMAND import_test    ${XPG_FIXTURE})
    xpg_add_test(NAME viewmodel  COMMAND viewmodel_test  ${XPG_FIXTURE})
    xpg_add_test(NAME roundtrip  COMMAND roundtrip_test  ${XPG_FIXTURE})
    xpg_add_test(NAME project    COMMAND project_test    ${XPG_FIXTURE})
    xpg_add_test(NAME robustness COMMAND parser_robustness_test ${XPG_FIXTURE})
    xpg_add_test(NAME delete     COMMAND delete_test     ${XPG_FIXTURE})
    xpg_add_test(NAME grafcet    COMMAND grafcet_test    ${XPG_FIXTURE})
    xpg_add_test(NAME table      COMMAND table_test)
    xpg_add_test(NAME macro      COMMAND macro_test)
    xpg_add_test(NAME xref       COMMAND xref_test       ${XPG_FIXTURE})
    xpg_add_test(NAME equipment  COMMAND equipment_test)
    xpg_add_test(NAME font       COMMAND font_test)
    xpg_add_test(NAME theme      COMMAND theme_test)
    xpg_add_test(NAME corner     COMMAND corner_test)
    xpg_add_test(NAME hardware   COMMAND hardware_test   ${XPG_FIXTURE} ${XHW_FIXTURE})

    xpg_add_test(NAME libhelp     COMMAND libhelp_test)
    xpg_add_test(NAME helparticle COMMAND helparticle_test)
    xpg_add_test(NAME execorder   COMMAND executionorder_test)
    xpg_add_test(NAME simorder    COMMAND simorder_test)
    xpg_add_test(NAME treednd     COMMAND treednd_test)
    xpg_add_test(NAME execwiring  COMMAND execorder_wiring_test)
    xpg_add_test(NAME macroxls    COMMAND macroxls_test)
    xpg_add_test(NAME macronatives COMMAND macronatives_test)
    xpg_add_test(NAME macros2      COMMAND macros2_test ${XPG_FIXTURE})
    xpg_add_test(NAME api2         COMMAND api2_test ${XPG_FIXTURE})
    xpg_add_test(NAME api34        COMMAND api34_test ${XPG_FIXTURE} ${XHW_FIXTURE})
    xpg_add_test(NAME typeusage    COMMAND typeusage_test ${XPG_FIXTURE})
    xpg_add_test(NAME versionstate COMMAND versionstate_test)
    xpg_add_test(NAME themecatalog COMMAND themecatalog_test)
    xpg_add_test(NAME projecticon  COMMAND projecticon_test ${XPG_FIXTURE})
    xpg_add_test(NAME lisible      COMMAND lisible_test ${XPG_FIXTURE})
    xpg_add_test(NAME membertree   COMMAND membertree_test ${XPG_FIXTURE})
    xpg_add_test(NAME renameplan   COMMAND renameplan_test ${XPG_FIXTURE})
    xpg_add_test(NAME mastimport   COMMAND mastimport_test ${XPG_FIXTURE} ${XHW_FIXTURE})
    xpg_add_test(NAME projectstats COMMAND projectstats_test ${XPG_FIXTURE})
    xpg_add_test(NAME simdebugui   COMMAND simdebugui_test ${XPG_FIXTURE})   # ---- Lot API 8 : Simulation > Debogage ----
    xpg_add_test(NAME dialogwindow COMMAND dialogwindow_test)                # ---- Lot API 8 : les dialogues dans la fenetre detachee ----
    xpg_add_test(NAME exportask    COMMAND exportask_test)                   # ---- Lot API 8 : les exports qui demandent ou ----
    xpg_add_test(NAME themefile    COMMAND themefile_test)                   # ---- Lot API 8 : themes ----
    xpg_add_test(NAME simstatus    COMMAND simstatus_test)                   # ---- Lot API 8 : Centre de simulation ----
    xpg_add_test(NAME statusstrip  COMMAND statusstrip_test)                 # ---- Lot API 8 : le bandeau bas ----
    xpg_add_test(NAME simdebug     COMMAND simdebug_test ${XPG_FIXTURE})     # ---- Lot API 8 : le moteur de simulation ----
    xpg_add_test(NAME dropfiles    COMMAND dropfiles_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ihm)   # ---- Lot API 8 : glisser n'importe quel fichier ----
    xpg_add_test(NAME renamehmipaths COMMAND rename_hmi_paths_test)         # ---- Lot API 8 : renommer dans l'IHM par tous les chemins ----
    xpg_add_test(NAME filtermemory COMMAND filtermemory_test)                # ---- Lot API 8 : les filtres retenus ----
    # 1.11 (I111) : les echantillons de l'IHM (tests/fixtures/ihm) nommes, et non
    # pris a cote de XPG_FIXTURE : un MAST.XPG designe ailleurs n'a pas son ihm/.
    xpg_add_test(NAME hmi          COMMAND hmi_test ${XPG_FIXTURE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ihm)
    xpg_add_test(NAME hmieditor    COMMAND hmi_editor_test ${XPG_FIXTURE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ihm)
    xpg_add_test(NAME hmicheckexpr COMMAND hmicheck_expr_test)               # ---- Lot API 8 : les expressions impossibles ----
    xpg_add_test(NAME hmibuild     COMMAND hmi_build_test)                   # ---- 1.11.13 : la generation incrementale ----
    xpg_add_test(NAME hmilog       COMMAND hmi_log_test)                     # ---- 1.11.14 : IHM_LOG et la Console ----
    xpg_add_test(NAME hmisimdata   COMMAND hmi_simdata_test)                 # ---- 1.11.15 : la remanence de simulation ----
    xpg_add_test(NAME hmiretain    COMMAND hmi_retain_test)                  # ---- 1.11.16 : la remanence d'exploitation ----
    xpg_add_test(NAME hmitrace     COMMAND hmi_trace_test ${CMAKE_CURRENT_SOURCE_DIR}/bac/Armoire_Gaz      # ---- 1.11.17 : la trace de reference ----
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/trace/armoire_gaz.trace)
    xpg_add_test(NAME hmimigrate   COMMAND hmi_migrate_test ${CMAKE_CURRENT_SOURCE_DIR}/bac/Armoire_Gaz)    # ---- 1.11.18 : la migration (lot 4) ----
    xpg_add_test(NAME hmitracemig  COMMAND hmi_trace_test ${CMAKE_CURRENT_SOURCE_DIR}/bac/Armoire_Gaz      # ---- la meme trace, migre ----
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/trace/armoire_gaz.trace --migrer)
    xpg_add_test(NAME hmitypes     COMMAND hmi_types_test)                   # ---- 1.11.19 : le registre des types (lot 6) ----
    xpg_add_test(NAME hmioverload  COMMAND hmi_overload_test)                # ---- 1.11.20 : les signatures et les surcharges ----
    xpg_add_test(NAME hmidecl      COMMAND hmi_decl_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/decl/corpus.txt   # ---- 1.11.18 : les declarations (lot 2) ----
                 ${CMAKE_CURRENT_SOURCE_DIR}/bac/Armoire_Gaz ${CMAKE_CURRENT_SOURCE_DIR}/projets/project)
    # ---- 1.11.17 (refonte des scripts, lot 0) : XpgAnalyzer.vcxproj (outils\build.bat, MSBuild)
    #  nomme chaque source que CMake ramasse ; il en manquait vingt et un depuis la 1.11.3.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME vcxproj COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/outils/verifier_vcxproj.py)
    endif()
    xpg_add_test(NAME searchfocus COMMAND searchfocus_test)                  # ---- Lot API 8 : finitions (Ctrl+F) ----
    xpg_add_test(NAME lot8tutorials COMMAND lot8tutorials_test)              # ---- Lot API 8 : didacticiels et aide ----
    xpg_add_test(NAME topbar8 COMMAND topbar8_test)                          # ---- Lot API 8 : bandeau haut ----
    xpg_add_test(NAME fileexplorer COMMAND fileexplorer_test ${XPG_FIXTURE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ihm)   # ---- Lot API 8 : l'explorateur de fichiers (2e partie : [MAST.XPG] [ihm]) ----

    # LES TESTS QUI LISENT libs/ NE SONT ENREGISTRES QUE SI libs/ EST LA.
    #
    # La bibliotheque est livree dans une archive a part et se range ou
    # l'integrateur veut. Enregistrer les tests quand meme donnerait quatre
    # echecs rouges qui ne disent rien sur le code - et un CI rouge en
    # permanence est un CI qu'on cesse de lire. XPG_LIBS_DIR permet de la
    # designer ailleurs : cmake -DXPG_LIBS_DIR=D:/affaires/libs
    set(XPG_LIBS_DIR "${XPG_LIBS}" CACHE PATH "Le dossier libs/ des DDT, DFB et macros")
    if(EXISTS "${XPG_LIBS_DIR}")
        xpg_add_test(NAME libcatalog  COMMAND libcatalog_test      ${XPG_LIBS_DIR})
        xpg_add_test(NAME macrohelp   COMMAND macrohelp_parse_test ${XPG_LIBS_DIR})
        xpg_add_test(NAME libpage     COMMAND libraryhelppage_test ${XPG_LIBS_DIR})
        # help_test lance les exemples de TOUTE la bibliotheque dans le vrai
        # simulateur. C'est lui qui dira le jour ou un exemple de l'aide est
        # devenu faux - aujourd'hui 27 sur 27 tournent.
        xpg_add_test(NAME help        COMMAND help_test            ${XPG_LIBS_DIR})
        xpg_add_test(NAME helpsession COMMAND helpsession_test     ${XPG_LIBS_DIR})
        # Lot macros 1 : les 31 macros livrees et les modeles se lisent sans defaut.
        xpg_add_test(NAME macros2libs COMMAND macros2_test ${XPG_FIXTURE} ${XPG_LIBS_DIR})
        # Lot API 2 : le tableau de bord face a la bibliotheque, les pastilles.
        xpg_add_test(NAME api2libs    COMMAND api2_test ${XPG_FIXTURE} ${XPG_LIBS_DIR})
        # Lot API 6 : les 31 macros relues puis reecrites a l'identique.
        xpg_add_test(NAME macrospecwrite COMMAND macrospec_write_test ${XPG_LIBS_DIR}/Macros)
        # ---- Lot API 8 : le moteur de simulation sur le projet d'essai, BUILDING 0.24 (cycles complets) ----
        xpg_add_test(NAME simdebuglibs COMMAND simdebug_test ${XPG_FIXTURE} ${XPG_LIBS_DIR})
    else()
        message(STATUS "libs/ introuvable (${XPG_LIBS_DIR}) : les tests de l'aide "
                       "sont compiles mais pas enregistres")
    endif()

    # Idem pour les classeurs d'essai : xlsm_test lit des fichiers fabriques par
    # tests/make_fixtures.py, qui ne sont pas dans le depot.
    if(EXISTS "${XLSM_FIXTURES}/simple.xlsx")
        xpg_add_test(NAME xlsm COMMAND xlsm_test ${XLSM_FIXTURES})
    else()
        message(STATUS "fixtures .xlsx absentes : xlsm_test est compile mais pas "
                       "enregistre (python3 tests/make_fixtures.py pour les creer)")
    endif()

    get_property(sautes GLOBAL PROPERTY XPG_ESSAIS_SAUTES)
    get_property(non_enregistres GLOBAL PROPERTY XPG_ESSAIS_NON_ENREGISTRES)
    list(LENGTH sautes n_sautes)
    list(LENGTH non_enregistres n_non)
    if(n_sautes OR n_non)
        message(STATUS "essais : ${n_sautes} sautes (source absente), ${n_non} non enregistres (fichier d'entree absent)")
    endif()
endif()