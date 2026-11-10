// =============================================================================
//  license_admin/JournalStats.cpp - 1.47.0 : voir JournalStats.hpp
// =============================================================================
#include "JournalStats.hpp"

#include "gui/Workspace.hpp"

#include "../core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace license_admin::journalstats {

namespace adm = licensing::admins;
using core::json::Value;

bool known(double v) noexcept { return !std::isnan(v); }

// ------------------------------------------------------------------------------------- l'heure ---
namespace {

constexpr Seconds kDay = 86400;

std::int64_t floorDiv(std::int64_t a, std::int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

// L'algorithme civil de H. Hinnant (comme licensing/Time).
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

unsigned monthLength(std::int64_t y, unsigned m) {
    static const unsigned n[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return n[m - 1];
}

// Le dernier dimanche du mois (son jour).
std::int64_t lastSunday(std::int64_t y, unsigned m) {
    const std::int64_t last = daysFromCivil(y, m, monthLength(y, m));
    return last - ((weekday(last) - 6 + 7) % 7);
}

} // namespace

int weekday(std::int64_t day) noexcept { return static_cast<int>(((day % 7) + 7 + 3) % 7); }   // le 01/01/1970 : un jeudi

int parisOffset(Seconds utc) noexcept {
    std::int64_t y;
    unsigned m, d;
    civilFromDays(floorDiv(utc, kDay), y, m, d);
    const Seconds start = lastSunday(y, 3) * kDay + 3600;    // 01:00 UTC
    const Seconds end = lastSunday(y, 10) * kDay + 3600;
    return utc >= start && utc < end ? 7200 : 3600;
}

TimeZone TimeZone::paris() { return TimeZone{[](Seconds t) { return parisOffset(t); }}; }
TimeZone TimeZone::fixed(int seconds) { return TimeZone{[seconds](Seconds) { return seconds; }}; }

std::int64_t localDay(Seconds utc, const TimeZone& tz) { return floorDiv(utc + tz.at(utc), kDay); }
int localMinuteOfDay(Seconds utc, const TimeZone& tz) { return static_cast<int>((utc + tz.at(utc) - localDay(utc, tz) * kDay) / 60); }
int localHour(Seconds utc, const TimeZone& tz) { return localMinuteOfDay(utc, tz) / 60; }

Seconds dayStart(std::int64_t day, const TimeZone& tz) {
    const Seconds guess = day * kDay - tz.at(day * kDay - 3600);
    const int off = tz.at(guess);
    return day * kDay - off;
}

std::string dayText(std::int64_t day) {
    std::int64_t y;
    unsigned m, d;
    civilFromDays(day, y, m, d);
    char b[16];
    std::snprintf(b, sizeof b, "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return b;
}

bool parseDay(std::string_view t, std::int64_t& day) noexcept {
    if (t.size() != 10 || t[4] != '-' || t[7] != '-') return false;
    int v[3] = {0, 0, 0};
    const std::size_t at[3] = {0, 5, 8}, len[3] = {4, 2, 2};
    for (int k = 0; k < 3; ++k)
        for (std::size_t i = 0; i < len[k]; ++i) {
            const char c = t[at[k] + i];
            if (c < '0' || c > '9') return false;
            v[k] = v[k] * 10 + (c - '0');
        }
    if (v[1] < 1 || v[1] > 12 || v[2] < 1 || static_cast<unsigned>(v[2]) > monthLength(v[0], static_cast<unsigned>(v[1]))) return false;
    day = daysFromCivil(v[0], static_cast<unsigned>(v[1]), static_cast<unsigned>(v[2]));
    return true;
}

// --------------------------------------------------------------------------------- la periode ---
Period lastDays(Seconds now, int days, const TimeZone& tz) {
    const std::int64_t today = localDay(now, tz);
    return Period{today - std::max(1, days) + 1, today};
}

Period lastMonths(Seconds now, int months, const TimeZone& tz) {
    const std::int64_t today = localDay(now, tz);
    std::int64_t y;
    unsigned m, d;
    civilFromDays(today, y, m, d);
    std::int64_t total = y * 12 + (m - 1) - std::max(1, months);
    const std::int64_t py = floorDiv(total, 12);
    const unsigned pm = static_cast<unsigned>(total - py * 12) + 1;
    const unsigned pd = std::min(d, monthLength(py, pm));
    return Period{daysFromCivil(py, pm, pd) + 1, today};
}

bool between(std::string_view from, std::string_view to, Period& out) noexcept {
    std::int64_t a = 0, b = 0;
    if (!parseDay(from, a) || !parseDay(to, b) || b < a) return false;
    out = Period{a, b};
    return true;
}

Period previousOf(const Period& p) noexcept {
    if (p.empty()) return {};
    const int n = p.days();
    return Period{p.first - n, p.first - 1};
}

// --------------------------------------------------------------------------- les types d'action ---
const std::array<TypeInfo, kTypes>& types() {
    static const std::array<TypeInfo, kTypes> t{{
        {"licence_creer", "Cr\xC3\xA9" "er et signer une licence"},
        {"licence_prolonger", "Prolonger une licence"},
        {"licence_option", "Ajouter une option"},
        {"licence_revoquer", "R\xC3\xA9voquer une licence"},
        {"poste_retirer", "Retirer un PC"},
        {"offres", "Modifier les offres"},
        {"options", "Cr\xC3\xA9" "er, modifier une option"},
        {"annonces", "Annonces"},
        {"demandes", "Accepter, refuser une demande"},
        {"codes", "Revoir le code"},
        {"client_supprimer", "Supprimer un client"},
        {"clients_exporter", "Exporter les clients"},
        {"courriels", "\xC3\x89" "crire aux clients"},
        {"boutique", "Boutique : livrer, revoir"},
    }};
    return t;
}

int typeOf(std::string_view a) noexcept {
    if (a == "licence_creer") return 0;
    if (a == "licence_prolonger") return 1;
    if (a == "licence_option") return 2;
    if (a == "licence_revoquer") return 3;
    if (a == "poste_retirer") return 4;
    if (a == "offres") return 5;
    if (a == "options") return 6;
    if (a == "annonces") return 7;
    if (a == "demande_accepter" || a == "demande_refuser") return 8;
    if (a == "code_revoir") return 9;
    if (a == "client_supprimer" || a == "client_restaurer") return 10;
    if (a == "clients_exporter") return 11;
    if (a == "courriel") return 12;
    if (a == "commande_livrer" || a == "commande_revoir") return kShop;
    return -1;
}

bool reserved(std::string_view a) noexcept {
    return a == "admin_ajouter" || a == "admin_retirer" || a == "admin_droits" || a == "admin_profil" || a == "version_publier" ||
           a == "fichier_envoyer" || a == "criticite" || a == "boutique" || a == "stripe_brancher" || a == "commande_rembourser" ||
           a == "sauvegarde" || a == "restauration";
}

const char* severityLabel(Severity s) noexcept {
    switch (s) {
        case Severity::Critical: return "Critique";
        case Severity::High: return "Haute";
        default: return "\xC3\x80 voir";
    }
}

// --------------------------------------------------------------------------------------- les calculs ---
namespace {

bool serviceAction(std::string_view a) {
    return a == "signature_refusee" || a == "autre_pc" || a == "demande_admin" || a == "sauvegarde" || a == "restauration";
}

Value objectOf(const std::string& text) {
    Value v;
    if (text.empty() || !core::json::parse(text, v, nullptr) || !v.isObject()) return Value::object();
    return v;
}

std::vector<std::string> rightsIn(const Value& o) {
    std::vector<std::string> out;
    if (const Value* a = o.find("droits"); a && a->isArray())
        for (const auto& x : a->items())
            if (x.isString()) out.push_back(x.asString());
    return adm::normalizeRights(out);
}

Ref refOf(const Line& l) { return Ref{l.number, l.at(), l.action, l.object, l.summary}; }

// "1.45.2" < "1.47.0"
bool versionLess(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i++] - '0');
        while (j < b.size() && b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j++] - '0');
        if (x != y) return x < y;
        if (i < a.size()) ++i;
        if (j < b.size()) ++j;
    }
    return false;
}

const char* kWeekdays[] = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};

std::string dayMonth(std::int64_t day) {
    std::int64_t y;
    unsigned m, d;
    civilFromDays(day, y, m, d);
    char b[16];
    std::snprintf(b, sizeof b, "%02u/%02u", d, m);
    return b;
}
std::string clock(Seconds t, const TimeZone& tz) {
    const int mn = localMinuteOfDay(t, tz);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d", mn / 60, mn % 60);
    return b;
}
std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Ce qui est calcule une fois pour toutes les lignes : a qui elle est, son jour, sa minute.
struct Prepared {
    const Inputs*            in{nullptr};
    Period                   period, previous;
    std::int64_t             today{0};
    std::vector<int>         owner;        // l'index de l'admin a qui la ligne revient (-1 : personne)
    std::vector<bool>        signedBy;     // signee par lui (sinon : ecrite par le service a son sujet)
    std::vector<std::int64_t> day;
    std::vector<int>         minute;
    std::map<std::string, int> byKey, byId;
    int                      plus{-1};
};

Prepared prepare(const Inputs& in) {
    Prepared p;
    p.in = &in;
    p.today = localDay(in.now, in.tz);
    p.period = in.period.empty() && in.period.first == 0 && in.period.last == -1 ? lastDays(in.now, 30, in.tz) : in.period;
    p.previous = previousOf(p.period);
    for (std::size_t i = 0; i < in.admins.size(); ++i) {
        for (const auto& k : in.admins[i].keys) p.byKey[k] = static_cast<int>(i);
        p.byId[in.admins[i].id] = static_cast<int>(i);
        if (in.admins[i].plus && p.plus < 0) p.plus = static_cast<int>(i);
    }
    const std::size_t n = in.lines.size();
    p.owner.assign(n, -1);
    p.signedBy.assign(n, false);
    p.day.assign(n, 0);
    p.minute.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& l = in.lines[i];
        p.day[i] = localDay(l.at(), in.tz);
        p.minute[i] = localMinuteOfDay(l.at(), in.tz);
        if (const auto it = p.byKey.find(l.key); it != p.byKey.end() && !(l.who == "le service")) {
            p.owner[i] = it->second;
            p.signedBy[i] = true;
            continue;
        }
        // Une ligne du service : a qui elle revient.
        if (l.action == "signature_refusee" || l.action == "autre_pc") {
            const Value d = objectOf(l.detail);
            if (const auto it = p.byKey.find(d.str("cle")); it != p.byKey.end()) p.owner[i] = it->second;
            else if (const auto jt = p.byId.find(l.object); jt != p.byId.end()) p.owner[i] = jt->second;
        } else if (l.action == "sauvegarde" || l.action == "restauration") {
            p.owner[i] = p.plus;   // l'admin+ seul les demande
        }
    }
    return p;
}

// Les jours ouvres d'affilee avec une action, jusqu'a `from` (le samedi et le dimanche ne coupent pas la serie).
int streakEndingAt(const std::set<std::int64_t>& active, std::int64_t from) {
    int n = 0;
    for (std::int64_t d = from;; --d) {
        if (weekday(d) >= 5) {
            if (from - d > 4000) break;
            continue;
        }
        if (!active.count(d)) break;
        ++n;
    }
    return n;
}

AdminStats compute(const Prepared& p, int idx) {
    const Inputs& in = *p.in;
    const Admin& a = in.admins[static_cast<std::size_t>(idx)];
    const Options& o = in.options;
    AdminStats s;
    s.id = a.id;
    s.name = a.name;
    s.plus = a.plus;
    s.retired = a.retired != 0;
    const int nDays = p.period.days();
    s.perDay.assign(static_cast<std::size_t>(nDays), 0);
    s.perDayBefore.assign(static_cast<std::size_t>(nDays), 0);
    std::set<std::int64_t> activeAll, activeNow, activeBefore;
    std::array<Seconds, kTypes> lastOfType{};
    // La fenetre des habitudes, et celle de << jamais utilise >>.
    const Period habits{p.period.last - std::max(1, o.habitsDays) + 1, p.period.last};
    std::vector<int> habitMinutes;
    std::map<std::int64_t, int> habitDays;
    std::map<std::int64_t, int> nowCount;
    std::set<std::uint64_t> otherPcLines;
    std::set<std::string> licencesNow, licencesBefore;
    int requestsNow = 0, requestsBefore = 0;
    bool broken = false;

    // ---- les changements de ses droits (qui, quand) : tout le journal ----
    std::map<std::string, Seconds> given;   // le droit -> la derniere fois qu'il lui a ete donne
    for (std::size_t i = 0; i < in.lines.size(); ++i) {
        const auto& l = in.lines[i];
        if (l.object != a.id || (l.action != "admin_droits" && l.action != "admin_ajouter" && l.action != "admin_retirer")) continue;
        RightsChange c;
        c.line = refOf(l);
        const int by = p.owner[i] >= 0 && p.signedBy[i] ? p.owner[i] : -1;
        c.by = by >= 0 ? in.admins[static_cast<std::size_t>(by)].name : (l.who.empty() ? std::string("cl\xC3\xA9 inconnue") : l.who);
        const auto before = rightsIn(objectOf(l.before)), after = rightsIn(objectOf(l.after));
        for (const auto& r : after)
            if (std::find(before.begin(), before.end(), r) == before.end()) {
                c.added.push_back(r);
                given[r] = l.at();
            }
        for (const auto& r : before)
            if (std::find(after.begin(), after.end(), r) == after.end()) c.removed.push_back(r);
        c.profile = objectOf(l.detail).str("profil");
        c.joined = l.action == "admin_ajouter";
        c.left = l.action == "admin_retirer";
        s.rightsChanges.push_back(std::move(c));
    }

    for (std::size_t i = 0; i < in.lines.size(); ++i) {
        if (p.owner[i] != idx) continue;
        const auto& l = in.lines[i];
        const std::int64_t d = p.day[i];
        const bool inNow = p.period.contains(d), inBefore = p.previous.contains(d);
        if (!p.signedBy[i]) {
            // ---- ce que le service a ecrit a son sujet ----
            if (l.action == "signature_refusee" && inNow) {
                const Value det = objectOf(l.detail);
                Refusal r;
                r.line = refOf(l);
                r.route = det.str("route");
                r.right = det.str("droit");
                r.reason = det.str("raison") == "droit" || !r.right.empty() ? RefusalReason::MissingRight : RefusalReason::RemovedKey;
                s.refused.push_back(std::move(r));
            } else if (l.action == "autre_pc" && inNow) {
                const Value det = objectOf(l.detail);
                const auto ref = static_cast<std::uint64_t>(det.integer("ligne"));
                if (!otherPcLines.count(ref)) {
                    otherPcLines.insert(ref);
                    OtherPc x;
                    x.line = refOf(l);
                    if (ref) x.line.number = ref;
                    x.pc = det.str("pc");
                    x.machine = det.str("poste_entree");
                    x.usualPc = a.pc;
                    s.otherPc.push_back(std::move(x));
                }
            }
            if (l.action != "sauvegarde" && l.action != "restauration") continue;   // la sauvegarde : un geste de l'admin+
        } else {
            // ---- ses lignes : la chaine ----
            ++s.chain.lines;
            if (l.verified) ++s.chain.verified;
            if (!l.chainOk && !broken) {
                broken = true;
                s.chain.firstBreak = l.number;
            }
            s.chain.last = std::max(s.chain.last, l.number);
        }
        if (l.action == "connexion") continue;   // une connexion n'est pas un geste
        const int t = typeOf(l.action);
        activeAll.insert(d);
        if (l.at() >= s.last.when) s.last = refOf(l);
        if (t >= 0 && t < 13) lastOfType[static_cast<std::size_t>(t)] = std::max(lastOfType[static_cast<std::size_t>(t)], l.at());
        if (t == kShop) lastOfType[kShop] = std::max(lastOfType[kShop], l.at());
        if (habits.contains(d)) {
            habitMinutes.push_back(p.minute[i]);
            ++habitDays[d];
        }
        // Depuis un autre PC que le sien (l'identifiant de la liste) : meme sans la ligne du service.
        if (inNow && p.signedBy[i] && !a.machine.empty() && !l.machine.empty() && l.machine != a.machine && !otherPcLines.count(l.number)) {
            otherPcLines.insert(l.number);
            s.otherPc.push_back(OtherPc{refOf(l), l.pc, l.machine, a.pc});
        }
        if (inNow) {
            ++s.actions;
            activeNow.insert(d);
            ++nowCount[d];
            s.perDay[static_cast<std::size_t>(d - p.period.first)]++;
            s.dayHour[static_cast<std::size_t>(weekday(d))][static_cast<std::size_t>(p.minute[i] / 60)]++;
            s.byLevel[static_cast<std::size_t>(l.level)]++;
            if (t >= 0) {
                ++s.typed;
                s.byType[static_cast<std::size_t>(t)]++;
            }
            if (l.action == "client_supprimer") s.deletions.push_back(refOf(l));
            if (l.action == "licence_revoquer") s.revocations.push_back(refOf(l));
            if (l.action == "code_revoir") s.codeReviews.push_back(refOf(l));
            if (l.action == "licence_creer" && !l.object.empty()) licencesNow.insert(l.object);
            if (l.action == "demande_accepter" || l.action == "demande_refuser") ++requestsNow;
        } else if (inBefore) {
            ++s.actionsBefore;
            activeBefore.insert(d);
            s.perDayBefore[static_cast<std::size_t>(d - p.previous.first)]++;
            s.byLevelBefore[static_cast<std::size_t>(l.level)]++;
            if (t >= 0) {
                ++s.typedBefore;
                s.byTypeBefore[static_cast<std::size_t>(t)]++;
            }
            if (l.action == "licence_creer" && !l.object.empty()) licencesBefore.insert(l.object);
            if (l.action == "demande_accepter" || l.action == "demande_refuser") ++requestsBefore;
        }
    }
    s.activeDays = static_cast<int>(activeNow.size());
    s.activeDaysBefore = static_cast<int>(activeBefore.size());
    s.change = s.actionsBefore > 0 ? (static_cast<double>(s.actions) - s.actionsBefore) * 100.0 / s.actionsBefore : kUnknown;

    // ---- la serie en cours (jusqu'a aujourd'hui ; aujourd'hui sans action encore : jusqu'a hier) et le record ----
    {
        std::int64_t from = p.today;
        while (weekday(from) >= 5) --from;
        if (!activeAll.count(from)) {
            --from;
            while (weekday(from) >= 5) --from;
        }
        s.streak = streakEndingAt(activeAll, from);
        int run = 0;
        if (!activeAll.empty())
            for (std::int64_t d = *activeAll.begin(); d <= p.today; ++d) {
                if (weekday(d) >= 5) continue;
                run = activeAll.count(d) ? run + 1 : 0;
                s.bestStreak = std::max(s.bestStreak, run);
            }
    }

    // ---- ses habitudes : ses heures (le 10e et le 90e centile), sa moyenne par jour actif ----
    if (static_cast<int>(habitMinutes.size()) >= o.habitsMinActions) {
        std::sort(habitMinutes.begin(), habitMinutes.end());
        const std::size_t n = habitMinutes.size();
        s.usualStart = habitMinutes[static_cast<std::size_t>(std::floor(0.1 * static_cast<double>(n - 1)))];
        s.usualEnd = habitMinutes[static_cast<std::size_t>(std::ceil(0.9 * static_cast<double>(n - 1)))];
        s.habitsKnown = true;
    }
    if (!habitDays.empty()) {
        int total = 0;
        for (const auto& [d, c] : habitDays) total += c;
        s.meanPerActiveDay = static_cast<double>(total) / static_cast<double>(habitDays.size());
    }
    for (std::size_t i = 0; i < in.lines.size() && s.habitsKnown; ++i) {
        if (p.owner[i] != idx || !p.signedBy[i] || !p.period.contains(p.day[i]) || in.lines[i].action == "connexion") continue;
        const int mn = p.minute[i];
        if (mn < s.usualStart - o.unusualMarginMinutes || mn > s.usualEnd + o.unusualMarginMinutes) {
            s.unusual.push_back(refOf(in.lines[i]));
            s.dayHourUnusual[static_cast<std::size_t>(weekday(p.day[i]))][static_cast<std::size_t>(mn / 60)]++;
        }
    }
    if (s.meanPerActiveDay > 0.0)
        for (const auto& [d, c] : nowCount)
            if (c >= o.peakMinActions && static_cast<double>(c) > o.peakFactor * s.meanPerActiveDay)
                s.peaks.push_back(Peak{d, c, static_cast<double>(c) / s.meanPerActiveDay});

    // ---- ses droits : ceux qu'il a, ce qu'il en fait, ceux qu'il n'a jamais utilises ----
    const auto& rights = adm::rights();
    for (std::size_t r = 0; r < rights.size() && r < 13; ++r) {
        RightUse u;
        u.id = rights[r].id;
        u.label = types()[r].label;
        u.held = a.plus || std::find(a.rights.begin(), a.rights.end(), u.id) != a.rights.end();
        if (const auto it = given.find(u.id); it != given.end()) u.given = it->second;
        u.count = s.byType[r];
        u.last = lastOfType[r];
        if (u.held && !a.plus && !s.retired) {
            const std::int64_t since = localDay(u.given ? u.given : a.since, in.tz);
            const Period window{std::max(p.period.last - std::max(1, o.unusedDays) + 1, since), p.period.last};
            for (std::size_t i = 0; i < in.lines.size(); ++i)
                if (p.owner[i] == idx && p.signedBy[i] && window.contains(p.day[i]) && typeOf(in.lines[i].action) == static_cast<int>(r)) ++u.countWindow;
            // Un droit donne il y a moins de 14 jours n'est pas (encore) << jamais utilise >>.
            u.neverUsed = u.countWindow == 0 && window.days() >= std::min(14, std::max(1, o.unusedDays));
            if (u.neverUsed) s.unused.push_back(u.id);
        }
        s.rights.push_back(std::move(u));
    }

    // ---- ce que le service en dit ; sinon le journal (les licences signees, les demandes traitees) ----
    if (const auto it = in.service.find(a.id); it != in.service.end()) s.service = it->second;
    s.licences = known(s.service.licences) ? s.service.licences : static_cast<double>(licencesNow.size());
    s.licencesBefore = known(s.service.licencesBefore) ? s.service.licencesBefore : static_cast<double>(licencesBefore.size());
    s.licencesFromJournal = !known(s.service.licences);
    s.requests = known(s.service.requests) ? s.service.requests : static_cast<double>(requestsNow);
    s.requestsBefore = known(s.service.requestsBefore) ? s.service.requestsBefore : static_cast<double>(requestsBefore);
    s.requestsFromJournal = !known(s.service.requests);
    return s;
}

std::string rightsList(const std::vector<std::string>& ids) {
    std::string t;
    for (const auto& id : ids) {
        if (!t.empty()) t += ", ";
        const int k = [&] {
            for (int i = 0; i < 13; ++i)
                if (id == types()[static_cast<std::size_t>(i)].id) return i;
            return -1;
        }();
        t += k >= 0 ? types()[static_cast<std::size_t>(k)].label : id;
    }
    return t;
}

} // namespace

AdminStats adminStats(const Inputs& in, const std::string& adminId) {
    const Prepared p = prepare(in);
    const auto it = p.byId.find(adminId);
    if (it == p.byId.end()) return {};
    return compute(p, it->second);
}

Overview overview(const Inputs& in) {
    const Prepared p = prepare(in);
    const Options& o = in.options;
    const TimeZone& tz = in.tz;
    Overview v;
    v.period = p.period;
    v.previous = p.previous;
    for (std::size_t i = 0; i < in.admins.size(); ++i) v.admins.push_back(compute(p, static_cast<int>(i)));
    for (const auto& a : in.admins) {
        if (a.plus) continue;
        if (a.retired) ++v.retiredAdmins;
        else ++v.activeAdmins;
    }
    v.pendingAdmins = static_cast<int>(in.pending.size());
    for (const auto& s : v.admins) {
        v.actions += s.actions;
        v.actionsBefore += s.actionsBefore;
        v.typed += s.typed;
        v.typedBefore += s.typedBefore;
    }
    v.change = v.actionsBefore > 0 ? (static_cast<double>(v.actions) - v.actionsBefore) * 100.0 / v.actionsBefore : kUnknown;

    // ---- le classement ----
    for (const auto& s : v.admins) {
        RankRow r;
        r.id = s.id;
        r.name = s.name;
        r.plus = s.plus;
        r.retired = s.retired;
        r.actions = s.actions;
        r.change = s.change;
        r.licences = s.licences;
        r.requests = s.requests;
        r.meanDelay = s.service.meanDelay;
        r.amount = s.service.amount;
        r.last = s.last;
        v.ranking.push_back(std::move(r));
    }
    std::stable_sort(v.ranking.begin(), v.ranking.end(), [](const RankRow& a, const RankRow& b) {
        if (a.actions != b.actions) return a.actions > b.actions;
        return a.name < b.name;
    });

    // ---- qui fait quoi ----
    for (int t = -1; t < kTypes; ++t) {
        ShareRow r;
        r.type = t;
        r.label = t < 0 ? std::string("Toutes (hors r\xC3\xA9serv\xC3\xA9" "es)") : std::string(types()[static_cast<std::size_t>(t)].label);
        int doers = 0, best = 0;
        for (std::size_t i = 0; i < v.admins.size(); ++i) {
            const auto& s = v.admins[i];
            const int n = t < 0 ? s.typed : s.byType[static_cast<std::size_t>(t)];
            const int before = t < 0 ? s.typedBefore : s.byTypeBefore[static_cast<std::size_t>(t)];
            r.perAdmin.push_back(n);
            r.total += n;
            r.totalBefore += before;
            if (n > 0) ++doers;
            if (n > best) {
                best = n;
                r.leader = static_cast<int>(i);
            }
        }
        r.leaderShare = r.total > 0 ? static_cast<double>(best) / r.total : 0.0;
        r.alone = doers == 1;
        r.changeInPercent = r.totalBefore >= 10;
        r.changeCount = r.total - r.totalBefore;
        r.changePct = r.totalBefore > 0 ? (static_cast<double>(r.total) - r.totalBefore) * 100.0 / r.totalBefore : kUnknown;
        v.shares.push_back(std::move(r));
    }

    // ---- les alertes, chacune avec son geste ----
    for (std::size_t i = 0; i < v.admins.size(); ++i) {
        const auto& s = v.admins[i];
        const auto& a = in.admins[i];
        const std::string who = s.name + (s.retired ? std::string(" (retir\xC3\xA9(e))") : std::string{});
        if (!s.refused.empty()) {
            Alert al;
            al.kind = AlertKind::RefusedSignatures;
            al.severity = Severity::Critical;
            al.adminId = s.id;
            al.adminName = s.name;
            const auto& lastR = s.refused.back();
            bool missing = false;
            for (const auto& r : s.refused) {
                al.lines.push_back(r.line.number);
                missing = missing || r.reason == RefusalReason::MissingRight;
            }
            al.when = lastR.line.when;
            al.text = who + " : " + plural(static_cast<int>(s.refused.size()), "signature refus\xC3\xA9" "e", "signatures refus\xC3\xA9" "es") + ", la derni\xC3\xA8re le " +
                      dayMonth(localDay(lastR.line.when, tz)) + " \xC3\xA0 " + clock(lastR.line.when, tz) +
                      (missing ? std::string(" (droit manquant)") : std::string(" (cl\xC3\xA9 retir\xC3\xA9" "e)"));
            al.gesture = "Voir dans le journal";
            al.gestureId = "journal";
            v.alerts.push_back(std::move(al));
        }
        if (s.retired) continue;
        if (!a.plus) {
            const Seconds lastWhen = s.last.number ? s.last.when : 0;
            const Seconds ref = lastWhen ? lastWhen : a.since;
            if (ref > 0 && in.now - ref >= static_cast<Seconds>(o.inactiveDays) * kDay) {
                Alert al;
                al.kind = AlertKind::Inactive;
                al.severity = Severity::ToSee;
                al.adminId = s.id;
                al.adminName = s.name;
                al.when = ref;
                const auto days = static_cast<int>((in.now - ref) / kDay);
                al.text = lastWhen ? s.name + " : aucune action depuis " + std::to_string(days) + " jours (la derni\xC3\xA8re le " + dayMonth(localDay(lastWhen, tz)) + ")"
                                   : s.name + " : aucune action depuis son arriv\xC3\xA9" "e (" + std::to_string(days) + " jours)";
                al.gesture = "Lui \xC3\xA9" "crire\xE2\x80\xA6";
                al.gestureId = "ecrire";
                v.alerts.push_back(std::move(al));
            }
            if (!s.unused.empty()) {
                Alert al;
                al.kind = AlertKind::RightsUnused;
                al.severity = Severity::ToSee;
                al.adminId = s.id;
                al.adminName = s.name;
                al.rights = s.unused;
                al.text = s.name + " : " + plural(static_cast<int>(s.unused.size()), "droit jamais utilis\xC3\xA9", "droits jamais utilis\xC3\xA9s") + " en " +
                          std::to_string(o.unusedDays) + " jours (" + rightsList(s.unused) + ")";
                al.gesture = s.unused.size() > 1 ? "Retirer ces droits ?" : "Retirer ce droit ?";
                al.gestureId = "retirer-droits";
                v.alerts.push_back(std::move(al));
            }
        }
        if (!s.service.adminVersion.empty() && !in.latestAdminVersion.empty() && versionLess(s.service.adminVersion, in.latestAdminVersion)) {
            Alert al;
            al.kind = AlertKind::AdminOutdated;
            al.severity = Severity::High;
            al.adminId = s.id;
            al.adminName = s.name;
            al.when = s.service.lastSeen;
            al.text = s.name + " : son Admin est en " + s.service.adminVersion + ", pas \xC3\xA0 jour (" + in.latestAdminVersion + ")";
            al.gesture = "Lui envoyer la mise \xC3\xA0 jour";
            al.gestureId = "envoyer-maj";
            v.alerts.push_back(std::move(al));
        }
        if (!s.unusual.empty()) {
            Alert al;
            al.kind = AlertKind::UnusualHour;
            al.severity = Severity::High;
            al.adminId = s.id;
            al.adminName = s.name;
            Seconds lo = s.unusual.front().when, hi = lo;
            for (const auto& r : s.unusual) {
                al.lines.push_back(r.number);
                lo = std::min(lo, r.when);
                hi = std::max(hi, r.when);
            }
            al.when = hi;
            const std::int64_t d0 = localDay(lo, tz), d1 = localDay(hi, tz);
            const int n = static_cast<int>(s.unusual.size());
            al.text = s.name + " : " + plural(n, "action", "actions") + " \xC3\xA0 une heure inhabituelle d'apr\xC3\xA8s " + (a.plus ? "tes" : "ses") + " habitudes, " +
                      (d0 == d1 ? std::string(kWeekdays[weekday(d0)]) + " " + dayMonth(d0) + (n > 1 ? " de " + clock(lo, tz) + " \xC3\xA0 " + clock(hi, tz) : " \xC3\xA0 " + clock(lo, tz))
                                : "du " + dayMonth(d0) + " au " + dayMonth(d1));
            al.gesture = n > 1 ? "Voir ces " + std::to_string(n) + " actions" : std::string("Voir cette action");
            al.gestureId = "journal";
            v.alerts.push_back(std::move(al));
        }
        if (!s.otherPc.empty()) {
            Alert al;
            al.kind = AlertKind::OtherPc;
            al.severity = Severity::High;
            al.adminId = s.id;
            al.adminName = s.name;
            for (const auto& x : s.otherPc) {
                al.lines.push_back(x.line.number);
                al.when = std::max(al.when, x.line.when);
            }
            const auto& last = s.otherPc.back();
            const int n = static_cast<int>(s.otherPc.size());
            al.text = s.name + " : " + (n > 1 ? std::to_string(n) + " actions" : std::string("une action")) + " depuis un autre PC que le sien (" +
                      (last.pc.empty() ? last.machine : last.pc) + "), le " + dayMonth(localDay(last.line.when, tz));
            al.gesture = n > 1 ? "Voir ces " + std::to_string(n) + " lignes" : std::string("Voir la ligne");
            al.gestureId = "journal";
            v.alerts.push_back(std::move(al));
        }
    }
    // Les actions critiques recentes (de tout le monde), les plus recentes d'abord.
    {
        std::vector<std::size_t> crit;
        for (std::size_t i = 0; i < in.lines.size(); ++i) {
            const auto& l = in.lines[i];
            if (p.owner[i] < 0 || !p.signedBy[i] || l.level != Level::Critical || l.action == "connexion") continue;
            if (l.at() > in.now || in.now - l.at() > static_cast<Seconds>(o.criticalDays) * kDay) continue;
            crit.push_back(i);
        }
        std::sort(crit.begin(), crit.end(), [&](std::size_t a, std::size_t b) { return in.lines[a].at() > in.lines[b].at(); });
        for (std::size_t k = 0; k < crit.size() && static_cast<int>(k) < o.maxCriticalAlerts; ++k) {
            const auto& l = in.lines[crit[k]];
            const auto& a = in.admins[static_cast<std::size_t>(p.owner[crit[k]])];
            Alert al;
            al.kind = AlertKind::RecentCritical;
            al.severity = Severity::ToSee;
            al.adminId = a.id;
            al.adminName = a.name;
            al.when = l.at();
            al.lines = {l.number};
            al.text = "Action critique r\xC3\xA9" "cente : " + a.name + " " + (l.summary.empty() ? l.action : l.summary) + " (le " + dayMonth(localDay(l.at(), tz)) +
                      " \xC3\xA0 " + clock(l.at(), tz) + ")";
            al.gesture = "Voir la ligne";
            al.gestureId = "journal";
            v.alerts.push_back(std::move(al));
        }
    }
    for (const auto& q : in.pending) {
        Alert al;
        al.kind = AlertKind::Pending;
        al.severity = Severity::ToSee;
        al.adminName = q.name;
        al.when = q.received;
        al.text = q.name + " attend depuis le " + dayMonth(localDay(q.received, tz)) + " \xC3\xA0 " + clock(q.received, tz) +
                  " : son empreinte \xC3\xA0 lire au t\xC3\xA9l\xC3\xA9phone avant de l'accepter";
        al.gesture = "V\xC3\xA9rifier et accepter\xE2\x80\xA6";
        al.gestureId = "accepter";
        v.alerts.push_back(std::move(al));
    }
    std::stable_sort(v.alerts.begin(), v.alerts.end(), [](const Alert& a, const Alert& b) {
        if (a.severity != b.severity) return static_cast<int>(a.severity) < static_cast<int>(b.severity);
        return a.when > b.when;
    });

    // ---- l'admin+ : ce que lui seul fait ----
    for (std::size_t i = 0; i < in.lines.size(); ++i) {
        const auto& l = in.lines[i];
        if (p.owner[i] < 0 || !in.admins[static_cast<std::size_t>(p.owner[i])].plus) continue;
        if (l.action == "sauvegarde" && l.at() >= v.plus.lastBackup.when) v.plus.lastBackup = refOf(l);
        if (!p.period.contains(p.day[i])) continue;
        const std::string& x = l.action;
        if (x == "version_publier") v.plus.versions.push_back(refOf(l));
        else if (x == "sauvegarde") v.plus.backups.push_back(refOf(l));
        else if (x == "restauration") v.plus.restores.push_back(refOf(l));
        else if (x == "admin_ajouter") v.plus.adminsAdded.push_back(refOf(l));
        else if (x == "admin_retirer") v.plus.adminsRemoved.push_back(refOf(l));
        else if (x == "admin_droits" || x == "admin_profil") v.plus.rights.push_back(refOf(l));
        else if (x == "criticite") v.plus.criticities.push_back(refOf(l));
        else if (x == "stripe_brancher" || x == "boutique" || x == "commande_rembourser") v.plus.stripe.push_back(refOf(l));
        else if (x == "fichier_envoyer") v.plus.files.push_back(refOf(l));
    }

    // ---- la chaine de tout le journal ----
    for (const auto& l : in.lines) {
        ++v.chain.lines;
        if (l.verified) ++v.chain.verified;
        if (!l.chainOk && v.chain.firstBreak == 0) v.chain.firstBreak = l.number;
        v.chain.last = std::max(v.chain.last, l.number);
    }
    return v;
}

// ------------------------------------------------------------------------------- depuis l'Admin ---
std::vector<Line> linesOf(const gui::JournalView& view) {
    std::vector<Line> out;
    out.reserve(view.rows.size());
    std::uint64_t prevNumber = 0;
    std::string prevHash;
    bool broken = false;
    for (const auto& r : view.rows) {
        const auto& L = r.line;
        if (prevNumber && L.number != prevNumber + 1) broken = true;
        else if ((prevNumber || L.number == 1) && L.previous != prevHash) broken = true;
        prevNumber = L.number;
        prevHash = L.hash;
        Line l;
        l.number = L.number;
        l.when = L.parsed.when;
        l.received = L.received;
        l.key = L.key;
        l.who = r.who;
        l.action = L.parsed.action;
        l.object = L.parsed.object;
        l.summary = L.parsed.summary;
        l.detail = L.parsed.detail;
        l.before = L.parsed.before;
        l.after = L.parsed.after;
        l.pc = L.parsed.pc;
        l.machine = L.parsed.machine;
        l.software = L.parsed.software;
        l.level = r.level;
        l.verified = r.verified;
        l.chainOk = !broken;
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<Admin> adminsOf(const licensing::admins::AdminList& list, const std::vector<std::string>& editorKeys, const std::string& plusName) {
    std::vector<Admin> out;
    Admin plus;
    plus.id = "admin+";
    plus.name = plusName;
    plus.keys = editorKeys;
    plus.rights = adm::allRights();
    plus.plus = true;
    out.push_back(std::move(plus));
    for (const auto& a : list.admins) {
        Admin x;
        x.id = a.id;
        x.name = a.name;
        x.keys = {a.key};
        x.pc = a.pc;
        x.machine = a.machine;
        x.rights = adm::normalizeRights(a.rights);
        x.since = a.since;
        x.retired = a.retired;
        x.profile = a.profile;
        out.push_back(std::move(x));
    }
    return out;
}

} // namespace license_admin::journalstats
