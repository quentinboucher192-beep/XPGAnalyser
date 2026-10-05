// =============================================================================
//  hmi/HmiPackage.cpp - des vues qui voyagent (lot 20) : voir l'en-tete.
// =============================================================================
#include "HmiPackage.hpp"

#include "HmiAssets.hpp"
#include "HmiDesign.hpp"
#include "HmiExport.hpp"
#include "HmiStore.hpp"
#include "HmiSymbols.hpp"
#include "HmiTemplates.hpp"
#include "HmiTypes.hpp"
#include "../../third_party/miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <set>

namespace hmi::pkg {

namespace {

constexpr const char* kManifestFile = "paquet.txt";

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

bool isTemplateRoleName(std::string_view role) { return role == "modele" || role == "entete" || role == "pied"; }

// L'en-tete et le pied qu'une vue montre (kNoId dans la vue : le premier du projet).
Id headerOf(const Project& p, const View& v, bool footer) {
    if (footer ? !v.showFooter : !v.showHeader) return kNoId;
    const Id chosen = footer ? v.footer : v.header;
    if (chosen != kNoId) return chosen;
    const char* role = footer ? "pied" : "entete";
    for (const auto& x : p.views)
        if (x.role == role) return x.id;
    return kNoId;
}

// Les vues qu'une vue ouvre en popup (par leur nom).
void popupTargets(const Project& p, const View& v, std::vector<Id>& out) {
    const auto look = [&](const Action& a) {
        if (a.operation != Operation::Popup && a.operation != Operation::ChangePopup) return;
        if (const auto* t = p.viewByName(a.target)) out.push_back(t->id);
    };
    for (const auto& a : v.actions) look(a);
    for (const auto& o : v.objects)
        for (const auto& a : o.actions) look(a);
}

// Ce qui fait une vue, sans ses identifiants : deux vues "identiques" le sont
// a leurs identifiants pres (un paquet reimporte dans son projet d'origine).
std::string viewSignature(const View& v) {
    std::string s;
    s += v.role + "|" + std::to_string(v.width) + "x" + std::to_string(v.height) + "|" + v.background + "|" + v.description + "\n";
    const auto layerName = [&](Id id) {
        const auto* l = v.layer(id);
        return l ? l->name : std::string("-");
    };
    for (const auto& l : v.layers) s += "L " + l.name + (l.visible ? "" : " cache") + (l.locked ? " verrou" : "") + "\n";
    for (const auto& o : v.objects) {
        const auto* parent = v.object(o.parent);
        s += "O " + std::to_string(static_cast<int>(o.kind)) + " " + o.name + " @" + layerName(o.layer) + " ^" + (parent ? parent->name : "-")
           + (o.locked ? " verrou" : "") + (o.hidden ? " cache" : "") + "\n";
        for (const auto& pr : o.props) s += "  " + pr.key + "=" + pr.value + (pr.expr.empty() ? "" : " := " + pr.expr) + "\n";
        for (const auto& a : o.actions) s += "  A " + describeAction(a) + "|" + a.value + "|" + a.guard + "\n";
    }
    for (const auto& a : v.actions) s += "VA " + describeAction(a) + "|" + a.value + "|" + a.guard + "\n";
    for (const auto& sc : v.scripts) s += "S " + sc.name + " " + sc.event + "\n" + sc.body + "\n";
    for (const auto& pa : v.params) s += "P " + pa.name + "=" + pa.defaultValue + "\n";
    return s;
}

ItemKind kindOfView(const View& v, bool chosen) {
    if (chosen) return ItemKind::View;
    if (v.role == "symbole") return ItemKind::Symbol;
    if (isTemplateRoleName(v.role)) return ItemKind::Template;
    if (v.role == "popup") return ItemKind::Popup;
    return ItemKind::View;
}

bool chosenView(const Package& pkg, std::string_view name) {
    return std::find(pkg.manifest.views.begin(), pkg.manifest.views.end(), name) != pkg.manifest.views.end();
}

// Une copie de `v` aux identifiants neufs de `dst` (calques, objets, scripts).
View withNewIds(Project& dst, const View& v) {
    View copy = v;
    copy.id = dst.allocate();
    std::map<Id, Id> ids;
    for (auto& l : copy.layers) {
        const Id n = dst.allocate();
        ids[l.id] = n;
        l.id = n;
    }
    for (auto& o : copy.objects) ids[o.id] = dst.allocate();
    for (auto& o : copy.objects) {
        o.id = ids[o.id];
        if (ids.count(o.layer)) o.layer = ids[o.layer];
        if (ids.count(o.parent)) o.parent = ids[o.parent];
    }
    for (auto& sc : copy.scripts) sc.id = dst.allocate();
    for (auto& fn : copy.functions) fn.id = dst.allocate();   // 1.11.10 : les fonctions d'un symbole
    if (ids.count(copy.activeLayer)) copy.activeLayer = ids[copy.activeLayer];
    else if (!copy.layers.empty()) copy.activeLayer = copy.layers.front().id;
    return copy;
}

// Renommer une vue DANS LE PAQUET : les actions qui la citent (naviguer, popup)
// et, pour un symbole, ses instances suivent.
void renameViewIn(Project& src, const std::string& from, const std::string& to) {
    View* v = src.viewByName(from);
    if (!v) return;
    if (isSymbolView(*v)) {
        (void)renameSymbol(src, from, to);
        if (View* again = src.viewByName(from)) again->name = to;    // si renameSymbol ne l'a pas fait
    } else {
        v->name = to;
    }
    const auto fix = [&](Action& a) {
        if ((a.operation == Operation::Navigate || a.operation == Operation::Popup || a.operation == Operation::ChangePopup) && a.target == from)
            a.target = to;
    };
    for (auto& x : src.views) {
        for (auto& a : x.actions) fix(a);
        for (auto& o : x.objects)
            for (auto& a : o.actions) fix(a);
    }
}

// 1.11.2 : un nom de style libre dans le projet ET dans le paquet (Alerte_2, Alerte_3...).
std::string uniqueStyleName(const Project& dst, const Project& src, const std::string& base) {
    for (int n = 2; n < 1000; ++n) {
        const std::string name = base + "_" + std::to_string(n);
        const auto taken = [&](const Project& p) {
            return std::any_of(p.styles.begin(), p.styles.end(), [&](const Style& x) { return upper(x.name) == upper(name); });
        };
        if (!taken(dst) && !taken(src)) return name;
    }
    return base + "_import";
}

std::string resourceWord(MediaKind k, std::size_t n) {
    switch (k) {
        case MediaKind::Image: return n == 1 ? "image" : "images";
        case MediaKind::Sound: return n == 1 ? "son" : "sons";
        case MediaKind::Video: return n == 1 ? "vid\xC3\xA9o" : "vid\xC3\xA9os";
        case MediaKind::Font: return n == 1 ? "police" : "polices";
        case MediaKind::Document: return n == 1 ? "document" : "documents";      // lot API 8
        default: return n == 1 ? "fichier" : "fichiers";
    }
}

// 1.11.2 (decision 174) : un nom employe dans un texte (mot entier, sans casse,
// comme le ST) ; et le texte ou ce nom devient un autre.
bool usesWord(std::string_view text, std::string_view name) {
    if (text.empty() || name.empty()) return false;
    design::FindOptions o;
    o.wholeWord = true;
    std::size_t hits = 0;
    (void)design::replaced(text, name, name, o, &hits);
    return hits > 0;
}
std::string renamedWord(std::string_view text, std::string_view from, std::string_view to) {
    design::FindOptions o;
    o.wholeWord = true;
    return design::replaced(text, from, to, o);
}

const char* programKindWord(ProgramKind k) {
    switch (k) {
        case ProgramKind::Types: return "types";
        case ProgramKind::Functions: return "fonctions";
        case ProgramKind::Scripts: return "scripts";
    }
    return "types";
}

// Deux types, deux scripts "identiques" : a leurs identifiants pres.
bool sameOperators(const std::vector<HmiOperator>& a, const std::vector<HmiOperator>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].op != b[i].op || a[i].left != b[i].left || a[i].right != b[i].right || a[i].result != b[i].result || a[i].body != b[i].body)
            return false;
    return true;
}
bool sameType(const HmiType& a, const HmiType& b) {
    return a.kind == b.kind && a.members == b.members && a.values == b.values && sameOperators(a.operators, b.operators);
}
bool sameScript(const Script& a, const Script& b) {
    return a.lang == b.lang && a.event == b.event && a.body == b.body && a.periodMs == b.periodMs && a.watch == b.watch;
}

// Un nom libre dans le projet ET dans le paquet (T_Mode_2...), pour un type, une
// fonction, un script : les trois se citent par leur nom dans le meme ST.
std::string uniqueProgramName(const Project& dst, const Project& src, ItemKind k, const std::string& base) {
    const auto taken = [&](const Project& p, const std::string& name) {
        const auto same = [&](const std::string& x) { return upper(x) == upper(name); };
        switch (k) {
            case ItemKind::Type: return std::any_of(p.programs.types.begin(), p.programs.types.end(), [&](const HmiType& x) { return same(x.name); });
            case ItemKind::Function:
                return std::any_of(p.programs.functions.begin(), p.programs.functions.end(), [&](const HmiFunction& x) { return same(x.name); });
            default: return std::any_of(p.programs.scripts.begin(), p.programs.scripts.end(), [&](const Script& x) { return same(x.name); });
        }
    };
    for (int n = 2; n < 1000; ++n) {
        const std::string name = base + "_" + std::to_string(n);
        if (!taken(dst, name) && !taken(src, name)) return name;
    }
    return base + "_import";
}

bool chosenItem(const Package& pkg, std::string_view name) {
    return std::any_of(pkg.manifest.items.begin(), pkg.manifest.items.end(), [&](const std::string& x) { return upper(x) == upper(name); });
}

} // namespace

// ---------------------------------------------------------------- collect ----
Package collect(const Project& p, const std::vector<Id>& chosen) {
    Package out;
    out.manifest.fromProject = p.config.name;
    out.manifest.created = nowStamp();
    std::set<Id> in;
    std::deque<Id> todo(chosen.begin(), chosen.end());
    for (Id id : chosen)
        if (const auto* v = p.view(id)) out.manifest.views.push_back(v->name);
    while (!todo.empty()) {
        const Id id = todo.front();
        todo.pop_front();
        if (id == kNoId || in.count(id)) continue;
        const View* v = p.view(id);
        if (!v) continue;
        in.insert(id);
        if (v->templateView != kNoId) todo.push_back(v->templateView);
        todo.push_back(headerOf(p, *v, false));
        todo.push_back(headerOf(p, *v, true));
        for (const auto& o : v->objects)
            if (o.kind == Kind::SymbolInstance)
                if (const auto* s = symbolOf(p, o)) todo.push_back(s->id);
        std::vector<Id> popups;
        popupTargets(p, *v, popups);
        for (Id pid : popups) todo.push_back(pid);
        // 1.11.10 : un symbole emporte ses popups, meme celles qu'aucune action n'ouvre.
        if (isSymbolView(*v))
            for (const auto& w : p.views)
                if (w.ownerSymbol == v->id) todo.push_back(w.id);
    }
    Project& c = out.content;
    c = Project{};
    c.config = p.config;
    c.nextId = p.nextId;
    for (const auto& v : p.views)
        if (in.count(v.id)) c.views.push_back(v);
    // Les ressources citees par ces vues.
    for (const auto& r : p.assets.resources) {
        bool used = false;
        for (const auto& cite : citations(p, r.name))
            if (in.count(cite.view)) used = true;
        if (used) c.assets.resources.push_back(r);
    }
    // Les styles nommes de leurs objets.
    for (const auto& st : p.styles) {
        bool used = false;
        for (const auto& v : c.views)
            for (const auto& o : v.objects)
                if (const auto* cite = o.find("namedStyle"); cite && upper(cite->value) == upper(st.name)) used = true;
        if (used) c.styles.push_back(st);
    }
    // Les variables IHM et les fonctions qu'elles lisent, ecrivent, appellent.
    design::FindOptions opt;
    opt.wholeWord = true;
    const auto usedIn = [&](const std::string& name) {
        for (Id vid : in) {
            opt.view = vid;
            if (!design::find(p, name, "", opt).empty()) return true;
        }
        return false;
    };
    for (const auto& var : p.programs.variables)
        if (usedIn(var.name)) c.programs.variables.push_back(var);
    for (const auto& f : p.programs.functions)
        if (usedIn(f.name)) c.programs.functions.push_back(f);
    // Les types IHM de ces variables (et ceux de leurs membres), jusqu'au point fixe.
    std::set<std::string> wantedTypes;
    const auto wantTypesIn = [&](const std::string& typeText) {
        for (const auto& t : p.programs.types) {
            design::FindOptions o2;
            o2.wholeWord = true;
            std::size_t hits = 0;
            (void)design::replaced(typeText, t.name, t.name, o2, &hits);
            if (hits > 0) wantedTypes.insert(t.name);
        }
    };
    for (const auto& var : c.programs.variables) wantTypesIn(var.type);
    for (std::size_t guard = 0; guard < 8; ++guard) {
        const std::size_t before = wantedTypes.size();
        for (const auto& t : p.programs.types)
            if (wantedTypes.count(t.name))
                for (const auto& m : t.members) wantTypesIn(m.type);
        if (wantedTypes.size() == before) break;
    }
    for (const auto& t : p.programs.types)
        if (wantedTypes.count(t.name)) c.programs.types.push_back(t);
    return out;
}

bool isSymbolsPackage(const Package& pkg) noexcept { return pkg.manifest.kind == "symboles"; }

Package collectSymbols(const Project& p, const std::vector<Id>& symbols) {
    std::vector<Id> only;
    for (Id id : symbols)
        if (const auto* v = p.view(id); v && isSymbolView(*v) && std::find(only.begin(), only.end(), id) == only.end()) only.push_back(id);
    Package out = collect(p, only);
    out.manifest.kind = "symboles";
    // Les methodes d'objet (decision 156) : declarees par objet, elles sont dans
    // les objets des symboles, donc deja dans out.content. Si API-M les range
    // ailleurs (une liste du projet), c'est ICI qu'on ajoute celles des objets
    // de ces symboles (voir l'en-tete).
    return out;
}

// 1.11.2 (decision 174) : les types, fonctions ou scripts choisis et ce dont ils ont besoin.
bool isProgramsPackage(const Package& pkg) noexcept {
    const auto& k = pkg.manifest.kind;
    return k == "types" || k == "fonctions" || k == "scripts";
}

std::string_view extensionOf(const Package& pkg) noexcept {
    const auto& k = pkg.manifest.kind;
    if (k == "symboles") return kSymbolsExtension;
    if (k == "types") return kTypesExtension;
    if (k == "fonctions") return kFunctionsExtension;
    if (k == "scripts") return kScriptsExtension;
    if (k == "modele") return kTemplateExtension;
    return kViewsExtension;
}

std::string importTitle(const Package& pkg) {
    const auto& k = pkg.manifest.kind;
    if (k == "symboles") return "Importer des symboles";
    if (k == "types") return "Importer des types IHM";
    if (k == "fonctions") return "Importer des fonctions IHM";
    if (k == "scripts") return "Importer des scripts g\xC3\xA9n\xC3\xA9raux";
    if (k == "modele") return "Importer un mod\xC3\xA8le";
    return "Importer des vues";
}

Package collectPrograms(const Project& p, ProgramKind kind, const std::vector<Id>& chosen) {
    Package out;
    out.manifest.kind = programKindWord(kind);
    out.manifest.fromProject = p.config.name;
    out.manifest.created = nowStamp();
    std::set<std::string> types, functions, scripts, variables;   // en majuscules
    for (Id id : chosen) {
        const auto pick = [&](std::set<std::string>& set, const std::string& name) {
            if (set.insert(upper(name)).second) out.manifest.items.push_back(name);
        };
        switch (kind) {
            case ProgramKind::Types:
                for (const auto& t : p.programs.types) if (t.id == id) pick(types, t.name);
                break;
            case ProgramKind::Functions:
                for (const auto& f : p.programs.functions) if (f.id == id) pick(functions, f.name);
                break;
            case ProgramKind::Scripts:
                for (const auto& sc : p.programs.scripts) if (sc.id == id) pick(scripts, sc.name);
                break;
        }
    }
    // De proche en proche : ce que les textes des elements retenus emploient
    // (les types de leurs membres, de leurs operateurs, de leurs variables ;
    // les fonctions appelees ; les variables IHM lues ou ecrites).
    for (std::size_t guard = 0; guard < 64; ++guard) {
        const std::size_t before = types.size() + functions.size() + variables.size();
        std::vector<std::string_view> texts;
        for (const auto& t : p.programs.types)
            if (types.count(upper(t.name))) {
                for (const auto& m : t.members) texts.push_back(m.type);
                for (const auto& o : t.operators) {
                    texts.push_back(o.left);
                    texts.push_back(o.right);
                    texts.push_back(o.result);
                    texts.push_back(o.body);
                }
            }
        for (const auto& f : p.programs.functions)
            if (functions.count(upper(f.name))) {
                texts.push_back(f.returnType);
                texts.push_back(f.body);
            }
        for (const auto& sc : p.programs.scripts)
            if (scripts.count(upper(sc.name))) {
                texts.push_back(sc.body);
                texts.push_back(sc.watch);
            }
        for (const auto& v : p.programs.variables)
            if (variables.count(upper(v.name))) texts.push_back(v.type);
        const auto used = [&](const std::string& name) {
            return std::any_of(texts.begin(), texts.end(), [&](std::string_view x) { return usesWord(x, name); });
        };
        for (const auto& t : p.programs.types)
            if (!types.count(upper(t.name)) && used(t.name)) types.insert(upper(t.name));
        for (const auto& f : p.programs.functions)
            if (!functions.count(upper(f.name)) && used(f.name)) functions.insert(upper(f.name));
        for (const auto& v : p.programs.variables)
            if (!variables.count(upper(v.name)) && used(v.name)) variables.insert(upper(v.name));
        if (types.size() + functions.size() + variables.size() == before) break;
    }
    Project& c = out.content;
    c = Project{};
    c.config = p.config;
    c.nextId = p.nextId;
    for (const auto& t : p.programs.types)
        if (types.count(upper(t.name))) c.programs.types.push_back(t);
    for (const auto& f : p.programs.functions)
        if (functions.count(upper(f.name))) c.programs.functions.push_back(f);
    for (const auto& sc : p.programs.scripts)
        if (scripts.count(upper(sc.name))) c.programs.scripts.push_back(sc);
    for (const auto& v : p.programs.variables)
        if (variables.count(upper(v.name))) c.programs.variables.push_back(v);
    return out;
}

Contents contentsOf(const Package& pkg) {
    Contents n;
    const bool symbolsPkg = isSymbolsPackage(pkg);
    for (const auto& v : pkg.content.views) {
        if (symbolsPkg && chosenView(pkg, v.name) && isSymbolView(v)) { ++n.chosenSymbols; continue; }
        if (chosenView(pkg, v.name)) { ++n.views; continue; }
        switch (kindOfView(v, false)) {
            case ItemKind::Symbol: ++n.symbols; break;
            case ItemKind::Template: ++n.templates; break;
            case ItemKind::Popup: ++n.popups; break;
            default: ++n.views; break;
        }
    }
    n.resources = pkg.content.assets.resources.size();
    n.styles = pkg.content.styles.size();
    n.variables = pkg.content.programs.variables.size();
    n.types = pkg.content.programs.types.size();
    n.functions = pkg.content.programs.functions.size();
    n.scripts = pkg.content.programs.scripts.size();       // 1.11.2 (decision 174)
    n.chosen = pkg.manifest.items.size();
    return n;
}

std::string contentsText(const Package& pkg, bool withViews) {
    const auto n = contentsOf(pkg);
    std::vector<std::string> parts;
    if (isProgramsPackage(pkg)) {
        // 1.11.2 (decision 174) : "1 fonction", puis ce qu'elle emporte
        // ("2 fonctions appel\xC3\xA9es, 1 type IHM, 3 variables").
        const auto& k = pkg.manifest.kind;
        const std::size_t extra = [&] {
            const std::size_t all = k == "types" ? n.types : k == "fonctions" ? n.functions : n.scripts;
            return all > n.chosen ? all - n.chosen : 0;
        }();
        if (k == "types") {
            if (withViews) parts.push_back(plural(n.chosen, "type IHM", "types IHM"));
            if (extra) parts.push_back(plural(extra, "type IHM employ\xC3\xA9", "types IHM employ\xC3\xA9s"));
            if (n.functions) parts.push_back(plural(n.functions, "fonction", "fonctions"));
        } else if (k == "fonctions") {
            if (withViews) parts.push_back(plural(n.chosen, "fonction", "fonctions"));
            if (extra) parts.push_back(plural(extra, "fonction appel\xC3\xA9" "e", "fonctions appel\xC3\xA9" "es"));
            if (n.types) parts.push_back(plural(n.types, "type IHM", "types IHM"));
        } else {
            if (withViews) parts.push_back(plural(n.chosen, "script", "scripts"));
            if (n.functions) parts.push_back(plural(n.functions, "fonction", "fonctions"));
            if (n.types) parts.push_back(plural(n.types, "type IHM", "types IHM"));
        }
        if (n.variables) parts.push_back(plural(n.variables, "variable", "variables"));
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
        return out.empty() ? std::string("rien d'autre") : out;
    }
    if (isSymbolsPackage(pkg)) {
        // 1.11.2 : "2 symboles" (les choisis), puis ceux qu'ils contiennent.
        if (withViews) parts.push_back(plural(n.chosenSymbols, "symbole", "symboles"));
        if (withViews && n.views) parts.push_back(plural(n.views, "vue", "vues"));
        if (n.symbols) parts.push_back(plural(n.symbols, "symbole imbriqu\xC3\xA9", "symboles imbriqu\xC3\xA9s"));
    } else {
        if (withViews) parts.push_back(plural(n.views, "vue", "vues"));
        if (n.symbols) parts.push_back(plural(n.symbols, "symbole", "symboles"));
    }
    if (n.popups) parts.push_back(plural(n.popups, "popup", "popups"));
    if (n.templates) parts.push_back(plural(n.templates, "\xC3\xA9" "cran mod\xC3\xA8le", "\xC3\xA9" "crans mod\xC3\xA8les"));
    std::map<MediaKind, std::size_t> kinds;
    for (const auto& r : pkg.content.assets.resources) ++kinds[r.kind()];
    for (const auto& [k, count] : kinds) parts.push_back(std::to_string(count) + " " + resourceWord(k, count));
    if (n.styles) parts.push_back(plural(n.styles, "style", "styles"));
    if (n.types) parts.push_back(plural(n.types, "type IHM", "types IHM"));
    if (n.functions) parts.push_back(plural(n.functions, "fonction", "fonctions"));
    if (n.variables) parts.push_back(plural(n.variables, "variable", "variables"));
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
    return out.empty() ? std::string("rien d'autre") : out;
}

// -------------------------------------------------------------------- zip ----
Bytes toZip(const Package& pkg) {
    const auto& m = pkg.manifest;
    // 1.11.2 : le format du paquet et la version qui l'a ecrit (une version
    // d'avant les ignore ; une version d'apres refuse un format qu'elle ne connait pas).
    std::string text = std::string("# XpgAnalyzer - paquet de ") + (m.kind == "modele" || m.kind.empty() ? std::string("vues") : m.kind) + " (format "
                     + std::to_string(kPackageFormat) + ")\n";
    text += "paquet format=" + std::to_string(kPackageFormat) + " version=" + quote(kWriterVersion) + " genre=" + quote(m.kind) + " nom=" + quote(m.name) + " categorie=" + quote(m.category)
          + " description=" + quote(m.description) + " projet=" + quote(m.fromProject) + " poste=" + quote(m.fromStation)
          + " cree=" + quote(m.created) + " variables=" + (m.askVariables ? "choisir" : "garder") + "\n";
    for (const auto& v : m.views) text += "vue nom=" + quote(v) + "\n";
    for (const auto& x : m.items) text += "element nom=" + quote(x) + "\n";   // 1.11.2 (une 1.11.1 l'ignore)
    text += "fin\n";
    std::vector<std::pair<std::string, std::string>> items;
    items.emplace_back(kManifestFile, text);
    for (const auto& f : serializeProject(pkg.content))
        if (f.data) items.emplace_back("ihm/" + f.path, std::string(f.data->begin(), f.data->end()));
    return zipStored(items);
}

core::Result<Package> fromZip(const Bytes& bytes) {
    std::map<std::string, std::string> files;
    {
        mz_zip_archive zip;
        std::memset(&zip, 0, sizeof zip);
        if (bytes.empty() || !mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
            return core::fail(core::ErrorCode::FileUnreadable, "pas un paquet de vues (le fichier n'est pas un zip)");
        const mz_uint n = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
            std::size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (!data) continue;
            std::string name = st.m_filename;
            for (auto& ch : name) if (ch == '\\') ch = '/';
            files[name].assign(static_cast<const char*>(data), size);
            mz_free(data);
        }
        mz_zip_reader_end(&zip);
    }
    const auto man = files.find(kManifestFile);
    if (man == files.end()) return core::fail(core::ErrorCode::FileUnreadable, "pas un paquet de vues (paquet.txt absent)");
    Package pkg;
    std::size_t pos = 0;
    const std::string& m = man->second;
    while (pos < m.size()) {
        const std::size_t eol = m.find('\n', pos);
        std::string_view line(m.data() + pos, (eol == std::string::npos ? m.size() : eol) - pos);
        pos = eol == std::string::npos ? m.size() : eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        Record r;
        std::string error;
        if (!parseRecord(line, r, error)) return core::fail(core::ErrorCode::XmlMalformed, "paquet.txt : " + error);
        const auto get = [&](const char* key) { const auto* v = r.get(key); return v ? *v : std::string{}; };
        if (r.word == "paquet") {
            const std::string f = get("format");
            pkg.manifest.format = f.empty() ? 1 : std::atoi(f.c_str());
            pkg.manifest.writer = get("version");
            pkg.manifest.kind = get("genre");
            pkg.manifest.name = get("nom");
            pkg.manifest.category = get("categorie");
            pkg.manifest.description = get("description");
            pkg.manifest.fromProject = get("projet");
            pkg.manifest.fromStation = get("poste");
            pkg.manifest.created = get("cree");
            pkg.manifest.askVariables = get("variables") == "choisir";
        } else if (r.word == "vue") {
            pkg.manifest.views.push_back(get("nom"));
        } else if (r.word == "element") {
            pkg.manifest.items.push_back(get("nom"));   // 1.11.2 (decision 174)
        }
    }
    // 1.11.2 : un paquet d'une version plus recente est refuse, en le disant.
    const auto newer = [&](const std::string& what) {
        return core::fail(core::ErrorCode::XmlUnsupportedDtd,
                          "ce fichier vient d'une version plus r\xC3\xA9" "cente de XpgAnalyzer"
                              + (pkg.manifest.writer.empty() ? std::string{} : " (" + pkg.manifest.writer + ")") + " : cette version ("
                              + std::string(kWriterVersion) + ") ne sait pas le lire (" + what
                              + "). Rien n'est import\xC3\xA9 ; mets l'application \xC3\xA0 jour pour l'importer.");
    };
    if (pkg.manifest.format > kPackageFormat)
        return newer("paquet au format " + std::to_string(pkg.manifest.format) + ", elle lit jusqu'au format " + std::to_string(kPackageFormat));
    {
        // Le format IHM du petit projet (ihm/ihm.txt, "ihm format=23") : lu ici
        // pour le dire en clair (parseProject le refuserait aussi).
        const auto idx = files.find("ihm/ihm.txt");
        if (idx != files.end()) {
            const std::string& t = idx->second;
            const auto at = t.compare(0, 11, "ihm format=") == 0 ? 0 : t.find("\nihm format=");
            if (at != std::string::npos) {
                const int f = std::atoi(t.c_str() + at + (at == 0 ? 11 : 12));
                if (f > kFormatVersion)
                    return newer("projet IHM au format " + std::to_string(f) + ", elle lit jusqu'au format " + std::to_string(kFormatVersion));
            }
        }
    }
    const auto read = [&files](const std::string& path, std::string& content) {
        const auto it = files.find("ihm/" + path);
        if (it == files.end()) return false;
        content = it->second;
        return true;
    };
    auto project = parseProject(read);
    if (!project && project.error().code == core::ErrorCode::XmlUnsupportedDtd) return newer(project.error().context);
    if (!project) return core::fail(project.error().code, "paquet : " + project.error().context);
    pkg.content = std::move(*project);
    if (pkg.manifest.views.empty() && !pkg.content.views.empty()) pkg.manifest.views.push_back(pkg.content.views.front().name);
    return pkg;
}

core::Status writeFile(const Package& pkg, const std::string& path) {
    const Bytes data = toZip(pkg);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + path);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + path);
    return core::ok();
}

core::Result<Package> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return core::fail(core::ErrorCode::FileNotFound, "fichier introuvable : " + path);
    const Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromZip(data);
}

bool tooNew(const core::Error& e) noexcept { return e.code == core::ErrorCode::XmlUnsupportedDtd; }

// ------------------------------------------------------------------- plan ----
std::string_view itemKindLabel(ItemKind k) noexcept {
    switch (k) {
        case ItemKind::View: return "vue";
        case ItemKind::Symbol: return "symbole";
        case ItemKind::Template: return "\xC3\xA9" "cran mod\xC3\xA8le";
        case ItemKind::Popup: return "popup";
        case ItemKind::Resource: return "ressource";
        case ItemKind::Style: return "style";
        case ItemKind::Type: return "type IHM";
        case ItemKind::Function: return "fonction";
        case ItemKind::Variable: return "variable";
        case ItemKind::Script: return "script";
    }
    return "";
}

std::string_view choiceLabel(Choice c) noexcept {
    switch (c) {
        case Choice::Add: return "ajout\xC3\xA9";
        case Choice::KeepOurs: return "garder celui du projet";
        case Choice::Rename: return "importer sous un autre nom";
        case Choice::Replace: return "remplacer celui du projet";
        case Choice::Skip: return "ne pas cr\xC3\xA9" "er";
    }
    return "";
}

std::vector<Choice> Item::choices() const {
    if (state == State::Missing) return kind == ItemKind::Variable ? std::vector<Choice>{Choice::Add, Choice::Skip} : std::vector<Choice>{Choice::Add};
    if (state == State::Same) return {Choice::KeepOurs};
    switch (kind) {
        case ItemKind::View: case ItemKind::Symbol: case ItemKind::Template: case ItemKind::Popup: case ItemKind::Resource:
            return {Choice::Rename, Choice::Replace, Choice::KeepOurs};
        case ItemKind::Style:   // 1.11.2 (decision 162) : un style en conflit se renomme aussi (ses objets suivent)
            return {Choice::KeepOurs, Choice::Replace, Choice::Rename};
        case ItemKind::Type: case ItemKind::Function: case ItemKind::Script:
            // 1.11.2 (decision 174) : renommer (les citations du paquet suivent) ou remplacer, au choix.
            return {Choice::Rename, Choice::Replace, Choice::KeepOurs};
        case ItemKind::Variable:
            return {Choice::KeepOurs};
    }
    return {Choice::KeepOurs};
}

std::size_t Plan::count(State s) const {
    return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [&](const Item& i) { return i.state == s; }));
}

Item* Plan::find(ItemKind k, std::string_view name) {
    for (auto& i : items)
        if (i.kind == k && i.name == name) return &i;
    // Une vue choisie peut aussi etre cherchee comme popup ou symbole : par son nom seul.
    for (auto& i : items)
        if (i.name == name && (k == ItemKind::View || k == ItemKind::Symbol || k == ItemKind::Template || k == ItemKind::Popup)
            && (i.kind == ItemKind::View || i.kind == ItemKind::Symbol || i.kind == ItemKind::Template || i.kind == ItemKind::Popup))
            return &i;
    return nullptr;
}
const Item* Plan::find(ItemKind k, std::string_view name) const { return const_cast<Plan*>(this)->find(k, name); }

Plan plan(const Project& dst, const Package& pkg) {
    Plan pl;
    const auto& src = pkg.content;
    for (const auto& v : src.views) {
        Item it;
        // 1.11.2 : un symbole choisi d'un paquet de symboles se dit "symbole".
        it.kind = kindOfView(v, chosenView(pkg, v.name) && !(isSymbolsPackage(pkg) && isSymbolView(v)));
        it.name = v.name;
        const View* ours = dst.viewByName(v.name);
        if (!ours) {
            it.state = State::Missing;
            it.choice = Choice::Add;
            it.detail = "absente du projet";
        } else if (viewSignature(*ours) == viewSignature(v)) {
            it.state = State::Same;
            it.choice = Choice::KeepOurs;
            it.detail = "identique, gard\xC3\xA9" "e";
        } else {
            it.state = State::Different;
            it.choice = Choice::Rename;
            it.newName = uniqueViewName(dst, v.name);
            it.detail = "diff\xC3\xA9rente (" + plural(v.objects.size(), "objet", "objets") + ", " + std::to_string(ours->objects.size())
                      + " dans le projet)";
        }
        pl.items.push_back(std::move(it));
    }
    for (const auto& r : src.assets.resources) {
        Item it;
        it.kind = ItemKind::Resource;
        it.name = r.name;
        const Resource* ours = dst.resourceByName(r.name);
        const bool same = ours && ((ours->data && r.data && *ours->data == *r.data) || (!ours->data && !r.data));
        if (!ours) { it.state = State::Missing; it.choice = Choice::Add; it.detail = "absente du projet"; }
        else if (same) { it.state = State::Same; it.choice = Choice::KeepOurs; it.detail = "identique, gard\xC3\xA9" "e"; }
        else {
            it.state = State::Different;
            it.choice = Choice::Rename;
            it.newName = uniqueResourceName(dst, r.name);
            it.detail = "diff\xC3\xA9rente (m\xC3\xAAme nom, autre contenu)";
        }
        pl.items.push_back(std::move(it));
    }
    for (const auto& st : src.styles) {
        Item it;
        it.kind = ItemKind::Style;
        it.name = st.name;
        const Style* ours = dst.styleByName(st.name);
        if (!ours) { it.state = State::Missing; it.choice = Choice::Add; it.detail = "absent du projet"; }
        else if (ours->props == st.props) { it.state = State::Same; it.choice = Choice::KeepOurs; it.detail = "identique, gard\xC3\xA9"; }
        else {
            it.state = State::Different;
            it.choice = Choice::KeepOurs;
            it.detail = "diff\xC3\xA9rent : celui du projet est gard\xC3\xA9";
            it.newName = uniqueStyleName(dst, src, st.name);   // 1.11.2 : si on choisit de le renommer
        }
        pl.items.push_back(std::move(it));
    }
    for (const auto& t : src.programs.types) {
        Item it;
        it.kind = ItemKind::Type;
        it.name = t.name;
        const HmiType* ours = dst.hmiTypeByName(t.name);
        if (!ours) { it.state = State::Missing; it.choice = Choice::Add; it.detail = "absent du projet"; }
        else if (sameType(*ours, t)) { it.state = State::Same; it.choice = Choice::KeepOurs; it.detail = "identique, gard\xC3\xA9"; }
        else {
            it.state = State::Different;
            it.newName = uniqueProgramName(dst, src, ItemKind::Type, t.name);   // 1.11.2 (decision 174)
            // Un type choisi (paquet de types) : sous un autre nom par defaut ; un type employe : celui du projet.
            const bool chosen = pkg.manifest.kind == "types" && chosenItem(pkg, t.name);
            it.choice = chosen ? Choice::Rename : Choice::KeepOurs;
            it.detail = chosen ? "diff\xC3\xA9rent : import\xC3\xA9 sous " + it.newName : "diff\xC3\xA9rent : celui du projet est gard\xC3\xA9";
        }
        pl.items.push_back(std::move(it));
    }
    for (const auto& f : src.programs.functions) {
        Item it;
        it.kind = ItemKind::Function;
        it.name = f.name;
        const HmiFunction* ours = dst.functionByName(f.name);
        if (!ours) { it.state = State::Missing; it.choice = Choice::Add; it.detail = "absente du projet"; }
        else if (ours->body == f.body && ours->returnType == f.returnType) { it.state = State::Same; it.choice = Choice::KeepOurs; it.detail = "identique, gard\xC3\xA9" "e"; }
        else {
            it.state = State::Different;
            it.newName = uniqueProgramName(dst, src, ItemKind::Function, f.name);   // 1.11.2 (decision 174)
            const bool chosen = pkg.manifest.kind == "fonctions" && chosenItem(pkg, f.name);
            it.choice = chosen ? Choice::Rename : Choice::KeepOurs;
            it.detail = chosen ? "diff\xC3\xA9rente : import\xC3\xA9" "e sous " + it.newName : "diff\xC3\xA9rente : celle du projet est gard\xC3\xA9" "e";
        }
        pl.items.push_back(std::move(it));
    }
    // 1.11.2 (decision 174) : les scripts generaux (un paquet de scripts).
    for (const auto& sc : src.programs.scripts) {
        Item it;
        it.kind = ItemKind::Script;
        it.name = sc.name;
        const Script* ours = dst.generalScript(sc.name);
        if (!ours) { it.state = State::Missing; it.choice = Choice::Add; it.detail = "absent du projet"; }
        else if (sameScript(*ours, sc)) { it.state = State::Same; it.choice = Choice::KeepOurs; it.detail = "identique, gard\xC3\xA9"; }
        else {
            it.state = State::Different;
            it.newName = uniqueProgramName(dst, src, ItemKind::Script, sc.name);
            it.choice = Choice::Rename;
            it.detail = "diff\xC3\xA9rent : import\xC3\xA9 sous " + it.newName;
        }
        pl.items.push_back(std::move(it));
    }
    for (const auto& var : src.programs.variables) {
        Item it;
        it.kind = ItemKind::Variable;
        it.name = var.name;
        const Variable* ours = dst.variable(var.name);
        if (!ours) { it.state = State::Missing; it.choice = Choice::Add; it.detail = "absente : cr\xC3\xA9\xC3\xA9" "e (" + var.type + ")"; }
        else if (upper(ours->type) == upper(var.type)) { it.state = State::Same; it.choice = Choice::KeepOurs; it.detail = "d\xC3\xA9j\xC3\xA0 l\xC3\xA0 (" + ours->type + ")"; }
        else { it.state = State::Different; it.choice = Choice::KeepOurs; it.detail = "d\xC3\xA9j\xC3\xA0 l\xC3\xA0, en " + ours->type + " (le paquet : " + var.type + ")"; }
        pl.items.push_back(std::move(it));
    }
    return pl;
}

// ----------------------------------------------------------------- import ----
std::string ImportResult::summary() const {
    std::vector<std::string> parts;
    if (!added.empty()) parts.push_back(std::to_string(added.size()) + " ajout\xC3\xA9(s)");
    if (!renamed.empty()) parts.push_back(std::to_string(renamed.size()) + " sous un autre nom");
    if (!replaced.empty()) parts.push_back(std::to_string(replaced.size()) + " remplac\xC3\xA9(s)");
    if (!kept.empty()) parts.push_back(std::to_string(kept.size()) + " d\xC3\xA9j\xC3\xA0 l\xC3\xA0 (gard\xC3\xA9s)");
    if (!skipped.empty()) parts.push_back(std::to_string(skipped.size()) + " non cr\xC3\xA9\xC3\xA9(s)");
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
    return out;
}

ImportResult importInto(Project& dst, const Package& pkg, const Plan& pl) {
    ImportResult res;
    Project src = pkg.content;
    // Les choix par vue (par leur nom d'origine, avant les renommages).
    std::map<Id, const Item*> itemOfView;
    for (const auto& v : src.views)
        if (const auto* it = pl.find(ItemKind::View, v.name)) itemOfView[v.id] = it;
    std::map<std::string, std::string> chosenRenamed;      // vue choisie : nom d'origine -> nom retenu
    // 1. Les renommages, DANS LE PAQUET : les citations suivent (actions,
    //    instances, images citees, styles des objets).
    for (const auto& it : pl.items) {
        if (it.choice != Choice::Rename || it.newName.empty() || it.newName == it.name) continue;
        switch (it.kind) {
            case ItemKind::View: case ItemKind::Symbol: case ItemKind::Template: case ItemKind::Popup:
                renameViewIn(src, it.name, it.newName);
                chosenRenamed[it.name] = it.newName;
                break;
            case ItemKind::Resource:
                for (auto& r : src.assets.resources)
                    if (r.name == it.name) { (void)renameResource(src, r.id, it.newName); break; }
                break;
            case ItemKind::Style:
                // 1.11.2 : le style et les objets du paquet qui le portent.
                for (auto& st : src.styles)
                    if (upper(st.name) == upper(it.name)) st.name = it.newName;
                for (auto& v : src.views)
                    for (auto& o : v.objects)
                        for (auto& pr : o.props)
                            if (pr.key == "namedStyle" && upper(pr.value) == upper(it.name)) pr.value = it.newName;
                break;
            case ItemKind::Type: case ItemKind::Function: {
                // 1.11.2 (decision 174) : le type ou la fonction, et tout ce qui le cite
                // dans le paquet (vues, scripts, corps des fonctions - le sien aussi :
                // une fonction rend sa valeur en l'affectant a son nom -, membres et
                // operateurs des types, types des variables, retours des fonctions).
                design::FindOptions o;
                o.wholeWord = true;
                (void)design::replaceAll(src, it.name, it.newName, o);
                for (auto& t : src.programs.types) {
                    if (it.kind == ItemKind::Type && upper(t.name) == upper(it.name)) t.name = it.newName;
                    for (auto& m : t.members) m.type = renamedWord(m.type, it.name, it.newName);
                    for (auto& op : t.operators) {
                        op.left = renamedWord(op.left, it.name, it.newName);
                        op.right = renamedWord(op.right, it.name, it.newName);
                        op.result = renamedWord(op.result, it.name, it.newName);
                        op.body = renamedWord(op.body, it.name, it.newName);
                    }
                }
                for (auto& f : src.programs.functions) {
                    if (it.kind == ItemKind::Function && upper(f.name) == upper(it.name)) f.name = it.newName;
                    f.returnType = renamedWord(f.returnType, it.name, it.newName);
                    f.body = renamedWord(f.body, it.name, it.newName);
                }
                for (auto& sc : src.programs.scripts) sc.body = renamedWord(sc.body, it.name, it.newName);
                for (auto& v : src.programs.variables) v.type = renamedWord(v.type, it.name, it.newName);
                break;
            }
            case ItemKind::Script:
                for (auto& sc : src.programs.scripts)
                    if (upper(sc.name) == upper(it.name)) sc.name = it.newName;
                break;
            default: break;
        }
    }
    // 2. Les vues : ajoutees (identifiants neufs), remplacees, ou gardees.
    std::map<Id, Id> viewMap;       // identifiant dans le paquet -> dans le projet
    std::vector<std::pair<Id, Id>> placed;   // (paquet, projet) des vues ecrites (a relier)
    for (const auto& sv : src.views) {
        const Item* it = itemOfView.count(sv.id) ? itemOfView[sv.id] : nullptr;
        const Choice choice = it ? it->choice : Choice::Add;
        const std::string origin = it ? it->name : sv.name;
        const std::string what = std::string(it ? itemKindLabel(it->kind) : std::string_view("vue")) + " " + origin;
        if (choice == Choice::KeepOurs) {
            if (const auto* ours = dst.viewByName(origin)) viewMap[sv.id] = ours->id;
            res.kept.push_back(what);
            continue;
        }
        if (choice == Choice::Replace) {
            if (View* ours = dst.viewByName(origin)) {
                View copy = withNewIds(dst, sv);
                copy.id = ours->id;
                copy.name = ours->name;
                *ours = std::move(copy);
                viewMap[sv.id] = ours->id;
                placed.emplace_back(sv.id, ours->id);
                res.replaced.push_back(what);
                continue;
            }
        }
        View copy = withNewIds(dst, sv);
        viewMap[sv.id] = copy.id;
        placed.emplace_back(sv.id, copy.id);
        if (choice == Choice::Rename) res.renamed.push_back(what + " \xE2\x86\x92 " + copy.name);
        else res.added.push_back(what);
        dst.views.push_back(std::move(copy));
    }
    // 3. Les liens entre vues (ecran modele, en-tete, pied, vue parente).
    const auto relink = [&](Id id) -> Id {
        if (id == kNoId) return kNoId;
        const auto m = viewMap.find(id);
        return m == viewMap.end() ? kNoId : m->second;
    };
    for (const auto& [from, to] : placed) {
        (void)from;
        if (View* v = dst.view(to)) {
            v->templateView = relink(v->templateView);
            v->header = relink(v->header);
            v->footer = relink(v->footer);
            v->upView = relink(v->upView);
            v->ownerSymbol = relink(v->ownerSymbol);   // 1.11.10 : la popup suit son symbole
        }
    }
    // 4. Les ressources.
    for (const auto& r : src.assets.resources) {
        const Item* it = pl.find(ItemKind::Resource, r.name);
        // Apres un renommage, l'element se retrouve par son nouveau nom.
        if (!it)
            for (const auto& x : pl.items)
                if (x.kind == ItemKind::Resource && x.newName == r.name) it = &x;
        const Choice choice = it ? it->choice : Choice::Add;
        const std::string origin = it ? it->name : r.name;
        if (choice == Choice::KeepOurs) { res.kept.push_back("ressource " + origin); continue; }
        if (choice == Choice::Replace) {
            for (auto& ours : dst.assets.resources)
                if (ours.name == origin) {
                    const Id keep = ours.id;
                    ours = r;
                    ours.id = keep;
                    ours.name = origin;
                }
            res.replaced.push_back("ressource " + origin);
            continue;
        }
        Resource copy = r;
        copy.id = dst.allocate();
        dst.assets.resources.push_back(std::move(copy));
        if (choice == Choice::Rename) res.renamed.push_back("ressource " + origin + " \xE2\x86\x92 " + r.name);
        else res.added.push_back("ressource " + r.name);
    }
    // 5. Les styles, les types, les fonctions : ajoutes, remplaces, ou gardes.
    for (const auto& st : src.styles) {
        const Item* it = pl.find(ItemKind::Style, st.name);
        // 1.11.2 : apres un renommage, le style se retrouve par son nouveau nom.
        if (!it)
            for (const auto& x : pl.items)
                if (x.kind == ItemKind::Style && x.choice == Choice::Rename && x.newName == st.name) it = &x;
        const Choice choice = it ? it->choice : Choice::Add;
        if (choice == Choice::Rename) {
            Style copy = st;
            copy.id = dst.allocate();
            dst.styles.push_back(std::move(copy));
            res.renamed.push_back("style " + (it ? it->name : st.name) + " \xE2\x86\x92 " + st.name);
            continue;
        }
        if (choice == Choice::KeepOurs) { res.kept.push_back("style " + st.name); continue; }
        if (choice == Choice::Replace) {
            for (auto& ours : dst.styles)
                if (upper(ours.name) == upper(st.name)) ours.props = st.props;
            res.replaced.push_back("style " + st.name);
            continue;
        }
        Style copy = st;
        copy.id = dst.allocate();
        dst.styles.push_back(std::move(copy));
        res.added.push_back("style " + st.name);
    }
    // 1.11.2 : apres un renommage, un type, une fonction, un script se retrouvent par leur nouveau nom.
    const auto itemOf = [&](ItemKind k, const std::string& name) -> const Item* {
        if (const Item* it = pl.find(k, name); it && !(it->choice == Choice::Rename && upper(it->newName) != upper(name))) return it;
        for (const auto& x : pl.items)
            if (x.kind == k && x.choice == Choice::Rename && upper(x.newName) == upper(name)) return &x;
        return pl.find(k, name);
    };
    for (const auto& t : src.programs.types) {
        const Item* it = itemOf(ItemKind::Type, t.name);
        const Choice choice = it ? it->choice : Choice::Add;
        if (choice == Choice::KeepOurs) { res.kept.push_back("type " + t.name); continue; }
        if (choice == Choice::Replace) {
            for (auto& ours : dst.programs.types)
                if (upper(ours.name) == upper(t.name)) {
                    // 1.11.2 : tout le type (une enumeration : ses valeurs), sauf son identifiant, son nom, son dossier.
                    const Id keepId = ours.id;
                    const std::string keepName = ours.name, keepFolder = ours.folder;
                    ours = t;
                    ours.id = keepId;
                    ours.name = keepName;
                    ours.folder = keepFolder;
                }
            res.replaced.push_back("type " + t.name);
            continue;
        }
        HmiType copy = t;
        copy.id = dst.allocate();
        for (auto& op : copy.operators) op.id = dst.allocate();
        dst.programs.types.push_back(std::move(copy));
        if (choice == Choice::Rename && it) res.renamed.push_back("type " + it->name + " \xE2\x86\x92 " + t.name);
        else res.added.push_back("type " + t.name);
    }
    for (const auto& f : src.programs.functions) {
        const Item* it = itemOf(ItemKind::Function, f.name);
        const Choice choice = it ? it->choice : Choice::Add;
        if (choice == Choice::KeepOurs) { res.kept.push_back("fonction " + f.name); continue; }
        if (choice == Choice::Replace) {
            for (auto& ours : dst.programs.functions)
                if (upper(ours.name) == upper(f.name)) {
                    ours.body = f.body;
                    ours.returnType = f.returnType;
                    ours.description = f.description;
                }
            res.replaced.push_back("fonction " + f.name);
            continue;
        }
        HmiFunction copy = f;
        copy.id = dst.allocate();
        dst.programs.functions.push_back(std::move(copy));
        if (choice == Choice::Rename && it) res.renamed.push_back("fonction " + it->name + " \xE2\x86\x92 " + f.name);
        else res.added.push_back("fonction " + f.name);
    }
    // 1.11.2 (decision 174) : les scripts generaux.
    for (const auto& sc : src.programs.scripts) {
        const Item* it = itemOf(ItemKind::Script, sc.name);
        const Choice choice = it ? it->choice : Choice::Add;
        if (choice == Choice::KeepOurs) { res.kept.push_back("script " + sc.name); continue; }
        if (choice == Choice::Replace) {
            for (auto& ours : dst.programs.scripts)
                if (upper(ours.name) == upper(sc.name)) {
                    const Id keepId = ours.id;
                    const std::string keepName = ours.name, keepFolder = ours.folder;
                    ours = sc;
                    ours.id = keepId;
                    ours.name = keepName;
                    ours.folder = keepFolder;
                }
            res.replaced.push_back("script " + sc.name);
            continue;
        }
        Script copy = sc;
        copy.id = dst.allocate();
        dst.programs.scripts.push_back(std::move(copy));
        if (choice == Choice::Rename && it) res.renamed.push_back("script " + it->name + " \xE2\x86\x92 " + sc.name);
        else res.added.push_back("script " + sc.name);
    }
    // 6. Les variables absentes : creees (avec leur dossier), ou non.
    for (const auto& var : src.programs.variables) {
        const Item* it = pl.find(ItemKind::Variable, var.name);
        const Choice choice = it ? it->choice : Choice::Add;
        if (choice == Choice::Skip) { res.skipped.push_back("variable " + var.name); continue; }
        if (choice != Choice::Add || dst.variable(var.name)) { res.kept.push_back("variable " + var.name); continue; }
        Variable copy = var;
        copy.id = dst.allocate();
        if (!copy.folder.empty()
            && std::none_of(dst.programs.folders.begin(), dst.programs.folders.end(), [&](const std::string& f) { return upper(f) == upper(copy.folder); }))
            dst.programs.folders.push_back(copy.folder);
        // L'equipement n'existe peut-etre pas ici : la variable reste liee par
        // son nom (Generer le dira), comme apres un import d'archive.
        dst.programs.variables.push_back(std::move(copy));
        res.added.push_back("variable " + var.name);
    }
    // Les vues choisies, telles qu'elles sont maintenant dans le projet.
    for (const auto& name : pkg.manifest.views) {
        const std::string now = chosenRenamed.count(name) ? chosenRenamed[name] : name;
        for (const auto& sv : src.views)
            if (sv.name == now && viewMap.count(sv.id)) res.views.push_back(viewMap[sv.id]);
    }
    return res;
}

// ---------------------------------------------------------------- modeles ----
const View* mainView(const Package& pkg) {
    if (!pkg.manifest.views.empty())
        if (const auto* v = pkg.content.viewByName(pkg.manifest.views.front())) return v;
    return pkg.content.views.empty() ? nullptr : &pkg.content.views.front();
}

std::vector<std::string> templateVariables(const Package& pkg) {
    std::vector<std::string> out;
    for (const auto& v : pkg.content.programs.variables) out.push_back(v.name);
    return out;
}

Id instantiate(Project& dst, const Package& pkg, const std::string& name, const std::string& role,
               const std::vector<std::pair<std::string, std::string>>& replace, ImportResult* result) {
    const View* main = mainView(pkg);
    if (!main) return kNoId;
    // Un nom deja pris dans le projet : le suivant de libre (Vue_Armoires_2).
    const std::string wanted = dst.viewByName(name) ? uniqueViewName(dst, name) : name;
    Package only = pkg;
    only.manifest.views = {main->name};
    Plan pl = plan(dst, only);
    for (auto& it : pl.items) {
        const bool isMain = it.name == main->name && (it.kind == ItemKind::View || it.kind == ItemKind::Popup || it.kind == ItemKind::Symbol
                                                      || it.kind == ItemKind::Template);
        if (isMain) {
            // La vue du modele : toujours une vue neuve, sous le nom choisi.
            it.choice = Choice::Rename;
            it.newName = wanted;
            continue;
        }
        // Ce qui existe deja dans le projet est garde ; ce qui manque est ajoute.
        if (it.state == State::Different) it.choice = Choice::KeepOurs;
        // Une variable remplacee a la creation n'a pas a etre creee sous son ancien nom.
        if (it.kind == ItemKind::Variable)
            for (const auto& [from, to] : replace)
                if (from == it.name && to != from) it.choice = Choice::Skip;
    }
    // Le nom choisi ne doit rien ecraser dans le paquet (un symbole du meme nom).
    ImportResult res = importInto(dst, only, pl);
    const Id made = res.views.empty() ? kNoId : res.views.front();
    if (View* v = dst.view(made)) {
        v->name = wanted;
        if (!role.empty()) v->role = role;
        design::FindOptions opt;
        opt.wholeWord = true;
        opt.matchCase = true;
        opt.view = made;
        for (const auto& [from, to] : replace)
            if (!from.empty() && from != to) (void)design::replaceAll(dst, from, to, opt);
    }
    if (result) *result = std::move(res);
    return made;
}

} // namespace hmi::pkg
