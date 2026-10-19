// =============================================================================
//  hmi/HmiPerfHistory.hpp - 1.12.17 : LES PERFORMANCES DANS LE TEMPS, LES LENTEURS
// -----------------------------------------------------------------------------
//  LES PERFORMANCES DANS LE TEMPS. Le moteur garde une mesure par seconde : la
//  mediane, le p95 et le maximum des cycles IHM de la seconde, le dessin et les
//  expressions (l'ecran), les ecritures, les echanges avec les equipements, les
//  depassements et les lenteurs. Une heure a la seconde ; au-dela, une mesure
//  toutes les 10 s jusqu'a 8 h. Les POSTES du cycle (chaque script, les alarmes,
//  les courbes et archives, le reste) disent ou passe le temps. La Vue d'ensemble
//  de la Simulation le dessine ; rien n'est ecrit dans le projet.
//
//  LES LENTEURS. Ce qui a dure anormalement longtemps : plus de 50 ms, ou plus de
//  5 fois son temps habituel (la mediane de ses 100 dernieres fois) et au moins
//  10 ms ; un cycle plus long que sa periode, toujours. Un clic et ce qu'il
//  declenche, un script, l'affichage d'une vue, l'ouverture d'une popup, un
//  echange lent avec un equipement. Pour chacune : l'heure a la milliseconde, la
//  duree, son temps habituel, ou, qui, et ce qui a pris le temps dedans. Les 500
//  dernieres, d'une marche a l'autre, jusqu'a Vider ou la fermeture du projet.
//
//  Sans ecran, sans horloge : le moteur (Runtime) les remplit ; les essais aussi.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hmi {

// ---- le temps habituel -------------------------------------------------------------
// Par poste (une action, un script, une vue, une popup...) : la mediane de ses 100
// dernieres fois ; combien de fois depuis le lancement, combien lentes.
class UsualTimes {
public:
    static constexpr std::size_t kKeep = 100;
    // Le temps habituel ; 0 : moins de 3 fois (on ne sait pas encore).
    [[nodiscard]] double      usual(const std::string& key) const;
    [[nodiscard]] std::size_t runs(const std::string& key) const;
    [[nodiscard]] std::size_t slowRuns(const std::string& key) const;
    void add(const std::string& key, double ms, bool slow);
    void clear() { map_.clear(); }

private:
    struct Ring {
        std::array<float, kKeep> v{};
        std::size_t              n{0}, at{0}, runs{0}, slow{0};
    };
    std::unordered_map<std::string, Ring> map_;
};

// ---- quand c'est une lenteur ----------------------------------------------------------
struct SlowRules {
    double absoluteMs{50.0};   // plus de 50 ms...
    double factor{5.0};        // ... ou plus de 5 fois son temps habituel...
    double floorMs{10.0};      // ... et au moins 10 ms
    [[nodiscard]] bool slow(double ms, double usual) const noexcept {
        return ms > absoluteMs || (usual > 0.0 && ms > factor * usual && ms >= floorMs);
    }
};

enum class SlowKind : std::uint8_t { Action, Script, View, Popup, Cycle, Equipment };
inline constexpr std::size_t kSlowKinds = 6;
// "Action", "Script", "Vue", "Popup", "Cycle", "Equipement".
[[nodiscard]] std::string slowKindName(SlowKind) noexcept;

// Ce qui a pris le temps dedans : "Ouvrir Pop_Reglage", "Script d'ouverture"...
struct SlowPart {
    std::string what;
    double      ms{0};
    int         depth{0};     // 0 : directement dedans ; 1 : dans la ligne d'au-dessus
};

struct SlowEntry {
    std::uint64_t         serial{0};     // son numero : une cle stable ("lenteur:<n>")
    double                at{0};         // l'heure de marche (s)
    std::string           clock;         // "14:08:55.204" (l'heure du poste)
    double                ms{0};         // la duree
    double                usual{0};      // son temps habituel ; 0 : inconnu
    double                period{0};     // un cycle : sa periode
    SlowKind              kind{SlowKind::Action};
    std::string           what;          // "Clic sur Btn_Regler -> ouvrir Pop_Reglage"
    std::string           where;         // "Vue_Four", "IHM", "Automate_1"
    std::string           user;          // l'utilisateur connecte ; vide : personne
    std::string           view;          // la vue montree
    std::vector<SlowPart> inside;
    std::size_t           runs{0}, slowRuns{0};   // "1 fois plus lente sur 42"
    // Aller a : la vue et l'objet, le script, l'equipement.
    Id                    viewId{kNoId}, objectId{kNoId}, scriptId{kNoId};
    std::string           equipment;
    [[nodiscard]] double ratio() const noexcept { return usual > 0.0 ? ms / usual : 0.0; }
};

class SlowLog {
public:
    static constexpr std::size_t kKeep = 500;
    const SlowEntry& add(SlowEntry e);           // lui donne son numero ; le plus ancien part au-dela de 500
    void clear();
    [[nodiscard]] const std::deque<SlowEntry>& entries() const noexcept { return entries_; }   // les plus anciennes d'abord
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::uint64_t lastSerial() const noexcept { return serial_; }
    [[nodiscard]] const SlowEntry* find(std::uint64_t serial) const;
    [[nodiscard]] std::size_t count(SlowKind) const;
    // L'export : heure;duree (ms);habituel (ms);rapport;genre;quoi;ou;utilisateur;dedans
    [[nodiscard]] std::string csv() const;

private:
    std::deque<SlowEntry> entries_;
    std::uint64_t         serial_{0}, revision_{0};
};

// ---- les performances dans le temps -------------------------------------------------
struct PerfSecond {
    double        t{0};                 // son debut (s de marche)
    float         span{1};              // 1 s ; 10 s au-dela d'une heure
    float         median{0}, p95{0}, max{0}, mean{0};   // le cycle (ms)
    double        maxAt{0};             // l'heure du maximum
    float         paint{0}, evaluate{0};   // l'ecran (ms, la moyenne)
    std::uint32_t cycles{0}, overruns{0}, slow{0}, writes{0}, exchanges{0}, expressions{0};
    float         exchangeMs{0};        // la reponse des equipements (ms)
};

class PerfHistory {
public:
    static constexpr double kFineSeconds = 3600.0;        // une mesure par seconde, une heure
    static constexpr double kCoarseSeconds = 8 * 3600.0;  // une par 10 s, jusqu'a 8 h
    void reset(double now);
    void setPeriod(double ms) { if (ms > 0) periodMs_ = ms; }
    void cycle(double now, double ms, double periodMs);
    // Un poste du cycle : "script:12" ("Script Chauffer (Vue_Four, a chaque cycle)"),
    // "alarmes", "courbes", "reste".
    void post(const std::string& key, const std::string& label, double ms);
    void screen(double paintMs, double evaluateMs, std::size_t expressions);
    void write() { ++open_.writes; }
    void exchanges(std::uint32_t n, double responseMs);
    void slow() { ++open_.slow; }
    // Ferme les secondes passees (le moteur l'appelle a chaque tour).
    void advance(double now);

    [[nodiscard]] double                    since() const noexcept { return since_; }
    // L'heure du poste d'un instant de marche : "14:07:40" (millis : "14:07:40.118").
    [[nodiscard]] std::string               clockAt(double t, bool millis = false) const;
    [[nodiscard]] double                    periodMs() const noexcept { return periodMs_; }
    [[nodiscard]] std::uint64_t             revision() const noexcept { return revision_; }
    [[nodiscard]] const std::deque<PerfSecond>& seconds() const noexcept { return seconds_; }   // les plus anciennes d'abord
    // Les mesures d'une periode (secondes ; <= 0 : depuis le lancement).
    [[nodiscard]] std::vector<const PerfSecond*> window(double now, double seconds) const;

    struct Summary {
        std::size_t cycles{0}, overruns{0}, slow{0}, writes{0}, exchanges{0};
        double      mean{0}, p95{0}, max{0}, maxAt{0};
        double      load{0}, worstLoad{0}, worstAt{0};      // la charge de la periode (0..1+)
        double      paint{0}, paintP95{0}, evaluate{0};
        std::size_t expressions{0};
        double      writesPerS{0}, exchangesPerS{0}, exchangeMs{0};
        double      span{0};                                 // les secondes couvertes
    };
    [[nodiscard]] Summary summary(double now, double seconds) const;

    struct PostRow {
        std::string        key, label;
        double             share{0};      // sa part du temps de cycle
        std::size_t        times{0};
        double             mean{0}, p95{0}, max{0};
        std::vector<float> trend;         // sa moyenne par tranche (60 au plus)
    };
    [[nodiscard]] std::vector<PostRow> posts(double now, double seconds) const;
    // L'export : une ligne par mesure.
    [[nodiscard]] std::string csv() const;

private:
    struct Open {
        double             t{-1};
        std::vector<float> cycles;
        double             maxAt{0};
        double             paintSum{0}, evalSum{0};
        std::uint32_t      paintN{0}, evalN{0};
        std::uint32_t      overruns{0}, slow{0}, writes{0}, exchanges{0}, expressions{0};
        double             exchangeMsSum{0};
        std::uint32_t      exchangeMsN{0};
    };
    struct PostRun {
        float t, ms;
    };
    struct Post {
        std::string         label;
        std::deque<PostRun> runs;     // la derniere heure
        double              olderTotal{0};
        std::size_t         olderTimes{0};
        float               olderMax{0};
    };
    void close(double upTo);
    void squeeze();

    std::deque<PerfSecond>               seconds_;
    std::deque<std::pair<float, float>>  raw_;       // (t, ms) : les cycles de la derniere heure
    std::unordered_map<std::string, Post> posts_;
    std::vector<std::string>             postOrder_;
    Open                                 open_;
    double                               since_{0}, periodMs_{100};
    double                               wallSince_{0};      // l'heure du poste au lancement (s depuis 1970)
    std::uint64_t                        revision_{0};
};

// "4,2 ms", "112 ms", "1,2 s".
[[nodiscard]] std::string slowMs(double ms);
// "14:08:55.204" : l'heure du poste, a la milliseconde.
[[nodiscard]] std::string wallClockMs();

} // namespace hmi
