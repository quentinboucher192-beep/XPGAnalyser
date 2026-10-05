// =============================================================================
//  app/AboutDialog.hpp - 1.10, chantier R2 (menu Aide, rapide n. 5)
// -----------------------------------------------------------------------------
//  "A propos d'XPGAnalyser" : la version, la date de construction, les dossiers
//  de l'application, les composants libres et leurs licences ; un bouton copie
//  les informations pour le support (version, systeme, dossiers, projet ouvert :
//  son nom, ses nombres de sections et de vues - jamais ses donnees).
// =============================================================================
#pragma once

#include <string>

namespace app {

    class App;

    // Le texte de la fenetre (francais, UTF-8).
    [[nodiscard]] std::string aboutText(const App& app);
    // Ce que "Copier les informations pour le support" met dans le presse-papiers.
    [[nodiscard]] std::string supportInformation(const App& app);
    // La fenetre ; le bouton principal copie supportInformation().
    void showAboutDialog(App& app);

} // namespace app
