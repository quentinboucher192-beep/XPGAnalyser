// =============================================================================
//  hmi/HmiIncident.hpp - 1.20.0 : LES INCIDENTS (la partie sans ecran)
// -----------------------------------------------------------------------------
//  Quand une alarme apparait, l'IHM ne se contente plus de le noter : elle garde
//  un DOSSIER D'INCIDENT qui dit a quoi l'alarme est reliee dans le projet et ce
//  qui s'est passe autour. Ici, ce qui ne depend ni de l'ecran ni des fichiers :
//
//    le detecteur    les FRONTS seulement (apparition, maintien au-dela d'un
//                    temps, disparition, reapparition apres disparition) - jamais
//                    un incident a chaque cycle tant que l'alarme reste active ;
//                    les defauts de communication et d'equipement (injoignable,
//                    retrouve) ; les alarmes surveillees ou non (reglage) ;
//    le contexte     au moment du defaut, COPIE : l'alarme (sa condition, sa
//                    priorite, son delai...), la chaine de dependances lue dans le
//                    graphe des relations (hmi::rel) - condition -> variables ->
//                    structures -> equipement et adresse -> scripts et fonctions
//                    qui les ecrivent ou les lisent -> vues et objets -> autres
//                    alarmes -, les valeurs disponibles (une valeur absente est dite
//                    absente, jamais inventee), les alarmes actives, la chronologie ;
//    l'analyse       deterministe : les alarmes liees (une dependance commune,
//                    simultanees, potentiellement secondaires, independantes), des
//                    PISTES classees avec leur niveau de certitude (un fait verifie,
//                    une relation indirecte, une hypothese) - l'ordre d'apparition
//                    n'est jamais une preuve de causalite ;
//    la fiche        le diagnostic en dix sections, en francais.
//
//  XPGAnalyser n'est pas un systeme de securite : ceci aide a COMPRENDRE un
//  evenement, ce n'est pas une surveillance deterministe de la logique automate.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiRuntime.hpp"   // AlarmNotice, EquipmentStatus

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::rel { class Graph; }

namespace hmi::incident {

// ---- les evenements ----------------------------------------------------------------------------------------------
enum class EventKind : std::uint8_t { Appear, Reappear, Held, Ack, Clear, CommLost, CommBack, Technical };
// "Apparition", "R\xC3\xA9" "apparition", "Maintien", "Acquittement", "Disparition", "Perte de communication",
// "Communication retrouv\xC3\xA9" "e", "\xC3\x89v\xC3\xA9nement technique".
[[nodiscard]] std::string_view eventLabel(EventKind) noexcept;
[[nodiscard]] std::string_view eventKey(EventKind) noexcept;            // "apparition"... (le fichier)
[[nodiscard]] EventKind        eventFromKey(std::string_view) noexcept;

struct Event {
    EventKind   kind{EventKind::Appear};
    std::string name;          // l'alarme, ou l'equipement (CommLost, CommBack)
    Id          alarm{kNoId};
    std::string at;            // "2026-10-31 17:40:12.350" (l'horodatage du moteur)
    double      t{0.0};        // secondes (l'horloge du moteur) : les ecarts entre evenements
    std::string detail;        // le message, l'etat, pourquoi
    int         priority{3};
};

struct Settings {
    bool                     enabled{true};
    std::vector<std::string> watched;          // vide : toutes les alarmes ; sinon seulement celles-ci (sans casse, * permis)
    std::vector<std::string> ignored;          // jamais celles-ci (* permis)
    bool                     communication{true};   // un defaut de communication fait aussi un incident
    double                   heldAfter{30.0};  // le << maintien >> est dit une fois, apres ce temps (0 : jamais)
    double                   debounce{5.0};    // une reapparition dans ce delai : le meme incident (pas un nouveau)
    double                   before{10.0};     // les secondes gardees AVANT le defaut (images, chronologie)
    double                   after{5.0};       // ... et APRES
    double                   timeline{120.0};  // la fenetre de la chronologie
    int                      framesPerSecond{2};
    std::size_t              maxPending{200};  // au-dela, les plus anciens incidents en attente non commentes sont dits
};
// Une alarme est-elle surveillee ?
[[nodiscard]] bool watched(const Settings&, std::string_view alarm);

// LE DETECTEUR : il recoit les avis du moteur (les fronts) et l'etat des equipements ; il rend les evenements et ceux
// qui demandent un incident. Il ne lit jamais le projet : rien de lourd a chaque cycle.
class Monitor {
public:
    explicit Monitor(Settings s = {});
    void setSettings(Settings s) { settings_ = std::move(s); }
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }

    void notice(const AlarmNotice&, double t);                         // un avis d'alarme du moteur
    void equipments(const std::vector<EquipmentStatus>&, double t);    // l'etat des equipements (les fronts)
    void technical(std::string name, std::string detail, double t, std::string at = {});   // un evenement configurable
    void tick(double t);                                               // le maintien

    [[nodiscard]] std::vector<Event> take();          // les evenements depuis le dernier appel
    [[nodiscard]] std::vector<Event> takeTriggers();  // ceux qui demandent un incident
    [[nodiscard]] std::vector<Event> timeline(double from, double to) const;   // la chronologie gardee
    [[nodiscard]] std::vector<std::string> active() const;                      // les alarmes actives (connues ici)
    [[nodiscard]] std::size_t occurrences(std::string_view alarm) const;        // ses apparitions vues (reapparitions comprises)
    void clear();

private:
    struct State {
        bool        active{false};
        double      since{0}, cleared{-1}, lastTrigger{-1e9};
        bool        heldSaid{false};
        std::size_t seen{0};
        std::string at;
        Id          alarm{kNoId};
        int         priority{3};
    };
    void push(Event e, bool trigger);
    Settings                     settings_;
    std::map<std::string, State> alarms_;      // par nom en majuscules
    std::map<std::string, bool>  reachable_;   // par equipement
    std::deque<Event>            history_;
    std::vector<Event>           fresh_, triggers_;
};

// ---- le contexte ---------------------------------------------------------------------------------------------------
enum class Certainty : std::uint8_t { Fact, Indirect, Hypothesis };
// "fait v\xC3\xA9rifi\xC3\xA9", "relation indirecte", "hypoth\xC3\xA8se".
[[nodiscard]] std::string_view certaintyLabel(Certainty) noexcept;
[[nodiscard]] std::string_view certaintyKey(Certainty) noexcept;
[[nodiscard]] Certainty        certaintyFromKey(std::string_view) noexcept;

// Un maillon de la chaine : "variable Pompe_1 (T_Pompe)" <- "lue par la condition" ; sa profondeur (1 : direct).
struct Link {
    std::string kind;          // "variable", "structure", "equipement", "adresse", "script", "fonction", "vue", "objet", "alarme"
    std::string name;
    std::string relation;      // "lue par la condition", "\xC3\xA9" "crite par", "li\xC3\xA9" "e \xC3\xA0"...
    std::string where;         // "script Calcul_Niveau, ligne 3", "Vue_Station . Pompe_1 . value"
    std::string detail;        // "Modbus TCP/IP 192.168.1.30:502", "40011"
    int         depth{1};
    Certainty   certainty{Certainty::Fact};
};
struct ValueAt {
    std::string path, value, quality;
    bool        available{false};
};
struct Related {
    enum class Kind : std::uint8_t { Shared, Simultaneous, Secondary, Independent };
    std::string name;
    Kind        kind{Kind::Independent};
    std::string why;            // "m\xC3\xAAme variable Pompe_1.Pression", "apparue 0,8 s apr\xC3\xA8s"
    std::string at;
    bool        active{false};
};
[[nodiscard]] std::string_view relatedLabel(Related::Kind) noexcept;
struct Hypothesis {
    std::string              text;
    Certainty                certainty{Certainty::Hypothesis};
    std::vector<std::string> evidence;   // ce qui l'appuie, dans les donnees
    std::vector<std::string> check;      // ce qu'il faut verifier
};

struct Snapshot {
    int         format{1};
    std::string id, createdAt, project, projectVersion, software;
    Event       trigger;
    // l'alarme telle qu'elle etait (copie : elle peut changer ou disparaitre du projet ensuite)
    struct Alarm {
        Id          id{kNoId};
        std::string name, description, condition, message, group, category, instruction;
        int         priority{3}, delayMs{0};
        bool        ackRequired{true};
        bool        known{false};        // trouvee dans le projet (une alarme d'objet : son nom seul)
    } alarm;
    std::vector<std::string> conditionVariables;   // les chemins lus par la condition (directs)
    std::vector<Link>        chain;                // la chaine de dependances (bornee)
    std::vector<ValueAt>     values;               // les valeurs au moment du defaut
    std::vector<std::string> activeAlarms;
    std::vector<Event>       timeline;
    std::vector<Related>     related;
    std::vector<Hypothesis>  hypotheses;           // classees (les mieux appuyees d'abord)
    std::vector<std::string> equipmentStates;      // "Station_Modbus : Injoignable (delai depasse)"
    std::vector<std::string> toCheck;              // les elements a examiner
    std::vector<std::string> missing;              // ce qui n'etait pas disponible
    std::string              openView;
    std::vector<std::string> visibleObjects;       // les objets de la vue ouverte lies a la chaine
    std::string              summary;              // une phrase
    // l'historique au moment du defaut : les incidents precedents de la meme alarme (le stockage les remplit)
    struct Previous {
        std::string id, at, folder, state, summary;
    };
    std::vector<Previous> previous;
};

// Lire une valeur au moment du defaut : nullopt - non disponible (jamais inventee).
using ValueReader = std::function<std::optional<std::pair<std::string, std::string>>(std::string_view path)>;   // valeur, qualite

struct SnapshotInput {
    const Project*                project{nullptr};
    Event                         trigger;
    std::vector<Event>            timeline;          // la chronologie (Monitor::timeline)
    std::vector<std::string>      active;            // les alarmes actives
    ValueReader                   read;              // nul : aucune valeur disponible
    std::vector<EquipmentStatus>  equipments;
    std::string                   openView;          // la vue affichee
    std::string                   projectName, projectVersion, software;
    std::string                   id, createdAt;
    int                           maxLinks{120};     // la chaine, bornee
    const rel::Graph*             graph{nullptr};    // le graphe deja construit (nul : construit ici)
};
// LE CONTEXTE DU DEFAUT, copie du projet a cet instant ; ne lit rien d'autre que ce qui est donne.
[[nodiscard]] Snapshot snapshot(const SnapshotInput&);

// La fiche de diagnostic (dix sections, markdown leger) : Resume, Alarme declenchee, Origine des donnees, Chaine de
// dependances, Autres alarmes, Chronologie, Analyse technique, Causes potentielles, Elements a verifier, Niveau de
// certitude. `analysis` : le texte d'une analyse IA (vide : la fiche deterministe seule).
[[nodiscard]] std::string diagnosis(const Snapshot&, std::string_view analysis = {});
// Le contexte cible envoye a une IA (pas tout le projet) : la fiche et la chaine, borne a `maxBytes`.
[[nodiscard]] std::string aiContext(const Snapshot&, std::size_t maxBytes = 12000);
// Un nom de dossier sur : "20261031-174012-POMPE_01_DEFAUT_PRESSION-a1b2c3d4".
[[nodiscard]] std::string folderName(const Snapshot&);

} // namespace hmi::incident
