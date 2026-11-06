// =============================================================================
//  hmi/HmiViewCycle.hpp - 1.27.0 : le cycle d'une vue, deballe (l'OnCycle a son moment)
// -----------------------------------------------------------------------------
//  LES ETAPES DU CYCLE D'UNE VUE, dans l'ordre ou le moteur les fait (HmiRuntime.cpp,
//  Runtime::tick et Runtime::cycle ; l'ecran de la simulation pour 8 et 9) :
//
//      1 Lecture des equipements      6 Alarmes
//      2 Entrees                      7 Courbes, archives, compteurs
//      3 Minuteries                   8 Formules des objets (fx)
//      4 Scripts generaux cycliques   9 Dessin de la vue
//      5 Declencheurs des objets     10 Ecritures envoyees
//
//  UN SCRIPT DE CYCLE (un script de vue dont l'evenement est OnCycle) s'execute APRES
//  L'ETAPE `Script::stage` (1 a 9 : apres 10, c'est avant 1 du cycle suivant), 1 cycle
//  sur `everyCycles`, et seulement si sa condition `when` est vraie. Une vue en a
//  plusieurs ; dans un meme moment, ils tournent dans leur ordre dans la vue (leur rang).
//  L'OnCycle d'avant est un script de cycle apres l'etape 4 : un projet existant ne change
//  pas.
//
//  CE QUE CHAQUE MOMENT VEUT DIRE EXACTEMENT : Step::exact (l'infobulle de l'etape, le
//  guide) - ce que le moteur fait, et ce qui n'est pas une etape a part entiere.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::viewcycle {

inline constexpr int kSteps = 10;            // les etapes du cycle de la vue
inline constexpr int kFirstMoment = 1;       // un script se place apres l'etape 1...
inline constexpr int kLastMoment = 9;        // ... jusqu'apres l'etape 9 (apres 10 : avant 1 du cycle suivant)
inline constexpr int kLegacyMoment = 4;      // l'OnCycle d'avant : apres les scripts generaux, avant les declencheurs
inline constexpr int kMaxEvery = 1000;       // 1 cycle sur 1000 au plus
inline constexpr std::string_view kCycleEvent = "OnCycle";

struct Step {
    int              number{0};
    std::string_view title;       // "Lecture des \xC3\xA9quipements"
    std::string_view shortTitle;  // "Lecture" (la ligne pointillee : apres 8 Formules, avant 9 Dessin)
    std::string_view detail;      // "variables li\xC3\xA9" "es (%MW, Modbus), leur qualit\xC3\xA9"
    std::string_view exact;       // ce que le moteur fait exactement (l'infobulle, le guide)
    std::string_view after;       // ce que voit un script place apres elle (la ligne pointillee)
};
[[nodiscard]] const std::array<Step, kSteps>& steps() noexcept;
[[nodiscard]] const Step& step(int number) noexcept;    // borne a 1..10

[[nodiscard]] bool isCycleScript(const Script&) noexcept;   // l'evenement OnCycle (un script de vue)
[[nodiscard]] int  momentOf(const Script&) noexcept;        // Script::stage borne a 1..9
[[nodiscard]] int  everyOf(const Script&) noexcept;         // Script::everyCycles borne a 1..kMaxEvery
// Un moment, une frequence ou une condition reglees (autre chose que l'OnCycle d'avant).
[[nodiscard]] bool customized(const Script&) noexcept;

// "apr\xC3\xA8s l'\xC3\xA9tape 8 (Formules des objets (fx))" ; "avant" : "avant l'\xC3\xA9tape 9 (Dessin de la vue)".
[[nodiscard]] std::string momentLabel(int moment);
// Le menu Moment : "Apr\xC3\xA8s l'\xC3\xA9tape 8 : Formules des objets (fx)", une ligne par moment (1 a 9) ;
// l'inverse (le texte d'un choix, ou un nombre seul) ; 0 : illisible.
[[nodiscard]] std::vector<std::string> momentChoices();
[[nodiscard]] int momentFromText(std::string_view);
// "Chaque cycle", "1 cycle sur 5" ; le menu Frequence ; l'inverse ("1 cycle sur 7", "7", "chaque cycle") ; 0 : illisible.
[[nodiscard]] std::string everyLabel(int every);
[[nodiscard]] std::vector<std::string> everyChoices();
[[nodiscard]] int everyFromText(std::string_view);
// La condition telle qu'elle est gardee : sans le = de tete, sans blancs autour.
[[nodiscard]] std::string plainCondition(std::string_view);
// Ce que dit la liste des scripts : "apr\xC3\xA8s l'\xC3\xA9tape 6 (Alarmes) \xC2\xB7 1 cycle sur 5 \xC2\xB7 si =Mode_Auto".
[[nodiscard]] std::string summary(const Script&);
// La pastille courte de la liste : "apr\xC3\xA8s 6", "apr\xC3\xA8s 6 \xC2\xB7 1/5", "apr\xC3\xA8s 8 \xC2\xB7 si".
[[nodiscard]] std::string badge(const Script&);
// La ligne pointillee d'un depot : "D\xC3\xA9poser ici : apr\xC3\xA8s 8 Formules, avant 9 Dessin - tes modifications...".
[[nodiscard]] std::string dropText(int moment);

// Les scripts de cycle d'une vue, dans l'ordre ou le moteur les execute (par moment, puis leur rang).
[[nodiscard]] std::vector<const Script*> cycleScripts(const View&);
[[nodiscard]] std::vector<const Script*> atMoment(const View&, int moment);
// Le rang (1..n) d'un script dans son moment, et n. {0, 0} : pas un script de cycle de la vue.
[[nodiscard]] std::pair<int, int> rankOf(const View&, Id script);
// DEPLACER un script de cycle : a `moment` (1..9), au rang `rank` (0 : le premier ; n ou plus : le dernier) parmi
// les autres scripts de ce moment. Le rang, c'est l'ordre des scripts dans la vue : il change en consequence (les
// scripts OnOpen et OnClose gardent leur place). Faux : pas un script de cycle de la vue, ou rien ne change.
bool move(View&, Id script, int moment, std::size_t rank);
// Un nom libre parmi les scripts de la vue ("Cycle_2", "Lire_Consignes_2"...).
[[nodiscard]] std::string freeName(const View&, std::string_view base);
// Le nom qu'un script de cycle porte sans qu'on l'ait choisi (OnCycle, Vue_OnCycle, Vue.OnCycle, Pompe_1.Sym_OnCycle :
// l'evenement au bout) : le moteur ne le cite pas (le journal garde "Vue.OnCycle", comme avant).
[[nodiscard]] bool defaultName(const Script&, std::string_view viewName);

// ---- LA MESURE (le moteur la remplit a chaque cycle ; l'editeur la montre : << derniere simulation >>) ----
struct Timing {
    std::uint64_t cycle{0};                       // le cycle mesure (0 : aucun encore)
    double        periodMs{0};                    // la periode du cycle IHM (Configuration)
    Id            view{kNoId};                    // la vue montree pendant ce cycle
    std::array<double, kSteps + 1> stepMs{};      // [1..9] en ms ; < 0 : pas mesuree (8, 9 : sans ecran)
    std::size_t   writes{0};                      // [10] : les ecritures de ce cycle (variables IHM, automate, equipements)
    [[nodiscard]] bool measured() const noexcept { return cycle > 0; }
};
// "0,40 ms" ; "\xE2\x80\x94" (pas mesure).
[[nodiscard]] std::string msText(double ms);

} // namespace hmi::viewcycle
