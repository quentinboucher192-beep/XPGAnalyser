#include "Novelties.hpp"

#include "ReleaseNotes.hpp"

#include <algorithm>
#include <cstdlib>

namespace help::news {

namespace {


// Les champs d'une version, en nombres ("1.10.0" -> 1, 10, 0).
std::vector<long> fields(std::string_view v) {
    std::vector<long> out;
    std::size_t i = 0;
    while (i <= v.size()) {
        const auto dot = v.find('.', i);
        const auto part = v.substr(i, dot == std::string_view::npos ? std::string_view::npos : dot - i);
        long n = 0;
        for (const char c : part) {
            if (c < '0' || c > '9') break;
            n = n * 10 + (c - '0');
        }
        out.push_back(n);
        if (dot == std::string_view::npos) break;
        i = dot + 1;
    }
    return out;
}

bool contains(const std::vector<std::string>& v, std::string_view x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) {
        if (s.empty()) continue;
        if (!out.empty()) out += ',';
        out += s;
    }
    return out;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        auto c = s.find(',', i);
        if (c == std::string::npos) c = s.size();
        std::string part = s.substr(i, c - i);
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (!part.empty() && !contains(out, part)) out.push_back(std::move(part));
        i = c + 1;
    }
    return out;
}

} // namespace

// 1.11 (chantier T2) : LE REGISTRE est la table des notes de version
// (help/ReleaseNotes) : ses lignes marquees news, dans leur ordre. Une
// nouveaute de plus : sa ligne dans ReleaseNotes.cpp, avec card(...).
const std::vector<Item>& all() {
    static const std::vector<Item> items = [] {
        std::vector<Item> out;
        for (const auto& n : notes::all()) {
            if (!n.news) continue;
            out.push_back(Item{std::string(n.id), std::string(n.version), std::string(n.title), std::string(n.text),
                               std::string(n.image), std::string(n.go), std::string(n.widget), std::string(n.topic)});
        }
        return out;
    }();
    return items;
}

const Item* find(std::string_view id) {
    for (const auto& i : all())
        if (i.id == id) return &i;
    return nullptr;
}

std::vector<const Item*> ofVersion(std::string_view version) {
    std::vector<const Item*> out;
    for (const auto& i : all())
        if (compareVersions(i.version, version) == 0) out.push_back(&i);
    return out;
}

std::vector<std::string> versions() {
    std::vector<std::string> out;
    for (const auto& i : all())
        if (std::none_of(out.begin(), out.end(), [&](const std::string& v) { return compareVersions(v, i.version) == 0; }))
            out.push_back(i.version);
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return compareVersions(a, b) > 0; });
    return out;
}

int compareVersions(std::string_view a, std::string_view b) {
    auto x = fields(a), y = fields(b);
    const std::size_t n = std::max(x.size(), y.size());
    x.resize(n, 0);
    y.resize(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (x[i] < y[i]) return -1;
        if (x[i] > y[i]) return 1;
    }
    return 0;
}

std::string shortVersion(std::string_view v) {
    std::string s(v);
    while (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0 && std::count(s.begin(), s.end(), '.') > 1) s.resize(s.size() - 2);
    return s;
}

std::string versionBefore(std::string_view v) {
    std::string best = "0";
    for (const auto& x : versions())
        if (compareVersions(x, v) < 0 && compareVersions(x, best) > 0) best = x;
    return best;
}

bool onLaunch(State& s, std::string_view current, bool existingProfile) {
    // 1.10.1 : la version du registre qui porte les nouveautes de `current` (un
    // correctif, 1.10.1, n'en a pas a lui : ce sont celles de la 1.10.0).
    std::string carrier = "0";
    for (const auto& v : versions())
        if (compareVersions(v, current) <= 0 && compareVersions(v, carrier) > 0) carrier = v;
    std::string previous = s.lastVersion;
    if (previous.empty()) previous = existingProfile ? std::string(kUnknownBefore) : versionBefore(carrier == "0" ? std::string(current) : carrier);
    if (compareVersions(current, previous) <= 0) {
        // La meme version (ou une plus ancienne, relancee a cote) : rien ne s'ouvre.
        if (s.lastVersion.empty()) s.lastVersion = std::string(current);
        if (s.baseline.empty()) s.baseline = previous;
        return false;
    }
    s.lastVersion = std::string(current);
    s.marksHidden = false;          // une nouvelle version : de nouveaux reperes
    // 1.10.1 : une version sans nouveaute depuis la derniere lancee (1.10.0 -> 1.10.1)
    // garde la reference : les nouveautes de la 1.10 et leurs reperes orange restent.
    if (compareVersions(carrier, previous) <= 0 && !s.baseline.empty()) return false;
    s.baseline = previous;
    return !pending(s, current).empty();
}

bool isNew(const State& s, std::string_view version) {
    if (version.empty()) return false;
    return compareVersions(version, s.baseline.empty() ? std::string_view("0") : std::string_view(s.baseline)) > 0;
}

std::vector<const Item*> pending(const State& s, std::string_view current) {
    std::vector<const Item*> out;
    for (const auto& v : versions()) {
        if (compareVersions(v, current) > 0 || !isNew(s, v)) continue;
        for (const auto* i : ofVersion(v)) out.push_back(i);
    }
    return out;
}

bool markShown(const State& s, const Item& item) {
    return !s.marksHidden && !item.widget.empty() && isNew(s, item.version) && !wasUsed(s, item.id);
}

std::vector<const Item*> marksFor(const State& s, std::string_view widgetId) {
    std::vector<const Item*> out;
    if (widgetId.empty()) return out;
    for (const auto& i : all())
        if (i.widget == widgetId && markShown(s, i)) out.push_back(&i);
    return out;
}

bool helpIsNew(const State& s, std::string_view since) {
    if (since.empty() || !isNew(s, since)) return false;
    return s.helpRead.empty() || compareVersions(since, s.helpRead) > 0;
}

void markSeen(State& s, std::string_view id) {
    if (!id.empty() && !contains(s.seen, id)) s.seen.emplace_back(id);
}

void markUsed(State& s, std::string_view id) {
    if (!id.empty() && !contains(s.used, id)) s.used.emplace_back(id);
}

bool wasSeen(const State& s, std::string_view id) { return contains(s.seen, id); }
bool wasUsed(const State& s, std::string_view id) { return contains(s.used, id); }

void markAllSeen(State& s, std::string_view current) {
    for (const auto* i : pending(s, current)) markSeen(s, i->id);
}

void markHelpRead(State& s, std::string_view current) { s.helpRead = std::string(current); }

std::string& sessionVersion() {
    static std::string v = "1.10.0";
    return v;
}

State& session() {
    // Tant que l'application ne l'a pas charge (les tests, un ecran construit
    // sans elle) : rien n'est nouveau - pas d'orange par surprise.
    static State s = [] {
        State x;
        x.lastVersion = x.baseline = sessionVersion();
        return x;
    }();
    return s;
}

std::string label(std::string_view version) {
    return "NOUVEAU \xC2\xB7 " + shortVersion(version);
}

void load(State& s, const std::function<std::string(const std::string&)>& get) {
    s.lastVersion = get("nouveautes.version");
    s.baseline = get("nouveautes.depuis");
    s.helpRead = get("nouveautes.aideLue");
    s.seen = split(get("nouveautes.vues"));
    s.used = split(get("nouveautes.utilisees"));
    const auto b = [&](const char* key) {
        const auto v = get(key);
        return v == "1" || v == "true" || v == "yes" || v == "on";
    };
    s.marksHidden = b("nouveautes.reperesMasques");
    s.helpOnlyNew = b("nouveautes.aideFiltre");
    s.helpNotation = get("aide.notation");
}

void save(const State& s, const std::function<void(const std::string&, const std::string&)>& set) {
    set("nouveautes.version", s.lastVersion);
    set("nouveautes.depuis", s.baseline);
    set("nouveautes.aideLue", s.helpRead);
    set("nouveautes.vues", join(s.seen));
    set("nouveautes.utilisees", join(s.used));
    set("nouveautes.reperesMasques", s.marksHidden ? "true" : "false");
    set("nouveautes.aideFiltre", s.helpOnlyNew ? "true" : "false");
    set("aide.notation", s.helpNotation);
}

} // namespace help::news
