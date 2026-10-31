// =============================================================================
//  hmi/HmiAudit.cpp - 1.19.0 : la verification globale du projet, l'analyse et
//                     l'optimisation des adresses
// -----------------------------------------------------------------------------
//  Rien n'est recalcule a la main quand le logiciel sait le faire : Generer et
//  Compiler (HmiCheck), la carte memoire et ses chevauchements (HmiZones), la
//  place d'une adresse (HmiEquipment : canonicalAddress, placeEquipmentAddress),
//  le graphe des relations (HmiRelations). Ce fichier les rassemble, ajoute les
//  corrections proposees, et le plan des adresses.
// =============================================================================
#include "HmiAudit.hpp"

#include "HmiEquipment.hpp"   // placeEquipmentAddress, registerType
#include "HmiFolders.hpp"     // fold : les dossiers des listes
#include "HmiRelations.hpp"   // le graphe : les orphelins
#include "HmiTypes.hpp"       // types::validFolder, allFolders, isFormula
#include "HmiZones.hpp"       // zones::buildMap, conflictText, modicon

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace hmi::audit {

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool same(std::string_view a, std::string_view b) { return upper(a) == upper(b); }
std::string joined(const std::vector<std::string>& v, std::string_view sep = ", ") {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? std::string{} : std::string(sep)) + s;
    return out;
}
// Un motif avec * (sans casse) : "Pompe_*", "*Cuve*", "Pompe_1".
bool glob(std::string_view pattern, std::string_view text) {
    const std::string p = upper(trimmed(pattern)), t = upper(text);
    std::size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && p[pi] == t[ti]) {
            ++pi;
            ++ti;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ti;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

// "registre", "registres d'entree", "bobine", "entree TOR" (au pluriel si `many`).
std::string tableNoun(MemTable t, bool many) {
    switch (t) {
        case MemTable::Coils:          return many ? "bobines" : "bobine";
        case MemTable::DiscreteInputs: return many ? "entr\xC3\xA9" "es TOR" : "entr\xC3\xA9" "e TOR";
        case MemTable::InputRegisters: return many ? "registres d'entr\xC3\xA9" "e" : "registre d'entr\xC3\xA9" "e";
        case MemTable::Holding:        break;
    }
    return many ? "registres" : "registre";
}
std::string spanText(MemTable t, long first, long last) {
    if (last <= first) return tableNoun(t, false) + " " + zones::modicon(t, static_cast<std::uint32_t>(first));
    return tableNoun(t, true) + " " + zones::modicon(t, static_cast<std::uint32_t>(first)) + " \xC3\xA0 "
         + zones::modicon(t, static_cast<std::uint32_t>(last));
}

// ---- les places ------------------------------------------------------------------
using Interval = std::pair<long, long>;   // [debut, fin], bornes comprises
constexpr long kLastCell = 65535;

std::vector<Interval> merged(std::vector<Interval> v) {
    std::sort(v.begin(), v.end());
    std::vector<Interval> out;
    for (const auto& r : v) {
        if (r.first > r.second) continue;
        if (!out.empty() && r.first <= out.back().second + 1) out.back().second = std::max(out.back().second, r.second);
        else out.push_back(r);
    }
    return out;
}
// Ce qu'une table permet entre `from` et `to` : tout, ou ses zones declarees.
std::vector<Interval> allowed(const Equipment& e, MemTable t, long from, long to) {
    if (!e.zones.declared) return {{from, to}};
    std::vector<Interval> out;
    for (const auto& r : e.zones.of(t)) {
        const long a = std::max<long>(from, r.first), b = std::min<long>(to, r.last);
        if (a <= b) out.push_back({a, b});
    }
    return merged(std::move(out));
}
// `allow` moins `used` (tous deux fusionnes).
std::vector<Interval> gapsOf(const std::vector<Interval>& allow, const std::vector<Interval>& used) {
    std::vector<Interval> out;
    for (const auto& [a, b] : allow) {
        long cur = a;
        for (const auto& [ua, ub] : used) {
            if (ub < cur) continue;
            if (ua > b) break;
            if (ua > cur) out.push_back({cur, std::min(b, ua - 1)});
            cur = std::max(cur, ub + 1);
            if (cur > b) break;
        }
        if (cur <= b) out.push_back({cur, b});
    }
    return out;
}

// Les cases d'une variable dans la carte (une structure : tous ses membres).
struct Cells {
    bool          found{false};
    bool          oneTable{true};
    bool          topBit{false};       // la variable elle-meme est un bit d'un mot (%MW10.3)
    MemTable      table{MemTable::Holding};
    long          first{0}, last{-1};
    std::vector<std::size_t> vars;     // les rangs dans la carte
};
Cells cellsOf(const zones::MemoryMap& m, std::string_view root) {
    Cells c;
    for (std::size_t i = 0; i < m.vars.size(); ++i) {
        const auto& v = m.vars[i];
        if (!same(v.root, root)) continue;
        if (!c.found) {
            c.found = true;
            c.table = v.table;
            c.first = v.first;
            c.last = v.last;
        } else {
            if (v.table != c.table) c.oneTable = false;
            c.first = std::min<long>(c.first, v.first);
            c.last = std::max<long>(c.last, v.last);
        }
        if (v.bit >= 0 && same(v.name, root)) c.topBit = true;
        c.vars.push_back(i);
    }
    return c;
}
// Les cases occupees d'une table, sauf celles des variables `except` (des racines en majuscules).
std::vector<Interval> usedCells(const zones::MemoryMap& m, MemTable t, const std::set<std::string>& except) {
    std::vector<Interval> out;
    for (const auto& v : m.vars)
        if (v.table == t && !except.count(upper(v.root))) out.push_back({static_cast<long>(v.first), static_cast<long>(v.last)});
    return merged(std::move(out));
}

// L'adresse `address` decalee de `delta` cases, ecrite comme elle : 40101 -> 40105, 400101, %MW100 -> %MW104, %MF20,
// 4x0101 -> 4x0105, HR100 -> HR104 (le bit garde : 40101.3) ; vide : illisible ou hors des numeros.
std::string shifted(std::string_view address, long delta) {
    const std::string t = trimmed(address);
    if (t.empty() || types::isFormula(t)) return {};
    std::size_t a = 0;
    std::string prefix;
    if (t.size() > 2 && std::isdigit(uc(t[0])) && (t[1] == 'x' || t[1] == 'X')) {
        prefix = t.substr(0, 2);
        a = 2;
    } else {
        while (a < t.size() && !std::isdigit(uc(t[a]))) ++a;
        prefix = t.substr(0, a);
    }
    std::size_t b = a;
    while (b < t.size() && std::isdigit(uc(t[b]))) ++b;
    if (b == a || b - a > 6) return {};
    const std::string digits = t.substr(a, b - a), suffix = t.substr(b);
    const auto padded = [](long n, std::size_t width) {
        std::string s = std::to_string(n);
        if (s.size() < width) s.insert(0, width - s.size(), '0');
        return s;
    };
    if (prefix.empty()) {                                   // Modicon : 40101, 400101 (a partir de 1)
        if (digits.size() != 5 && digits.size() != 6) return {};
        const long n = std::stol(digits.substr(1)) + delta;
        if (n < 1 || n > kLastCell + 1) return {};
        return digits.substr(0, 1) + padded(n, digits.size() == 6 || n > 9999 ? 5 : 4) + suffix;
    }
    const long n = std::stol(digits) + delta;
    if (n < 0 || n > kLastCell + 1) return {};
    return prefix + padded(n, digits.front() == '0' ? digits.size() : 1) + suffix;
}

// La place de `address` pour cette variable : (table, premiere case) ; faux : illisible.
bool placeOf(const Variable& v, std::string_view address, MemTable& table, long& offset) {
    comm::Point pt;
    if (!equip::placeEquipmentAddress(address, equip::registerType(v), pt)) return false;
    table = zones::tableOf(pt.area);
    offset = pt.offset;
    return true;
}

const Variable* variableNamed(const Project& p, std::string_view name) { return p.variable(trimmed(name)); }

// La place libre la plus proche de la variable dans la carte `m` de son equipement `e`.
std::string nearestIn(const Equipment& e, const zones::MemoryMap& m, const Variable& v, std::string* why) {
    const auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return std::string{};
    };
    const Cells c = cellsOf(m, v.name);
    if (!c.found) return fail(v.name + " n'a pas d'adresse lisible sur " + e.name);
    if (!c.oneTable) return fail(v.name + " a des cases dans deux tables : d\xC3\xA9placez-la membre par membre");
    if (!v.places.empty()) return fail(v.name + " a des adresses corrig\xC3\xA9" "es (membres plac\xC3\xA9s ailleurs) : d\xC3\xA9placez-la \xC3\xA0 la main");
    const long size = c.last - c.first + 1;
    const auto gaps = gapsOf(allowed(e, c.table, 0, kLastCell), usedCells(m, c.table, {upper(v.name)}));
    long best = -1, bestDist = 0;
    for (const auto& [ga, gb] : gaps) {
        if (gb - ga + 1 < size) continue;
        const long pos = std::clamp(c.first, ga, gb - size + 1);
        const long dist = pos > c.first ? pos - c.first : c.first - pos;
        if (best < 0 || dist < bestDist || (dist == bestDist && pos > best)) {
            best = pos;
            bestDist = dist;
        }
    }
    if (best < 0) return fail("aucune place libre de " + std::to_string(size) + " cases sur " + e.name);
    const std::string after = shifted(v.address, best - c.first);
    MemTable t{};
    long start = 0, now = 0;
    if (after.empty() || !placeOf(v, after, t, start) || !placeOf(v, v.address, t, now) || start - now != best - c.first)
        return fail("adresse de " + v.name + " impossible \xC3\xA0 r\xC3\xA9\xC3\xA9" "crire (" + v.address + ")");
    return after;
}

// ---- la conversion des constats de Generer et Compiler ----------------------------------
Severity severityOf(Issue::Severity s) {
    switch (s) {
        case Issue::Severity::Error:   return Severity::Error;
        case Issue::Severity::Warning: return Severity::Warning;
        case Issue::Severity::Info:    break;
    }
    return Severity::Info;
}
std::string placeOf(const Project& p, const Issue& i) {
    std::string w;
    if (const View* v = p.view(i.view)) {
        w = v->name;
        if (const Object* o = v->object(i.object)) w += " . " + o->name;
    }
    const Script* sc = i.script != kNoId ? p.script(i.script) : nullptr;
    if (sc) w += (w.empty() ? "script " : " . ") + sc->name;
    if (!i.property.empty() && !(sc && i.property == sc->name)) w += (w.empty() ? i.category + " " : " . ") + i.property;
    if (i.line > 0) w += ", ligne " + std::to_string(i.line);
    if (w.empty()) w = i.category;
    return w;
}
Finding findingOf(const Project& p, const Issue& i, const char* source) {
    Finding f;
    f.severity = severityOf(i.severity);
    f.category = i.category;
    f.message = i.message;
    f.where = placeOf(p, i);
    f.view = i.view;
    f.object = i.object;
    f.item = i.item;
    f.script = i.script;
    f.line = i.line;
    f.property = i.property;
    f.source = source;
    if (!i.fixFrom.empty())
        f.fixes.push_back({"texte", f.where, i.fixFrom, i.fixTo, std::string("la correction d'un clic propos\xC3\xA9" "e par ") + source});
    return f;
}

// Le texte que Generer ecrit pour un chevauchement d'adresses (checkEquipments) - pour ne pas le dire deux fois.
std::string generateConflictText(const Equipment& e, const zones::MemoryMap& m, const zones::MapConflict& c) {
    return e.name + " : " + zones::conflictText(m, c)
         + (c.severity == 3 ? " - la derni\xC3\xA8re \xC3\xA9" "crite efface l'autre" : " (voulu ? une consigne relue)");
}

// ---- les adresses en conflit ----------------------------------------------------------
struct EquipmentMap {
    const Equipment*  equipment{nullptr};
    zones::MemoryMap  map;
};

void addressFindings(const Project& p, const std::vector<EquipmentMap>& maps, std::vector<Finding>& out) {
    // L'ordre du projet : a emprise egale, c'est la variable declaree apres qui bouge.
    std::map<std::string, std::size_t> rank;
    for (std::size_t i = 0; i < p.programs.variables.size(); ++i) rank.emplace(upper(p.programs.variables[i].name), i);
    for (const auto& em : maps) {
        const Equipment& e = *em.equipment;
        const auto& m = em.map;
        if (!e.enabled) continue;
        std::map<std::pair<std::string, std::string>, std::size_t> done;   // une paire de variables : un constat
        for (const auto& c : m.conflicts) {
            const auto& a = m.vars[c.a];
            const auto& b = m.vars[c.b];
            const std::string ra = upper(a.root), rb = upper(b.root);
            const auto key = ra < rb ? std::make_pair(ra, rb) : std::make_pair(rb, ra);
            if (const auto it = done.find(key); it != done.end()) {
                Finding& f = out[it->second];
                if (c.severity == 3) f.severity = Severity::Error;
                else if (c.severity == 2 && f.severity == Severity::Info) f.severity = Severity::Warning;
                continue;
            }
            Finding f;
            f.severity = c.severity == 3 ? Severity::Error : c.severity == 2 ? Severity::Warning : Severity::Info;
            f.category = "Adresses";
            f.source = "Adresses";
            const std::string text = zones::conflictText(m, c);
            if (ra == rb) {
                f.message = e.name + " : deux cases de " + a.root + " se chevauchent (" + text
                          + ") - une adresse corrig\xC3\xA9" "e d'un membre en recouvre un autre";
                f.where = "variable " + a.root;
                f.property = a.root;
                if (const Variable* v = variableNamed(p, a.root)) f.item = v->id;
                done.emplace(key, out.size());
                out.push_back(std::move(f));
                continue;
            }
            f.message = e.name + " : " + text
                      + (c.severity == 3   ? " - la derni\xC3\xA8re \xC3\xA9" "crite efface l'autre"
                         : c.severity == 2 ? " (voulu ? une consigne relue)"
                                           : " - deux noms pour les m\xC3\xAAmes cases : voulu ? sinon une erreur de saisie");
            // Celle qui bouge : la plus petite emprise ; a egalite, la declaree apres.
            const Variable* va = variableNamed(p, a.root);
            const Variable* vb = variableNamed(p, b.root);
            const Cells ca = cellsOf(m, a.root), cb = cellsOf(m, b.root);
            const long sa = ca.last - ca.first + 1, sb = cb.last - cb.first + 1;
            const auto rk = [&](const std::string& r) { const auto it = rank.find(r); return it == rank.end() ? std::size_t{0} : it->second; };
            const bool moveA = sa < sb || (sa == sb && rk(ra) > rk(rb));
            const Variable* moved = moveA ? va : vb;
            const Variable* kept = moveA ? vb : va;
            f.where = "variables " + a.root + " et " + b.root;
            f.property = moved ? moved->name : b.root;
            if (moved) f.item = moved->id;
            if (moved) {
                std::string why;
                const std::string after = nearestIn(e, m, *moved, &why);
                const Cells cm = moveA ? ca : cb;
                if (!after.empty())
                    f.fixes.push_back({"adresse", "variable " + moved->name, moved->address, after,
                                       "la place libre la plus proche pour " + moved->name + " ("
                                           + std::to_string(cm.last - cm.first + 1) + (cm.table == MemTable::Coils || cm.table == MemTable::DiscreteInputs ? " bit" : " mot")
                                           + (cm.last > cm.first ? "s" : "") + ") : " + (kept ? kept->name + " garde la sienne, " : std::string{})
                                           + "plus aucun chevauchement"});
                else
                    f.message += " (aucune correction propos\xC3\xA9" "e : " + why + ")";
            }
            done.emplace(key, out.size());
            out.push_back(std::move(f));
        }
    }
}

// ---- les dossiers ------------------------------------------------------------------------
void folderFindings(const Project& p, std::vector<Finding>& out) {
    const auto add = [&](Severity s, std::string message, std::string where, std::vector<Fix> fixes = {}) {
        Finding f;
        f.severity = s;
        f.category = "Dossiers";
        f.source = "Dossiers";
        f.message = std::move(message);
        f.where = std::move(where);
        f.fixes = std::move(fixes);
        out.push_back(std::move(f));
    };
    // Des dossiers gardes pour une liste qui n'existe pas (le fichier, une version d'avant) : invisibles.
    for (const auto& [key, list] : p.listFolders) {
        if (key == "espaces" || fold::fromKey(key) || list.empty()) continue;   // espaces : les espaces de noms gardes (HmiNames)
        add(Severity::Warning,
            std::to_string(list.size()) + (list.size() > 1 ? " dossiers gard\xC3\xA9s" : " dossier gard\xC3\xA9") + " pour une liste qui n'existe pas (\xC2\xAB "
                + key + " \xC2\xBB) : " + joined(list) + " - aucun \xC3\xA9l\xC3\xA9ment ne peut y \xC3\xAAtre rang\xC3\xA9, ils ne se montrent nulle part",
            "dossiers de la liste " + key,
            {Fix{"dossier", "liste " + key, joined(list), "", "les retirer : ils ne rangent rien et ne se voient pas"}});
    }
    // Les noms illisibles, un meme dossier ecrit de deux facons (la casse ne compte pas : ils n'en font qu'un).
    for (int l = 0; l < static_cast<int>(fold::List::Count); ++l) {
        const auto list = static_cast<fold::List>(l);
        const std::string label(fold::label(list));
        const auto kept = p.listFolders.find(std::string(fold::key(list)));
        if (kept != p.listFolders.end())
            for (const auto& f : kept->second) {
                std::string why;
                if (!types::validFolder(f, &why)) add(Severity::Warning, "nom de dossier illisible \xC2\xAB " + f + " \xC2\xBB : " + why, label + " : " + f);
            }
        for (const auto& item : fold::items(p, list)) {
            std::string why;
            if (!trimmed(item.folder).empty() && !types::validFolder(item.folder, &why))
                add(Severity::Warning, item.name + " est rang\xC3\xA9 dans un dossier illisible \xC2\xAB " + item.folder + " \xC2\xBB : " + why,
                    label + " : " + item.name);
        }
        // Les ecritures telles qu'elles sont gardees (fold::allFolders n'en montre qu'une) : la liste et les elements.
        std::map<std::string, std::vector<std::string>> spellings;
        const auto spell = [&](const std::string& folder) {
            for (const auto& f : types::folderChain(folder)) {
                auto& s = spellings[upper(f)];
                if (std::find(s.begin(), s.end(), f) == s.end()) s.push_back(f);
            }
        };
        if (kept != p.listFolders.end())
            for (const auto& f : kept->second) spell(f);
        for (const auto& item : fold::items(p, list))
            if (!trimmed(item.folder).empty()) spell(item.folder);
        for (const auto& [k, s] : spellings)
            if (s.size() > 1)
                add(Severity::Warning, "le m\xC3\xAAme dossier \xC3\xA9" "crit de " + std::to_string(s.size()) + " fa\xC3\xA7ons : " + joined(s)
                                           + " (la casse ne compte pas : ses \xC3\xA9l\xC3\xA9ments sont m\xC3\xAAl\xC3\xA9s)",
                    label + " : " + s.front(), {Fix{"dossier", label + " : " + joined(s), joined(s), s.front(), "une seule \xC3\xA9" "criture"}});
    }
    {
        std::map<std::string, std::vector<std::string>> spellings;
        const auto spell = [&](const std::string& folder) {
            for (const auto& f : types::folderChain(folder)) {
                auto& s = spellings[upper(f)];
                if (std::find(s.begin(), s.end(), f) == s.end()) s.push_back(f);
            }
        };
        for (const auto& f : p.programs.folders) spell(f);
        for (const auto& v : p.programs.variables)
            if (!trimmed(v.folder).empty()) spell(v.folder);
        for (const auto& [k, s] : spellings)
            if (s.size() > 1)
                add(Severity::Warning, "le m\xC3\xAAme dossier de variables \xC3\xA9" "crit de " + std::to_string(s.size()) + " fa\xC3\xA7ons : " + joined(s),
                    "Variables IHM : " + s.front(), {Fix{"dossier", "Variables IHM : " + joined(s), joined(s), s.front(), "une seule \xC3\xA9" "criture"}});
    }
}

// ---- les orphelins (le graphe) -------------------------------------------------------------
void orphanFindings(const Project& p, std::vector<Finding>& out) {
    const auto g = rel::build(p);
    for (const auto i : rel::orphans(g)) {
        const auto& n = g.node(i);
        // Les symboles sans instance, les ressources inutilisees, les equipements sans variable : Generer les dit.
        if (n.kind != rel::Kind::Variable && n.kind != rel::Kind::Popup && n.kind != rel::Kind::Function && n.kind != rel::Kind::Script
            && n.kind != rel::Kind::Type)
            continue;
        // Une popup jamais ouverte : la Qualite de Generer la dit deja (case cochee).
        if (n.kind == rel::Kind::Popup
            && std::any_of(out.begin(), out.end(), [&](const Finding& f) { return f.view == n.id && f.message.find("jamais ouverte") != std::string::npos; }))
            continue;
        Finding f;
        f.severity = Severity::Info;
        f.category = "Orphelin";
        f.source = "Relations";
        f.message = std::string(rel::kindLabel(n.kind)) + " " + n.name + " : " + rel::orphanReason(g, i);
        f.where = rel::describe(g, i);
        f.property = n.name;
        if (n.kind == rel::Kind::Popup) f.view = n.id;
        else f.item = n.id;
        if (n.kind == rel::Kind::Script) f.script = n.id;
        f.fixes.push_back({"suppression", std::string(rel::kindLabel(n.kind)) + " " + n.name, n.name, "",
                           "rien ne l'emploie dans le projet : la retirer all\xC3\xA8ge le projet (si aucun programme ext\xC3\xA9rieur ne s'en sert)"});
        out.push_back(std::move(f));
    }
}

// L'emprise d'une table : premiere et derniere cases, cases occupees, trous.
Footprint footprintOf(const Equipment& e, const zones::MemoryMap& m, MemTable t) {
    Footprint f;
    f.equipment = e.name;
    f.table = t;
    std::vector<Interval> cells;
    for (const auto& v : m.vars)
        if (v.table == t) cells.push_back({static_cast<long>(v.first), static_cast<long>(v.last)});
    const auto used = merged(std::move(cells));
    if (used.empty()) {
        f.text = tableNoun(t, true) + " : aucun";
        return f;
    }
    f.first = used.front().first;
    f.last = used.back().second;
    for (const auto& [a, b] : used) f.used += b - a + 1;
    f.holes = (f.last - f.first + 1) - f.used;
    f.text = spanText(t, f.first, f.last) + " : " + std::to_string(f.used) + (f.used > 1 ? " occup\xC3\xA9" "es, " : " occup\xC3\xA9" "e, ")
           + std::to_string(f.holes) + (f.holes > 1 ? " libres" : " libre");
    return f;
}
std::size_t distinctConflicts(const zones::MemoryMap& m) {
    std::set<std::pair<std::string, std::string>> pairs;
    for (const auto& c : m.conflicts) {
        const std::string a = upper(m.vars[c.a].root), b = upper(m.vars[c.b].root);
        if (a != b) pairs.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    }
    return pairs.size();
}

} // namespace

// ------------------------------------------------------------------- le rapport ---
std::string_view severityLabel(Severity s) noexcept {
    switch (s) {
        case Severity::Error:   return "erreur bloquante";
        case Severity::Warning: return "avertissement";
        case Severity::Info:    break;
    }
    return "information";
}

std::string Report::summary() const {
    const auto part = [](std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); };
    return part(errors, "erreur bloquante", "erreurs bloquantes") + ", " + part(warnings, "avertissement", "avertissements") + ", "
         + part(infos, "information", "informations");
}

Report run(const Project& p, const NameExists& plcHasName, const std::string& projectFolder) {
    Report r;
    // Sans automate : aucun nom de l'automate (un nom inconnu de l'IHM est une erreur, pas << peut-etre de l'automate >>).
    const NameExists plc = plcHasName ? plcHasName : NameExists([](std::string_view) { return false; });
    // Les cartes memoire : les chevauchements, que Generer dit sans correction, sont redits ici avec la leur.
    std::vector<EquipmentMap> maps;
    std::set<std::string> redone;
    for (const auto& e : p.equipments) {
        if (!e.modbus() || e.name.empty()) continue;
        EquipmentMap em{&e, zones::buildMap(p, e)};
        if (e.enabled)
            for (const auto& c : em.map.conflicts)
                if (c.severity >= 2) redone.insert(generateConflictText(e, em.map, c));
        maps.push_back(std::move(em));
    }
    // Generer et Compiler, chaque constat une fois.
    GenerateOptions options;
    options.projectFolder = projectFolder;
    std::vector<Finding> found;
    std::set<std::string> seen;
    const auto take = [&](const std::vector<Issue>& issues, const char* source) {
        for (const auto& i : issues) {
            if (i.category == "Variable IHM" && redone.count(i.message)) continue;
            const std::string key = i.category + "|" + i.message + "|" + std::to_string(i.view) + "|" + std::to_string(i.object) + "|" + i.property
                                  + "|" + std::to_string(i.script) + "|" + std::to_string(i.line) + "|" + std::to_string(i.item);
            if (!seen.insert(key).second) continue;
            if (i.category == "Bilan") {                    // le bilan de Compiler : pas un constat
                r.notes.push_back(std::string(source) + " : " + i.message);
                continue;
            }
            found.push_back(findingOf(p, i, source));
        }
    };
    take(generateWith(p, plc, options), "Generer");
    take(compileWith(p, plc), "Compiler");
    addressFindings(p, maps, found);
    orphanFindings(p, found);
    folderFindings(p, found);
    // Les erreurs bloquantes, puis les avertissements, puis les informations.
    for (const Severity s : {Severity::Error, Severity::Warning, Severity::Info})
        for (auto& f : found)
            if (f.severity == s) r.findings.push_back(std::move(f));
    for (const auto& f : r.findings) {
        if (f.severity == Severity::Error) ++r.errors;
        else if (f.severity == Severity::Warning) ++r.warnings;
        else ++r.infos;
    }
    return r;
}

// --------------------------------------------------------------- les adresses ---
std::string Range::text() const {
    std::string s = spanText(table, first, last);
    if (bit >= 0) s += ", bit " + std::to_string(bit);
    return s;
}

namespace {
Range rangeOf(const Equipment& e, const zones::MapVar& v) {
    Range r;
    r.equipment = e.name;
    r.variable = v.root;
    r.cell = v.name;
    r.address = v.address;
    r.type = v.type;
    r.table = v.table;
    r.first = v.first;
    r.last = v.last;
    r.bit = v.bit;
    r.written = v.written();
    r.writers = v.writers;
    return r;
}
bool wanted(const Equipment& e, std::string_view equipment) { return e.modbus() && (trimmed(equipment).empty() || same(e.name, trimmed(equipment))); }
} // namespace

std::vector<Range> ranges(const Project& p, std::string_view equipment) {
    std::vector<Range> out;
    for (const auto& e : p.equipments) {
        if (!wanted(e, equipment)) continue;
        const auto m = zones::buildMap(p, e);
        for (const auto& v : m.vars) out.push_back(rangeOf(e, v));
    }
    return out;
}

std::vector<Overlap> overlaps(const Project& p, std::string_view equipment) {
    std::vector<Overlap> out;
    for (const auto& e : p.equipments) {
        if (!wanted(e, equipment)) continue;
        const auto m = zones::buildMap(p, e);
        for (const auto& c : m.conflicts) {
            Overlap o;
            o.a = rangeOf(e, m.vars[c.a]);
            o.b = rangeOf(e, m.vars[c.b]);
            o.severity = c.severity;
            o.table = c.table;
            o.first = c.first;
            o.last = c.last;
            o.text = zones::conflictText(m, c);
            out.push_back(std::move(o));
        }
    }
    return out;
}

std::vector<std::pair<long, long>> freeRanges(const Project& p, std::string_view equipment, MemTable table, long from, long to) {
    const Equipment* e = p.equipmentByName(trimmed(equipment));
    if (!e || !e->modbus()) return {};
    from = std::max(0L, from);
    to = std::min(kLastCell, to);
    if (from > to) return {};
    const auto m = zones::buildMap(p, *e);
    return gapsOf(allowed(*e, table, from, to), usedCells(m, table, {}));
}

std::string nearestFreeAddress(const Project& p, std::string_view variable, std::string* why) {
    const Variable* v = variableNamed(p, variable);
    if (!v) {
        if (why) *why = "variable inconnue : " + trimmed(variable);
        return {};
    }
    const Equipment* e = v->bound() ? p.equipmentByName(v->equipment) : nullptr;
    if (!e || !e->modbus()) {
        if (why) *why = v->name + " n'est li\xC3\xA9" "e \xC3\xA0 aucun \xC3\xA9quipement Modbus";
        return {};
    }
    return nearestIn(*e, zones::buildMap(p, *e), *v, why);
}

// ------------------------------------------------------------ l'optimisation ---
Optimization optimize(const Project& p, const std::vector<std::string>& variables, const std::vector<std::string>& frozen) {
    Optimization out;
    // Les variables choisies, dans l'ordre donne (un motif : dans l'ordre du projet), chacune une fois.
    std::vector<const Variable*> chosen;
    const auto take = [&](const Variable& v) {
        if (std::find(chosen.begin(), chosen.end(), &v) == chosen.end()) chosen.push_back(&v);
    };
    if (variables.empty()) {
        for (const auto& v : p.programs.variables)
            if (v.bound() && !trimmed(v.address).empty()) take(v);
    } else {
        for (const auto& pattern : variables) {
            bool any = false;
            for (const auto& v : p.programs.variables)
                if (glob(pattern, v.name)) {
                    take(v);
                    any = true;
                }
            if (!any) out.notes.push_back("aucune variable ne r\xC3\xA9pond \xC3\xA0 \xC2\xAB " + trimmed(pattern) + " \xC2\xBB");
        }
    }
    const auto isFrozen = [&](const Variable& v) {
        return std::any_of(frozen.begin(), frozen.end(), [&](const std::string& f) { return glob(f, v.name); });
    };
    Project after = p;
    std::vector<const Equipment*> touched;
    for (const auto& e : p.equipments) {
        std::vector<const Variable*> mine;
        for (const auto* v : chosen)
            if (v->bound() && same(v->equipment, e.name)) mine.push_back(v);
        if (mine.empty()) continue;
        if (!e.modbus()) {
            out.notes.push_back(e.name + " n'est pas un \xC3\xA9quipement Modbus : ses variables n'ont pas d'adresse \xC3\xA0 ranger");
            continue;
        }
        touched.push_back(&e);
        const auto m = zones::buildMap(p, e);
        for (const auto* v : mine)
            if (!cellsOf(m, v->name).found)
                out.notes.push_back(v->name + " ne bouge pas : son adresse \xC2\xAB " + v->address + " \xC2\xBB ne se lit pas sur " + e.name);
        bool moved = false;
        for (const MemTable t : kMemTables) {
            // Les variables de cette table qui bougent, et celles qui restent.
            std::vector<std::pair<const Variable*, Cells>> movable;
            std::set<std::string> leaving;
            bool tableUsed = false;
            for (const auto* v : mine) {
                const Cells c = cellsOf(m, v->name);
                if (!c.found || c.table != t) continue;
                tableUsed = true;
                std::string why;
                if (isFrozen(*v)) why = "fig\xC3\xA9" "e (demand\xC3\xA9)";
                else if (!c.oneTable) why = "des cases dans deux tables";
                else if (!v->places.empty()) why = "des membres ont une adresse corrig\xC3\xA9" "e";
                else if (c.topBit) why = "un bit d'un mot (" + v->address + ")";
                else if (shifted(v->address, 0).empty()) why = "adresse \xC2\xAB " + v->address + " \xC2\xBB que le plan ne sait pas r\xC3\xA9\xC3\xA9" "crire";
                if (!why.empty()) {
                    if (why.rfind("fig", 0) != 0) out.notes.push_back(v->name + " ne bouge pas : " + why);
                    continue;
                }
                movable.emplace_back(v, c);
                leaving.insert(upper(v->name));
            }
            if (!tableUsed) continue;
            out.before.push_back(footprintOf(e, m, t));
            if (movable.empty()) continue;
            // Le depart : la premiere case occupee de la table (toutes variables) ; les autres cases restent prises.
            long base = kLastCell;
            for (const auto& v : m.vars)
                if (v.table == t) base = std::min<long>(base, v.first);
            std::vector<Interval> taken = usedCells(m, t, leaving);
            const auto allow = allowed(e, t, 0, kLastCell);
            long cursor = base;
            std::string previous;
            for (const auto& [v, c] : movable) {
                const long size = c.last - c.first + 1;
                long pos = -1;
                for (const auto& [ga, gb] : gapsOf(allow, merged(taken))) {
                    const long start = std::max(ga, cursor);
                    if (start + size - 1 <= gb) {
                        pos = start;
                        break;
                    }
                }
                if (pos < 0) {
                    out.notes.push_back(v->name + " ne bouge pas : aucune place de " + std::to_string(size) + " cases apr\xC3\xA8s " + previous);
                    taken.push_back({c.first, c.last});
                    continue;
                }
                taken.push_back({pos, pos + size - 1});
                cursor = pos + size;
                const std::string where = spanText(t, pos, pos + size - 1);
                if (pos != c.first) {
                    Move mv;
                    mv.variable = v->name;
                    mv.equipment = e.name;
                    mv.before = v->address;
                    mv.after = shifted(v->address, pos - c.first);
                    mv.reason = (previous.empty() ? std::string("premi\xC3\xA8re place de ") + e.name : "rang\xC3\xA9" "e \xC3\xA0 la suite de " + previous) + " (" + where + ")";
                    // Ce qu'elle quitte : un chevauchement.
                    std::set<std::string> with;
                    for (const auto& cf : m.conflicts) {
                        const auto& a = m.vars[cf.a];
                        const auto& b = m.vars[cf.b];
                        if (same(a.root, v->name) && !same(b.root, v->name)) with.insert(b.root);
                        if (same(b.root, v->name) && !same(a.root, v->name)) with.insert(a.root);
                    }
                    if (!with.empty()) mv.reason += " ; plus de chevauchement avec " + joined({with.begin(), with.end()});
                    for (auto& av : after.programs.variables)
                        if (same(av.name, v->name)) av.address = mv.after;
                    out.moves.push_back(std::move(mv));
                    moved = true;
                }
                previous = v->name;
            }
        }
        if (moved && !e.simulated)
            out.notes.push_back(e.name + " : les adresses d'un appareil r\xC3\xA9" "el sont les siennes - ne les d\xC3\xA9placez que si vous programmez aussi "
                                         "l'\xC3\xA9quipement (un automate, un esclave simul\xC3\xA9)");
    }
    // L'apres : la carte refaite par le logiciel sur le projet deplace (rien n'est applique au vrai projet).
    for (const auto* e : touched) {
        const auto m0 = zones::buildMap(p, *e);
        const auto m1 = zones::buildMap(after, *e);
        out.overlapsBefore += distinctConflicts(m0);
        out.overlapsAfter += distinctConflicts(m1);
        for (const auto& fb : out.before)
            if (fb.equipment == e->name) out.after.push_back(footprintOf(*e, m1, fb.table));
    }
    return out;
}

std::size_t applyMoves(Project& p, const std::vector<Move>& moves, std::string* why) {
    std::size_t n = 0;
    std::vector<std::string> refused;
    for (const auto& mv : moves) {
        Variable* v = nullptr;
        for (auto& x : p.programs.variables)
            if (same(x.name, mv.variable)) {
                v = &x;
                break;
            }
        if (!v) refused.push_back(mv.variable + " (introuvable)");
        else if (!same(v->equipment, mv.equipment) || trimmed(v->address) != trimmed(mv.before))
            refused.push_back(mv.variable + " (son adresse a chang\xC3\xA9 : " + v->address + ")");
        else {
            v->address = mv.after;
            ++n;
        }
    }
    if (why) *why = joined(refused);
    return n;
}

} // namespace hmi::audit
