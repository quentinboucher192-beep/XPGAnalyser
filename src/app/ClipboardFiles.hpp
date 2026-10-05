// =============================================================================
//  app/ClipboardFiles.hpp - les fichiers copies dans l'Explorateur (lot macros 1)
// -----------------------------------------------------------------------------
//  Ctrl+C sur un fichier dans l'Explorateur ne met pas de TEXTE dans le
//  presse-papiers : il y met une liste de fichiers (CF_HDROP sous Windows,
//  text/uri-list ailleurs). SDL ne lit que le texte et les images ; sans ceci,
//  Ctrl+V dans un champ de fichier ne collait rien.
//
//  Sous Windows, l'API Win32 directement (et windows.h reste enferme ici) ;
//  ailleurs, App passe le text/uri-list que SDL sait lire a parseUriList.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace app {

// Les fichiers du presse-papiers de Windows (CF_HDROP), en UTF-8. Vide ailleurs.
[[nodiscard]] std::vector<std::string> win32ClipboardFiles();

// "file:///C:/Affaires/a%20b.xlsm\r\n# commentaire\r\n..." -> {"C:/Affaires/a b.xlsm"}.
// Les lignes qui ne sont pas des file:// sont ignorees.
[[nodiscard]] std::vector<std::string> parseUriList(std::string_view text);

// Un chemin colle comme du texte : sans ses guillemets ("Copier en tant que
// chemin d'acces" les met), sans blancs ni retour a la ligne autour, et un
// file:// devenu chemin.
[[nodiscard]] std::string cleanPastedPath(std::string_view text);

} // namespace app
