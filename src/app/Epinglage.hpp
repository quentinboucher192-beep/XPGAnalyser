// =============================================================================
//  app/Epinglage.hpp - 1.8.0 : epingler XPGAnalyser a la barre des taches
// -----------------------------------------------------------------------------
//  L'installateur n'a pas le droit d'epingler un programme (Microsoft l'interdit
//  aux installateurs) : il note le souhait dans installation.ini
//  ([Installation] epingler_barre_taches = oui), et l'application, au premier
//  lancement, le DEMANDE a Windows (TaskbarManager.RequestPinCurrentAppAsync,
//  Windows.UI.Shell) : Windows affiche sa propre confirmation. Sur un Windows qui
//  ne le permet pas a une application non empaquetee, on montre le geste a faire.
// =============================================================================
#pragma once

#include <cstdint>

namespace app::epinglage {

enum class Resultat : std::uint8_t {
    Demande,        // Windows a recu la demande : il affiche sa confirmation
    NonPermis,      // ce Windows ne le permet pas ici : montrer le geste
    Indisponible,   // pas de Windows.UI.Shell (autre systeme, wine)
};

// Ne bloque pas : la demande part, la reponse de l'utilisateur arrive a Windows.
[[nodiscard]] Resultat demanderBarreDesTaches();

} // namespace app::epinglage
