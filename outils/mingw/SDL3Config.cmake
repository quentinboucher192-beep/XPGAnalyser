# =============================================================================
#  outils/mingw/SDL3Config.cmake - 1.11.3 : SDL3 pour MinGW, depuis le paquet
#  Visual Studio de third_party/SDL3 (memes en-tetes, meme SDL3.dll). La
#  bibliotheque d'import MinGW (libSDL3.dll.a) est faite par cross_mingw.sh
#  depuis la table d'exportation de SDL3.dll (dlltool).
# =============================================================================
get_filename_component(_xpg_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET SDL3::SDL3)
    add_library(SDL3::SDL3 SHARED IMPORTED)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION "${_xpg_root}/third_party/SDL3/lib/x64/SDL3.dll"
        IMPORTED_IMPLIB "${SDL3_MINGW_IMPLIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_xpg_root}/third_party/SDL3/include")
endif()
set(SDL3_FOUND TRUE)
