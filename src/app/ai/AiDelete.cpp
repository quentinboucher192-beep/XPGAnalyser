// =============================================================================
//  app/ai/AiDelete.cpp - voir AiDelete.hpp
// -----------------------------------------------------------------------------
//  Cinq temps, tous sur des copies du projet :
//    1. trouver ce qui est demande (le graphe des relations, par genre et nom) ;
//    2. ce qui part avec (rel::impactDetails : le contenu), ce qui en depend
//       (directes, indirectes, potentielles), ce qui est partage ;
//    3. la strategie (seul : refuse si quelque chose en depend ; remplacer ;
//       references) ;
//    4. le retrait, puis ce qui devient inutile (rel::orphans avant / apres) ;
//    5. la verification (absent, aucune reference restante, hmi::audit).
// =============================================================================
#include "AiDelete.hpp"

#include "../RenameDialog.hpp"            // rewriteHmiReferences : le passage de Renommer (remplacer)
#include "../../hmi/HmiAssets.hpp"        // renameResource (remplacer une ressource)
#include "../../hmi/HmiAudit.hpp"         // la verification globale
#include "../../hmi/HmiControls.hpp"      // listItems (la liste d'une barre de navigation, comme navItems la lit)
#include "../../hmi/HmiEdit.hpp"          // edit::remove (des objets, avec leurs enfants)
#include "../../hmi/HmiFolders.hpp"
#include "../../hmi/HmiMethods.hpp"       // renameTypeEverywhere (remplacer un type)
#include "../../hmi/HmiNavigation.hpp"    // parseMapZones, formatMapZones (les zones d'un plan)
#include "../../hmi/HmiRenameRefs.hpp"    // renameAlarmReferences (remplacer une alarme)
#include "../../hmi/HmiRoles.hpp"         // roles::nameOf
#include "../../hmi/HmiScript.hpp"        // forEachCode, renameFunctionEverywhere, scriptCalls
#include "../../hmi/HmiSymbols.hpp"       // isSymbolView
#include "../../hmi/HmiTemplates.hpp"     // headerOf, footerOf
#include "../../hmi/HmiTypes.hpp"         // types::usersOf, types::folderParent
#include "../../hmi/HmiWidgets.hpp"       // joinSemicolons (une liste reecrite, comme l'onglet Contenu)

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace app::ai::del {

namespace {

namespace rel = hmi::rel;
using RK = rel::Kind;

// ---- les textes ---------------------------------------------------------------------
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
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
std::string joined(const std::vector<std::string>& items, std::size_t max = 6) {
    std::string s;
    for (std::size_t i = 0; i < items.size() && i < max; ++i) s += (i ? ", " : "") + items[i];
    if (items.size() > max) s += ", \xE2\x80\xA6";
    return s;
}
std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const bool same = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1]));
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (same ? 0 : 1)});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}
bool readBool(std::string_view v) {
    const std::string l = lower(trimmed(v));
    return l == "true" || l == "1" || l == "vrai" || l == "oui";
}
std::string prop(const Operation& op, std::string_view key) {
    for (const auto& [k, v] : op.props)
        if (k == key) return trimmed(v);
    return {};
}

// Le texte NOMME-t-il `name` (sans casse) : un identifiant entier, pas un membre (x.Nom), hors des commentaires ;
// `quoted` : une chaine qui le contient tout entier compte aussi ('Pop_Pompe'). Un nom pointe (Vue.Objet) : sa racine,
// suivie de son point et du reste.
bool mentions(std::string_view text, std::string_view name, bool quoted) {
    if (name.empty() || text.size() < name.size()) return false;
    std::string_view root = name, rest;
    if (const auto dot = name.find('.'); dot != std::string_view::npos) {
        root = name.substr(0, dot);
        rest = name.substr(dot);
    }
    std::size_t i = 0;
    const auto starts = [&](std::size_t k, std::string_view w) { return text.substr(k, w.size()) == w; };
    while (i < text.size()) {
        const char c = text[i];
        if (starts(i, "(*")) {
            const auto e = text.find("*)", i + 2);
            i = e == std::string_view::npos ? text.size() : e + 2;
            continue;
        }
        if (starts(i, "//")) {
            const auto e = text.find('\n', i);
            i = e == std::string_view::npos ? text.size() : e + 1;
            continue;
        }
        if (c == '\'' || c == '"') {
            const auto e = text.find(c, i + 1);
            const std::string_view inner = text.substr(i + 1, (e == std::string_view::npos ? text.size() : e) - i - 1);
            if (quoted && iequal(trimmed(inner), name)) return true;
            i = e == std::string_view::npos ? text.size() : e + 1;
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < text.size() && identChar(text[j])) ++j;
            const bool member = i > 0 && text[i - 1] == '.';
            if (!member && iequal(text.substr(i, j - i), root)) {
                if (rest.empty()) return true;
                if (j + rest.size() <= text.size() && iequal(text.substr(j, rest.size()), rest)
                    && (j + rest.size() == text.size() || !identChar(text[j + rest.size()])))
                    return true;
            }
            i = j;
            continue;
        }
        ++i;
    }
    return false;
}

// Les trous d'un texte a trous ({Niveau:0.0}) qui nomment `name`, retires (les autres restent).
std::string dropHoles(std::string_view text, std::string_view name) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') {
            out += "{{";
            i += 2;
            continue;
        }
        if (c == '{') {
            const auto e = text.find('}', i + 1);
            if (e == std::string_view::npos) {
                out += text.substr(i);
                break;
            }
            const std::string_view hole = text.substr(i + 1, e - i - 1);
            if (!mentions(hole, name, false)) out += text.substr(i, e - i + 1);
            i = e + 1;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// La premiere ligne (1, 2...) d'un texte de plusieurs lignes qui NOMME `name` (mentions) ; 0 : un texte d'une ligne, ou
// nulle part.
int lineOf(std::string_view text, std::string_view name, bool quoted) {
    if (text.find('\n') == std::string_view::npos) return 0;
    int line = 1;
    for (std::size_t start = 0; start <= text.size(); ++line) {
        const auto nl = text.find('\n', start);
        if (mentions(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start), name, quoted)) return line;
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return 0;
}

// "A", "A ou B", "A, B ou C".
std::string joinedOr(const std::vector<std::string>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size(); ++i) s += (i == 0 ? "" : i + 1 == items.size() ? " ou " : ", ") + items[i];
    return s;
}

// Une barre de navigation AUTOMATIQUE (sa liste << views >> vide : les vues ordinaires du projet, navItems) ne nomme
// aucune vue : son arete vers une vue n'est pas une reference a retirer - elle suivra (une vue de moins).
bool automaticNav(const hmi::Project& p, const rel::Graph& g, const rel::Edge& e) {
    static const std::string tail = " . views (barre de navigation)";
    const rel::Node& src = g.node(e.from);
    if (src.kind != RK::Object || e.link != rel::Link::Opens || e.where.size() < tail.size()
        || e.where.compare(e.where.size() - tail.size(), tail.size(), tail) != 0)
        return false;
    const hmi::View* v = p.view(src.view);
    const hmi::Object* o = v ? v->object(src.id) : nullptr;
    return o && o->kind == hmi::Kind::NavBar && hmi::listItems(o->text("views")).empty();
}

const char* const kArrow = " \xE2\x86\x92 ";

// ---- les noms des genres ----------------------------------------------------------------------
struct Noun {
    const char* one;
    const char* many;
    bool        fem;
};
Noun nounOf(RK k) {
    switch (k) {
        case RK::View: return {"vue", "vues", true};
        case RK::Symbol: return {"symbole", "symboles", false};
        case RK::Popup: return {"popup", "popups", true};
        case RK::Template: return {"\xC3\xA9" "cran mod\xC3\xA8le", "\xC3\xA9" "crans mod\xC3\xA8les", false};
        case RK::Object: return {"objet", "objets", false};
        case RK::Type: return {"type", "types", false};
        case RK::Variable: return {"variable", "variables", true};
        case RK::Equipment: return {"\xC3\xA9quipement", "\xC3\xA9quipements", false};
        case RK::Alarm: return {"alarme", "alarmes", true};
        case RK::Script: return {"script", "scripts", false};
        case RK::Function: return {"fonction", "fonctions", true};
        case RK::Resource: return {"ressource", "ressources", true};
        case RK::Folder: return {"dossier", "dossiers", false};
        case RK::Recipe: return {"recette", "recettes", true};
    }
    return {"\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments", false};
}
// "4 variables", "1 type".
std::string counted(std::size_t n, RK k) {
    const Noun no = nounOf(k);
    return std::to_string(n) + " " + (n > 1 ? no.many : no.one);
}
// Un participe accorde : ("concern\xC3\xA9", 4 variables) -> "concern\xC3\xA9" "es".
std::string agreed(std::string_view participle, std::size_t n, bool fem) {
    return std::string(participle) + (fem ? "e" : "") + (n > 1 ? "s" : "");
}

RK kindOf(Genre g) noexcept {
    switch (g) {
        case Genre::Object:
        case Genre::Instance: return RK::Object;
        case Genre::View: return RK::View;
        case Genre::Popup: return RK::Popup;
        case Genre::Symbol: return RK::Symbol;
        case Genre::Template: return RK::Template;
        case Genre::Variable: return RK::Variable;
        case Genre::Type: return RK::Type;
        case Genre::Alarm: return RK::Alarm;
        case Genre::Script: return RK::Script;
        case Genre::Function: return RK::Function;
        case Genre::Equipment: return RK::Equipment;
        case Genre::Resource: return RK::Resource;
        case Genre::Folder: return RK::Folder;
    }
    return RK::Object;
}
bool viewLike(RK k) noexcept { return k == RK::View || k == RK::Popup || k == RK::Symbol || k == RK::Template; }

std::string display(const Item& it) {
    if (it.kind == RK::Object && !it.viewName.empty()) return it.viewName + "." + it.name;
    return it.name;
}

// La liste d'un dossier : -1 les variables IHM ; sinon un rang de hmi::fold::List ; nul : inconnue.
std::optional<int> folderList(std::string_view key) {
    std::string l = lower(trimmed(key));
    if (l == "variables" || l == "variable" || l == "variables ihm") return -1;
    if (l == "vue") l = "vues";
    if (l == "popup") l = "popups";
    if (l == "symbole") l = "symboles";
    if (l == "script") l = "scripts";
    if (l == "type") l = "types";
    if (l == "style") l = "styles";
    if (l == "ressource") l = "ressources";
    if (l == "modeles" || l == "modele" || l == "ecrans modeles") l = "ecrans_modeles";
    if (l == "entete" || l == "en-tetes") l = "entetes";
    if (l == "pied") l = "pieds";
    if (const auto f = hmi::fold::fromKey(l)) return static_cast<int>(*f);
    return std::nullopt;
}
std::string folderLabel(int list) {
    if (list < 0) return "Variables IHM";
    return std::string(hmi::fold::label(static_cast<hmi::fold::List>(list)));
}
std::optional<int> listOfLabel(std::string_view label) {
    if (label == "Variables IHM") return -1;
    for (int l = 0; l < static_cast<int>(hmi::fold::List::Count); ++l)
        if (hmi::fold::label(static_cast<hmi::fold::List>(l)) == label) return l;
    return std::nullopt;
}

Item itemOf(const rel::Graph& g, std::size_t n, const hmi::Project& p, std::string why) {
    const rel::Node& node = g.node(n);
    Item it;
    it.kind = node.kind;
    it.name = node.name;
    it.id = node.id;
    it.view = node.view;
    if (node.view != hmi::kNoId)
        if (const auto* v = p.view(node.view)) it.viewName = v->name;
    it.detail = node.kind == RK::Folder ? "dossier " + node.name + " (" + node.detail + ")" : rel::describe(g, n);
    it.why = std::move(why);
    return it;
}

// Ce noeud part-il avec une autre suppression de la proposition (lui, ou la vue qui le porte) ?
bool matches(const rel::Graph& g, std::size_t n, const hmi::Project& p, const std::vector<Item>& list) {
    if (list.empty()) return false;
    const rel::Node& node = g.node(n);
    std::string viewName;
    if (node.view != hmi::kNoId)
        if (const auto* v = p.view(node.view)) viewName = v->name;
    for (const auto& it : list) {
        if (it.id != hmi::kNoId && it.id == node.id) return true;
        if (it.kind == node.kind && iequal(it.name, node.name) && (it.kind != RK::Object || iequal(it.viewName, viewName))) return true;
        if (viewLike(it.kind) && !viewName.empty() && iequal(it.name, viewName)) return true;   // dans une vue qui part
    }
    return false;
}

// Les noms existants les plus proches (jamais un nom invente : ceux du graphe).
std::vector<std::string> nearNames(const rel::Graph& g, std::initializer_list<RK> kinds, std::string_view name) {
    std::vector<std::pair<std::size_t, std::string>> scored;
    for (const auto& n : g.nodes()) {
        if (std::find(kinds.begin(), kinds.end(), n.kind) == kinds.end()) continue;
        const std::size_t d = distance(lower(n.name), lower(name));
        if (d <= std::max<std::size_t>(2, name.size() / 3)) scored.emplace_back(d, n.name);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (const auto& [d, s] : scored)
        if (out.size() < 4 && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    return out;
}

// Ou est un texte de forEachCode, lisible ("Vue_A . Btn . action 2", "script Calcul").
std::string siteWhere(const hmi::CodeSite& s) {
    if (s.object && s.host) {
        const std::string base = s.host->name + " . " + s.object->name;
        if (!s.key.empty()) return base + " . " + s.key;
        if (s.action >= 0) return base + " . action " + std::to_string(s.action + 1);
        return base;
    }
    if (!s.object && s.host && s.action >= 0) return s.host->name + " . action " + std::to_string(s.action + 1);
    return s.where;
}

// Le dossier d'un Item de dossier : sa liste (le detail est "dossier <chemin> (<libelle>)").
std::optional<int> folderListOfItem(const Item& it) {
    const std::string head = "dossier " + it.name + " (";
    if (it.detail.rfind(head, 0) != 0 || it.detail.size() <= head.size()) return std::nullopt;
    return listOfLabel(std::string_view(it.detail).substr(head.size(), it.detail.size() - head.size() - 1));
}

// "du type", "de la variable", "de l'alarme".
std::string ofNoun(const std::string& noun) {
    if (noun.rfind("le ", 0) == 0) return "du " + noun.substr(3);
    return "de " + noun;
}
std::string titleOf(const Request& rq) {
    if (rq.names.size() == 1) return "Suppression " + ofNoun(genreNoun(rq.genre)) + " " + trimmed(rq.names.front());
    return "Suppression de " + counted(rq.names.size(), kindOf(rq.genre));
}

// Un element du projet est-il encore la (par son identifiant ; un dossier : par son chemin et sa liste) ?
bool present(const hmi::Project& p, const Item& it) {
    switch (it.kind) {
        case RK::Variable: return p.variableById(it.id) != nullptr;
        case RK::Type: return p.hmiType(it.id) != nullptr;
        case RK::Equipment: return p.equipment(it.id) != nullptr;
        case RK::Resource: return p.resource(it.id) != nullptr;
        case RK::View:
        case RK::Symbol:
        case RK::Popup:
        case RK::Template: return p.view(it.id) != nullptr;
        case RK::Object: {
            const hmi::View* v = p.view(it.view);
            return v && v->object(it.id);
        }
        case RK::Alarm:
            if (it.id == hmi::kNoId) return false;
            if (p.alarm(it.id)) return true;
            for (const auto& v : p.views)
                for (const auto& a : v.alarms)
                    if (a.id == it.id) return true;
            return false;
        case RK::Script: return it.id != hmi::kNoId && p.script(it.id) != nullptr;
        case RK::Function:
            if (it.id == hmi::kNoId) return false;
            for (const auto& f : p.programs.functions)
                if (f.id == it.id) return true;
            for (const auto& v : p.views)
                for (const auto& f : v.functions)
                    if (f.id == it.id) return true;
            for (const auto& t : p.programs.types)
                for (const auto& m : t.methods)
                    if (m.id == it.id) return true;
            return false;
        case RK::Folder: {
            const auto list = folderListOfItem(it);
            if (!list) return false;
            if (*list < 0) {
                for (const auto& f : hmi::types::allFolders(p))
                    if (iequal(f, it.name)) return true;
                return false;
            }
            for (const auto& f : hmi::fold::allFolders(p, static_cast<hmi::fold::List>(*list)))
                if (hmi::fold::sameFolder(f, it.name)) return true;
            return false;
        }
        case RK::Recipe: return p.recipe(it.id) != nullptr;
    }
    return false;
}

// ---- le retrait des elements (par identifiant) -------------------------------------------------
void eraseItems(hmi::Project& p, const std::vector<Item>& items) {
    std::set<hmi::Id> views, vars, types, equips, resources, alarms, scripts, functions;
    std::map<hmi::Id, std::vector<hmi::Id>> objects;   // vue -> objets
    std::vector<const Item*> folders;
    for (const auto& it : items) {
        switch (it.kind) {
            case RK::View:
            case RK::Symbol:
            case RK::Popup:
            case RK::Template: views.insert(it.id); break;
            case RK::Object: objects[it.view].push_back(it.id); break;
            case RK::Variable: vars.insert(it.id); break;
            case RK::Type: types.insert(it.id); break;
            case RK::Equipment: equips.insert(it.id); break;
            case RK::Resource: resources.insert(it.id); break;
            case RK::Alarm:
                if (it.id != hmi::kNoId) alarms.insert(it.id);
                break;
            case RK::Script:
                if (it.id != hmi::kNoId) scripts.insert(it.id);
                break;
            case RK::Function:
                if (it.id != hmi::kNoId) functions.insert(it.id);
                break;
            case RK::Folder: folders.push_back(&it); break;
            case RK::Recipe: break;
        }
    }
    // Les objets (avec leurs enfants), dans les vues qui restent.
    for (auto& [vid, ids] : objects) {
        if (views.count(vid)) continue;
        if (hmi::View* v = p.view(vid)) hmi::edit::remove(*v, ids);
    }
    // Les vues - et les popups de leurs symboles (View::ownerSymbol), qui n'existent pas sans lui.
    if (!views.empty()) {
        for (const auto& v : p.views)
            if (v.ownerSymbol != hmi::kNoId && views.count(v.ownerSymbol)) views.insert(v.id);
        std::erase_if(p.views, [&](const hmi::View& v) { return views.count(v.id) > 0; });
        if (views.count(p.config.startView)) {
            p.config.startView = hmi::kNoId;
            for (const auto& v : p.views)
                if (v.role == "vue") {
                    p.config.startView = v.id;
                    break;
                }
        }
        for (auto& gr : p.security.groups)
            if (views.count(gr.startView)) gr.startView = hmi::kNoId;
    }
    std::erase_if(p.programs.variables, [&](const hmi::Variable& v) { return vars.count(v.id) > 0; });
    std::erase_if(p.programs.types, [&](const hmi::HmiType& t) { return types.count(t.id) > 0; });
    std::erase_if(p.equipments, [&](const hmi::Equipment& e) { return equips.count(e.id) > 0; });
    std::erase_if(p.assets.resources, [&](const hmi::Resource& r) { return resources.count(r.id) > 0; });
    std::erase_if(p.alarms, [&](const hmi::AlarmDef& a) { return alarms.count(a.id) > 0; });
    std::erase_if(p.programs.scripts, [&](const hmi::Script& s) { return scripts.count(s.id) > 0; });
    std::erase_if(p.programs.functions, [&](const hmi::HmiFunction& f) { return functions.count(f.id) > 0; });
    for (auto& v : p.views) {
        std::erase_if(v.alarms, [&](const hmi::AlarmDef& a) { return alarms.count(a.id) > 0; });
        std::erase_if(v.scripts, [&](const hmi::Script& s) { return scripts.count(s.id) > 0; });
        std::erase_if(v.functions, [&](const hmi::HmiFunction& f) { return functions.count(f.id) > 0; });
    }
    for (auto& t : p.programs.types) std::erase_if(t.methods, [&](const hmi::HmiFunction& f) { return functions.count(f.id) > 0; });
    // Les dossiers : leurs elements et leurs sous-dossiers montent d'un cran (comme Supprimer le dossier).
    for (const Item* f : folders) {
        const auto list = folderListOfItem(*f);
        if (!list) continue;
        if (*list >= 0) {
            (void)hmi::fold::removeFolder(p, static_cast<hmi::fold::List>(*list), f->name);
            continue;
        }
        // Les variables : le dossier parti, ses sous-dossiers montent d'un cran (Ligne/Pompes -> Pompes).
        const std::string path = f->name, parent = hmi::types::folderParent(path);
        const auto inside = [&](const std::string& x) {
            return iequal(x, path) || (x.size() > path.size() && x[path.size()] == '/' && iequal(std::string_view(x).substr(0, path.size()), path));
        };
        const auto moved = [&](const std::string& x) {
            if (iequal(x, path)) return parent;
            return parent.empty() ? x.substr(path.size() + 1) : parent + x.substr(path.size());
        };
        std::erase_if(p.programs.folders, [&](const std::string& x) { return iequal(x, path); });
        for (auto& x : p.programs.folders)
            if (inside(x)) x = moved(x);
        for (auto& v : p.programs.variables)
            if (inside(v.folder)) v.folder = moved(v.folder);
    }
}

// ---- le planificateur ------------------------------------------------------------------------------
class Planner {
public:
    Planner(const hmi::Project& p, const rel::Graph& g, const Request& rq, const Context& ctx, Analysis& a)
        : p_(p), g_(g), rq_(rq), ctx_(ctx), a_(a), gone_(g.size(), 0) {}

    // 1. ce qui est demande.
    bool resolve() {
        a_.title = titleOf(rq_);
        for (const auto& raw : rq_.names) {
            const std::string name = trimmed(raw);
            if (name.empty()) continue;
            if (kept(name)) {
                a_.warnings.push_back("\xC2\xAB " + name + " \xC2\xBB est exclu de la suppression : il reste.");
                continue;
            }
            const auto n = find(name);
            if (!n) continue;   // l'erreur est dite
            if (gone_[*n]) continue;
            gone_[*n] = 1;
            targets_.push_back(*n);
            a_.targets.push_back(itemOf(g_, *n, p_, "demand\xC3\xA9"));
        }
        if (a_.targets.empty() && a_.errors.empty()) a_.errors.push_back(a_.title + " : rien \xC3\xA0 supprimer (tout est exclu).");
        return a_.errors.empty();
    }

    // 2. ce qui part avec (le contenu), ce qui remonte (un dossier).
    void closure() {
        a_.removed = a_.targets;
        for (const auto t : targets_) {
            const bool folder = g_.node(t).kind == RK::Folder;
            for (const auto& h : rel::impactDetails(g_, t)) {
                const rel::Edge& e = g_.edges()[h.edge];
                if (folder && h.how == rel::Hit::How::Moved) {
                    a_.moved.push_back(itemOf(g_, h.node, p_, "remonte d'un cran (dans " + hmi::types::folderParent(g_.node(t).name) + ")"));
                    continue;
                }
                if (h.how != rel::Hit::How::Removed || gone_[h.node]) continue;
                gone_[h.node] = 1;
                Item it = itemOf(g_, h.node, p_, "contenu par " + g_.node(e.from).name);
                if (kept(it.name) || kept(display(it)))
                    a_.warnings.push_back("\xC2\xAB " + display(it) + " \xC2\xBB part avec " + g_.node(e.from).name + " (son contenu) : l'exclure n'est pas possible.");
                a_.removed.push_back(std::move(it));
            }
            // Les enfants d'un groupe (un objet) partent avec lui.
            if (g_.node(t).kind == RK::Object)
                if (const hmi::View* v = p_.view(g_.node(t).view))
                    for (const auto child : v->descendantsOf(g_.node(t).id))
                        if (const auto cn = g_.byId(child); cn && !gone_[*cn]) {
                            gone_[*cn] = 1;
                            a_.removed.push_back(itemOf(g_, *cn, p_, "dans le groupe " + g_.node(t).name));
                        }
        }
    }

    // 3. ce qui en depend, ce qui est partage.
    void dependents() {
        for (std::size_t n = 0; n < g_.size(); ++n) {
            if (!gone_[n]) continue;
            for (const auto e : g_.in(n)) {
                const rel::Edge& edge = g_.edges()[e];
                if (edge.link == rel::Link::Contains) continue;
                const std::size_t src = edge.from;
                if (gone_[src] || g_.node(src).kind == RK::Folder || matches(g_, src, p_, ctx_.also)) continue;
                // Une liste a joker de dossier (Ligne/*) ne le nomme pas : elle suivra (une ligne de moins) - pas un blocage.
                if (edge.where.find("(dossier ") != std::string::npos && edge.where.find("/*)") != std::string::npos) {
                    if (jokerNodes_.insert(src).second) a_.indirect.push_back(itemOf(g_, src, p_, rel::edgeText(g_, edge) + " (joker de dossier : la liste suivra)"));
                    continue;
                }
                // 1.22.7 : une barre de navigation automatique (sans liste) ne la nomme pas : elle suivra.
                if (automaticNav(p_, g_, edge)) {
                    if (jokerNodes_.insert(src).second)
                        a_.indirect.push_back(itemOf(g_, src, p_, rel::edgeText(g_, edge) + " (barre automatique : elle suivra, une vue de moins)"));
                    continue;
                }
                directEdges_.push_back(e);
                if (directNodes_.insert(src).second) a_.direct.push_back(itemOf(g_, src, p_, rel::edgeText(g_, edge)));
            }
        }
        // Les emplois que le graphe note sans arete (une fonction appelee par Vue.F(...), un type employe ailleurs) :
        // directs aussi ; l'historique : un avertissement. La vue de demarrage, l'accueil d'un groupe, un ecran du poste :
        // regles (et dits) par settle.
        for (const auto t : targets_) {
            const rel::Node& node = g_.node(t);
            for (const auto& note : node.notes) {
                if (note.rfind("appel\xC3\xA9", 0) == 0 || note.rfind("employ\xC3\xA9", 0) == 0) {
                    notedUsers_.push_back(node.name + " : " + note);
                    continue;
                }
                if (note.rfind("archiv", 0) == 0)
                    a_.warnings.push_back(node.name + " est archiv\xC3\xA9" "e par l'historique : l'historique ne la gardera plus.");
            }
            // Le type : la regle de Types IHM (types::usersOf), en plus du graphe.
            // (seulement si le graphe ne voit aucun emploi du tout : sinon il les a dits, ou ils partent avec)
            const bool seenByGraph = std::any_of(g_.in(t).begin(), g_.in(t).end(), [&](std::size_t e) {
                return g_.edges()[e].link == rel::Link::TypeOf || g_.edges()[e].link == rel::Link::Member;
            });
            if (node.kind == RK::Type && !seenByGraph
                && std::none_of(node.notes.begin(), node.notes.end(), [](const std::string& x) { return x.rfind("employ\xC3\xA9", 0) == 0; }))
                if (const auto* t2 = p_.hmiType(node.id))
                    for (const auto& u : hmi::types::usersOf(p_, hmi::fullName(*t2))) notedUsers_.push_back(node.name + " : employ\xC3\xA9 par " + u);
        }
        // De proche en proche (sans ce qui part ni les dossiers) ; les vues qui montrent des objets touches.
        std::set<std::size_t> seen(directNodes_.begin(), directNodes_.end());
        seen.insert(jokerNodes_.begin(), jokerNodes_.end());
        for (const auto t : targets_) {
            if (g_.node(t).kind == RK::Folder) continue;
            for (const auto& h : rel::impactDetails(g_, t)) {
                if (h.how != rel::Hit::How::Affected || gone_[h.node] || seen.count(h.node)) continue;
                if (g_.node(h.node).kind == RK::Folder || matches(g_, h.node, p_, ctx_.also)) continue;
                seen.insert(h.node);
                a_.indirect.push_back(itemOf(g_, h.node, p_, rel::edgeText(g_, g_.edges()[h.edge])));
            }
        }
        // Les equipements des variables concernees (celles qui partent, qui en dependent).
        std::set<std::size_t> eqSeen;
        const auto equipmentsOf = [&](std::size_t v) {
            if (g_.node(v).kind != RK::Variable) return;
            for (const auto e : g_.out(v)) {
                const rel::Edge& edge = g_.edges()[e];
                if (edge.link != rel::Link::Address || gone_[edge.to] || !eqSeen.insert(edge.to).second) continue;
                a_.equipments.push_back(itemOf(g_, edge.to, p_, "li\xC3\xA9 \xC3\xA0 la variable " + g_.node(v).name));
            }
        };
        for (std::size_t n = 0; n < g_.size(); ++n)
            if (gone_[n] || seen.count(n)) equipmentsOf(n);
        // Ce que ce qui part emploie, et qui sert aussi ailleurs : garde (un script appele par la vue ET par une autre).
        std::set<std::size_t> sharedSeen;
        for (std::size_t n = 0; n < g_.size(); ++n) {
            if (!gone_[n]) continue;
            for (const auto e : g_.out(n)) {
                const rel::Edge& edge = g_.edges()[e];
                if (edge.link == rel::Link::Contains || gone_[edge.to] || sharedSeen.count(edge.to)) continue;
                std::vector<std::string> others;
                for (const auto e2 : g_.in(edge.to)) {
                    const rel::Edge& in = g_.edges()[e2];
                    if (in.link == rel::Link::Contains || gone_[in.from] || g_.node(in.from).kind == RK::Folder) continue;
                    const std::string who = g_.node(in.from).name;
                    if (std::find(others.begin(), others.end(), who) == others.end()) others.push_back(who);
                }
                if (others.empty()) continue;
                sharedSeen.insert(edge.to);
                a_.shared.push_back(itemOf(g_, edge.to, p_, "sert aussi \xC3\xA0 " + joined(others, 3) + " : gard\xC3\xA9"));
            }
        }
        potentials(seen);
    }

    // 3 bis. les dependances potentielles : un texte qui NOMME l'element sans que le logiciel le lise (a verifier).
    void potentials(const std::set<std::size_t>& known) {
        std::set<hmi::Id> skip;   // ce qui part, ce qui en depend deja
        std::vector<std::string> skipWhere;   // les alarmes et les recettes deja la ("alarme Niveau_Haut", "recette R")
        for (std::size_t n = 0; n < g_.size(); ++n) {
            if (!(gone_[n] || known.count(n))) continue;
            if (g_.node(n).id != hmi::kNoId) skip.insert(g_.node(n).id);
            if (g_.node(n).kind == RK::Alarm) skipWhere.push_back(lower("alarme " + g_.node(n).name));
            if (g_.node(n).kind == RK::Recipe) skipWhere.push_back(lower("recette " + g_.node(n).name));
        }
        std::vector<std::pair<std::string, bool>> names;   // le nom, une chaine qui le nomme compte
        for (const auto& t : a_.targets) {
            if (t.kind == RK::Folder) continue;
            if (t.kind == RK::Object) names.emplace_back(t.viewName + "." + t.name, false);
            else names.emplace_back(t.name, viewLike(t.kind) || t.kind == RK::Script || t.kind == RK::Resource || t.kind == RK::Alarm);
        }
        if (names.empty()) return;
        std::set<std::string> where;
        const auto add = [&](RK kind, std::string at, hmi::Id id, hmi::Id view, const std::string& name) {
            if (a_.potential.size() >= 40 || !where.insert(at).second) return;
            Item it;
            it.kind = kind;
            it.name = at;
            it.id = id;
            it.view = view;
            it.detail = at;
            it.why = "nomme " + name + " (par le nom seulement : \xC3\xA0 v\xC3\xA9rifier)";
            a_.potential.push_back(std::move(it));
        };
        hmi::forEachCode(p_, [&](const std::string& text, hmi::CodeForm, const hmi::CodeSite& s) {
            if (s.again || text.empty()) return;
            const hmi::View* host = s.host ? s.host : s.view;
            if (s.object ? skip.count(s.object->id) > 0 : host && skip.count(host->id) > 0) return;
            if (s.script != hmi::kNoId && skip.count(s.script)) return;
            if (s.function != hmi::kNoId && skip.count(s.function)) return;
            if (!s.object && !host) {
                const std::string w = lower(s.where);
                for (const auto& k : skipWhere)
                    if (w.rfind(k, 0) == 0 && (w.size() == k.size() || !identChar(w[k.size()]))) return;
            }
            for (const auto& [name, quoted] : names)
                if (mentions(text, name, quoted)) {
                    const RK kind = s.object ? RK::Object : s.script != hmi::kNoId ? RK::Script : s.function != hmi::kNoId ? RK::Function
                                  : host ? RK::View : RK::Alarm;
                    add(kind, siteWhere(s), s.object ? s.object->id : s.script != hmi::kNoId ? s.script : s.function, host ? host->id : hmi::kNoId, name);
                    break;
                }
        });
        // Les actions qui le visent par son nom (acquitter une alarme, jouer un GIF...) sans arete du graphe.
        for (const auto& v : p_.views) {
            if (skip.count(v.id)) continue;
            const auto scan = [&](const std::vector<hmi::Action>& list, const std::string& base, hmi::Id obj) {
                for (std::size_t k = 0; k < list.size(); ++k)
                    for (const auto& [name, quoted] : names)
                        if (iequal(hmi::roles::nameOf(list[k].target), name)) {
                            add(obj != hmi::kNoId ? RK::Object : RK::View, base + " . action " + std::to_string(k + 1) + " (" + std::string(hmi::operationLabel(list[k].operation)) + ")",
                                obj, v.id, name);
                            break;
                        }
            };
            scan(v.actions, v.name, hmi::kNoId);
            for (const auto& o : v.objects)
                if (!skip.count(o.id)) scan(o.actions, v.name + " . " + o.name, o.id);
        }
    }

    // 4. la strategie : seul (refuse si quelque chose en depend), remplacer, references.
    bool strategyCheck() {
        if (rq_.strategy == Strategy::Only) {
            if (directEdges_.empty() && notedUsers_.empty()) return true;
            std::vector<std::string> list;
            for (const auto& d : a_.direct) list.push_back(std::string(nounOf(d.kind).one) + " " + display(d) + " (" + d.why + ")");
            for (const auto& u : notedUsers_) list.push_back(u);
            const std::size_t n = a_.direct.size() + notedUsers_.size();
            a_.errors.push_back(a_.title + " : impossible seule - " + std::to_string(n) + (n > 1 ? " \xC3\xA9l\xC3\xA9ments en d\xC3\xA9pendent" : " \xC3\xA9l\xC3\xA9ment en d\xC3\xA9pend")
                                + " directement : " + joined(list, 6) + ". " + advice());
            return false;
        }
        if (rq_.strategy == Strategy::Replace) return replacementCheck();
        // References : ce qui ne se retire pas est dit par apply (sur la copie).
        if (!notedUsers_.empty()) {
            a_.errors.push_back(a_.title + " : " + joined(notedUsers_, 4) + " - ces emplois sont du code : on ne le r\xC3\xA9\xC3\xA9" "crit pas sans savoir. "
                                + replaceHint(g_.node(targets_.front())) + ", ou supprime d'abord ce qui l'emploie.");
            return false;
        }
        return true;
    }

    // 5. sur la copie : la strategie, puis le retrait. Seul et references : ce qui n'est pas une reference se regle (settle).
    bool apply(hmi::Project& w) {
        if (rq_.strategy == Strategy::Replace) {
            replaceIn(w);
        } else {
            if (rq_.strategy == Strategy::References) referencesIn(w);
            settle(w);
        }
        if (!a_.errors.empty()) return false;
        eraseItems(w, a_.removed);
        return true;
    }

    // 6. ce qui devient inutile (avant / apres : rel::orphans) ; supprime aussi avec << inutiles >>.
    void cascade(hmi::Project& w) {
        std::set<std::pair<RK, std::string>> before;
        for (const auto n : rel::orphans(g_)) before.emplace(g_.node(n).kind, lower(g_.node(n).name));
        std::set<std::pair<RK, std::string>> reported;
        for (int round = 0; round < 8; ++round) {
            const rel::Graph g2 = rel::build(w);
            std::vector<Item> drop;
            for (const auto n : rel::orphans(g2)) {
                const rel::Node& node = g2.node(n);
                const auto key = std::make_pair(node.kind, lower(node.name));
                if (before.count(key) || reported.count(key)) continue;
                reported.insert(key);
                const std::string reason = rel::orphanReason(g2, n);
                const bool eligible = (node.kind == RK::Popup || (node.kind == RK::Script && node.view == hmi::kNoId) || node.kind == RK::Resource
                                       || (node.kind == RK::Function && node.view == hmi::kNoId && !isMethod(w, node.id)))
                                   && node.id != hmi::kNoId;
                Item it = itemOf(g2, n, w, {});
                if (rq_.useless && eligible && !kept(node.name) && !matches(g2, n, w, ctx_.also)) {
                    it.why = "devenu inutile (" + reason + ")";
                    drop.push_back(std::move(it));
                    continue;
                }
                it.why = reason + (eligible ? (kept(node.name) ? " - exclu : gard\xC3\xA9" : " - gard\xC3\xA9 (strat\xC3\xA9gie inutiles : supprim\xC3\xA9" + std::string(nounOf(node.kind).fem ? "e" : "") + " aussi)")
                                            : node.kind == RK::Variable ? " - gard\xC3\xA9" "e (une variable ne part jamais d'office)"
                                            : node.kind == RK::Symbol || node.kind == RK::Type ? " - gard\xC3\xA9 (une d\xC3\xA9" "finition ne part jamais d'office)"
                                                                                                : " - gard\xC3\xA9");
                a_.orphans.push_back(std::move(it));
            }
            if (drop.empty()) break;
            eraseItems(w, drop);
            for (auto& d : drop) a_.removed.push_back(std::move(d));
        }
    }

    // Les noms des elements dont la strategie a change une reference (la verification globale les regarde aussi).
    [[nodiscard]] std::vector<std::string> touched() const {
        std::vector<std::string> out;
        for (const auto& d : a_.direct) out.push_back(d.name);
        return out;
    }

private:
    [[nodiscard]] bool kept(std::string_view name) const {
        return std::any_of(rq_.keep.begin(), rq_.keep.end(), [&](const std::string& k) { return iequal(trimmed(k), name); });
    }
    static bool isMethod(const hmi::Project& p, hmi::Id id) {
        for (const auto& t : p.programs.types)
            for (const auto& m : t.methods)
                if (m.id == id) return true;
        return false;
    }

    std::string notFound(std::string_view name, std::initializer_list<RK> kinds) const {
        std::string msg = "\xC2\xAB " + std::string(name) + " \xC2\xBB : " + genreNoun(rq_.genre) + " introuvable dans le projet";
        const auto near = nearNames(g_, kinds, name);
        if (!near.empty()) msg += " (les plus proches : " + joined(near, 4) + ")";
        return msg + ". Seuls les \xC3\xA9l\xC3\xA9ments existants se suppriment (aucun nom invent\xC3\xA9).";
    }
    std::string realGenre(std::size_t n) const {
        switch (g_.node(n).kind) {
            case RK::Symbol: return "symbole";
            case RK::Popup: return "popup";
            case RK::Template: return "modele";
            case RK::View: return "vue";
            default: return std::string(nounOf(g_.node(n).kind).one);
        }
    }

    std::optional<std::size_t> find(const std::string& name) {
        switch (rq_.genre) {
            case Genre::Object:
            case Genre::Instance: {
                const hmi::View* v = rq_.view.empty() ? nullptr : p_.viewByName(rq_.view);
                if (!v) {
                    a_.errors.push_back(rq_.view.empty() ? "\xC2\xAB " + name + " \xC2\xBB : aucune vue ouverte (\xC2\xAB vue \xC2\xBB : la vue de l'objet)."
                                                        : "La vue \xC2\xAB " + rq_.view + " \xC2\xBB n'existe pas.");
                    return std::nullopt;
                }
                const hmi::Object* o = nullptr;
                for (const auto& x : v->objects)
                    if (x.name == name) o = &x;
                for (const auto& x : v->objects)
                    if (!o && iequal(x.name, name)) o = &x;
                if (!o) {
                    a_.errors.push_back("\xC2\xAB " + name + " \xC2\xBB n'est pas dans la vue " + v->name + ".");
                    return std::nullopt;
                }
                if (rq_.genre == Genre::Instance && o->kind != hmi::Kind::SymbolInstance) {
                    a_.errors.push_back("\xC2\xAB " + o->name + " \xC2\xBB n'est pas une instance de symbole (c'est " + std::string(hmi::kindLabel(o->kind))
                                        + ") : genre objet.");
                    return std::nullopt;
                }
                const auto n = g_.byId(o->id);
                if (!n) a_.errors.push_back("\xC2\xAB " + o->name + " \xC2\xBB : introuvable dans le graphe du projet.");
                return n;
            }
            case Genre::Folder: {
                std::optional<int> list;
                if (!rq_.list.empty()) {
                    list = folderList(rq_.list);
                    if (!list) {
                        a_.errors.push_back("Dossier \xC2\xAB " + name + " \xC2\xBB : liste \xC2\xAB " + rq_.list + " \xC2\xBB inconnue (vues, popups, symboles, ecrans_modeles, "
                                            "entetes, pieds, scripts, types, styles, ressources, variables).");
                        return std::nullopt;
                    }
                }
                std::vector<std::size_t> found;
                for (std::size_t n = 0; n < g_.size(); ++n) {
                    const rel::Node& node = g_.node(n);
                    if (node.kind != RK::Folder || !iequal(node.name, name)) continue;
                    if (list && node.detail != folderLabel(*list)) continue;
                    found.push_back(n);
                }
                if (found.empty()) {
                    a_.errors.push_back(notFound(name, {RK::Folder}));
                    return std::nullopt;
                }
                if (found.size() > 1) {
                    std::vector<std::string> lists;
                    for (const auto n : found) lists.push_back(g_.node(n).detail);
                    a_.errors.push_back("Le dossier \xC2\xAB " + name + " \xC2\xBB existe dans plusieurs listes (" + joined(lists) + ") : pr\xC3\xA9" "cise \xC2\xAB liste \xC2\xBB.");
                    return std::nullopt;
                }
                return found.front();
            }
            case Genre::View:
            case Genre::Popup:
            case Genre::Symbol:
            case Genre::Template: {
                const auto n = g_.find(RK::View, name);
                if (!n) {
                    a_.errors.push_back(notFound(name, {RK::View, RK::Popup, RK::Symbol, RK::Template}));
                    return std::nullopt;
                }
                const RK k = g_.node(*n).kind;
                const bool ok = rq_.genre == Genre::View ? k != RK::Symbol
                              : rq_.genre == Genre::Popup ? k == RK::Popup
                              : rq_.genre == Genre::Symbol ? k == RK::Symbol
                                                           : k == RK::Template;
                if (!ok) {
                    std::string msg = "\xC2\xAB " + g_.node(*n).name + " \xC2\xBB est " + (k == RK::Symbol ? std::string("un symbole (sa d\xC3\xA9" "finition)") : "un(e) " + realGenre(*n))
                                    + ", pas " + genreNoun(rq_.genre) + " demand\xC3\xA9" "(e) : genre " + realGenre(*n) + ".";
                    if (k == RK::Symbol)
                        msg += " Supprimer le symbole touche toutes ses instances ; une seule instance : genre instance (le symbole reste).";
                    a_.errors.push_back(msg);
                    return std::nullopt;
                }
                return n;
            }
            default: {
                const RK k = kindOf(rq_.genre);
                const auto n = g_.find(k, name);
                if (!n) {
                    a_.errors.push_back(notFound(name, {k}));
                    return std::nullopt;
                }
                if (k == RK::Alarm && g_.node(*n).id == hmi::kNoId) {
                    a_.errors.push_back("\xC2\xAB " + g_.node(*n).name + " \xC2\xBB est une alarme d'objet (n\xC3\xA9" "e de sa biblioth\xC3\xA8que ou de son symbole) : "
                                        "elle se d\xC3\xA9" "coche dans l'objet, elle ne se supprime pas.");
                    return std::nullopt;
                }
                return n;
            }
        }
    }

    // Que faire, selon le genre (le refus de << seul >>).
    [[nodiscard]] std::string advice() const {
        switch (rq_.genre) {
            case Genre::Type:
                return "Un type employ\xC3\xA9 ne se supprime pas en laissant des variables invalides. " + replaceHint(g_.node(targets_.front()))
                       + ", ou supprime d'abord ce qui l'emploie.";
            case Genre::Symbol:
                return "Ses instances montreraient \xC2\xAB Symbole introuvable \xC2\xBB : strat\xC3\xA9gie references (ses instances sont retir\xC3\xA9" "es), "
                       "remplacer (par un autre symbole existant), ou supprime seulement une instance (genre instance).";
            case Genre::Variable:
                return "Strat\xC3\xA9gie references (les propri\xC3\xA9t\xC3\xA9s d'objets qui la lisent sont vid\xC3\xA9" "es, les actions retir\xC3\xA9" "es ; du code "
                       "qui la lit est refus\xC3\xA9), remplacer (par une autre variable existante), ou ajoute ce qui en d\xC3\xA9pend \xC3\xA0 la suppression.";
            case Genre::View:
            case Genre::Popup:
            case Genre::Template:
                return "Strat\xC3\xA9gie references (ce qui la cite est modifi\xC3\xA9 et dit : les actions qui l'ouvrent retir\xC3\xA9" "es, elle sort des "
                       "barres de navigation et des plans, les vues filles remontent, l'en-t\xC3\xAAte et le pied suivent ; du code qui la nomme "
                       "reste refus\xC3\xA9), remplacer (par une autre vue existante : les liens la suivent), ou ajoute ces \xC3\xA9l\xC3\xA9ments \xC3\xA0 la "
                       "suppression.";
            case Genre::Equipment:
                return "Strat\xC3\xA9gie references (ses variables sont d\xC3\xA9li\xC3\xA9" "es et gard\xC3\xA9" "es) ou remplacer (par un autre \xC3\xA9quipement existant).";
            case Genre::Script:
            case Genre::Function:
                return "Strat\xC3\xA9gie references (les actions qui l'appellent sont retir\xC3\xA9" "es ; du code qui l'appelle est refus\xC3\xA9) ou remplacer "
                       "(par un autre \xC3\xA9l\xC3\xA9ment existant du m\xC3\xAAme genre).";
            case Genre::Resource:
                return "Strat\xC3\xA9gie references (les propri\xC3\xA9t\xC3\xA9s qui la citent sont vid\xC3\xA9" "es) ou remplacer (par une autre ressource existante).";
            default: return "Choisis une autre strat\xC3\xA9gie (references, remplacer) ou ajoute ces \xC3\xA9l\xC3\xA9ments \xC3\xA0 la suppression.";
        }
    }

    // ---- remplacer ----------------------------------------------------------------------------
    std::optional<std::size_t> replacementNode_;
    bool replacementCheck() {
        const std::string name = trimmed(rq_.replacement);
        if (rq_.genre == Genre::Object || rq_.genre == Genre::Instance || rq_.genre == Genre::Folder) {
            a_.errors.push_back(a_.title + " : remplacer ne s'applique pas \xC3\xA0 " + genreNoun(rq_.genre) + ".");
            return false;
        }
        const RK k = kindOf(rq_.genre);
        std::optional<std::size_t> n = viewLike(k) ? g_.find(RK::View, name) : g_.find(k, name);
        if (n && viewLike(k)) {
            const RK got = g_.node(*n).kind;
            if ((k == RK::Symbol) != (got == RK::Symbol) || (k == RK::Popup && got != RK::Popup) || (k == RK::Template && got != RK::Template)) n.reset();
        }
        if (!n) {
            std::string msg = a_.title + " : le rempla\xC3\xA7" "ant \xC2\xAB " + name + " \xC2\xBB n'existe pas - il doit \xC3\xAAtre "
                            "un \xC3\xA9l\xC3\xA9ment EXISTANT du m\xC3\xAAme genre (" + std::string(nounOf(k).one) + "), jamais un nom invent\xC3\xA9";
            const auto near = nearNames(g_, {k}, name);
            if (!near.empty()) msg += " (les plus proches : " + joined(near, 4) + ")";
            a_.errors.push_back(msg + ".");
            return false;
        }
        if (gone_[*n] || matches(g_, *n, p_, ctx_.also)) {
            a_.errors.push_back(a_.title + " : le rempla\xC3\xA7" "ant \xC2\xAB " + g_.node(*n).name + " \xC2\xBB part aussi.");
            return false;
        }
        if (g_.node(*n).kind == RK::Alarm && g_.node(*n).id == hmi::kNoId) {
            a_.errors.push_back(a_.title + " : le rempla\xC3\xA7" "ant \xC2\xAB " + g_.node(*n).name + " \xC2\xBB est une alarme d'objet.");
            return false;
        }
        replacementNode_ = n;
        // Deux types : les membres qui manquent au remplacant (ce qui les lit sera signale par la verification).
        if (k == RK::Type)
            if (const auto* to = p_.hmiType(g_.node(*n).id))
                for (const auto t : targets_)
                    if (const auto* from = p_.hmiType(g_.node(t).id)) {
                        if (from->ns != to->ns) {
                            a_.errors.push_back(a_.title + " : " + hmi::fullName(*from) + " et " + hmi::fullName(*to)
                                                + " sont dans des espaces de noms diff\xC3\xA9rents : remplace-le \xC3\xA0 la main.");
                            return false;
                        }
                        std::vector<std::string> missing;
                        for (const auto& m : from->members)
                            if (std::none_of(to->members.begin(), to->members.end(), [&](const hmi::TypeMember& x) { return iequal(x.name, m.name); }))
                                missing.push_back(m.name);
                        if (!missing.empty())
                            a_.warnings.push_back(hmi::fullName(*to) + " n'a pas les membres " + joined(missing) + " de " + hmi::fullName(*from)
                                                  + " : ce qui les lit est v\xC3\xA9rifi\xC3\xA9 (et refus\xC3\xA9 s'il casse).");
                    }
        return true;
    }

    void replaceIn(hmi::Project& w) {
        if (!replacementNode_) return;
        const rel::Node& rep = g_.node(*replacementNode_);
        for (const auto t : targets_) {
            const rel::Node& node = g_.node(t);
            const std::size_t users = std::count_if(directEdges_.begin(), directEdges_.end(), [&](std::size_t e) { return g_.edges()[e].to == t; });
            switch (node.kind) {
                case RK::Variable: {
                    for (const auto& c : rewriteHmiReferences(w, false, node.name, rep.name)) a_.errors.push_back(a_.title + " : " + c + ".");
                    break;
                }
                case RK::View:
                case RK::Popup:
                case RK::Symbol:
                case RK::Template: {
                    for (const auto& c : rewriteHmiReferences(w, true, node.name, rep.name)) a_.errors.push_back(a_.title + " : " + c + ".");
                    // Les liens par identifiant (le modele, l'en-tete, le pied, la vue parente, la vue de demarrage).
                    for (auto& v : w.views) {
                        if (v.templateView == node.id) v.templateView = rep.id;
                        if (v.header == node.id) v.header = rep.id;
                        if (v.footer == node.id) v.footer = rep.id;
                        if (v.upView == node.id) v.upView = rep.id;
                    }
                    if (w.config.startView == node.id && node.kind == RK::View) w.config.startView = rep.id;
                    for (auto& gr : w.security.groups)
                        if (gr.startView == node.id) gr.startView = rep.id;
                    break;
                }
                case RK::Type:
                    (void)hmi::renameTypeEverywhere(w, node.id, w.hmiType(rep.id) ? w.hmiType(rep.id)->name : rep.name);
                    break;
                case RK::Function:
                    (void)hmi::renameFunctionEverywhere(w, node.name, rep.name);
                    break;
                case RK::Script: {
                    const std::string from = node.name, to = rep.name;
                    hmi::forEachCode(w, [&](std::string& text, hmi::CodeForm, const hmi::CodeSite&) {
                        if (text.empty() || !mentions(text, from, true)) return;
                        const auto calls = hmi::scriptCalls(text);
                        if (std::none_of(calls.begin(), calls.end(), [&](const std::string& c) { return iequal(c, from); })) return;
                        for (const char q : {'\'', '"'}) {
                            const std::string a = std::string(1, q) + from + q, b = std::string(1, q) + to + q;
                            for (std::size_t at = 0; (at = lower(text).find(lower(a), at)) != std::string::npos; at += b.size()) text.replace(at, a.size(), b);
                        }
                    });
                    const auto retarget = [&](std::vector<hmi::Action>& list) {
                        for (auto& act : list)
                            if (act.operation == hmi::Operation::CallScript && iequal(hmi::roles::nameOf(act.target), from)) act.target = to;
                    };
                    for (auto& v : w.views) {
                        retarget(v.actions);
                        for (auto& o : v.objects) retarget(o.actions);
                    }
                    break;
                }
                case RK::Resource: {
                    // renameResource refuse un nom pris : le remplacant prend un nom d'attente, le partant prend son nom (ce qui
                    // le cite suit), le partant s'en va, le remplacant reprend son nom (ce qui le citait suit).
                    const std::string wait = rep.name + ".remplacement";
                    std::string why;
                    bool ok = hmi::renameResource(w, rep.id, wait, nullptr, &why);
                    ok = ok && hmi::renameResource(w, node.id, rep.name, nullptr, &why);
                    std::erase_if(w.assets.resources, [&](const hmi::Resource& r) { return r.id == node.id; });
                    ok = ok && hmi::renameResource(w, rep.id, rep.name, nullptr, &why);
                    if (!ok) a_.errors.push_back(a_.title + " : la ressource ne se remplace pas (" + why + ").");
                    break;
                }
                case RK::Equipment:
                    for (auto& v : w.programs.variables)
                        if (iequal(v.equipment, node.name)) v.equipment = rep.name;
                    break;
                case RK::Alarm: {
                    (void)hmi::renameAlarmReferences(w, node.name, rep.name);
                    const auto retarget = [&](std::vector<hmi::Action>& list) {
                        for (auto& act : list)
                            if (act.operation == hmi::Operation::AckAlarm && iequal(hmi::roles::nameOf(act.target), node.name)) act.target = rep.name;
                    };
                    for (auto& v : w.views) {
                        retarget(v.actions);
                        for (auto& o : v.objects) retarget(o.actions);
                    }
                    break;
                }
                default: break;
            }
            a_.fixes.push_back(std::to_string(users) + (users > 1 ? " r\xC3\xA9" "f\xC3\xA9rences \xC3\xA0 " : " r\xC3\xA9" "f\xC3\xA9rence \xC3\xA0 ") + node.name
                               + " : " + rep.name + " \xC3\xA0 la place.");
        }
    }

    // ---- references : modifier ce qui le cite, et le dire ------------------------------------------------
    ObjectEdit& editOf(const hmi::View& v, const hmi::Object& o) {
        for (auto& e : a_.objects)
            if (e.object == o.id && e.view == v.id) return e;
        ObjectEdit e;
        e.view = v.id;
        e.object = o.id;
        e.viewName = v.name;
        e.name = o.name;
        e.kind = o.kind;
        a_.objects.push_back(std::move(e));
        return a_.objects.back();
    }

    // Une vue (par son identifiant) part-elle avec CETTE suppression ?
    [[nodiscard]] bool removedView(hmi::Id id) const {
        if (id == hmi::kNoId) return false;
        const auto n = g_.byId(id);
        return n && gone_[*n] && viewLike(g_.node(*n).kind);
    }
    // ... avec celle-ci ou une autre de la proposition (Context::also) : jamais un remplacant.
    [[nodiscard]] bool leaving(hmi::Id id) const {
        if (id == hmi::kNoId) return false;
        const auto n = g_.byId(id);
        return n && (gone_[*n] || matches(g_, *n, p_, ctx_.also));
    }
    // La premiere vue de ce role qui reste (dans l'ordre du projet), sauf `self` ; nul : aucune.
    [[nodiscard]] const hmi::View* firstRemaining(std::string_view role, hmi::Id self) const {
        for (const auto& v : p_.views)
            if (v.role == role && v.id != self && !leaving(v.id)) return &v;
        return nullptr;
    }
    // En remontant depuis `from` (qui part) par `up` (le modele du modele, la parente de la parente) : la premiere vue qui
    // reste, sauf `self` ; kNoId : aucune.
    [[nodiscard]] hmi::Id survivor(hmi::Id from, hmi::Id self, const std::function<hmi::Id(const hmi::View&)>& up) const {
        std::set<hmi::Id> seen;
        for (hmi::Id id = from; id != hmi::kNoId && seen.insert(id).second;) {
            const hmi::View* x = p_.view(id);
            if (!x) break;
            if (!leaving(id)) return id == self ? hmi::kNoId : id;
            id = up(*x);
        }
        return hmi::kNoId;
    }
    // Une modification hors des objets, dite : une consequence de l'apercu, une ligne de Check::made.
    void said(const std::string& what, const std::string& who, const std::string& change) {
        a_.fixes.push_back(who + " : " + change + ".");
        a_.made.push_back({what, who, change});
    }

    // Les elements EXISTANTS du meme genre qui restent, pour remplacer : les plus proches par le nom (vrai) ; sans proche,
    // quelques vues (faux) - jamais un nom invente.
    [[nodiscard]] std::pair<std::vector<std::string>, bool> candidates(const rel::Node& dst) const {
        std::vector<std::string> out;
        const auto usable = [&](std::size_t n) {
            const rel::Node& x = g_.node(n);
            return x.kind == dst.kind && !gone_[n] && !matches(g_, n, p_, ctx_.also) && x.id != hmi::kNoId
                && !((x.kind == RK::Script || x.kind == RK::Function) && x.view != hmi::kNoId)
                && std::find(out.begin(), out.end(), x.name) == out.end();
        };
        for (const auto& name : nearNames(g_, {dst.kind}, dst.name))
            if (const auto n = g_.find(dst.kind, name); n && usable(*n) && out.size() < 3) out.push_back(g_.node(*n).name);
        if (!out.empty() || !viewLike(dst.kind)) return {out, true};
        for (std::size_t n = 0; n < g_.size() && out.size() < 3; ++n)
            if (usable(n)) out.push_back(g_.node(n).name);
        return {out, false};
    }
    // La strategie qui marcherait : remplacer, avec des elements existants (remplacer_par).
    [[nodiscard]] std::string replaceHint(const rel::Node& dst) const {
        const Noun no = nounOf(dst.kind);
        std::string s = std::string(no.fem ? "Remplace-la par une autre " : "Remplace-le par un autre ") + no.one + (no.fem ? " existante" : " existant")
                      + " (strat\xC3\xA9gie remplacer";
        const auto [names, near] = candidates(dst);
        if (!names.empty())
            s += ", remplacer_par : " + joinedOr(names)
               + (near ? (names.size() > 1 ? " - les plus proches" : no.fem ? " - la plus proche" : " - le plus proche") : " par exemple");
        return s + ")";
    }
    // Un refus : ou, quoi, pourquoi ; et la strategie qui marcherait.
    void refuse(const std::string& where, const std::string& what, const rel::Node& dst, const std::string& why = {},
                const std::string& otherwise = "ajoute cet \xC3\xA9l\xC3\xA9ment \xC3\xA0 la suppression") {
        a_.errors.push_back(a_.title + " (r\xC3\xA9" "f\xC3\xA9rences) : " + where + " - " + what
                            + (why.empty() ? std::string(" ne se retire pas sans casser") : " : " + why) + ". " + replaceHint(dst) + ", ou " + otherwise
                            + ".");
    }
    // Ou un code de `src` NOMME `name` (forEachCode) : "script Nav, ligne 2", "Vue_B . action 1" - 3 au plus ; `only` :
    // seulement ces endroits.
    [[nodiscard]] std::vector<std::string> codeSpots(const rel::Node& src, const std::string& name, bool quoted,
                                                     const std::function<bool(const hmi::CodeSite&)>& only = {}) const {
        std::vector<std::string> out;
        const std::string alarmHead = lower("alarme " + src.name);
        hmi::forEachCode(p_, [&](const std::string& text, hmi::CodeForm, const hmi::CodeSite& s) {
            if (s.again || text.empty() || out.size() >= 3) return;
            const hmi::View* host = s.host ? s.host : s.view;
            bool mine = false;
            switch (src.kind) {
                case RK::Script: mine = s.script == src.id; break;
                case RK::Function: mine = s.function == src.id || (s.method && s.method->id == src.id) || (s.own && s.own->id == src.id); break;
                case RK::Object: mine = s.object && s.object->id == src.id; break;
                case RK::View:
                case RK::Popup:
                case RK::Symbol:
                case RK::Template: mine = !s.object && s.script == hmi::kNoId && s.function == hmi::kNoId && host && host->id == src.id; break;
                case RK::Alarm: {
                    const std::string w = lower(s.where);
                    mine = !s.object && w.rfind(alarmHead, 0) == 0 && (w.size() == alarmHead.size() || !identChar(w[alarmHead.size()]));
                    break;
                }
                default: break;
            }
            if (!mine || (only && !only(s)) || !mentions(text, name, quoted)) return;
            const int line = lineOf(text, name, quoted);
            const std::string at = (s.object && s.key.empty() && s.action < 0 ? s.where : siteWhere(s)) + (line > 0 ? ", ligne " + std::to_string(line) : std::string{});
            if (std::find(out.begin(), out.end(), at) == out.end()) out.push_back(at);
        });
        return out;
    }
    // Du code de `src` nomme l'element : refuse (une fois par code et par element), en disant ou (le script, la ligne).
    void refuseCode(const rel::Edge& edge, const rel::Node& src, const rel::Node& dst, const std::function<bool(const hmi::CodeSite&)>& only = {}) {
        if (!codeRefused_.insert({edge.from, edge.to}).second) return;
        const bool quoted = viewLike(dst.kind) || dst.kind == RK::Script || dst.kind == RK::Resource;
        const auto spots = codeSpots(src, dst.name, quoted, only);
        refuse(spots.empty() ? edge.where : joined(spots, 3), "du code nomme " + dst.name, dst, kCodeWhy, "corrige ce code d'abord");
    }
    static constexpr const char* kCodeWhy = "on ne r\xC3\xA9\xC3\xA9" "crit pas du code sans savoir";

    // Les actions d'une liste qui citent l'element (sa cible ou sa valeur), retirees ; rend leur libelle.
    static std::vector<std::string> dropActions(std::vector<hmi::Action>& list, const std::function<bool(const hmi::Action&)>& cites) {
        std::vector<std::string> out;
        for (std::size_t k = list.size(); k-- > 0;)
            if (cites(list[k])) {
                out.push_back("action " + std::to_string(k + 1) + " (" + std::string(hmi::operationLabel(list[k].operation)) + " " + trimmed(list[k].target) + ")");
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(k));
            }
        return out;
    }

    // ---- les listes d'un objet (1.22.7) : l'entree qui le nomme, retiree - lues et reecrites comme l'application les lit
    // La liste << views >> d'une barre de navigation (listItems, comme navItems) : l'entree qui nomme la vue retiree, son
    // libelle (au meme rang) avec ; les autres gardees, dans l'ordre. Vide : la barre montre toutes les vues - dit.
    void navViews(const hmi::View& v, hmi::Object& o, hmi::Prop& pr, const rel::Node& dst, ObjectEdit& ed) {
        const auto items = hmi::listItems(pr.value);
        hmi::Prop* pl = o.find("labels");
        const auto labels = pl ? hmi::roles::splitList(pl->value, ';') : std::vector<std::string>{};
        std::vector<std::string> keep, keepLabels;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (hmi::roles::nameOf(items[i]) == dst.name) continue;
            keep.push_back(items[i]);
            keepLabels.push_back(i < labels.size() ? labels[i] : std::string{});
        }
        if (keep.size() == items.size()) return;
        const std::string before = pr.value;
        pr.value = hmi::joinSemicolons(keep);
        ed.changes.push_back({"views", before, pr.value});
        if (pl) {
            while (!keepLabels.empty() && keepLabels.back().empty()) keepLabels.pop_back();
            const std::string after = keepLabels.empty() ? std::string{} : hmi::joinSemicolons(keepLabels);
            if (after != pl->value) {
                ed.changes.push_back({"labels", pl->value, after});
                pl->value = after;
            }
        }
        a_.fixes.push_back(v.name + "." + o.name + " : " + dst.name + " " + agreed("retir\xC3\xA9", 1, nounOf(dst.kind).fem) + " de la liste des vues ("
                           + std::to_string(items.size()) + kArrow + std::to_string(keep.size()) + ").");
        if (keep.empty())
            a_.warnings.push_back(v.name + "." + o.name + " : sa liste des vues est vide - la barre de navigation montre alors toutes les vues "
                                  "ordinaires du projet. Choisis ses vues, ou supprime-la si elle ne sert plus.");
    }
    // Les zones d'un plan (<< mapZones >>, une par ligne : parseMapZones, formatMapZones) : la zone qui mene a la vue garde
    // son nom, ses points, ses alarmes et sa couleur ; sa vue est videe (un clic n'y navigue plus). Les autres lignes : telles quelles.
    void zoneViews(const hmi::View& v, const hmi::Object& o, hmi::Prop& pr, const rel::Node& dst, ObjectEdit& ed) {
        std::string out;
        std::vector<std::string> zones;
        const std::string& text = pr.value;
        for (std::size_t start = 0; start <= text.size();) {
            const auto nl = text.find('\n', start);
            std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            auto parsed = hmi::parseMapZones(line);
            if (parsed.size() == 1 && hmi::roles::nameOf(parsed.front().view) == dst.name) {
                parsed.front().view.clear();
                zones.push_back(parsed.front().name);
                line = hmi::formatMapZones(parsed);
            }
            out += line;
            if (nl == std::string::npos) break;
            out += '\n';
            start = nl + 1;
        }
        if (zones.empty()) return;
        ed.changes.push_back({"mapZones", pr.value, out});
        pr.value = out;
        const bool many = zones.size() > 1;
        a_.fixes.push_back(v.name + "." + o.name + " : " + (many ? "les zones " : "la zone ") + joined(zones) + (many ? " ne m\xC3\xA8nent" : " ne m\xC3\xA8ne")
                           + " plus \xC3\xA0 " + dst.name + " (vue vid\xC3\xA9" "e ; " + (many ? "les zones et leurs alarmes restent" : "la zone et ses alarmes restent")
                           + ").");
    }
    // Les arguments d'une instance (<< params >>) : l'argument qui le nomme vide (givenArguments, withArgument : comme
    // l'inspecteur) - le parametre reprend sa valeur par defaut. Un argument hors des parametres du symbole : refuse.
    void instanceArguments(const hmi::Project& w, const hmi::View& v, const hmi::Object& o, hmi::Prop& pr, const rel::Node& dst, bool quoted,
                           ObjectEdit& ed) {
        const std::string where = v.name + " . " + o.name + " . params";
        const hmi::View* sym = hmi::symbolOf(w, o);
        if (!sym || mentions(pr.expr, dst.name, quoted)) {
            refuse(where, "ses arguments", dst, sym ? "ils sont calcul\xC3\xA9s par une formule" : "son symbole est introuvable");
            return;
        }
        std::string text = pr.value;
        std::vector<std::string> lines;
        const auto given = hmi::givenArguments(*sym, text);
        for (std::size_t i = 0; i < given.size() && i < sym->params.size(); ++i) {
            if (!mentions(given[i], dst.name, quoted)) continue;
            text = hmi::withArgument(*sym, text, i, "");
            const std::string& def = sym->params[i].defaultValue;
            lines.push_back(v.name + "." + o.name + " : argument " + sym->params[i].name + " vid\xC3\xA9 (" + given[i] + kArrow
                           + (def.empty() ? std::string("sa valeur par d\xC3\xA9" "faut") : "valeur par d\xC3\xA9" "faut " + def) + ").");
        }
        if (mentions(text, dst.name, quoted)) {
            refuse(where, "un argument qui n'est pas un param\xC3\xA8tre de " + sym->name, dst);
            return;
        }
        if (lines.empty()) return;
        ed.changes.push_back({"params", pr.value, text});
        pr.value = text;
        for (auto& l : lines) a_.fixes.push_back(std::move(l));
    }

    // ---- les liens par identifiant (1.22.7) : modifies, dits ----------------------------------------------
    //  Les vues qui restent et qui pointent une vue qui part : l'ecran modele (celui au-dessus, sinon aucun), l'en-tete et
    //  le pied montres (le premier du meme role qui reste, sinon la case decochee), la vue parente (celle au-dessus, sinon
    //  la racine) ; un en-tete ou un pied choisi dont la case est decochee : oublie. `hiddenOnly` (strategie seul) :
    //  seulement ce dernier (il ne se voit pas : pas une dependance) - seul refuse les autres.
    void relink(hmi::Project& w, bool hiddenOnly) {
        for (auto& v : w.views) {
            const hmi::View* pv = p_.view(v.id);
            if (!pv || leaving(v.id) || kept(v.name)) continue;
            const std::string what(hmi::viewRoleLabel(v.role));
            if (!hiddenOnly && removedView(v.templateView)) {
                const std::string was = p_.view(v.templateView)->name;
                const hmi::Id up = survivor(v.templateView, v.id, [](const hmi::View& x) { return x.templateView; });
                v.templateView = up;
                said(what, v.name, "\xC3\xA9" "cran mod\xC3\xA8le " + was + kArrow + (up != hmi::kNoId ? p_.view(up)->name + " (celui au-dessus de " + was + ")" : std::string("aucun")));
            }
            for (const bool header : {true, false}) {
                hmi::Id& chosen = header ? v.header : v.footer;
                bool& show = header ? v.showHeader : v.showFooter;
                const hmi::View* shown = header ? hmi::headerOf(p_, *pv) : hmi::footerOf(p_, *pv);
                const std::string band = header ? "en-t\xC3\xAAte" : "pied de page";
                if (shown && removedView(shown->id)) {
                    if (hiddenOnly) continue;
                    const hmi::View* next = firstRemaining(header ? "entete" : "pied", v.id);
                    chosen = next ? next->id : hmi::kNoId;
                    show = next != nullptr;
                    said(what, v.name, band + " " + shown->name + kArrow + (next ? next->name + " (le premier qui reste)" : std::string("aucun (case d\xC3\xA9" "coch\xC3\xA9" "e)")));
                } else if (removedView(chosen)) {
                    const std::string was = p_.view(chosen)->name;
                    chosen = hmi::kNoId;
                    said(what, v.name, band + " choisi " + was + " oubli\xC3\xA9 (sa case \xC3\xA9tait d\xC3\xA9" "coch\xC3\xA9" "e)");
                }
            }
            if (!hiddenOnly && removedView(v.upView)) {
                const std::string was = p_.view(v.upView)->name;
                const hmi::Id up = survivor(v.upView, v.id, [](const hmi::View& x) { return x.upView; });
                v.upView = up;
                said(what, v.name, "vue parente " + was + kArrow + (up != hmi::kNoId ? p_.view(up)->name + " (celle au-dessus de " + was + ")"
                                                                                    : std::string("aucune (une racine du fil d'Ariane)")));
            }
        }
    }

    // Ce qui n'est pas une reference (une note du graphe, seul et references) : la vue de demarrage (la premiere vue ordinaire
    // qui reste ; aucune : refuse), la vue d'accueil d'un groupe (la vue de demarrage), un ecran du poste (retire de la
    // liste ; seul : refuse) ; seul : un en-tete ou un pied choisi, case decochee (oublie). Tout est dit.
    void settle(hmi::Project& w) {
        const bool refs = rq_.strategy == Strategy::References;
        if (!refs) relink(w, true);
        if (removedView(p_.config.startView)) {
            const std::string was = p_.view(p_.config.startView)->name;
            if (const hmi::View* next = firstRemaining("vue", hmi::kNoId)) {
                w.config.startView = next->id;
                a_.fixes.push_back("Vue de d\xC3\xA9marrage : " + was + kArrow + next->name + " (la premi\xC3\xA8re vue qui reste).");
                a_.made.push_back({"Vue de d\xC3\xA9marrage", next->name, "avant : " + was + " (supprim\xC3\xA9" "e)"});
                a_.warnings.push_back(was + " est la vue de d\xC3\xA9marrage : " + next->name + " la remplace (la premi\xC3\xA8re vue qui reste).");
            } else {
                a_.errors.push_back(a_.title + " : " + was + " est la vue de d\xC3\xA9marrage et aucune autre vue ordinaire ne reste - le projet n'aurait "
                                    "plus de vue de d\xC3\xA9marrage. Cr\xC3\xA9" "e d'abord une autre vue (ou garde celle-ci).");
            }
        }
        const hmi::View* start = removedView(w.config.startView) ? nullptr : w.view(w.config.startView);
        for (auto& gr : w.security.groups) {
            if (!removedView(gr.startView)) continue;
            const std::string was = p_.view(gr.startView)->name;
            gr.startView = hmi::kNoId;
            const std::string now = "la vue de d\xC3\xA9marrage" + (start ? " (" + start->name + ")" : std::string{});
            a_.fixes.push_back("Groupe " + gr.name + " : vue d'accueil " + was + kArrow + now + ".");
            a_.made.push_back({"Groupe", gr.name, "vue d'accueil " + was + kArrow + now});
        }
        for (std::size_t i = w.station.screens.size(); i-- > 0;) {
            const hmi::StationScreen sc = w.station.screens[i];
            const hmi::View* sv = p_.viewByName(hmi::roles::nameOf(sc.view));
            if (!sv || !removedView(sv->id)) continue;
            const std::string screen = "\xC3\xA9" "cran " + std::to_string(sc.display) + " du poste";
            if (!refs) {
                a_.errors.push_back(a_.title + " : " + sv->name + " est montr\xC3\xA9" "e par l'" + screen + " - strat\xC3\xA9gie references (l'\xC3\xA9" "cran est "
                                    "retir\xC3\xA9 de la liste des \xC3\xA9" "crans du poste) ou remplacer (par une autre vue existante).");
                continue;
            }
            w.station.screens.erase(w.station.screens.begin() + static_cast<std::ptrdiff_t>(i));
            a_.fixes.push_back("Poste : l'" + screen + " ne montre plus " + sv->name + " (retir\xC3\xA9 de la liste des \xC3\xA9" "crans).");
            a_.made.push_back({"Poste", screen, "ne montre plus " + sv->name + " (retir\xC3\xA9)"});
        }
    }

    // Une alarme du projet ou d'un symbole, par son identifiant (dans la copie).
    static hmi::AlarmDef* alarmIn(hmi::Project& w, hmi::Id id) {
        if (id == hmi::kNoId) return nullptr;
        if (hmi::AlarmDef* a = w.alarm(id)) return a;
        for (auto& v : w.views)
            for (auto& a : v.alarms)
                if (a.id == id) return &a;
        return nullptr;
    }

    void referencesIn(hmi::Project& w) {
        std::set<std::pair<std::size_t, std::size_t>> done;   // (qui cite, ce qui part) : une fois
        std::set<hmi::Id> dropped;                             // les instances retirees
        // Les instances d'abord : une instance retiree n'a plus de reference a corriger.
        std::vector<std::size_t> edges = directEdges_;
        std::stable_partition(edges.begin(), edges.end(), [&](std::size_t e) { return g_.edges()[e].link == rel::Link::Instance; });
        for (const auto e : edges) {
            const rel::Edge& edge = g_.edges()[e];
            const rel::Node& src = g_.node(edge.from);
            const rel::Node& dst = g_.node(edge.to);
            const std::string at = rel::edgeText(g_, edge);
            // Une exclusion n'est jamais modifiee : ni l'element, ni (1.22.7) ce que porte une vue exclue (sa barre, son plan).
            const hmi::View* holder = src.view != hmi::kNoId ? p_.view(src.view) : nullptr;
            if (kept(src.name) || (holder && kept(holder->name))) {
                a_.errors.push_back(a_.title + " : " + (kept(src.name) || !holder ? src.name : holder->name) + " est exclu, sa r\xC3\xA9" "f\xC3\xA9rence resterait ("
                                    + at + ").");
                continue;
            }
            const std::string name = dst.name;
            const bool quoted = viewLike(dst.kind) || dst.kind == RK::Script || dst.kind == RK::Resource;
            const auto cites = [&](const hmi::Action& a) {
                const bool target = iequal(hmi::roles::nameOf(a.target), name);
                switch (dst.kind) {
                    case RK::View:
                    case RK::Popup:
                    case RK::Template: return target && hmi::operationOpensView(a.operation);
                    case RK::Script: return target && a.operation == hmi::Operation::CallScript;
                    case RK::Function: return (target && a.operation == hmi::Operation::Call) || (a.operation == hmi::Operation::Call && iequal(trimmed(a.target), "=" + name));
                    case RK::Variable:
                        return mentions(a.target, name, false) || mentions(a.value, name, false) || mentions(a.guard, name, false) || mentions(a.watch, name, false);
                    case RK::Resource: return target || mentions(a.value, name, true);
                    default: return false;
                }
            };
            // Les aretes de `src` vers `dst` : ou le graphe a lu l'element (les proprietes, par leur cle ; les actions qui le
            // visent, par leur rang ; les actions dont le CODE le nomme ; ailleurs : du code). `head` : "Vue . Objet . ".
            struct Sites {
                std::set<std::string> keys;
                bool                  actions{false}, code{false};
                std::set<int>         codeActions;
            };
            const auto sitesOf = [&](const std::string& head, const std::vector<hmi::Action>* acts) {
                Sites s;
                for (const auto e2 : directEdges_) {
                    const rel::Edge& x = g_.edges()[e2];
                    if (x.from != edge.from || x.to != edge.to) continue;
                    if (head.empty() || x.where.rfind(head, 0) != 0) {
                        s.code = true;
                        continue;
                    }
                    const std::string rest = x.where.substr(head.size());
                    if (rest.find("(dossier ") != std::string::npos) continue;
                    if (rest.rfind("action ", 0) == 0) {
                        const auto rank = std::strtoul(rest.c_str() + 7, nullptr, 10);
                        if (acts && rank >= 1 && rank <= acts->size() && !cites((*acts)[rank - 1])) s.codeActions.insert(static_cast<int>(rank) - 1);
                        else s.actions = true;
                    } else {
                        s.keys.insert(rest.substr(0, rest.find(' ')));
                    }
                }
                return s;
            };
            switch (src.kind) {
                case RK::Object: {
                    hmi::View* v = w.view(src.view);
                    hmi::Object* o = v ? v->object(src.id) : nullptr;
                    if (!o || dropped.count(src.id)) break;
                    // Une instance d'un symbole qui part : elle part (ses autres references avec elle) - au retrait, avec le reste.
                    if (edge.link == rel::Link::Instance) {
                        dropped.insert(src.id);
                        editOf(*v, *o).removed = true;
                        a_.removed.push_back(itemOf(g_, edge.from, p_, "instance de " + name + " : retir\xC3\xA9" "e (r\xC3\xA9" "f\xC3\xA9rences)"));
                        for (const auto child : v->descendantsOf(o->id))
                            if (const auto cn = g_.byId(child)) a_.removed.push_back(itemOf(g_, *cn, p_, "dans le groupe " + src.name));
                        a_.fixes.push_back("Instance " + v->name + "." + src.name + " retir\xC3\xA9" "e (elle posait " + name + ").");
                        break;
                    }
                    if (!done.insert({edge.from, edge.to}).second) break;
                    const hmi::View* pv = p_.view(src.view);
                    const hmi::Object* po = pv ? pv->object(src.id) : nullptr;
                    const Sites st = sitesOf(pv ? pv->name + " . " + src.name + " . " : std::string{}, po ? &po->actions : nullptr);
                    // Du code de l'objet le nomme (une redefinition, le code d'une action) : refuse, en disant ou.
                    if (st.code || !st.codeActions.empty()) {
                        const auto spots = codeSpots(src, name, quoted, [&](const hmi::CodeSite& s) {
                            return s.key.empty() && (s.action < 0 ? st.code : st.codeActions.count(s.action) > 0);
                        });
                        refuse(spots.empty() ? at : joined(spots, 3), "du code de " + src.name + " nomme " + name, dst, kCodeWhy, "corrige ce code d'abord");
                    }
                    ObjectEdit& ed = editOf(*v, *o);
                    if (st.actions)
                        for (const auto& d : dropActions(o->actions, cites)) {
                            ed.changes.push_back({d, "", "retir\xC3\xA9" "e"});
                            a_.fixes.push_back(v->name + "." + o->name + " : " + d + " retir\xC3\xA9" "e.");
                        }
                    for (const auto& key : st.keys) {
                        hmi::Prop* pr = o->find(key);
                        if (!pr) {
                            refuse(at, src.name + " (" + key + ")", dst);
                            continue;
                        }
                        // 1.22.7 : les listes - l'entree qui le nomme, retiree (avant : un refus).
                        if (key == "views" && o->kind == hmi::Kind::NavBar) {
                            navViews(*v, *o, *pr, dst, ed);
                            continue;
                        }
                        if (key == "mapZones" && o->kind == hmi::Kind::ZoneMap) {
                            zoneViews(*v, *o, *pr, dst, ed);
                            continue;
                        }
                        if (key == "params" && o->kind == hmi::Kind::SymbolInstance) {
                            instanceArguments(w, *v, *o, *pr, dst, quoted && dst.kind != RK::Variable, ed);
                            continue;
                        }
                        if (key == "params" || key == "views" || key == "mapZones") {
                            refuse(v->name + " . " + o->name + " . " + key, "cette liste nomme " + name, dst);
                            continue;
                        }
                        const bool q = quoted && dst.kind != RK::Variable;
                        const bool inValue = dst.kind == RK::Resource ? (iequal(trimmed(pr->value), name) || mentions(pr->value, name, true)) : mentions(pr->value, name, q);
                        const bool inExpr = mentions(pr->expr, name, q);
                        const hmi::Prop before = *pr;
                        if (inExpr || !inValue) pr->expr.clear();
                        if (inValue || !inExpr) pr->value = inValue && pr->value.find('{') != std::string::npos ? dropHoles(pr->value, name) : std::string{};
                        if (before == *pr) continue;
                        ed.changes.push_back({pr->key, before.expr.empty() ? before.value : "=" + before.expr, pr->expr.empty() ? pr->value : "=" + pr->expr});
                        a_.fixes.push_back(v->name + "." + o->name + " : " + pr->key + " ne cite plus " + name + ".");
                    }
                    for (const auto& ao : o->alarmOverrides)
                        if ((ao.condition && mentions(*ao.condition, name, false)) || (ao.message && mentions(*ao.message, name, false)))
                            refuse(v->name + " . " + o->name, "son alarme surcharg\xC3\xA9" "e " + ao.alarm + " le nomme", dst, kCodeWhy, "corrige-la d'abord");
                    if (ed.changes.empty() && !ed.removed) std::erase_if(a_.objects, [&](const ObjectEdit& x) { return x.object == o->id && x.changes.empty() && !x.removed; });
                    break;
                }
                case RK::View:
                case RK::Popup:
                case RK::Symbol:
                case RK::Template: {
                    hmi::View* v = w.view(src.id);
                    const hmi::View* pv = p_.view(src.id);
                    if (!v || !pv) break;
                    // L'ecran modele, l'en-tete, le pied, la vue parente : des liens par identifiant (relink, plus bas).
                    if (edge.link == rel::Link::Inherits || (edge.link == rel::Link::Opens && edge.where == pv->name + " (vue parente, fil d'Ariane)")) break;
                    if (edge.link == rel::Link::TypeOf) {
                        refuse(edge.where, "sa d\xC3\xA9" "claration est de ce type", dst, "elle ne change pas de type d'office", "change d'abord cette d\xC3\xA9" "claration");
                        break;
                    }
                    // Une ressource (un fond de vue, une action qui la joue) : comme avant - les actions qui la citent retirees.
                    if (!viewLike(dst.kind) && dst.kind != RK::Variable && dst.kind != RK::Script && dst.kind != RK::Function) {
                        const auto gone = dropActions(v->actions, cites);
                        for (const auto& d : gone) a_.fixes.push_back(v->name + " : " + d + " retir\xC3\xA9" "e.");
                        if (gone.empty()) refuse(at, src.name, dst);
                        break;
                    }
                    if (!done.insert({edge.from, edge.to}).second) break;
                    const Sites st = sitesOf(pv->name + " . ", &pv->actions);
                    if (st.actions)
                        for (const auto& d : dropActions(v->actions, cites)) a_.fixes.push_back(v->name + " : " + d + " retir\xC3\xA9" "e.");
                    // Du code de la vue (le code d'une action, son titre, un parametre) le nomme : refuse, en disant ou.
                    if (st.code || !st.codeActions.empty() || !st.keys.empty()) {
                        const auto spots = codeSpots(src, name, quoted, [&](const hmi::CodeSite& s) { return s.action < 0 || st.codeActions.count(s.action) > 0; });
                        refuse(spots.empty() ? at : joined(spots, 3), "du code de " + src.name + " nomme " + name, dst, kCodeWhy, "corrige ce code d'abord");
                    }
                    break;
                }
                case RK::Variable: {
                    if (edge.link == rel::Link::Address) {
                        if (hmi::Variable* x = w.variableById(src.id)) {
                            x->equipment.clear();
                            x->address.clear();
                            x->places.clear();
                            a_.fixes.push_back("Variable " + x->name + " d\xC3\xA9li\xC3\xA9" "e de " + name + " (gard\xC3\xA9" "e).");
                        }
                        break;
                    }
                    if (edge.link == rel::Link::TypeOf)
                        refuse("variable " + src.name, "elle est de ce type", dst, "une variable ne change pas de type d'office", "supprime d'abord la variable");
                    else
                        refuseCode(edge, src, dst);
                    break;
                }
                case RK::Alarm: {
                    const auto ends = [&](std::string_view t) { return edge.where.size() >= t.size() && edge.where.compare(edge.where.size() - t.size(), t.size(), t) == 0; };
                    const bool message = ends(" (message)"), instruction = ends(" (consigne)");
                    // Le message, la consigne : des textes a trous - le trou qui le lit, retire (comme le texte d'un objet).
                    if (hmi::AlarmDef* al = alarmIn(w, src.id); al && (message || instruction)) {
                        std::string& text = message ? al->message : al->instruction;
                        const std::string before = text;
                        text = dropHoles(text, name);
                        if (text != before) {
                            a_.fixes.push_back("Alarme " + src.name + " : " + (message ? "son message" : "sa consigne") + " ne cite plus " + name + " (" + before + kArrow
                                               + text + ").");
                            break;
                        }
                    }
                    if (ends(" (condition)"))
                        refuse(edge.where, "la condition de l'alarme " + src.name + " le lit", dst, "une condition ne se r\xC3\xA9\xC3\xA9" "crit pas sans savoir", "corrige-la d'abord");
                    else
                        refuse(edge.where, "l'alarme " + src.name, dst, src.id == hmi::kNoId ? "une alarme d'objet (n\xC3\xA9" "e de sa biblioth\xC3\xA8que ou de son symbole) le lit" : std::string{});
                    break;
                }
                case RK::Recipe: refuse(at, "la recette " + src.name, dst, "un de ses champs l'\xC3\xA9" "crit : une recette ne perd pas un champ d'office"); break;
                case RK::Type:
                    if (edge.link == rel::Link::TypeOf || edge.link == rel::Link::Member)
                        refuse(at, "le type " + src.name, dst, edge.link == rel::Link::Member ? "un de ses membres est de ce type" : "il en d\xC3\xA9pend (h\xC3\xA9ritage, interface)");
                    else
                        refuseCode(edge, src, dst);
                    break;
                case RK::Script:
                case RK::Function: refuseCode(edge, src, dst); break;
                default: refuse(at, std::string(nounOf(src.kind).one) + " " + src.name, dst); break;
            }
        }
        // Les liens par identifiant des vues qui restent (le modele, l'en-tete, le pied, la vue parente).
        relink(w, false);
        // L'historique (une note, pas une arete) : la variable n'y est plus archivee.
        for (const auto t : targets_)
            if (g_.node(t).kind == RK::Variable) {
                const std::string n = g_.node(t).name;
                const auto before = w.history.archived.size();
                std::erase_if(w.history.archived, [&](const std::string& x) { return mentions(x, n, false); });
                if (w.history.archived.size() != before) a_.fixes.push_back("Historique : " + n + " n'est plus archiv\xC3\xA9" "e.");
            }
        // Les actions qui acquittent une alarme qui part (sans arete du graphe).
        for (const auto t : targets_)
            if (g_.node(t).kind == RK::Alarm) {
                const std::string n = g_.node(t).name;
                const auto acks = [&](const hmi::Action& a) { return a.operation == hmi::Operation::AckAlarm && iequal(hmi::roles::nameOf(a.target), n); };
                for (auto& v : w.views) {
                    for (const auto& d : dropActions(v.actions, acks)) a_.fixes.push_back(v.name + " : " + d + " retir\xC3\xA9" "e.");
                    for (auto& o : v.objects)
                        for (const auto& d : dropActions(o.actions, acks)) {
                            editOf(v, o).changes.push_back({d, "", "retir\xC3\xA9" "e"});
                            a_.fixes.push_back(v.name + "." + o.name + " : " + d + " retir\xC3\xA9" "e.");
                        }
                }
            }
    }

    const hmi::Project&      p_;
    const rel::Graph&        g_;
    const Request&           rq_;
    const Context&           ctx_;
    Analysis&                a_;
    std::vector<char>        gone_;
    std::vector<std::size_t> targets_;
    std::vector<std::size_t> directEdges_;
    std::set<std::size_t>    directNodes_;
    std::set<std::size_t>    jokerNodes_;     // les listes a joker de dossier qui le lisent (elles suivront)
    std::vector<std::string> notedUsers_;   // des emplois que le graphe note sans arete (types::usersOf, functionCallers)
    std::set<std::pair<std::size_t, std::size_t>> codeRefused_;   // (le code, ce qui part) : un refus chacun
};

// L'erreur bloquante d'une verification globale, sa cle (pour comparer avant / apres).
std::string findingKey(const hmi::audit::Finding& f) { return f.category + "|" + f.message + "|" + f.where; }

// LA VERIFICATION GLOBALE (Generer, Compiler, les adresses... : hmi::audit::run) : les erreurs bloquantes NOUVELLES qui
// citent un element parti ou un element dont une reference a change (`names`).
std::vector<std::string> globalCheck(const hmi::Project& before, const hmi::Project& after, const std::vector<std::string>& names,
                                     const hmi::NameExists& plcName) {
    std::vector<std::string> out;
    const hmi::NameExists plc = plcName ? plcName : hmi::NameExists([](std::string_view) { return false; });
    const auto was = hmi::audit::run(before, plc);
    const auto now = hmi::audit::run(after, plc);
    std::set<std::string> known;
    for (const auto& f : was.findings)
        if (f.severity == hmi::audit::Severity::Error) known.insert(findingKey(f));
    for (const auto& f : now.findings) {
        if (f.severity != hmi::audit::Severity::Error || known.count(findingKey(f))) continue;
        const bool cites = std::any_of(names.begin(), names.end(), [&](const std::string& n) {
            return !n.empty() && (mentions(f.message, n, true) || mentions(f.where, n, true) || mentions(f.property, n, true));
        });
        if (cites) out.push_back("v\xC3\xA9rification globale : " + f.message + (f.where.empty() ? std::string{} : " (" + f.where + ")"));
    }
    return out;
}

} // namespace

// ---- les genres, les strategies --------------------------------------------------------------------
std::string_view genreKey(Genre g) noexcept {
    switch (g) {
        case Genre::Object: return "objet";
        case Genre::Instance: return "instance";
        case Genre::View: return "vue";
        case Genre::Popup: return "popup";
        case Genre::Symbol: return "symbole";
        case Genre::Template: return "modele";
        case Genre::Variable: return "variable";
        case Genre::Type: return "type";
        case Genre::Alarm: return "alarme";
        case Genre::Script: return "script";
        case Genre::Function: return "fonction";
        case Genre::Equipment: return "equipement";
        case Genre::Resource: return "ressource";
        case Genre::Folder: return "dossier";
    }
    return "objet";
}

std::optional<Genre> genreFromKey(std::string_view raw) noexcept {
    const std::string k = lower(trimmed(raw));
    if (k.empty() || k == "objet" || k == "objets" || k == "objet_graphique") return Genre::Object;
    if (k == "instance" || k == "instances" || k == "instance_symbole" || k == "instance_de_symbole") return Genre::Instance;
    if (k == "vue" || k == "vues" || k == "synoptique") return Genre::View;
    if (k == "popup" || k == "popups") return Genre::Popup;
    if (k == "symbole" || k == "symboles" || k == "definition_symbole") return Genre::Symbol;
    if (k == "modele" || k == "modeles" || k == "ecran_modele" || k == "ecrans_modeles" || k == "entete" || k == "en-tete" || k == "pied")
        return Genre::Template;
    if (k == "variable" || k == "variables") return Genre::Variable;
    if (k == "type" || k == "types" || k == "type_ihm" || k == "structure") return Genre::Type;
    if (k == "alarme" || k == "alarmes") return Genre::Alarm;
    if (k == "script" || k == "scripts") return Genre::Script;
    if (k == "fonction" || k == "fonctions") return Genre::Function;
    if (k == "equipement" || k == "equipements" || k == "\xC3\xA9quipement" || k == "\xC3\xA9quipements") return Genre::Equipment;
    if (k == "ressource" || k == "ressources" || k == "image" || k == "images") return Genre::Resource;
    if (k == "dossier" || k == "dossiers") return Genre::Folder;
    return std::nullopt;
}

std::string genreNoun(Genre g) {
    switch (g) {
        case Genre::Object: return "l'objet";
        case Genre::Instance: return "l'instance de symbole";
        case Genre::View: return "la vue";
        case Genre::Popup: return "la popup";
        case Genre::Symbol: return "le symbole";
        case Genre::Template: return "l'\xC3\xA9" "cran mod\xC3\xA8le";
        case Genre::Variable: return "la variable";
        case Genre::Type: return "le type";
        case Genre::Alarm: return "l'alarme";
        case Genre::Script: return "le script";
        case Genre::Function: return "la fonction";
        case Genre::Equipment: return "l'\xC3\xA9quipement";
        case Genre::Resource: return "la ressource";
        case Genre::Folder: return "le dossier";
    }
    return "l'\xC3\xA9l\xC3\xA9ment";
}

bool objectGenre(std::string_view key) noexcept {
    const auto g = genreFromKey(key);
    return !g || *g == Genre::Object || *g == Genre::Instance;   // inconnu : refuse a la lecture, jamais du projet
}

bool projectRemoval(const Operation& op) noexcept { return op.action == Operation::Action::Remove && !objectGenre(op.kind); }

std::string_view strategyKey(Strategy s) noexcept {
    switch (s) {
        case Strategy::Only: return "seul";
        case Strategy::Replace: return "remplacer";
        case Strategy::References: return "references";
    }
    return "seul";
}

std::optional<Strategy> strategyFromKey(std::string_view raw) noexcept {
    const std::string k = lower(trimmed(raw));
    if (k.empty() || k == "seul" || k == "seulement" || k == "element" || k == "inutiles" || k == "cascade") return Strategy::Only;
    if (k == "remplacer" || k == "remplacement") return Strategy::Replace;
    if (k == "references" || k == "r\xC3\xA9" "f\xC3\xA9rences" || k == "retirer_references" || k == "garder_partages") return Strategy::References;
    return std::nullopt;
}

bool requestOf(const Operation& op, Request& rq, std::string* why, std::string_view openView) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    rq = Request{};
    const auto g = genreFromKey(op.kind);
    if (!g)
        return fail("supprimer : genre \xC2\xAB " + op.kind + " \xC2\xBB inconnu (objet, instance, vue, popup, symbole, modele, variable, type, alarme, "
                    "script, fonction, equipement, ressource, dossier).");
    rq.genre = *g;
    for (const auto& n : op.objects)
        if (!trimmed(n).empty()) rq.names.push_back(trimmed(n));
    if (rq.names.empty() && !trimmed(op.name).empty()) rq.names.push_back(trimmed(op.name));
    if (rq.names.empty()) return fail("supprimer : rien \xC3\xA0 supprimer (\xC2\xAB objets \xC2\xBB : les noms des \xC3\xA9l\xC3\xA9ments existants).");
    rq.view = trimmed(op.target).empty() ? std::string(openView) : trimmed(op.target);
    rq.list = prop(op, "liste");
    const std::string s = prop(op, "strategie");
    const auto st = strategyFromKey(s);
    if (!st) return fail("supprimer : strat\xC3\xA9gie \xC2\xAB " + s + " \xC2\xBB inconnue (seul, remplacer, references, inutiles).");
    rq.strategy = *st;
    rq.useless = lower(s) == "inutiles" || lower(s) == "cascade" || readBool(prop(op, "inutiles"));
    rq.replacement = prop(op, "remplacer_par");
    if (rq.replacement.empty()) rq.replacement = prop(op, "par");
    if (!rq.replacement.empty() && rq.strategy == Strategy::Only && s.empty()) rq.strategy = Strategy::Replace;
    if (rq.strategy == Strategy::Replace && rq.replacement.empty())
        return fail("supprimer (remplacer) : \xC2\xAB remplacer_par \xC2\xBB manque - un \xC3\xA9l\xC3\xA9ment EXISTANT du m\xC3\xAAme genre.");
    for (const auto& k : op.keep)
        if (!trimmed(k).empty()) rq.keep.push_back(trimmed(k));
    if ((rq.genre == Genre::Object || rq.genre == Genre::Instance) && (rq.strategy != Strategy::Only || rq.useless))
        return fail("supprimer des objets : strat\xC3\xA9gie seul (l'objet part, sa variable et son symbole restent) - remplacer, references et "
                    "inutiles s'appliquent aux \xC3\xA9l\xC3\xA9ments du projet ; ce qui deviendrait inutile est montr\xC3\xA9 dans l'aper\xC3\xA7u "
                    "(supprime-le par une autre op\xC3\xA9ration).");
    return true;
}

std::vector<Item> namedItems(const Operation& op, std::string_view openView) {
    std::vector<Item> out;
    Request rq;
    if (op.action != Operation::Action::Remove || !requestOf(op, rq, nullptr, openView)) return out;
    for (const auto& n : rq.names) {
        if (std::any_of(rq.keep.begin(), rq.keep.end(), [&](const std::string& k) { return iequal(k, n); })) continue;
        Item it;
        it.kind = kindOf(rq.genre);
        it.name = n;
        if (it.kind == RK::Object) it.viewName = rq.view;
        out.push_back(std::move(it));
    }
    return out;
}

bool exists(const hmi::Project& p, const Item& it) {
    switch (it.kind) {
        case RK::Object: {
            const hmi::View* v = p.viewByName(it.viewName);
            return v && std::any_of(v->objects.begin(), v->objects.end(), [&](const hmi::Object& o) { return iequal(o.name, it.name); });
        }
        case RK::View:
        case RK::Popup:
        case RK::Symbol:
        case RK::Template: return p.viewByName(it.name) != nullptr;
        case RK::Variable: return p.variable(it.name) != nullptr;
        case RK::Type: return p.hmiTypeByName(it.name) != nullptr;
        case RK::Alarm: return p.alarmByName(it.name) != nullptr;
        case RK::Script: return p.generalScript(it.name) != nullptr;
        case RK::Function: return p.functionByName(it.name) != nullptr;
        case RK::Equipment: return p.equipmentByName(it.name) != nullptr;
        case RK::Resource: return p.resourceByName(it.name) != nullptr;
        case RK::Recipe: return p.recipeByName(it.name) != nullptr;
        case RK::Folder: {
            for (const auto& f : hmi::types::allFolders(p))
                if (iequal(f, it.name)) return true;
            for (int l = 0; l < static_cast<int>(hmi::fold::List::Count); ++l)
                for (const auto& f : hmi::fold::allFolders(p, static_cast<hmi::fold::List>(l)))
                    if (hmi::fold::sameFolder(f, it.name)) return true;
            return false;
        }
    }
    return false;
}

// ---- l'analyse en texte ------------------------------------------------------------------------------
std::vector<std::string> Analysis::summary() const {
    std::vector<std::string> out;
    const auto byKind = [](const std::vector<Item>& list) {
        std::vector<std::pair<RK, std::size_t>> counts;
        for (const auto& it : list) {
            auto c = std::find_if(counts.begin(), counts.end(), [&](const auto& x) { return x.first == it.kind; });
            if (c == counts.end()) counts.emplace_back(it.kind, 1);
            else ++c->second;
        }
        return counts;
    };
    // ce qui part avec (le contenu, les devenus inutiles)
    std::vector<Item> contained, cascaded;
    for (const auto& it : removed) {
        if (it.why.rfind("contenu", 0) == 0 || it.why.rfind("dans le groupe", 0) == 0) contained.push_back(it);
        else if (it.why.rfind("devenu", 0) == 0) cascaded.push_back(it);
    }
    for (const auto& [k, n] : byKind(contained)) out.push_back(counted(n, k) + " " + agreed("supprim\xC3\xA9", n, nounOf(k).fem) + " avec (leur contenant)");
    for (const auto& [k, n] : byKind(direct)) out.push_back(counted(n, k) + " directement " + agreed("concern\xC3\xA9", n, nounOf(k).fem));
    for (const auto& [k, n] : byKind(indirect)) {
        const bool fem = nounOf(k).fem;
        std::string tail;
        if (k == RK::Alarm) tail = n > 1 ? "d\xC3\xA9pendantes" : "d\xC3\xA9pendante";
        else if (k == RK::Script || k == RK::Function) tail = "utilisant ces \xC3\xA9l\xC3\xA9ments";
        else if (viewLike(k)) tail = "contenant des objets associ\xC3\xA9s";
        else if (k == RK::Object) tail = agreed("associ\xC3\xA9", n, fem);
        else tail = agreed("touch\xC3\xA9", n, fem) + " indirectement";
        out.push_back(counted(n, k) + " " + tail);
    }
    if (!equipments.empty())
        out.push_back(counted(equipments.size(), RK::Equipment) + " " + agreed("concern\xC3\xA9", equipments.size(), false) + " (li\xC3\xA9"
                      + std::string(equipments.size() > 1 ? "s" : "") + " \xC3\xA0 ces variables)");
    if (!potential.empty())
        out.push_back(std::to_string(potential.size()) + (potential.size() > 1 ? " r\xC3\xA9" "f\xC3\xA9rences possibles" : " r\xC3\xA9" "f\xC3\xA9rence possible")
                      + " par le nom (\xC3\xA0 v\xC3\xA9rifier)");
    for (const auto& [k, n] : byKind(shared)) out.push_back(counted(n, k) + " " + agreed("partag\xC3\xA9", n, nounOf(k).fem) + " : " + agreed("gard\xC3\xA9", n, nounOf(k).fem));
    for (const auto& [k, n] : byKind(cascaded))
        out.push_back(counted(n, k) + " " + agreed("devenu", n, nounOf(k).fem) + (n > 1 ? " inutiles" : " inutile") + ", " + agreed("supprim\xC3\xA9", n, nounOf(k).fem) + " aussi");
    for (const auto& [k, n] : byKind(orphans))
        out.push_back(counted(n, k) + (n > 1 ? " deviendraient " : " deviendrait ") + agreed("orphelin", n, nounOf(k).fem) + " (" + agreed("gard\xC3\xA9", n, nounOf(k).fem) + ")");
    if (!moved.empty()) out.push_back(std::to_string(moved.size()) + (moved.size() > 1 ? " \xC3\xA9l\xC3\xA9ments remontent" : " \xC3\xA9l\xC3\xA9ment remonte") + " d'un cran");
    if (!fixes.empty()) out.push_back(std::to_string(fixes.size()) + (fixes.size() > 1 ? " r\xC3\xA9" "f\xC3\xA9rences modifi\xC3\xA9" "es" : " r\xC3\xA9" "f\xC3\xA9rence modifi\xC3\xA9" "e")
                                      + " par la strat\xC3\xA9gie " + std::string(strategyKey(strategy)));
    if (direct.empty() && indirect.empty() && potential.empty()) out.push_back("aucune d\xC3\xA9pendance : r\xC3\xA9" "ellement supprimable seul");
    return out;
}

std::string Analysis::text(std::size_t maxPerList) const {
    std::string t = title + "\n";
    for (const auto& s : summary()) t += "- " + s + "\n";
    const auto list = [&](const char* head, const std::vector<Item>& items) {
        if (items.empty()) return;
        t += std::string(head) + " :\n";
        for (std::size_t i = 0; i < items.size() && i < maxPerList; ++i) t += "  - " + display(items[i]) + (items[i].why.empty() ? std::string{} : " : " + items[i].why) + "\n";
        if (items.size() > maxPerList) t += "  - \xE2\x80\xA6 et " + std::to_string(items.size() - maxPerList) + " autres\n";
    };
    list("Part", removed);
    list("D\xC3\xA9pendances directes", direct);
    list("D\xC3\xA9pendances indirectes", indirect);
    list("D\xC3\xA9pendances potentielles", potential);
    list("Partag\xC3\xA9 (gard\xC3\xA9)", shared);
    list("Deviendrait orphelin", orphans);
    for (const auto& f : fixes) t += "  * " + f + "\n";
    for (const auto& w : warnings) t += "! " + w + "\n";
    for (const auto& e : errors) t += "X " + e + "\n";
    return t;
}

// ---- la suppression ----------------------------------------------------------------------------------
bool run(hmi::Project& project, const Request& rq, Analysis& a, const Context& ctx) {
    a = Analysis{};
    a.strategy = rq.strategy;
    a.useless = rq.useless;
    a.replacement = rq.replacement;
    const hmi::Project before = project;
    const rel::Graph g = rel::build(before);
    Planner pl(before, g, rq, ctx, a);
    if (!pl.resolve()) return false;
    pl.closure();
    pl.dependents();
    if (!pl.strategyCheck()) return false;
    hmi::Project work = before;
    if (!pl.apply(work)) return false;
    pl.cascade(work);
    // LA VERIFICATION sur la copie : absent, aucune reference restante (sauf vers ce que d'autres suppressions emportent),
    // et (seul) la verification globale.
    std::vector<std::string> problems;
    for (const auto& it : a.removed)
        if (present(work, it)) problems.push_back("\xC2\xAB " + display(it) + " \xC2\xBB est encore dans le projet");
    for (auto& r : remaining(work, a.removed, ctx.also)) problems.push_back("r\xC3\xA9" "f\xC3\xA9rence restante : " + r);
    if (ctx.global && problems.empty()) {
        std::vector<std::string> names = pl.touched();
        for (const auto& it : a.removed) names.push_back(it.name);
        for (auto& v : globalCheck(before, work, names, ctx.plcName)) problems.push_back(std::move(v));
    }
    if (!problems.empty()) {
        std::size_t k = 0;
        for (const auto& pb : problems) {
            if (k++ == 8) {
                a.errors.push_back(a.title + " : \xE2\x80\xA6 et " + std::to_string(problems.size() - 8) + " autres probl\xC3\xA8mes.");
                break;
            }
            a.errors.push_back(a.title + " - v\xC3\xA9rification sur la copie : " + pb + ".");
        }
        return false;
    }
    project = std::move(work);
    return true;
}

Analysis analyze(const hmi::Project& project, const Request& rq, const Context& ctx) {
    hmi::Project copy = project;
    Analysis a;
    (void)run(copy, rq, a, ctx);
    return a;
}

// ---- la verification -----------------------------------------------------------------------------------
std::vector<std::string> remaining(const hmi::Project& after, const std::vector<Item>& removed, const std::vector<Item>& ignore) {
    std::vector<std::string> out;
    // Des TEMOINS : un element du meme genre et du meme nom que chaque element parti (s'il n'en existe pas un autre) ; ce
    // qui le cite encore est une reference restante - lue par le graphe des relations, comme partout.
    hmi::Project probe = after;
    std::vector<hmi::Id> stubs;
    std::set<hmi::Id> goneViews;
    for (const auto& it : removed) {
        if (viewLike(it.kind) && it.id != hmi::kNoId) goneViews.insert(it.id);
        if (it.view != hmi::kNoId) continue;   // un objet, un script, une fonction, une alarme de vue : par leur vue
        const hmi::Id id = probe.allocate();
        std::string ns, name = it.name;
        if (const auto dot = name.rfind('.'); dot != std::string::npos && (it.kind == RK::Type || it.kind == RK::Script || it.kind == RK::Function)) {
            ns = name.substr(0, dot);
            name = name.substr(dot + 1);
        }
        bool added = true;
        switch (it.kind) {
            case RK::Variable:
                if (after.variable(it.name)) added = false;
                else {
                    hmi::Variable v;
                    v.id = id;
                    v.name = it.name;
                    v.type = "INT";
                    probe.programs.variables.push_back(v);
                }
                break;
            case RK::Type:
                if (after.hmiTypeByName(it.name)) added = false;
                else {
                    hmi::HmiType t;
                    t.id = id;
                    t.name = name;
                    t.ns = ns;
                    probe.programs.types.push_back(t);
                }
                break;
            case RK::Equipment:
                if (after.equipmentByName(it.name)) added = false;
                else {
                    hmi::Equipment e;
                    e.id = id;
                    e.name = it.name;
                    e.simulated = true;
                    probe.equipments.push_back(e);
                }
                break;
            case RK::Resource:
                if (after.resourceByName(it.name)) added = false;
                else {
                    hmi::Resource r;
                    r.id = id;
                    r.name = it.name;
                    probe.assets.resources.push_back(r);
                }
                break;
            case RK::Script:
                if (after.generalScript(it.name)) added = false;
                else {
                    hmi::Script s;
                    s.id = id;
                    s.name = name;
                    s.ns = ns;
                    s.event = "Appel";
                    probe.programs.scripts.push_back(s);
                }
                break;
            case RK::Function: {
                bool method = false;
                for (const auto& t : after.programs.types)
                    if (iequal(hmi::fullName(t), ns)) method = true;
                if (method || after.functionByName(it.name)) added = false;
                else {
                    hmi::HmiFunction f;
                    f.id = id;
                    f.name = name;
                    f.ns = ns;
                    probe.programs.functions.push_back(f);
                }
                break;
            }
            case RK::View:
            case RK::Popup:
            case RK::Symbol:
            case RK::Template:
                if (after.viewByName(it.name)) added = false;
                else {
                    hmi::View v;
                    v.id = id;
                    v.name = it.name;
                    v.role = it.kind == RK::Symbol ? "symbole" : it.kind == RK::Popup ? "popup" : it.kind == RK::Template ? "modele" : "vue";
                    probe.views.push_back(v);
                }
                break;
            default: added = false; break;
        }
        if (added) stubs.push_back(id);
    }
    if (!stubs.empty()) {
        const rel::Graph g = rel::build(probe);
        for (const auto id : stubs) {
            const auto n = g.byId(id);
            if (!n) continue;
            for (const auto e : g.in(*n)) {
                const rel::Edge& edge = g.edges()[e];
                if (edge.link == rel::Link::Contains || g.node(edge.from).kind == RK::Folder) continue;
                if (matches(g, edge.from, probe, ignore)) continue;
                if (automaticNav(probe, g, edge)) continue;   // une barre sans liste : les vues du projet (le temoin y passe)
                out.push_back(rel::edgeText(g, edge));
            }
        }
    }
    // Les liens par identifiant vers une vue partie.
    if (!goneViews.empty()) {
        for (const auto& v : after.views) {
            // une vue qu'une autre suppression de la proposition emporte : ses liens partent avec elle
            if (std::any_of(ignore.begin(), ignore.end(), [&](const Item& x) { return viewLike(x.kind) && iequal(x.name, v.name); })) continue;
            if (goneViews.count(v.templateView)) out.push_back(v.name + " h\xC3\xA9rite d'un \xC3\xA9" "cran mod\xC3\xA8le supprim\xC3\xA9");
            if (goneViews.count(v.header)) out.push_back(v.name + " montre un en-t\xC3\xAAte supprim\xC3\xA9");
            if (goneViews.count(v.footer)) out.push_back(v.name + " montre un pied de page supprim\xC3\xA9");
            if (goneViews.count(v.upView)) out.push_back(v.name + " a pour vue parente une vue supprim\xC3\xA9" "e");
            if (goneViews.count(v.ownerSymbol)) out.push_back("la popup " + v.name + " appartient \xC3\xA0 un symbole supprim\xC3\xA9");
        }
        if (goneViews.count(after.config.startView)) out.push_back("la vue de d\xC3\xA9marrage est supprim\xC3\xA9" "e");
        for (const auto& gr : after.security.groups)
            if (goneViews.count(gr.startView)) out.push_back("la vue d'accueil du groupe " + gr.name + " est supprim\xC3\xA9" "e");
    }
    // Les alarmes du projet : les actions qui les acquittent par leur nom.
    for (const auto& it : removed) {
        if (it.kind != RK::Alarm || it.view != hmi::kNoId || after.alarmByName(it.name)) continue;
        for (const auto& v : after.views) {
            const auto scan = [&](const std::vector<hmi::Action>& list, const std::string& base) {
                for (std::size_t k = 0; k < list.size(); ++k)
                    if (list[k].operation == hmi::Operation::AckAlarm && iequal(hmi::roles::nameOf(list[k].target), it.name))
                        out.push_back(base + " . action " + std::to_string(k + 1) + " acquitte l'alarme " + it.name);
            };
            scan(v.actions, v.name);
            for (const auto& o : v.objects) scan(o.actions, v.name + " . " + o.name);
        }
    }
    return out;
}

std::vector<std::string> verify(const hmi::Project& before, const hmi::Project& after, const std::vector<Item>& removed,
                                const std::vector<std::string>& touched, const hmi::NameExists& plcName, bool global) {
    std::vector<std::string> out;
    for (const auto& it : removed)
        if (present(after, it)) out.push_back("\xC2\xAB " + display(it) + " \xC2\xBB est encore dans le projet");
    for (auto& r : remaining(after, removed)) out.push_back("r\xC3\xA9" "f\xC3\xA9rence restante : " + r);
    if (!global) return out;
    std::vector<std::string> names = touched;
    for (const auto& it : removed) names.push_back(it.name);
    for (auto& g : globalCheck(before, after, names, plcName)) out.push_back(std::move(g));
    return out;
}

bool confirmRemoved(const hmi::Project& after, const Operation& op, std::string_view openView, std::string* note) {
    Request rq;
    std::string why;
    if (!requestOf(op, rq, &why, openView)) {
        if (note) *note = why;
        return false;
    }
    std::vector<Item> gone;
    std::vector<std::string> still;
    for (const auto& n : rq.names) {
        if (std::any_of(rq.keep.begin(), rq.keep.end(), [&](const std::string& k) { return iequal(k, n); })) continue;
        Item it;
        it.name = n;
        bool there = false;
        switch (rq.genre) {
            case Genre::Object:
            case Genre::Instance: {
                const hmi::View* v = after.viewByName(rq.view);
                there = v && std::any_of(v->objects.begin(), v->objects.end(), [&](const hmi::Object& o) { return iequal(o.name, n); });
                it.kind = RK::Object;
                it.viewName = rq.view;
                if (v) it.view = v->id;
                break;
            }
            case Genre::View:
            case Genre::Popup:
            case Genre::Symbol:
            case Genre::Template:
                there = after.viewByName(n) != nullptr;
                it.kind = kindOf(rq.genre);
                break;
            case Genre::Variable: there = after.variable(n) != nullptr; it.kind = RK::Variable; break;
            case Genre::Type: there = after.hmiTypeByName(n) != nullptr; it.kind = RK::Type; break;
            case Genre::Alarm: there = after.alarmByName(n) != nullptr; it.kind = RK::Alarm; break;
            case Genre::Script: there = after.generalScript(n) != nullptr; it.kind = RK::Script; break;
            case Genre::Function: there = after.functionByName(n) != nullptr; it.kind = RK::Function; break;
            case Genre::Equipment: there = after.equipmentByName(n) != nullptr; it.kind = RK::Equipment; break;
            case Genre::Resource: there = after.resourceByName(n) != nullptr; it.kind = RK::Resource; break;
            case Genre::Folder: {
                const auto list = rq.list.empty() ? std::optional<int>{} : folderList(rq.list);
                for (int l = -1; l < static_cast<int>(hmi::fold::List::Count) && !there; ++l) {
                    if (list && *list != l) continue;
                    if (l < 0) {
                        for (const auto& f : hmi::types::allFolders(after)) there = there || iequal(f, n);
                    } else {
                        for (const auto& f : hmi::fold::allFolders(after, static_cast<hmi::fold::List>(l))) there = there || hmi::fold::sameFolder(f, n);
                    }
                }
                it.kind = RK::Folder;
                break;
            }
        }
        if (there) still.push_back(n);
        else gone.push_back(std::move(it));
    }
    // Les references : un temoin de chaque nom (pas les objets ni les dossiers).
    std::erase_if(gone, [](const Item& it) { return it.kind == RK::Object || it.kind == RK::Folder; });
    const auto refs = remaining(after, gone);
    if (still.empty() && refs.empty()) return true;
    if (note) {
        if (!still.empty()) *note = joined(still) + (still.size() > 1 ? " sont encore dans le projet" : " est encore dans le projet");
        else *note = std::to_string(refs.size()) + (refs.size() > 1 ? " r\xC3\xA9" "f\xC3\xA9rences restantes : " : " r\xC3\xA9" "f\xC3\xA9rence restante : ") + joined(refs, 3);
    }
    return false;
}

// ---- l'apercu, la confirmation ---------------------------------------------------------------------------
Check::Removal removalOf(std::size_t op, const Analysis& a, const std::vector<std::string>& keep) {
    Check::Removal r;
    r.op = op;
    r.title = a.title;
    r.strategy = std::string(strategyKey(a.strategy));
    if (a.strategy == Strategy::References) r.strategy = "r\xC3\xA9" "f\xC3\xA9rences (seulement les r\xC3\xA9" "f\xC3\xA9rences concern\xC3\xA9" "es)";
    if (a.strategy == Strategy::Replace) r.strategy = "remplacer par " + a.replacement;
    if (a.useless) r.strategy += " + les \xC3\xA9l\xC3\xA9ments devenus inutiles";
    r.summary = a.summary();
    for (const auto& it : a.removed) r.removed.push_back({std::string(nounOf(it.kind).one), display(it), it.why});
    for (const auto& it : a.direct) r.dependencies.push_back({"directe", display(it), it.why});
    for (const auto& it : a.indirect) r.dependencies.push_back({"indirecte", display(it), it.why});
    for (const auto& it : a.potential) r.dependencies.push_back({"potentielle", display(it), it.why});
    for (const auto& it : a.shared) r.dependencies.push_back({"partag\xC3\xA9" "e", display(it), it.why});
    for (const auto& it : a.equipments) r.dependencies.push_back({"\xC3\xA9quipement", display(it), it.why});
    r.consequences = a.fixes;
    for (const auto& it : a.orphans) r.consequences.push_back(std::string(nounOf(it.kind).one) + " " + display(it) + " : " + it.why);
    for (const auto& it : a.moved) r.consequences.push_back(std::string(nounOf(it.kind).one) + " " + display(it) + " : " + it.why);
    r.keep = keep;
    return r;
}

Confirmation confirmation(const Check& ck, const Proposal& p) {
    Confirmation c;
    if (ck.projectRemovals == 0) {
        c.title = "Supprimer " + std::to_string(ck.remove) + " objets";
        c.text = "L'Assistant propose de supprimer " + std::to_string(ck.remove) + " objets de la vue \xC2\xAB " + p.view
                 + " \xC2\xBB. Une suppression multiple se confirme.\nCtrl+Z la reprend d'un coup.";
        for (const auto& it : ck.items)
            if (it.what == ::app::ai::Item::What::Remove) c.items.emplace_back(it.name, std::string(hmi::kindLabel(it.kind)));
        c.confirm = "Supprimer les " + std::to_string(ck.remove) + " objets";
        return c;
    }
    std::size_t n = 0;
    for (const auto& r : ck.removals) n += r.removed.size();
    for (const auto& it : ck.items)
        if (it.what == ::app::ai::Item::What::Remove) ++n;
    c.title = "Supprimer " + std::to_string(n) + (n > 1 ? " \xC3\xA9l\xC3\xA9ments du projet" : " \xC3\xA9l\xC3\xA9ment du projet");
    c.text = "L'Assistant propose de supprimer des \xC3\xA9l\xC3\xA9ments du projet (pas seulement des objets) : une suppression se confirme toujours.";
    for (const auto& r : ck.removals) {
        c.text += "\n\n" + r.title + " (strat\xC3\xA9gie " + r.strategy + ")";
        for (std::size_t i = 0; i < r.summary.size() && i < 5; ++i) c.text += "\n\xE2\x80\xA2 " + r.summary[i];
    }
    c.text += "\n\nV\xC3\xA9rifi\xC3\xA9 sur une copie du projet (aucune r\xC3\xA9" "f\xC3\xA9rence restante). Ctrl+Z reprend tout d'un coup.";
    for (const auto& r : ck.removals)
        for (const auto& m : r.removed) c.items.emplace_back(m.name, m.what + (m.detail.empty() ? std::string{} : " \xE2\x80\x94 " + m.detail));
    for (const auto& it : ck.items)
        if (it.what == ::app::ai::Item::What::Remove)
            c.items.emplace_back(it.name, std::string(hmi::kindLabel(it.kind)) + (it.view.empty() ? std::string{} : " (" + it.view + ")"));
    c.confirm = "Supprimer (" + std::to_string(n) + ")";
    return c;
}

} // namespace app::ai::del
