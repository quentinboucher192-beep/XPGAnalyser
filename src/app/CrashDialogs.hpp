// =============================================================================
//  app/CrashDialogs.hpp - 1.10.2 (CR) : les fenetres du plantage et du blocage,
//  cote appli (core/CrashGuard.hpp fait le reste).
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "../ui/widgets/CrashReport.hpp"   // 1.12.28 : la fenetre du rapport, dessinee par l'appli

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
// 1.12.28 : le dialogue de l'appli (ui::CrashReportDialog), plus la boite native : « Un
// blocage s'est produit » s'il y a un rapport nouveau (core::crash::takeNewReport) - ce qui
// s'est passe, ce que l'appli faisait, le rapport ; Ouvrir le dossier, Copier le rapport,
// Signaler le probleme, Fermer. Il s'ouvre a la premiere image, dans la fenetre principale
// (avant, la boite native venait avant la premiere image : la fenetre restait noire).
// Rend vrai s'il y avait un rapport.
bool showPendingReport(App& app);
// 1.12.28 : le dialogue sur le rapport le plus recent, meme deja vu (la session rejouee
// « rapport-dialogue », pour les captures). Faux : aucun rapport.
bool showNewestReport(App& app);

// Ce que la fenetre d'un rapport sait faire, pour ce rapport : les gestes communs au dialogue
// (au lancement suivant) et a la fenetre a part (app/ReportWindow : un blocage, un plantage).
[[nodiscard]] ui::CrashReportSetup setupFor(const core::crash::PendingReport& report, ui::crashreport::Mode mode);

// Le crochet de la 1.11 : « Signaler le probleme ». Quand il est donne, la fenetre a un
// bouton de plus, qui l'appelle avec le rapport (le chemin, la date, le genre, le texte).
// Il rend le message a montrer ensuite (vide : rien), la fenetre reste ouverte.
// 1.12.28 : prepareSignal est celui de l'appli (le zip de Signaler) ; main le pose.
using ReportHook = std::function<std::string(const core::crash::PendingReport&)>;
void setReportHook(ReportHook hook);
[[nodiscard]] std::string prepareSignal(const core::crash::PendingReport& report);
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
