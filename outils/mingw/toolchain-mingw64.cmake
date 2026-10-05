# =============================================================================
#  outils/mingw/toolchain-mingw64.cmake - 1.11.3 : compiler l'exe Windows sous
#  Linux, avec MinGW-w64 (GCC, posix threads). Voir outils/mingw/cross_mingw.sh.
# =============================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
# Le runtime C++ (libstdc++, libgcc, winpthread) DANS l'exe : un PC vierge n'a
# rien a installer a cote, comme le runtime Visual C++ copie par package.bat.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++ -mwindows")
