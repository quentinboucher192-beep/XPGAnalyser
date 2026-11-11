// =============================================================================
//  license_admin/Stats.hpp - 1.47.0 : LES STATISTIQUES ET LE GLOBE, COTE
//  EDITEUR (l'outil de l'EDITEUR seulement ; jamais livre a un client)
// -----------------------------------------------------------------------------
//  Le contrat : docs/STATISTIQUES.md. Le service des licences (1.47.0) calcule
//  les courbes (les ventes, les licences, les demandes, les PC, les mises a
//  jour, les admins) des tables qu'il a deja et de ce qu'il releve depuis la
//  1.47 ; il garde la ville de la DERNIERE connexion de chaque PC (jamais son
//  adresse IP) pour le globe. Les chiffres du journal des admins ([J]) sont
//  calcules par l'Admin lui-meme (JournalStats), pas ici.
//
//  CE QUI EST ICI (sans ecran) :
//    - les requetes (SeriesQuery, UpdatesQuery...) et leur adresse ;
//    - les reponses lues (parseSeries, parseUpdates, parseAdmins, parseGlobe,
//      parseLive) : des structures que les pages dessinent (Series : ses points,
//      ses parts, la comparaison ; GlobeCity : ses calques) ;
//    - les appels au service (BLOQUANTS, signes par la cle qui signe : l'admin+
//      ou un admin) ; un service d'avant la 1.47.0 : refuse avec la phrase
//      serviceTooOldText (<< deploie la 1.47.0 >>), comme shop::kServiceRequired ;
//    - les textes : un montant en euros, une duree, une variation, le nom d'une
//      mesure.
//  UN POINT INCONNU vaut NaN (isNumber : faux) : un ratio sans valeur, un jour
//  d'avant les releves.
// =============================================================================
#pragma once

#include "ServiceClient.hpp"

#include "../core/Json.hpp"
#include "../licensing/Time.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin::stats {

using licensing::Seconds;

// Le service qu'il faut pour les statistiques et le globe (ses routes) : un service plus ancien marche pour tout le reste.
inline constexpr const char* kServiceRequired = "1.47.0";
// La version du service sait-elle les statistiques (1.47.0 ou plus) ? `version` : celle de /v1/etat.
[[nodiscard]] bool serviceKnowsStats(const std::string& version);
// "Ton service en ligne est en 1.46.0 : d\xC3\xA9ploie la 1.47.0 pour les statistiques et le globe." (vide : il les sait)
[[nodiscard]] std::string serviceTooOldText(const std::string& version);

// ---- les periodes (des jours UTC "AAAA-MM-JJ", compris) ----
enum class Step { Day, Week, Month };          // "jour", "semaine", "mois"
enum class Compare { None, Previous, LastYear };   // "rien", "avant", "an"
[[nodiscard]] const char* stepKey(Step s) noexcept;
[[nodiscard]] const char* compareKey(Compare c) noexcept;
bool parseStep(std::string_view key, Step& out) noexcept;
bool parseCompare(std::string_view key, Compare& out) noexcept;

struct Period {
    std::string from, to;   // "2026-08-12", "2026-11-10" ; vides : le defaut du service (les 30 derniers jours)
    [[nodiscard]] bool empty() const noexcept { return from.empty() && to.empty(); }
};
[[nodiscard]] std::string dayOf(Seconds t);                 // le jour UTC de cet instant
[[nodiscard]] Seconds dayStart(std::string_view day);       // 00:00 UTC de ce jour ; -1 : illisible
[[nodiscard]] Period lastDays(Seconds now, int days);       // 7 j, 30 j, 90 j : les `days` derniers jours, aujourd'hui compris
[[nodiscard]] Period lastMonths(Seconds now, int months);   // 12 mois : du meme jour, `months` mois plus tot (+1 jour)
[[nodiscard]] Period nextDays(Seconds now, int days);       // les echeances : aujourd'hui -> +days
[[nodiscard]] bool isNumber(double v) noexcept;             // faux : NaN (inconnu)

// ---- les courbes : GET /admin/stats/serie ----
struct SeriesQuery {
    std::vector<std::string> measures;               // "ventes_montant", "pc_actifs"... (docs/STATISTIQUES.md, par. 5)
    Period                   period;
    Step                     step{Step::Day};
    std::string              split;                  // "", "offre", "option", "article", "admin", "client", "logiciel", "version", "type", "licence"
    Compare                  compare{Compare::Previous};
    std::string              client, admin, licence, software;   // les filtres (vides : aucun)
    [[nodiscard]] std::string path() const;           // "/admin/stats/serie?mesures=...&du=..."
};
struct Bucket {
    std::string from, to;   // la periode du pas (jours compris)
};
struct Part {
    std::string         key;      // la cle de la division ("PROFESSIONAL", "xpg-...", "Atelier Exemple") ; "" : sans ; "\xE2\x80\xA6" : le reste
    std::vector<double> points;
    double              total{0};
};
struct Series {
    std::string              name, unit, kind, source;   // "centimes"|"nombre"|"secondes" ; "flux"|"niveau"|"ratio"|"cumul"|"max" ; "S"|"N"|"S+N"
    std::vector<double>      points;                    // un par periode du pas (NaN : inconnu)
    double                   total{0};                  // flux : la somme ; niveau, cumul : le dernier point ; ratio : sur toute la periode
    std::vector<Part>        parts;                     // la plus grande d'abord
    std::vector<double>      before;                    // la comparaison, alignee sur `points` (vide : aucune)
    double                   beforeTotal{0};
    double                   change{0};                 // la variation en % (NaN : aucune)
    bool                     refused{false};            // un droit manque (`rights`), ou un filtre que la mesure n'a pas (`filter`)
    std::vector<std::string> rights;
    std::string              filter;
    [[nodiscard]] const Part* part(const std::string& key) const;
    [[nodiscard]] bool hasComparison() const noexcept { return !before.empty(); }
};
struct SeriesReply {
    Period              period, beforePeriod;
    Step                step{Step::Day};
    Compare             compare{Compare::Previous};
    std::string         split;
    std::vector<Bucket> buckets, beforeBuckets;
    std::vector<Series> series;
    Seconds             at{0};
    std::string         recordedSince;   // "AAAA-MM-JJ" : avant, des chiffres reconstitues
    std::string         serviceVersion;
    [[nodiscard]] const Series* find(std::string_view name) const;
};

// ---- les mises a jour : GET /admin/stats/mises-a-jour ----
struct UpdatesQuery {
    Period      period;                  // les PC vus dans la periode ; vide : les 90 derniers jours
    Step        step{Step::Week};        // la part a jour dans le temps
    int         days{42};                // la longueur des courbes d'adoption (1 a 120)
    std::string software{"tous"};        // "studios", "admins", "tous"
    [[nodiscard]] std::string path() const;
};
struct VersionRow {
    std::string version;
    Seconds     published{0};            // 0 : jamais publiee
    int         studios{0}, admins{0};   // les PC dessus a leur derniere connexion
};
struct AdoptionCurve {
    std::string      version, software;  // "studios", "admins"
    Seconds          published{0};
    int              base{0};            // le 100 %
    std::vector<int> days;               // days[k] : installee (ou plus recente) au plus tard k jours apres la publication
};
struct UpdateDelay {
    std::string version, software;
    int         count{0};
    double      median{0}, mean{0};      // secondes
};
struct BehindPc {
    std::string key, software, version, name, customer, licence, admin;
    Seconds     seen{0};
    int         behind{0};               // les versions publiees plus recentes
};
struct UpdatesReply {
    std::string                latest;
    Seconds                    latestPublished{0};
    std::vector<VersionRow>    versions;   // la plus recente d'abord
    int                        seenStudios{0}, seenAdmins{0}, upToDateStudios{0}, upToDateAdmins{0};
    std::string                seenSince;
    std::vector<AdoptionCurve> adoption;
    std::vector<UpdateDelay>   delays;
    std::vector<Bucket>        buckets;
    std::vector<int>           studiosUpToDate, studiosSeen, adminsUpToDate, adminsSeen;
    std::vector<BehindPc>      behind;     // les plus en retard d'abord (500 au plus)
    int                        behindTotal{0};
    std::string                recordedSince;
    [[nodiscard]] const AdoptionCurve* curve(const std::string& version, const std::string& software) const;
};

// ---- les admins : GET /admin/stats/admins ----
struct AdminUsage {
    std::string              id, key, role, name;     // role : "admin+", "admin"
    Seconds                  retired{0};
    std::vector<std::string> days;                    // les jours de connexion (UTC)
    std::array<int, 24>      hours{};                 // les appels de chaque heure UTC
    int                      calls{0};
    int                      usualStart{-1}, usualEnd{-1};   // heures UTC (medianes) ; -1 : aucun jour
    Seconds                  lastSeen{0};
    std::string              software, softwareVersion, city, country;
};
struct AdminsReply {
    Period                  period;
    std::string             recordedSince;
    std::vector<AdminUsage> admins;
    [[nodiscard]] const AdminUsage* find(const std::string& id) const;
};

// ---- le globe : GET /admin/globe, /admin/globe/direct ----
struct GlobeCity {
    std::string              id, city, region, country, colo;   // id : "FR|Lyon"
    double                   lat{0}, lon{0};
    int                      clients{0}, pcs{0}, active{0}, outdated{0}, admins{0}, discoveries{0}, orders{0}, requests{0};
    std::vector<std::string> customers, devices, adminIds;
    // Le compte d'un calque : "clients", "pc", "actifs", "pas_a_jour", "admins", "decouvertes", "commandes", "demandes".
    [[nodiscard]] int count(std::string_view layer) const noexcept;
};
struct GlobeReply {
    Seconds                at{0};
    int                    periodDays{30};
    std::string            latestVersion;
    std::vector<GlobeCity> cities;   // la plus grande d'abord
    GlobeCity              totals;   // les comptes de tout le globe (sans ville)
    int                    cityCount{0}, countries{0}, unplacedPcs{0}, unplacedAdmins{0};
    int                    unplacedActive{0};   // 1.48.1 : les actifs maintenant sans lieu (le direct : villes + eux)
    [[nodiscard]] const GlobeCity* find(const std::string& id) const;
};
struct LivePing {
    Seconds     at{0};
    std::string city;   // l'id de la ville
    std::string kind;   // "studio", "admin"
};
struct LiveReply {
    Seconds               at{0};
    int                   active{0};   // les PC des Studios vus dans les 15 dernieres minutes
    std::vector<LivePing> pings;
};
struct SnapshotReply {
    std::string       day;
    bool              taken{false};   // faux : il existait deja
    core::json::Value snapshot;
};

// ---- 1.48.1 : LA COHERENCE (docs/STATISTIQUES.md, par. 15) - POST /admin/stats/reevaluer ----
// Ce qui n'existe plus dans l'Admin et que le service garde (un client supprime, une licence supprimee), ou qui revient
// (un client restaure) : le service ne le compte plus (ou de nouveau). Vide : seulement reevaluer.
struct Exclusions {
    std::vector<std::string> deleteClients, deleteLicences, restoreClients, restoreLicences;
    [[nodiscard]] bool empty() const noexcept { return deleteClients.empty() && deleteLicences.empty() && restoreClients.empty() && restoreLicences.empty(); }
    [[nodiscard]] std::string body() const;   // {"supprimer": {...}, "remettre": {...}} ; "{}" : vide
};
// Une correction : `group` "exclus", "maintenant", "globe", "releves", "compteurs", "versions" ; `text` : en francais.
struct Correction {
    std::string group, text, measure, day;
    double      before{std::numeric_limits<double>::quiet_NaN()}, after{std::numeric_limits<double>::quiet_NaN()};
};
struct ReevaluateReply {
    std::string             day;
    int                     excludedClients{0}, excludedLicences{0}, excludedPcs{0};
    std::vector<Correction> corrections;
};
inline constexpr const char* kReevaluateRequired = "1.48.1";
// Le titre d'un groupe (<< Les releves des jours passes >>...) ; les groupes dans leur ordre : correctionGroups().
[[nodiscard]] std::string correctionGroupTitle(std::string_view group);
[[nodiscard]] const std::vector<std::string>& correctionGroups();
// << 3 corrections >> / << Rien a corriger : les statistiques etaient coherentes. >>
[[nodiscard]] std::string reevaluateSummary(const ReevaluateReply& r);

// ---- lire les reponses ; faux : `why` ----
bool parseSeries(const core::json::Value& v, SeriesReply& out, std::string* why);
bool parseUpdates(const core::json::Value& v, UpdatesReply& out, std::string* why);
bool parseAdmins(const core::json::Value& v, AdminsReply& out, std::string* why);
bool parseGlobe(const core::json::Value& v, GlobeReply& out, std::string* why);
bool parseLive(const core::json::Value& v, LiveReply& out, std::string* why);
bool parseReevaluate(const core::json::Value& v, ReevaluateReply& out, std::string* why);   // 1.48.1

// ---- le reseau (BLOQUANT ; un client ouvert) ----
// `raw` (facultatif) : la reponse du service telle quelle (la console --json).
[[nodiscard]] online::Result fetchSeries(online::Client& c, const SeriesQuery& q, SeriesReply& out, std::string* raw = nullptr);
[[nodiscard]] online::Result fetchUpdates(online::Client& c, const UpdatesQuery& q, UpdatesReply& out, std::string* raw = nullptr);
[[nodiscard]] online::Result fetchAdmins(online::Client& c, const Period& p, AdminsReply& out, std::string* raw = nullptr);
[[nodiscard]] online::Result fetchGlobe(online::Client& c, int periodDays, GlobeReply& out, std::string* raw = nullptr);
[[nodiscard]] online::Result fetchLive(online::Client& c, Seconds since, LiveReply& out, std::string* raw = nullptr);
// L'admin+ : le releve d'aujourd'hui, s'il n'est pas encore pris (POST /admin/stats/releve).
[[nodiscard]] online::Result takeSnapshot(online::Client& c, SnapshotReply& out, std::string* raw = nullptr);
// 1.48.1 : reevaluer (l'admin+ ou un admin ; dire ce qui est supprime : le droit de la suppression). Un service d'avant la
// 1.48.1 : << deploie la 1.48.1 pour Reevaluer >>.
[[nodiscard]] online::Result reevaluate(online::Client& c, const Exclusions& ex, ReevaluateReply& out, std::string* raw = nullptr);

// ---- les textes ----
[[nodiscard]] std::string euros(double cents);             // "26\xC2\xA0" "020\xC2\xA0\xE2\x82\xAC" (sans centimes) ; "\xE2\x80\x94" : inconnu
[[nodiscard]] std::string duration(double seconds);        // "3 h 40", "12 min", "2 j 4 h" ; "\xE2\x80\x94" : inconnu
[[nodiscard]] std::string changeText(double percent);      // "+13 %", "\xE2\x88\x92" "29 %" ; "" : inconnu
[[nodiscard]] std::string measureLabel(std::string_view name);   // "Montant encaiss\xC3\xA9", "PC actifs"... ; le nom s'il est inconnu
// Toutes les mesures du service 1.47.0 (docs/STATISTIQUES.md, par. 5).
[[nodiscard]] const std::vector<std::string>& measureNames();

} // namespace license_admin::stats
