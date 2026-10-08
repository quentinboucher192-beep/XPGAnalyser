// =============================================================================
//  hmi/HmiRetain.cpp - 1.11.16 : la remanence d'exploitation (voir le .hpp)
// =============================================================================
#include "HmiRetain.hpp"

#include "HmiEnums.hpp"
#include "HmiStore.hpp"
#include "HmiTypes.hpp"
#include "../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <set>

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
