// =============================================================================
//  license_admin/gui/StatsSamples.cpp - 1.47.0 : LES REPONSES D'ESSAI DES
//  STATISTIQUES (des chiffres FICTIFS, pour les essais et les captures)
// -----------------------------------------------------------------------------
//  Un faux service, deterministe : chaque jour a ses evenements (des commandes,
//  des licences signees a la main, prolongees, expirees, des PC actives, des
//  demandes...), tires d'une empreinte du jour - toujours les memes. Une
//  requete (GET /admin/stats/serie : les mesures, la periode, le pas, la
//  comparaison, la division, les filtres) est repondue en JSON comme le service
//  1.47.0 (docs/STATISTIQUES.md, par. 4 a 7), puis lue par les vrais lecteurs
//  (stats::parseSeries...). Les niveaux (licences actives, PC actifs) sont
//  ancres sur << maintenant >> (128 licences, 286 PC) et refaits par les flux.
//  Rien de reel : les admins Camille, Lea, Hugo et Nina Essai ; les clients
//  << Atelier Exemple >>, << Societe d'essai >>...
//  Montrees seulement par un essai (setFetchersForTests) ou par XPG_STATS_ESSAI=1.
// =============================================================================
#include "StatsKit.hpp"

#include "../../core/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace license_admin::gui::statskit {

namespace {

namespace json = core::json;
using Day = std::int64_t;

std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
double h01(Day d, std::uint64_t salt, std::uint64_t k = 0) {
    return static_cast<double>(mix(static_cast<std::uint64_t>(d) * 1315423911ull + salt * 2654435761ull + k * 97531ull) >> 11) / 9007199254740992.0;
}

Day dayOf(const std::string& s) {
    const Seconds t = stats::dayStart(s);
    return t < 0 ? -1 : t / 86400;
}
std::string dayText(Day d) {
    int y = 0, m = 0, dd = 0;
    ch::fr::civil(d, y, m, dd);
    char b[16];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, dd);
    return b;
}
int weekday(Day d) { return ch::fr::weekday(d); }   // 0 lundi

// ---- les noms fictifs ----
const char* const kAdminIds[4] = {"xpg-c4a11e0000000001", "xpg-1ea0000000000002", "xpg-4e60000000000003", "xpg-9a10000000000004"};
const char* const kAdminNames[4] = {"Camille Essai", "L\xC3\xA9" "a Essai", "Hugo Essai", "Nina Essai"};
const char* const kClients[] = {"Atelier Exemple", "Soci\xC3\xA9t\xC3\xA9 d'essai", "Usine Exemple Nord", "Bureau d'\xC3\xA9tudes Exemple", "Client d'essai",
                                "Laboratoire Exemple", "Garage d'essai", "Imprimerie Exemple", "Menuiserie Exemple", "Ferme d'essai",
                                "Cabinet Exemple", "Transports d'essai"};
constexpr int kClientCount = 12;
const double kClientWeight[kClientCount] = {0.04, 0.12, 0.10, 0.09, 0.05, 0.11, 0.08, 0.10, 0.09, 0.07, 0.08, 0.07};
const char* const kOffers[3] = {"PERSONAL", "PROFESSIONAL", "ENTERPRISE"};
const int kOfferCents[3] = {19000, 49000, 129000};
const char* const kOptions[3] = {"Exports avanc\xC3\xA9s", "Travail \xC3\xA0 plusieurs", "Imports avanc\xC3\xA9s"};
const int kOptionCents[3] = {7000, 8600, 5800};

int pickClient(double u) {
    double a = 0;
    for (int i = 0; i < kClientCount; ++i) {
        a += kClientWeight[i];
        if (u < a) return i;
    }
    return kClientCount - 1;
}
// Qui l'a fait : Camille (l'admin+) 46 %, Lea 32 %, Hugo 17 %, Nina 5 % (retiree depuis le 01/06/2026 : plus rien ensuite).
int pickAdmin(Day d, double u) {
    const bool nina = d < dayOf("2026-06-01");
    if (u < 0.46) return 0;
    if (u < 0.78) return 1;
    if (u < (nina ? 0.95 : 1.01)) return 2;
    return 3;
}

// ---- un evenement d'un jour ----
struct Ev {
    std::string kind;      // "commande", "remboursement", "main", "nouvelle", "prolongee", "option", "expiree", "revoquee",
                           // "demande", "traitee", "pc_active", "pc_libere", "pc_retire", "decouverte", "convertie"
    std::string offer;     // PERSONAL... ; une commande d'option : "options"
    std::string option;    // l'option (une commande d'option)
    int         client{0}, admin{0};
    double      cents{0};
    double      delay{0};  // une demande traitee : son delai (secondes)
    bool        online{false};
};

double trend(Day d) { return 1.0 + 0.00025 * static_cast<double>(d - dayOf("2026-01-01")); }

std::vector<Ev> eventsOf(Day d) {
    std::vector<Ev> out;
    const bool work = weekday(d) < 5;
    // les commandes (la boutique)
    const int orders = static_cast<int>(std::floor((work ? 1.15 : 0.22) * trend(d) + h01(d, 1)));
    for (int k = 0; k < orders; ++k) {
        Ev e;
        e.kind = "commande";
        e.online = true;
        e.client = pickClient(h01(d, 3, k));
        e.admin = pickAdmin(d, h01(d, 4, k));
        const double r = h01(d, 2, k);
        if (r < 0.74) {
            const int o = r < 0.36 ? 0 : r < 0.705 ? 1 : 2;
            e.offer = kOffers[o];
            e.cents = kOfferCents[o];
        } else {
            const double r2 = h01(d, 5, k);
            const int o = r2 < 0.62 ? 0 : r2 < 0.86 ? 1 : 2;
            e.offer = "options";
            e.option = kOptions[o];
            e.cents = kOptionCents[o];
        }
        out.push_back(e);
        if (e.offer != "options") {   // la licence vendue en ligne : neuve, ou une prolongation
            Ev l = e;
            l.kind = h01(d, 6, k) < 0.53 ? "nouvelle" : "prolongee";
            out.push_back(l);
        } else {
            Ev l = e;
            l.kind = "option";
            out.push_back(l);
        }
    }
    // un remboursement (une commande payee trois jours plus tot)
    for (int k = 0; k < 3; ++k)
        if (h01(d - 3, 7, k) < 0.035 && k < static_cast<int>(std::floor((weekday(d - 3) < 5 ? 1.15 : 0.22) * trend(d - 3) + h01(d - 3, 1)))) {
            Ev e;
            e.kind = "remboursement";
            const double r = h01(d - 3, 2, k);
            e.cents = r < 0.74 ? kOfferCents[r < 0.36 ? 0 : r < 0.705 ? 1 : 2] : 7000;
            e.client = pickClient(h01(d - 3, 3, k));
            out.push_back(e);
        }
    // les licences signees a la main
    if (work && h01(d, 8) < 0.42) {
        Ev e;
        e.kind = "main";
        const double r = h01(d, 9);
        e.offer = kOffers[r < 0.3 ? 0 : r < 0.85 ? 1 : 2];
        e.client = pickClient(h01(d, 10));
        e.admin = pickAdmin(d, h01(d, 11));
        out.push_back(e);
        Ev l = e;
        l.kind = h01(d, 12) < 0.53 ? "nouvelle" : "prolongee";
        out.push_back(l);
    }
    // les fins (dans le futur aussi : les echeances)
    for (int k = 0; k < 2; ++k)
        if (h01(d, 13, k) < (k == 0 ? 0.36 : 0.05)) {
            Ev e;
            e.kind = "expiree";
            const double r = h01(d, 14, k);
            const int o = r < 0.35 ? 0 : r < 0.88 ? 1 : 2;
            e.offer = kOffers[o];
            e.cents = kOfferCents[o];
            e.client = pickClient(h01(d, 15, k));
            e.admin = pickAdmin(d, h01(d, 16, k));
            out.push_back(e);
        }
    if (h01(d, 17) < 0.025) {
        Ev e;
        e.kind = "revoquee";
        e.offer = kOffers[h01(d, 18) < 0.5 ? 0 : 1];
        e.client = pickClient(h01(d, 19));
        e.admin = pickAdmin(d, h01(d, 20));
        out.push_back(e);
    }
    // les demandes (recues, traitees le jour meme ou le lendemain)
    const int req = static_cast<int>(std::floor((work ? 2.5 : 0.3) * trend(d) + h01(d, 21)));
    for (int k = 0; k < req; ++k) {
        Ev e;
        e.kind = "demande";
        e.client = pickClient(h01(d, 22, k));
        out.push_back(e);
    }
    const int prev = static_cast<int>(std::floor((weekday(d - 1) < 5 ? 2.5 : 0.3) * trend(d - 1) + h01(d - 1, 21)));
    for (int k = 0; k < prev; ++k) {
        Ev e;
        e.kind = "traitee";
        e.client = pickClient(h01(d - 1, 22, k));
        e.admin = pickAdmin(d, h01(d - 1, 23, k));
        // le delai median baisse doucement (5 h -> 3 h 30 sur l'annee), avec des ecarts
        const double base = 5.2 - 1.6 * std::clamp(static_cast<double>(d - dayOf("2026-01-01")) / 315.0, 0.0, 1.2);
        e.delay = std::max(0.2, base * (0.35 + 1.5 * h01(d - 1, 24, k) * h01(d - 1, 25, k) + 0.4 * h01(d - 1, 26, k))) * 3600.0;
        out.push_back(e);
    }
    // les PC
    const int act = static_cast<int>(std::floor((work ? 1.15 : 0.15) * trend(d) + h01(d, 27)));
    for (int k = 0; k < act; ++k) {
        Ev e;
        e.kind = "pc_active";
        const double r = h01(d, 28, k);
        e.offer = kOffers[r < 0.27 ? 0 : r < 0.78 ? 1 : 2];
        e.client = pickClient(h01(d, 29, k));
        out.push_back(e);
    }
    if (work && h01(d, 30) < 0.62) {
        Ev e;
        e.kind = "pc_libere";
        e.offer = kOffers[h01(d, 31) < 0.3 ? 0 : 1];
        e.client = pickClient(h01(d, 32));
        out.push_back(e);
    }
    if (h01(d, 33) < 0.14) {
        Ev e;
        e.kind = "pc_retire";
        e.offer = kOffers[h01(d, 34) < 0.4 ? 0 : 1];
        e.client = pickClient(h01(d, 35));
        out.push_back(e);
    }
    // les Decouvertes
    if (h01(d, 36) < (work ? 0.45 : 0.1)) {
        Ev e;
        e.kind = "decouverte";
        e.client = pickClient(h01(d, 37));
        out.push_back(e);
    }
    if (h01(d, 38) < 0.085) {
        Ev e;
        e.kind = "convertie";
        e.client = pickClient(h01(d, 39));
        out.push_back(e);
    }
    return out;
}

// ---- les filtres et les divisions ----
struct Filter {
    int client{-1}, admin{-1};
    bool none{false};   // un client ou un admin inconnu : rien
};
bool keeps(const Filter& f, const Ev& e) {
    if (f.none) return false;
    if (f.client >= 0 && e.client != f.client) return false;
    if (f.admin >= 0 && e.admin != f.admin) return false;
    return true;
}
std::string keyOf(const std::string& split, const Ev& e) {
    if (split == "offre") return e.offer;
    if (split == "option") return e.option;
    if (split == "client") return kClients[e.client];
    if (split == "admin") return kAdminIds[e.admin];
    return {};
}

// Le genre et l'unite de chaque mesure (docs/STATISTIQUES.md, par. 5).
struct Info {
    const char* unit;
    const char* kind;
    const char* source;
};
Info infoOf(const std::string& m) {
    static const std::map<std::string, Info> t = {
        {"ventes_montant", {"centimes", "flux", "S"}},          {"ventes_cumul", {"centimes", "cumul", "S"}},
        {"ventes_commandes", {"nombre", "flux", "S"}},          {"ventes_panier", {"centimes", "ratio", "S"}},
        {"ventes_remboursements", {"centimes", "flux", "S"}},   {"ventes_remboursements_nombre", {"nombre", "flux", "S"}},
        {"licences_vendues_en_ligne", {"nombre", "flux", "S"}}, {"licences_signees_main", {"nombre", "flux", "S+N"}},
        {"licences_actives", {"nombre", "niveau", "S+N"}},      {"licences_nouvelles", {"nombre", "flux", "S+N"}},
        {"licences_prolongees", {"nombre", "flux", "S+N"}},     {"licences_options", {"nombre", "flux", "S+N"}},
        {"licences_expirees", {"nombre", "flux", "S"}},         {"licences_echeances_montant", {"centimes", "flux", "S"}},
        {"licences_revoquees", {"nombre", "flux", "S+N"}},      {"demandes_recues", {"nombre", "flux", "S+N"}},
        {"demandes_traitees", {"nombre", "flux", "N"}},         {"demandes_acceptees", {"nombre", "flux", "N"}},
        {"demandes_refusees", {"nombre", "flux", "N"}},         {"demandes_delai_median", {"secondes", "ratio", "N"}},
        {"demandes_delai_moyen", {"secondes", "ratio", "N"}},   {"demandes_delai_max", {"secondes", "ratio", "N"}},
        {"demandes_attente", {"nombre", "niveau", "S+N"}},      {"demandes_plus_ancienne", {"secondes", "niveau", "S+N"}},
        {"pc_actifs", {"nombre", "niveau", "S+N"}},             {"pc_actives", {"nombre", "flux", "S+N"}},
        {"pc_liberes", {"nombre", "flux", "S+N"}},              {"pc_retires", {"nombre", "flux", "S+N"}},
        {"jetons_pic", {"nombre", "max", "N"}},                 {"jetons_plafond", {"nombre", "niveau", "S+N"}},
        {"licences_au_plafond", {"nombre", "flux", "N"}},       {"decouvertes_commencees", {"nombre", "flux", "S"}},
        {"decouvertes_converties", {"nombre", "flux", "S+N"}},  {"decouvertes_en_cours", {"nombre", "niveau", "S+N"}},
        {"pc_vus", {"nombre", "niveau", "S+N"}},                {"pc_a_jour", {"nombre", "niveau", "S+N"}},
        {"maj_installees", {"nombre", "flux", "N"}},            {"admin_appels", {"nombre", "flux", "N"}},
        {"admin_jours", {"nombre", "flux", "N"}},
    };
    const auto it = t.find(m);
    return it == t.end() ? Info{"nombre", "flux", "S"} : it->second;
}

// Les divisions qu'une mesure connait (les autres : pas de parts).
bool splits(const std::string& m, const std::string& split) {
    if (split.empty()) return false;
    if (m.rfind("ventes_", 0) == 0) return split == "offre" || split == "option" || split == "client" || split == "admin";
    if (m == "licences_vendues_en_ligne" || m == "licences_signees_main" || m == "licences_nouvelles" || m == "licences_prolongees" || m == "licences_expirees" ||
        m == "licences_revoquees" || m == "licences_actives")
        return split == "offre" || split == "client" || split == "admin";
    if (m == "demandes_traitees") return split == "admin" || split == "client";
    if (m == "pc_actifs" || m == "pc_actives" || m == "pc_liberes" || m == "pc_retires") return split == "offre" || split == "client";
    if (m == "pc_vus") return split == "version";
    if (m == "admin_appels" || m == "admin_jours") return split == "admin";
    return false;
}

// ---- les versions (planche 09) ----
struct Version {
    const char* name;
    const char* published;   // "" : jamais publiee (1.43.2)
};
const Version kVersions[] = {{"1.43.2", ""}, {"1.44.0", "2026-07-14"}, {"1.45.0", "2026-08-18"}, {"1.45.2", "2026-09-01"}, {"1.46.0", "2026-09-29"}, {"1.47.0", "2026-11-04"}};
constexpr int kVersionCount = 6;

// La version du PC n de ce parc a la fin du jour d (un parc de `count` PC ; des PC qui trainent).
int versionOf(int n, int count, Day d, std::uint64_t salt) {
    int v = 0;
    const double lag = h01(n, salt, 1);
    for (int k = 1; k < kVersionCount; ++k) {
        const Day pub = dayOf(kVersions[k].published);
        if (d < pub) break;
        if (lag < 0.04) continue;                    // il reste sur une vieille version
        if (lag < 0.14 && k >= 3) continue;          // il ne passe pas la 1.45
        const double u = h01(n, salt + k, 2);
        const Day delay = static_cast<Day>(std::floor(-std::log(std::max(1e-6, 1.0 - u)) * (k == 5 ? 4.5 : 6.0 + 2.0 * (5 - k))));
        if (d >= pub + delay) v = k;
    }
    (void)count;
    return v;
}

// ---- une requete -> son JSON ----
class Sampler {
public:
    Sampler(const stats::SeriesQuery& q, Seconds now) : q_(q), now_(stats::dayStart(stats::dayOf(now)) / 86400) {
        if (!q.client.empty()) {
            f_.none = true;
            for (int i = 0; i < kClientCount; ++i)
                if (lower(kClients[i]) == lower(q.client)) {
                    f_.client = i;
                    f_.none = false;
                }
        }
        if (!q.admin.empty()) {
            f_.none = true;
            for (int i = 0; i < 4; ++i)
                if (q.admin == kAdminIds[i]) {
                    f_.admin = i;
                    f_.none = false;
                }
        }
    }

    std::string json() {
        stats::Period p = q_.period.empty() ? stats::lastDays(now_ * 86400, 30) : q_.period;
        const Day a = dayOf(p.from), b = dayOf(p.to);
        json::Value v = json::Value::object();
        v.set("version", "1.47.0");
        v.set("heure", static_cast<std::int64_t>(now_ * 86400 + 10 * 3600 + 42 * 60));
        v.set("releves_depuis", "2026-11-10");
        v.set("du", p.from);
        v.set("au", p.to);
        v.set("pas", stats::stepKey(q_.step));
        if (!q_.split.empty()) v.set("par", q_.split);
        v.set("comparer", stats::compareKey(q_.compare));
        const auto buckets = bucketsOf(a, b);
        v.set("periodes", bucketsJson(buckets));
        std::vector<std::pair<Day, Day>> before;
        if (q_.compare != stats::Compare::None) {
            for (const auto& [x, y] : buckets) before.push_back({shift(x), shift(y)});
            json::Value av = json::Value::object();
            av.set("du", dayText(shift(a)));
            av.set("au", dayText(shift(b)));
            av.set("periodes", bucketsJson(before));
            v.set("avant", av);
        }
        json::Value ms = json::Value::object();
        for (const auto& m : q_.measures) {
            json::Value mv = json::Value::object();
            const Info inf = infoOf(m);
            mv.set("unite", inf.unit);
            mv.set("genre", inf.kind);
            mv.set("source", inf.source);
            std::vector<double> pts;
            for (const auto& [x, y] : buckets) pts.push_back(value(m, x, y, nullptr));
            mv.set("points", numbers(pts));
            const double total = totalOf(m, pts, a, b, nullptr);
            mv.set("total", number(total));
            if (splits(m, q_.split)) {
                std::map<std::string, std::vector<double>> parts;
                for (const auto& key : keysOf(m, a, b)) {
                    std::vector<double> pp;
                    for (const auto& [x, y] : buckets) pp.push_back(value(m, x, y, &key));
                    parts[key] = pp;
                }
                std::vector<std::pair<double, std::string>> order;
                for (const auto& [key, pp] : parts) order.push_back({totalOf(m, pp, a, b, &key), key});
                std::sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
                json::Value pa = json::Value::array();
                for (const auto& [t, key] : order) {
                    json::Value pv = json::Value::object();
                    pv.set("cle", key);
                    pv.set("points", numbers(parts[key]));
                    pv.set("total", number(t));
                    pa.push(pv);
                }
                mv.set("parts", pa);
            }
            if (q_.compare != stats::Compare::None) {
                std::vector<double> bp;
                for (const auto& [x, y] : before) bp.push_back(value(m, x, y, nullptr));
                const double bt = totalOf(m, bp, shift(a), shift(b), nullptr);
                json::Value av = json::Value::object();
                av.set("points", numbers(bp));
                av.set("total", number(bt));
                mv.set("avant", av);
                mv.set("variation", stats::isNumber(total) && stats::isNumber(bt) && bt != 0 ? json::Value((total - bt) / bt * 100.0) : json::Value());
            }
            ms.set(m, mv);
        }
        v.set("mesures", ms);
        return json::write(v, false);
    }

private:
    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    Day shift(Day d) const {
        if (q_.compare == stats::Compare::LastYear) {
            int y = 0, m = 0, dd = 0;
            ch::fr::civil(d, y, m, dd);
            if (m == 2 && dd == 29) dd = 28;
            return ch::fr::day(y - 1, m, dd);
        }
        const Day a = dayOf(q_.period.empty() ? stats::lastDays(now_ * 86400, 30).from : q_.period.from);
        const Day b = dayOf(q_.period.empty() ? stats::lastDays(now_ * 86400, 30).to : q_.period.to);
        return d - (b - a + 1);
    }
    std::vector<std::pair<Day, Day>> bucketsOf(Day a, Day b) const {
        std::vector<std::pair<Day, Day>> out;
        Day d = a;
        while (d <= b) {
            Day e = d;
            if (q_.step == stats::Step::Week) e = d + (6 - weekday(d));
            else if (q_.step == stats::Step::Month) {
                int y = 0, m = 0, dd = 0;
                ch::fr::civil(d, y, m, dd);
                e = (m == 12 ? ch::fr::day(y + 1, 1, 1) : ch::fr::day(y, m + 1, 1)) - 1;
            }
            e = std::min(e, b);
            out.push_back({d, e});
            d = e + 1;
        }
        return out;
    }
    static json::Value bucketsJson(const std::vector<std::pair<Day, Day>>& b) {
        json::Value a = json::Value::array();
        for (const auto& [x, y] : b) {
            json::Value o = json::Value::object();
            o.set("du", dayText(x));
            o.set("au", dayText(y));
            a.push(o);
        }
        return a;
    }
    static json::Value number(double v) { return stats::isNumber(v) ? json::Value(std::round(v * 100.0) / 100.0) : json::Value(); }
    static json::Value numbers(const std::vector<double>& v) {
        json::Value a = json::Value::array();
        for (const double x : v) a.push(number(x));
        return a;
    }

    const std::vector<Ev>& events(Day d) {
        auto it = cache_.find(d);
        if (it == cache_.end()) it = cache_.emplace(d, eventsOf(d)).first;
        return it->second;
    }
    bool matches(const Ev& e, const std::string* key) const {
        if (!keeps(f_, e)) return false;
        return !key || keyOf(q_.split, e) == *key;
    }
    double count(Day d, const char* kind, const std::string* key) {
        double n = 0;
        for (const auto& e : events(d))
            if (e.kind == kind && matches(e, key)) n += 1;
        return n;
    }
    double cents(Day d, const char* kind, const std::string* key) {
        double n = 0;
        for (const auto& e : events(d))
            if (e.kind == kind && matches(e, key)) n += e.cents;
        return n;
    }
    // Un flux d'un jour.
    double daily(const std::string& m, Day d, const std::string* key) {
        if (d > now_ && m != "licences_expirees" && m != "licences_echeances_montant") return 0;
        if (m == "ventes_montant") return cents(d, "commande", key);
        if (m == "ventes_commandes") return count(d, "commande", key);
        if (m == "ventes_remboursements") return cents(d, "remboursement", key);
        if (m == "ventes_remboursements_nombre") return count(d, "remboursement", key);
        if (m == "licences_vendues_en_ligne") {
            double n = 0;
            for (const auto& e : events(d))
                if (e.kind == "commande" && e.offer != "options" && matches(e, key)) n += 1;
            return n;
        }
        if (m == "licences_signees_main") return count(d, "main", key);
        if (m == "licences_nouvelles") return count(d, "nouvelle", key);
        if (m == "licences_prolongees") return count(d, "prolongee", key);
        if (m == "licences_options") return count(d, "option", key);
        if (m == "licences_expirees") return count(d, "expiree", key);
        if (m == "licences_echeances_montant") return cents(d, "expiree", key);
        if (m == "licences_revoquees") return count(d, "revoquee", key);
        if (m == "demandes_recues") return count(d, "demande", key);
        if (m == "demandes_traitees") return count(d, "traitee", key);
        if (m == "demandes_acceptees" || m == "demandes_refusees") {
            double n = 0;
            for (const auto& e : events(d))
                if (e.kind == "traitee" && matches(e, key)) n += ((h01(d, 40, static_cast<std::uint64_t>(e.delay)) < 0.86) == (m == "demandes_acceptees")) ? 1 : 0;
            return n;
        }
        if (m == "pc_actives") return count(d, "pc_active", key);
        if (m == "pc_liberes") return count(d, "pc_libere", key);
        if (m == "pc_retires") return count(d, "pc_retire", key);
        if (m == "decouvertes_commencees") return count(d, "decouverte", key);
        if (m == "decouvertes_converties") return count(d, "convertie", key);
        if (m == "licences_au_plafond") return (f_.admin >= 0 || f_.none) ? 0 : peak(d) >= 20 ? 1 : 0;
        if (m == "admin_appels" || m == "admin_jours") {
            double n = 0;
            for (int i = 0; i < 4; ++i) {
                if (key && *key != kAdminIds[i]) continue;
                if (f_.admin >= 0 && f_.admin != i) continue;
                if (i == 3 && d >= dayOf("2026-06-01")) continue;
                const bool came = weekday(d) < 5 ? h01(d, 41, i) < (i == 0 ? 0.92 : i == 1 ? 0.85 : 0.6) : h01(d, 41, i) < 0.08;
                if (came) n += m == "admin_jours" ? 1.0 : std::round(180.0 + 260.0 * h01(d, 42, i));
            }
            return n;
        }
        return 0;
    }
    double peak(Day d) const {
        if (weekday(d) >= 5) return 1 + std::floor(3 * h01(d, 43));
        if (h01(d, 44) < 0.045) return 20;
        return std::min(19.0, std::round(6.0 + 0.012 * static_cast<double>(d - dayOf("2026-08-01")) + 6.0 * h01(d, 45)));
    }
    // La part d'un niveau (une offre, un client, un admin) : un nombre stable.
    double levelShare(const std::string* key) const {
        double s = 1.0;
        if (f_.none) return 0;
        if (f_.client >= 0) s *= kClientWeight[f_.client] * 0.75;
        if (f_.admin >= 0) s *= f_.admin == 0 ? 0.46 : f_.admin == 1 ? 0.32 : f_.admin == 2 ? 0.17 : 0.05;
        if (key) {
            if (q_.split == "offre") s *= *key == "PERSONAL" ? 0.27 : *key == "PROFESSIONAL" ? 0.50 : *key == "ENTERPRISE" ? 0.18 : 0.0;
            else if (q_.split == "client") {
                for (int i = 0; i < kClientCount; ++i)
                    if (*key == kClients[i]) s *= kClientWeight[i];
            } else if (q_.split == "admin") {
                for (int i = 0; i < 4; ++i)
                    if (*key == kAdminIds[i]) s *= i == 0 ? 0.46 : i == 1 ? 0.32 : i == 2 ? 0.17 : 0.05;
            }
        }
        return s;
    }
    // Un niveau a la fin du jour d : l'ancre de maintenant, refaite par les flux (entrees - sorties).
    double cumulative(const std::vector<std::pair<const char*, int>>& flows, Day d, const std::string* key) {
        double s = 0;
        const Day a = std::min(d, now_), b = std::max(d, now_);
        for (Day x = a + 1; x <= b; ++x)
            for (const auto& [kind, sign] : flows) s += sign * count(x, kind, key);
        return d < now_ ? -s : s;
    }
    double level(const std::string& m, Day d, const std::string* key) {
        if (d > now_) d = now_;
        const double share = levelShare(key);
        if (m == "licences_actives") {
            if (q_.split == "offre" && key && share == 0) return 0;
            const double anchor = std::round(128.0 * share);
            return std::max(0.0, anchor + cumulative({{"nouvelle", 1}, {"expiree", -1}, {"revoquee", -1}}, d, key));
        }
        if (m == "pc_actifs") {
            const double anchor = std::round(286.0 * share);
            return std::max(0.0, anchor + cumulative({{"pc_active", 1}, {"pc_libere", -1}, {"pc_retire", -1}}, d, key));
        }
        if (m == "ventes_cumul") {
            int y = 0, mo = 0, dd = 0;
            ch::fr::civil(d, y, mo, dd);
            double s = 0;
            for (Day x = ch::fr::day(y, 1, 1); x <= d; ++x) s += cents(x, "commande", key);
            return s;
        }
        if (m == "demandes_attente") return f_.admin >= 0 ? 0 : std::round((3.0 + 4.0 * h01(d, 46)) * (f_.client >= 0 ? 0.1 : 1.0));
        if (m == "demandes_plus_ancienne") return std::round((4.0 + 20.0 * h01(d, 47)) * 3600.0);
        if (m == "jetons_plafond") return 20;
        if (m == "decouvertes_en_cours") return std::round((10.0 + 5.0 * std::sin(static_cast<double>(d) / 23.0) * 0.5 + 2.5 + h01(d, 48)) * (f_.client >= 0 ? 0.1 : 1.0));
        if (m == "pc_vus" || m == "pc_a_jour") {
            const int count = f_.client >= 0 ? (f_.client == 0 ? 9 : 4 + f_.client % 5) : 289;
            int seen = 0, upToDate = 0;
            int latest = 0;
            for (int k = 1; k < kVersionCount; ++k)
                if (d >= dayOf(kVersions[k].published)) latest = k;
            for (int n = 0; n < count; ++n) {
                const int v = versionOf(n, count, d, 500 + static_cast<std::uint64_t>(f_.client + 1));
                if (key && *key != kVersions[v].name) continue;
                ++seen;
                upToDate += v == latest ? 1 : 0;
            }
            return m == "pc_vus" ? seen : upToDate;
        }
        return 0;
    }
    double value(const std::string& m, Day a, Day b, const std::string* key) {
        const Info inf = infoOf(m);
        const std::string kind = inf.kind;
        if (kind == "niveau" || kind == "cumul") return level(m, b, key);
        if (kind == "max") {
            double mx = 0;
            for (Day d = a; d <= std::min(b, now_); ++d) mx = std::max(mx, peak(d));
            return mx;
        }
        if (kind == "ratio") {
            if (m == "ventes_panier") {
                double s = 0, n = 0;
                for (Day d = a; d <= b; ++d) {
                    s += daily("ventes_montant", d, key);
                    n += daily("ventes_commandes", d, key);
                }
                return n > 0 ? s / n : ch::kNone;
            }
            std::vector<double> delays;
            for (Day d = a; d <= std::min(b, now_); ++d)
                for (const auto& e : events(d))
                    if (e.kind == "traitee" && matches(e, key)) delays.push_back(e.delay);
            if (delays.empty()) return ch::kNone;
            std::sort(delays.begin(), delays.end());
            if (m == "demandes_delai_median") return delays[delays.size() / 2];
            if (m == "demandes_delai_max") return delays.back();
            double s = 0;
            for (const double x : delays) s += x;
            return s / static_cast<double>(delays.size());
        }
        double s = 0;
        for (Day d = a; d <= b; ++d) s += daily(m, d, key);
        return s;
    }
    double totalOf(const std::string& m, const std::vector<double>& pts, Day a, Day b, const std::string* key) {
        const std::string kind = infoOf(m).kind;
        if (kind == "niveau" || kind == "cumul") return pts.empty() ? ch::kNone : pts.back();
        if (kind == "ratio" || kind == "max") return value(m, a, b, key);
        double s = 0;
        for (const double x : pts)
            if (stats::isNumber(x)) s += x;
        return s;
    }
    std::vector<std::string> keysOf(const std::string& m, Day a, Day b) {
        std::vector<std::string> keys;
        if (q_.split == "offre") {
            keys = {"PERSONAL", "PROFESSIONAL", "ENTERPRISE"};
            if (m.rfind("ventes_", 0) == 0) keys.push_back("options");
        } else if (q_.split == "option") {
            keys = {kOptions[0], kOptions[1], kOptions[2], ""};
        } else if (q_.split == "client") {
            for (const char* c : kClients) keys.push_back(c);
        } else if (q_.split == "admin") {
            for (const char* id : kAdminIds) keys.push_back(id);
        } else if (q_.split == "version") {
            for (const auto& v : kVersions) keys.push_back(v.name);
        }
        (void)a;
        (void)b;
        return keys;
    }

    stats::SeriesQuery         q_;
    Day                        now_;
    Filter                     f_;
    std::map<Day, std::vector<Ev>> cache_;
};

json::Value curve(const char* version, const char* published, const char* software, int base, std::vector<int> days) {
    json::Value c = json::Value::object();
    c.set("version", version);
    c.set("publiee", static_cast<std::int64_t>(stats::dayStart(published)));
    c.set("logiciel", software);
    c.set("base", base);
    json::Value a = json::Value::array();
    for (const int d : days) a.push(d);
    c.set("jours", a);
    return c;
}

std::vector<int> adoption(int base, double top, double k, int days) {
    std::vector<int> out;
    for (int i = 0; i <= days; ++i) out.push_back(static_cast<int>(std::round(base * top * (1.0 - std::exp(-k * (i + 0.6))))));
    return out;
}

} // namespace

std::string sampleSeriesJson(const stats::SeriesQuery& q, Seconds now) {
    Sampler s(q, now);
    return s.json();
}

std::string sampleUpdatesJson(const stats::UpdatesQuery& q, Seconds now) {
    const Day today = stats::dayStart(stats::dayOf(now)) / 86400;
    json::Value v = json::Value::object();
    v.set("version", "1.47.0");
    v.set("heure", static_cast<std::int64_t>(today * 86400 + 38520));
    v.set("releves_depuis", "2026-11-10");
    json::Value last = json::Value::object();
    last.set("version", "1.47.0");
    last.set("publiee", static_cast<std::int64_t>(stats::dayStart("2026-11-04")));
    v.set("derniere", last);
    const struct {
        const char* v;
        const char* pub;
        int         st, ad;
    } rows[] = {{"1.47.0", "2026-11-04", 168, 2}, {"1.46.0", "2026-09-29", 81, 0}, {"1.45.2", "2026-09-01", 19, 1},
                {"1.45.0", "2026-08-18", 4, 0},   {"1.44.0", "2026-07-14", 8, 0},  {"1.43.2", "", 6, 0}};
    json::Value vs = json::Value::array();
    for (const auto& r : rows) {
        json::Value o = json::Value::object();
        o.set("version", r.v);
        o.set("publiee", static_cast<std::int64_t>(*r.pub ? stats::dayStart(r.pub) : 0));
        o.set("studios", r.st);
        o.set("admins", r.ad);
        vs.push(o);
    }
    v.set("versions", vs);
    stats::Period p = q.period.empty() ? stats::lastDays(now, 90) : q.period;
    json::Value seen = json::Value::object();
    seen.set("studios", 286);
    seen.set("admins", 3);
    seen.set("depuis", p.from);
    v.set("vus", seen);
    json::Value up = json::Value::object();
    up.set("studios", 168);
    up.set("admins", 2);
    v.set("a_jour", up);
    json::Value ad = json::Value::array();
    const int days = std::clamp(q.days, 1, 120);
    const Day d147 = dayOf("2026-11-04");
    ad.push(curve("1.44.0", "2026-07-14", "studios", 270, adoption(270, 0.77, 0.10, std::min(days, 35))));
    ad.push(curve("1.45.0", "2026-08-18", "studios", 276, adoption(276, 0.87, 0.115, std::min(days, 14))));
    ad.push(curve("1.45.2", "2026-09-01", "studios", 279, adoption(279, 0.88, 0.12, std::min(days, 28))));
    ad.push(curve("1.46.0", "2026-09-29", "studios", 283, adoption(283, 0.94, 0.15, std::min(days, 36))));
    ad.push(curve("1.47.0", "2026-11-04", "studios", 286, adoption(286, 0.9, 0.175, std::min<int>(days, static_cast<int>(std::max<Day>(0, today - d147))))));
    ad.push(curve("1.47.0", "2026-11-04", "admins", 3, {1, 2, 2, 2, 2, 2, 2}));
    v.set("adoption", ad);
    json::Value dl = json::Value::array();
    const struct {
        const char* v;
        double      med, mean;
        int         n;
    } delays[] = {{"1.44.0", 9, 14, 210}, {"1.45.0", 6, 11, 224}, {"1.45.2", 5, 9, 236}, {"1.46.0", 4, 8, 248}, {"1.47.0", 2, 2.6, 120}};
    for (const auto& x : delays) {
        json::Value o = json::Value::object();
        o.set("version", x.v);
        o.set("logiciel", "studios");
        o.set("nombre", x.n);
        o.set("median", x.med * 86400.0);
        o.set("moyen", x.mean * 86400.0);
        dl.push(o);
    }
    v.set("delais", dl);
    // la part a jour, chaque periode du pas : une dent de scie (chaque publication la fait tomber)
    json::Value per = json::Value::array();
    json::Value sUp = json::Value::array(), sSeen = json::Value::array(), aUp = json::Value::array(), aSeen = json::Value::array();
    const Day a = dayOf(p.from), b = dayOf(p.to);
    Day d = a;
    const Day pubs[] = {dayOf("2026-07-14"), dayOf("2026-08-18"), dayOf("2026-09-01"), dayOf("2026-09-29"), dayOf("2026-11-04")};
    while (d <= b) {
        Day e = d;
        if (q.step == stats::Step::Week) e = d + (6 - weekday(d));
        else if (q.step == stats::Step::Month) {
            int y = 0, m = 0, dd = 0;
            ch::fr::civil(d, y, m, dd);
            e = (m == 12 ? ch::fr::day(y + 1, 1, 1) : ch::fr::day(y, m + 1, 1)) - 1;
        }
        e = std::min(e, b);
        json::Value o = json::Value::object();
        o.set("du", dayText(d));
        o.set("au", dayText(e));
        per.push(o);
        Day lastPub = -100000;
        for (const Day x : pubs)
            if (e >= x) lastPub = x;
        const double t = static_cast<double>(e - lastPub);
        const double share = lastPub < 0 ? 0.84 : 0.04 + 0.9 * (1.0 - std::exp(-0.1 * t));
        sUp.push(static_cast<int>(std::round(286 * share)));
        sSeen.push(286);
        aUp.push(lastPub < 0 ? 3 : t < 2 ? 1 : t < 9 ? 2 : 3);
        aSeen.push(3);
        d = e + 1;
    }
    v.set("periodes", per);
    json::Value part = json::Value::object();
    json::Value ps = json::Value::object();
    ps.set("a_jour", sUp);
    ps.set("vus", sSeen);
    json::Value pa = json::Value::object();
    pa.set("a_jour", aUp);
    pa.set("vus", aSeen);
    part.set("studios", ps);
    part.set("admins", pa);
    v.set("part", part);
    json::Value behind = json::Value::array();
    const struct {
        const char* name;
        const char* customer;
        const char* version;
        int         retard;
        long        ago;   // secondes
        const char* admin;
    } pcs[] = {{"ATELIER-PC3", "Atelier Exemple", "1.43.2", 5, 41 * 86400L, ""},
               {"NORD-LIGNE-07", "Usine Exemple Nord", "1.44.0", 4, 30 * 3600L, ""},
               {"BE-CALCUL-02", "Bureau d'\xC3\xA9tudes Exemple", "1.44.0", 4, 3 * 3600L, ""},
               {"ESSAI-PORTABLE", "Client d'essai", "1.45.0", 3, 12 * 86400L, ""},
               {"PORTABLE-HUGO", "Hugo Essai", "1.45.2", 2, 20 * 60L, "xpg-4e60000000000003"},
               {"ATELIER-PC5", "Atelier Exemple", "1.46.0", 1, 5 * 3600L, ""},
               {"LABO-MESURE-1", "Laboratoire Exemple", "1.46.0", 1, 2 * 86400L, ""},
               {"GARAGE-ATELIER", "Garage d'essai", "1.46.0", 1, 6 * 86400L, ""},
               {"IMPRIM-PAO", "Imprimerie Exemple", "1.46.0", 1, 33 * 86400L, ""}};
    for (const auto& x : pcs) {
        json::Value o = json::Value::object();
        o.set("cle", std::string(64, 'a'));
        o.set("logiciel", *x.admin ? "XPG-License-Admin" : "IHM-Studio");
        o.set("version", x.version);
        o.set("retard", x.retard);
        o.set("vu", static_cast<std::int64_t>(now - x.ago));
        o.set("nom", x.name);
        o.set("client", *x.admin ? "" : x.customer);
        o.set("licence", *x.admin ? "" : "XPG-2024-00031");
        o.set("admin", x.admin);
        behind.push(o);
    }
    v.set("pas_a_jour", behind);
    v.set("pas_a_jour_total", 119);
    (void)q.software;
    return json::write(v, false);
}

std::string sampleAdminsJson(const stats::Period& p, Seconds now) {
    json::Value v = json::Value::object();
    v.set("du", p.from);
    v.set("au", p.to);
    v.set("releves_depuis", "2026-11-10");
    json::Value a = json::Value::array();
    const int starts[4] = {7, 7, 8, 8}, ends[4] = {17, 16, 17, 15};
    for (int i = 0; i < 4; ++i) {
        json::Value o = json::Value::object();
        o.set("id", kAdminIds[i]);
        o.set("cle", std::string(64, static_cast<char>('a' + i)));
        o.set("role", i == 0 ? "admin+" : "admin");
        o.set("nom", kAdminNames[i]);
        o.set("retire", static_cast<std::int64_t>(i == 3 ? stats::dayStart("2026-06-01") : 0));
        json::Value days = json::Value::array();
        json::Value hours = json::Value::array();
        int calls = 0;
        for (int h = 0; h < 24; ++h) {
            const int n = (i == 3) ? 0 : (h >= starts[i] && h <= ends[i]) ? 20 + (h * 7 + i * 13) % 40 : 0;
            hours.push(n);
            calls += n;
        }
        if (i != 3)
            for (Day d = dayOf(p.from); d <= dayOf(p.to); ++d)
                if (weekday(d) < 5 && h01(d, 41, i) < (i == 0 ? 0.92 : i == 1 ? 0.85 : 0.6)) days.push(dayText(d));
        o.set("jours", days);
        o.set("heures", hours);
        o.set("appels", calls);
        o.set("debut_habituel", i == 3 ? -1 : starts[i]);
        o.set("fin_habituelle", i == 3 ? -1 : ends[i]);
        o.set("vu", static_cast<std::int64_t>(i == 3 ? stats::dayStart("2026-05-29") + 15 * 3600 : now - (i + 1) * 1800));
        o.set("logiciel", "XPG-License-Admin");
        o.set("logiciel_version", i == 2 ? "1.45.2" : i == 3 ? "1.44.0" : "1.47.0");
        o.set("ville", i == 0 ? "Lyon" : i == 1 ? "Paris" : i == 2 ? "Toulouse" : "Nantes");
        o.set("pays", "FR");
        a.push(o);
    }
    v.set("admins", a);
    return json::write(v, false);
}

Fetchers sampleFetchers(std::function<Seconds()> now) {
    Fetchers f;
    const auto clock = [now] { return now ? now() : static_cast<Seconds>(std::time(nullptr)); };
    f.series = [clock](const stats::SeriesQuery& q, stats::SeriesReply& out) {
        json::Value v;
        std::string why;
        if (!json::parse(sampleSeriesJson(q, clock()), v, &why) || !stats::parseSeries(v, out, &why)) return OpResult::fail(why);
        return OpResult::done({});
    };
    f.updates = [clock](const stats::UpdatesQuery& q, stats::UpdatesReply& out) {
        json::Value v;
        std::string why;
        if (!json::parse(sampleUpdatesJson(q, clock()), v, &why) || !stats::parseUpdates(v, out, &why)) return OpResult::fail(why);
        return OpResult::done({});
    };
    f.admins = [clock](const stats::Period& p, stats::AdminsReply& out) {
        json::Value v;
        std::string why;
        const stats::Period pp = p.empty() ? stats::lastDays(clock(), 30) : p;
        if (!json::parse(sampleAdminsJson(pp, clock()), v, &why) || !stats::parseAdmins(v, out, &why)) return OpResult::fail(why);
        return OpResult::done({});
    };
    f.snapshot = [clock](stats::SnapshotReply& out) {
        out.day = stats::dayOf(clock());
        out.taken = true;
        return OpResult::done("Relev\xC3\xA9 du " + ddmmyyyy(out.day) + " pris.");
    };
    return f;
}

void installSamplesFromEnvironment(std::function<Seconds()> now) {
    static bool done = false;
    if (done) return;
    done = true;
    const char* v = std::getenv("XPG_STATS_ESSAI");
    if (!v || std::string(v) != "1") return;
    setFetchersForTests(sampleFetchers(std::move(now)));
}

} // namespace license_admin::gui::statskit
