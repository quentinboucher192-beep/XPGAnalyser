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

#include <array>
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
    int                      framesPerSecond{5};   // 1.20.2 : 5 (2 en 1.20.0 : une video a saccades)
    std::size_t              maxPending{200};  // au-dela, les plus anciens incidents en attente non commentes sont dits
    // 1.21.0 : UNE ALARME QUI BAT ne fait plus un incident par reapparition (voir Burst, plus bas). Une reapparition
    // moins de `regroup` secondes apres la derniere activite de l'alarme (son incident, une reapparition, sa
    // disparition : la fenetre GLISSE) est comptee dans son incident, sans dossier, sans image, sans video ; seule une
    // alarme restee calme plus longtemps rouvre un incident. 0 : desactive (le comportement d'avant).
    double                   regroup{600.0};
    // 1.21.0 : les garde-fous globaux (l'hote). Au plus `maxPerHour` NOUVEAUX incidents par heure glissante, toutes
    // alarmes confondues (au-dela : comptes, sans dossier ; 0 : sans plafond). Au-dela de `mediaQuotaMb` Mo d'images
    // et de videos d'incidents, les nouveaux sont crees sans image ni video (0 : sans quota) - rien n'est supprime.
    std::size_t              maxPerHour{60};
    std::size_t              mediaQuotaMb{2048};
};
// Une alarme est-elle surveillee ?
[[nodiscard]] bool watched(const Settings&, std::string_view alarm);
// 1.20.2 : LA CASE << INCIDENTS >> DES ALARMES (IHM > Alarmes) - cette alarme ouvre-t-elle un dossier d'incident ?
// Une alarme de IHM > Alarmes : sa case (AlarmDef::incidents) ; une autre (une alarme d'objet) : la case de son
// groupe (`group` : celui de l'avis, AlarmNotice::group - le groupe lie, ou celui de sa surcharge ; un groupe
// inconnu : oui) ; sans groupe : oui. L'hote (IncidentHost) la demande AVANT toute capture. `settings.enabled`
// reste l'interrupteur general (rien du tout quand il est faux).
[[nodiscard]] bool allowedFor(const Project&, std::string_view alarm, std::string_view group = {});

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

// ---- 1.21.0 : LES REAPPARITIONS REGROUPEES (une alarme qui bat) ------------------------------------------------------
//  Une alarme qui reapparait sans cesse (toutes les 2 s, des jours durant) faisait un incident a chaque fois : des
//  centaines de milliers de dossiers, d'images, de videos, et la memoire de la liste. Desormais, apres son incident,
//  chaque reapparition dans la fenetre de regroupement (Settings::regroup) est COMPTEE dans une rafale. Ce qui est
//  garde est BORNE, quel que soit le nombre de reapparitions (300 000 ou 30 millions : la meme taille, au plus
//  kMaxBytes) : le compte exact, l'origine, la premiere et la derniere reapparition, les intervalles min / moyen / max,
//  la duree active, deux histogrammes (par minute sur la derniere heure, par heure sur les dernieres 24 heures, jusqu'a
//  la derniere reapparition) et les kRecent dernieres detaillees. Aucune allocation par reapparition : l'anneau des
//  dernieres est reserve une fois, les textes sont de taille fixe.

struct Snapshot;

// Un petit texte de taille fixe (aucune allocation) : un horodatage, une valeur. Coupe sur un caractere entier.
class ShortText {
public:
    static constexpr std::size_t kMax = 23;
    ShortText() = default;
    explicit ShortText(std::string_view s) noexcept { set(s); }
    void set(std::string_view s) noexcept;
    [[nodiscard]] std::string_view view() const noexcept { return {chars_.data(), size_}; }
    [[nodiscard]] std::string      str() const { return std::string(view()); }
    [[nodiscard]] bool             empty() const noexcept { return size_ == 0; }

private:
    std::array<char, kMax> chars_{};
    std::uint8_t           size_{0};
};

struct Reappearance {
    double    t{0};             // l'horloge de l'hote (secondes) : les intervalles
    float     active{-1.f};     // restee active (s) jusqu'a sa disparition ; negatif : encore active, ou inconnue
    ShortText at;               // l'horodatage du moteur
    ShortText value;            // la valeur lue au meme cycle (la premiere variable de la condition) ; vide : inconnue
};

struct Burst {
    static constexpr std::size_t kRecent = 200;      // les dernieres reapparitions detaillees
    static constexpr int         kMinutes = 60;      // l'histogramme par minute : la derniere heure
    static constexpr int         kHours = 24;        // ... par heure : les dernieres 24 heures
    static constexpr std::size_t kMaxBytes = 16384;  // la memoire d'une rafale, au plus (memoryBytes ; les essais)

    // l'apparition qui a fait l'incident (l'origine des intervalles)
    double        originT{0};
    ShortText     originAt;
    // les reapparitions comptees (sans l'origine)
    std::uint64_t count{0};
    double        firstT{0}, lastT{0};
    ShortText     firstAt, lastAt;
    double        minGap{0}, maxGap{0};              // entre deux apparitions successives (l'origine comprise)
    // la duree active (de l'apparition a la disparition), sur les disparitions vues
    double        activeSum{0}, activeMax{0};
    std::uint64_t activeCount{0};
    // les histogrammes, la derniere case a droite : minutes[kMinutes - 1] est la minute de la derniere reapparition
    // (lastMinute = floor(lastT / 60)), hours[kHours - 1] son heure (lastHour = floor(lastT / 3600))
    std::int64_t                          lastMinute{0}, lastHour{0};
    std::array<std::uint32_t, kMinutes>   minutes{};
    std::array<std::uint32_t, kHours>     hours{};
    // les dernieres reapparitions : un anneau (au plus kRecent ; une fois plein, `head` est la plus ancienne)
    std::vector<Reappearance> ring;
    std::size_t               head{0};
    // une apparition dont on attend la disparition (sa duree active)
    bool                      pending{false};
    double                    pendingT{0};

    void start(double t, std::string_view at);                              // l'apparition qui fait l'incident
    void add(double t, std::string_view at, std::string_view value = {});   // une reapparition
    void cleared(double t);                                                 // l'alarme est repartie
    [[nodiscard]] double meanGap() const noexcept { return count ? (lastT - originT) / static_cast<double>(count) : 0.0; }
    [[nodiscard]] double meanActive() const noexcept { return activeCount ? activeSum / static_cast<double>(activeCount) : -1.0; }
    [[nodiscard]] std::vector<Reappearance> recent() const;               // du plus ancien au plus recent (au plus kRecent)
    void setRecent(const std::vector<Reappearance>& list);                  // (la relecture : les plus recentes gardees)
    [[nodiscard]] std::size_t memoryBytes() const noexcept { return sizeof(Burst) + ring.capacity() * sizeof(Reappearance); }
};

// 300000 -> "300 000" (l'espace est insecable : le nombre ne se coupe pas a la ligne).
[[nodiscard]] std::string countText(std::uint64_t n);
// "toutes les 10 s environ", "toutes les 3 min environ", "toutes les 2 h environ".
[[nodiscard]] std::string everyText(double seconds);
// Une duree courte, en francais : "0,4 s", "12 s", "3 min 20 s", "2 h 05".
[[nodiscard]] std::string durationText(double seconds);
// LE DIAGNOSTIC D'UNE RAFALE, en clair (markdown leger) : l'alarme est instable / bat, sa cadence, sa duree active,
// les valeurs lues, et les PISTES a verifier (hysteresis du seuil, temporisation d'apparition, capteur ou contact
// defectueux) - jamais une cause affirmee. `snapshot` (facultatif) : la temporisation et la condition de l'alarme.
[[nodiscard]] std::string burstDiagnosis(const Burst&, const Snapshot* snapshot = nullptr);
// Le resume d'une rafale pour une analyse IA (borne a `maxBytes`) : les chiffres, l'histogramme par heure, les dernieres.
[[nodiscard]] std::string burstContext(const Burst&, std::size_t maxBytes = 2500);

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
