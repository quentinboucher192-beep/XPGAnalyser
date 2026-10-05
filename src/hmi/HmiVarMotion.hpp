// =============================================================================
//  hmi/HmiVarMotion.hpp - 1.11.6 : forcer une variable par un mouvement entre
//                         deux bornes (les onglets Variables IHM et Variables API)
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 05/10 : « ajouter forcage par borne et type dans
//  variables IHM et API dans IHM -> Simulation » - comme les esclaves simules :
//  un TYPE de mouvement (constante, sinus, rampe, compteur, clignote, aleatoire,
//  etapes) et ses BORNES (min, max) avec une periode. La variable est tenue (forcee)
//  a la valeur du mouvement a chaque cycle de la simulation ; la liberer arrete le
//  mouvement.
//
//  LES MEMES FORMULES que les comportements d'un esclave (HmiTwin.cpp,
//  Behaviors::tick) : sinus de a a b sur une periode, rampe de a a b, compteur
//  (depart a, pas b, toutes les periodes), clignote (a 1 pendant `delay` s toutes
//  les periodes ; sans : une periode sur deux), aleatoire (une valeur dans [a, b]
//  toutes les periodes), etapes ("1; 5; 3", une par periode).
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::motion {

struct Motion {
    BehaviorKind kind{BehaviorKind::Sine};
    double       a{0};          // le minimum ; la valeur (constante) ; le depart (compteur)
    double       b{100};        // le maximum ; le pas (compteur)
    double       period{10};    // s
    double       delay{0};      // clignote : a 1 pendant delay s (0 : une periode sur deux)
    std::string  steps;         // etapes : "1; 5; 3"
    bool operator==(const Motion&) const = default;
};

// Les mouvements proposes (ni recopie ni « suit l'automate » : ils lisent une adresse) ;
// pour un BOOL : constante, clignote, etapes.
[[nodiscard]] std::vector<BehaviorKind> kindsFor(bool boolean);
// Un mouvement neuf de ce genre, autour de la valeur du moment.
[[nodiscard]] Motion defaultFor(BehaviorKind kind, bool boolean, double current);

struct State {
    double        nextAt{-1};
    double        value{0};
    std::uint64_t seed{0x9E3779B97F4A7C15ull};
};
// La valeur du mouvement a l'heure t (s, depuis le depart de la simulation).
[[nodiscard]] double valueAt(const Motion&, double t, State&);

// En clair : "sinus 20 -> 80 · 10 s", "clignote 1 s / 4 s", "constante 50".
[[nodiscard]] std::string text(const Motion&);
// Ce que propose le champ des bornes : "20 ; 80 ; 10" (min ; max ; periode), "50" (constante)...
[[nodiscard]] std::string editText(const Motion&);
// Le champ relu : "20 ; 80" (les bornes), "20 ; 80 ; 30" (et la periode, en s), "50"
// (une constante), "1,5 ; 4" (clignote : a 1 pendant ; la periode), "1; 5; 3" (etapes,
// la periode garde la sienne). La virgule decimale est acceptee. Faux : illisible (why).
bool parse(std::string_view text, Motion& m, std::string* why = nullptr);

} // namespace hmi::motion
