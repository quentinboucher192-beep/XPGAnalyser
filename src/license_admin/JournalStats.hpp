// =============================================================================
//  license_admin/JournalStats.hpp - 1.47.0 : LES CHIFFRES DU JOURNAL DES ADMINS [J]
// -----------------------------------------------------------------------------
//  La maquette 1.47.0 (docs/maquettes/1.47.0, planches 01 a 05) et la liste des
//  statistiques des admins : chaque chiffre marque [J] vient du journal des
//  admins (signe, chaine : docs/ADMINS.md par. 6) que l'Admin releve et garde
//  deja (Workspace::journalLines). Il est calcule ICI, dans l'Admin, sans
//  reseau ni ecran - le service n'y est pour rien (docs/STATISTIQUES.md : les
//  chiffres [S] et [N] viennent de lui ; ceux-ci, non).
//
//  POUR UNE PERIODE ET CELLE D'AVANT (la meme longueur, juste avant) :
//   - par admin (la fiche, planches 02 et 03) : ses actions par type (les 13
//     droits, puis la boutique - toujours dans cet ordre) et par criticite, par
//     jour, la carte jour x heure, ses jours actifs, sa serie en cours, sa
//     derniere action, ses droits et ceux qu'il n'a jamais utilises, les
//     signatures refusees (droit manquant, cle retiree), ses actions depuis un
//     autre PC que le sien, a une heure inhabituelle POUR LUI (d'apres ses
//     propres habitudes), ses pics (plus de N fois sa moyenne), le detail de ce
//     qui compte (suppressions, revocations, codes revus), les changements de
//     ses droits (qui, quand), la chaine verifiee pour ses lignes ;
//   - la vue d'ensemble (planches 01 et 05) : le classement, << qui fait quoi >>
//     (la part de chacun par type, et l'ecart a la periode d'avant), les alertes
//     (chacune avec le geste qu'elle propose), et ce que l'admin+ seul fait.
//  Ce qui vient des tables du service (les licences signees, les demandes
//  traitees et leur delai, le montant encaisse, la version de son Admin) entre
//  par ServiceFigures : la page le remplit avec le client de docs/STATISTIQUES.md
//  (license_admin/Stats : GET /admin/stats/serie avec admin=<id>, GET
//  /admin/stats/admins) ; absent, le chiffre reste inconnu (NaN), ou se deduit
//  du journal quand il le peut (les licences signees, les demandes traitees).
//
//  LES HEURES ET LES JOURS : A L'HEURE DE PARIS (Europe/Paris : UTC+1 l'hiver,
//  UTC+2 du dernier dimanche de mars, 01:00 UTC, au dernier dimanche
//  d'octobre, 01:00 UTC) - l'editeur et ses admins sont en France. Le journal,
//  lui, est en secondes UTC (l'heure de l'action, `quand`, prise sur le PC de
//  l'admin ; a defaut l'heure de reception du service). Un jour commence a
//  minuit, heure de Paris ; le jour du changement d'heure a 23 ou 25 heures.
//  TimeZone::fixed() sert aux essais (UTC) et a qui voudrait une autre heure.
//
//  Des structures simples en sortie : la page (un autre agent) les dessine.
// =============================================================================
#pragma once

#include "../licensing/Admins.hpp"
#include "../licensing/Time.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin::gui {
struct JournalView;
} // namespace license_admin::gui

namespace license_admin::journalstats {

using Seconds = licensing::Seconds;
using Level = licensing::admins::Level;
inline constexpr double kUnknown = std::numeric_limits<double>::quiet_NaN();
[[nodiscard]] bool known(double v) noexcept;   // faux : NaN

// ------------------------------------------------------------------------------------- l'heure ---
struct TimeZone {
    std::function<int(Seconds)> offset;   // le decalage (secondes) a ajouter a l'heure UTC, a cet instant
    [[nodiscard]] int at(Seconds utc) const { return offset ? offset(utc) : 0; }
    [[nodiscard]] static TimeZone paris();               // Europe/Paris (le defaut)
    [[nodiscard]] static TimeZone fixed(int seconds);    // un decalage fixe (0 : UTC)
};
// Le decalage de Paris a cet instant : 3600 (heure d'hiver) ou 7200 (heure d'ete).
[[nodiscard]] int parisOffset(Seconds utc) noexcept;
// Le jour local (jours depuis le 01/01/1970), l'heure locale (0-23), le jour de la semaine (0 lundi ... 6 dimanche).
[[nodiscard]] std::int64_t localDay(Seconds utc, const TimeZone& tz);
[[nodiscard]] int localHour(Seconds utc, const TimeZone& tz);
[[nodiscard]] int localMinuteOfDay(Seconds utc, const TimeZone& tz);
[[nodiscard]] int weekday(std::int64_t day) noexcept;
// L'instant UTC du minuit local qui commence ce jour.
[[nodiscard]] Seconds dayStart(std::int64_t day, const TimeZone& tz);
[[nodiscard]] std::string dayText(std::int64_t day);           // "2026-11-10"
[[nodiscard]] bool parseDay(std::string_view text, std::int64_t& day) noexcept;

// --------------------------------------------------------------------------------- la periode ---
// Des jours entiers, heure de Paris : de `first` a `last` compris (jours locaux). Vide : last < first.
struct Period {
    std::int64_t first{0}, last{-1};
    [[nodiscard]] bool empty() const noexcept { return last < first; }
    [[nodiscard]] int  days() const noexcept { return empty() ? 0 : static_cast<int>(last - first + 1); }
    [[nodiscard]] bool contains(std::int64_t day) const noexcept { return day >= first && day <= last; }
    [[nodiscard]] std::string from() const { return dayText(first); }   // "2026-10-12"
    [[nodiscard]] std::string to() const { return dayText(last); }
};
// Les `days` derniers jours, aujourd'hui compris (7 j, 30 j, 90 j).
[[nodiscard]] Period lastDays(Seconds now, int days, const TimeZone& tz = TimeZone::paris());
// 12 mois : du meme jour un an plus tot + 1 jour, a aujourd'hui.
[[nodiscard]] Period lastMonths(Seconds now, int months, const TimeZone& tz = TimeZone::paris());
// Deux dates "AAAA-MM-JJ" ; faux : illisibles ou a l'envers.
bool between(std::string_view from, std::string_view to, Period& out) noexcept;
// La periode d'avant : la meme longueur, juste avant.
[[nodiscard]] Period previousOf(const Period& p) noexcept;

// --------------------------------------------------------------------------- les types d'action ---
// Les 14 types de la maquette : les 13 droits (dans l'ordre de admins::rights()), puis la boutique.
inline constexpr int kTypes = 14;
inline constexpr int kShop = 13;
struct TypeInfo {
    const char* id;      // le droit ("licence_creer"...), "boutique" pour le 14e
    const char* label;   // << Creer et signer une licence >>, << Boutique : livrer, revoir >>
};
[[nodiscard]] const std::array<TypeInfo, kTypes>& types();
// Le type d'une action du journal (0..13) ; -1 : hors des 14 (reservee a l'admin+, connexion, ligne du service).
[[nodiscard]] int typeOf(std::string_view action) noexcept;
// Les actions reservees a l'admin+ (docs/ADMINS.md par. 1, docs/BOUTIQUE.md) : versions, sauvegardes et restaurations,
// admins ajoutes ou retires, droits et profils, criticites, Stripe et la boutique, fichiers envoyes.
[[nodiscard]] bool reserved(std::string_view action) noexcept;

// -------------------------------------------------------------------------------------- l'entree ---
// Une ligne du journal, telle que l'Admin la lit (Workspace::journalLines : JournalRow) - en clair.
struct Line {
    std::uint64_t number{0};
    Seconds       when{0};       // l'heure de l'action (l'entree : `quand`) ; 0 : celle de la reception
    Seconds       received{0};   // l'heure du service
    std::string   key;           // la cle qui a signe (64 hexadecimaux)
    std::string   who;           // ce que l'Admin en dit : "admin+", "le service", le nom de l'admin ; vide : inconnue
    std::string   action, object, summary;
    std::string   detail, before, after;   // des objets JSON (texte ; vides : {})
    std::string   pc, machine, software;   // le PC qui l'a signee, son identifiant, la version de l'Admin
    Level         level{Level::Info};      // la criticite en vigueur (la ligne, sinon son type)
    bool          verified{true};          // la signature est bonne, la cle connue
    bool          chainOk{true};           // la chaine est intacte jusqu'a elle (aucune ligne manquante ou modifiee avant)
    std::string   hash;                    // l'empreinte de la ligne (SHA-256 hexa ; vide : inconnue)
    [[nodiscard]] Seconds at() const noexcept { return when ? when : received; }
};

// Un admin de la liste (et l'admin+ : ses cles d'editeur).
struct Admin {
    std::string              id;           // "xpg-a41c0e7b92d45f18" ; l'admin+ : "admin+"
    std::string              name;         // << Lea Essai >>
    std::vector<std::string> keys;         // ses cles (64 hexa) ; l'admin+ : les cles de l'editeur
    std::string              pc, machine;  // son PC (la liste) : le nom, l'identifiant
    std::vector<std::string> rights;       // ses droits aujourd'hui (l'admin+ : tous)
    Seconds                  since{0};     // admin depuis
    Seconds                  retired{0};   // 0 : actif
    std::string              profile;
    bool                     plus{false};  // l'admin+ (la cle de l'editeur)
};

// Ce que les tables du service disent de lui sur la periode (license_admin/Stats, docs/STATISTIQUES.md par. 5 et 7) ;
// NaN : inconnu. `Before` : la periode d'avant.
struct ServiceFigures {
    double      licences{kUnknown}, licencesBefore{kUnknown};          // licences_nouvelles (admin=<id>)
    double      requests{kUnknown}, requestsBefore{kUnknown};          // demandes_traitees
    double      meanDelay{kUnknown}, meanDelayBefore{kUnknown};        // demandes_delai_moyen (secondes)
    double      medianDelay{kUnknown};                                 // demandes_delai_median (secondes)
    double      amount{kUnknown}, amountBefore{kUnknown};              // ventes_montant (centimes)
    double      orders{kUnknown};                                      // ventes_commandes
    std::string adminVersion;                                          // GET /admin/stats/admins : logiciel_version
    Seconds     lastSeen{0};                                           // ... vu
};

// Une demande pour devenir admin (en attente) : une alerte << ... attend >>.
struct Pending {
    std::string name;
    Seconds     received{0};
};

struct Options {
    int    habitsDays{90};          // la fenetre des habitudes (heures, moyenne par jour), finie a la fin de la periode
    int    habitsMinActions{20};    // moins d'actions : pas assez d'habitudes, rien d'inhabituel
    int    unusualMarginMinutes{60};   // hors de [10e centile - marge, 90e centile + marge] de ses heures : inhabituel
    double peakFactor{3.0};         // un pic : plus de N fois sa moyenne par jour actif
    int    peakMinActions{5};       // ... et au moins tant d'actions ce jour-la
    int    unusedDays{90};          // un droit << jamais utilise >> : sur ces jours (ou depuis qu'il l'a, si c'est plus court)
    int    inactiveDays{30};        // une alerte : aucune action depuis tant de jours
    int    criticalDays{7};         // une alerte : une action critique de moins de tant de jours
    int    maxCriticalAlerts{5};
};

struct Inputs {
    std::vector<Line>                     lines;     // les plus anciennes d'abord
    std::vector<Admin>                    admins;    // dans l'ordre des pages (l'admin+, puis les admins : leur couleur)
    Period                                period;    // vide : les 30 derniers jours
    Seconds                               now{0};
    TimeZone                              tz{TimeZone::paris()};
    std::map<std::string, ServiceFigures> service;   // par id d'admin
    std::string                           latestAdminVersion;   // la derniere version de l'Admin publiee ("" : inconnue)
    std::vector<Pending>                  pending;
    Options                               options;
};

// --------------------------------------------------------------------------------------- la sortie ---
struct Ref {                          // une ligne du journal citee
    std::uint64_t number{0};
    Seconds       when{0};
    std::string   action, object, summary;
};
struct RightUse {                     // un des 13 droits, pour un admin
    std::string id, label;
    bool        held{false};          // il l'a aujourd'hui
    Seconds     given{0};             // depuis quand (0 : depuis qu'il est admin, ou inconnu)
    int         count{0};             // ses actions de ce type sur la periode
    int         countWindow{0};       // ... sur la fenetre de << jamais utilise >> (Options::unusedDays)
    Seconds     last{0};              // sa derniere action de ce type (tout le journal ; 0 : jamais)
    bool        neverUsed{false};     // il l'a, et ne s'en est jamais servi sur la fenetre
};
enum class RefusalReason : std::uint8_t { RemovedKey, MissingRight };
struct Refusal {
    Ref           line;
    RefusalReason reason{RefusalReason::RemovedKey};
    std::string   route, right;       // l'appel refuse (le service le dit), le droit qui manquait
};
struct OtherPc {
    Ref         line;
    std::string pc, machine;          // le PC de l'entree
    std::string usualPc;              // le sien (la liste)
};
struct Peak {
    std::int64_t day{0};
    int          count{0};
    double       factor{0};           // count / sa moyenne par jour actif
};
struct RightsChange {
    Ref                      line;
    std::string              by;          // qui l'a fait (le nom, ou "admin+")
    std::vector<std::string> added, removed;
    std::string              profile;
    bool                     joined{false}, left{false};   // admin_ajouter, admin_retirer
};
struct Chain {
    int           lines{0}, verified{0};
    std::uint64_t firstBreak{0};      // le numero de la premiere ligne dont la chaine est rompue (0 : aucune)
    std::uint64_t last{0};            // sa derniere ligne
    std::string   lastHash;           // son empreinte (vide : inconnue)
    Seconds       first{0};           // la premiere ligne (l'heure)
    [[nodiscard]] bool ok() const noexcept { return lines == verified && firstBreak == 0; }
};

struct AdminStats {
    std::string id, name;
    bool        plus{false}, retired{false};
    int         actions{0}, actionsBefore{0};         // ses gestes (sans connexion ni ligne du service), reserves compris
    int         typed{0}, typedBefore{0};             // ceux des 14 types
    double      change{kUnknown};                     // l'ecart des actions en % (NaN : rien avant)
    std::array<int, kTypes> byType{}, byTypeBefore{};
    std::array<int, 4>      byLevel{}, byLevelBefore{};   // Info, Moyenne, Haute, Critique
    std::vector<int>        perDay, perDayBefore;     // un point par jour de la periode (alignes)
    std::array<std::array<int, 24>, 7> dayHour{};     // lundi..dimanche x 0..23 h (Paris)
    std::array<std::array<int, 24>, 7> dayHourUnusual{};   // ... dont a une heure inhabituelle
    int         activeDays{0}, activeDaysBefore{0};
    int         streak{0}, bestStreak{0};             // jours ouvres d'affilee avec une action (le samedi et le dimanche ne coupent pas)
    Ref         last;                                 // sa derniere action (tout le journal ; number 0 : aucune)
    std::vector<Ref> recent;                          // ses 5 dernieres actions (tout le journal), la plus recente d'abord
    std::vector<int> dayFirst, dayLast;               // chaque jour de la periode : la minute (Paris) de sa premiere et de sa
                                                      // derniere action ; -1 : aucune ce jour-la
    std::vector<RightUse>     rights;                 // les 13, dans l'ordre
    std::vector<std::string>  unused;                 // les droits jamais utilises (ids)
    std::vector<Refusal>      refused;
    std::vector<OtherPc>      otherPc;
    bool                      habitsKnown{false};     // assez d'actions pour savoir ses heures
    int                       usualStart{-1}, usualEnd{-1};   // ses heures habituelles (minutes depuis minuit, Paris)
    std::vector<Ref>          unusual;                // ses actions a une heure inhabituelle (la periode)
    std::vector<Peak>         peaks;
    double                    meanPerActiveDay{0};    // sur la fenetre des habitudes
    std::vector<Ref>          deletions, revocations, codeReviews;   // le detail de ce qui compte (la periode)
    std::vector<RightsChange> rightsChanges;          // tout le journal
    Chain                     chain;                  // ses lignes (tout le journal)
    ServiceFigures            service;                // ce que le service en dit (copie)
    double                    licences{kUnknown}, licencesBefore{kUnknown};   // le service, sinon le journal (licence_creer)
    double                    requests{kUnknown}, requestsBefore{kUnknown};   // le service, sinon le journal (accepter, refuser)
    bool                      licencesFromJournal{false}, requestsFromJournal{false};
};

struct RankRow {
    std::string id, name;
    bool        plus{false}, retired{false};
    int         actions{0};
    double      change{kUnknown};
    double      licences{kUnknown}, requests{kUnknown}, meanDelay{kUnknown}, amount{kUnknown};
    Ref         last;
};

struct ShareRow {                     // << qui fait quoi >> : un type (ou << Toutes >>, type -1)
    int              type{-1};
    std::string      label;
    std::vector<int> perAdmin;        // aligne sur Overview::admins
    int              total{0}, totalBefore{0};
    double           changePct{kUnknown};   // en % si avant >= 10
    int              changeCount{0};        // sinon en nombre (total - avant)
    bool             changeInPercent{false};
    int              leader{-1};      // l'index de celui qui en fait le plus (-1 : personne)
    double           leaderShare{0};  // sa part (0..1)
    bool             alone{false};    // il est le seul a l'avoir fait
};

enum class AlertKind : std::uint8_t { Inactive, RightsUnused, AdminOutdated, RefusedSignatures, RecentCritical, UnusualHour, OtherPc, Pending };
enum class Severity : std::uint8_t { Critical, High, ToSee };   // << Critique >>, << Haute >>, << A voir >>
struct Alert {
    AlertKind                  kind{AlertKind::Inactive};
    Severity                   severity{Severity::ToSee};
    std::string                adminId, adminName;
    std::string                text;          // << Hugo Essai : 4 actions a une heure inhabituelle pour lui, dim. 08/11 de 23:40 a 23:58 >>
    std::string                gesture;       // le geste propose : << Voir ces 4 actions >>
    std::string                gestureId;     // "journal", "envoyer-maj", "retirer-droits", "fiche", "accepter"
    std::vector<std::uint64_t> lines;         // les lignes du journal concernees
    std::vector<std::string>   rights;        // retirer-droits : lesquels
    Seconds                    when{0};
};
[[nodiscard]] const char* severityLabel(Severity) noexcept;   // << Critique >>, << Haute >>, << A voir >>

struct PlusReserved {                 // l'admin+ : ce que lui seul fait (planche 05), sur la periode
    std::vector<Ref> versions;        // version_publier
    std::vector<Ref> backups;         // sauvegarde (ecrite par le service : l'admin+ seul la demande)
    std::vector<Ref> restores;        // restauration
    std::vector<Ref> adminsAdded, adminsRemoved;
    std::vector<Ref> rights;          // admin_droits, admin_profil
    std::vector<Ref> criticities;     // criticite
    std::vector<Ref> stripe;          // stripe_brancher, boutique (regler), commande_rembourser
    std::vector<Ref> files;           // fichier_envoyer
    Ref              lastBackup;      // la derniere sauvegarde (tout le journal)
};

struct Overview {
    Period                  period, previous;
    std::vector<AdminStats> admins;          // dans l'ordre de Inputs::admins
    int                     activeAdmins{0}, retiredAdmins{0}, pendingAdmins{0};
    int                     actions{0}, actionsBefore{0};   // tous les admins (l'admin+ compris)
    int                     typed{0}, typedBefore{0};       // << Toutes (hors reservees) >>
    double                  change{kUnknown};
    std::vector<RankRow>    ranking;         // les actions d'abord (puis le nom)
    std::vector<ShareRow>   shares;          // << Toutes >>, puis les 14 types dans l'ordre
    std::vector<Alert>      alerts;          // Critique, Haute, A voir ; les plus recentes d'abord
    PlusReserved            plus;
    Chain                   chain;           // tout le journal
};

// Les calculs (purs : rien d'autre que les entrees).
[[nodiscard]] Overview   overview(const Inputs& in);
[[nodiscard]] AdminStats adminStats(const Inputs& in, const std::string& adminId);   // la fiche (id inconnu : vide)

// -------------------------------------------------------- depuis l'Admin (Workspace, la liste signee) ---
// Les lignes de Workspace::journalLines (la chaine relue : chainOk). `editorKeys` : celles de l'admin+.
[[nodiscard]] std::vector<Line> linesOf(const gui::JournalView& view);
// Les admins de la liste (et l'admin+ en tete, `plusName` : son nom ; ses cles : `editorKeys`, en hexadecimal).
[[nodiscard]] std::vector<Admin> adminsOf(const licensing::admins::AdminList& list, const std::vector<std::string>& editorKeys,
                                          const std::string& plusName = "Toi (admin+)");

} // namespace license_admin::journalstats
