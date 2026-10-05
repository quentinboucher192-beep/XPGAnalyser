// =============================================================================
//  app/SimJournal.hpp - lot API 8 : le journal de la simulation
// -----------------------------------------------------------------------------
//  TOUT CE QUI ARRIVE PENDANT UNE SIMULATION, AU MEME ENDROIT ET DANS L'ORDRE :
//  l'automate (demarrage, pause, arret, halte, forcage, fonction non simulee,
//  modification en ligne, point d'arret), l'IHM (demarrage, vue affichee,
//  alarme, utilisateur, erreur de script), les equipements (liaison perdue,
//  retrouvee), le debogage. Chaque evenement est dit en francais, avec ce qu'il
//  veut dire et OU ALLER (une cle, que l'ecran sait suivre).
//
//  SANS ECRAN, SANS SIMULATEUR : un magasin d'evenements. App en tient un
//  (App::simJournal()), qui vit plus longtemps que les onglets ; l'onglet
//  Simulation > Journal le lit, la Vue d'ensemble y prend ses 30 dernieres
//  secondes. Le collecteur (SimCenterFeed, SimCenter.hpp) y ecrit ce qu'il voit
//  passer (les signaux de SimulationHost, l'IHM en marche, les equipements).
//
//  POUR Y ECRIRE (le debogage, un volet, un script) : l'API simple
//
//      app_.simJournal().add(SimSource::Debogage, SimSeverity::Info,
//                            "Point d'arret pose : SFC_PurgeA, ligne 42",
//                            "ligne:SFC_PurgeA:42");
//
//  ou la complete (le genre, l'explication). Les cles "aller a" que l'ecran
//  suit (MainAnalysisScreen::simCenterGo, SimulationWorkspace.cpp) :
//    "ligne:<section>:<n>"   la section, a la ligne n ("Bloc.Section" : un DFB)
//    "variable:<chemin>"     la variable dans l'onglet Automate
//    "vue:<nom>"             cette vue dans l'IHM en marche
//    "ensemble", "automate", "ihm", "equipements", "debogage", "forcages",
//    "courbes", "journal"    un onglet du Centre de simulation
//    "sim.run", "sim.pause", "sim.stop", "sim.step", "relancer" (arreter puis
//    simuler), "bibliotheque" (mettre a jour un bloc), "alarmes" (l'IHM en
//    marche, sur ses alarmes)
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// D'ou vient un evenement. L'ordre est celui des filtres de l'onglet Journal.
enum class SimSource : std::uint8_t { Automate, Ihm, Equipements, Debogage, Simulation };
// Sa gravite : Info (bleu), Ok (vert : ce qui repart), Warning (orange : a
// surveiller), Error (rouge : ce qui arrete ou coupe).
enum class SimSeverity : std::uint8_t { Info, Ok, Warning, Error };

struct SimEvent {
    std::uint64_t id{0};
    double        time{0};               // secondes, l'horloge du journal (now())
    std::string   clock;                 // "14:05:12", l'heure du poste
    std::uint64_t cycle{0};              // le cycle de l'automate a ce moment
    SimSource     source{SimSource::Simulation};
    SimSeverity   severity{SimSeverity::Info};
    std::string   kind;                  // "demarrage", "halte", "forcage", "vue", "alarme"... (voir defaultExplanation)
    std::string   text;                  // ce qui s'est passe
    std::string   explain;               // ce que ca veut dire (vide : celle du genre)
    std::string   go;                    // la cle "aller a" ; vide : nulle part
    std::size_t   repeats{1};            // le meme, repete aussitot : une ligne, "x 3"
};

class SimJournal {
public:
    SimJournal();

    // ---- ecrire -----------------------------------------------------------------
    // L'API SIMPLE : un evenement de plus (son genre se deduit : "debogage" pour
    // le debogage, "info" sinon). Rend son numero.
    std::uint64_t add(SimSource source, SimSeverity severity, std::string text, std::string go = {});
    // La complete. Le meme evenement (source, genre, texte) repete dans les deux
    // secondes ne fait pas une ligne de plus : `repeats` compte. Un arret du
    // debogage ("arret", "point-arret") au meme endroit qu'un autre ecrit juste
    // avant (le collecteur l'a vu passer par SimulationHost::breakHit) le
    // remplace : une ligne, la phrase du dernier.
    std::uint64_t add(SimSource source, SimSeverity severity, std::string kind, std::string text, std::string explain,
                      std::string go);
    void clear();

    // ---- lire -------------------------------------------------------------------
    [[nodiscard]] const std::deque<SimEvent>& events() const noexcept { return events_; }
    [[nodiscard]] std::size_t   size() const noexcept { return events_.size(); }
    [[nodiscard]] bool          empty() const noexcept { return events_.empty(); }
    // Change a chaque ajout (ou repetition) et a chaque effacement.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const SimEvent* find(std::uint64_t id) const;
    // Les evenements des `seconds` dernieres secondes, les plus anciens d'abord.
    [[nodiscard]] std::vector<const SimEvent*> since(double seconds) const;
    // Filtrer : `sources` en masque (bit = 1 << SimSource ; 0 : toutes), la
    // gravite minimale (Info : tout ; Warning : orange et rouge ; Error : rouge),
    // un texte cherche (sans casse, dans le texte, le genre, l'explication).
    // Les plus RECENTS d'abord : c'est ce qu'on lit en premier.
    [[nodiscard]] std::vector<const SimEvent*> filtered(unsigned sources, SimSeverity atLeast, std::string_view search = {}) const;
    [[nodiscard]] std::size_t count(SimSource source) const;
    [[nodiscard]] std::size_t countAtLeast(SimSeverity severity) const;
    [[nodiscard]] static unsigned bit(SimSource s) noexcept { return 1u << static_cast<unsigned>(s); }

    // ---- le contexte ---------------------------------------------------------------
    // Le cycle de l'automate, note sur les evenements qui suivent.
    void setCycle(std::uint64_t cycle) noexcept { cycle_ = cycle; }
    [[nodiscard]] std::uint64_t cycle() const noexcept { return cycle_; }
    // L'horloge (en secondes) et l'heure affichee ; par defaut steady_clock et
    // l'heure locale. Pour les tests : une horloge a la main.
    void setClock(std::function<double()> clock, std::function<std::string()> wallClock = {});
    [[nodiscard]] double now() const;
    // Combien d'evenements sont gardes (2 000 par defaut ; les plus anciens partent).
    void setCapacity(std::size_t n);

    // ---- les mots --------------------------------------------------------------------
    [[nodiscard]] static std::string sourceName(SimSource s);        // "Automate", "IHM", "Equipements"...
    [[nodiscard]] static std::string severityName(SimSeverity s);    // "info", "ok", "a surveiller", "erreur"
    // Ce que veut dire un evenement de ce genre, quand celui qui l'ecrit ne l'a
    // pas dit (le debogage, un script).
    [[nodiscard]] static std::string defaultExplanation(std::string_view kind, SimSource source, SimSeverity severity);
    [[nodiscard]] static std::string explanationOf(const SimEvent& e);
    // La cle "aller a" d'une ligne d'une section : "ligne:<section>:<n>".
    [[nodiscard]] static std::string goLine(const std::string& section, int line);
    // Une ligne : "14:05:12  cycle 1 234  Automate  erreur  Halte : ..." (les scripts).
    [[nodiscard]] static std::string line(const SimEvent& e);
    // Tout le journal en CSV (separateur ;, les plus anciens d'abord).
    [[nodiscard]] std::string csv() const;

private:
    std::deque<SimEvent>          events_;
    std::size_t                   capacity_{2000};
    std::uint64_t                 nextId_{1};
    std::uint64_t                 revision_{0};
    std::uint64_t                 cycle_{0};
    std::function<double()>       clock_;
    std::function<std::string()>  wall_;
};

} // namespace app
