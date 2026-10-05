// =============================================================================
//  app/SimStatus.hpp - lot API 8 : l'etat de la simulation, dit en clair
// -----------------------------------------------------------------------------
//  LE CALCUL DE LA VUE D'ENSEMBLE, SANS ECRAN. L'ecran photographie ce qu'il
//  sait (Snapshot : l'automate, l'IHM, les equipements) ; compute() en fait ce
//  que la Vue d'ensemble montre :
//
//   * LE BANDEAU, une phrase en francais clair et sa couleur - vert << Tout
//     tourne >>, orange << A surveiller >>, rouge << L'automate est arrete >>,
//     bleu (le programme a change en ligne, un point d'arret), gris (rien ne
//     tourne) -, ce que ca veut dire, et LE bouton qui repare ;
//   * TROIS CARTES (automate, IHM, equipements) : un voyant, une phrase, trois
//     chiffres, leurs boutons ;
//   * LA CHAINE equipements <-> automate <-> IHM : ce qui passe, ce qui est coupe ;
//   * CE QUI MERITE TON ATTENTION : une liste courte, la plus grave d'abord,
//     chaque ligne avec son bouton ;
//   * la pastille du dossier Simulation de l'arbre.
//
//  Tout est pur (des chaines et des nombres) : simstatus_test le verifie cas par
//  cas, sans simulateur. Les boutons portent une CLE (celles de SimJournal.hpp,
//  que MainAnalysisScreen::simCenterGo sait suivre).
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace app::simstatus {

// La couleur d'un etat : gris (rien ne tourne), vert, orange, rouge, bleu.
enum class Tone : std::uint8_t { Off, Ok, Warning, Error, Info };

// ------------------------------------------------------------------ l'entree ----
struct PlcFacts {
    enum class State : std::uint8_t { Stopped, Running, Paused, Halted };
    bool          prepared{false};        // un programme est pret (prepare)
    State         state{State::Stopped};
    std::string   prepareError;           // la preparation a echoue (en francais) ; vide sinon
    std::uint64_t cycle{0};
    std::int64_t  clockMs{0};             // le temps simule
    std::int64_t  scanMicros{0};          // le calcul du dernier cycle
    std::int64_t  periodMs{20};           // la periode de MAST (a defaut : celle de la simulation)
    int           speed{1};               // 1, 10 ; 0 : au plus vite
    std::size_t   entries{0}, sections{0};// ce que MAST execute
    // La halte : la cause en francais, l'endroit, le bloc en cause et sa
    // version plus recente en bibliotheque ; une boucle sans fin (la limite du
    // cycle en ms, a dire : "le cycle a depasse 1,5 s").
    std::string   haltReason;
    std::string   haltSection;
    std::uint32_t haltLine{0};
    std::string   haltBlock, haltNewer;
    bool          haltLoop{false};
    std::int64_t  haltLimitMs{1500};
    std::string   haltLimitText;          // non vide : ce qu'a depasse le cycle, dit autrement ("2 000 000 instructions")
    // Les fonctions que le simulateur ne connait pas : elles rendent 0 (ou
    // arretent le cycle, selon la politique).
    struct Unknown { std::string name; std::uint64_t calls{0}; std::string section; std::uint32_t line{0}; };
    std::vector<Unknown> unknown;
    bool          continueUnknown{true};
    // Les forcages : combien, et le plus ancien (depuis combien de secondes).
    std::size_t   forced{0};
    std::string   oldestForced;
    double        oldestForcedSeconds{0};
    // Le programme a change depuis la preparation (la simulation tourne sur l'ancien).
    bool          stale{false};
    // La derniere modification en ligne, si elle est recente (le moteur).
    bool          online{false}, onlineFailed{false};
    std::string   onlineSummary;
    double        onlineAgeSeconds{0};
    // En pause sur un point d'arret (le moteur).
    bool          breakHit{false};
    std::string   breakSection;
    int           breakLine{0};
    std::size_t   breakpoints{0};
    // La section la plus lourde du dernier cycle (le moteur ; vide : inconnue).
    std::string   heaviestSection;
    std::int64_t  heaviestMicros{0};
    // 1.10.2 : les sections qui n'ont pas tourne au dernier cycle (leur
    // condition d'activation etait fausse) ; la premiere, et sa condition.
    std::size_t   inactiveSections{0};
    std::string   inactiveSection;
    std::string   inactiveCondition;
};

struct HmiFacts {
    bool          exists{false};          // le projet a une IHM (au moins une vue)
    bool          running{false};         // la simulation de l'IHM tourne
    std::string   view;                   // la vue affichee
    std::string   user;                   // l'utilisateur connecte ; vide : personne
    std::size_t   alarms{0};              // alarmes dans la liste (actives ou pas acquittees)
    std::size_t   activeAlarms{0};
    std::size_t   unacked{0};
    std::string   topAlarm;               // la plus prioritaire (son message)
    int           topPriority{0};
    std::size_t   plcReads{0};            // les variables de l'automate que l'IHM lit
    std::size_t   scriptErrors{0};
    std::string   lastScriptError;
};

struct EquipFacts {
    struct One {
        std::string name;
        bool        enabled{true};
        bool        simulated{false};     // seulement simule, ou parle a son jumeau
        bool        ok{false};            // repond (connecte, joignable, jumeau en marche)
        bool        tested{false};        // au moins un essai a eu lieu
        std::string state;                // "Connecte", "Injoignable"...
        std::string why;                  // pourquoi il ne repond pas
    };
    std::vector<One> list;
    double           exchangesPerSecond{0};   // requetes Modbus par seconde (liaisons et jumeaux)
};

struct Snapshot {
    bool       project{false};            // un projet est ouvert
    PlcFacts   plc;
    HmiFacts   hmi;
    EquipFacts equip;
};

// ------------------------------------------------------------------ la sortie ----
// Un bouton : son libelle et sa cle (vide : pas de bouton).
struct Action {
    std::string label, key;
    [[nodiscard]] bool empty() const noexcept { return key.empty(); }
};

struct Banner {
    Tone        tone{Tone::Off};
    std::string word;                     // "TOUT TOURNE", "A SURVEILLER", "ARRETE", "EN PAUSE"...
    std::string title;                    // la phrase en francais clair
    std::string meaning;                  // << Ce que ca veut dire : ... >> (sans le prefixe)
    Action      fix;                      // LE bouton qui repare
    Action      second;                   // un deuxieme, facultatif
};

struct Figure {
    std::string label, value;
    std::string detail;                   // sous la valeur, en petit ("sur 20 ms")
    double      bar{-1};                  // 0..1 : une petite barre ; < 0 : pas de barre
    Tone        tone{Tone::Off};          // Off : la couleur du texte
};

struct Card {
    std::string         key;              // "automate", "ihm", "equipements"
    std::string         title;            // "AUTOMATE"
    Tone                tone{Tone::Off};  // le voyant
    std::string         state;            // "en marche", "arretee"...
    std::string         sentence;
    std::array<Figure, 3> figures;
    std::vector<Action> buttons;
    std::string         open;             // la cle d'un clic sur la carte
};

struct Link {
    std::string from, to;                 // "equipements", "automate", "ihm"
    bool        alive{false};             // ca circule (la fleche vit)
    bool        broken{false};            // coupee (rouge)
    bool        idle{false};              // rien a relier (grise, en pointilles)
    std::string text;                     // "64 echanges/s", "37 variables lues"
    std::string why;                      // coupee, ou au repos : pourquoi
};

struct Attention {
    std::string id;                       // "halte", "inconnues", "cycle-lent", "forcages", "alarmes", "equipement:<nom>"...
    Tone        tone{Tone::Warning};
    int         weight{0};                // plus grand : plus grave (le tri)
    std::string text, detail;
    Action      action;
    Action      second;                   // un deuxieme bouton, comme la maquette (facultatif)
};

struct Report {
    Banner                 banner;
    std::array<Card, 3>    cards;         // automate, IHM, equipements
    std::array<Link, 2>    chain;         // equipements <-> automate, automate <-> IHM
    std::vector<Attention> attention;     // la plus grave d'abord ; vide : rien a signaler
    // La pastille du dossier Simulation (et son ton) : "en marche", "en pause",
    // "halte", "arretee" ; celle de l'IHM ("en marche" ou vide) et des
    // equipements ("3", "1 KO" ou vide).
    std::string            folderBadge;
    Tone                   folderTone{Tone::Off};
    std::string            hmiBadge;
    std::string            equipBadge;
    Tone                   equipTone{Tone::Off};
};

[[nodiscard]] Report compute(const Snapshot& s);

// ------------------------------------------------------------------ les mots ----
// "12 480" (espace fine insecable) ; "1,5 s", "140 ms", "2,3 ms" ; "2 min 30 s".
[[nodiscard]] std::string thousands(std::uint64_t n);
[[nodiscard]] std::string milliseconds(double ms);
[[nodiscard]] std::string duration(double seconds);
[[nodiscard]] std::string plural(std::size_t n, const std::string& one, const std::string& many);
[[nodiscard]] std::string toneName(Tone t);        // "gris", "vert", "orange", "rouge", "bleu"
// Le bandeau en une ligne (etat-bandeau des scripts) : "[orange] A surveiller : ... | Ce que ca veut dire : ... | Bouton : ..."
[[nodiscard]] std::string bannerLine(const Report& r);

} // namespace app::simstatus
