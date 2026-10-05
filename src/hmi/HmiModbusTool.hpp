// =============================================================================
//  hmi/HmiModbusTool.hpp - l'outil Modbus (lot 15), a la facon de Modbus Doctor
// -----------------------------------------------------------------------------
//  POUR METTRE AU POINT UNE LIAISON sans l'IHM : lire et ecrire un equipement
//  (fonctions 1 a 6, 15, 16, 43), le lire en boucle et tracer ses valeurs,
//  envoyer une trame telle quelle et voir ce qui revient. L'espion (HmiSpy)
//  montre les trames de toutes les liaisons.
//
//  LES VALEURS d'un registre se lisent en decimal (signe ou non), hexa,
//  binaire, ASCII ; deux registres en 32 bits (entier signe ou non, flottant),
//  poids faible ou fort d'abord.
//
//  LA LECTURE CYCLIQUE (Poller) tourne sur son propre fil : chaque periode, la
//  requete, son resultat, le temps de reponse ; l'historique des valeurs pour
//  le graphique (le dernier quart d'heure), et l'export CSV.
// =============================================================================
#pragma once

#include "HmiModbus.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hmi::mbtool {

// La cible : un equipement, ou une adresse tapee.
struct Target {
    std::string host{"127.0.0.1"};
    int         port{502};
    int         unit{1};
    int         timeoutMs{1000};
    bool operator==(const Target&) const = default;
};

// Une requete de l'outil. Adresse a partir de 0 (le registre 40001 est l'adresse 0).
struct Query {
    int                        function{3};     // 1, 2, 3, 4 (lire) ; 5, 6, 15, 16 (ecrire) ; 43 (identification)
    int                        address{0};
    int                        count{10};
    std::vector<std::uint16_t> registers;       // 6 (le premier), 16
    std::vector<bool>          bits;            // 5 (le premier), 15
    bool operator==(const Query&) const = default;
};
// "lire des mots (3)" ; faux si la requete est impossible (why).
bool validQuery(const Query&, std::string* why = nullptr);

struct Reply {
    bool                       ok{false};
    int                        exception{0};
    bool                       timeout{false};
    double                     ms{0};
    std::string                why;
    std::vector<std::uint16_t> registers;
    std::vector<bool>          bits;
    modbus::Identification     objects;         // 43
    modbus::Frame              request, response;
};

// Un client de l'outil : se connecte a la premiere requete, se reconnecte apres
// une coupure. Synchrone - a appeler hors de l'ecran (un fil).
class Session {
public:
    Session() = default;
    explicit Session(Target t) : target_(std::move(t)) {}
    void setTarget(Target t);
    [[nodiscard]] const Target& target() const noexcept { return target_; }
    Reply run(const Query&);
    // Une trame telle quelle (l'en-tete MBAP compris).
    Reply raw(const modbus::Frame&);
    void close();
    [[nodiscard]] bool connected() const noexcept { return client_.connected(); }

private:
    bool ensure(Reply& r);
    Target         target_;
    modbus::Client client_;
};

// ------------------------------------------------------------------ trames ---
// "00 01 00 00 00 06 01 03 00 00 00 0A"
[[nodiscard]] std::string hex(const std::vector<std::uint8_t>& bytes);
// "00 01 00 00 00 06 01 03 00 00 00 0A", "000100000006010300" : faux si illisible.
bool parseHex(std::string_view text, std::vector<std::uint8_t>& out, std::string* why = nullptr);
// La trame d'une requete (transaction, esclave).
[[nodiscard]] modbus::Frame buildFrame(const Query&, int unit, std::uint16_t transaction);
// Une trame en clair : "transaction 1, esclave 1 : lire des mots (3), adresse 0, 10 mots".
// `request` : une requete (sinon une reponse) ; `asked` : la requete d'une reponse (0 : inconnue).
[[nodiscard]] std::string describe(const std::vector<std::uint8_t>& adu, bool request, int askedFunction = 0);

// ------------------------------------------------------------------ valeurs ---
enum class Format : std::uint8_t { Unsigned, Signed, Hex, Binary, Ascii, Unsigned32, Signed32, Float32 };
[[nodiscard]] const std::vector<std::string>& formatLabels();     // dans l'ordre de l'enum
[[nodiscard]] Format formatFrom(std::string_view label) noexcept;
[[nodiscard]] bool   wide(Format f) noexcept;                     // deux registres
// Le texte d'un registre (et du suivant, pour un format 32 bits ; lowFirst :
// poids faible d'abord).
[[nodiscard]] std::string formatValue(const std::vector<std::uint16_t>& regs, std::size_t i, Format f, bool lowFirst);
// Un nombre tape -> les registres a ecrire (un, ou deux pour un format 32 bits).
bool parseValue(std::string_view text, Format f, bool lowFirst, std::vector<std::uint16_t>& out, std::string* why = nullptr);
[[nodiscard]] double numericValue(const std::vector<std::uint16_t>& regs, std::size_t i, Format f, bool lowFirst);

// ------------------------------------------------------------ lecture cyclique ---
class Poller {
public:
    Poller() = default;
    ~Poller();
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    void start(Target, Query, int periodMs);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    struct Sample {
        double                     t{0};        // secondes (horloge murale, depuis 1970)
        bool                       ok{false};
        double                     ms{0};
        std::vector<std::uint16_t> registers;
        std::vector<bool>          bits;
    };
    struct Stats {
        std::uint64_t requests{0}, errors{0};
        double        lastMs{0}, avgMs{0}, maxMs{0};
        std::string   lastError;
    };
    [[nodiscard]] Stats  stats() const;
    [[nodiscard]] Sample last() const;
    // Les echantillons des `seconds` dernieres secondes (le graphique).
    [[nodiscard]] std::vector<Sample> history(double seconds) const;
    void clear();
    [[nodiscard]] Query  query() const;
    [[nodiscard]] Target target() const;
    // Le releve en CSV : l'heure, la duree, puis une colonne par registre (ou bit).
    [[nodiscard]] std::string csv(Format f, bool lowFirst) const;

private:
    void loop();
    mutable std::mutex   mutex_;
    std::thread          thread_;
    std::atomic<bool>    running_{false};
    std::atomic<bool>    stop_{false};
    Target               target_;
    Query                query_;
    int                  periodMs_{1000};
    std::deque<Sample>   samples_;
    Stats                stats_;
    double               sumMs_{0};
};

} // namespace hmi::mbtool
