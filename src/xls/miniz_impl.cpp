// =============================================================================
//  xls/miniz_impl.cpp — miniz, compile AVEC le lecteur
// -----------------------------------------------------------------------------
//  POURQUOI CE FICHIER EXISTE.
//
//  La premiere version demandait d'ajouter third_party/miniz.c au projet, a
//  cote de src/xls/Workbook.cpp. C'est une etape qu'on oublie - et quand on
//  l'oublie, l'editeur de liens dit :
//
//      LNK2019  symbole externe non resolu mz_zip_reader_init_file
//
//  ce qui n'apprend rien a qui n'a pas ecrit miniz. Les noms sont NON DECORES,
//  donc l'appelant les demande bien en liaison C et les declarations sont
//  correctes : il manque juste le fichier qui les definit.
//
//  En posant l'implementation ICI, ajouter le dossier src/xls/ a un projet
//  suffit. C'est le seul geste a faire, et on ne peut pas le faire a moitie.
//
//  N'AJOUTEZ PAS third_party/miniz.c AU PROJET EN PLUS DE CE FICHIER. Les deux
//  definiraient les memes fonctions, et LNK2019 deviendrait LNK2005 - symbole
//  deja defini - ce qui n'est pas un progres.
//
//  MINIZ_NO_DEFLATE_APIS N'EST PAS UNE OPTIMISATION. Sans lui, miniz ne compile
//  pas en C++ : s_tdefl_num_probes y est defini deux fois, ce que le C tolere -
//  ce sont des definitions tentatives - et que le C++ refuse. Le drapeau retire
//  la partie COMPRESSION, dont un lecteur de classeur n'a aucun besoin. Il est
//  pose ici plutot que dans le systeme de construction pour la meme raison que
//  le reste : un drapeau qu'il faut penser a mettre est un drapeau qu'on
//  oubliera.
// =============================================================================

// 1.8.0 : QUAND LE SYSTEME DE CONSTRUCTION AJOUTE third_party/miniz.c (CMake le
// fait, et le dit par XPG_HAVE_MINIZ), CE FICHIER SE TAIT. miniz.c, compile en C,
// garde la compression (l'export lisible : le classeur et le PDF compresses) ;
// les deux ensemble definiraient les memes fonctions.
#ifndef XPG_HAVE_MINIZ

#ifndef MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_DEFLATE_APIS
#endif

// miniz ouvre les fichiers avec fopen. MSVC le deconseille depuis quinze ans et
// emet C4996 ; sur un projet compile avec /WX cet avertissement devient une
// erreur, dans un fichier qui n'est pas le notre et qu'on ne veut pas modifier.
#ifdef _MSC_VER
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#  pragma warning(push)
#  pragma warning(disable : 4127 4244 4267 4996)
#endif

#include "../../third_party/miniz.c"

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#endif // XPG_HAVE_MINIZ
