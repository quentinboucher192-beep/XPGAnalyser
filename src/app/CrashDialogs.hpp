// =============================================================================
//  app/CrashDialogs.hpp - 1.10.2 (CR) : les fenetres du plantage et du blocage,
//  cote appli (core/CrashGuard.hpp fait le reste).
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace core::crash { struct PendingReport; }

namespace app {
class App;
}

namespace app::crashdialogs {

// La fenetre native du blocage, avec les boutons « Attendre » et « Fermer XPGAnalyser »
// (SDL_ShowMessageBox), donnee au chien de garde. Appelee une fois, apres
// core::crash::install().
void install();

// ---- au lancement suivant ------------------------------------------------------------
// La fenetre « Un plantage s'est produit le … » s'il y a un rapport nouveau
// (core::crash::takeNewReport) : Ouvrir le dossier, Copier le rapport, Fermer.
// Une boite native de SDL, avant la premiere image (le chien de garde ne tourne pas
// encore). Rend vrai si elle s'est ouverte.
bool showPendingReport();

// Le crochet de la 1.11 : « Signaler le probleme ». Quand il est donne, la fenetre a un
// bouton de plus, qui l'appelle avec le rapport (le chemin, la date, le genre, le texte).
// Il rend le message a montrer ensuite (vide : rien), la fenetre reste ouverte.
using ReportHook = std::function<std::string(const core::crash::PendingReport&)>;
void setReportHook(ReportHook hook);
inline constexpr const char* kReportButton = "Signaler le probl\xC3\xA8me";

// ---- Aide > Journal interne (historique des appels) ---------------------------------
// Le texte : la pile des portees ouvertes de chaque fil, puis les `entries` dernieres
// entrees, les plus recentes en tete ; `filter` (vide : tout) garde les lignes qui le
// contiennent, sans tenir compte de la casse (ASCII).
[[nodiscard]] std::string callTrailText(std::size_t entries, std::string_view filter = {});
// La fenetre : un extrait (les 40 dernieres entrees et les piles) ; le bouton principal
// copie les 2 000 dernieres entrees dans le presse-papiers et les exporte dans crashs/
// (journal-…txt, le format d'un rapport).
void showCallTrail(App& app);

// ---- la reprise apres un blocage ----------------------------------------------------
// La note du journal de l'appli quand la boucle principale repart (takeRecovery).
[[nodiscard]] std::string recoveryNote(double stalledSeconds);

} // namespace app::crashdialogs
