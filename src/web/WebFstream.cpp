// =============================================================================
//  web/WebFstream.cpp - 1.12.31 : <fstream> pour la page web
// -----------------------------------------------------------------------------
//  La libc++ wasm32-wasi d'ici est construite sans <filesystem> ; ses classes de
//  fichiers (basic_filebuf...) ne sont donc pas instanciees dans la bibliotheque.
//  Le moteur en declare (std::ifstream, std::ofstream) meme si, sur la page, il
//  ne lit ni n'ecrit aucun fichier (pas de disque : l'archive est en memoire). On
//  les instancie ici, une fois, pour que l'edition des liens les trouve.
// =============================================================================
#ifdef XPG_WEB

#include <fstream>

template class std::basic_filebuf<char>;
template class std::basic_ifstream<char>;
template class std::basic_ofstream<char>;
template class std::basic_fstream<char>;

#endif // XPG_WEB
