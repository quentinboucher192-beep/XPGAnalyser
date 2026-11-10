// =============================================================================
//  license_admin/Stats.cpp - 1.47.0 : les statistiques et le globe, cote
//  editeur (voir Stats.hpp, docs/STATISTIQUES.md). JAMAIS LIVRE A UN CLIENT.
// =============================================================================
#include "Stats.hpp"

#include "../licensing/Admins.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace license_admin::stats {

namespace {

using core::json::Value;

constexpr double  kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr Seconds kDay = 86400;

online::Result refused(std::string why) {
    online::Result r;
    r.ok = false;
    r.message = "Refus\xC3\xA9 : " + std::move(why) + ".";
    return r;
}
online::Result said(std::string sentence) {
    online::Result r;
    r.ok = false;
    r.message = std::move(sentence);
    return r;
}
online::Result done(std::string message) {
    online::Result r;
    r.ok = true;
    r.message = std::move(message);
    return r;
}

// Une ligne propre (sans caractere de controle), `max` octets au plus (sans couper un caractere).
std::string clean(std::string_view s, std::size_t max = 300) {
    std::string out;
    for (char c : s) out += (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ? ' ' : c;
    if (out.size() > max) {
        std::size_t n = max;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;
        out.resize(n);
    }
    return out;
}

// Une valeur d'une adresse : tout sauf [A-Za-z0-9-_.~] en %XX (le service relit l'adresse telle quelle : la signature
// porte exactement ces octets).
std::string urlEncode(std::string_view s) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else {
            out += '%';
            out += hexd[c >> 4];
            out += hexd[c & 15];
        }
    }
    return out;
}

double numberOf(const Value* v) { return v && v->isNumber() ? v->asNumber() : kNaN; }
std::vector<double> numbersOf(const Value* a) {
    std::vector<double> out;
    if (a && a->isArray())
        for (const auto& x : a->items()) out.push_back(x.isNumber() ? x.asNumber() : kNaN);
    return out;
}
std::vector<int> intsOf(const Value* a) {
    std::vector<int> out;
    if (a && a->isArray())
        for (const auto& x : a->items()) out.push_back(static_cast<int>(x.asInt(0)));
    return out;
}
std::vector<std::string> textsOf(const Value* a, std::size_t max = 1000) {
    std::vector<std::string> out;
    if (a && a->isArray())
        for (const auto& x : a->items())
            if (x.isString() && out.size() < max) out.push_back(clean(x.asString(), 200));
    return out;
}
std::vector<Bucket> bucketsOf(const Value* a) {
    std::vector<Bucket> out;
    if (a && a->isArray())
        for (const auto& x : a->items())
            if (x.isObject()) out.push_back({clean(x.str("du"), 10), clean(x.str("au"), 10)});
    return out;
}

// ---- les jours (le calendrier civil, en jours depuis le 1970-01-01) ----
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
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2);
}
std::string dayText(std::int64_t days) {
    std::int64_t y = 0;
    unsigned m = 0, d = 0;
    civilFromDays(days, y, m, d);
    char b[16];
    std::snprintf(b, sizeof b, "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return b;
}
std::int64_t dayNumber(Seconds t) { return t >= 0 ? t / kDay : -((-t + kDay - 1) / kDay); }

// Un appel au service : refuse (en francais) s'il n'a pas repondu 2xx avec un objet JSON ; un service d'avant la 1.47
// (sa version lue, ou 404 << route >>) : la phrase << deploie la 1.47.0 >>.
online::Result call(online::Client& c, const std::string& method, const std::string& path, const std::string& body, const std::string& what, online::Reply& out) {
    const std::string version = c.state().isObject() ? c.state().str("version") : std::string();
    if (!version.empty() && !serviceKnowsStats(version)) return said(serviceTooOldText(version));
    out = c.call(method, path, body);
    if (!out.error.empty() || out.status < 200 || out.status >= 300) {
        if (out.error.empty() && out.status == 404 && out.code() == "route") return said(serviceTooOldText(version.empty() ? std::string("?") : version));
        return refused(c.explain(out, what));
    }
    if (!out.json.isObject()) return refused("le service a rendu une r\xC3\xA9ponse illisible (" + what + ")");
    return done({});
}

std::string groupDigits(std::uint64_t n) {
    const std::string units = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < units.size(); ++i) {
        if (i > 0 && (units.size() - i) % 3 == 0) out += "\xC2\xA0";
        out += units[i];
    }
    return out;
}

} // namespace

// ============================================================================ la version du service
bool serviceKnowsStats(const std::string& version) {
    int a = 0, b = 0, c = 0;
    return licensing::admins::parseSemver(version, a, b, c) && licensing::admins::compareSemver(version, kServiceRequired) >= 0;
}

std::string serviceTooOldText(const std::string& version) {
    if (serviceKnowsStats(version)) return {};
    int a = 0, b = 0, c = 0;
    if (!licensing::admins::parseSemver(version, a, b, c))
        return "Ton service en ligne ne conna\xC3\xAEt pas encore les statistiques : d\xC3\xA9ploie la " + std::string(kServiceRequired) +
               " pour les statistiques et le globe.";
    return "Ton service en ligne est en " + version + " : d\xC3\xA9ploie la " + std::string(kServiceRequired) + " pour les statistiques et le globe.";
}

// ============================================================================ les periodes
const char* stepKey(Step s) noexcept { return s == Step::Week ? "semaine" : s == Step::Month ? "mois" : "jour"; }
const char* compareKey(Compare c) noexcept { return c == Compare::None ? "rien" : c == Compare::LastYear ? "an" : "avant"; }
bool parseStep(std::string_view k, Step& out) noexcept {
    if (k == "jour") out = Step::Day;
    else if (k == "semaine") out = Step::Week;
    else if (k == "mois") out = Step::Month;
    else return false;
    return true;
}
bool parseCompare(std::string_view k, Compare& out) noexcept {
    if (k == "rien") out = Compare::None;
    else if (k == "avant") out = Compare::Previous;
    else if (k == "an") out = Compare::LastYear;
    else return false;
    return true;
}

std::string dayOf(Seconds t) { return dayText(dayNumber(t)); }

Seconds dayStart(std::string_view s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return -1;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
        if (s[i] < '0' || s[i] > '9') return -1;
    const auto n = [&](std::size_t at, std::size_t len) {
        int v = 0;
        for (std::size_t i = 0; i < len; ++i) v = v * 10 + (s[at + i] - '0');
        return v;
    };
    const int y = n(0, 4);
    const unsigned m = static_cast<unsigned>(n(5, 2)), d = static_cast<unsigned>(n(8, 2));
    if (m < 1 || m > 12 || d < 1 || d > 31) return -1;
    const std::int64_t days = daysFromCivil(y, m, d);
    if (dayText(days) != s) return -1;   // le 30 fevrier...
    return static_cast<Seconds>(days * kDay);
}

Period lastDays(Seconds now, int days) {
    if (days < 1) days = 1;
    return {dayOf(now - static_cast<Seconds>(days - 1) * kDay), dayOf(now)};
}

Period lastMonths(Seconds now, int months) {
    std::int64_t y = 0;
    unsigned m = 0, d = 0;
    civilFromDays(dayNumber(now), y, m, d);
    std::int64_t total = y * 12 + (m - 1) - months;
    const std::int64_t y2 = total >= 0 ? total / 12 : (total - 11) / 12;
    const unsigned m2 = static_cast<unsigned>(total - y2 * 12) + 1;
    // le dernier jour du mois vise, si le jour n'y est pas
    const unsigned last = static_cast<unsigned>(daysFromCivil(m2 == 12 ? y2 + 1 : y2, m2 == 12 ? 1 : m2 + 1, 1) - daysFromCivil(y2, m2, 1));
    const std::int64_t from = daysFromCivil(y2, m2, std::min(d, last)) + 1;
    return {dayText(from), dayOf(now)};
}

Period nextDays(Seconds now, int days) { return {dayOf(now), dayOf(now + static_cast<Seconds>(days) * kDay)}; }

bool isNumber(double v) noexcept { return !std::isnan(v); }

// ============================================================================ les adresses
std::string SeriesQuery::path() const {
    std::string p = "/admin/stats/serie?mesures=";
    for (std::size_t i = 0; i < measures.size(); ++i) p += (i ? "," : "") + urlEncode(measures[i]);
    if (!period.from.empty()) p += "&du=" + urlEncode(period.from);
    if (!period.to.empty()) p += "&au=" + urlEncode(period.to);
    p += std::string("&pas=") + stepKey(step);
    if (!split.empty()) p += "&par=" + urlEncode(split);
    p += std::string("&comparer=") + compareKey(compare);
    if (!client.empty()) p += "&client=" + urlEncode(client);
    if (!admin.empty()) p += "&admin=" + urlEncode(admin);
    if (!licence.empty()) p += "&licence=" + urlEncode(licence);
    if (!software.empty()) p += "&logiciel=" + urlEncode(software);
    return p;
}

std::string UpdatesQuery::path() const {
    std::string p = std::string("/admin/stats/mises-a-jour?pas=") + stepKey(step) + "&jours=" + std::to_string(days) + "&logiciel=" + urlEncode(software);
    if (!period.from.empty()) p += "&du=" + urlEncode(period.from);
    if (!period.to.empty()) p += "&au=" + urlEncode(period.to);
    return p;
}

// ============================================================================ les recherches
const Part* Series::part(const std::string& key) const {
    for (const auto& p : parts)
        if (p.key == key) return &p;
    return nullptr;
}
const Series* SeriesReply::find(std::string_view name) const {
    for (const auto& s : series)
        if (s.name == name) return &s;
    return nullptr;
}
const AdoptionCurve* UpdatesReply::curve(const std::string& version, const std::string& software) const {
    for (const auto& c : adoption)
        if (c.version == version && c.software == software) return &c;
    return nullptr;
}
const AdminUsage* AdminsReply::find(const std::string& id) const {
    for (const auto& a : admins)
        if (a.id == id) return &a;
    return nullptr;
}
const GlobeCity* GlobeReply::find(const std::string& id) const {
    for (const auto& c : cities)
        if (c.id == id) return &c;
    return nullptr;
}
int GlobeCity::count(std::string_view layer) const noexcept {
    if (layer == "clients") return clients;
    if (layer == "pc") return pcs;
    if (layer == "actifs") return active;
    if (layer == "pas_a_jour") return outdated;
    if (layer == "admins") return admins;
    if (layer == "decouvertes") return discoveries;
    if (layer == "commandes") return orders;
    if (layer == "demandes") return requests;
    return 0;
}

// ============================================================================ lire les reponses
bool parseSeries(const Value& v, SeriesReply& out, std::string* why) {
    out = SeriesReply{};
    const Value* mesures = v.isObject() ? v.find("mesures") : nullptr;
    if (!mesures || !mesures->isObject()) {
        if (why) *why = "la r\xC3\xA9ponse des courbes est illisible";
        return false;
    }
    out.period = {clean(v.str("du"), 10), clean(v.str("au"), 10)};
    (void)parseStep(v.str("pas"), out.step);
    if (!parseCompare(v.str("comparer"), out.compare)) out.compare = Compare::None;
    out.split = clean(v.str("par"), 40);
    out.buckets = bucketsOf(v.find("periodes"));
    if (const Value* a = v.find("avant"); a && a->isObject()) {
        out.beforePeriod = {clean(a->str("du"), 10), clean(a->str("au"), 10)};
        out.beforeBuckets = bucketsOf(a->find("periodes"));
    }
    out.at = static_cast<Seconds>(v.integer("heure"));
    out.recordedSince = clean(v.str("releves_depuis"), 10);
    out.serviceVersion = clean(v.str("version"), 20);
    for (const auto& [name, m] : mesures->members()) {
        Series s;
        s.name = clean(name, 60);
        s.total = kNaN;
        s.beforeTotal = kNaN;
        s.change = kNaN;
        if (!m.isObject()) continue;
        if (m.has("refuse")) {
            s.refused = true;
            s.rights = textsOf(m.find("droits"), 20);
            s.filter = clean(m.str("filtre"), 20);
            out.series.push_back(std::move(s));
            continue;
        }
        s.unit = clean(m.str("unite"), 20);
        s.kind = clean(m.str("genre"), 20);
        s.source = clean(m.str("source"), 10);
        s.points = numbersOf(m.find("points"));
        s.total = numberOf(m.find("total"));
        if (const Value* ps = m.find("parts"); ps && ps->isArray())
            for (const auto& p : ps->items()) {
                if (!p.isObject()) continue;
                Part x;
                x.key = clean(p.str("cle"), 200);
                x.points = numbersOf(p.find("points"));
                x.total = numberOf(p.find("total"));
                s.parts.push_back(std::move(x));
            }
        if (const Value* a = m.find("avant"); a && a->isObject()) {
            s.before = numbersOf(a->find("points"));
            s.beforeTotal = numberOf(a->find("total"));
        }
        s.change = numberOf(m.find("variation"));
        if (!out.buckets.empty() && s.points.size() != out.buckets.size()) {
            if (why) *why = "la mesure " + s.name + " n'a pas un point par p\xC3\xA9riode";
            return false;
        }
        out.series.push_back(std::move(s));
    }
    return true;
}

bool parseUpdates(const Value& v, UpdatesReply& out, std::string* why) {
    out = UpdatesReply{};
    if (!v.isObject() || !v.find("versions") || !v.find("adoption")) {
        if (why) *why = "la r\xC3\xA9ponse des mises \xC3\xA0 jour est illisible";
        return false;
    }
    if (const Value* d = v.find("derniere"); d && d->isObject()) {
        out.latest = clean(d->str("version"), 20);
        out.latestPublished = static_cast<Seconds>(d->integer("publiee"));
    }
    if (const Value* a = v.find("versions"); a && a->isArray())
        for (const auto& x : a->items())
            if (x.isObject())
                out.versions.push_back({clean(x.str("version"), 20), static_cast<Seconds>(x.integer("publiee")), static_cast<int>(x.integer("studios")),
                                        static_cast<int>(x.integer("admins"))});
    if (const Value* s = v.find("vus"); s && s->isObject()) {
        out.seenStudios = static_cast<int>(s->integer("studios"));
        out.seenAdmins = static_cast<int>(s->integer("admins"));
        out.seenSince = clean(s->str("depuis"), 10);
    }
    if (const Value* s = v.find("a_jour"); s && s->isObject()) {
        out.upToDateStudios = static_cast<int>(s->integer("studios"));
        out.upToDateAdmins = static_cast<int>(s->integer("admins"));
    }
    if (const Value* a = v.find("adoption"); a && a->isArray())
        for (const auto& x : a->items())
            if (x.isObject())
                out.adoption.push_back({clean(x.str("version"), 20), clean(x.str("logiciel"), 20), static_cast<Seconds>(x.integer("publiee")),
                                        static_cast<int>(x.integer("base")), intsOf(x.find("jours"))});
    if (const Value* a = v.find("delais"); a && a->isArray())
        for (const auto& x : a->items())
            if (x.isObject())
                out.delays.push_back({clean(x.str("version"), 20), clean(x.str("logiciel"), 20), static_cast<int>(x.integer("nombre")), numberOf(x.find("median")),
                                      numberOf(x.find("moyen"))});
    out.buckets = bucketsOf(v.find("periodes"));
    if (const Value* p = v.find("part"); p && p->isObject()) {
        if (const Value* s = p->find("studios"); s && s->isObject()) {
            out.studiosUpToDate = intsOf(s->find("a_jour"));
            out.studiosSeen = intsOf(s->find("vus"));
        }
        if (const Value* s = p->find("admins"); s && s->isObject()) {
            out.adminsUpToDate = intsOf(s->find("a_jour"));
            out.adminsSeen = intsOf(s->find("vus"));
        }
    }
    if (const Value* a = v.find("pas_a_jour"); a && a->isArray())
        for (const auto& x : a->items())
            if (x.isObject()) {
                BehindPc b;
                b.key = clean(x.str("cle"), 64);
                b.software = clean(x.str("logiciel"), 40);
                b.version = clean(x.str("version"), 20);
                b.name = clean(x.str("nom"), 120);
                b.customer = clean(x.str("client"), 200);
                b.licence = clean(x.str("licence"), 64);
                b.admin = clean(x.str("admin"), 40);
                b.seen = static_cast<Seconds>(x.integer("vu"));
                b.behind = static_cast<int>(x.integer("retard"));
                out.behind.push_back(std::move(b));
            }
    out.behindTotal = static_cast<int>(v.integer("pas_a_jour_total"));
    out.recordedSince = clean(v.str("releves_depuis"), 10);
    return true;
}

bool parseAdmins(const Value& v, AdminsReply& out, std::string* why) {
    out = AdminsReply{};
    const Value* a = v.isObject() ? v.find("admins") : nullptr;
    if (!a || !a->isArray()) {
        if (why) *why = "la r\xC3\xA9ponse des admins est illisible";
        return false;
    }
    out.period = {clean(v.str("du"), 10), clean(v.str("au"), 10)};
    out.recordedSince = clean(v.str("releves_depuis"), 10);
    for (const auto& x : a->items()) {
        if (!x.isObject()) continue;
        AdminUsage u;
        u.id = clean(x.str("id"), 40);
        u.key = clean(x.str("cle"), 64);
        u.role = clean(x.str("role"), 10);
        u.name = clean(x.str("nom"), 120);
        u.retired = static_cast<Seconds>(x.integer("retire"));
        u.days = textsOf(x.find("jours"), 1000);
        const std::vector<int> h = intsOf(x.find("heures"));
        for (std::size_t i = 0; i < 24 && i < h.size(); ++i) u.hours[i] = h[i];
        u.calls = static_cast<int>(x.integer("appels"));
        u.usualStart = static_cast<int>(x.integer("debut_habituel", -1));
        u.usualEnd = static_cast<int>(x.integer("fin_habituelle", -1));
        u.lastSeen = static_cast<Seconds>(x.integer("vu"));
        u.software = clean(x.str("logiciel"), 40);
        u.softwareVersion = clean(x.str("logiciel_version"), 20);
        u.city = clean(x.str("ville"), 120);
        u.country = clean(x.str("pays"), 4);
        out.admins.push_back(std::move(u));
    }
    return true;
}

namespace {
void readCounts(const Value& x, GlobeCity& c) {
    c.clients = static_cast<int>(x.integer("clients"));
    c.pcs = static_cast<int>(x.integer("pc"));
    c.active = static_cast<int>(x.integer("actifs"));
    c.outdated = static_cast<int>(x.integer("pas_a_jour"));
    c.admins = static_cast<int>(x.integer("admins"));
    c.discoveries = static_cast<int>(x.integer("decouvertes"));
    c.orders = static_cast<int>(x.integer("commandes"));
    c.requests = static_cast<int>(x.integer("demandes"));
}
} // namespace

bool parseGlobe(const Value& v, GlobeReply& out, std::string* why) {
    out = GlobeReply{};
    const Value* villes = v.isObject() ? v.find("villes") : nullptr;
    if (!villes || !villes->isArray()) {
        if (why) *why = "la r\xC3\xA9ponse du globe est illisible";
        return false;
    }
    out.at = static_cast<Seconds>(v.integer("heure"));
    out.periodDays = static_cast<int>(v.integer("periode", 30));
    out.latestVersion = clean(v.str("version_derniere"), 20);
    for (const auto& x : villes->items()) {
        if (!x.isObject()) continue;
        GlobeCity c;
        c.id = clean(x.str("id"), 200);
        c.city = clean(x.str("ville"), 120);
        c.region = clean(x.str("region"), 120);
        c.country = clean(x.str("pays"), 4);
        c.colo = clean(x.str("colo"), 8);
        c.lat = x.num("lat");
        c.lon = x.num("lon");
        readCounts(x, c);
        c.customers = textsOf(x.find("noms_clients"), 50);
        c.devices = textsOf(x.find("postes"), 200);
        c.adminIds = textsOf(x.find("ids_admins"), 64);
        out.cities.push_back(std::move(c));
    }
    if (const Value* t = v.find("totaux"); t && t->isObject()) {
        readCounts(*t, out.totals);
        out.cityCount = static_cast<int>(t->integer("villes"));
        out.countries = static_cast<int>(t->integer("pays"));
    }
    if (const Value* s = v.find("sans_lieu"); s && s->isObject()) {
        out.unplacedPcs = static_cast<int>(s->integer("pc"));
        out.unplacedAdmins = static_cast<int>(s->integer("admins"));
    }
    return true;
}

bool parseLive(const Value& v, LiveReply& out, std::string* why) {
    out = LiveReply{};
    const Value* c = v.isObject() ? v.find("connexions") : nullptr;
    if (!c || !c->isArray()) {
        if (why) *why = "la r\xC3\xA9ponse du direct est illisible";
        return false;
    }
    out.at = static_cast<Seconds>(v.integer("heure"));
    out.active = static_cast<int>(v.integer("actifs"));
    for (const auto& x : c->items())
        if (x.isObject()) out.pings.push_back({static_cast<Seconds>(x.integer("t")), clean(x.str("ville"), 200), clean(x.str("genre"), 10)});
    return true;
}

// ============================================================================ le reseau
online::Result fetchSeries(online::Client& c, const SeriesQuery& q, SeriesReply& out, std::string* raw) {
    if (q.measures.empty()) return refused("aucune mesure demand\xC3\xA9" "e");
    online::Reply r;
    online::Result res = call(c, "GET", q.path(), {}, "lire les courbes", r);
    if (!res.ok) return res;
    if (raw) *raw = r.body;
    std::string why;
    if (!parseSeries(r.json, out, &why)) return refused(why);
    return done(std::to_string(out.series.size()) + " courbe(s), du " + out.period.from + " au " + out.period.to + " (" + stepKey(out.step) + ").");
}

online::Result fetchUpdates(online::Client& c, const UpdatesQuery& q, UpdatesReply& out, std::string* raw) {
    online::Reply r;
    online::Result res = call(c, "GET", q.path(), {}, "lire les mises \xC3\xA0 jour", r);
    if (!res.ok) return res;
    if (raw) *raw = r.body;
    std::string why;
    if (!parseUpdates(r.json, out, &why)) return refused(why);
    return done("Derni\xC3\xA8re version : " + (out.latest.empty() ? std::string("aucune") : out.latest) + " ; " + std::to_string(out.upToDateStudios) + " PC \xC3\xA0 jour sur " +
                std::to_string(out.seenStudios) + ".");
}

online::Result fetchAdmins(online::Client& c, const Period& p, AdminsReply& out, std::string* raw) {
    std::string path = "/admin/stats/admins";
    std::string sep = "?";
    if (!p.from.empty()) {
        path += sep + "du=" + urlEncode(p.from);
        sep = "&";
    }
    if (!p.to.empty()) path += sep + "au=" + urlEncode(p.to);
    online::Reply r;
    online::Result res = call(c, "GET", path, {}, "lire les heures des admins", r);
    if (!res.ok) return res;
    if (raw) *raw = r.body;
    std::string why;
    if (!parseAdmins(r.json, out, &why)) return refused(why);
    return done(std::to_string(out.admins.size()) + " admin(s).");
}

online::Result fetchGlobe(online::Client& c, int periodDays, GlobeReply& out, std::string* raw) {
    online::Reply r;
    online::Result res = call(c, "GET", "/admin/globe?periode=" + std::to_string(std::clamp(periodDays, 1, 400)), {}, "lire le globe", r);
    if (!res.ok) return res;
    if (raw) *raw = r.body;
    std::string why;
    if (!parseGlobe(r.json, out, &why)) return refused(why);
    return done(std::to_string(out.cities.size()) + " ville(s), " + std::to_string(out.countries) + " pays.");
}

online::Result fetchLive(online::Client& c, Seconds since, LiveReply& out, std::string* raw) {
    online::Reply r;
    online::Result res = call(c, "GET", "/admin/globe/direct?depuis=" + std::to_string(since < 0 ? 0 : since), {}, "lire le direct", r);
    if (!res.ok) return res;
    if (raw) *raw = r.body;
    std::string why;
    if (!parseLive(r.json, out, &why)) return refused(why);
    return done(std::to_string(out.pings.size()) + " connexion(s), " + std::to_string(out.active) + " PC actif(s).");
}

online::Result takeSnapshot(online::Client& c, SnapshotReply& out, std::string* raw) {
    online::Reply r;
    online::Result res = call(c, "POST", "/admin/stats/releve", "{}", "prendre le relev\xC3\xA9 du jour", r);
    if (!res.ok) return res;
    if (raw) *raw = r.body;
    out = SnapshotReply{};
    out.day = clean(r.json.str("jour"), 10);
    out.taken = r.json.flag("pris");
    if (const Value* s = r.json.find("releve")) out.snapshot = *s;
    return done(std::string(out.taken ? "Relev\xC3\xA9 du " : "Le relev\xC3\xA9 du ") + out.day + (out.taken ? " pris." : " \xC3\xA9tait d\xC3\xA9j\xC3\xA0 pris."));
}

// ============================================================================ les textes
std::string euros(double cents) {
    if (!isNumber(cents)) return "\xE2\x80\x94";
    const long long e = std::llround(cents / 100.0);
    return (e < 0 ? "-" : "") + groupDigits(static_cast<std::uint64_t>(e < 0 ? -e : e)) + "\xC2\xA0\xE2\x82\xAC";
}

std::string duration(double seconds) {
    if (!isNumber(seconds) || seconds < 0) return "\xE2\x80\x94";
    const long long s = std::llround(seconds);
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string(s / 60) + " min";
    if (s < 86400) {
        const long long h = s / 3600, m = (s % 3600) / 60;
        return std::to_string(h) + " h" + (m ? " " + std::string(m < 10 ? "0" : "") + std::to_string(m) : std::string());
    }
    const long long d = s / 86400, h = (s % 86400) / 3600;
    return std::to_string(d) + " j" + (h ? " " + std::to_string(h) + " h" : std::string());
}

std::string changeText(double percent) {
    if (!isNumber(percent)) return {};
    const long long p = std::llround(percent);
    if (p > 0) return "+" + std::to_string(p) + "\xC2\xA0%";
    if (p < 0) return "\xE2\x88\x92" + std::to_string(-p) + "\xC2\xA0%";
    return "0\xC2\xA0%";
}

namespace {
const std::vector<std::pair<std::string, std::string>>& labels() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        {"ventes_montant", "Montant encaiss\xC3\xA9"},
        {"ventes_cumul", "Cumul\xC3\xA9 sur l'ann\xC3\xA9" "e"},
        {"ventes_commandes", "Commandes"},
        {"ventes_panier", "Panier moyen"},
        {"ventes_remboursements", "Remboursements"},
        {"ventes_remboursements_nombre", "Commandes rembours\xC3\xA9" "es"},
        {"licences_vendues_en_ligne", "Licences vendues en ligne"},
        {"licences_signees_main", "Licences sign\xC3\xA9" "es \xC3\xA0 la main"},
        {"licences_actives", "Licences actives"},
        {"licences_nouvelles", "Licences nouvelles"},
        {"licences_prolongees", "Licences prolong\xC3\xA9" "es"},
        {"licences_options", "Options ajout\xC3\xA9" "es"},
        {"licences_expirees", "Licences expir\xC3\xA9" "es (\xC3\xA9" "ch\xC3\xA9" "ances)"},
        {"licences_echeances_montant", "Montant \xC3\xA0 renouveler"},
        {"licences_revoquees", "Licences r\xC3\xA9voqu\xC3\xA9" "es"},
        {"demandes_recues", "Demandes re\xC3\xA7ues"},
        {"demandes_traitees", "Demandes trait\xC3\xA9" "es"},
        {"demandes_acceptees", "Demandes accept\xC3\xA9" "es"},
        {"demandes_refusees", "Demandes refus\xC3\xA9" "es"},
        {"demandes_delai_median", "D\xC3\xA9lai de r\xC3\xA9ponse m\xC3\xA9" "dian"},
        {"demandes_delai_moyen", "D\xC3\xA9lai de r\xC3\xA9ponse moyen"},
        {"demandes_delai_max", "D\xC3\xA9lai de r\xC3\xA9ponse le plus long"},
        {"demandes_attente", "Demandes en attente"},
        {"demandes_plus_ancienne", "La plus ancienne qui attend"},
        {"pc_actifs", "PC actifs"},
        {"pc_actives", "PC activ\xC3\xA9s"},
        {"pc_liberes", "PC lib\xC3\xA9r\xC3\xA9s"},
        {"pc_retires", "PC retir\xC3\xA9s"},
        {"jetons_pic", "Jetons flottants (le pic du jour)"},
        {"jetons_plafond", "Jetons flottants (le plafond)"},
        {"licences_au_plafond", "Jours au plafond"},
        {"decouvertes_commencees", "D\xC3\xA9" "couvertes commenc\xC3\xA9" "es"},
        {"decouvertes_converties", "D\xC3\xA9" "couvertes converties"},
        {"decouvertes_en_cours", "D\xC3\xA9" "couvertes en cours"},
        {"pc_vus", "PC vus"},
        {"pc_a_jour", "PC \xC3\xA0 jour"},
        {"maj_installees", "Mises \xC3\xA0 jour install\xC3\xA9" "es"},
        {"admin_appels", "Appels au service"},
        {"admin_jours", "Jours de connexion"},
    };
    return v;
}
} // namespace

std::string measureLabel(std::string_view name) {
    for (const auto& [k, l] : labels())
        if (k == name) return l;
    return std::string(name);
}

const std::vector<std::string>& measureNames() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out;
        for (const auto& [k, l] : labels()) out.push_back(k);
        return out;
    }();
    return v;
}

} // namespace license_admin::stats
