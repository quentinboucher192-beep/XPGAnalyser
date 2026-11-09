// =============================================================================
//  session/SessionJournal.cpp - 1.45.0 : voir SessionJournal.hpp
// =============================================================================
#include "SessionJournal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace session {

namespace fs = std::filesystem;

namespace {

std::int64_t nowSeconds() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::tm localOf(std::int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return tm;
}

const std::string kSep = " \xE2\x80\xBA ";   // " > " (le chevron des chemins)

// Les chemins en UTF-8 (C++20 : u8string est d'un autre type).
fs::path u8p(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string u8s(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

} // namespace

// ============================================================================================== le journal ===
const JournalEntry& Journal::add(int member, const std::string& who, std::uint32_t color, const std::string& text, std::int64_t t) {
    if (t <= 0) t = nowSeconds();
    // Un geste repete (un glisser) : l'entree recente du meme auteur et du meme texte.
    const std::size_t look = std::min<std::size_t>(entries_.size(), 8);
    for (std::size_t i = 0; i < look; ++i) {
        auto& e = entries_[entries_.size() - 1 - i];
        if (e.member == member && e.text == text && t - e.t <= kJournalMerge && t >= e.t) {
            e.t = t;
            ++e.count;
            ++revision_;
            return e;
        }
    }
    JournalEntry e;
    e.n = next_++;
    e.t = t;
    e.member = member;
    e.who = who;
    e.color = color;
    e.text = text;
    entries_.push_back(std::move(e));
    bound();
    ++revision_;
    return entries_.back();
}

void Journal::put(const JournalEntry& e) {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
        if (it->n == e.n) {
            if (*it == e) return;
            *it = e;
            ++revision_;
            return;
        }
    // Dans l'ordre des numeros (une entree plus ancienne qu'on n'a pas : a sa place).
    auto at = entries_.end();
    while (at != entries_.begin() && std::prev(at)->n > e.n) --at;
    entries_.insert(at, e);
    if (e.n >= next_) next_ = e.n + 1;
    bound();
    ++revision_;
}

void Journal::replace(std::vector<JournalEntry> all) {
    std::sort(all.begin(), all.end(), [](const JournalEntry& a, const JournalEntry& b) { return a.n < b.n; });
    entries_ = std::move(all);
    next_ = entries_.empty() ? 1 : entries_.back().n + 1;
    dropped_ = 0;
    bound();
    ++revision_;
}

void Journal::clear() {
    entries_.clear();
    next_ = 1;
    dropped_ = 0;
    ++revision_;
}

void Journal::bound() {
    if (entries_.size() <= kJournalMax) return;
    const std::size_t extra = entries_.size() - kJournalMax;
    entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(extra));
    dropped_ += extra;
}

std::string clockText(std::int64_t t) {
    const std::tm tm = localOf(t);
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return b;
}

std::string lineOf(const JournalEntry& e) {
    std::string s = clockText(e.t) + " " + (e.who.empty() ? std::string("?") : e.who) + " : " + e.text;
    if (e.count > 1) s += " (\xC3\x97" + std::to_string(e.count) + ")";
    return s;
}

core::json::Value toJson(const JournalEntry& e) {
    core::json::Value v = core::json::Value::object();
    v.set("n", static_cast<std::int64_t>(e.n));
    v.set("t", e.t);
    v.set("membre", static_cast<std::int64_t>(e.member));
    v.set("de", e.who);
    v.set("couleur", static_cast<std::int64_t>(e.color));
    v.set("texte", e.text);
    if (e.count > 1) v.set("fois", static_cast<std::int64_t>(e.count));
    return v;
}

bool fromJson(const core::json::Value& v, JournalEntry& e) {
    const auto n = v.integer("n", 0);
    if (n <= 0) return false;
    e.n = static_cast<std::uint64_t>(n);
    e.t = v.integer("t", 0);
    const auto m = v.integer("membre", -1);
    e.member = m < -1 || m > 15 ? -1 : static_cast<int>(m);
    e.who = v.str("de").substr(0, 100);
    e.color = static_cast<std::uint32_t>(v.integer("couleur", 0) & 0xFFFFFF);
    e.text = v.str("texte").substr(0, 500);
    e.count = static_cast<int>(std::clamp<std::int64_t>(v.integer("fois", 1), 1, 1000000));
    return !e.text.empty();
}

namespace {
std::string csvField(const std::string& s) {
    if (s.find_first_of(";\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') out += "\"\"";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out + "\"";
}
} // namespace

std::string journalCsv(const std::vector<JournalEntry>& entries) {
    std::string out = "\xEF\xBB\xBF" "Date;Heure;Auteur;Changement;Gestes\r\n";
    for (const auto& e : entries) {
        const std::tm tm = localOf(e.t);
        char d[48], h[48];
        std::snprintf(d, sizeof d, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        std::snprintf(h, sizeof h, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        out += std::string(d) + ";" + h + ";" + csvField(e.who) + ";" + csvField(e.text) + ";" + std::to_string(e.count) + "\r\n";
    }
    return out;
}

std::string journalFileName(const std::string& sessionName, std::int64_t started) {
    const std::tm tm = localOf(started > 0 ? started : nowSeconds());
    char b[80];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d_%02dh%02d_", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    std::string name;
    for (const char c : sessionName) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == ' ' || c == '-' || c == '_' || u >= 0x80) name.push_back(c);
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.size() > 60) name.resize(60);
    if (name.empty()) name = "Session";
    return std::string(b) + name + ".csv";
}

const char* journalFolder() noexcept { return "ihm/historique/sessions"; }

bool saveJournal(const std::string& projectFolder, const std::string& sessionName, std::int64_t started,
                 const std::vector<JournalEntry>& entries, std::string* path, std::string* why) {
    if (path) path->clear();
    if (entries.empty()) return true;
    if (projectFolder.empty()) {
        if (why) *why = "le projet n'a pas de dossier";
        return false;
    }
    const fs::path dir = u8p(projectFolder) / "ihm" / "historique" / "sessions";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (why) *why = "le dossier " + u8s(dir) + " ne se cr\xC3\xA9" "e pas (" + ec.message() + ")";
        return false;
    }
    fs::path target = dir / u8p(journalFileName(sessionName, started));
    // Deux sessions dans la meme minute : un suffixe.
    for (int k = 2; fs::exists(target, ec) && k < 100; ++k) {
        std::string stem = journalFileName(sessionName, started);
        stem.resize(stem.size() - 4);
        target = dir / u8p(stem + " (" + std::to_string(k) + ").csv");
    }
    const std::string text = journalCsv(entries);
    {
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            if (why) *why = u8s(target) + " ne s'\xC3\xA9" "crit pas";
            return false;
        }
    }
    if (path) *path = u8s(target);
    return true;
}

// ============================================================================================ les phrases ===
namespace {

std::string objName(const hmi::Object* o) { return o && !o->name.empty() ? o->name : std::string("un objet"); }

// Les cles qui different entre deux etats d'un objet (les proprietes, puis le reste en mots).
std::set<std::string> changedKeys(const hmi::Object& a, const hmi::Object& b) {
    std::set<std::string> out;
    std::map<std::string, std::pair<std::string, std::string>> pa, pb;
    for (const auto& p : a.props) pa[p.key] = {p.value, p.expr};
    for (const auto& p : b.props) pb[p.key] = {p.value, p.expr};
    for (const auto& [k, v] : pa) {
        const auto it = pb.find(k);
        if (it == pb.end() || it->second != v) out.insert(k);
    }
    for (const auto& [k, v] : pb) {
        (void)v;
        if (!pa.count(k)) out.insert(k);
    }
    if (a.kind != b.kind) out.insert("genre");
    if (a.layer != b.layer) out.insert("calque");
    if (a.parent != b.parent) out.insert("groupe");
    if (a.locked != b.locked) out.insert("verrou");
    if (a.hidden != b.hidden) out.insert("cach\xC3\xA9");
    if (!(a.actions == b.actions)) out.insert("actions");
    if (!(a.alarmOverrides == b.alarmOverrides)) out.insert("alarmes");
    if (!(a.functionOverrides == b.functionOverrides)) out.insert("fonctions");
    return out;
}

bool within(const std::set<std::string>& keys, std::initializer_list<const char*> allowed) {
    for (const auto& k : keys) {
        bool ok = false;
        for (const char* a : allowed) ok = ok || k == a;
        if (!ok) return false;
    }
    return true;
}

std::string plural(const std::string& verb) {
    // "deplace" -> "deplaces" ; "modifie (x, y)" n'est pas groupe.
    return verb + "s";
}

// Le nom lisible d'un fichier du projet : "0012-logo_site.png" -> "logo_site.png" ; "0031-Init.st" -> "Init".
std::string leafName(const std::string& path, bool keepExtension) {
    std::string leaf = path.substr(path.find_last_of('/') + 1);
    std::size_t k = 0;
    while (k < leaf.size() && std::isdigit(static_cast<unsigned char>(leaf[k]))) ++k;
    if (k > 0 && k < leaf.size() && leaf[k] == '-') leaf.erase(0, k + 1);
    if (!keepExtension)
        if (const auto dot = leaf.find_last_of('.'); dot != std::string::npos && dot > 0) leaf.resize(dot);
    return leaf.empty() ? path : leaf;
}

// L'identifiant en tete d'un nom de fichier ("0031-Init.st" -> 31) ; 0 : aucun.
std::uint64_t leadingId(const std::string& path) {
    const std::string leaf = path.substr(path.find_last_of('/') + 1);
    std::uint64_t v = 0;
    std::size_t k = 0;
    while (k < leaf.size() && std::isdigit(static_cast<unsigned char>(leaf[k])) && k < 12) v = v * 10 + static_cast<unsigned>(leaf[k++] - '0');
    return k > 0 && k < leaf.size() && leaf[k] == '-' ? v : 0;
}

// Une ligne de l'index (ihm.txt) : son mot, son identifiant, son nom.
struct IndexLine {
    std::string word, name;
    std::uint64_t id{0};
};

std::string fieldValue(const std::string& line, const std::string& key) {
    const std::string probe = " " + key + "=";
    const auto at = line.find(probe);
    if (at == std::string::npos) return {};
    std::size_t i = at + probe.size();
    std::string out;
    if (i < line.size() && line[i] == '"') {
        ++i;
        while (i < line.size() && line[i] != '"') {
            if (line[i] == '\\' && i + 1 < line.size()) ++i;
            out.push_back(line[i++]);
        }
        return out;
    }
    while (i < line.size() && line[i] != ' ') out.push_back(line[i++]);
    return out;
}

bool parseIndexLine(const std::string& line, IndexLine& out) {
    std::size_t k = 0;
    while (k < line.size() && (std::islower(static_cast<unsigned char>(line[k])) || line[k] == '_')) ++k;
    if (k == 0) return false;
    out.word = line.substr(0, k);
    out.name = fieldValue(line, "nom");
    const std::string id = fieldValue(line, "id");
    out.id = 0;
    for (const char c : id) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            out.id = 0;
            break;
        }
        out.id = out.id * 10 + static_cast<unsigned>(c - '0');
    }
    return true;
}

// Le genre d'une ligne de l'index : son nom en clair et son accord ; vide : un reglage du projet.
struct Noun {
    const char* word;
    const char* label;
    bool feminine;
};
constexpr Noun kNouns[] = {
    {"variable", "variable", true},       {"ressource", "ressource", true},   {"script", "script", false},
    {"fonction", "fonction IHM", true},   {"recette", "recette", true},       {"utilisateur", "utilisateur", false},
    {"equipement", "\xC3\xA9quipement", false}, {"type_ihm", "type IHM", false}, {"style", "style", false},
    {"groupe_alarmes", "groupe d'alarmes", false}, {"essai", "essai", false}, {"rapport", "rapport", false},
    {"langue", "langue", true},           {"role", "r\xC3\xB4le", false},     {"externe", "fichier externe", false},
};
const Noun* nounOf(const std::string& word) {
    for (const auto& n : kNouns)
        if (word == n.word) return &n;
    return nullptr;
}

std::string agreed(const char* verb, bool feminine) { return std::string(verb) + (feminine ? "e" : ""); }

} // namespace

std::vector<std::string> describeView(const ViewDelta& d, const hmi::View* before, const std::string& viewName) {
    std::vector<std::string> out;
    const std::string head = viewName.empty() ? std::string("une vue") : viewName;
    // Par geste : les objets qui l'ont subi (plus de 3 : un compte).
    std::vector<std::pair<std::string, std::vector<std::string>>> byVerb;
    const auto note = [&](const std::string& verb, const std::string& name) {
        for (auto& [v, names] : byVerb)
            if (v == verb) {
                names.push_back(name);
                return;
            }
        byVerb.push_back({verb, {name}});
    };
    for (const auto& o : d.objects) {
        const hmi::Object* prev = before ? before->object(o.id) : nullptr;
        if (!prev) {
            note("ajout\xC3\xA9", objName(&o));
            continue;
        }
        if (prev->name != o.name) {
            out.push_back(head + kSep + objName(prev) + " renomm\xC3\xA9 en " + objName(&o));
            if (within(changedKeys(*prev, o), {})) continue;
        }
        const auto keys = changedKeys(*prev, o);
        if (keys.empty()) continue;
        if (within(keys, {"x", "y"})) note("d\xC3\xA9plac\xC3\xA9", objName(&o));
        else if (within(keys, {"x", "y", "w", "h"})) note("redimensionn\xC3\xA9", objName(&o));
        else if (within(keys, {"rot", "x", "y"})) note("tourn\xC3\xA9", objName(&o));
        else {
            std::string what;
            std::size_t shown = 0;
            for (const auto& k : keys) {
                if (shown == 3) {
                    what += ", \xE2\x80\xA6";
                    break;
                }
                what += (shown ? ", " : "") + k;
                ++shown;
            }
            out.push_back(head + kSep + objName(&o) + " modifi\xC3\xA9 (" + what + ")");
        }
    }
    for (const auto id : d.removed) note("supprim\xC3\xA9", objName(before ? before->object(id) : nullptr));
    for (const auto& [verb, names] : byVerb) {
        if (names.size() > 3) {
            out.push_back(head + kSep + std::to_string(names.size()) + " objets " + plural(verb));
            continue;
        }
        for (const auto& n : names) out.push_back(head + kSep + n + " " + verb);
    }
    if (d.shellChanged) {
        if (before && before->name != d.shell.name && !before->name.empty())
            out.push_back("vue " + before->name + " renomm\xC3\xA9" "e en " + d.shell.name);
        else
            out.push_back(head + " : propri\xC3\xA9t\xC3\xA9s de la vue modifi\xC3\xA9" "es");
    }
    if (d.orderChanged && out.empty()) out.push_back(head + " : ordre de dessin chang\xC3\xA9");
    return out;
}

std::vector<std::string> describeFiles(const ProjectDelta& pd, const std::function<bool(const std::string&)>& existed) {
    std::vector<std::string> out;
    // L'index (ihm.txt) : les lignes ajoutees et retirees, par (mot, identifiant).
    struct Seen {
        std::string name;
        bool added{false}, removed{false};
    };
    std::map<std::pair<std::string, std::uint64_t>, Seen> seen;
    std::vector<std::pair<std::string, std::uint64_t>> order;
    bool settings = false;
    for (const auto& f : pd.files) {
        if (f.path != "ihm.txt" || f.kind != FileDelta::Kind::Patch) continue;
        const auto take = [&](const std::string& line, bool add) {
            IndexLine il;
            if (!parseIndexLine(line, il)) return;
            if (il.word == "vue" || il.word == "fin" || il.word == "ihm") return;   // les vues : l'appelant
            if (!nounOf(il.word) || il.id == 0) {
                settings = true;
                return;
            }
            const auto key = std::make_pair(il.word, il.id);
            auto [it, fresh] = seen.try_emplace(key);
            if (fresh) order.push_back(key);
            if (it->second.name.empty() || add) it->second.name = il.name;
            (add ? it->second.added : it->second.removed) = true;
        };
        for (const auto& h : f.hunks) {
            for (const auto& l : h.removed) take(l, false);
            for (const auto& l : h.added) take(l, true);
        }
    }
    for (const auto& key : order) {
        const auto& s = seen[key];
        const Noun* n = nounOf(key.first);
        const std::string name = s.name.empty() ? "n\xC2\xB0" + std::to_string(key.second) : s.name;
        const char* verb = s.added && s.removed ? "modifi\xC3\xA9" : s.added ? "ajout\xC3\xA9" : "supprim\xC3\xA9";
        out.push_back(std::string(n->label) + " " + name + " " + agreed(verb, n->feminine));
    }
    // Les fichiers : le corps d'un script ou d'une fonction, une ressource (entiere), un autre fichier.
    for (const auto& f : pd.files) {
        if (f.path == "ihm.txt") continue;
        const auto dir = f.path.substr(0, f.path.find('/'));
        const std::uint64_t id = leadingId(f.path);
        const char* word = dir == "ressources" ? "ressource" : dir == "scripts" ? "script" : dir == "fonctions" ? "fonction" : nullptr;
        if (word && id && seen.count({word, id})) continue;   // l'index l'a deja dit
        if (dir == "ressources") {
            const bool was = existed && existed(f.path);
            const char* verb = f.kind == FileDelta::Kind::Removed ? "retir\xC3\xA9" "e" : was ? "remplac\xC3\xA9" "e" : "ajout\xC3\xA9" "e";
            out.push_back("ressource " + leafName(f.path, true) + " " + verb);
        } else if (dir == "scripts" || dir == "fonctions") {
            const bool fn = dir == "fonctions";
            const bool was = !existed || existed(f.path);
            const char* verb = f.kind == FileDelta::Kind::Removed ? "supprim\xC3\xA9" : was ? "modifi\xC3\xA9" : "ajout\xC3\xA9";
            out.push_back(std::string(fn ? "fonction IHM " : "script ") + leafName(f.path, false) + " " + agreed(verb, fn));
        } else if (dir == "vues") {
            continue;   // une vue neuve : l'appelant la nomme
        } else {
            out.push_back("fichier " + f.path + (f.kind == FileDelta::Kind::Removed ? " supprim\xC3\xA9" : " modifi\xC3\xA9"));
        }
    }
    if (settings) out.push_back("r\xC3\xA9glages du projet modifi\xC3\xA9s");
    return out;
}

std::vector<std::string> trimmed(std::vector<std::string> lines) {
    std::vector<std::string> out;
    for (auto& l : lines)
        if (!l.empty() && std::find(out.begin(), out.end(), l) == out.end()) out.push_back(std::move(l));
    constexpr std::size_t kMax = 6;
    if (out.size() > kMax) {
        const std::size_t more = out.size() - (kMax - 1);
        out.resize(kMax - 1);
        out.push_back("\xE2\x80\xA6 et " + std::to_string(more) + " autres changements");
    }
    return out;
}

// ============================================================================ le dossier des copies ===
std::string guestArea(const std::string& sharedFolder) {
    if (sharedFolder.empty()) return {};
    return (fs::path(sharedFolder) / "Sessions").string();   // comme le dossier ou le projet recu arrive (app/SessionService)
}

bool insideArea(const std::string& area, const std::string& path) {
    if (area.empty() || path.empty()) return false;
    const fs::path a = fs::path(area).lexically_normal(), p = fs::path(path).lexically_normal();
    auto ai = a.begin(), pi = p.begin();
    for (; ai != a.end(); ++ai, ++pi) {
        if (ai->empty()) continue;   // la barre finale
        if (pi == p.end() || *ai != *pi) return false;
    }
    return true;
}

bool removeCopy(const std::string& area, const std::string& sessionId, std::string* why) {
    if (area.empty() || sessionId.empty() || sessionId.find_first_of("/\\.:") != std::string::npos) return true;
    const fs::path dir = fs::path(area) / fs::path(sessionId);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return true;
    fs::remove_all(dir, ec);
    if (ec) {
        if (why) *why = dir.string() + " : " + ec.message();
        return false;
    }
    return true;
}

std::size_t purgeArea(const std::string& area, const std::function<bool(const std::string&)>& keep) {
    if (area.empty()) return 0;
    std::error_code ec;
    const fs::path base = fs::path(area);
    if (!fs::is_directory(base, ec)) return 0;
    std::vector<fs::path> gone;
    for (auto it = fs::directory_iterator(base, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path dir = it->path();
        bool kept = false;
        if (keep) {
            std::error_code ec2;
            for (auto sub = fs::directory_iterator(dir, ec2); !ec2 && sub != fs::directory_iterator(); sub.increment(ec2))
                kept = kept || keep(sub->path().string());
            kept = kept || keep(dir.string());
        }
        if (!kept) gone.push_back(dir);
    }
    std::size_t n = 0;
    for (const auto& dir : gone) {
        std::error_code ec2;
        fs::remove_all(dir, ec2);
        if (!ec2) ++n;
    }
    return n;
}

} // namespace session
