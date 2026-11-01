// =============================================================================
//  core/Perf.hpp - 1.22.2 : ce qui consomme sur le fil de l'interface
// -----------------------------------------------------------------------------
//  Le client (01/11) : << ajoute un onglet quelque part avec la performance du
//  logiciel, qu'est-ce qui consomme, car la l'IA on dirait que ca fait parfois
//  laguer >>. L'onglet est Performances (le panneau du bas, app/PerfPane) ; ce
//  fichier est la mesure, sans widget, toujours active.
//
//  LES PARTIES. Une partie est un nom fixe (un litteral : "ia.tick",
//  "peinture.presenter") enregistre UNE fois (part()) : elle recoit un numero, et
//  tout le reste travaille sur des tableaux fixes indexes par ce numero. Une
//  portee de mesure (XPG_MESURE("ia.tick")) lit l'horloge a l'entree et a la
//  sortie et ajoute au cumul de l'image en cours :
//    * le temps PROPRE de la partie (sans les parties mesurees dedans) ;
//    * son temps AVEC ce qu'elle appelle (une partie qui s'appelle elle-meme ne
//      compte qu'une fois) ;
//    * un appel de plus.
//  Ni allocation, ni verrou, ni chaine construite : deux lectures de l'horloge
//  et quelques additions. Seul le fil de l'interface (attachUiThread) compte ;
//  sur un autre fil, une portee ne fait rien.
//
//  L'IMAGE. La boucle principale appelle frameEnd() a chaque tour : la duree de
//  l'image (d'une fin d'image a la suivante), ce qui n'a ete mesure par aucune
//  portee (la partie 0, << (hors mesure) >>), les cumuls de chaque partie. Ils
//  vont dans la seconde en cours (un tableau tournant de 64 secondes : les
//  fenetres de 10 s et de 60 s, la courbe) et dans les totaux depuis le
//  lancement (ou depuis Remettre a zero).
//
//  LES SACCADES. Une image d'au moins le seuil (50 ms par defaut ; 33, 100,
//  250 au choix) entre au journal des saccades (les 200 dernieres) avec les
//  trois parties qui l'ont prise (le temps propre). Au-dela de 250 ms, une ligne
//  va aussi dans l'historique interne (core/CallTrail.hpp, genre << chien de
//  garde >>) : un futur rapport de blocage les montre.
//
//  LES TRAVAUX DE FOND. XPG_TRAVAIL("ia.appel"), au debut du fil d'un travail :
//  il est en cours tant que la portee vit (sans verrou : une case prise par un
//  compare-and-swap), puis il entre dans les derniers finis avec sa duree.
//
//  LE PROCESSUS. La memoire (Windows : WorkingSetSize et PrivateUsage ; Linux :
//  /proc/self/statm) et le temps processeur (GetProcessTimes ; getrusage), relus
//  au plus une fois par seconde.
//
//  Tout ce qui se lit (window, timeline, stutters, report) se lit sur le fil de
//  l'interface, la ou tout s'ecrit : rien a proteger.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace core::perf {

using Part = std::uint16_t;
inline constexpr std::size_t kMaxParts = 256;           // parties enregistrees au plus
inline constexpr Part        kNoPart = 0xFFFF;
inline constexpr Part        kUnmeasured = 0;           // << (hors mesure) >> : le reste de chaque image
inline constexpr std::size_t kSeconds = 64;             // les secondes gardees (la courbe : 60)
inline constexpr std::size_t kStutterCapacity = 200;    // le journal des saccades
inline constexpr int         kDefaultThresholdMs = 50;
inline constexpr int         kTrailStutterMs = 250;     // au-dela : une ligne dans l'historique interne
inline constexpr std::size_t kMaxJobs = 64;             // travaux de fond suivis en meme temps
inline constexpr std::size_t kFinishedJobs = 32;        // les derniers finis

// ---- les parties ---------------------------------------------------------------------
// Le numero de la partie `name` (un litteral, garde tel quel) ; enregistree au premier
// appel. Sans verrou au-dela du premier : XPG_MESURE garde le numero dans une statique.
// Plus de kMaxParts parties : kNoPart (la portee ne fait rien).
[[nodiscard]] Part part(const char* name) noexcept;
[[nodiscard]] const char* partName(Part p) noexcept;    // "" hors bornes
[[nodiscard]] std::size_t partCount() noexcept;

// ---- le fil de l'interface -------------------------------------------------------------
// Le fil appelant devient celui qui compte (App::run, les essais). Un seul a la fois.
void attachUiThread() noexcept;
[[nodiscard]] bool onUiThread() noexcept;

// ---- la portee de mesure ---------------------------------------------------------------
class Scope {
public:
    explicit Scope(Part p) noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Part          part_{kNoPart};
    Scope*        parent_{nullptr};
    std::int64_t  start_{0};
    std::int64_t  children_{0};    // le temps des portees mesurees dedans
};

// ---- des etapes a la suite (App::frame) -------------------------------------------------
// Dans un meme bloc : chaque to() ferme l'etape d'avant et ouvre la suivante ; stop() (ou la fin
// du bloc) ferme la derniere. Rien d'autre ne s'ouvre AU MEME NIVEAU entre deux to() (une portee
// XPG_MESURE y casserait l'emboitement) : les fonctions appelees ont les leurs, plus profond.
class Lap {
public:
    Lap() noexcept = default;
    ~Lap() { stop(); }
    Lap(const Lap&) = delete;
    Lap& operator=(const Lap&) = delete;
    void to(Part p) noexcept;
    void stop() noexcept;

private:
    std::optional<Scope> scope_;
};

// ---- l'image -------------------------------------------------------------------------
// A la fin de chaque tour de la boucle principale (sur le fil de l'interface).
void frameEnd() noexcept;
// Le seuil des saccades, en ms (33, 50, 100, 250 ; borne a 1..10 000).
void setStutterThresholdMs(int ms) noexcept;
[[nodiscard]] int stutterThresholdMs() noexcept;
// Tout remettre a zero : les secondes, les totaux, le journal des saccades (les parties
// gardent leur numero).
void reset() noexcept;

// ---- lire ----------------------------------------------------------------------------
struct PartStat {
    Part          part{kNoPart};
    const char*   name{""};
    std::int64_t  selfNs{0};        // le temps propre, cumule sur la fenetre
    std::int64_t  inclNs{0};        // avec ce qu'elle appelle
    std::int64_t  worstSelfNs{0};   // le plus long temps propre dans une seule image
    std::int64_t  worstInclNs{0};   // ... avec ce qu'elle appelle
    std::uint64_t calls{0};
};
struct Window {
    int                   seconds{0};       // 10, 60 ; 0 : depuis le lancement (ou la remise a zero)
    double                span{0.0};        // les secondes vraiment couvertes
    std::uint64_t         frames{0};
    std::int64_t          frameNs{0};       // la somme des images
    std::int64_t          worstFrameNs{0};
    std::vector<PartStat> parts;            // celles appelees dans la fenetre, la plus gourmande (temps propre) d'abord
    [[nodiscard]] double fps() const noexcept;               // images par seconde
    [[nodiscard]] double averageFrameMs() const noexcept;
};
[[nodiscard]] Window window(int seconds);
// Les secondes mesurees depuis le lancement (ou la remise a zero).
[[nodiscard]] double measuredSeconds() noexcept;

// Une seconde de la courbe (de la plus ancienne a la plus recente).
struct Second {
    int           ago{0};            // 0 : la seconde en cours
    std::uint32_t frames{0};
    double        averageMs{0.0};
    double        worstMs{0.0};
};
[[nodiscard]] std::vector<Second> timeline(int seconds = 60);

struct Stutter {
    std::uint64_t serial{0};         // 1, 2, 3... depuis le lancement
    std::uint64_t utcMs{0};          // l'heure de la fin de l'image
    std::int64_t  frameNs{0};
    std::uint8_t  count{0};          // les parties retenues (au plus 3)
    Part          top[3]{kNoPart, kNoPart, kNoPart};
    std::int64_t  topNs[3]{0, 0, 0}; // leur temps propre dans cette image
};
// Les saccades gardees, de la plus ancienne a la plus recente (au plus kStutterCapacity).
[[nodiscard]] std::vector<Stutter> stutters();
[[nodiscard]] std::uint64_t stutterCount() noexcept;      // depuis le lancement (ou la remise a zero)
// Les saccades (au moins le seuil) des `seconds` dernieres secondes - sans allocation (la pastille de l'onglet).
[[nodiscard]] std::size_t recentStutters(int seconds) noexcept;
// << 13:02:11.532 >> (l'heure locale) ; << ia.carte 280 ms ; peinture 12 ms >>.
[[nodiscard]] std::string stutterClock(const Stutter& s);
[[nodiscard]] std::string stutterParts(const Stutter& s);

// ---- les travaux de fond ---------------------------------------------------------------
class Job {
public:
    explicit Job(const char* name) noexcept;     // un litteral
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

private:
    const char*  name_{nullptr};
    int          slot_{-1};
    std::int64_t startMs_{0};
};
struct JobInfo {
    const char*   name{""};
    double        seconds{0.0};      // en cours : depuis quand ; fini : combien de temps
    double        endedAgo{-1.0};    // fini : il y a combien de secondes (-1 : en cours)
};
[[nodiscard]] std::vector<JobInfo> runningJobs();                     // le plus ancien d'abord
[[nodiscard]] std::vector<JobInfo> finishedJobs(std::size_t max = 10); // le plus recent d'abord

// ---- le processus --------------------------------------------------------------------
struct ProcessStats {
    bool          memoryKnown{false};
    std::uint64_t workingSet{0};     // Windows : WorkingSetSize ; Linux : la memoire residente
    std::uint64_t privateBytes{0};   // Windows : PrivateUsage ; Linux : residente moins partagee
    bool          cpuKnown{false};
    double        cpuSeconds{0.0};   // le temps processeur du processus depuis son lancement
    double        cpuPercent{-1.0};  // sur la derniere seconde, 100 = un processeur logique entier ; -1 : pas encore
    unsigned      cores{0};          // les processeurs logiques
};
// Relu au plus une fois par seconde (sinon : la derniere lecture).
[[nodiscard]] ProcessStats processStats();

// ---- le rapport (Copier le rapport) --------------------------------------------------
// La configuration, la cadence, la memoire, les travaux de fond, les `topParts` parties qui
// consomment le plus sur la fenetre, les `lastStutters` dernieres saccades (au moins le seuil).
[[nodiscard]] std::string report(int windowSeconds, std::size_t topParts = 20, std::size_t lastStutters = 20);

// ---- l'ecriture des nombres (le volet, le rapport) -------------------------------------
[[nodiscard]] std::string formatMs(double ms);         // "0,42 ms", "12,3 ms", "312 ms", "12,3 s"
[[nodiscard]] std::string formatBytes(std::uint64_t);   // "412 Mo", "1,2 Go"
[[nodiscard]] std::string formatPercent(double fraction);   // 0,421 -> "42,1 %"
[[nodiscard]] std::string windowLabel(int seconds);    // "10 s", "60 s", "depuis le lancement"

// ---- les essais -------------------------------------------------------------------------
// Une horloge a la main (en ns) : nul, l'horloge du systeme. Les essais seulement.
void setTestClock(const std::int64_t* ns) noexcept;
// Tout vider (aucune portee ouverte) ; les parties gardent leur numero.
void resetForTests() noexcept;

} // namespace core::perf

// XPG_MESURE("ia.tick") : la portee de mesure de la partie, son numero garde une fois.
// XPG_TRAVAIL("ia.appel") : un travail de fond en cours (au debut de son fil).
// XPG_PARTIE("app.messages") : le numero seul (pour Lap::to).
#define XPG_PERF_CAT2(a, b) a##b
#define XPG_PERF_CAT(a, b) XPG_PERF_CAT2(a, b)
#define XPG_MESURE(nom)                                                                                     \
    static const ::core::perf::Part XPG_PERF_CAT(xpgMesurePart_, __LINE__) = ::core::perf::part(nom);       \
    ::core::perf::Scope XPG_PERF_CAT(xpgMesure_, __LINE__) { XPG_PERF_CAT(xpgMesurePart_, __LINE__) }
#define XPG_TRAVAIL(nom) ::core::perf::Job XPG_PERF_CAT(xpgTravail_, __LINE__) { nom }
// XPG_PARTIE("app.messages") : le numero de la partie, garde une fois (une expression : Lap::to).
#define XPG_PARTIE(nom) ([]() noexcept -> ::core::perf::Part { static const ::core::perf::Part xpgPartie = ::core::perf::part(nom); return xpgPartie; }())
