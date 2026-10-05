// =============================================================================
//  hmi/HmiPerf.hpp - mesurer la marche (lot 13) : le cycle IHM, les scripts,
//                    l'evaluation des expressions, le dessin
// -----------------------------------------------------------------------------
//  LE MOTEUR MESURE CE QU'IL FAIT : le travail de chaque cycle IHM (scripts
//  generaux et de vue, alarmes, echantillons des courbes et des archives,
//  compteurs), chaque execution de script, les ecritures. Un cycle plus long
//  que sa periode (Configuration > Projet) est un DEPASSEMENT : l'IHM prend du
//  retard sur ce qu'elle doit surveiller.
//
//  L'ECRAN MESURE LE RESTE et le donne au moteur (Runtime::notePerf) :
//  l'evaluation des expressions de la vue montree, et son dessin.
//
//  Les temps sont en millisecondes, mesures a l'horloge du poste
//  (steady_clock) : ils changent d'un poste a l'autre, d'une marche a l'autre.
//  Le releve se remet a zero (onglet Performances de la simulation).
// =============================================================================
#pragma once

#include "HmiExport.hpp"
#include "HmiModel.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>

namespace hmi {

struct PerfSeries {
    std::size_t count{0};
    double      last{0}, total{0}, max{0};
    void add(double ms) {
        ++count;
        last = ms;
        total += ms;
        max = std::max(max, ms);
    }
    [[nodiscard]] double mean() const noexcept { return count ? total / static_cast<double>(count) : 0.0; }
};

struct ScriptPerf {
    std::string name;       // "script Animation_Ligne", "Vue_Commandes.OnCycle"
    PerfSeries  time;
};

struct PerfStats {
    PerfSeries                cycle;          // le travail d'un cycle IHM
    std::size_t               overruns{0};    // cycles plus longs que la periode
    double                    periodMs{100};
    std::map<Id, ScriptPerf>  scripts;        // par script
    PerfSeries                evaluate;       // l'ecran : les expressions de la vue
    PerfSeries                paint;          // l'ecran : le dessin
    std::size_t               expressions{0}; // evaluees au dernier rafraichissement
    std::size_t               objects{0};     // dessines au dernier rafraichissement
    std::size_t               writes{0};      // ecritures (IHM et automate) depuis le debut
    double                    since{0};       // le debut du releve (secondes de marche)
};

// Le releve : Mesure, Derniere, Moyenne, Maximum (et les N scripts les plus
// longs). `elapsedS` : la duree du releve, pour les ecritures par seconde.
[[nodiscard]] ExportTable perfTable(const PerfStats&, double elapsedS, std::size_t topScripts = 8);
// "12,4 ms" (une decimale).
[[nodiscard]] std::string perfMs(double ms);

} // namespace hmi
