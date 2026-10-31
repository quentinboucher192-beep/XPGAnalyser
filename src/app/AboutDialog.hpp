// =============================================================================
//  app/AboutDialog.hpp - 1.10, chantier R2 (menu Aide, rapide n. 5)
// -----------------------------------------------------------------------------
//  "A propos d'XPGAnalyser" : la version, la date de construction, les dossiers
//  de l'application, les composants libres et leurs licences ; un bouton copie
//  les informations pour le support (version, systeme, dossiers, projet ouvert :
//  son nom, ses nombres de sections et de vues - jamais ses donnees).
//
//  1.21.0 : LES FICHIERS DE L'APPLICATION. Sous l'en-tete (le logo, le nom de
//  l'edition, sa version, la construction) et les informations (elles
//  defilent), un arbre de TOUS les fichiers de l'application avec leur poids :
//  le dossier du programme, les reglages de l'utilisateur (l'Assistant IA, les
//  rapports de plantage...), les donnees (projets, bibliotheque, captures), les
//  journaux. Le poids total en evidence, celui de chaque racine sur sa ligne.
//  Le compte se fait sur un fil de fond (app/ArbreTailles.hpp) : la fenetre
//  s'ouvre tout de suite, un compteur avance, puis l'arbre ; la fermer arrete
//  le compte. Ouvrir le dossier, Trier par nom / par poids, Actualiser, Copier
//  le resume. La fenetre se deplace et s'agrandit (ui/DialogFrame.hpp).
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
