// =============================================================================
//  hmi/HmiEditHistory.cpp - 1.12.25 : l'historique des modifications des objets
//  (voir HmiEditHistory.hpp).
// =============================================================================
#include "HmiEditHistory.hpp"

#include "HmiNetInfo.hpp"
#include "HmiRoles.hpp"
#include "HmiStore.hpp"
#include "core/Version.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace hmi::edits {
namespace fs = std::filesystem;

namespace {

constexpr const char* kDir = "historique-edition";
constexpr int kFormat = 1;

// ---- les champs d'une ligne (le format des vues) ----------------------------------
std::string field(std::string_view key, std::string_view value) { return " " + std::string(key) + "=" + quote(value); }
std::string fieldNum(std::string_view key, long long v) { return " " + std::string(key) + "=" + std::to_string(v); }
std::string get(const Record& r, std::string_view key) {
    const std::string* v = r.get(key);
    return v ? *v : std::string{};
}
long long getNum(const Record& r, std::string_view key, long long fallback = 0) {
    const std::string* v = r.get(key);
    if (!v || v->empty()) return fallback;
    char* end = nullptr;
    const long long n = std::strtoll(v->c_str(), &end, 10);
    return end && *end == '\0' ? n : fallback;
}

std::string_view sortKey(Sort s) noexcept {
    switch (s) {
        case Sort::Object:     return "objet";
        case Sort::Geometry:   return "geometrie";
        case Sort::Appearance: return "apparence";
        case Sort::Bindings:   return "liaisons";
        case Sort::Actions:    return "actions";
    }
    return "apparence";
}
Sort sortFromKey(std::string_view k) noexcept {
    if (k == "objet") return Sort::Object;
    if (k == "geometrie") return Sort::Geometry;
    if (k == "liaisons") return Sort::Bindings;
    if (k == "actions") return Sort::Actions;
    return Sort::Appearance;
}
std::string_view whatKey(What w) noexcept {
    switch (w) {
        case What::Created:  return "cree";
        case What::Modified: return "modifie";
        case What::Deleted:  return "supprime";
        case What::Merged:   return "regroupe";
    }
    return "modifie";
}
What whatFromKey(std::string_view k) noexcept {
    if (k == "cree") return What::Created;
    if (k == "supprime") return What::Deleted;
    if (k == "regroupe") return What::Merged;
    return What::Modified;
}

// ---- un objet seul dans une vue : ses lignes du format des vues ----------------------------
View miniView(const Object& o) {
    View v;
    v.id = 1;
    v.name = "historique";
    if (o.layer != kNoId) {   // son calque : sans lui, la relecture le mettrait ailleurs
        Layer l;
        l.id = o.layer;
        l.name = "calque";
        v.layers.push_back(l);
        v.activeLayer = o.layer;
    }
    v.objects.push_back(o);
    return v;
}

// Ses blocs : les fonctions redefinies (la ligne "redefinition" et ses declarations), les alarmes
// surchargees, les actions - chacun sous sa cle ("fonction Ouvrir", "alarme Defaut", "action 2").
struct Block {
    std::string key;
    Sort        sort{Sort::Actions};
    std::string text;   // ses lignes, sans le dernier saut de ligne
};
std::vector<Block> blocksOf(const Object& o) {
    std::vector<Block> out;
    if (o.actions.empty() && o.alarmOverrides.empty() && o.functionOverrides.empty()) return out;
    Object c = o;
    c.props.clear();
    const std::string text = serializeView(miniView(c));
    std::size_t pos = 0;
    int action = 0;
    Block* open = nullptr;   // une redefinition, que ses declarations suivent
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        Record r;
        std::string why;
        if (line.empty() || line[0] == '#' || !parseRecord(line, r, why)) continue;
        if (r.word == "redefinition") {
            out.push_back({"fonction " + get(r, "fonction"), Sort::Actions, line});
            open = &out.back();
            continue;
        }
        if (open && r.word.rfind("declaration", 0) == 0) {
            open->text += "\n" + line;
            continue;
        }
        open = nullptr;
        if (r.word == "surcharge_alarme") {
            std::string key = "alarme " + get(r, "alarme");
            if (const std::string path = get(r, "chemin"); !path.empty()) key += " (" + path + ")";
            out.push_back({key, Sort::Bindings, line});
        } else if (r.word == "action") {
            out.push_back({"action " + std::to_string(++action), Sort::Actions, line});
        }
    }
    return out;
}
bool isBlockKey(std::string_view key) noexcept { return key.find(' ') != std::string_view::npos; }

// Remet les blocs (fonctions, alarmes, actions) d'un objet a partir de leurs lignes : le fichier
// d'une vue a lui seul, relu par le lecteur des vues.
bool rebuildBlocks(Object& o, const std::vector<Block>& blocks) {
    Object c = o;
    c.props.clear();
    c.actions.clear();
    c.alarmOverrides.clear();
    c.functionOverrides.clear();
    std::string text = serializeView(miniView(c));
    const std::string end = "fin\n";
    if (text.size() < end.size() || text.compare(text.size() - end.size(), end.size(), end) != 0) return false;
    text.resize(text.size() - end.size());
    // l'ordre du fichier : les fonctions et les alarmes avec l'objet, les actions apres tous les objets
    for (const auto& b : blocks) if (b.key.rfind("action ", 0) != 0) text += b.text + "\n";
    for (const auto& b : blocks) if (b.key.rfind("action ", 0) == 0) text += b.text + "\n";
    text += end;
    auto v = parseView(text);
    if (!v || v->objects.empty()) return false;
    const Object& back = v->objects.front();
    o.actions = back.actions;
    o.alarmOverrides = back.alarmOverrides;
    o.functionOverrides = back.functionOverrides;
    return true;
}

std::string joinDistinct(const std::vector<std::string>& parts) {
    std::vector<std::string> seen;
    for (const auto& p : parts)
        if (!p.empty() && std::find(seen.begin(), seen.end(), p) == seen.end()) seen.push_back(p);
    std::string out;
    for (const auto& s : seen) out += (out.empty() ? "" : ", ") + s;
    return out;
}

core::Status writeText(const fs::path& target, const std::string& text) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + tmp.string());
        out << text;
        if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + tmp.string());
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return core::fail(core::ErrorCode::FileUnreadable, target.string() + " : " + ec.message());
    }
    return core::ok();
}

bool readText(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Le fichier d'une vue dans le dossier d'un projet : ihm/vues/0003-Nom.vue.
std::optional<fs::path> viewFile(const std::string& projectFolder, Id view) {
    if (projectFolder.empty()) return std::nullopt;
    char prefix[16];
    std::snprintf(prefix, sizeof prefix, "%04u-", static_cast<unsigned>(view));
    const fs::path dir = fs::path(projectFolder) / "ihm" / "vues";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return std::nullopt;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && e.path().extension() == ".vue") return e.path();
    }
    return std::nullopt;
}

} // namespace

// ================================================================== la signature ===
Signature currentSignature() {
    Signature s;
    s.t = static_cast<std::int64_t>(std::time(nullptr));
    s.pc = netinfo::computerName();
    for (const char* var : {"USERNAME", "USER", "LOGNAME"})
        if (const char* v = std::getenv(var); v && *v) { s.account = v; break; }
    s.version = XPG_ANALYZER_VERSION;
    return s;
}

// ================================================================== comparer ===
Sort sortOf(Kind kind, std::string_view key, bool expression) {
    if (key == "x" || key == "y" || key == "w" || key == "h" || key == "rot" || key == "flipH" || key == "flipV"
        || key == "pivotX" || key == "pivotY")
        return Sort::Geometry;
    if (expression || key == "variable" || roles::readsAsFormula(kind, key)) return Sort::Bindings;
    return Sort::Appearance;
}

std::vector<Change> diff(const Object& a, const Object& b) {
    std::vector<Change> out;
    const auto simple = [&](std::string key, std::string before, std::string after) {
        if (before == after) return;
        Change c;
        c.key = std::move(key);
        c.sort = Sort::Object;
        c.before = std::move(before);
        c.after = std::move(after);
        out.push_back(std::move(c));
    };
    simple("nom", a.name, b.name);
    simple("calque", std::to_string(a.layer), std::to_string(b.layer));
    simple("groupe", std::to_string(a.parent), std::to_string(b.parent));
    simple("verrou", a.locked ? "oui" : "non", b.locked ? "oui" : "non");
    simple("cache", a.hidden ? "oui" : "non", b.hidden ? "oui" : "non");
    // les proprietes : dans l'ordre d'apres, puis celles qui ont disparu
    const auto prop = [&](const Prop* p, const Prop* q, std::string_view key) {
        if (p && q && p->value == q->value && p->expr == q->expr) return;
        if (!p && !q) return;
        Change c;
        c.key = std::string(key);
        c.sort = sortOf(b.kind, key, (p && !p->expr.empty()) || (q && !q->expr.empty()));
        c.beforeAbsent = !p;
        c.afterAbsent = !q;
        if (p) { c.before = p->value; c.beforeExpr = p->expr; }
        if (q) { c.after = q->value; c.afterExpr = q->expr; }
        out.push_back(std::move(c));
    };
    for (const auto& q : b.props) prop(a.find(q.key), &q, q.key);
    for (const auto& p : a.props) if (!b.find(p.key)) prop(&p, nullptr, p.key);
    // les fonctions redefinies, les alarmes surchargees, les actions : leurs lignes
    const auto ba = blocksOf(a), bb = blocksOf(b);
    const auto find = [](const std::vector<Block>& v, const std::string& key) -> const Block* {
        for (const auto& x : v) if (x.key == key) return &x;
        return nullptr;
    };
    const auto block = [&](const Block* p, const Block* q) {
        if (p && q && p->text == q->text) return;
        Change c;
        c.key = q ? q->key : p->key;
        c.sort = q ? q->sort : p->sort;
        c.beforeAbsent = !p;
        c.afterAbsent = !q;
        if (p) c.before = p->text;
        if (q) c.after = q->text;
        out.push_back(std::move(c));
    };
    for (const auto& q : bb) block(find(ba, q.key), &q);
    for (const auto& p : ba) if (!find(bb, p.key)) block(&p, nullptr);
    return out;
}

std::vector<Entry> compare(const View* before, const View& after, const Signature& sig, const Notes& notes) {
    std::vector<Entry> out;
    std::map<Id, const Object*> old;
    if (before) for (const auto& o : before->objects) old[o.id] = &o;
    const auto make = [&](const Object& o, What what) {
        Entry e;
        e.t = sig.t;
        e.pc = sig.pc;
        e.account = sig.account;
        e.version = sig.version;
        e.object = o.id;
        e.name = o.name;
        e.kind = std::string(kindKey(o.kind));
        e.what = what;
        if (const auto n = notes.find(o.id); n != notes.end()) e.note = n->second;
        return e;
    };
    for (const auto& o : after.objects) {
        const auto it = old.find(o.id);
        if (it == old.end()) {
            out.push_back(make(o, What::Created));
            continue;
        }
        auto changes = diff(*it->second, o);
        old.erase(it);
        if (changes.empty()) continue;
        Entry e = make(o, What::Modified);
        e.changes = std::move(changes);
        out.push_back(std::move(e));
    }
    if (before)
        for (const auto& o : before->objects) {   // supprimes : dans l'ordre de la vue d'avant
            if (!old.count(o.id)) continue;
            Entry e = make(o, What::Deleted);
            e.note.clear();
            e.snapshot = snapshotOf(o);
            out.push_back(std::move(e));
        }
    return out;
}

// ================================================================== revenir ===
bool apply(Object& o, const Change& c, bool toBefore) {
    const std::string& value = toBefore ? c.before : c.after;
    const std::string& expr = toBefore ? c.beforeExpr : c.afterExpr;
    const bool absent = toBefore ? c.beforeAbsent : c.afterAbsent;
    if (c.sort == Sort::Object && !isBlockKey(c.key)) {
        if (c.key == "nom") { o.name = value; return true; }
        if (c.key == "verrou") { o.locked = value == "oui"; return true; }
        if (c.key == "cache") { o.hidden = value == "oui"; return true; }
        if (c.key == "calque" || c.key == "groupe") {
            char* end = nullptr;
            const unsigned long id = std::strtoul(value.c_str(), &end, 10);
            if (!end || *end) return false;
            (c.key == "calque" ? o.layer : o.parent) = static_cast<Id>(id);
            return true;
        }
        return false;
    }
    if (isBlockKey(c.key)) {
        auto blocks = blocksOf(o);
        auto it = std::find_if(blocks.begin(), blocks.end(), [&](const Block& b) { return b.key == c.key; });
        if (absent) {
            if (it == blocks.end()) return true;
            blocks.erase(it);
        } else if (it != blocks.end()) {
            it->text = value;
        } else {
            Block b{c.key, c.sort, value};
            if (c.key.rfind("action ", 0) == 0) {
                // a son rang parmi les actions : "action 2" avant l'actuelle deuxieme
                const int rank = std::atoi(c.key.c_str() + 7);
                int seen = 0;
                auto at = blocks.end();
                for (auto j = blocks.begin(); j != blocks.end(); ++j)
                    if (j->key.rfind("action ", 0) == 0 && ++seen == rank) { at = j; break; }
                blocks.insert(at, std::move(b));
            } else {
                blocks.push_back(std::move(b));
            }
        }
        return rebuildBlocks(o, blocks);
    }
    // une propriete
    Prop* p = o.find(c.key);
    if (absent) {
        if (p) o.props.erase(o.props.begin() + (p - o.props.data()));
        return true;
    }
    if (!p) {
        o.props.push_back({c.key, value, expr});
        return true;
    }
    p->value = value;
    p->expr = expr;
    return true;
}

std::string snapshotOf(const Object& o) { return serializeView(miniView(o)); }

std::optional<Object> fromSnapshot(std::string_view text) {
    if (text.empty()) return std::nullopt;
    auto v = parseView(text);
    if (!v || v->objects.empty()) return std::nullopt;
    return v->objects.front();
}

// ================================================================== garder ===
void trim(Log& log, std::size_t perObject) {
    if (perObject < 2) perObject = 2;
    std::map<Id, std::vector<std::size_t>> byObject;
    for (std::size_t i = 0; i < log.entries.size(); ++i) byObject[log.entries[i].object].push_back(i);
    std::set<std::size_t> drop;
    for (auto& [id, idx] : byObject) {
        (void)id;
        if (idx.size() <= perObject) continue;
        const std::size_t first = log.entries[idx.front()].what == What::Created ? 1 : 0;
        // regrouper idx[first .. first + n] en une : il en reste perObject
        const std::size_t n = idx.size() - perObject;
        Entry merged = log.entries[idx[first]];
        std::vector<std::string> pcs, accounts;
        std::int64_t since = merged.what == What::Merged && merged.since ? merged.since : merged.t;
        int count = 0;
        std::vector<Change> changes;
        for (std::size_t k = first; k <= first + n; ++k) {
            const Entry& e = log.entries[idx[k]];
            for (const auto* src : {&e.pc, &e.account}) {   // une entree deja regroupee dit "PC-A, PC-B"
                auto& into = src == &e.pc ? pcs : accounts;
                std::size_t from = 0;
                while (from <= src->size()) {
                    const std::size_t comma = src->find(", ", from);
                    into.push_back(src->substr(from, comma == std::string::npos ? std::string::npos : comma - from));
                    if (comma == std::string::npos) break;
                    from = comma + 2;
                }
            }
            if (e.what == What::Merged && e.since) since = std::min(since, e.since);
            count += std::max(1, e.count);
            for (const auto& c : e.changes) {
                auto it = std::find_if(changes.begin(), changes.end(), [&](const Change& x) { return x.key == c.key; });
                if (it == changes.end()) {
                    changes.push_back(c);
                } else {
                    it->after = c.after;
                    it->afterExpr = c.afterExpr;
                    it->afterAbsent = c.afterAbsent;
                }
            }
            if (k > first) drop.insert(idx[k]);
        }
        const Entry& last = log.entries[idx[first + n]];
        merged.t = last.t;
        merged.since = since;
        merged.count = count;
        merged.what = What::Merged;
        merged.pc = joinDistinct(pcs);
        merged.account = joinDistinct(accounts);
        merged.version = last.version;
        merged.name = last.name;
        merged.kind = last.kind;
        merged.note.clear();
        merged.snapshot.clear();
        changes.erase(std::remove_if(changes.begin(), changes.end(),
                                     [](const Change& c) {
                                         return c.before == c.after && c.beforeExpr == c.afterExpr && c.beforeAbsent == c.afterAbsent;
                                     }),
                      changes.end());
        merged.changes = std::move(changes);
        log.entries[idx[first]] = std::move(merged);
    }
    if (drop.empty()) return;
    std::vector<Entry> kept;
    kept.reserve(log.entries.size() - drop.size());
    for (std::size_t i = 0; i < log.entries.size(); ++i)
        if (!drop.count(i)) kept.push_back(std::move(log.entries[i]));
    log.entries = std::move(kept);
}

// ================================================================== le fichier ===
std::string fileOf(Id view) {
    char b[48];
    std::snprintf(b, sizeof b, "%s/%04u.txt", kDir, static_cast<unsigned>(view));
    return b;
}

std::string serialize(const Log& log) {
    std::string s = "# IHM Studio - historique des modifications des objets d'une vue (format " + std::to_string(kFormat) + ")\n";
    s += "historique" + fieldNum("vue", log.view) + "\n";
    for (const auto& e : log.entries) {
        s += "entree" + fieldNum("t", e.t) + field("pc", e.pc) + field("compte", e.account) + field("version", e.version)
           + fieldNum("objet", e.object) + field("nom", e.name) + " type=" + (e.kind.empty() ? std::string("-") : e.kind)
           + " quoi=" + std::string(whatKey(e.what));
        if (!e.note.empty()) s += field("note", e.note);
        if (e.what == What::Merged) s += fieldNum("depuis", e.since) + fieldNum("nombre", e.count);
        s += "\n";
        for (const auto& c : e.changes) {
            s += "chg" + field("cle", c.key) + " sorte=" + std::string(sortKey(c.sort));
            if (!c.beforeAbsent) s += field("avant", c.before);
            if (!c.beforeExpr.empty()) s += field("avant_expr", c.beforeExpr);
            if (!c.afterAbsent) s += field("apres", c.after);
            if (!c.afterExpr.empty()) s += field("apres_expr", c.afterExpr);
            s += "\n";
        }
        if (!e.snapshot.empty()) s += "image" + field("texte", e.snapshot) + "\n";
    }
    s += "fin\n";
    return s;
}

Log parse(std::string_view text, Id view) {
    Log log;
    log.view = view;
    std::size_t pos = 0;
    Entry* cur = nullptr;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        Record r;
        std::string why;
        if (!parseRecord(line, r, why)) continue;   // une ligne abimee : sautee, le reste se lit
        if (r.word == "historique") {
            if (const long long v = getNum(r, "vue", -1); v >= 0) log.view = static_cast<Id>(v);
        } else if (r.word == "entree") {
            Entry e;
            e.t = getNum(r, "t");
            e.pc = get(r, "pc");
            e.account = get(r, "compte");
            e.version = get(r, "version");
            e.object = static_cast<Id>(getNum(r, "objet"));
            e.name = get(r, "nom");
            e.kind = get(r, "type");
            if (e.kind == "-") e.kind.clear();
            e.what = whatFromKey(get(r, "quoi"));
            e.note = get(r, "note");
            e.since = getNum(r, "depuis");
            e.count = static_cast<int>(std::max(1LL, getNum(r, "nombre", 1)));
            log.entries.push_back(std::move(e));
            cur = &log.entries.back();
        } else if (r.word == "chg" && cur) {
            Change c;
            c.key = get(r, "cle");
            c.sort = sortFromKey(get(r, "sorte"));
            c.beforeAbsent = !r.get("avant");
            c.afterAbsent = !r.get("apres");
            c.before = get(r, "avant");
            c.after = get(r, "apres");
            c.beforeExpr = get(r, "avant_expr");
            c.afterExpr = get(r, "apres_expr");
            if (!c.key.empty()) cur->changes.push_back(std::move(c));
        } else if (r.word == "image" && cur) {
            cur->snapshot = get(r, "texte");
        } else if (r.word == "fin") {
            break;
        }
    }
    return log;
}

Log load(const std::string& projectFolder, Id view) {
    std::string text;
    if (projectFolder.empty() || !readText(fs::path(projectFolder) / "ihm" / fileOf(view), text)) {
        Log log;
        log.view = view;
        return log;
    }
    return parse(text, view);
}

std::optional<View> savedView(const std::string& projectFolder, Id view) {
    const auto file = viewFile(projectFolder, view);
    std::string text;
    if (!file || !readText(*file, text)) return std::nullopt;
    auto v = parseView(text);
    if (!v) return std::nullopt;
    return std::move(*v);
}

core::Result<int> record(const Project& now, const std::string& baseFolder, const std::string& targetFolder,
                         const Signature& sig, const Notes& notes) {
    if (targetFolder.empty()) return 0;
    std::error_code ec;
    const fs::path target = fs::path(targetFolder) / "ihm";
    const std::string base = baseFolder.empty() ? targetFolder : baseFolder;
    // Enregistrer sous : l'historique suit le projet dans son nouveau dossier.
    if (base != targetFolder) {
        const fs::path from = fs::path(base) / "ihm" / kDir;
        if (fs::is_directory(from, ec)) {
            fs::create_directories(target / kDir, ec);
            for (const auto& e : fs::directory_iterator(from, ec)) {
                if (!e.is_regular_file(ec)) continue;
                const fs::path to = target / kDir / e.path().filename();
                if (!fs::exists(to, ec)) fs::copy_file(e.path(), to, ec);
            }
        }
    }
    int written = 0;
    for (const auto& v : now.views) {
        std::optional<View> old;
        if (const auto file = viewFile(base, v.id)) {
            std::string text;
            if (readText(*file, text)) {
                if (text == serializeView(v)) continue;   // rien n'a change dans cette vue
                auto parsed = parseView(text);
                if (!parsed) continue;                    // un fichier illisible : rien ne se compare
                old = std::move(*parsed);
            }
        }
        auto entries = compare(old ? &*old : nullptr, v, sig, notes);
        if (entries.empty()) continue;
        Log log = load(targetFolder, v.id);
        log.view = v.id;
        written += static_cast<int>(entries.size());
        for (auto& e : entries) log.entries.push_back(std::move(e));
        trim(log);
        if (auto st = writeText(target / fileOf(v.id), serialize(log)); !st) return core::Err<core::Error>(st.error());
    }
    return written;
}

// ================================================================== pour l'interface ===
std::string_view sortLabel(Sort s) noexcept {
    switch (s) {
        case Sort::Object:     return "Objet";
        case Sort::Geometry:   return "Position et taille";
        case Sort::Appearance: return "Apparence";
        case Sort::Bindings:   return "Liaisons et expressions";
        case Sort::Actions:    return "Actions";
    }
    return "Apparence";
}

std::string_view whatLabel(What w) noexcept {
    switch (w) {
        case What::Created:  return "Cr\xC3\xA9" "ation";
        case What::Modified: return "Modification";
        case What::Deleted:  return "Suppression";
        case What::Merged:   return "Regroup\xC3\xA9" "es";
    }
    return "Modification";
}

std::vector<Sort> sortsOf(const Entry& e) {
    std::vector<Sort> out;
    for (const Sort s : {Sort::Object, Sort::Geometry, Sort::Appearance, Sort::Bindings, Sort::Actions})
        for (const auto& c : e.changes)
            if (c.sort == s) { out.push_back(s); break; }
    return out;
}

bool renamed(const Entry& e) noexcept {
    for (const auto& c : e.changes) if (c.key == "nom") return true;
    return false;
}

std::string shown(const Change& c, bool before) {
    if (before ? c.beforeAbsent : c.afterAbsent) return {};
    const std::string& expr = before ? c.beforeExpr : c.afterExpr;
    if (!expr.empty()) return "=" + expr;
    if (isBlockKey(c.key)) return blockSummary(c, before);
    return before ? c.before : c.after;
}

std::string blockSummary(const Change& c, bool before) {
    const std::string& text = before ? c.before : c.after;
    if ((before ? c.beforeAbsent : c.afterAbsent) || text.empty()) return {};
    const std::string first = text.substr(0, text.find('\n'));
    Record r;
    std::string why;
    if (!parseRecord(first, r, why)) return first;
    const std::string dot = " \xC2\xB7 ";
    if (r.word == "action") {
        std::string s = get(r, "declencheur");
        if (const std::string op = get(r, "operation"); !op.empty()) s += dot + op;
        if (const std::string t = get(r, "cible"); !t.empty()) s += " " + t;
        if (const std::string v = get(r, "valeur"); !v.empty()) s += " = " + v;
        return s;
    }
    if (r.word == "surcharge_alarme") {
        std::string s;
        for (const auto& [k, v] : r.fields) {
            if (k == "alarme" || k == "chemin") continue;
            s += (s.empty() ? "" : dot) + k + " " + v;
        }
        return s.empty() ? std::string("surcharg\xC3\xA9" "e") : s;
    }
    if (r.word == "redefinition") {
        std::string body = get(r, "corps");
        for (char& ch : body) if (ch == '\n' || ch == '\t') ch = ' ';
        if (body.size() > 80) body = body.substr(0, 77) + "...";
        return body.empty() ? std::string("red\xC3\xA9" "finie") : body;
    }
    return first;
}

const Entry* lastOf(const Log& log, Id object) noexcept {
    for (auto it = log.entries.rbegin(); it != log.entries.rend(); ++it)
        if (it->object == object) return &*it;
    return nullptr;
}

const Entry* createdOf(const Log& log, Id object) noexcept {
    for (const auto& e : log.entries)
        if (e.object == object) return e.what == What::Created ? &e : nullptr;
    return nullptr;
}

int countOf(const Log& log, Id object) noexcept {
    int n = 0;
    for (const auto& e : log.entries)
        if (e.object == object) n += std::max(1, e.count);
    return n;
}

std::string dateText(std::int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return b;
}

} // namespace hmi::edits
