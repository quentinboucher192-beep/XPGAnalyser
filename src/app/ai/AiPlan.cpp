// =============================================================================
//  app/ai/AiPlan.cpp - 1.19.0 : le plan d'operations de l'Assistant IA
// -----------------------------------------------------------------------------
//  Voir AiPlan.hpp. Trois temps, tous sans toucher au projet :
//    1. les noms : ce que chaque operation cree, modifie, lit (refsOf) ;
//    2. l'ordre : les unites et leur tri topologique (schedule) ;
//    3. le plan et le compte rendu : les etapes, leurs etats, la difference
//       entre le projet d'avant et celui d'apres (report).
// =============================================================================
#include "AiPlan.hpp"

#include "../../hmi/HmiFolders.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace app::ai::plan {

namespace {

using A = Operation::Action;

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
std::string prop(const Operation& op, std::string_view key) {
    for (const auto& [k, v] : op.props)
        if (k == key) return trimmed(v);
    return {};
}
std::string roleOf(const Operation& op) {
    const std::string r = lower(prop(op, "role"));
    return r.empty() ? std::string("vue") : r;
}
bool objectAction(A a) noexcept { return !projectLevel(a); }

// Les noms LUS dans un texte (une expression, du code, un type) : la racine de chaque chemin (Pompe_1.Defaut -> Pompe_1)
// et le contenu de chaque chaine ('Reglage_P1' : une popup ouverte par son nom). Les commentaires sont sautes.
void namesIn(std::string_view s, std::vector<std::string>& out) {
    std::size_t i = 0;
    const auto starts = [&](std::size_t k, std::string_view w) { return s.substr(k, w.size()) == w; };
    while (i < s.size()) {
        const char c = s[i];
        if (starts(i, "(*")) {
            const auto e = s.find("*)", i + 2);
            i = e == std::string_view::npos ? s.size() : e + 2;
            continue;
        }
        if (starts(i, "/*")) {
            const auto e = s.find("*/", i + 2);
            i = e == std::string_view::npos ? s.size() : e + 2;
            continue;
        }
        if (starts(i, "//")) {
            const auto e = s.find('\n', i);
            i = e == std::string_view::npos ? s.size() : e + 1;
            continue;
        }
        if (c == '\'' || c == '"') {
            const auto e = s.find(c, i + 1);
            const std::string inner(s.substr(i + 1, (e == std::string_view::npos ? s.size() : e) - i - 1));
            if (!inner.empty() && inner.size() <= 64 && std::all_of(inner.begin(), inner.end(), [](char x) {
                    return std::isalnum(static_cast<unsigned char>(x)) || x == '_';
                }))
                out.push_back(inner);
            i = e == std::string_view::npos ? s.size() : e + 1;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            const bool member = i > 0 && s[i - 1] == '.';
            if (!member) out.emplace_back(s.substr(i, j - i));
            i = j;
            continue;
        }
        ++i;
    }
}

std::string kindOfView(const Operation& op) {
    const std::string r = roleOf(op);
    if (r == "symbole" || r == "popup" || r == "modele" || r == "entete" || r == "pied") return r;
    return "vue";
}

// Ce qui existe deja dans le projet, par sorte.
bool existsIn(const hmi::Project& p, std::string_view kind, std::string_view name, std::string_view view = {}) {
    if (name.empty()) return false;
    if (kind == "type") return p.hmiTypeByName(name) != nullptr;
    if (kind == "variable") return p.variable(name) != nullptr;
    if (kind == "equipement") return p.equipmentByName(name) != nullptr;
    if (kind == "vue" || kind == "symbole" || kind == "popup" || kind == "modele" || kind == "entete" || kind == "pied")
        return p.viewByName(name) != nullptr;
    if (kind == "script") return p.generalScript(name) != nullptr;
    if (kind == "fonction") return p.functionByName(name) != nullptr;
    if (kind == "alarme") return p.alarmByName(name) != nullptr;
    if (kind == "ressource") return p.resourceByName(name) != nullptr;
    if (kind == "dossier") {
        for (std::size_t l = 0; l < static_cast<std::size_t>(hmi::fold::List::Count); ++l)
            for (const auto& f : hmi::fold::allFolders(p, static_cast<hmi::fold::List>(l)))
                if (hmi::fold::sameFolder(f, name)) return true;
        return false;
    }
    if (kind == "objet") {
        const hmi::View* v = view.empty() ? nullptr : p.viewByName(view);
        if (!v) return false;
        return std::any_of(v->objects.begin(), v->objects.end(), [&](const hmi::Object& o) { return iequal(o.name, name); });
    }
    return false;
}

// Un nom lu : sa sorte d'apres le projet (une variable, un type...) ; "nom" s'il n'y est pas (cree par une autre etape,
// ou un mot du langage).
Ref readRef(const hmi::Project& p, const std::string& name) {
    static const char* kinds[] = {"variable", "type", "fonction", "vue", "ressource", "equipement", "alarme", "script"};
    for (const char* k : kinds)
        if (existsIn(p, k, name)) {
            Ref r{k, name, {}, true};
            if (std::string_view(k) == "vue")
                if (const auto* v = p.viewByName(name)) r.kind = v->role == "vue" ? std::string("vue") : v->role;
            return r;
        }
    return Ref{"nom", name, {}, false};
}

void addUnique(std::vector<Ref>& list, Ref r) {
    for (const auto& x : list)
        if (x.kind == r.kind && iequal(x.name, r.name) && iequal(x.view, r.view)) return;
    list.push_back(std::move(r));
}

// Les mots du langage et des types : jamais une dependance.
bool keyword(std::string_view w) {
    static const std::set<std::string> k = {
        "if", "then", "else", "elsif", "end_if", "case", "of", "end_case", "for", "to", "by", "do", "end_for", "while", "end_while",
        "repeat", "until", "end_repeat", "and", "or", "xor", "not", "mod", "true", "false", "return", "exit", "continue", "var",
        "var_input", "var_output", "var_in_out", "var_temp", "end_var", "array", "list", "vector", "map", "set", "ring", "tuple",
        "bool", "int", "dint", "sint", "lint", "uint", "udint", "usint", "ulint", "word", "dword", "byte", "real", "lreal", "string",
        "wstring", "time", "date", "tod", "dt", "char", "any", "sys", "api", "self", "this", "null", "callback"};
    return k.count(lower(w)) > 0;
}

std::vector<std::string> readNames(std::string_view text) {
    std::vector<std::string> raw, out;
    namesIn(text, raw);
    std::set<std::string> seen;
    for (auto& n : raw)
        if (!keyword(n) && seen.insert(lower(n)).second) out.push_back(std::move(n));
    return out;
}

} // namespace

// ---- les libelles ------------------------------------------------------------------------------------
std::string_view stateLabel(State s) noexcept {
    switch (s) {
        case State::ToDo: return "\xC3\xA0 faire";
        case State::Validated: return "valid\xC3\xA9" "e";
        case State::Applied: return "appliqu\xC3\xA9" "e";
        case State::Failed: return "\xC3\xA9" "chou\xC3\xA9" "e";
        case State::Skipped: return "ignor\xC3\xA9" "e";
        case State::Cancelled: return "annul\xC3\xA9" "e";
    }
    return "";
}
std::string_view stateKey(State s) noexcept {
    switch (s) {
        case State::ToDo: return "a_faire";
        case State::Validated: return "validee";
        case State::Applied: return "appliquee";
        case State::Failed: return "echouee";
        case State::Skipped: return "ignoree";
        case State::Cancelled: return "annulee";
    }
    return "a_faire";
}
State stateFromKey(std::string_view k) noexcept {
    for (State s : {State::ToDo, State::Validated, State::Applied, State::Failed, State::Skipped, State::Cancelled})
        if (stateKey(s) == k) return s;
    return State::ToDo;
}
std::string_view categoryLabel(Category c) noexcept {
    switch (c) {
        case Category::Types: return "Types et variables";
        case Category::Equipments: return "\xC3\x89quipements";
        case Category::Addresses: return "Adresses et liaisons";
        case Category::Code: return "Scripts et fonctions";
        case Category::Alarms: return "Alarmes";
        case Category::Views: return "Vues et symboles";
        case Category::Popups: return "Popups";
        case Category::Resources: return "Ressources";
        case Category::Folders: return "Dossiers";
        case Category::Objects: return "Objets des vues";
    }
    return "";
}
Category categoryOf(const Operation& op) noexcept {
    switch (op.action) {
        case A::Type:
        case A::Variables: return Category::Types;
        case A::Equipment: return Category::Equipments;
        case A::Address: return Category::Addresses;
        case A::Script:
        case A::Function: return Category::Code;
        case A::Alarm: return Category::Alarms;
        case A::View: {
            for (const auto& [k, v] : op.props)
                if (k == "role" && (v == "popup" || v == "Popup" || v == "POPUP")) return Category::Popups;
            return Category::Views;
        }
        case A::Resource: return Category::Resources;
        case A::Folder:
        case A::Store: return Category::Folders;
        default: return Category::Objects;
    }
}
std::string_view verbLabel(Verb v) noexcept {
    switch (v) {
        case Verb::Create: return "cr\xC3\xA9" "ation";
        case Verb::Modify: return "modification";
        case Verb::Reuse: return "r\xC3\xA9utilisation";
        case Verb::Remove: return "suppression";
        case Verb::Arrange: return "disposition";
    }
    return "";
}

// ---- ce qu'une operation cree, modifie, lit ------------------------------------------------------------
void refsOf(const hmi::Project& pr, const Proposal& p, std::size_t index, std::vector<Ref>& creates, std::vector<Ref>& modifies,
            std::vector<Ref>& uses) {
    if (index >= p.ops.size()) return;
    const Operation& op = p.ops[index];
    const std::string target = opTarget(p, index);
    const auto read = [&](std::string_view text) {
        for (auto& n : readNames(text)) addUnique(uses, readRef(pr, n));
    };
    const auto obj = [&](const std::string& name) {
        Ref r{"objet", trimmed(name), target, false};
        r.exists = existsIn(pr, "objet", r.name, target.empty() ? (p.view) : target);
        return r;
    };
    switch (op.action) {
        case A::Create:
            addUnique(creates, obj(op.name));
            for (const auto& [k, v] : op.props) read(v);
            break;
        case A::Modify:
            addUnique(modifies, obj(op.name));
            for (const auto& [k, v] : op.props) read(v);
            break;
        case A::Bind:
            addUnique(modifies, obj(op.name));
            read(op.variable);
            break;
        case A::Align:
        case A::Distribute:
        case A::Remove:
            for (const auto& n : op.objects) addUnique(modifies, obj(n));
            break;
        case A::Duplicate:
            for (const auto& n : op.objects) addUnique(uses, obj(n));
            for (const auto& n : op.names)
                if (!trimmed(n).empty()) addUnique(creates, obj(n));
            break;
        case A::Code:
            read(op.code);
            break;
        case A::View: {
            Ref r{kindOfView(op), trimmed(op.name), {}, existsIn(pr, "vue", trimmed(op.name))};
            addUnique(creates, r);
            std::vector<std::string> gens;
            for (const auto& g : op.generics) gens.push_back(lower(trimmed(g.substr(0, g.find(':')))));
            for (const auto& prm : op.params)
                for (auto& n : readNames(prm.type))
                    if (std::find(gens.begin(), gens.end(), lower(n)) == gens.end()) addUnique(uses, readRef(pr, n));
            if (const std::string d = prop(op, "dossier"); !d.empty()) addUnique(uses, Ref{"dossier", d, {}, existsIn(pr, "dossier", d)});
            break;
        }
        case A::Variables:
            for (const auto& v : op.variables) {
                const std::string n = trimmed(v.name);
                addUnique(pr.variable(n) ? modifies : creates, Ref{"variable", n, {}, pr.variable(n) != nullptr});
                read(v.type);
            }
            break;
        case A::Type: {
            addUnique(creates, Ref{"type", trimmed(op.name), {}, existsIn(pr, "type", trimmed(op.name))});
            std::vector<std::string> gens;
            for (const auto& g : op.generics) gens.push_back(lower(trimmed(g.substr(0, g.find(':')))));
            for (const auto& m : op.members)
                for (auto& n : readNames(m.type))
                    if (std::find(gens.begin(), gens.end(), lower(n)) == gens.end() && !iequal(n, op.name)) addUnique(uses, readRef(pr, n));
            break;
        }
        case A::Equipment:
            addUnique(creates, Ref{"equipement", trimmed(op.name), {}, existsIn(pr, "equipement", trimmed(op.name))});
            break;
        case A::Address: {
            std::vector<std::string> root;
            namesIn(op.variable, root);
            const std::string v = root.empty() ? trimmed(op.variable) : root.front();
            addUnique(modifies, Ref{"variable", v, {}, pr.variable(v) != nullptr});
            if (const std::string e = prop(op, "equipement"); !e.empty())
                addUnique(uses, Ref{"equipement", e, {}, existsIn(pr, "equipement", e)});
            break;
        }
        case A::Script:
            addUnique(creates, Ref{"script", trimmed(op.name), {}, existsIn(pr, "script", trimmed(op.name))});
            read(prop(op, "corps").empty() ? op.code : prop(op, "corps"));
            read(prop(op, "surveille"));
            break;
        case A::Function: {
            addUnique(creates, Ref{"fonction", trimmed(op.name), {}, existsIn(pr, "fonction", trimmed(op.name))});
            std::vector<std::string> gens;
            for (const auto& g : op.generics) gens.push_back(lower(trimmed(g.substr(0, g.find(':')))));
            for (auto& n : readNames((prop(op, "corps").empty() ? op.code : prop(op, "corps")) + " " + prop(op, "retour")))
                if (std::find(gens.begin(), gens.end(), lower(n)) == gens.end() && !iequal(n, op.name)) addUnique(uses, readRef(pr, n));
            break;
        }
        case A::Alarm:
            addUnique(creates, Ref{"alarme", trimmed(op.name), {}, existsIn(pr, "alarme", trimmed(op.name))});
            read(prop(op, "condition"));
            for (std::size_t a = prop(op, "message").find('{'); a != std::string::npos;) {
                const std::string m = prop(op, "message");
                const auto b = m.find('}', a);
                if (b == std::string::npos) break;
                read(m.substr(a + 1, b - a - 1));
                a = m.find('{', b);
            }
            break;
        case A::Resource:
            addUnique(creates, Ref{"ressource", trimmed(op.name), {}, existsIn(pr, "ressource", trimmed(op.name))});
            break;
        case A::Folder:
            addUnique(creates, Ref{"dossier", prop(op, "dossier"), {}, existsIn(pr, "dossier", prop(op, "dossier"))});
            break;
        case A::Store:
            addUnique(uses, Ref{"dossier", prop(op, "dossier"), {}, existsIn(pr, "dossier", prop(op, "dossier"))});
            for (const auto& n : op.objects) {
                Ref r = readRef(pr, trimmed(n));
                if (!r.exists) r.kind = "element";
                addUnique(modifies, r);
            }
            break;
    }
    // Un objet vise sa vue : une vue creee plus haut (ou du projet).
    if (objectAction(op.action) && !target.empty()) addUnique(uses, Ref{"vue", target, {}, existsIn(pr, "vue", target)});
}

// ---- l'ordonnancement ---------------------------------------------------------------------------------------
namespace {

// Le rang d'une unite a egalite de dependances : les fondations d'abord, l'ecran ensuite, le rangement a la fin.
int rankOf(const Proposal& p, const Unit& u) {
    if (u.kind == Unit::Kind::ExistingView) return 12;
    const Operation& op = p.ops[u.ops.front()];
    switch (op.action) {
        case A::Folder: return 0;
        case A::Type: return 1;
        case A::Variables: return 2;
        case A::Equipment: return 3;
        case A::Address: return 4;
        case A::Resource: return 5;
        case A::Function: return 6;
        case A::Script: return 7;
        case A::Alarm: return 8;
        case A::View: {
            const std::string r = roleOf(op);
            return r == "vue" ? 11 : r == "popup" ? 10 : 9;
        }
        case A::Store: return 13;
        default: return 12;
    }
}

bool codeAction(A a) noexcept { return a == A::Function || a == A::Script; }

} // namespace

std::vector<std::size_t> Schedule::order() const {
    std::vector<std::size_t> out;
    for (const auto& u : units) out.insert(out.end(), u.ops.begin(), u.ops.end());
    return out;
}

Schedule schedule(const Proposal& p) {
    Schedule s;
    const std::size_t n = p.ops.size();
    s.deps.assign(n, {});
    if (n == 0) return s;
    static const hmi::Project empty;
    // 1. ce que chaque operation cree (par nom, sans casse) et lit.
    std::map<std::string, std::vector<std::size_t>> createdBy;                     // sauf les objets
    std::map<std::pair<std::string, std::string>, std::vector<std::size_t>> objectBy;   // (vue, objet)
    std::vector<std::vector<Ref>> reads(n);
    std::vector<std::string> targets(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<Ref> c, m, u;
        refsOf(empty, p, i, c, m, u);
        targets[i] = lower(opTarget(p, i));
        for (const auto& r : c) {
            if (r.kind == "objet") objectBy[{lower(r.view), lower(r.name)}].push_back(i);
            else createdBy[lower(r.name)].push_back(i);
        }
        // une modification depend de la creation de ce qu'elle modifie (une variable creee puis adressee)
        for (const auto& r : m) u.push_back(r);
        reads[i] = std::move(u);
    }
    // 2. les dependances : une operation depend de celles qui creent ce qu'elle lit.
    for (std::size_t i = 0; i < n; ++i) {
        std::set<std::size_t> d;
        const bool onObjects = objectAction(p.ops[i].action);
        for (const auto& r : reads[i]) {
            const std::string key = lower(r.name);
            if (r.kind == "objet") {
                if (auto it = objectBy.find({lower(r.view), key}); it != objectBy.end())
                    for (auto j : it->second) d.insert(j);
                continue;
            }
            if (auto it = createdBy.find(key); it != createdBy.end())
                for (auto j : it->second) d.insert(j);
            if (onObjects)
                if (auto it = objectBy.find({targets[i], key}); it != objectBy.end())
                    for (auto j : it->second) d.insert(j);
        }
        d.erase(i);
        // dans une meme vue, les objets gardent l'ordre ecrit entre eux (un nom renomme, un alignement apres la creation)
        s.deps[i].assign(d.begin(), d.end());
    }
    // 3. les unites.
    std::vector<int> unitOf(n, -1);
    std::map<std::string, int> viewUnit;   // vue neuve -> unite
    for (std::size_t i = 0; i < n; ++i)
        if (p.ops[i].action == A::View) {
            const std::string key = lower(trimmed(p.ops[i].name));
            if (viewUnit.count(key)) {   // deux creer_vue du meme nom : la seconde a part (la validation la refusera)
                unitOf[i] = static_cast<int>(s.units.size());
                s.units.push_back(Unit{Unit::Kind::Project, {}, {i}});
                continue;
            }
            viewUnit[key] = static_cast<int>(s.units.size());
            unitOf[i] = static_cast<int>(s.units.size());
            s.units.push_back(Unit{Unit::Kind::NewView, trimmed(p.ops[i].name), {i}});
        }
    std::map<std::string, int> existingUnit;
    for (std::size_t i = 0; i < n; ++i) {
        if (unitOf[i] >= 0) continue;
        if (!objectAction(p.ops[i].action)) {
            unitOf[i] = static_cast<int>(s.units.size());
            s.units.push_back(Unit{Unit::Kind::Project, {}, {i}});
            continue;
        }
        if (auto it = viewUnit.find(targets[i]); it != viewUnit.end()) {
            unitOf[i] = it->second;
            s.units[static_cast<std::size_t>(it->second)].ops.push_back(i);
            continue;
        }
        auto it = existingUnit.find(targets[i]);
        if (it == existingUnit.end()) {
            it = existingUnit.emplace(targets[i], static_cast<int>(s.units.size())).first;
            s.units.push_back(Unit{Unit::Kind::ExistingView, opTarget(p, i), {}});
        }
        unitOf[i] = it->second;
        s.units[static_cast<std::size_t>(it->second)].ops.push_back(i);
    }
    // une vue neuve : sa creation, puis ses objets dans l'ordre ecrit
    for (auto& u : s.units)
        if (u.kind == Unit::Kind::NewView || u.kind == Unit::Kind::ExistingView) {
            std::stable_sort(u.ops.begin(), u.ops.end(), [&](std::size_t a, std::size_t b) {
                const bool va = p.ops[a].action == A::View, vb = p.ops[b].action == A::View;
                if (va != vb) return va;
                return a < b;
            });
        }
    // 4. le tri topologique des unites (a egalite : le rang, puis l'ordre ecrit).
    const std::size_t m = s.units.size();
    std::vector<std::set<std::size_t>> after(m), before(m);
    for (std::size_t i = 0; i < n; ++i)
        for (auto j : s.deps[i]) {
            const auto ui = static_cast<std::size_t>(unitOf[i]), uj = static_cast<std::size_t>(unitOf[j]);
            if (ui != uj) {
                before[ui].insert(uj);
                after[uj].insert(ui);
            }
        }
    std::vector<int> rank(m);
    std::vector<std::size_t> first(m);
    for (std::size_t u = 0; u < m; ++u) {
        rank[u] = rankOf(p, s.units[u]);
        first[u] = *std::min_element(s.units[u].ops.begin(), s.units[u].ops.end());
    }
    std::vector<std::size_t> waiting(m);
    for (std::size_t u = 0; u < m; ++u) waiting[u] = before[u].size();
    std::vector<bool> done(m, false);
    std::vector<std::size_t> sorted;
    const auto pick = [&]() -> std::optional<std::size_t> {
        std::optional<std::size_t> best;
        for (std::size_t u = 0; u < m; ++u) {
            if (done[u] || waiting[u] != 0) continue;
            if (!best || std::tie(rank[u], first[u]) < std::tie(rank[*best], first[*best])) best = u;
        }
        return best;
    };
    while (sorted.size() < m) {
        auto u = pick();
        if (!u) {
            // Un cycle : les unites restantes. Entre du code seulement (des fonctions, des scripts qui s'appellent), il est
            // permis - le code est compile une fois tout cree : on le rompt dans l'ordre ecrit. Sinon : bloquant.
            std::vector<std::size_t> rest;
            for (std::size_t k = 0; k < m; ++k)
                if (!done[k]) rest.push_back(k);
            const bool code = std::all_of(rest.begin(), rest.end(), [&](std::size_t k) {
                return s.units[k].kind == Unit::Kind::Project && codeAction(p.ops[s.units[k].ops.front()].action);
            });
            std::size_t chosen = rest.front();
            for (auto k : rest)
                if (std::tie(rank[k], first[k]) < std::tie(rank[chosen], first[chosen])) chosen = k;
            if (!code) {
                std::string names;
                for (std::size_t k = 0; k < rest.size() && k < 6; ++k) {
                    const Operation& op = p.ops[s.units[rest[k]].ops.front()];
                    names += (names.empty() ? "" : " \xE2\x86\x92 ") + (op.name.empty() ? std::string(actionKey(op.action)) : trimmed(op.name));
                }
                s.errors.push_back("D\xC3\xA9pendance circulaire : " + names + " (chacun attend l'autre) - rien de ce cycle ne peut s'appliquer.");
                for (auto k : rest)
                    for (auto o : s.units[k].ops) s.blocked.push_back(o);
                for (auto k : rest) {
                    done[k] = true;
                    sorted.push_back(k);
                }
                break;
            }
            u = chosen;
            waiting[*u] = 0;
        }
        done[*u] = true;
        sorted.push_back(*u);
        for (auto k : after[*u])
            if (waiting[k] > 0) --waiting[k];
    }
    std::vector<Unit> ordered;
    for (auto u : sorted) ordered.push_back(std::move(s.units[u]));
    s.units = std::move(ordered);
    return s;
}

// ---- le plan -----------------------------------------------------------------------------------------------
const Step* Plan::byOp(std::size_t op) const noexcept {
    for (const auto& s : steps)
        if (s.op == op) return &s;
    return nullptr;
}
Step* Plan::byOp(std::size_t op) noexcept {
    for (auto& s : steps)
        if (s.op == op) return &s;
    return nullptr;
}
const Step* Plan::byId(std::string_view id) const noexcept {
    for (const auto& s : steps)
        if (s.id == id) return &s;
    return nullptr;
}
std::size_t Plan::count(State st) const noexcept {
    return static_cast<std::size_t>(std::count_if(steps.begin(), steps.end(), [&](const Step& s) { return s.state == st; }));
}
bool Plan::worthShowing() const noexcept {
    if (steps.size() >= 3) return true;
    std::set<Category> cats;
    for (const auto& s : steps) cats.insert(s.category);
    return cats.size() >= 2;
}

namespace {

std::string list(const std::vector<std::string>& names, std::size_t max = 3) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < max; ++i) out += (i ? ", " : "") + names[i];
    if (names.size() > max) out += ", \xE2\x80\xA6";
    return out;
}

std::string titleOf(const Proposal& p, std::size_t index, const hmi::Project& pr, bool reuse) {
    const Operation& op = p.ops[index];
    const std::string name = trimmed(op.name);
    const std::string target = opTarget(p, index);
    const std::string inView = target.empty() ? std::string{} : " (" + target + ")";
    const std::string create = reuse ? std::string("R\xC3\xA9utiliser ") : std::string("Cr\xC3\xA9" "er ");
    switch (op.action) {
        case A::Create: return "Poser " + name + inView;
        case A::Modify: return "Modifier " + name + inView;
        case A::Bind: return "Associer " + name + " \xC3\xA0 " + trimmed(op.variable);
        case A::Align: return "Aligner " + std::to_string(op.objects.size()) + " objets" + inView;
        case A::Distribute: return "R\xC3\xA9partir " + std::to_string(op.objects.size()) + " objets" + inView;
        case A::Remove: return "Supprimer " + list(op.objects) + inView;
        case A::Duplicate: return "Dupliquer " + list(op.objects) + (op.count > 1 ? " (" + std::to_string(op.count) + " copies)" : std::string{}) + inView;
        case A::Code: return "Poser des objets (code brut)" + inView;
        case A::View: {
            const std::string r = roleOf(op);
            const std::string what = r == "symbole" ? "le symbole " : r == "popup" ? "la popup " : r == "modele" ? "l'\xC3\xA9" "cran mod\xC3\xA8le "
                                   : r == "entete" ? "l'en-t\xC3\xAAte " : r == "pied" ? "le pied de page " : "la vue ";
            std::string g;
            for (const auto& x : op.generics) g += (g.empty() ? "" : ", ") + trimmed(x);
            return create + what + name + (g.empty() ? std::string{} : "<" + g + ">");
        }
        case A::Variables: {
            std::vector<std::string> names;
            for (const auto& v : op.variables) names.push_back(trimmed(v.name));
            if (names.size() == 1) return create + "la variable " + names.front();
            return create + std::to_string(names.size()) + " variables (" + list(names) + ")";
        }
        case A::Type: {
            std::string g;
            for (const auto& x : op.generics) g += (g.empty() ? "" : ", ") + trimmed(x);
            return create + (g.empty() ? "le type " : "le type g\xC3\xA9n\xC3\xA9rique ") + name + (g.empty() ? std::string{} : "<" + g + ">");
        }
        case A::Equipment: {
            const std::string host = prop(op, "adresse").empty() ? prop(op, "ip") : prop(op, "adresse");
            return create + "l'\xC3\xA9quipement " + name + (host.empty() ? std::string{} : " (" + host + ")");
        }
        case A::Address: {
            const std::string a = prop(op, "adresse"), f = prop(op, "formule");
            return "Adresser " + trimmed(op.variable) + " sur " + prop(op, "equipement")
                   + (!f.empty() ? " (formule " + f + ")" : !a.empty() ? " (" + a + ")" : std::string(" (\xC3\xA0 la suite)"));
        }
        case A::Script: return create + "le script " + name;
        case A::Function: return create + "la fonction " + name;
        case A::Alarm: return create + "l'alarme " + name;
        case A::Resource: return create + "la ressource " + name;
        case A::Folder: return create + "le dossier " + prop(op, "dossier") + " (" + prop(op, "liste") + ")";
        case A::Store: return "Ranger " + list(op.objects) + " dans " + prop(op, "dossier");
    }
    (void)pr;
    return name;
}

std::vector<std::string> checksOf(const Operation& op) {
    switch (op.action) {
        case A::Create:
        case A::Modify: return {"le genre et ses propri\xC3\xA9t\xC3\xA9s", "les variables et les formules lues"};
        case A::Bind: return {"la variable existe"};
        case A::View: return {"le nom libre", "les param\xC3\xA8tres et leurs types"};
        case A::Variables: return {"les noms libres (ou identiques : r\xC3\xA9utilis\xC3\xA9" "es)", "les types", "les valeurs initiales"};
        case A::Type: return {"les membres et leurs types", "un type identique : r\xC3\xA9utilis\xC3\xA9"};
        case A::Equipment: return {"l'adresse IP, le port, l'esclave"};
        case A::Address: return {"la variable et l'\xC3\xA9quipement existent", "l'adresse lisible", "les chevauchements"};
        case A::Script:
        case A::Function: return {"le code compil\xC3\xA9 (jamais ex\xC3\xA9" "cut\xC3\xA9)"};
        case A::Alarm: return {"la condition compil\xC3\xA9" "e : chaque nom lu existe"};
        case A::Resource: return {"le fichier joint existe"};
        case A::Folder:
        case A::Store: return {"la liste et le chemin", "les \xC3\xA9l\xC3\xA9ments existent"};
        default: return {"les objets existent"};
    }
}

Verb verbOf(const Operation& op) {
    switch (op.action) {
        case A::Modify:
        case A::Bind:
        case A::Address:
        case A::Store: return Verb::Modify;
        case A::Align:
        case A::Distribute: return Verb::Arrange;
        case A::Remove: return Verb::Remove;
        default: return Verb::Create;
    }
}

} // namespace

Plan build(const hmi::Project& pr, const Proposal& p, const Check& ck) {
    Plan plan;
    const Schedule sch = ck.order.empty() ? schedule(p) : Schedule{};
    std::vector<std::size_t> order = ck.order.empty() ? sch.order() : ck.order;
    const auto& deps = ck.order.empty() ? sch.deps : ck.deps;
    plan.errors = ck.order.empty() ? sch.errors : ck.planErrors;
    std::map<std::size_t, std::string> idOf;
    for (std::size_t k = 0; k < order.size(); ++k) idOf[order[k]] = "E" + std::to_string(k + 1);
    for (std::size_t k = 0; k < order.size(); ++k) {
        const std::size_t i = order[k];
        if (i >= p.ops.size()) continue;
        const Operation& op = p.ops[i];
        Step s;
        s.id = idOf[i];
        s.op = i;
        s.action = op.action;
        s.category = categoryOf(op);
        s.verb = verbOf(op);
        s.name = trimmed(op.name.empty() ? op.variable : op.name);
        s.target = objectAction(op.action) ? opTarget(p, i) : std::string{};
        refsOf(pr, p, i, s.creates, s.modifies, s.uses);
        s.checks = checksOf(op);
        if (i < deps.size())
            for (auto j : deps[i])
                if (idOf.count(j)) s.deps.push_back(idOf[j]);
        bool reuse = false;
        if (i < ck.outcomes.size()) {
            const auto& o = ck.outcomes[i];
            s.errors = o.errors;
            s.warnings = o.warnings;
            s.note = o.note;
            reuse = !o.reused.empty() && o.made.empty();
            switch (o.state) {
                case Check::Outcome::State::Validated: s.state = State::Validated; break;
                case Check::Outcome::State::Failed: s.state = State::Failed; break;
                case Check::Outcome::State::Skipped: s.state = State::Skipped; break;
                case Check::Outcome::State::Excluded:
                    s.state = State::Skipped;
                    s.excluded = true;
                    break;
                case Check::Outcome::State::ToDo: s.state = State::ToDo; break;
            }
        }
        if (reuse) s.verb = Verb::Reuse;
        s.title = titleOf(p, i, pr, reuse);
        plan.steps.push_back(std::move(s));
    }
    // facultative : aucune autre etape n'en depend
    for (auto& s : plan.steps)
        s.optional = std::none_of(plan.steps.begin(), plan.steps.end(), [&](const Step& o) {
            return !o.excluded && std::find(o.deps.begin(), o.deps.end(), s.id) != o.deps.end();
        });
    return plan;
}

// ---- le fichier --------------------------------------------------------------------------------------------
namespace {
json::Value strings(const std::vector<std::string>& v) {
    json::Value a = json::Value::array();
    for (const auto& s : v) a.push(s);
    return a;
}
std::vector<std::string> stringsOf(const json::Value* v) {
    std::vector<std::string> out;
    if (v && v->isArray())
        for (const auto& x : v->items())
            if (x.isString()) out.push_back(x.asString());
    return out;
}
std::string str(const json::Value& o, const char* key) {
    const json::Value* v = o.find(key);
    return v && v->isString() ? v->asString() : std::string{};
}
} // namespace

json::Value toJson(const Plan& plan) {
    json::Value o = json::Value::object();
    json::Value steps = json::Value::array();
    for (const auto& s : plan.steps) {
        json::Value j = json::Value::object();
        j.set("id", s.id);
        j.set("operation", static_cast<double>(s.op));
        j.set("action", std::string(actionKey(s.action)));
        j.set("categorie", static_cast<double>(static_cast<int>(s.category)));
        j.set("verbe", static_cast<double>(static_cast<int>(s.verb)));
        j.set("titre", s.title);
        j.set("nom", s.name);
        if (!s.target.empty()) j.set("vue", s.target);
        j.set("depend", strings(s.deps));
        j.set("etat", std::string(stateKey(s.state)));
        if (!s.note.empty()) j.set("note", s.note);
        if (!s.errors.empty()) j.set("erreurs", strings(s.errors));
        if (!s.warnings.empty()) j.set("avertissements", strings(s.warnings));
        if (s.excluded) j.set("retiree", true);
        steps.push(std::move(j));
    }
    o.set("etapes", std::move(steps));
    if (!plan.errors.empty()) o.set("erreurs", strings(plan.errors));
    return o;
}

Plan fromJson(const json::Value& o) {
    Plan plan;
    if (!o.isObject()) return plan;
    if (const json::Value* a = o.find("etapes"); a && a->isArray())
        for (const auto& j : a->items()) {
            if (!j.isObject()) continue;
            Step s;
            s.id = str(j, "id");
            if (const json::Value* v = j.find("operation"); v && v->isNumber()) s.op = static_cast<std::size_t>(std::max(0.0, v->asNumber()));
            if (auto act = actionFromKey(str(j, "action"))) s.action = *act;
            if (const json::Value* v = j.find("categorie"); v && v->isNumber()) {
                const int c = static_cast<int>(v->asNumber());
                if (c >= 0 && c <= static_cast<int>(Category::Objects)) s.category = static_cast<Category>(c);
            }
            if (const json::Value* v = j.find("verbe"); v && v->isNumber()) {
                const int c = static_cast<int>(v->asNumber());
                if (c >= 0 && c <= static_cast<int>(Verb::Arrange)) s.verb = static_cast<Verb>(c);
            }
            s.title = str(j, "titre");
            s.name = str(j, "nom");
            s.target = str(j, "vue");
            s.deps = stringsOf(j.find("depend"));
            s.state = stateFromKey(str(j, "etat"));
            s.note = str(j, "note");
            s.errors = stringsOf(j.find("erreurs"));
            s.warnings = stringsOf(j.find("avertissements"));
            if (const json::Value* v = j.find("retiree"); v && v->isBool()) s.excluded = v->asBool();
            plan.steps.push_back(std::move(s));
        }
    plan.errors = stringsOf(o.find("erreurs"));
    return plan;
}

// ---- le compte rendu ------------------------------------------------------------------------------------------
namespace {

void add(std::vector<Line>& lines, const std::string& category, const std::vector<std::string>& names) {
    if (names.empty()) return;
    for (auto& l : lines)
        if (l.category == category) {
            for (const auto& n : names)
                if (std::find(l.names.begin(), l.names.end(), n) == l.names.end()) l.names.push_back(n);
            return;
        }
    lines.push_back(Line{category, names});
}

template <class C, class F>
std::vector<std::string> newNames(const C& before, const C& after, F name) {
    std::set<std::string> had;
    for (const auto& x : before) had.insert(lower(name(x)));
    std::vector<std::string> out;
    for (const auto& x : after)
        if (!had.count(lower(name(x)))) out.push_back(name(x));
    return out;
}

std::string roleNoun(const std::string& role, std::size_t n) {
    const bool many = n > 1;
    if (role == "symbole") return many ? "Symboles" : "Symbole";
    if (role == "popup") return many ? "Popups" : "Popup";
    if (role == "modele") return many ? "\xC3\x89" "crans mod\xC3\xA8les" : "\xC3\x89" "cran mod\xC3\xA8le";
    if (role == "entete") return many ? "En-t\xC3\xAAtes" : "En-t\xC3\xAAte";
    if (role == "pied") return many ? "Pieds de page" : "Pied de page";
    return many ? "Vues" : "Vue";
}

bool confirmed(const hmi::Project& after, const Step& s, const Proposal& p) {
    if (s.op >= p.ops.size()) return false;
    const Operation& op = p.ops[s.op];
    const std::string name = trimmed(op.name);
    switch (op.action) {
        case A::Type: return after.hmiTypeByName(name) != nullptr;
        case A::Variables:
            return std::all_of(op.variables.begin(), op.variables.end(), [&](const auto& v) { return after.variable(trimmed(v.name)) != nullptr; });
        case A::View: return after.viewByName(name) != nullptr;
        case A::Equipment: return after.equipmentByName(name) != nullptr;
        case A::Address: {
            std::vector<std::string> root;
            namesIn(op.variable, root);
            const auto* v = after.variable(root.empty() ? trimmed(op.variable) : root.front());
            return v && iequal(v->equipment, prop(op, "equipement"));
        }
        case A::Script: return after.generalScript(name) != nullptr;
        case A::Function: return after.functionByName(name) != nullptr;
        case A::Alarm: return after.alarmByName(name) != nullptr;
        case A::Resource: return after.resourceByName(name) != nullptr;
        case A::Folder: return existsIn(after, "dossier", prop(op, "dossier"));
        case A::Create: {
            const std::string t = s.target.empty() ? p.view : s.target;
            const hmi::View* v = after.viewByName(t);
            if (!v && p.viewId != hmi::kNoId && s.target.empty()) v = after.view(p.viewId);
            // un nom pris : l'objet neuf s'appelle autrement (l'apercu l'a dit) - la vue a au moins change
            return v != nullptr;
        }
        default: return true;
    }
}

} // namespace

std::size_t Report::createdCount() const noexcept {
    std::size_t n = 0;
    for (const auto& l : created) n += l.names.size();
    return n;
}

Report report(const hmi::Project& before, const hmi::Project& after, Plan& plan, const Proposal& p) {
    Report r;
    // 1. les etats : une etape validee est APPLIQUEE si son resultat est dans le projet d'apres.
    for (auto& s : plan.steps) {
        if (s.state == State::Validated || s.state == State::Applied) {
            if (confirmed(after, s, p)) s.state = State::Applied;
            else {
                s.state = State::Failed;
                s.note = "son r\xC3\xA9sultat n'est pas dans le projet apr\xC3\xA8s Appliquer";
            }
        }
    }
    r.planned = plan.steps.size();
    r.applied = plan.count(State::Applied);
    r.failed = plan.count(State::Failed);
    r.skipped = plan.count(State::Skipped);
    // 2. CREE : ce qui est dans le projet d'apres et pas dans celui d'avant.
    add(r.created, "Types", newNames(before.programs.types, after.programs.types, [](const hmi::HmiType& t) {
            return t.generics.empty() ? t.name : t.name + "<" + hmi::genericsText(t.generics) + ">";
        }));
    add(r.created, "Variables", newNames(before.programs.variables, after.programs.variables, [](const hmi::Variable& v) { return v.name; }));
    add(r.created, "\xC3\x89quipements", newNames(before.equipments, after.equipments, [](const hmi::Equipment& e) { return e.name; }));
    add(r.created, "Alarmes", newNames(before.alarms, after.alarms, [](const hmi::AlarmDef& a) { return a.name; }));
    add(r.created, "Scripts", newNames(before.programs.scripts, after.programs.scripts, [](const hmi::Script& s) { return s.name; }));
    add(r.created, "Fonctions", newNames(before.programs.functions, after.programs.functions, [](const hmi::HmiFunction& f) { return f.name; }));
    add(r.created, "Ressources", newNames(before.assets.resources, after.assets.resources, [](const hmi::Resource& x) { return x.name; }));
    {
        std::map<std::string, std::vector<std::string>> byRole;
        for (const auto& v : after.views)
            if (!before.viewByName(v.name)) byRole[v.role].push_back(v.name);
        for (const char* role : {"symbole", "popup", "modele", "entete", "pied", "vue"})
            if (auto it = byRole.find(role); it != byRole.end()) add(r.created, roleNoun(role, it->second.size()), it->second);
    }
    {
        std::vector<std::string> folders;
        for (std::size_t l = 0; l < static_cast<std::size_t>(hmi::fold::List::Count); ++l) {
            const auto list = static_cast<hmi::fold::List>(l);
            const auto had = hmi::fold::allFolders(before, list);
            for (const auto& f : hmi::fold::allFolders(after, list))
                if (std::none_of(had.begin(), had.end(), [&](const std::string& h) { return hmi::fold::sameFolder(h, f); }))
                    folders.push_back(f + " (" + std::string(hmi::fold::label(list)) + ")");
        }
        add(r.created, "Dossiers", folders);
    }
    // les objets poses dans les vues (neuves ou existantes)
    {
        std::vector<std::string> objects;
        for (const auto& v : after.views) {
            const hmi::View* old = before.view(v.id);
            std::size_t n = 0;
            for (const auto& o : v.objects)
                if (!old || std::none_of(old->objects.begin(), old->objects.end(), [&](const hmi::Object& x) { return x.id == o.id; })) ++n;
            if (n > 0) objects.push_back(v.name + " : " + std::to_string(n));
        }
        add(r.created, "Objets dans les vues", objects);
    }
    // 3. MODIFIE : ce qui existait et a change.
    {
        std::vector<std::string> addressed, retyped;
        for (const auto& v : after.programs.variables)
            if (const hmi::Variable* old = before.variable(v.name)) {
                if (old->equipment != v.equipment || old->address != v.address || old->places.size() != v.places.size())
                    addressed.push_back(v.name + (old->address.empty() ? std::string{} : " (" + old->address + " \xE2\x86\x92 " + v.address + ")"));
                if (old->type != v.type) retyped.push_back(v.name);
            }
        add(r.modified, "Adresses et liaisons", addressed);
        add(r.modified, "Variables", retyped);
        std::vector<std::string> views;
        for (const auto& v : after.views)
            if (const hmi::View* old = before.view(v.id); old && fingerprint(*old) != fingerprint(v)) views.push_back(v.name);
        add(r.modified, "Vues existantes", views);
        std::vector<std::string> moved;
        for (std::size_t l = 0; l < static_cast<std::size_t>(hmi::fold::List::Count); ++l) {
            const auto list = static_cast<hmi::fold::List>(l);
            for (const auto& it : hmi::fold::items(after, list)) {
                if (!hmi::fold::contains(before, list, it.id)) continue;
                if (!hmi::fold::sameFolder(hmi::fold::folderOf(before, list, it.id), hmi::fold::folderOf(after, list, it.id)))
                    moved.push_back(it.name + " \xE2\x86\x92 " + hmi::fold::folderOf(after, list, it.id));
            }
        }
        add(r.modified, "Rang\xC3\xA9s dans un dossier", moved);
    }
    // 4. REUTILISE : ce que les etapes appliquees lisent et qui existait deja (jamais compte comme une creation).
    {
        std::map<std::string, std::vector<std::string>> reused;
        static const std::map<std::string, std::string> label = {
            {"type", "Types"}, {"variable", "Variables"}, {"equipement", "\xC3\x89quipements"}, {"ressource", "Ressources"},
            {"symbole", "Symboles"}, {"popup", "Popups"}, {"fonction", "Fonctions"}, {"vue", "Vues"}};
        for (const auto& s : plan.steps) {
            if (s.state != State::Applied) continue;
            for (const auto& u : s.uses) {
                if (!u.exists || !label.count(u.kind)) continue;
                {
                    auto& v = reused[label.at(u.kind)];
                    if (std::find(v.begin(), v.end(), u.name) == v.end()) v.push_back(u.name);
                }
            }
            if (s.verb == Verb::Reuse)
                for (const auto& c : s.creates)
                    if (label.count(c.kind)) {
                        auto& v = reused[label.at(c.kind)];
                        if (std::find(v.begin(), v.end(), c.name) == v.end()) v.push_back(c.name);
                    }
        }
        for (const auto& [k, v] : reused) add(r.reused, k, v);
    }
    // 5. A FAIRE : ce que le logiciel ne fait pas a ta place.
    for (const auto& [eq, range] : proposedDetections(p))
        if (after.equipmentByName(eq))
            r.todo.push_back("Lancer la d\xC3\xA9tection de ce qui r\xC3\xA9pond" + (range.empty() ? std::string{} : " sur " + range)
                             + " (le bouton de la carte : c'est toi qui la lances).");
    for (const auto& e : after.equipments)
        if (!before.equipmentByName(e.name))
            r.todo.push_back(e.simulated ? "\xC3\x89quipement " + e.name + " : simul\xC3\xA9 - indiquer son adresse IP d\xC3\xA9" "finitive avant la mise en service."
                                         : "\xC3\x89quipement " + e.name + " : v\xC3\xA9rifier son adresse IP d\xC3\xA9" "finitive (" + e.host + ").");
    for (const auto& s : plan.steps) {
        if (s.state == State::Failed)
            r.failures.push_back(s.id + " " + s.title + " : " + (!s.errors.empty() ? s.errors.front() : s.note));
        else if (s.state == State::Skipped)
            r.failures.push_back(s.id + " " + s.title + " : non ex\xC3\xA9" "cut\xC3\xA9" "e" + (s.note.empty() ? std::string{} : " (" + s.note + ")"));
    }
    if (!proposedDetections(p).empty())
        r.external.push_back("La d\xC3\xA9tection r\xC3\xA9seau n'a PAS \xC3\xA9t\xC3\xA9 lanc\xC3\xA9" "e : si tu la lances, elle ne se reprend pas par Ctrl+Z "
                             "(elle ne modifie pas le projet).");
    // 6. le verdict
    const std::string ops = std::to_string(r.applied) + (r.applied > 1 ? " op\xC3\xA9rations appliqu\xC3\xA9" "es" : " op\xC3\xA9ration appliqu\xC3\xA9" "e");
    if (r.failed == 0 && r.skipped == 0) r.verdict = ops + ", aucune erreur bloquante. Un seul Ctrl+Z reprend tout.";
    else
        r.verdict = ops + " sur " + std::to_string(r.planned) + " pr\xC3\xA9vues : " + std::to_string(r.failed)
                    + (r.failed > 1 ? " \xC3\xA9" "chou\xC3\xA9" "es" : " \xC3\xA9" "chou\xC3\xA9" "e") + ", " + std::to_string(r.skipped)
                    + " non ex\xC3\xA9" "cut\xC3\xA9" "e" + (r.skipped > 1 ? "s" : "") + ". Ce qui est appliqu\xC3\xA9 se reprend d'un seul Ctrl+Z.";
    return r;
}

json::Value reportToJson(const Report& r) {
    json::Value o = json::Value::object();
    const auto lines = [](const std::vector<Line>& v) {
        json::Value a = json::Value::array();
        for (const auto& l : v) {
            json::Value j = json::Value::object();
            j.set("categorie", l.category);
            j.set("noms", strings(l.names));
            a.push(std::move(j));
        }
        return a;
    };
    o.set("cree", lines(r.created));
    o.set("modifie", lines(r.modified));
    o.set("reutilise", lines(r.reused));
    o.set("a_faire", strings(r.todo));
    o.set("echecs", strings(r.failures));
    o.set("externe", strings(r.external));
    o.set("prevues", static_cast<double>(r.planned));
    o.set("appliquees", static_cast<double>(r.applied));
    o.set("echouees", static_cast<double>(r.failed));
    o.set("ignorees", static_cast<double>(r.skipped));
    if (r.restored) o.set("restaure", true);
    o.set("verdict", r.verdict);
    return o;
}

Report reportFromJson(const json::Value& o) {
    Report r;
    if (!o.isObject()) return r;
    const auto lines = [](const json::Value* a) {
        std::vector<Line> out;
        if (a && a->isArray())
            for (const auto& j : a->items())
                if (j.isObject()) out.push_back(Line{str(j, "categorie"), stringsOf(j.find("noms"))});
        return out;
    };
    r.created = lines(o.find("cree"));
    r.modified = lines(o.find("modifie"));
    r.reused = lines(o.find("reutilise"));
    r.todo = stringsOf(o.find("a_faire"));
    r.failures = stringsOf(o.find("echecs"));
    r.external = stringsOf(o.find("externe"));
    const auto num = [&](const char* k) {
        const json::Value* v = o.find(k);
        return v && v->isNumber() ? static_cast<std::size_t>(std::max(0.0, v->asNumber())) : std::size_t{0};
    };
    r.planned = num("prevues");
    r.applied = num("appliquees");
    r.failed = num("echouees");
    r.skipped = num("ignorees");
    if (const json::Value* v = o.find("restaure"); v && v->isBool()) r.restored = v->asBool();
    r.verdict = str(o, "verdict");
    return r;
}

std::string reportText(const Report& r, std::string_view title) {
    std::string t = "### Compte rendu" + (title.empty() ? std::string{} : " \xE2\x80\x94 " + std::string(title)) + "\n";
    const auto section = [&](const char* head, const std::vector<Line>& lines) {
        if (lines.empty()) return;
        t += "**" + std::string(head) + "**\n";
        for (const auto& l : lines) t += "- " + std::to_string(l.names.size()) + " " + l.category + " : " + list(l.names, 6) + "\n";
    };
    section("Cr\xC3\xA9\xC3\xA9", r.created);
    section("Modifi\xC3\xA9", r.modified);
    section("R\xC3\xA9utilis\xC3\xA9", r.reused);
    if (!r.todo.empty()) {
        t += "**\xC3\x80 faire**\n";
        for (const auto& x : r.todo) t += "- " + x + "\n";
    }
    if (!r.failures.empty()) {
        t += "**Erreurs**\n";
        for (const auto& x : r.failures) t += "- " + x + "\n";
    }
    for (const auto& x : r.external) t += "> " + x + "\n";
    t += r.verdict;
    return t;
}

std::string planText(const Plan& plan, std::size_t maxLines) {
    std::string t;
    std::size_t k = 0;
    for (const auto& s : plan.steps) {
        if (k == maxLines) {
            t += "\xE2\x80\xA6 et " + std::to_string(plan.steps.size() - k) + " autres \xC3\xA9tapes\n";
            break;
        }
        ++k;
        t += std::to_string(k) + ". " + s.title + "\n";
    }
    return t;
}

} // namespace app::ai::plan
