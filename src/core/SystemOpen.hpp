// =============================================================================
//  core/SystemOpen.hpp - 1.32.0 : ouvrir un fichier ou un dossier avec le
//  programme du systeme, SANS jamais lancer un programme ni passer par un shell
// -----------------------------------------------------------------------------
//  Avant la 1.32.0, plusieurs ecrans ouvraient un fichier par system("start ...")
//  ou system("xdg-open \"...\"") : sous Linux, un nom de fichier avec $(...) ou
//  `...` lancait une commande ; sous Windows, un fichier .hta, .lnk ou .exe venu
//  d'un projet (un fichier externe, une piece jointe) etait LANCE.
//
//  Ici, un seul chemin, pour tout le logiciel :
//    - un dossier s'ouvre toujours (l'Explorateur, le gestionnaire de fichiers) ;
//    - un fichier dont le type lance du code (programmes, scripts, raccourcis,
//      installeurs, aide compilee...) est REFUSE, avec une phrase qui le dit :
//      l'utilisateur l'ouvre lui-meme depuis l'Explorateur s'il lui fait confiance ;
//      sous Linux, un fichier executable (droit x) aussi ;
//    - Windows : ShellExecuteW ("open") ; Linux : xdg-open lance par posix_spawnp
//      avec une liste d'arguments (aucun shell) ; macOS : open, de meme.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace core {

// Le type de ce fichier lance-t-il du code a l'ouverture (.exe, .bat, .ps1, .hta, .lnk, .url, .msi, .scr...) ?
[[nodiscard]] bool launchesCode(std::string_view pathUtf8);
// Ouvrir `pathUtf8` (UTF-8) avec le programme du systeme. Vide : ouvert ; sinon la phrase (en francais).
[[nodiscard]] std::string openWithSystem(const std::string& pathUtf8);

// 1.32.0 : au tout debut de main (chaque programme). Windows : les DLL chargees par leur nom ne se cherchent
// plus que dans le dossier du programme et dans System32 - jamais dans le dossier courant (un projet, un
// dossier de donnees) ni dans le PATH : une DLL posee a cote d'un projet ne se charge pas. Ailleurs : rien.
void hardenLibrarySearch() noexcept;

// 1.32.0 : `target` passe-t-il par un lien (lien symbolique, jonction : un point de reparse sous Windows) SOUS `root` ?
// Les elements qui existent, au-dela de ceux de `root` (compte en elements) ; s'arrete au premier qui n'existe pas.
// Sous Windows, weakly_canonical (libstdc++) ne suit pas les jonctions : c'est la garde des dossiers permis.
[[nodiscard]] bool linkBelow(const std::filesystem::path& target, const std::filesystem::path& root);

} // namespace core
