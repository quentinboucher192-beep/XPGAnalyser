// =============================================================================
//  hmi/HmiReports.hpp - les rapports periodiques (lot 14)
// -----------------------------------------------------------------------------
//  UN RAPPORT A SON HEURE. Chaque jour, chaque semaine (un jour dit) ou chaque
//  mois (un jour du mois), a l'heure dite : la periode qui finit la. Le moteur
//  (Runtime::reportsCycle) le construit et l'ecrit dans exports/ (PDF ou
//  Excel), puis l'ecran l'envoie aux destinataires dits, en piece jointe.
//
//  CE QU'IL CONTIENT, PRIS DANS L'HISTORIQUE :
//    la synthese   les alarmes apparues (par priorite), la plus longue, les
//                  mesures, la production ;
//    les alarmes   chaque apparition de la periode : quand, laquelle, sa
//                  priorite, son message, acquittee par qui, disparue, sa duree ;
//                  puis les plus frequentes ;
//    les mesures   chaque variable archivee : minimum, moyenne, maximum, la
//                  derniere valeur, le nombre de mesures ;
//    la production les compteurs de production (bons, rebuts, cadence, TRS) ;
//    les evenements (au choix) : connexions, recettes, ecritures...
//
//  Les heures sont celles du poste (l'heure legale : un jour de changement
//  d'heure dure 23 ou 25 heures).
// =============================================================================
#pragma once

#include "HmiExport.hpp"
#include "HmiHistory.hpp"
#include "HmiModel.hpp"
#include "HmiProduction.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::report {

// "06:00" -> 360 ; faux : illisible.
[[nodiscard]] bool parseTime(std::string_view text, int& minutes);

// L'instant (secondes depuis 1970) du dernier depart <= now, du prochain > now,
// et le debut de la periode qui finit a un depart.
[[nodiscard]] double lastDue(const Report&, double nowEpoch);
[[nodiscard]] double nextDue(const Report&, double nowEpoch);
[[nodiscard]] double periodStart(const Report&, double dueEpoch);

// "2026-09-25 06:00" (heure du poste) ; et l'inverse d'un horodatage de
// l'historique ("2026-09-25 06:00:12.350") - NaN s'il est illisible.
[[nodiscard]] std::string stampOf(double epoch, bool seconds = false);
[[nodiscard]] double      epochOf(std::string_view stamp);

// Le libelle de la periode : "Journalier", "Hebdomadaire", "Mensuel".
[[nodiscard]] std::string periodLabel(const Report&);

struct Production {
    std::string       name;       // l'objet (et sa vue)
    ProductionFigures figures;
};

struct Content {
    std::string title;                                         // "Rapport journalier - Armoire_Gaz"
    std::string subtitle;                                      // "du 2026-09-24 06:00 au 2026-09-25 06:00 - ecrit le ..."
    std::vector<std::pair<std::string, std::string>> summary;  // la synthese : libelle, valeur
    std::vector<int> byPriority;                               // les apparitions par priorite (1..4)
    std::vector<ExportTable> sections;                         // alarmes, frequentes, mesures, production, evenements
};

// Le rapport de la periode [from, to[.
[[nodiscard]] Content build(const Project&, const History&, const Report&, double fromEpoch, double toEpoch,
                            const std::vector<Production>& production, std::string_view writtenAt);

// Le fichier : un PDF (A4 portrait : la synthese, un graphique des alarmes
// par priorite, puis chaque section) ou un classeur Excel (une feuille par
// section, la synthese d'abord).
[[nodiscard]] Bytes render(const Content&, ExportFormat);
[[nodiscard]] Bytes renderPdf(const Content&);
[[nodiscard]] Bytes renderXlsx(const Content&);

// "rapport_Journalier_2026-09-25.pdf" (le jour de la fin de la periode).
[[nodiscard]] std::string fileName(const Report&, double toEpoch, ExportFormat);

} // namespace hmi::report
