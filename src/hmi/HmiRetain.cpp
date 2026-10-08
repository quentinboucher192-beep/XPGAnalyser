// =============================================================================
//  hmi/HmiRetain.cpp - 1.11.16 : la remanence d'exploitation (voir le .hpp)
// =============================================================================
#include "HmiRetain.hpp"

#include "HmiEnums.hpp"
#include "HmiHistory.hpp"
#include "HmiStore.hpp"
#include "HmiTypes.hpp"
#include "../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <map>
#include <set>

#ifdef _WIN32
#include <process.h>
#define XPG_RETAIN_PID _getpid
#else
#include <unistd.h>
#define XPG_RETAIN_PID getpid
#endif

namespace hmi::retain {
namespace fs = std::filesystem;
namespace {

std::string str(const Record& r, const char* key) {
    const auto* v = r.get(key);
    return v ? *v : std::string{};
}
long long num(const Record& r, const char* key, long long fallback) {
    const auto* v = r.get(key);
    if (!v || v->empty()) return fallback;
    long long n = 0;
    const auto res = std::from_chars(v->data(), v->data() + v->size(), n);
    return res.ec == std::errc{} && res.ptr == v->data() + v->size() ? n : fallback;
}
std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Un titre de colonne, pour le reconnaitre : sans espaces ni ponctuation, en
// minuscules, un e ou un a accentues (UTF-8) ramenes a leur lettre.
std::string titleKey(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[++i]);
            if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) out += 'e';
            else if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) out += 'a';
            continue;
        }
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}
std::vector<std::string_view> linesOf(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto nl = text.find('\n', at);
        std::string_view line = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        at = nl == std::string_view::npos ? text.size() : nl + 1;
    }
    return out;
}
// ---- le verrou ----
long currentPid() { return static_cast<long>(XPG_RETAIN_PID()); }
fs::path lockOf(const fs::path& file) {
    fs::path l = file;
    l.replace_extension(".lock");
    return l;
}
std::string mineKey() { return "pid=" + std::to_string(currentPid()) + " "; }
// Le verrou d'un autre, encore frais : "PID 4120, depuis ..." ; vide sinon (le notre,
// aucun, illisible, ou perime : personne ne l'a rafraichi depuis 3 minutes).
std::string otherOwner(const fs::path& lock) {
    std::string text;
    if (!core::readFileAll(lock, text)) return {};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    Record r;
    std::string why;
    if (text.empty() || !parseRecord(text, r, why)) return {};
    const long other = static_cast<long>(num(r, "pid", 0));
    if (other == 0 || other == currentPid()) return {};
    std::error_code ec;
    const auto age = fs::last_write_time(lock, ec);
    if (ec || fs::file_time_type::clock::now() - age > std::chrono::minutes(3)) return {};
    const std::string since = str(r, "depuis");
    return "PID " + std::to_string(other) + (since.empty() ? std::string{} : ", depuis " + since);
}

std::string_view withoutBom(std::string_view t) {
    if (t.size() >= 3 && t.substr(0, 3) == "\xEF\xBB\xBF") t.remove_prefix(3);
    return t;
}

// Les cases d'une variable a ses valeurs par defaut, sous son type d'aujourd'hui :
// le retour s'y essaie (les memes regles que le poste) sans toucher a rien.
std::map<std::string, sim::Value> blankCells(const Project& p, const Variable& var) {
    std::map<std::string, sim::Value> out;
    const auto simple = [](const std::string& type) { return sim::Value::defaultOf(sim::typeFromName(type == "LREAL" ? "REAL" : type)); };
    if (findEnumeration(p, var.type)) {
        out[var.name] = sim::Value::defaultOf(sim::Type::DInt);
        return out;
    }
    if (!types::isComposite(var.type)) {
        out[var.name] = simple(var.type);
        return out;
    }
    std::string error;
    for (const auto& leaf : types::leafVariables(p, var, nullptr, nullptr, &error)) out[leaf.name] = simple(leaf.type);
    return out;
}

} // namespace

const Entry* Store::find(Id variable, std::string_view path) const {
    for (const auto& e : entries)
        if (e.cell.variable == variable && e.cell.path == path) return &e;
    return nullptr;
}

std::string serialize(const Store& s) {
    std::string out(kHeader);
    out += "\n# XPGAnalyser : les variables remanentes du poste d'exploitation. Ecrit par le poste.\n";
    out += "poste projet=" + quote(s.project) + " date=" + quote(s.date) + "\n";
    for (const auto& e : s.entries)
        out += "valeur variable=" + std::to_string(e.cell.variable) + " nom=" + quote(e.cell.name) + " declare=" + quote(e.cell.declared)
             + " chemin=" + quote(e.cell.path) + " type=" + std::string(sim::toString(e.cell.value.type())) + " valeur=" + quote(simdata::valueText(e.cell.value))
             + " date=" + quote(e.date) + "\n";
    out += "fin valeurs=" + std::to_string(s.entries.size()) + "\n";
    return out;
}

bool parse(std::string_view text, Store& out, std::string* why) {
    out = Store{};
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        out = Store{};
        return false;
    };
    std::size_t at = 0, lineNo = 0;
    bool header = false, end = false;
    while (at < text.size()) {
        const auto nl = text.find('\n', at);
        std::string_view line = text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        at = nl == std::string_view::npos ? text.size() : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        if (!header) {
            if (line != kHeader) return fail("ce n'est pas le fichier des variables r\xC3\xA9manentes d'un poste");
            header = true;
            continue;
        }
        if (end) return fail("des lignes apr\xC3\xA8s la fin (ligne " + std::to_string(lineNo) + ")");
        Record r;
        std::string error;
        if (!parseRecord(line, r, error)) return fail("ligne " + std::to_string(lineNo) + " illisible : " + error);
        if (r.word == "poste") {
            out.project = str(r, "projet");
            out.date = str(r, "date");
        } else if (r.word == "valeur") {
            Entry e;
            e.cell.variable = static_cast<Id>(num(r, "variable", static_cast<long long>(kNoId)));
            e.cell.name = str(r, "nom");
            e.cell.declared = str(r, "declare");
            e.cell.path = str(r, "chemin");
            auto v = simdata::valueFrom(sim::typeFromName(str(r, "type")), str(r, "valeur"));
            if (e.cell.variable == kNoId || !v) return fail("valeur illisible (ligne " + std::to_string(lineNo) + ")");
            e.cell.value = std::move(*v);
            e.date = str(r, "date");
            out.entries.push_back(std::move(e));
        } else if (r.word == "fin") {
            if (num(r, "valeurs", -1) != static_cast<long long>(out.entries.size()))
                return fail("fichier incomplet : la fin ne compte pas les m\xC3\xAAmes valeurs");
            end = true;
        }
        // un autre mot : d'une version plus recente, ignore
    }
    if (!header) return fail("fichier vide");
    if (!end) return fail("fichier coup\xC3\xA9 (pas de ligne de fin)");
    return true;
}

fs::path fileOf(const std::string& projectFolder) {
    if (projectFolder.empty()) return {};
    return fs::path(projectFolder) / "ihm" / "historique" / std::string(kFileName);
}

bool load(const fs::path& file, Store& out, std::string* why, bool* fromBackup) {
    if (fromBackup) *fromBackup = false;
    std::error_code ec;
    const bool exists = !file.empty() && fs::exists(file, ec);
    std::string text, first;
    if (exists && core::readFileAll(file, text) && parse(text, out, &first)) return true;
    if (exists && first.empty()) first = "illisible";
    fs::path bak = file;
    bak += ".bak";
    std::string backup, second;
    if (!file.empty() && core::readFileAll(bak, backup) && parse(backup, out, &second)) {
        if (fromBackup) *fromBackup = true;
        if (why) *why = exists ? first : std::string("fichier absent");
        return true;
    }
    out = Store{};
    if (why) *why = exists ? first : std::string("aucune valeur gard\xC3\xA9" "e (pas de fichier)");
    return false;
}

core::Status save(const fs::path& file, const Store& store) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return core::writeFileAtomic(file, serialize(store));
}

std::vector<simdata::Cell> captureRetained(const Project& p, const simdata::Reader& read) {
    return simdata::captureVariables(p, read, [](const Variable& v) { return v.retain; });
}

std::vector<simdata::Cell> retainedCells(const Project& p, const Store& store, int* ignored) {
    std::map<Id, const Variable*> byId;
    for (const auto& v : p.programs.variables) byId[v.id] = &v;
    std::vector<simdata::Cell> out;
    std::set<Id> notRetained;
    for (const auto& e : store.entries) {
        const auto it = byId.find(e.cell.variable);
        // Une variable supprimee passe : le retour la compte (ignoree) ; une qui n'est plus
        // remanente (ou liee a un equipement) : sa valeur gardee est ignoree ici.
        if (it != byId.end() && (!it->second->retain || it->second->bound())) {
            notRetained.insert(e.cell.variable);
            continue;
        }
        out.push_back(e.cell);
    }
    if (ignored) *ignored = static_cast<int>(notRetained.size());
    return out;
}

bool merge(Store& store, const Project&, const std::vector<simdata::Cell>& cells, const std::string& now) {
    bool changed = cells.size() != store.entries.size();
    std::vector<Entry> next;
    next.reserve(cells.size());
    for (const auto& c : cells) {
        Entry e{c, now};
        const Entry* old = store.find(c.variable, c.path);
        if (old && old->cell.value.type() == c.value.type() && old->cell.value.equals(c.value) && old->cell.declared == c.declared
            && old->cell.name == c.name)
            e.date = old->date;
        else
            changed = true;
        next.push_back(std::move(e));
    }
    if (changed) {
        store.entries = std::move(next);
        store.date = now;
    }
    return changed;
}

VariableState stateOf(const Project& p, const Variable& var, const Store* store) {
    VariableState st;
    std::vector<simdata::Cell> cells;
    if (store)
        for (const auto& e : store->entries)
            if (e.cell.variable == var.id) {
                cells.push_back(e.cell);
                if (e.date > st.date) st.date = e.date;
            }
    st.saved = !cells.empty();
    if (st.saved)
        st.value = cells.size() == 1 && cells.front().path.empty() ? simdata::valueText(cells.front().value)
                                                                    : plural(cells.size(), "case", "cases");
    if (!var.retain) {
        st.state = st.saved ? "non r\xC3\xA9manente : la valeur gard\xC3\xA9" "e sera ignor\xC3\xA9" "e au prochain lancement du poste"
                            : "non r\xC3\xA9manente";
        return st;
    }
    if (var.bound()) {
        st.state = "li\xC3\xA9" "e \xC3\xA0 " + var.equipment + " : sa valeur vient de l'\xC3\xA9quipement (non gard\xC3\xA9" "e)";
        st.problem = true;
        return st;
    }
    if (!st.saved) {
        st.state = store ? "jamais sauvegard\xC3\xA9" "e (aucun changement sur le poste pour l'instant)"
                         : "jamais sauvegard\xC3\xA9" "e (pas encore de fichier sur ce poste)";
        return st;
    }
    // Le retour, essaye a vide sous le type d'aujourd'hui : les memes regles que le poste.
    auto blank = blankCells(p, var);
    const auto rep = simdata::restoreVariables(p, cells, [&blank](const std::string& path) -> sim::Value* {
        const auto it = blank.find(path);
        return it == blank.end() ? nullptr : &it->second;
    });
    if (rep.incompatible > 0) {
        st.problem = true;
        st.state = "type chang\xC3\xA9 (" + cells.front().declared + " devient " + var.type + ") : incompatible \xE2\x80\x94 la valeur initiale sera reprise";
    } else if (rep.converted > 0) {
        st.state = "type chang\xC3\xA9 (" + cells.front().declared + " devient " + var.type + ") : la valeur sera convertie au prochain lancement";
    } else {
        st.state = "\xC3\xA0 jour : rendue au prochain lancement du poste";
    }
    return st;
}

Lock acquireLock(const fs::path& file) {
    Lock l;
    if (file.empty()) {
        l.error = "pas de stockage";
        return l;
    }
    const fs::path lock = lockOf(file);
    if (auto other = otherOwner(lock); !other.empty()) {
        l.owner = std::move(other);
        return l;
    }
    std::error_code ec;
    fs::create_directories(lock.parent_path(), ec);
    const auto st = core::writeFileAtomic(lock, "verrou " + mineKey() + "depuis=" + quote(simdata::nowStamp()) + "\n", core::AtomicWrite{false, false});
    if (!st) {
        l.error = st.error().message();
        return l;
    }
    l.held = true;
    return l;
}

void refreshLock(const fs::path& file) {
    if (file.empty()) return;
    const fs::path lock = lockOf(file);
    std::string text;
    std::error_code ec;
    if (core::readFileAll(lock, text) && text.find(mineKey()) != std::string::npos) {
        fs::last_write_time(lock, fs::file_time_type::clock::now(), ec);
        if (!ec) return;
    }
    // Disparu (ou repris par erreur) : le notre, de nouveau.
    (void)core::writeFileAtomic(lock, "verrou " + mineKey() + "depuis=" + quote(simdata::nowStamp()) + "\n", core::AtomicWrite{false, false});
}

void releaseLock(const fs::path& file) {
    if (file.empty()) return;
    const fs::path lock = lockOf(file);
    std::string text;
    if (core::readFileAll(lock, text) && text.find(mineKey()) == std::string::npos) return;   // pas le notre : on n'y touche pas
    std::error_code ec;
    fs::remove(lock, ec);
}

std::string lockedBy(const fs::path& file) { return file.empty() ? std::string{} : otherOwner(lockOf(file)); }

std::string toCsv(const Store& s) {
    std::string out = "\xEF\xBB\xBF" "Variable;Chemin;Type;Valeur;Date;Type d\xC3\xA9" "clar\xC3\xA9;Id\n";
    for (const auto& e : s.entries) {
        std::string value = simdata::valueText(e.cell.value);
        if (e.cell.value.type() == sim::Type::Real) std::replace(value.begin(), value.end(), '.', ',');
        // Excel ferait une formule d'un texte qui commence par = + - @ : l'apostrophe l'en garde.
        if (e.cell.value.type() == sim::Type::String && !value.empty() && (value[0] == '=' || value[0] == '+' || value[0] == '-' || value[0] == '@'))
            value.insert(value.begin(), '\'');
        out += csvField(e.cell.name) + ";" + csvField(e.cell.path) + ";" + std::string(sim::toString(e.cell.value.type())) + ";" + csvField(value) + ";"
             + csvField(e.date) + ";" + csvField(e.cell.declared) + ";" + std::to_string(e.cell.variable) + "\n";
    }
    return out;
}

bool fromCsv(std::string_view text, Store& out, std::string* why) {
    out = Store{};
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        out = Store{};
        return false;
    };
    const auto lines = linesOf(withoutBom(text));
    std::size_t i = 0;
    while (i < lines.size() && trim(lines[i]).empty()) ++i;
    char sep = 0;
    if (i < lines.size() && lines[i].size() >= 5 && lowerAscii(std::string(lines[i].substr(0, 4))) == "sep=") {
        sep = lines[i][4];
        ++i;
    }
    if (i >= lines.size()) return fail("fichier vide");
    const std::string_view head = lines[i++];
    if (!sep) {
        // Le separateur : celui que la ligne des titres emploie le plus (';' a egalite).
        const auto count = [head](char c) { return std::count(head.begin(), head.end(), c); };
        sep = ';';
        if (count('\t') > count(sep)) sep = '\t';
        if (count(',') > count(sep)) sep = ',';
    }
    int cName = -1, cPath = -1, cType = -1, cValue = -1, cDate = -1, cDeclared = -1, cId = -1;
    const auto titles = csvSplit(head, sep, false);
    for (std::size_t c = 0; c < titles.size(); ++c) {
        const std::string k = titleKey(titles[c]);
        const int at = static_cast<int>(c);
        if (k == "variable" || k == "nom" || k == "name") cName = at;
        else if (k == "chemin" || k == "path" || k == "case") cPath = at;
        else if (k == "type") cType = at;
        else if (k == "valeur" || k == "value" || k == "valeurgardee" || k == "dernierevaleursauvegardee") cValue = at;
        else if (k == "date" || k == "datededernieresauvegarde" || k == "datedernieresauvegarde" || k == "datedeladernieresauvegarde") cDate = at;
        else if (k == "typedeclare" || k == "declare" || k == "declared") cDeclared = at;
        else if (k == "id" || k == "identifiant") cId = at;
    }
    if (cName < 0 && cId < 0) return fail("colonne Variable absente (titres attendus : Variable;Chemin;Type;Valeur;Date)");
    if (cValue < 0) return fail("colonne Valeur absente");
    if (cType < 0) return fail("colonne Type absente (BOOL, INT, DINT, REAL, TIME, STRING...)");
    for (std::size_t lineNo = i + 1; i < lines.size(); ++i, ++lineNo) {
        if (trim(lines[i]).empty()) continue;
        const auto f = csvSplit(lines[i], sep, false);
        const auto field = [&f](int c) { return c >= 0 && static_cast<std::size_t>(c) < f.size() ? trim(f[static_cast<std::size_t>(c)]) : std::string{}; };
        Entry e;
        e.cell.name = field(cName);
        e.cell.path = field(cPath);
        e.cell.declared = field(cDeclared);
        e.date = field(cDate);
        if (const std::string id = field(cId); !id.empty()) {
            unsigned long long n = 0;
            const auto res = std::from_chars(id.data(), id.data() + id.size(), n);
            if (res.ec == std::errc{} && res.ptr == id.data() + id.size()) e.cell.variable = static_cast<Id>(n);
        }
        if (e.cell.name.empty() && e.cell.variable == kNoId) return fail("ligne " + std::to_string(lineNo) + " : pas de variable");
        const std::string typeText = field(cType);
        const sim::Type type = sim::typeFromName(typeText);
        if (type == sim::Type::Unknown) return fail("ligne " + std::to_string(lineNo) + " : type inconnu \xC2\xAB " + typeText + " \xC2\xBB");
        // La valeur : un texte garde ses espaces ; le reste se lit comme Excel l'ecrit.
        std::string value = type == sim::Type::String && cValue >= 0 && static_cast<std::size_t>(cValue) < f.size() ? f[static_cast<std::size_t>(cValue)] : field(cValue);
        if (type == sim::Type::Real) {
            std::replace(value.begin(), value.end(), ',', '.');
            if (!value.empty() && value.front() == '+') value.erase(value.begin());
        } else if (type == sim::Type::Bool) {
            const std::string b = lowerAscii(value);
            if (b == "true" || b == "vrai" || b == "oui" || b == "1" || b == "x") value = "TRUE";
            else if (b == "false" || b == "faux" || b == "non" || b == "0" || b.empty()) value = "FALSE";
        } else if (type == sim::Type::String) {
            if (value.size() >= 2 && value[0] == '\'' && (value[1] == '=' || value[1] == '+' || value[1] == '-' || value[1] == '@')) value.erase(value.begin());
        }
        auto v = simdata::valueFrom(type, value);
        if (!v) return fail("ligne " + std::to_string(lineNo) + " : valeur illisible \xC2\xAB " + value + " \xC2\xBB pour un " + std::string(sim::toString(type)));
        e.cell.value = std::move(*v);
        out.entries.push_back(std::move(e));
    }
    return true;
}

bool parseAny(std::string_view text, Store& out, std::string* why) {
    const std::string_view t = withoutBom(text);
    for (const auto line : linesOf(t)) {
        if (trim(line).empty()) continue;
        return line == kHeader ? parse(t, out, why) : fromCsv(t, out, why);
    }
    out = Store{};
    if (why) *why = "fichier vide";
    return false;
}

std::string ImportReport::summary() const {
    std::string out = plural(values, "valeur import\xC3\xA9" "e", "valeurs import\xC3\xA9" "es") + " (" + plural(variables, "variable", "variables") + ")";
    if (unknown) out += ", " + plural(unknown, "variable inconnue \xC3\xA9" "cart\xC3\xA9" "e", "variables inconnues \xC3\xA9" "cart\xC3\xA9" "es");
    if (notRetained)
        out += ", " + plural(notRetained, "variable non r\xC3\xA9manente (ou li\xC3\xA9" "e) \xC3\xA9" "cart\xC3\xA9" "e",
                             "variables non r\xC3\xA9manentes (ou li\xC3\xA9" "es) \xC3\xA9" "cart\xC3\xA9" "es");
    if (incompatible)
        out += ", " + plural(incompatible, "type incompatible : la valeur initiale au lancement", "types incompatibles : les valeurs initiales au lancement");
    return out;
}

ImportReport importInto(Store& store, const Project& p, const Store& incoming, const std::string& now) {
    ImportReport r;
    std::map<Id, const Variable*> byId;
    for (const auto& v : p.programs.variables) byId[v.id] = &v;
    std::map<Id, std::vector<Entry>> taken;
    std::vector<Id> order;
    std::set<std::string> unknown, notRetained;
    for (const auto& e : incoming.entries) {
        const Variable* v = nullptr;
        if (const auto it = byId.find(e.cell.variable);
            it != byId.end() && (e.cell.name.empty() || lowerAscii(it->second->name) == lowerAscii(e.cell.name)))
            v = it->second;
        else if (!e.cell.name.empty())
            v = p.variable(e.cell.name);
        if (!v) {
            unknown.insert(e.cell.name.empty() ? "#" + std::to_string(e.cell.variable) : lowerAscii(e.cell.name));
            continue;
        }
        if (!v->retain || v->bound()) {
            notRetained.insert(lowerAscii(v->name));
            continue;
        }
        Entry x = e;
        x.cell.variable = v->id;
        x.cell.name = v->name;
        if (x.cell.declared.empty()) x.cell.declared = v->type;   // un CSV sans le type declare : celui d'aujourd'hui
        if (x.date.empty()) x.date = now;
        auto& list = taken[v->id];
        if (list.empty()) order.push_back(v->id);
        // Une meme case deux fois : la derniere l'emporte.
        std::erase_if(list, [&x](const Entry& o) { return o.cell.path == x.cell.path; });
        list.push_back(std::move(x));
    }
    for (const Id id : order) {
        auto& list = taken[id];
        (void)reset(store, id);
        r.values += list.size();
        ++r.variables;
        for (auto& e : list) store.entries.push_back(std::move(e));
    }
    if (!order.empty()) store.date = now;
    for (const Id id : order)
        if (stateOf(p, *byId[id], &store).problem) ++r.incompatible;
    r.unknown = unknown.size();
    r.notRetained = notRetained.size();
    return r;
}

std::size_t reset(Store& store, Id variable) {
    const auto before = store.entries.size();
    std::erase_if(store.entries, [variable](const Entry& e) { return variable == kNoId || e.cell.variable == variable; });
    return before - store.entries.size();
}

std::string checkIntegrity(const fs::path& file, const Project& p) {
    std::error_code ec;
    if (file.empty()) return "Le projet n'est pas encore enregistr\xC3\xA9 : pas de stockage.";
    fs::path bak = file;
    bak += ".bak";
    const bool exists = fs::exists(file, ec);
    std::string text, why;
    Store s;
    if (!exists && !fs::exists(bak, ec)) return "Aucun fichier : rien n'a encore \xC3\xA9t\xC3\xA9 gard\xC3\xA9 sur ce poste (" + file.string() + ").";
    if (!exists || !core::readFileAll(file, text) || !parse(text, s, &why)) {
        Store b;
        std::string bw, btext;
        const bool backup = core::readFileAll(bak, btext) && parse(btext, b, &bw);
        return std::string(exists ? "Le fichier est ab\xC3\xAEm\xC3\xA9 (" + (why.empty() ? std::string("illisible") : why) + ")" : std::string("Le fichier manque"))
             + (backup ? " ; sa copie de secours est lisible (" + plural(b.entries.size(), "valeur", "valeurs") + ", \xC3\xA9" "crite le " + b.date
                             + ") : le poste la reprendra."
                       : " ; pas de copie de secours lisible : le poste reprendra les valeurs initiales.");
    }
    std::set<Id> vars, gone, unflagged;
    std::map<Id, const Variable*> byId;
    for (const auto& v : p.programs.variables) byId[v.id] = &v;
    for (const auto& e : s.entries) {
        vars.insert(e.cell.variable);
        const auto it = byId.find(e.cell.variable);
        if (it == byId.end()) gone.insert(e.cell.variable);
        else if (!it->second->retain || it->second->bound()) unflagged.insert(e.cell.variable);
    }
    std::size_t incompatible = 0;
    for (const auto& v : p.programs.variables)
        if (v.retain && !v.bound() && vars.count(v.id) && stateOf(p, v, &s).problem) ++incompatible;
    std::string out = "Fichier lisible et complet : " + plural(s.entries.size(), "valeur", "valeurs") + " de " + plural(vars.size(), "variable", "variables")
                    + (s.date.empty() ? std::string{} : ", \xC3\xA9" "crit le " + s.date);
    if (!gone.empty()) out += " ; " + plural(gone.size(), "variable qui n'existe plus (ignor\xC3\xA9" "e)", "variables qui n'existent plus (ignor\xC3\xA9" "es)");
    if (!unflagged.empty()) out += " ; " + plural(unflagged.size(), "variable non r\xC3\xA9manente (ignor\xC3\xA9" "e)", "variables non r\xC3\xA9manentes (ignor\xC3\xA9" "es)");
    if (incompatible) out += " ; " + plural(incompatible, "type incompatible (valeur initiale)", "types incompatibles (valeurs initiales)");
    return out + ".";
}

} // namespace hmi::retain
