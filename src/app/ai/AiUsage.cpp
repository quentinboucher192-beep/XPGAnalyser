// =============================================================================
//  app/ai/AiUsage.cpp - les jetons consommes, la limite du service (1.16.0) : voir AiUsage.hpp
// -----------------------------------------------------------------------------
//  IA/usage.json, ecrit a chaque reponse (petit : des nombres par jour, par
//  mois, par fournisseur et par conversation ; jamais la demande ni la cle) :
//
//    {"format": 1,
//     "jours": {"2026-10-31": {"anthropic": {"jetons": 1234, "demandes": 2, "entree": .., "sortie": .., "cache": ..}}},
//     "mois":  {"2026-10": {...}},
//     "conversations": {"c-20261031-...": 12345}}
//
//  On garde les 62 derniers jours, les 24 derniers mois, les 300 dernieres
//  conversations.
// =============================================================================
#include "AiUsage.hpp"

#include "AiJson.hpp"
#include "AiPaths.hpp"
#include "../../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace app::ai {

namespace fs = std::filesystem;

namespace {

std::mutex& fileMutex() {
    static std::mutex m;
    return m;
}

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

json::Value readUsage() {
    json::Value v;
    const std::string path = usageFile();
    if (path.empty()) return json::Value::object();
    std::string text;
    if (!core::readFileAll(P(path), text) || !json::parse(text, v) || !v.isObject()) return json::Value::object();
    return v;
}

void writeUsage(const json::Value& v) {
    const std::string path = usageFile();
    if (path.empty()) return;
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    core::AtomicWrite how;
    how.keepBackup = false;
    (void)core::writeFileAtomic(P(path), json::write(v), how);
}

// Garde les `keep` dernieres cles d'un objet (l'ordre d'ajout : les plus anciennes d'abord).
void prune(json::Value& o, std::size_t keep) {
    auto& m = o.members();
    if (m.size() > keep) m.erase(m.begin(), m.begin() + static_cast<std::ptrdiff_t>(m.size() - keep));
}

void add(json::Value& bucket, const ProviderUsage& u) {
    const auto inc = [&](const char* key, long long n) { bucket.set(key, static_cast<std::int64_t>(bucket.integer(key) + n)); };
    inc("jetons", u.total());
    inc("demandes", 1);
    inc("entree", u.input);
    inc("sortie", u.output);
    inc("cache", u.cacheRead + u.cacheWrite);
}

std::string dayKey() { return isoDate(now()).substr(0, 10); }
std::string monthKey() { return isoDate(now()).substr(0, 7); }

// Un nombre (entier ou decimal) ; faux : illisible.
bool number(std::string_view s, double& out) {
    const std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

// Jours depuis 1970-01-01 (le calendrier civil, sans fuseau).
long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// Une remise : une duree ("6m0s"), une date RFC 3339, ou une date en secondes / millisecondes depuis 1970.
double resetSeconds(std::string_view text, std::time_t now) {
    const std::string t = trim(text);
    if (t.empty()) return -1;
    if (t.find('T') != std::string::npos || t.find('-') == 4) return secondsUntil(t, now);
    double n = 0;
    if (number(t, n)) {
        if (n > 1e12) return std::max(0.0, n / 1000.0 - static_cast<double>(now));   // des millisecondes depuis 1970
        if (n > 1e9) return std::max(0.0, n - static_cast<double>(now));            // des secondes depuis 1970
        return n;
    }
    return parseDuration(t);
}

long long integerOf(std::string_view s) {
    double n = 0;
    return number(s, n) && n >= 0 ? static_cast<long long>(n) : -1;
}

std::string thousands(long long n) {
    std::string d = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += "\xC2\xA0";   // insecable
        out += d[i];
    }
    return (n < 0 ? "-" : "") + out;
}

std::string clockText(std::time_t t) {
    const std::string iso = isoDate(t);
    return iso.size() >= 19 ? iso.substr(11, 8) : iso;
}

} // namespace

// ---- le fichier ---------------------------------------------------------------------------------
std::string usageFile() { return detail::join(root(), "usage.json"); }

void recordUsage(std::string_view provider, const ProviderUsage& usage) { recordUsage(provider, usage, {}); }

void recordUsage(std::string_view provider, const ProviderUsage& usage, std::string_view conversation) {
    if (usage.empty() || provider.empty()) return;
    std::lock_guard<std::mutex> lk(fileMutex());
    json::Value v = readUsage();
    v.set("format", 1);
    json::Value& days = v["jours"];
    if (!days.isObject()) days = json::Value::object();
    json::Value& day = days[dayKey()];
    if (!day.isObject()) day = json::Value::object();
    json::Value& dp = day[provider];
    if (!dp.isObject()) dp = json::Value::object();
    add(dp, usage);
    prune(days, 62);
    json::Value& months = v["mois"];
    if (!months.isObject()) months = json::Value::object();
    json::Value& month = months[monthKey()];
    if (!month.isObject()) month = json::Value::object();
    json::Value& mp = month[provider];
    if (!mp.isObject()) mp = json::Value::object();
    add(mp, usage);
    prune(months, 24);
    if (!conversation.empty()) {
        json::Value& convs = v["conversations"];
        if (!convs.isObject()) convs = json::Value::object();
        const long long before = convs.integer(conversation);
        // La plus recente a la fin (elle survit au menage).
        convs.erase(conversation);
        convs.set(conversation, static_cast<std::int64_t>(before + usage.total()));
        prune(convs, 300);
    }
    writeUsage(v);
}

UsageTotals usageTotals(std::string_view provider) {
    std::lock_guard<std::mutex> lk(fileMutex());
    const json::Value v = readUsage();
    UsageTotals t;
    const auto sum = [&](const json::Value& bucket, long long& tokens, long long& requests) {
        if (!bucket.isObject()) return;
        for (const auto& [key, p] : bucket.members()) {
            if (!provider.empty() && key != provider) continue;
            tokens += p.integer("jetons");
            requests += p.integer("demandes");
        }
    };
    sum(v.at("jours").at(dayKey()), t.tokensToday, t.requestsToday);
    sum(v.at("mois").at(monthKey()), t.tokensMonth, t.requestsMonth);
    return t;
}

long long conversationTokens(std::string_view conversation) {
    if (conversation.empty()) return 0;
    std::lock_guard<std::mutex> lk(fileMutex());
    return readUsage().at("conversations").integer(conversation);
}

// ---- les en-tetes de limite ----------------------------------------------------------------------
double parseDuration(std::string_view text) {
    const std::string t = lower(trim(text));
    if (t.empty()) return -1;
    double n = 0;
    if (number(t, n)) return n >= 0 ? n : -1;   // des secondes
    double total = 0;
    std::size_t i = 0;
    bool any = false;
    while (i < t.size()) {
        std::size_t j = i;
        while (j < t.size() && (std::isdigit(static_cast<unsigned char>(t[j])) || t[j] == '.')) ++j;
        if (j == i) return -1;
        const double value = std::atof(t.substr(i, j - i).c_str());
        std::size_t k = j;
        while (k < t.size() && std::isalpha(static_cast<unsigned char>(t[k]))) ++k;
        const std::string unit = t.substr(j, k - j);
        if (unit == "h") total += value * 3600;
        else if (unit == "m") total += value * 60;
        else if (unit == "s") total += value;
        else if (unit == "ms") total += value / 1000.0;
        else if (unit == "us" || unit == "\xC2\xB5s") total += value / 1e6;
        else return -1;
        any = true;
        i = k;
    }
    return any ? total : -1;
}

double secondsUntil(std::string_view text, std::time_t now) {
    const std::string t = trim(text);
    // AAAA-MM-JJTHH:MM:SS[.fff](Z|+HH:MM|-HH:MM)
    if (t.size() < 19 || t[4] != '-' || t[7] != '-' || (t[10] != 'T' && t[10] != 't' && t[10] != ' ') || t[13] != ':' || t[16] != ':') return -1;
    const auto num = [&](std::size_t at, std::size_t n) -> int {
        int v = 0;
        for (std::size_t k = at; k < at + n; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(t[k]))) return -1;
            v = v * 10 + (t[k] - '0');
        }
        return v;
    };
    const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), s = num(17, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) return -1;
    std::size_t at = 19;
    double frac = 0;
    if (at < t.size() && t[at] == '.') {
        std::size_t e = at + 1;
        while (e < t.size() && std::isdigit(static_cast<unsigned char>(t[e]))) ++e;
        frac = std::atof(("0" + t.substr(at, e - at)).c_str());
        at = e;
    }
    long long offset = 0;
    if (at < t.size() && (t[at] == 'Z' || t[at] == 'z')) {
        ++at;
    } else if (at < t.size() && (t[at] == '+' || t[at] == '-')) {
        if (t.size() < at + 6 || t[at + 3] != ':') return -1;
        const int oh = num(at + 1, 2), om = num(at + 4, 2);
        if (oh < 0 || om < 0) return -1;
        offset = (t[at] == '+' ? 1 : -1) * (oh * 3600LL + om * 60LL);
        at += 6;
    } else {
        return -1;   // sans fuseau : illisible (RFC 3339 l'exige)
    }
    if (at != t.size()) return -1;
    const long long epoch = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400LL + h * 3600LL + mi * 60LL + s - offset;
    return std::max(0.0, static_cast<double>(epoch - static_cast<long long>(now)) + frac);
}

ProviderLimits limitsFromHeaders(const HttpHeaders& headers, std::time_t now) {
    ProviderLimits l;
    const auto get = [&](std::string_view name) -> std::string {
        for (const auto& [k, v] : headers)
            if (lower(k) == name) return v;
        return {};
    };
    struct Trio { long long limit{-1}, remaining{-1}; double reset{-1}; };
    const auto trio = [&](std::string_view limit, std::string_view remaining, std::string_view reset) {
        Trio t;
        t.limit = integerOf(get(limit));
        t.remaining = integerOf(get(remaining));
        if (const std::string r = get(reset); !r.empty()) t.reset = resetSeconds(r, now);
        return t;
    };
    // Le plus contraignant de plusieurs : la plus petite part restante (sinon le plus petit reste).
    const auto tighter = [](const Trio& a, const Trio& b) {
        if (b.remaining < 0 && b.limit < 0) return a;
        if (a.remaining < 0 && a.limit < 0) return b;
        const double ra = a.limit > 0 && a.remaining >= 0 ? static_cast<double>(a.remaining) / static_cast<double>(a.limit) : 2.0;
        const double rb = b.limit > 0 && b.remaining >= 0 ? static_cast<double>(b.remaining) / static_cast<double>(b.limit) : 2.0;
        if (ra != rb) return rb < ra ? b : a;
        return b.remaining >= 0 && (a.remaining < 0 || b.remaining < a.remaining) ? b : a;
    };
    // Anthropic.
    Trio req = trio("anthropic-ratelimit-requests-limit", "anthropic-ratelimit-requests-remaining", "anthropic-ratelimit-requests-reset");
    Trio tok = trio("anthropic-ratelimit-tokens-limit", "anthropic-ratelimit-tokens-remaining", "anthropic-ratelimit-tokens-reset");
    tok = tighter(tok, trio("anthropic-ratelimit-input-tokens-limit", "anthropic-ratelimit-input-tokens-remaining", "anthropic-ratelimit-input-tokens-reset"));
    tok = tighter(tok, trio("anthropic-ratelimit-output-tokens-limit", "anthropic-ratelimit-output-tokens-remaining", "anthropic-ratelimit-output-tokens-reset"));
    // Compatibles OpenAI (et par prudence chez Anthropic).
    req = tighter(req, trio("x-ratelimit-limit-requests", "x-ratelimit-remaining-requests", "x-ratelimit-reset-requests"));
    req = tighter(req, trio("x-ratelimit-limit", "x-ratelimit-remaining", "x-ratelimit-reset"));   // OpenRouter
    tok = tighter(tok, trio("x-ratelimit-limit-tokens", "x-ratelimit-remaining-tokens", "x-ratelimit-reset-tokens"));
    l.requestsLimit = req.limit;
    l.requestsRemaining = req.remaining;
    l.requestsResetSeconds = req.reset;
    l.tokensLimit = tok.limit;
    l.tokensRemaining = tok.remaining;
    l.tokensResetSeconds = tok.reset;
    if (const std::string ms = get("retry-after-ms"); !ms.empty()) {
        double n = 0;
        if (number(ms, n) && n >= 0) l.retryAfterSeconds = n / 1000.0;
    }
    if (const std::string ra = get("retry-after"); !ra.empty() && l.retryAfterSeconds < 0) {
        double n = 0;
        if (number(ra, n) && n >= 0) l.retryAfterSeconds = n;
    }
    return l;
}

// ---- les textes ------------------------------------------------------------------------------------
std::string tokensText(long long n) {
    if (n < 0) n = 0;
    char buf[32];
    const auto comma = [](std::string s) {
        std::replace(s.begin(), s.end(), '.', ',');
        return s;
    };
    if (n < 1000) return std::to_string(n);
    if (n < 1000000) {
        const double k = static_cast<double>(n) / 1000.0;
        if (k >= 100.0 || std::fabs(k - std::round(k)) < 0.05) std::snprintf(buf, sizeof buf, "%.0f\xC2\xA0k", std::round(k));
        else std::snprintf(buf, sizeof buf, "%.1f\xC2\xA0k", k);
        return comma(buf);
    }
    const double m = static_cast<double>(n) / 1e6;
    if (std::fabs(m - std::round(m)) < 0.05) std::snprintf(buf, sizeof buf, "%.0f\xC2\xA0M", std::round(m));
    else std::snprintf(buf, sizeof buf, "%.1f\xC2\xA0M", m);
    return comma(buf);
}

std::string waitText(double seconds) {
    const long long s = std::max(0LL, static_cast<long long>(std::ceil(seconds)));
    if (s < 60) return std::to_string(std::max(1LL, s)) + "\xC2\xA0s";
    if (s < 3600) {
        const long long m = (s + 30) / 60;
        return std::to_string(m) + "\xC2\xA0min";
    }
    const long long h = s / 3600, m = (s % 3600) / 60;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld\xC2\xA0h\xC2\xA0%02lld", h, m);
    return buf;
}

bool budgetExceeded(const Settings& s, const UsageTotals& t, std::string* question) {
    if (s.budgetTokens <= 0) return false;
    const bool month = s.budgetPeriod == "mois";
    const long long used = month ? t.tokensMonth : t.tokensToday;
    if (used < s.budgetTokens) return false;
    if (question)
        *question = std::string("Ton budget ") + (month ? "du mois" : "du jour") + " est atteint (" + tokensText(used) + " / " + tokensText(s.budgetTokens)
                    + " jetons). Envoyer quand m\xC3\xAAme\xC2\xA0?";
    return true;
}

UsageBand usageBand(const Settings& s, const LimitState& st, const UsageTotals& t, bool local, double now) {
    UsageBand b;
    if (!providerUsesNetwork(s.provider)) {
        b.lines = {"Sans connexion : gratuit, sans limite"};
        b.shortLines = b.lines;
        b.tooltip = "Cet Assistant ne passe pas par le r\xC3\xA9seau : rien n'est compt\xC3\xA9, rien ne quitte ce PC.";
        return b;
    }
    const bool sameProvider = st.provider.empty() || st.provider == s.provider;
    const ProviderLimits& l = st.limits;
    const bool haveLimits = sameProvider && st.at >= 0 && l.known();
    const double elapsed = st.at >= 0 ? std::max(0.0, now - st.at) : 0.0;
    const auto left = [&](double reset) { return reset < 0 ? -1.0 : std::max(0.0, reset - elapsed); };
    std::string tip;
    // ---- la limite
    std::string l1, s1;
    if (sameProvider && st.at >= 0 && l.retryAfterSeconds > 0 && left(l.retryAfterSeconds) > 0) {
        const std::string w = waitText(left(l.retryAfterSeconds));
        l1 = "Limite atteinte : le service demande d'attendre " + w;
        s1 = "Limite atteinte : attends " + w;
        b.waiting = true;
    } else if (!haveLimits || l.local) {
        if (local || (haveLimits && l.local)) {
            l1 = s1 = "Sur ce PC : pas de limite";
        } else if (sameProvider && st.answered) {
            l1 = "Le service ne dit pas sa limite";
            s1 = "Limite non dite";
        } else {
            l1 = "La limite s'affichera apr\xC3\xA8s la premi\xC3\xA8re r\xC3\xA9ponse";
            s1 = "Limite : apr\xC3\xA8s la 1re r\xC3\xA9ponse";
        }
    } else {
        // 1.16.0 (captures) : une limite annoncee s'affiche, meme d'un serveur de ce PC ou de l'usine (un proxy
        // qui limite le debit) ; "Sur ce PC : pas de limite" seulement quand aucune n'est dite.
        std::vector<std::string> parts, shorts;
        if (l.requestsRemaining >= 0 || l.requestsLimit >= 0) {
            const std::string r = (l.requestsRemaining >= 0 ? std::to_string(l.requestsRemaining) : std::string("?")) + (l.requestsLimit >= 0 ? "/" + std::to_string(l.requestsLimit) : std::string{});
            parts.push_back(r + " requ\xC3\xAA" "tes");
            shorts.push_back(r + " req.");
        }
        if (l.tokensRemaining >= 0 || l.tokensLimit >= 0) {
            const std::string k = (l.tokensRemaining >= 0 ? tokensText(l.tokensRemaining) : std::string("?")) + (l.tokensLimit >= 0 ? "/" + tokensText(l.tokensLimit) : std::string{});
            parts.push_back(k + " jetons");
            shorts.push_back(k + " jetons");
        }
        // La remise de la limite la plus serree (les jetons a egalite).
        const double rr = l.requestsLimit > 0 && l.requestsRemaining >= 0 ? static_cast<double>(l.requestsRemaining) / static_cast<double>(l.requestsLimit) : 2.0;
        const double rt = l.tokensLimit > 0 && l.tokensRemaining >= 0 ? static_cast<double>(l.tokensRemaining) / static_cast<double>(l.tokensLimit) : 2.0;
        double reset = rr < rt ? l.requestsResetSeconds : l.tokensResetSeconds;
        if (reset < 0) reset = std::max(l.requestsResetSeconds, l.tokensResetSeconds);
        std::string when;
        if (reset >= 0) {
            const double rest = left(reset);
            when = rest > 0 ? "remise dans " + waitText(rest) : std::string("remise faite");
        }
        l1 = "Limite : ";
        s1.clear();
        for (std::size_t i = 0; i < parts.size(); ++i) {
            l1 += (i ? " \xC2\xB7 " : "") + parts[i];
            s1 += (i ? " \xC2\xB7 " : "") + shorts[i];
        }
        if (!when.empty()) {
            l1 += " \xC2\xB7 " + when;
            s1 += " \xC2\xB7 " + (when == "remise faite" ? when : waitText(left(reset)));
        }
    }
    b.lines.push_back(l1);
    b.shortLines.push_back(s1);
    // ---- la consommation, le budget, le contexte
    const bool month = s.budgetPeriod == "mois";
    const long long used = month ? t.tokensMonth : t.tokensToday;
    if (st.conversation > 0 || t.tokensToday > 0 || t.tokensMonth > 0 || s.budgetTokens > 0) {
        std::string l2 = "Cette conversation : " + tokensText(st.conversation) + " jetons";
        std::string s2 = "Conv. " + tokensText(st.conversation);
        const std::string period = month ? "ce mois" : "aujourd'hui";
        if (s.budgetTokens > 0) {
            l2 += " \xC2\xB7 " + period + " " + tokensText(used) + " / " + tokensText(s.budgetTokens);
            s2 += " \xC2\xB7 " + tokensText(used) + " / " + tokensText(s.budgetTokens);
            b.ratio = static_cast<double>(used) / static_cast<double>(s.budgetTokens);
            b.level = b.ratio >= 1.0 ? 2 : b.ratio >= 0.8 ? 1 : 0;
        } else {
            l2 += " \xC2\xB7 aujourd'hui " + tokensText(t.tokensToday);
            s2 += " \xC2\xB7 auj. " + tokensText(t.tokensToday);
        }
        b.lines.push_back(l2);
        b.shortLines.push_back(s2);
    }
    if (st.contextWindow > 0 && st.contextUsed > 0) {
        b.lines.push_back("Contexte de la derni\xC3\xA8re demande : " + tokensText(st.contextUsed) + " / " + tokensText(st.contextWindow));
        b.shortLines.push_back("Contexte " + tokensText(st.contextUsed) + " / " + tokensText(st.contextWindow));
    }
    // ---- l'infobulle : le detail exact
    if (haveLimits && !l.local) {
        tip += std::string(local ? "Limite annonc\xC3\xA9" "e par le serveur (sur ce PC ou de l'usine)" : "Limite annonc\xC3\xA9" "e par le service")
               + ", lue \xC3\xA0 " + clockText(st.clock) + " dans les en-t\xC3\xAAtes de sa derni\xC3\xA8re r\xC3\xA9ponse :";
        if (l.requestsRemaining >= 0 || l.requestsLimit >= 0)
            tip += "\n- requ\xC3\xAA" "tes : " + (l.requestsRemaining >= 0 ? thousands(l.requestsRemaining) : std::string("?")) + " restantes sur "
                   + (l.requestsLimit >= 0 ? thousands(l.requestsLimit) : std::string("?"))
                   + (l.requestsResetSeconds >= 0 ? (left(l.requestsResetSeconds) > 0 ? ", remise dans " + waitText(left(l.requestsResetSeconds)) : std::string(", remise faite")) : std::string{});
        if (l.tokensRemaining >= 0 || l.tokensLimit >= 0)
            tip += "\n- jetons : " + (l.tokensRemaining >= 0 ? thousands(l.tokensRemaining) : std::string("?")) + " restants sur "
                   + (l.tokensLimit >= 0 ? thousands(l.tokensLimit) : std::string("?"))
                   + (l.tokensResetSeconds >= 0 ? (left(l.tokensResetSeconds) > 0 ? ", remise dans " + waitText(left(l.tokensResetSeconds)) : std::string(", remise faite")) : std::string{});
    } else if (local || (haveLimits && l.local)) {
        tip += "Le mod\xC3\xA8le tourne sur ce PC (ou sur un serveur de l'usine) : pas de limite de service, pas de co\xC3\xBBt ; la vitesse d\xC3\xA9pend de la machine.";
    } else if (b.waiting) {
        tip += "Le service a r\xC3\xA9pondu 429 (trop de demandes) et demande d'attendre.";
    } else {
        tip += "Le service dit sa limite dans les en-t\xC3\xAAtes de chaque r\xC3\xA9ponse : elle s'affiche d\xC3\xA8s la premi\xC3\xA8re.";
    }
    if (b.waiting && l.retryAfterSeconds > 0) tip += "\nAttente demand\xC3\xA9" "e par le service : " + waitText(left(l.retryAfterSeconds)) + ".";
    if (!st.last.empty())
        tip += "\nDerni\xC3\xA8re r\xC3\xA9ponse : " + thousands(st.last.total()) + " jetons (entr\xC3\xA9" "e " + thousands(st.last.input) + ", sortie "
               + thousands(st.last.output) + (st.last.cacheRead + st.last.cacheWrite > 0 ? ", cache " + thousands(st.last.cacheRead + st.last.cacheWrite) : std::string{}) + ").";
    tip += "\nCette conversation : " + thousands(st.conversation) + " jetons ; aujourd'hui : " + thousands(t.tokensToday) + " (" + std::to_string(t.requestsToday)
           + (t.requestsToday > 1 ? " demandes" : " demande") + ") ; ce mois : " + thousands(t.tokensMonth) + ".";
    if (s.budgetTokens > 0)
        tip += "\nBudget : " + thousands(used) + " / " + thousands(s.budgetTokens) + (month ? " ce mois" : " aujourd'hui") + " ("
               + std::to_string(static_cast<int>(std::lround(b.ratio * 100.0))) + "\xC2\xA0%) ; au-del\xC3\xA0, l'envoi demande confirmation.";
    if (st.contextWindow > 0)
        tip += "\nFen\xC3\xAAtre de contexte du mod\xC3\xA8le : " + thousands(st.contextWindow) + " jetons"
               + (st.contextUsed > 0 ? " ; la derni\xC3\xA8re demande en a utilis\xC3\xA9 " + thousands(st.contextUsed) : std::string{}) + ".";
    tip += "\nLes jetons sont ceux que le service compte ; leur cumul est gard\xC3\xA9 dans IA/usage.json (jamais la demande ni la cl\xC3\xA9).";
    b.tooltip = std::move(tip);
    return b;
}

} // namespace app::ai
