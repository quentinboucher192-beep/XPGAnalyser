// =============================================================================
//  app/hmi/HmiConsole.hpp - 1.11.14 : la Console du panneau du bas (ses lignes)
// -----------------------------------------------------------------------------
//  CE QUE DIT LA SIMULATION DE L'IHM, ligne a ligne : les appels a IHM_LOG, les
//  erreurs d'execution, les actions et la navigation, ce que dit le moteur. Chaque
//  ligne garde son heure, son niveau (TRACE a CRITICAL), sa categorie, sa source
//  (le script, la fonction, la vue, l'objet) et la ligne du code, le cycle et la
//  session (le numero du demarrage de la simulation) : de quoi la retrouver et
//  aller a sa source d'un double-clic.
//    LA CONSERVATION est reglable (500 a 100 000 lignes) : au-dela, les plus
//    anciennes tombent (le compte de celles qui sont tombees est garde).
//    LES FILTRES (niveaux coches, texte cherche) servent a l'ecran, a la copie et
//    a l'export (texte ou CSV, l'en-tete dit la session et le filtre).
//  Rien ici ne dessine : le volet (HmiBuildOutputPane) lit, les essais aussi.
// =============================================================================
#pragma once

#include "../../hmi/HmiLog.hpp"
#include "../../hmi/HmiModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace hmi { struct JournalEntry; }

namespace app {

struct ConsoleEntry {
    std::uint64_t seq{0};            // le rang depuis l'ouverture (1, 2, ...)
    std::string   date;              // "2026-10-08"
    std::string   time;              // "14:05:12.350"
    hmi::LogLevel level{hmi::LogLevel::Info};
    std::string   category;          // "IHM_LOG", "Erreur", "Navigation", "Simulation"...
    std::string   source;            // "script Horloge", "Vue_A/Btn_Marche"
    std::string   code;              // le script ou la fonction ("Horloge", "Vue_A.OnOpen")
    std::string   message;
    int           line{0};           // la ligne du code (0 : inconnue)
    long long     cycle{0};
    int           session{0};
    hmi::Id       view{hmi::kNoId}, object{hmi::kNoId}, script{hmi::kNoId}, function{hmi::kNoId};
    // On peut y aller (un script, une fonction, un objet, une vue).
    [[nodiscard]] bool hasSource() const noexcept {
        return script != hmi::kNoId || function != hmi::kNoId || object != hmi::kNoId || view != hmi::kNoId;
    }
    // "script Horloge, ligne 3" ; "Vue_A/Btn_Marche" ; "" (rien).
    [[nodiscard]] std::string where() const;
};

class HmiConsole {
public:
    static constexpr std::size_t kDefaultRetention = 5000;
    static constexpr std::size_t kMinRetention = 500;
    static constexpr std::size_t kMaxRetention = 100000;

    struct Filter {
        std::array<bool, hmi::kLogLevelCount> levels{true, true, true, true, true, true, true};
        std::string search;              // sans casse : message, source, code, categorie
    };

    // Une ligne du moteur IHM (le crochet journaled) ; la date est celle du poste.
    void addRuntime(const hmi::JournalEntry& e);
    void add(ConsoleEntry e);
    void clear();
    void setRetention(std::size_t lines);
    [[nodiscard]] std::size_t retention() const noexcept { return retention_; }
    // La conservation suivante (500, 1 000, 5 000, 20 000, 100 000, puis 500).
    [[nodiscard]] static std::size_t nextRetention(std::size_t current) noexcept;

    [[nodiscard]] const std::deque<ConsoleEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] int count(hmi::LogLevel level) const noexcept { return counts_[static_cast<std::size_t>(level)]; }
    [[nodiscard]] int errors() const noexcept;      // ERROR et CRITICAL
    [[nodiscard]] int warnings() const noexcept { return count(hmi::LogLevel::Warning); }
    [[nodiscard]] int session() const noexcept { return session_; }      // la derniere vue (0 : aucune)
    [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }

    [[nodiscard]] static bool matches(const ConsoleEntry&, const Filter&);
    // Une ligne de texte : "14:05:12.350  WARNING   script Horloge:3   Temperature : 21.5"
    [[nodiscard]] static std::string lineOf(const ConsoleEntry&);
    // L'export : un en-tete (la date, la session, le filtre), puis une ligne par entree.
    [[nodiscard]] std::string exportText(const Filter&) const;
    // CSV (point-virgule, comme Excel en francais) : Date;Heure;Niveau;Categorie;Source;Ligne;Session;Cycle;Message.
    [[nodiscard]] std::string exportCsv(const Filter&) const;

private:
    void drop();
    std::deque<ConsoleEntry>                    entries_;
    std::array<int, hmi::kLogLevelCount>        counts_{};
    std::size_t                                 retention_{kDefaultRetention};
    std::size_t                                 dropped_{0};
    std::uint64_t                               revision_{0}, seq_{0};
    int                                         session_{0};
};

} // namespace app
