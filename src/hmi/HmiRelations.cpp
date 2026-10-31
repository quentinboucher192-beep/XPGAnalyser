// =============================================================================
//  hmi/HmiRelations.cpp - 1.19.0 : le graphe des relations du projet
// -----------------------------------------------------------------------------
//  LA CONSTRUCTION, en trois temps :
//    1. les noeuds - les vues (et leurs objets, leurs scripts, leurs fonctions),
//       les types (et leurs methodes), les variables, les equipements, les
//       ressources, les scripts et les fonctions du projet, les alarmes (du
//       projet, des symboles, des objets), les recettes, les dossiers ;
//    2. les aretes des champs - le type d'une variable, son equipement, les
//       membres d'un type, le symbole d'une instance, les vues ouvertes par les
//       actions et les objets de navigation, les modeles d'une vue, les
//       ressources citees, les champs des recettes, le rangement en dossiers ;
//    3. les aretes des textes - chaque texte de hmi::forEachCode, et les alarmes,
//       lus par le lecteur de noms (wordsOf), chaque nom confirme par le lecteur
//       du logiciel avant de devenir une arete.
// =============================================================================
#include "HmiRelations.hpp"

#include "HmiAssets.hpp"        // citations, unusedResources
#include "HmiDeclEdit.hpp"      // decledit::referencesIn
#include "HmiFolders.hpp"       // fold : les dossiers des listes
#include "HmiMarkers.hpp"       // markers::strip : les $ des reperes
#include "HmiNavigation.hpp"    // navItems, parseMapZones
#include "HmiObjectAlarms.hpp"  // objectAlarms
#include "HmiRoles.hpp"         // roles::nameOf, splitListRaw, readsAsFormula
#include "HmiScript.hpp"        // forEachCode, scriptNames, scriptCalls, scriptViews, renameCalls, functionCallers
#include "HmiSources.hpp"       // sources::folderOf, variablesInFolder
#include "HmiSymbols.hpp"       // isSymbolView, symbolOf, symbolFunction, viewmembers
#include "HmiTemplates.hpp"     // headerOf, footerOf, viewRoleLabel
#include "HmiTypes.hpp"         // usersOf, allFolders, folderParent

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <set>
#include <utility>

namespace hmi::rel {

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }
bool identStart(char c) { return std::isalpha(uc(c)) != 0 || c == '_'; }
bool identChar(char c) { return std::isalnum(uc(c)) != 0 || c == '_'; }

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
bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(uc(a[i])) != std::toupper(uc(b[i]))) return false;
    return true;
}
std::string noSpaces(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(uc(c))) out += c;
    return out;
}
// `word` dans `text`, sans casse, entre deux bords de nom (Pompe_2 n'est pas dans Pompe_21).
bool containsWord(std::string_view text, std::string_view word) {
    if (word.empty() || word.size() > text.size()) return false;
    const std::string t = upper(text), w = upper(word);
    for (auto at = t.find(w); at != std::string::npos; at = t.find(w, at + 1)) {
        const bool left = at == 0 || !identChar(t[at - 1]);
        const bool right = at + w.size() >= t.size() || !identChar(t[at + w.size()]);
        if (left && right) return true;
    }
    return false;
}

// `edgePath` (un chemin lu) concerne-t-il `query` ? Le meme chemin, un membre en dessous (Cuve_1.Niveau pour Cuve_1),
// ou la variable entiere au-dessus (Cuve_1 lue entiere pour Cuve_1.Niveau). Sans casse ni blancs.
bool pathMatches(std::string_view edgePath, std::string_view query) {
    const std::string e = upper(noSpaces(edgePath)), q = upper(noSpaces(query));
    if (e.empty() || q.empty()) return false;
    const auto below = [](const std::string& longer, const std::string& prefix) {
        return longer.size() > prefix.size() && longer.compare(0, prefix.size(), prefix) == 0
            && (longer[prefix.size()] == '.' || longer[prefix.size()] == '[');
    };
    return e == q || below(e, q) || below(q, e);
}
std::string rootOf(std::string_view path) {
    std::size_t k = 0;
    while (k < path.size() && identChar(path[k])) ++k;
    return std::string(path.substr(0, k));
}

// ---- le lecteur de noms -------------------------------------------------------
//  Les noms d'un code ou d'une expression, hors des chaines ('...', "..."), des commentaires ((* *), /* */, //), des
//  nombres et des litteraux types (T#5s : leur type a part) : chaque nom en tete d'un chemin, avec son chemin
//  (Cuve_1.Niveau, Cuves[i].Niveau), sa ligne, s'il est appele (suivi de '(') ou ecrit (suivi de :=). Un membre
//  (Autre.Nom) n'est pas un nom ; l'argument nomme d'un appel (F(Nom := 1)) non plus. Ce ne sont que des CANDIDATS :
//  le lecteur du logiciel confirme chacun avant qu'il devienne une arete.
struct Word {
    std::string root;
    std::string path;
    int         line{1};
    bool        call{false};
    bool        assigned{false};
    bool        typed{false};      // T_MODE#Auto : root = T_MODE
};

std::vector<Word> wordsOf(std::string_view source) {
    const std::string s = markers::strip(source);
    std::vector<Word> out;
    int line = 1, depth = 0;
    char prev = 0;                 // le dernier signe (hors blancs) avant le mot
    std::size_t i = 0;
    const auto skipTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i)
            if (s[i] == '\n') ++line;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += (s[j] == '$' || s[j] == '\\') && j + 1 < s.size() ? 2 : 1;
            skipTo(std::min(s.size(), j + 1));
            prev = 'v';
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            skipTo(end == std::string::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*/", i + 2);
            skipTo(end == std::string::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            skipTo(end == std::string::npos ? s.size() : end);
            continue;
        }
        if (std::isdigit(uc(c))) {
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            prev = 'v';
            continue;
        }
        if (identStart(c)) {
            const std::size_t start = i;
            const bool member = start > 0 && s[start - 1] == '.';
            while (i < s.size() && identChar(s[i])) ++i;
            std::string word = s.substr(start, i - start);
            if (i < s.size() && s[i] == '#') {                      // T#5s, INT#3, T_MODE#Auto
                ++i;
                while (i < s.size() && (identChar(s[i]) || s[i] == '.')) ++i;
                if (!member) out.push_back({word, word, line, false, false, true});
                prev = 'v';
                continue;
            }
            if (member) {
                prev = 'w';
                continue;
            }
            // Le chemin : .Nom, [ ... ] (les noms des indices se lisent ensuite, a leur tour).
            std::string path = word;
            std::size_t k = i;
            while (k < s.size()) {
                if (s[k] == '.' && k + 1 < s.size() && identStart(s[k + 1])) {
                    std::size_t e = k + 1;
                    while (e < s.size() && identChar(s[e])) ++e;
                    path += s.substr(k, e - k);
                    k = e;
                    continue;
                }
                if (s[k] == '[') {
                    int d = 0;
                    std::size_t e = k;
                    for (; e < s.size(); ++e) {
                        if (s[e] == '[') ++d;
                        else if (s[e] == ']' && --d == 0) break;
                        else if (s[e] == '\n' || s[e] == ';') break;
                    }
                    if (e >= s.size() || s[e] != ']') break;
                    for (std::size_t q = k; q <= e; ++q)
                        if (!std::isspace(uc(s[q]))) path += s[q];
                    k = e + 1;
                    continue;
                }
                break;
            }
            std::size_t after = k;
            while (after < s.size() && (s[after] == ' ' || s[after] == '\t' || s[after] == '\r')) ++after;
            const bool call = after < s.size() && s[after] == '(';
            const bool assigned = after + 1 < s.size() && s[after] == ':' && s[after + 1] == '=';
            const char before = prev;
            prev = 'w';
            // F(Nom := 1), (Nom := 'A', Niveau := 2.5) : un argument nomme, un membre d'une structure ecrite.
            if (assigned && depth > 0 && (before == '(' || before == ',')) continue;
            if (isReservedWord(word)) continue;
            out.push_back({std::move(word), std::move(path), line, call, assigned, false});
            continue;
        }
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        if (!std::isspace(uc(c))) prev = c;
        ++i;
    }
    return out;
}

// Les trous d'un texte a trous ({Niveau:0.0} -> Niveau), comme le moteur les lit ({{ et }} : des accolades) ;
// "=expression" : toute l'expression.
std::vector<std::string> holesOf(std::string_view text) {
    std::vector<std::string> out;
    if (!text.empty() && text.front() == '=') {
        out.emplace_back(text.substr(1));
        return out;
    }
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
            i += 2;
            continue;
        }
        if (c != '{') {
            ++i;
            continue;
        }
        const auto close = text.find('}', i + 1);
        if (close == std::string_view::npos) break;
        std::string_view inside = text.substr(i + 1, close - i - 1);
        if (const auto colon = inside.rfind(':'); colon != std::string_view::npos && colon + 1 < inside.size() && inside[colon + 1] != '=')
            inside = inside.substr(0, colon);
        out.emplace_back(inside);
        i = close + 1;
    }
    return out;
}

// ---- la construction ------------------------------------------------------------
std::string viewKindLabel(const View& v) {
    if (isSymbolView(v)) return "symbole";
    return std::string(viewRoleLabel(v.role));
}

Kind kindOfView(const View& v) {
    if (isSymbolView(v)) return Kind::Symbol;
    if (v.role == "popup") return Kind::Popup;
    if (v.role == "modele" || v.role == "entete" || v.role == "pied") return Kind::Template;
    return Kind::View;
}

// Le contexte d'un texte lu : qui le porte, ce qui y cache un nom global, ou il est.
struct Ctx {
    std::size_t        from{0};
    const CodeSite*    site{nullptr};      // ses declarations cachent leurs noms ; nul : rien
    const View*        view{nullptr};      // ses constantes et ses parametres cachent leurs noms ; nul : aucune vue
    std::string        where;
    bool               code{false};        // un code ST : := ecrit, les lignes comptent
    Link               read{Link::Reads};  // Watches : une condition d'alarme, l'expression d'un script sur changement
    const std::string* writeTarget{nullptr};   // une cible ecrite (une action) : ce texte-la est une ecriture
    bool               writesAll{false};   // la variable d'une commande : le texte entier est ecrit
    bool               notesOnly{false};   // pas de noeud (l'historique, une autorisation) : une note sur la variable
    std::string        note;
};

class Builder {
public:
    explicit Builder(const Project& p) : p_(p) {}

    Graph run() {
        nodes();
        fields();
        texts();
        alarms();
        resources();
        folders();
        finish();
        return std::move(g_);
    }

private:
    const Project& p_;
    Graph          g_;
    std::unordered_map<std::string, std::size_t>              vars_;        // NOM -> variable
    std::unordered_map<std::string, std::vector<std::size_t>> types_;       // NOM COMPLET, NOM COURT -> types
    std::unordered_map<std::string, std::size_t>              equipments_;  // NOM -> equipement
    std::unordered_map<std::string, std::size_t>              recipes_;     // NOM -> recette
    std::unordered_map<const void*, std::size_t>              owners_;      // des declarations, un corps -> son code
    std::unordered_map<const std::string*, std::size_t>       watches_;     // l'expression d'un script sur changement
    std::set<std::string>                                     callables_;   // les noms (courts) de fonctions
    std::map<std::pair<int, std::string>, std::size_t>        folders_;     // (liste, CHEMIN) -> dossier ; -1 : variables

    std::optional<std::size_t> id(Id i) const { return i == kNoId ? std::nullopt : g_.byId(i); }

    std::size_t addNode(Kind k, std::string name, Id ident, Id view, std::string detail) {
        Node n;
        n.kind = k;
        n.name = std::move(name);
        n.id = ident;
        n.view = view;
        n.detail = std::move(detail);
        return g_.add(std::move(n));
    }

    // Les types qu'un texte de type nomme : ARRAY[1..4] OF T_Pompe, T_Mesure<REAL>, Gaz.T_Vanne, LIST OF T_Cuve.
    std::vector<std::size_t> typesIn(std::string_view text) const {
        std::vector<std::size_t> out;
        std::size_t i = 0;
        while (i < text.size()) {
            if (!identStart(text[i]) || (i > 0 && identChar(text[i - 1]))) {
                ++i;
                continue;
            }
            std::size_t e = i;
            while (e < text.size() && (identChar(text[e]) || (text[e] == '.' && e + 1 < text.size() && identStart(text[e + 1])))) ++e;
            const std::string token(text.substr(i, e - i));
            i = e;
            const auto add = [&](const std::string& key) {
                const auto it = types_.find(key);
                if (it == types_.end() || it->second.size() != 1) return false;
                if (std::find(out.begin(), out.end(), it->second.front()) == out.end()) out.push_back(it->second.front());
                return true;
            };
            if (add(upper(token))) continue;
            if (const auto dot = token.rfind('.'); dot != std::string::npos) add(upper(token.substr(dot + 1)));
        }
        return out;
    }
    void typeEdges(std::size_t from, std::string_view typeText, Link link, const std::string& where) {
        for (const auto t : typesIn(typeText))
            if (t != from || link != Link::TypeOf) g_.link(from, t, link, where, trimmed(typeText));
    }
    void declTypes(std::size_t from, const std::vector<Declaration>& decls, const std::string& where) {
        for (const auto& d : decls) {
            typeEdges(from, d.type, Link::TypeOf, where + " (" + (d.kind == DeclKind::Parameter ? "param\xC3\xA8tre " : d.kind == DeclKind::Constant ? "constante " : "variable ") + d.name + ")");
            for (const auto& l : d.locals) typeEdges(from, l.type, Link::TypeOf, where + " (calcul de " + d.name + ")");
        }
    }

    // ---- 1. les noeuds ----------------------------------------------------------------
    void nodes() {
        for (const auto& v : p_.views) {
            std::string detail = viewKindLabel(v);
            if (!v.folder.empty()) detail += ", dossier " + v.folder;
            const auto vn = addNode(kindOfView(v), v.name, v.id, kNoId, std::move(detail));
            if (p_.config.startView == v.id) g_.note(vn, "vue de d\xC3\xA9marrage");
            for (const auto& o : v.objects) {
                const auto on = addNode(Kind::Object, o.name, o.id, v.id, std::string(hmi::kindLabel(o.kind)) + ", " + v.name);
                g_.alias(on, v.name + "." + o.name);
                g_.link(vn, on, Link::Contains, v.name + " . " + o.name);
            }
            for (const auto& sc : v.scripts) {
                const auto sn = addNode(Kind::Script, v.name + "." + sc.event, sc.id, v.id,
                                        "script de " + v.name + " (" + std::string(eventLabel(sc.event)) + ")");
                if (!sc.name.empty() && sc.name != sc.event) g_.alias(sn, v.name + "." + sc.name);
                g_.note(sn, "s'ex\xC3\xA9" "cute avec sa vue");
                g_.link(vn, sn, Link::Contains, v.name + " . " + sc.event);
                owners_[&sc.decls] = sn;
            }
            for (const auto& f : v.functions) {
                const auto fn = addNode(Kind::Function, v.name + "." + f.name, f.id, v.id, "fonction de " + v.name);
                g_.link(vn, fn, Link::Contains, v.name + " . " + f.name);
                owners_[&f.decls] = fn;
                callables_.insert(upper(f.name));
            }
            owners_[&v.constants] = vn;
            for (const auto& o : v.operators) owners_[&o.decls] = vn;
            for (const auto& o : v.objects)
                for (const auto& fo : o.functionOverrides)
                    if (const auto on = id(o.id)) owners_[&fo.decls] = *on;
        }
        for (const auto& t : p_.programs.types) {
            const std::string full = fullName(t);
            const char* what = t.kind == HmiTypeKind::Enumeration ? "\xC3\xA9num\xC3\xA9ration"
                             : t.kind == HmiTypeKind::Callback  ? "rappel"
                             : t.kind == HmiTypeKind::Interface ? "interface"
                                                                : "structure";
            std::string detail = what;
            if (t.kind == HmiTypeKind::Structure) detail += ", " + std::to_string(t.members.size()) + " membre" + (t.members.size() > 1 ? "s" : "");
            const auto tn = addNode(Kind::Type, full, t.id, kNoId, std::move(detail));
            types_[upper(full)].push_back(tn);
            if (full != t.name) {
                types_[upper(t.name)].push_back(tn);
                g_.alias(tn, t.name);
            }
            for (const auto& o : t.operators) owners_[&o.decls] = tn;
            for (const auto& m : t.methods) {
                const auto mn = addNode(Kind::Function, full + "." + m.name, m.id, kNoId, "m\xC3\xA9thode de " + full);
                g_.link(tn, mn, Link::Contains, full + " . " + m.name);
                owners_[&m.decls] = mn;
                owners_[&m] = mn;
            }
        }
        for (const auto& e : p_.equipments) {
            const auto en = addNode(Kind::Equipment, e.name, e.id, kNoId,
                                    std::string(equipmentTypeLabel(e.type)) + (e.simulated ? ", simul\xC3\xA9" : ", " + e.host));
            equipments_[upper(e.name)] = en;
        }
        for (const auto& v : p_.programs.variables) {
            std::string detail = v.type;
            if (v.bound()) detail += ", li\xC3\xA9" "e \xC3\xA0 " + v.equipment + (v.address.empty() ? std::string{} : " (" + v.address + ")");
            const auto vn = addNode(Kind::Variable, v.name, v.id, kNoId, std::move(detail));
            vars_.emplace(upper(v.name), vn);
        }
        for (const auto& r : p_.assets.resources) {
            const auto rn = addNode(Kind::Resource, r.name, r.id, kNoId, r.format.empty() ? std::string(mediaKindLabel(r.kind())) : r.format);
            if (r.kind() == MediaKind::Document) g_.note(rn, "document joint au projet");
        }
        for (const auto& sc : p_.programs.scripts) {
            const std::string full = fullName(sc);
            const auto sn = addNode(Kind::Script, full, sc.id, kNoId,
                                    "script g\xC3\xA9n\xC3\xA9ral (" + std::string(eventLabel(sc.event)) + ")"
                                        + (sc.lang == ScriptLang::ST ? std::string{} : ", " + std::string(scriptLangKey(sc.lang))));
            if (full != sc.name) g_.alias(sn, sc.name);
            if (!same(sc.event, "Appel")) g_.note(sn, "s'ex\xC3\xA9" "cute seul (" + std::string(eventLabel(sc.event)) + ")");
            owners_[&sc.decls] = sn;
            watches_[&sc.watch] = sn;
        }
        for (const auto& f : p_.programs.functions) {
            const std::string full = fullName(f);
            const auto fn = addNode(Kind::Function, full, f.id, kNoId, functionSignature(f));
            if (full != f.name) g_.alias(fn, f.name);
            owners_[&f.decls] = fn;
            callables_.insert(upper(f.name));
        }
        for (const auto& a : p_.alarms) addNode(Kind::Alarm, a.name, a.id, kNoId, "alarme du projet, priorit\xC3\xA9 " + std::to_string(a.priority));
        for (const auto& v : p_.views)
            for (const auto& a : v.alarms) {
                const auto an = addNode(Kind::Alarm, v.name + "." + a.name, a.id, v.id, "alarme du symbole " + v.name);
                if (const auto vn = id(v.id)) g_.link(*vn, an, Link::Contains, v.name + " . alarme " + a.name);
            }
        for (const auto& r : p_.recipes) {
            const auto rn = addNode(Kind::Recipe, r.name, r.id, kNoId,
                                    std::to_string(r.fields.size()) + " champ" + (r.fields.size() > 1 ? "s" : "") + ", "
                                        + std::to_string(r.records.size()) + " jeu" + (r.records.size() > 1 ? "x" : ""));
            recipes_[upper(r.name)] = rn;
        }
        // Les emplois sans arete : la vue d'accueil d'un groupe, un ecran du poste.
        for (const auto& gr : p_.security.groups)
            if (const auto vn = id(gr.startView)) g_.note(*vn, "vue d'accueil du groupe " + gr.name);
        for (const auto& sc : p_.station.screens)
            if (const auto* v = p_.viewByName(roles::nameOf(sc.view)))
                if (const auto vn = id(v->id)) g_.note(*vn, "\xC3\xA9" "cran " + std::to_string(sc.display) + " du poste");
    }

    // ---- 2. les aretes des champs ------------------------------------------------------
    void fields() {
        // Les variables : leur type, leur equipement.
        for (const auto& v : p_.programs.variables) {
            const auto vn = vars_.at(upper(v.name));
            typeEdges(vn, v.type, Link::TypeOf, "variable " + v.name);
            if (v.bound())
                if (const auto it = equipments_.find(upper(v.equipment)); it != equipments_.end())
                    g_.link(vn, it->second, Link::Address, "variable " + v.name, v.address);
        }
        // Les types : leurs membres, leur parent, leurs interfaces, les declarations de leurs methodes.
        for (const auto& t : p_.programs.types) {
            const auto tn = id(t.id);
            if (!tn) continue;
            const std::string full = fullName(t);
            for (const auto& m : t.members) typeEdges(*tn, m.type, Link::Member, full + " . " + m.name);
            if (!trimmed(t.base).empty()) typeEdges(*tn, t.base, Link::TypeOf, full + " EXTENDS " + trimmed(t.base));
            for (const auto& itf : t.interfaces) typeEdges(*tn, itf, Link::TypeOf, full + " IMPLEMENTS " + trimmed(itf));
            for (const auto& m : t.methods)
                if (const auto mn = id(m.id)) {
                    declTypes(*mn, m.decls, "m\xC3\xA9thode " + full + "." + m.name);
                    if (!m.returnType.empty()) typeEdges(*mn, m.returnType, Link::TypeOf, "m\xC3\xA9thode " + full + "." + m.name + " (retour)");
                }
            for (const auto& o : t.operators) declTypes(*tn, o.decls, "op\xC3\xA9rateur de " + full);
        }
        // Les scripts et les fonctions du projet : les types de leurs declarations.
        for (const auto& sc : p_.programs.scripts)
            if (const auto sn = id(sc.id)) declTypes(*sn, sc.decls, "script " + fullName(sc));
        for (const auto& f : p_.programs.functions)
            if (const auto fn = id(f.id)) {
                declTypes(*fn, f.decls, "fonction " + fullName(f));
                if (!f.returnType.empty()) typeEdges(*fn, f.returnType, Link::TypeOf, "fonction " + fullName(f) + " (retour)");
            }
        // Les recettes : leurs champs ecrivent leurs variables (appliquer un jeu).
        for (const auto& r : p_.recipes) {
            const auto rn = recipes_.at(upper(r.name));
            for (const auto& f : r.fields) {
                const std::string path = trimmed(markers::strip(f.variable));
                const auto it = vars_.find(upper(rootOf(path)));
                if (it != vars_.end()) g_.link(rn, it->second, Link::Writes, "recette " + r.name + " . " + f.name, path);
            }
        }
        for (const auto& v : p_.views) viewFields(v);
    }

    void opens(std::size_t from, std::string_view target, const std::string& where) {
        const std::string name = roles::nameOf(target);
        if (name.empty()) return;
        if (const auto* t = p_.viewByName(name))
            if (const auto tn = id(t->id)) g_.link(from, *tn, Link::Opens, where);
    }

    void actionFields(std::size_t from, const std::vector<Action>& list, const std::string& base) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const Action& a = list[i];
            const std::string where = base + " . action " + std::to_string(i + 1) + " (" + std::string(operationLabel(a.operation)) + ")";
            if (operationOpensView(a.operation)) opens(from, a.target, where);
            if (a.operation == Operation::CallScript) {
                const std::string name = roles::nameOf(a.target);
                if (const auto* sc = p_.generalScript(name))
                    if (const auto sn = id(sc->id)) g_.link(from, *sn, Link::Calls, where);
            }
            if (a.operation == Operation::Call) {
                std::string name = trimmed(a.target);
                if (name.size() > 1 && name.front() == '=') name = trimmed(std::string_view(name).substr(1));
                if (const auto* f = p_.functionByName(name))
                    if (const auto fn = id(f->id)) g_.link(from, *fn, Link::Calls, where);
            }
            if (a.operation == Operation::LoadRecipe) {
                std::string name = roles::nameOf(a.target);
                if (const auto slash = name.find('/'); slash != std::string::npos) name = trimmed(name.substr(0, slash));
                if (const auto it = recipes_.find(upper(name)); it != recipes_.end()) g_.link(from, it->second, Link::Uses, where);
            }
        }
    }

    void viewFields(const View& v) {
        const auto vn = id(v.id);
        if (!vn) return;
        // Les modeles : l'ecran modele, l'en-tete et le pied montres.
        if (v.templateView != kNoId)
            if (const auto tn = id(v.templateView); tn && *tn != *vn) g_.link(*vn, *tn, Link::Inherits, v.name + " (\xC3\xA9" "cran mod\xC3\xA8le)");
        if (v.showHeader && v.role != "entete")
            if (const auto* h = headerOf(p_, v))
                if (const auto hn = id(h->id); hn && *hn != *vn) g_.link(*vn, *hn, Link::Inherits, v.name + " (en-t\xC3\xAAte)");
        if (v.showFooter && v.role != "pied")
            if (const auto* f = footerOf(p_, v))
                if (const auto fn = id(f->id); fn && *fn != *vn) g_.link(*vn, *fn, Link::Inherits, v.name + " (pied de page)");
        if (v.upView != kNoId)
            if (const auto un = id(v.upView); un && *un != *vn) g_.link(*vn, *un, Link::Opens, v.name + " (vue parente, fil d'Ariane)");
        // Une popup d'un symbole : le symbole la porte.
        if (v.ownerSymbol != kNoId)
            if (const auto sn = id(v.ownerSymbol); sn && *sn != *vn) g_.link(*sn, *vn, Link::Contains, v.name + " (popup du symbole)");
        // Les parametres : leur type.
        for (const auto& prm : v.params)
            if (!prm.type.empty()) typeEdges(*vn, prm.type, Link::TypeOf, v.name + " (param\xC3\xA8tre " + prm.name + ")");
        declTypes(*vn, v.constants, v.name);
        for (const auto& sc : v.scripts)
            if (const auto sn = id(sc.id)) declTypes(*sn, sc.decls, v.name + "." + sc.event);
        for (const auto& f : v.functions)
            if (const auto fn = id(f.id)) {
                declTypes(*fn, f.decls, "fonction " + v.name + "." + f.name);
                if (!f.returnType.empty()) typeEdges(*fn, f.returnType, Link::TypeOf, "fonction " + v.name + "." + f.name + " (retour)");
            }
        actionFields(*vn, v.actions, v.name);
        for (const auto& o : v.objects) {
            const auto on = id(o.id);
            if (!on) continue;
            const std::string base = v.name + " . " + o.name;
            actionFields(*on, o.actions, base);
            if (o.kind == hmi::Kind::SymbolInstance)
                if (const View* sym = symbolOf(p_, o))
                    if (const auto sn = id(sym->id)) g_.link(*on, *sn, Link::Instance, base + " . symbol");
            if (o.kind == hmi::Kind::NavBar)
                for (const auto& item : navItems(&p_, o)) opens(*on, item.view, base + " . views (barre de navigation)");
            if (o.kind == hmi::Kind::ZoneMap)
                for (const auto& z : parseMapZones(o.text("mapZones")))
                    if (!z.view.empty()) opens(*on, z.view, base + " . mapZones (zone " + z.name + ")");
            if (o.kind == hmi::Kind::LoginPanel)
                if (const auto* after = o.find("afterLogin"); after && !after->value.empty()) opens(*on, after->value, base + " . afterLogin");
            if (const auto* rp = o.find("recipe"); rp && !rp->value.empty())
                if (const auto it = recipes_.find(upper(roles::nameOf(rp->value))); it != recipes_.end())
                    g_.link(*on, it->second, Link::Uses, base + " . recipe");
            for (const auto& fo : o.functionOverrides) declTypes(*on, fo.decls, base + "." + fo.function + " (red\xC3\xA9" "finition)");
            // Les listes de formules a joker de dossier (Ligne/Convoyeur/*) : les variables du dossier.
            for (const auto& pr : o.props) {
                if (pr.value.find("/*") == std::string::npos) continue;
                if (pr.key != "variables" && pr.key != "references" && pr.key != "values" && !roles::readsAsFormula(o.kind, pr.key)) continue;
                for (const auto& item : roles::splitListRaw(pr.value)) {
                    const auto folder = sources::folderOf(item);
                    if (!folder) continue;
                    for (const auto& name : sources::variablesInFolder(p_, *folder))
                        if (const auto it = vars_.find(upper(rootOf(name))); it != vars_.end())
                            g_.link(*on, it->second, Link::Reads, base + " . " + pr.key + " (dossier " + *folder + "/*)", name);
                }
            }
        }
    }

    // ---- 3. les aretes des textes ------------------------------------------------------
    // Le noeud qui porte un texte de forEachCode ; nul : aucun (une alarme : lue a part ; l'historique, une autorisation :
    // une note sur la variable).
    std::optional<std::size_t> ownerOf(const CodeSite& s) const {
        if (s.object) return id(s.object->id);
        if (s.script != kNoId) return id(s.script);
        if (s.function != kNoId) return id(s.function);
        if (s.method)
            if (const auto it = owners_.find(s.method); it != owners_.end()) return it->second;
        if (s.decls)
            if (const auto it = owners_.find(s.decls); it != owners_.end()) return it->second;
        if (s.host) return id(s.host->id);
        if (s.view) return id(s.view->id);
        if (s.where.rfind("recette ", 0) == 0)
            if (const auto it = recipes_.find(upper(s.where.substr(8))); it != recipes_.end()) return it->second;
        return std::nullopt;
    }

    static const Action* actionOf(const CodeSite& s) {
        if (s.action < 0) return nullptr;
        const std::vector<Action>* list = s.object ? &s.object->actions : s.host ? &s.host->actions : nullptr;
        if (!list || static_cast<std::size_t>(s.action) >= list->size()) return nullptr;
        return &(*list)[static_cast<std::size_t>(s.action)];
    }

    static std::string whereOf(const CodeSite& s, const Action* a) {
        if (s.object && s.host) {
            const std::string base = s.host->name + " . " + s.object->name;
            if (!s.key.empty()) return base + " . " + s.key;
            if (s.action >= 0)
                return base + " . action " + std::to_string(s.action + 1) + (a ? " (" + std::string(operationLabel(a->operation)) + ")" : std::string{});
        }
        if (!s.object && s.host && s.action >= 0)
            return s.host->name + " . action " + std::to_string(s.action + 1) + (a ? " (" + std::string(operationLabel(a->operation)) + ")" : std::string{});
        std::string w = s.where;
        if (s.declaration) w += " . " + s.declaration->name;
        return w;
    }

    // `name` est-il cache, a cet endroit, par une declaration du code (ses declarations, les locales d'un calcul, les
    // membres du type d'une methode) ou de la vue (une constante, un parametre ; une popup d'un symbole : un parametre du
    // symbole) ?
    bool hidden(const Ctx& c, const std::string& whole, std::string_view name) const {
        if (c.site) {
            const CodeSite& s = *c.site;
            if (s.decls)
                for (const auto& d : *s.decls)
                    if (same(d.name, name)) return true;
            if (s.declaration && whole.data() == s.declaration->calc.data())
                for (const auto& l : s.declaration->locals)
                    if (same(l.name, name)) return true;
            // Le corps d'une methode : les membres de son type s'y lisent par leur nom nu (SELF.Nom).
            if (s.ownerType)
                for (const auto& m : types::membersOf(p_, fullName(*s.ownerType)))
                    if (same(m.name, name)) return true;
        }
        const View* v = c.view;
        for (int depth = 0; v && depth < 2; ++depth) {
            if (v->param(name)) return true;
            for (const auto& d : v->constants)
                if (same(d.name, name)) return true;
            v = v->ownerSymbol != kNoId && v->ownerSymbol != v->id ? p_.view(v->ownerSymbol) : nullptr;
        }
        return false;
    }

    std::optional<std::size_t> functionFor(const Ctx& c, std::string_view name) const {
        if (c.site && c.site->scope)
            if (const auto* f = symbolFunction(*c.site->scope, name))
                if (const auto fn = id(f->id)) return fn;
        if (c.view) {
            const auto fs = viewmembers::functions(*c.view, name);
            if (!fs.empty())
                if (const auto fn = id(fs.front()->id)) return fn;
        }
        if (const auto* f = p_.functionByName(name)) return id(f->id);
        return std::nullopt;
    }

    // Un texte : du code (`c.code`) ou une expression ; `whole` : le texte entier qui le porte (un trou est un morceau).
    void read(const Ctx& c, const std::string& whole, std::string_view piece) {
        if (trimmed(piece).empty()) return;
        const auto words = wordsOf(piece);
        std::set<std::string> confirmed;        // un code : les noms que scriptNames y lit
        bool namesRead = false;
        const std::string pieceText(piece);
        const bool writtenTarget = c.writeTarget && *c.writeTarget == pieceText;
        const std::string writtenRoot = writtenTarget ? upper(rootOf(trimmed(targetVariable(*c.writeTarget)))) : std::string{};
        const auto lineOf = [&](int line) { return c.code && line > 0 ? c.where + ", ligne " + std::to_string(line) : c.where; };
        for (const auto& w : words) {
            const std::string key = upper(w.root);
            if (w.typed) {
                // T_MODE#Auto : une valeur d'une enumeration IHM.
                if (const auto it = types_.find(key); it != types_.end() && it->second.size() == 1 && !c.notesOnly)
                    g_.link(c.from, it->second.front(), Link::TypeOf, lineOf(w.line), w.root + "#...");
                continue;
            }
            if (const auto it = vars_.find(key); it != vars_.end()) {
                if (hidden(c, whole, w.root)) continue;
                bool ok = false;
                if (c.code) {
                    if (!namesRead) {
                        for (const auto& u : scriptNames(piece)) confirmed.insert(upper(u.name));
                        namesRead = true;
                    }
                    ok = confirmed.count(key) != 0 || (w.call && !decledit::referencesIn(piece, CodeForm::Code, w.root).empty());
                } else {
                    ok = !decledit::referencesIn(piece, CodeForm::Expression, w.root).empty();
                }
                if (!ok) continue;
                if (c.notesOnly) {
                    g_.note(it->second, c.note);
                    continue;
                }
                Link l = c.read;
                if ((c.code && w.assigned) || c.writesAll || (writtenTarget && key == writtenRoot)) l = Link::Writes;
                g_.link(c.from, it->second, l, lineOf(w.line), w.path);
                continue;
            }
            if (c.notesOnly) continue;
            if (callables_.count(key) || w.call) {
                if (const auto fn = functionFor(c, w.root); fn && *fn != c.from) {
                    if (renameCalls(piece, w.root, "\x01", false, true) != piece) g_.link(c.from, *fn, Link::Calls, lineOf(w.line));
                } else if (w.call && key.size() > 3 && key.compare(0, 3, "TO_") == 0) {
                    // TO_T_MODE(...) : la conversion vers un type IHM (son operateur).
                    if (const auto it = types_.find(key.substr(3)); it != types_.end() && it->second.size() == 1)
                        g_.link(c.from, it->second.front(), Link::TypeOf, lineOf(w.line), w.root + "(...)");
                }
            }
        }
        if (c.notesOnly) return;
        // Les chaines qui nomment : IHM_APPELER('Calcul') appelle un script ; IHM_POPUP('Pop_Pompe') ouvre une vue.
        if (pieceText.find('\'') == std::string::npos && pieceText.find('"') == std::string::npos) return;
        for (const auto& name : scriptCalls(pieceText))
            if (const auto* sc = p_.generalScript(name))
                if (const auto sn = id(sc->id); sn && *sn != c.from) g_.link(c.from, *sn, Link::Calls, c.where + " (IHM_APPELER)");
        for (const auto& name : scriptViews(pieceText))
            if (const auto* v = p_.viewByName(name))
                if (const auto vn = id(v->id)) g_.link(c.from, *vn, Link::Opens, c.where + " (" + name + ")");
    }

    void readText(const Ctx& c, const std::string& text, CodeForm form) {
        if (form == CodeForm::Template) {
            Ctx e = c;
            e.code = false;
            for (const auto& piece : holesOf(text)) read(e, text, piece);
            return;
        }
        Ctx e = c;
        e.code = form == CodeForm::Code;
        read(e, text, text);
    }

    void texts() {
        forEachCode(p_, [&](const std::string& text, CodeForm form, const CodeSite& s) {
            if (s.again || trimmed(text).empty()) return;
            if (!s.object && !s.host && !s.view && s.where.rfind("alarme ", 0) == 0) return;   // lues a part (alarms)
            Ctx c;
            c.site = &s;
            c.view = s.view ? s.view : s.host;
            const Action* a = actionOf(s);
            c.where = whereOf(s, a);
            const auto from = ownerOf(s);
            if (!from) {
                // L'historique, une autorisation par expression : des emplois sans noeud.
                c.notesOnly = true;
                c.note = s.where == "historique" ? std::string("archiv\xC3\xA9" "e par l'historique") : s.where;
                readText(c, text, form);
                return;
            }
            c.from = *from;
            if (const auto it = watches_.find(&text); it != watches_.end()) c.read = Link::Watches;
            if (a && (operationWritesVariable(a->operation) || a->operation == Operation::RequestResource)) c.writeTarget = &a->target;
            if (s.object && (s.key == "variable" || s.key == "output")
                && (kindWritesVariable(s.object->kind) || s.object->kind == hmi::Kind::InputField))
                c.writesAll = true;
            readText(c, text, form);
        });
        // Les scripts C et C++ : ni compiles ni executes, mais ils nomment des variables et des fonctions.
        const auto cScript = [&](const Script& sc, const View* v, const std::string& where) {
            if (sc.lang == ScriptLang::ST) return;
            const auto sn = id(sc.id);
            if (!sn) return;
            for (const auto& w : wordsOf(sc.body)) {
                const std::string key = upper(w.root);
                const std::string at = where + ", ligne " + std::to_string(w.line);
                if (const auto it = vars_.find(key); it != vars_.end()) {
                    if (v && (v->param(w.root) || viewmembers::constant(*v, w.root))) continue;
                    g_.link(*sn, it->second, Link::Reads, at, w.path);
                } else if (w.call && callables_.count(key)) {
                    Ctx c;
                    c.view = v;
                    if (const auto fn = functionFor(c, w.root)) g_.link(*sn, *fn, Link::Calls, at);
                }
            }
        };
        for (const auto& sc : p_.programs.scripts) cScript(sc, nullptr, "script " + fullName(sc));
        for (const auto& v : p_.views)
            for (const auto& sc : v.scripts) cScript(sc, &v, v.name + "." + sc.event);
    }

    // Les alarmes : du projet, des symboles, des objets (developpees avec les arguments des instances).
    void alarm(std::size_t from, const AlarmDef& a, const View* view, const std::string& where) {
        Ctx c;
        c.from = from;
        c.view = view;
        c.where = where + " (condition)";
        c.read = Link::Watches;
        readText(c, a.condition, CodeForm::Expression);
        c.read = Link::Reads;
        c.where = where + " (message)";
        readText(c, a.message, CodeForm::Template);
        c.where = where + " (consigne)";
        readText(c, a.instruction, CodeForm::Template);
    }
    void alarms() {
        for (const auto& a : p_.alarms)
            if (const auto an = id(a.id)) alarm(*an, a, nullptr, "alarme " + a.name);
        for (const auto& v : p_.views)
            for (const auto& a : v.alarms)
                if (const auto an = id(a.id)) alarm(*an, a, &v, "alarme " + v.name + "." + a.name);
        for (const auto& oa : objectAlarms(p_)) {
            std::string detail = "alarme de l'objet " + oa.objectName + " (" + oa.view + ", "
                               + (oa.source == ObjectAlarmSource::Symbol ? "symbole " + oa.symbol : std::string("biblioth\xC3\xA8que")) + ")";
            if (!oa.active) detail += ", d\xC3\xA9" "coch\xC3\xA9" "e";
            const auto an = addNode(Kind::Alarm, oa.def.name, kNoId, oa.viewId, std::move(detail));
            if (const auto on = id(oa.objectId)) g_.link(*on, an, Link::Contains, oa.view + " . " + oa.objectName + " . alarme " + oa.localName);
            alarm(an, oa.def, nullptr, "alarme " + oa.def.name);
        }
    }

    // Les ressources : qui les cite (hmi::citations, la regle de Generer).
    void resources() {
        for (const auto& r : p_.assets.resources) {
            const auto rn = id(r.id);
            if (!rn) continue;
            for (const auto& ci : citations(p_, r.name)) {
                std::optional<std::size_t> from;
                std::string where = ci.where;
                if (ci.object != kNoId) {
                    from = id(ci.object);
                    // "Vue/Objet.image" -> "Vue . Objet . image" (la forme des autres aretes).
                    const View* v = p_.view(ci.view);
                    const Object* o = v ? v->object(ci.object) : nullptr;
                    if (o) {
                        const std::string head = v->name + "/" + o->name;
                        where = v->name + " . " + o->name;
                        if (ci.where.rfind(head + ".", 0) == 0) where += " . " + ci.where.substr(head.size() + 1);
                        else if (ci.where.size() > head.size() && ci.where.rfind(head, 0) == 0) where += ci.where.substr(head.size());
                    }
                }
                else if (ci.view != kNoId) {
                    from = id(ci.view);
                    if (const auto* v = p_.view(ci.view))
                        for (const auto& sc : v->scripts)
                            if (ci.where == v->name + "." + sc.event) from = id(sc.id);
                } else if (ci.where.rfind("script ", 0) == 0) {
                    if (const auto* sc = p_.generalScript(ci.where.substr(7))) from = id(sc->id);
                }
                if (from) g_.link(*from, *rn, Link::Resource, where);
                else g_.note(*rn, ci.where);
            }
        }
    }

    // Les dossiers : un noeud par dossier de chaque liste ; il contient ses elements et ses sous-dossiers.
    std::size_t folderNode(int list, const std::string& path, const std::string& label) {
        const auto key = std::make_pair(list, upper(path));
        if (const auto it = folders_.find(key); it != folders_.end()) return it->second;
        const auto fn = addNode(Kind::Folder, path, kNoId, kNoId, label);
        folders_.emplace(key, fn);
        const std::string parent = types::folderParent(path);
        if (!parent.empty()) g_.link(folderNode(list, parent, label), fn, Link::Contains, label + " : " + parent);
        return fn;
    }
    void folders() {
        for (int l = 0; l < static_cast<int>(fold::List::Count); ++l) {
            const auto list = static_cast<fold::List>(l);
            const std::string label(fold::label(list));
            for (const auto& f : fold::allFolders(p_, list)) folderNode(l, f, label);
            for (const auto& item : fold::items(p_, list)) {
                if (trimmed(item.folder).empty()) continue;
                if (const auto in = id(item.id)) g_.link(folderNode(l, item.folder, label), *in, Link::Contains, label + " : " + item.folder);
            }
        }
        const std::string label = "Variables IHM";
        for (const auto& f : types::allFolders(p_)) folderNode(-1, f, label);
        for (const auto& v : p_.programs.variables)
            if (!trimmed(v.folder).empty())
                g_.link(folderNode(-1, v.folder, label), vars_.at(upper(v.name)), Link::Contains, label + " : " + v.folder);
    }

    // Les emplois que le graphe ne voit pas, confirmes par le logiciel : une fonction appelee d'une facon que le
    // lecteur ne suit pas (Vue.Fonction(...), une valeur initiale de rappel), un type employe ailleurs.
    void finish() {
        const auto hasIn = [&](std::size_t n, std::initializer_list<Link> links) {
            for (const auto e : g_.in(n))
                if (std::find(links.begin(), links.end(), g_.edges()[e].link) != links.end()) return true;
            return false;
        };
        for (const auto& f : p_.programs.functions)
            if (const auto fn = id(f.id); fn && !hasIn(*fn, {Link::Calls})) {
                const auto callers = functionCallers(p_, fullName(f));
                if (!callers.empty()) g_.note(*fn, "appel\xC3\xA9" "e par " + callers.front());
            }
        for (const auto& t : p_.programs.types)
            if (const auto tn = id(t.id); tn && !hasIn(*tn, {Link::TypeOf, Link::Member})) {
                const auto employers = types::usersOf(p_, fullName(t));
                if (!employers.empty()) g_.note(*tn, "employ\xC3\xA9 par " + employers.front());
            }
    }
};

bool hasInLink(const Graph& g, std::size_t n, std::initializer_list<Link> links) {
    for (const auto e : g.in(n))
        if (std::find(links.begin(), links.end(), g.edges()[e].link) != links.end()) return true;
    return false;
}

void pushUnique(std::vector<std::size_t>& out, std::vector<char>& seen, std::size_t n) {
    if (seen[n]) return;
    seen[n] = 1;
    out.push_back(n);
}

} // namespace

// ---------------------------------------------------------------- libelles ----
std::string_view kindLabel(Kind k) noexcept {
    switch (k) {
        case Kind::View:      return "vue";
        case Kind::Symbol:    return "symbole";
        case Kind::Popup:     return "popup";
        case Kind::Template:  return "\xC3\xA9" "cran mod\xC3\xA8le";
        case Kind::Object:    return "objet";
        case Kind::Type:      return "type";
        case Kind::Variable:  return "variable";
        case Kind::Equipment: return "\xC3\xA9quipement";
        case Kind::Alarm:     return "alarme";
        case Kind::Script:    return "script";
        case Kind::Function:  return "fonction";
        case Kind::Resource:  return "ressource";
        case Kind::Folder:    return "dossier";
        case Kind::Recipe:    return "recette";
    }
    return "\xC3\xA9l\xC3\xA9ment";
}

std::string_view linkLabel(Link l) noexcept {
    switch (l) {
        case Link::Reads:    return "lit";
        case Link::Writes:   return "\xC3\xA9" "crit";
        case Link::TypeOf:   return "est du type";
        case Link::Member:   return "a un membre de type";
        case Link::Address:  return "est li\xC3\xA9" "e \xC3\xA0";
        case Link::Calls:    return "appelle";
        case Link::Instance: return "instancie";
        case Link::Opens:    return "ouvre";
        case Link::Contains: return "contient";
        case Link::Resource: return "cite la ressource";
        case Link::Watches:  return "surveille";
        case Link::Inherits: return "h\xC3\xA9rite de";
        case Link::Uses:     return "emploie";
    }
    return "d\xC3\xA9pend de";
}

// ------------------------------------------------------------------- Graph ----
std::size_t Graph::add(Node n) {
    const std::size_t i = nodes_.size();
    byName_[upper(n.name)].push_back(i);
    if (n.id != kNoId) byId_.emplace(n.id, i);
    nodes_.push_back(std::move(n));
    out_.emplace_back();
    in_.emplace_back();
    return i;
}

bool Graph::link(std::size_t from, std::size_t to, Link l, std::string where, std::string path) {
    if (from >= nodes_.size() || to >= nodes_.size()) return false;
    std::string key = std::to_string(from) + "|" + std::to_string(to) + "|" + std::to_string(static_cast<int>(l)) + "|" + where + "|" + path;
    if (!seen_.insert(std::move(key)).second) return false;
    const std::size_t e = edges_.size();
    edges_.push_back({from, to, l, std::move(where), std::move(path)});
    out_[from].push_back(e);
    in_[to].push_back(e);
    return true;
}

void Graph::note(std::size_t node, std::string text) {
    if (node >= nodes_.size() || text.empty()) return;
    auto& notes = nodes_[node].notes;
    if (std::find(notes.begin(), notes.end(), text) == notes.end()) notes.push_back(std::move(text));
}

void Graph::alias(std::size_t node, std::string_view name) {
    if (node >= nodes_.size() || name.empty()) return;
    auto& list = byName_[upper(name)];
    if (std::find(list.begin(), list.end(), node) == list.end()) list.push_back(node);
}

std::optional<std::size_t> Graph::find(Kind k, std::string_view name) const {
    const auto it = byName_.find(upper(trimmed(name)));
    if (it == byName_.end()) return std::nullopt;
    for (const auto i : it->second) {
        const Kind nk = nodes_[i].kind;
        if (nk == k) return i;
        if (k == Kind::View && (nk == Kind::Symbol || nk == Kind::Popup || nk == Kind::Template)) return i;
    }
    return std::nullopt;
}

std::vector<std::size_t> Graph::findAll(std::string_view name) const {
    const auto it = byName_.find(upper(trimmed(name)));
    if (it == byName_.end()) return {};
    std::vector<std::size_t> out = it->second;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::optional<std::size_t> Graph::byId(Id i) const {
    const auto it = byId_.find(i);
    if (it == byId_.end()) return std::nullopt;
    return it->second;
}

Graph build(const Project& p) { return Builder(p).run(); }

// --------------------------------------------------------------- questions ----
std::vector<std::size_t> users(const Graph& g, std::size_t node) {
    std::vector<std::size_t> out;
    if (node >= g.size()) return out;
    std::vector<char> seen(g.size(), 0);
    for (const auto e : g.in(node)) {
        const Edge& edge = g.edges()[e];
        if (edge.link == Link::Contains) continue;
        pushUnique(out, seen, edge.from);
    }
    return out;
}

std::vector<Hit> impactDetails(const Graph& g, std::size_t start) {
    std::vector<Hit> out;
    if (start >= g.size()) return out;
    // 0 : rien ; 1 : supprime ; 2 : touche ; 3 : deplace.
    std::vector<char> state(g.size(), 0);
    std::deque<std::size_t> queue;
    state[start] = 1;
    queue.push_back(start);
    const auto valueLink = [](Link l) {
        return l == Link::Reads || l == Link::Writes || l == Link::Watches || l == Link::TypeOf || l == Link::Member || l == Link::Calls
            || l == Link::Instance || l == Link::Inherits || l == Link::Address || l == Link::Uses || l == Link::Resource;
    };
    while (!queue.empty()) {
        const std::size_t x = queue.front();
        queue.pop_front();
        const bool removed = state[x] == 1;
        if (removed) {
            const bool folder = g.node(x).kind == Kind::Folder;
            for (const auto e : g.out(x)) {
                const Edge& edge = g.edges()[e];
                if (edge.link != Link::Contains || state[edge.to]) continue;
                if (folder) {
                    state[edge.to] = 3;
                    out.push_back({edge.to, Hit::How::Moved, e});
                } else {
                    state[edge.to] = 1;
                    out.push_back({edge.to, Hit::How::Removed, e});
                    queue.push_back(edge.to);
                }
            }
        }
        for (const auto e : g.in(x)) {
            const Edge& edge = g.edges()[e];
            const std::size_t src = edge.from;
            if (state[src] || g.node(src).kind == Kind::Folder) continue;
            if (!(edge.link == Link::Contains || removed || valueLink(edge.link))) continue;
            state[src] = 2;
            out.push_back({src, Hit::How::Affected, e});
            queue.push_back(src);
        }
    }
    return out;
}

std::vector<std::size_t> impact(const Graph& g, std::size_t node) {
    std::vector<std::size_t> out;
    for (const auto& h : impactDetails(g, node)) out.push_back(h.node);
    return out;
}

std::string orphanReason(const Graph& g, std::size_t i) {
    if (i >= g.size()) return {};
    const Node& n = g.node(i);
    if (!n.notes.empty()) return {};
    switch (n.kind) {
        case Kind::Variable:
            if (!hasInLink(g, i, {Link::Reads, Link::Writes, Link::Watches})) return "variable jamais lue ni \xC3\xA9" "crite";
            break;
        case Kind::Symbol:
            if (!hasInLink(g, i, {Link::Instance})) return "symbole jamais instanci\xC3\xA9";
            break;
        case Kind::Popup:
            if (!hasInLink(g, i, {Link::Opens})) return "popup jamais ouverte";
            break;
        case Kind::Function: {
            if (n.view != kNoId) break;                              // une fonction de vue : appelee par Vue.Fonction aussi
            bool method = false;
            for (const auto e : g.in(i))
                if (g.edges()[e].link == Link::Contains && g.node(g.edges()[e].from).kind == Kind::Type) method = true;
            if (!method && !hasInLink(g, i, {Link::Calls})) return "fonction jamais appel\xC3\xA9" "e";
            break;
        }
        case Kind::Script:
            if (n.view == kNoId && !hasInLink(g, i, {Link::Calls})) return "script sur appel jamais appel\xC3\xA9";
            break;
        case Kind::Resource:
            if (!hasInLink(g, i, {Link::Resource})) return "ressource inutilis\xC3\xA9" "e";
            break;
        case Kind::Equipment:
            if (!hasInLink(g, i, {Link::Address})) return "\xC3\xA9quipement sans variable li\xC3\xA9" "e";
            break;
        case Kind::Type:
            if (!hasInLink(g, i, {Link::TypeOf, Link::Member})) return "type sans emploi";
            break;
        default:
            break;
    }
    return {};
}

std::vector<std::size_t> orphans(const Graph& g) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < g.size(); ++i)
        if (!orphanReason(g, i).empty()) out.push_back(i);
    return out;
}

std::vector<std::size_t> readers(const Graph& g, std::string_view path, const std::vector<Kind>& kinds) {
    std::vector<std::size_t> out;
    const std::string root = upper(rootOf(trimmed(path)));
    if (root.empty()) return out;
    std::vector<char> seen(g.size(), 0);
    for (const auto& e : g.edges()) {
        if (e.link != Link::Reads && e.link != Link::Writes && e.link != Link::Watches) continue;
        const Node& to = g.node(e.to);
        if (to.kind != Kind::Variable || upper(to.name) != root) continue;
        if (!pathMatches(e.path.empty() ? to.name : e.path, path)) continue;
        if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), g.node(e.from).kind) == kinds.end()) continue;
        pushUnique(out, seen, e.from);
    }
    return out;
}

std::vector<std::size_t> objectsUsing(const Graph& g, std::string_view path) { return readers(g, path, {Kind::Object}); }

std::vector<std::size_t> scriptsUsing(const Graph& g, std::string_view path) { return readers(g, path, {Kind::Script, Kind::Function}); }

std::vector<std::size_t> equipmentsOfView(const Graph& g, std::size_t view) {
    std::vector<std::size_t> out;
    if (view >= g.size()) return out;
    // Ce que la vue montre ou fait tourner : elle, ce qu'elle contient (de proche en proche), ses modeles, les symboles
    // de ses instances (et ce qu'ils contiennent).
    std::vector<char> inside(g.size(), 0);
    std::deque<std::size_t> queue{view};
    inside[view] = 1;
    while (!queue.empty()) {
        const auto x = queue.front();
        queue.pop_front();
        for (const auto e : g.out(x)) {
            const Edge& edge = g.edges()[e];
            if (edge.link != Link::Contains && edge.link != Link::Inherits && edge.link != Link::Instance) continue;
            if (g.node(edge.to).kind == Kind::Popup) continue;                // une popup s'ouvre, elle n'est pas dans la vue
            if (inside[edge.to]) continue;
            inside[edge.to] = 1;
            queue.push_back(edge.to);
        }
    }
    std::vector<char> seen(g.size(), 0);
    for (std::size_t x = 0; x < g.size(); ++x) {
        if (!inside[x]) continue;
        for (const auto e : g.out(x)) {
            const Edge& edge = g.edges()[e];
            if (edge.link != Link::Reads && edge.link != Link::Writes && edge.link != Link::Watches) continue;
            for (const auto e2 : g.out(edge.to))
                if (g.edges()[e2].link == Link::Address) pushUnique(out, seen, g.edges()[e2].to);
        }
    }
    return out;
}

std::vector<std::size_t> alarmsAbout(const Graph& g, std::string_view name) {
    std::vector<std::size_t> out;
    const std::string q = trimmed(name);
    if (q.empty()) return out;
    for (std::size_t i = 0; i < g.size(); ++i) {
        const Node& n = g.node(i);
        if (n.kind != Kind::Alarm) continue;
        bool about = containsWord(n.name, q);
        for (const auto e : g.out(i)) {
            if (about) break;
            const Edge& edge = g.edges()[e];
            if ((edge.link == Link::Watches || edge.link == Link::Reads) && g.node(edge.to).kind == Kind::Variable)
                about = pathMatches(edge.path.empty() ? g.node(edge.to).name : edge.path, q);
        }
        if (about) out.push_back(i);
    }
    return out;
}

std::string describe(const Graph& g, std::size_t i) {
    if (i >= g.size()) return {};
    const Node& n = g.node(i);
    return std::string(kindLabel(n.kind)) + " " + n.name + (n.detail.empty() ? std::string{} : " (" + n.detail + ")");
}

std::string edgeText(const Graph& g, const Edge& e) {
    if (e.from >= g.size() || e.to >= g.size()) return {};
    const Node& a = g.node(e.from);
    const Node& b = g.node(e.to);
    // Le chemin lu ; le nom seul quand le lien dit deja le genre (est du type T_Pompe, cite la ressource logo.png).
    std::string target = std::string(kindLabel(b.kind)) + " " + b.name;
    if ((e.link == Link::Reads || e.link == Link::Writes || e.link == Link::Watches) && !e.path.empty()) target = e.path;
    else if (e.link == Link::TypeOf || e.link == Link::Member || e.link == Link::Resource) target = b.name;
    return e.where + " : " + std::string(kindLabel(a.kind)) + " " + a.name + " " + std::string(linkLabel(e.link)) + " " + target;
}

} // namespace hmi::rel
