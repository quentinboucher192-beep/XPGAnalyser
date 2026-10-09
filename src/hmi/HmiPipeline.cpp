#include "HmiPipeline.hpp"

#include "HmiComm.hpp"
#include "HmiDecl.hpp"
#include "HmiEnums.hpp"
#include "HmiScript.hpp"
#include "HmiScriptCheck.hpp"
#include "HmiStore.hpp"
#include "HmiSymbols.hpp"
#include "HmiTypes.hpp"
#include "../core/AtomicFile.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <set>
#include <unordered_map>

#ifdef _WIN32
#include <process.h>
#define XPG_GETPID _getpid
#else
#include <unistd.h>
#define XPG_GETPID getpid
#endif

namespace hmi::pipeline {

namespace fs = std::filesystem;

namespace {

// ------------------------------------------------------------- les genres ----
struct KindInfo {
    ElementKind      kind;
    Area             area;
    int              step;
    std::string_view key;
    std::string_view label;
    bool             code;
};
constexpr KindInfo kKinds[] = {
    {ElementKind::ApiConfig, Area::Api, 0, "api-config", "Configuration de l'API", false},
    {ElementKind::ApiVariable, Area::Api, 1, "api-variable", "Variable de l'API", false},
    {ElementKind::ApiType, Area::Api, 2, "api-type", "Type de l'API", false},
    {ElementKind::ApiExchange, Area::Api, 3, "api-echanges", "Table d'\xC3\xA9" "change", false},
    {ElementKind::Variable, Area::Ihm, 0, "variable", "Variable IHM", true},
    {ElementKind::Displays, Area::Ihm, 0, "affichages", "Unit\xC3\xA9s et formats", true},
    {ElementKind::Function, Area::Ihm, 1, "fonction", "Fonction IHM", true},
    {ElementKind::Type, Area::Ihm, 2, "type", "Type IHM", true},
    {ElementKind::Symbol, Area::Ihm, 3, "symbole", "Symbole", true},
    {ElementKind::SymbolFunction, Area::Ihm, 3, "fonction-symbole", "Fonction de symbole", true},
    {ElementKind::Script, Area::Ihm, 4, "script", "Script g\xC3\xA9n\xC3\xA9ral", true},
    {ElementKind::Resource, Area::Ihm, 5, "ressource", "Ressource", false},
    {ElementKind::Languages, Area::Ihm, 5, "langues", "Langues", false},
    {ElementKind::ViewTemplate, Area::Ihm, 6, "modele", "Mod\xC3\xA8le de vue", false},
    {ElementKind::Styles, Area::Ihm, 6, "styles", "Styles", false},
    {ElementKind::View, Area::Ihm, 7, "vue", "Vue", false},
    {ElementKind::ViewScript, Area::Ihm, 7, "script-vue", "Script de vue", true},
    {ElementKind::Popup, Area::Ihm, 8, "popup", "Popup", false},
    {ElementKind::Animations, Area::Ihm, 9, "animations", "Animations", true},
    {ElementKind::Actions, Area::Ihm, 10, "actions", "Actions", true},
    {ElementKind::Alarm, Area::Ihm, 11, "alarme", "Alarme", true},
    {ElementKind::AlarmSettings, Area::Ihm, 11, "alarmes-reglages", "R\xC3\xA9glages des alarmes", false},
    {ElementKind::Recipe, Area::Ihm, 12, "recette", "Recette", true},
    {ElementKind::Security, Area::Ihm, 13, "securite", "Utilisateurs et s\xC3\xA9" "curit\xC3\xA9", true},
    {ElementKind::History, Area::Ihm, 14, "historiques", "Historiques", true},
    {ElementKind::SimConfig, Area::Ihm, 15, "simulation", "Configuration de simulation", false},
};
const KindInfo& infoOf(ElementKind k) {
    for (const auto& i : kKinds)
        if (i.kind == k) return i;
    return kKinds[0];
}
constexpr std::string_view kApiStepNames[kApiSteps] = {"Configuration de l'API", "Variables globales", "Types et structures",
                                                       "Tables d'\xC3\xA9" "change"};
constexpr std::string_view kIhmStepNames[kIhmSteps] = {
    "Variables IHM", "Fonctions IHM", "Types IHM", "Symboles IHM", "Scripts g\xC3\xA9n\xC3\xA9raux", "Ressources",
    "Mod\xC3\xA8les de vues", "Vues", "Popups", "Animations", "Actions", "Alarmes", "Recettes",
    "Utilisateurs et s\xC3\xA9" "curit\xC3\xA9", "Historiques", "Configuration de simulation"};

// ------------------------------------------------------------- utilitaires ----
std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
std::string nowText() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[96];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}
// Un nom de fichier : lettres, chiffres, _ et -. Une lettre accentuee perd son
// accent (« Scripts généraux » : scripts_generaux), tout autre caractere devient
// un seul _ (et non un par octet de son UTF-8).
std::string sanitize(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += (std::isalnum(c) || c == '_' || c == '-') ? static_cast<char>(c) : '_';
            continue;
        }
        char base = 0;
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto n = static_cast<unsigned char>(s[i + 1]);
            switch (n | 0x20) {   // les majuscules (0x80 a 0x9F) comme leurs minuscules
                case 0xA0: case 0xA1: case 0xA2: case 0xA4: base = 'a'; break;
                case 0xA7: base = 'c'; break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB: base = 'e'; break;
                case 0xAE: case 0xAF: base = 'i'; break;
                case 0xB4: case 0xB6: base = 'o'; break;
                case 0xB9: case 0xBB: case 0xBC: base = 'u'; break;
                default: break;
            }
            if (base && n < 0xA0) base = static_cast<char>(std::toupper(static_cast<unsigned char>(base)));
        }
        out += base ? base : '_';
        while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) ++i;   // la suite du caractere
    }
    if (out.size() > 48) out.resize(48);
    return out.empty() ? std::string("_") : out;
}
std::string bytesText(const std::shared_ptr<const Bytes>& b) { return b ? std::string(b->begin(), b->end()) : std::string{}; }

// Une ligne "mot cle=valeur ..." sans ses champs de documentation.
std::string canonical(const Record& r, std::initializer_list<std::string_view> drop) {
    std::string out = r.word;
    for (const auto& [k, v] : r.fields) {
        bool skip = false;
        for (const auto d : drop) skip = skip || k == d;
        if (!skip) out += " " + k + "=" + quote(v);
    }
    return out;
}

// ------------------------------------------------- les enregistrements d'ihm.txt
//  Le texte que l'enregistrement du projet ecrirait : tous les champs de chaque
//  element, meme ceux que ce fichier ne connait pas encore (un champ ajoute plus
//  tard entre dans l'empreinte sans rien changer ici).
struct Records {
    std::map<std::string, std::string> variables, types, alarms, recipes, resources, externals, packages;   // id -> lignes
    std::string alarmSettings, security, history, styles, languages, displays, exchange, simConfig;
};
Records recordsOf(const Project& p) {
    Records out;
    std::string index;
    for (const auto& f : serializeProject(p))
        if (f.path == "ihm.txt") index = bytesText(f.data);
    std::string currentType;
    std::size_t at = 0;
    while (at < index.size()) {
        const auto end = index.find('\n', at);
        const std::string line = index.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? index.size() : end + 1;
        if (line.empty() || line[0] == '#') continue;
        Record r;
        std::string why;
        if (!parseRecord(line, r, why)) continue;
        const auto id = [&r](const char* key) { const auto* v = r.get(key); return v ? *v : std::string{}; };
        const std::string doc = canonical(r, {"description", "dossier"}) + "\n";
        const std::string& w = r.word;
        if (w == "type_ihm") { currentType = id("id"); out.types[currentType] += doc; continue; }
        if (w == "type_genre" || w == "membre" || w == "valeur_enum" || w == "operateur_type") {
            if (!currentType.empty()) out.types[currentType] += doc;
            continue;
        }
        currentType.clear();
        if (w == "variable") out.variables[id("id")] += doc;
        else if (w == "alarme") out.alarms[id("id")] += doc;
        else if (w == "recette") out.recipes[id("id")] += doc;
        else if (w == "element" || w == "jeu") out.recipes[id("recette")] += doc;
        else if (w == "ressource") out.resources[id("id")] += doc;
        else if (w == "externe") out.externals[id("id")] += doc;
        else if (w == "modele") out.packages[id("id")] += doc;
        else if (w == "alarmes_reglages" || w == "groupe_alarmes" || w == "lien_groupe_alarmes" || w == "notifications" || w == "destinataire")
            out.alarmSettings += doc;
        else if (w == "securite" || w == "role" || w == "groupe" || w == "utilisateur") out.security += doc;
        else if (w == "historique" || w == "rapport") out.history += doc;
        else if (w == "style" || w == "style_prop") out.styles += doc;
        else if (w == "langues" || w == "langue" || w == "traduction") out.languages += doc;
        else if (w == "affichage") out.displays += doc;
        else if (w == "communication" || w == "adresse_modbus") out.exchange += doc;
        else if (w == "config") out.simConfig += canonical(r, {"nom", "description", "version", "auteur", "cree", "modifie"}) + "\n";
        else if (w == "poste" || w == "poste_ecran" || w == "web" || w == "equipement" || w == "comportement" || w == "forcage"
                 || w == "memoire_jumeau" || w == "port_simule")
            out.simConfig += doc;
        // ihm, vue, script, fonction, dossiers, essais, jeux Modbus : ailleurs, ou sans effet sur l'execution
    }
    return out;
}

// ---------------------------------------------------- vues : leurs textes ----
// Le contenu d'une vue sans ce qui a son propre element (scripts, actions,
// expressions, fonctions) ni ce qui ne sert qu'a l'editeur (guides, grille,
// description, dossier) ; ses reglages (taille, fond, popup) a part.
View strippedView(const View& v) {
    View c = v;
    c.description.clear();
    c.folder.clear();
    c.guides.clear();
    c.grid = GridSettings{};
    c.activeLayer = kNoId;
    c.scripts.clear();
    c.actions.clear();
    for (auto& f : c.functions) f.body.clear(), f.description.clear(), f.decls.clear();   // 1.11.18 (lot 3) : leurs elements
    for (auto& o : c.objects) {
        o.actions.clear();
        for (auto& pr : o.props) pr.expr.clear();
    }
    const View d;
    c.width = d.width;
    c.height = d.height;
    c.background = d.background;
    c.popup = d.popup;
    c.zoomable = d.zoomable;
    c.upView = d.upView;
    return c;
}
std::string viewConfig(const View& v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%dx%d", v.width, v.height);
    std::string s = std::string("taille=") + buf + " fond=" + v.background + " zoom=" + (v.zoomable ? "1" : "0") + " parente=" + std::to_string(v.upView);
    s += " popup=" + std::to_string(v.popup.titleBar) + std::to_string(v.popup.movable) + std::to_string(v.popup.closeButton) + std::to_string(v.popup.modal)
       + std::to_string(v.popup.closeOutside) + " titre=" + v.popup.title + " place=" + v.popup.placement;
    return s;
}
std::string paramsText(const View& v) {
    std::string s;
    for (const auto& prm : v.params) s += prm.name + ":" + prm.type + ":" + std::to_string(static_cast<int>(prm.mode)) + ";";
    return s;
}
// Les expressions et les textes a trous d'une vue (l'etape Animations).
std::string animationsText(const View& v) {
    std::string s;
    for (const auto& o : v.objects)
        for (const auto& pr : o.props)
            if (!pr.expr.empty() || pr.key == "text" || pr.key == "cells" || pr.key == "states" || pr.key == "params" || pr.key == "variable"
                || pr.key == "state" || pr.key == "lamp" || pr.key == "condition" || pr.key == "min" || pr.key == "max")
                s += std::to_string(o.id) + "." + pr.key + (pr.expr.empty() ? "=" + pr.value : " := " + pr.expr) + "\n";
    for (const auto& prm : v.params) s += "param " + prm.name + " := " + prm.defaultValue + "\n";
    if (!v.popup.title.empty()) s += "titre " + v.popup.title + "\n";
    return s;
}
// Les actions d'une vue (la vue et ses objets), comme les ecrit le fichier de la vue.
std::string actionsText(const View& v) {
    View a;
    a.id = v.id;
    a.name = v.name;
    a.actions = v.actions;
    for (const auto& o : v.objects)
        if (!o.actions.empty()) {
            Object k;
            k.id = o.id;
            k.kind = o.kind;
            k.name = o.name;
            k.layer = o.layer;
            k.actions = o.actions;
            a.objects.push_back(std::move(k));
        }
    a.layers.clear();
    if (a.actions.empty() && a.objects.empty()) return {};
    return serializeView(a);
}
// 1.11.18 (refonte, lot 3) : les declarations du modele d'un code, dans son contenu - tout
// ce qui change son execution (genre, nom, type, valeur, stockage, mode, visibilite) ; sa
// documentation seule ne relance rien. Vide sans declaration : les empreintes d'avant.
std::string declarationsCanon(const std::vector<Declaration>& decls) {
    std::string s;
    for (const auto& d : decls)
        s += "\ndecl " + std::string(declKindKey(d.kind)) + " " + d.name + " : " + d.type + " := " + d.value + " "
           + std::string(storageKey(d.storage)) + " " + std::string(passModeKey(d.mode)) + " " + std::string(visibilityKey(d.visibility));
    return s;
}
std::string scriptText(const Script& s) {
    return "langage=" + std::string(scriptLangKey(s.lang)) + " evenement=" + s.event + " periode=" + std::to_string(s.periodMs) + " surveille=" + s.watch + "\n" + s.body
         + declarationsCanon(s.decls);
}
std::string functionCanon(const HmiFunction& f) {
    return "retour=" + f.returnType + " virtuelle=" + std::string(f.isVirtual ? "1" : "0") + "\n" + f.body + declarationsCanon(f.decls);
}
std::string functionIface(std::string_view name, const HmiFunction& f) { return std::string(name) + "(" + signatureOf(decl::codeOf(f)) + ")" + (f.returnType.empty() ? "" : " : " + f.returnType); }

// ----------------------------------------------- les noms et les references ----
struct Names {
    std::unordered_map<std::string, std::string> variables, functions, types, views, scripts, resources, apiVars, apiTypes, alarms;   // nom (minuscules) -> cle
    std::unordered_map<std::string, std::string> viewNames;   // cle de vue -> nom
    const Project* p{nullptr};
};
std::string keyOf(ElementKind k, Id id) { return std::string(infoOf(k).key) + ":" + std::to_string(id); }
ElementKind viewKind(const View& v) {
    if (isSymbolView(v)) return ElementKind::Symbol;
    if (v.role == "popup") return ElementKind::Popup;
    if (v.role == "modele" || v.role == "entete" || v.role == "pied") return ElementKind::ViewTemplate;
    return ElementKind::View;
}
std::string rootOf(std::string_view dotted) {
    const auto dot = dotted.find('.');
    return std::string(dotted.substr(0, dot));
}

// Resoudre ce qu'un code cite : variables, fonctions, types, vues, appels
// Vue.Instance.Fonction (ou Instance.Fonction dans la vue), l'API.
void resolveCode(const Names& n, const View* here, std::string_view code, std::vector<Dependency>& deps, std::set<std::string>& seen,
                 const std::set<std::string>& locals = {}) {
    const auto add = [&](const std::string& name, const std::string& key, DepMode mode, const char* via) {
        if (key.empty() || !seen.insert(key).second) return;
        deps.push_back({name, key, mode, via});
    };
    for (const auto& id : identifiers(code)) {
        const std::string root = rootOf(id), lroot = lower(root);
        if (locals.count(lroot)) continue;
        if (auto it = n.variables.find(lroot); it != n.variables.end()) { add(root, it->second, DepMode::Interface, "variable"); continue; }
        if (auto it = n.functions.find(lroot); it != n.functions.end()) { add(root, it->second, DepMode::Interface, "fonction"); continue; }
        if (auto it = n.types.find(lroot); it != n.types.end()) { add(root, it->second, DepMode::Interface, "type"); continue; }
        if (auto it = n.apiVars.find(lroot); it != n.apiVars.end()) { add(root, it->second, DepMode::Interface, "API"); continue; }
        // Vue.Instance.Fonction (partout) ou Instance.Fonction (dans la vue qui pose l'instance)
        const auto parts = [&id] {
            std::vector<std::string> v;
            std::size_t a = 0;
            for (;;) {
                const auto d = id.find('.', a);
                v.push_back(id.substr(a, d == std::string::npos ? std::string::npos : d - a));
                if (d == std::string::npos) break;
                a = d + 1;
            }
            return v;
        }();
        const View* view = nullptr;
        std::string inst, fn;
        if (parts.size() >= 3 && n.views.count(lower(parts[0]))) { view = n.p->viewByName(parts[0]); inst = parts[1]; fn = parts[2]; }
        else if (parts.size() >= 2 && here) { view = here; inst = parts[0]; fn = parts[1]; }
        if (view && parts.size() >= 3) add(parts[0], keyOf(viewKind(*view), view->id), DepMode::Interface, "vue");
        if (!view) continue;
        const Object* o = view->objectByName(inst);
        const View* sym = o && o->kind == Kind::SymbolInstance ? symbolOf(*n.p, *o) : nullptr;
        if (!sym) continue;
        for (const auto& f : sym->functions)
            if (lower(f.name) == lower(fn)) add(sym->name + "." + f.name, keyOf(ElementKind::SymbolFunction, f.id), DepMode::Interface, "appel");
    }
}

} // namespace

// ================================================================ publics ====
Area areaOf(ElementKind k) noexcept { return infoOf(k).area; }
int stepOf(ElementKind k) noexcept { return infoOf(k).step; }
std::string_view stepName(Area a, int s) noexcept {
    if (a == Area::Api) return s >= 0 && s < kApiSteps ? kApiStepNames[s] : std::string_view{};
    return s >= 0 && s < kIhmSteps ? kIhmStepNames[s] : std::string_view{};
}
std::string_view kindLabel(ElementKind k) noexcept { return infoOf(k).label; }
std::string_view kindKey(ElementKind k) noexcept { return infoOf(k).key; }
std::optional<ElementKind> kindFromKey(std::string_view key) noexcept {
    for (const auto& i : kKinds)
        if (i.key == key) return i.kind;
    return std::nullopt;
}
bool compilable(ElementKind k) noexcept { return infoOf(k).code; }

std::string hash(std::string_view text) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
    char buf[20];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

// 1.11.18 (refonte des scripts, lot 2) : les parametres lus par hmi::decl (la lecture
// partagee) - tous les blocs VAR_INPUT, VAR_IN_OUT et VAR_OUTPUT, et non plus le texte
// du premier VAR_INPUT : "a, b : REAL" et "a : REAL; b : REAL" ont la meme interface,
// un commentaire // ne la change plus, un parametre VAR_IN_OUT la change.
std::string signatureOf(std::string_view body) { return decl::parameterSignature(decl::extract(body)); }

std::vector<std::string> identifiers(std::string_view code) {
    std::vector<std::string> out;
    const std::size_t n = code.size();
    const auto identStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    const auto identChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::size_t i = 0;
    while (i < n) {
        const char c = code[i];
        if (c == '(' && i + 1 < n && code[i + 1] == '*') { const auto e = code.find("*)", i + 2); i = e == std::string_view::npos ? n : e + 2; continue; }
        if (c == '/' && i + 1 < n && code[i + 1] == '/') { while (i < n && code[i] != '\n') ++i; continue; }
        if (c == '\'' || c == '"') {
            // une chaine : ses trous {Nom} ou {Nom:0.0} comptent (IHM_LOG, IHM_JOURNAL, textes a trous)
            const char q = c;
            std::size_t j = i + 1;
            while (j < n) {
                if (code[j] == q) { if (j + 1 < n && code[j + 1] == q) { j += 2; continue; } break; }
                if (code[j] == '{' && j + 1 < n && identStart(code[j + 1])) {
                    std::size_t k = j + 1;
                    while (k < n && (identChar(code[k]) || code[k] == '.' || code[k] == '[' || code[k] == ']')) ++k;
                    std::string name(code.substr(j + 1, k - j - 1));
                    if (const auto br = name.find('['); br != std::string::npos) name.resize(br);
                    if (!name.empty()) out.push_back(name);
                    j = k;
                    continue;
                }
                ++j;
            }
            i = j + 1;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) { while (i < n && (identChar(code[i]) || code[i] == '#' || code[i] == '.')) ++i; continue; }
        if (identStart(c)) {
            std::string name;
            for (;;) {
                const std::size_t s = i;
                while (i < n && identChar(code[i])) ++i;
                name += std::string(code.substr(s, i - s));
                // indices sautes : Armoires[i].ana -> Armoires.ana (et i, cite par l'indice)
                while (i < n && code[i] == '[') {
                    const std::size_t open = i;
                    int depth = 0;
                    while (i < n) { if (code[i] == '[') ++depth; else if (code[i] == ']' && --depth == 0) { ++i; break; } ++i; }
                    const std::size_t close = i > open + 1 && i <= n && code[i - 1] == ']' ? i - 1 : i;
                    for (auto& inner : identifiers(code.substr(open + 1, close - open - 1))) out.push_back(std::move(inner));
                }
                if (i + 1 < n && code[i] == '.' && identStart(code[i + 1])) { name += '.'; ++i; continue; }
                break;
            }
            // T#2s, 16#FF : un litteral, pas un nom
            if (i < n && code[i] == '#') { while (i < n && (identChar(code[i]) || code[i] == '#' || code[i] == '.')) ++i; continue; }
            out.push_back(std::move(name));
            continue;
        }
        ++i;
    }
    return out;
}

// ============================================================== collecte ====
std::vector<Element> collect(const Project& p, const ApiInfo& api) {
    std::vector<Element> out;
    // une reference rendue par make() reste bonne pendant toute la collecte
    out.reserve(64 + api.variables.size() + api.types.size() + p.programs.variables.size() + p.programs.functions.size() + p.programs.types.size()
                + p.programs.scripts.size() + p.assets.resources.size() + p.assets.files.size() + p.viewTemplates.size() + p.alarms.size() + p.recipes.size()
                + [&p] { std::size_t k = 0; for (const auto& v : p.views) k += 3 + v.scripts.size() + v.functions.size(); return k; }());
    const Records rec = recordsOf(p);
    Names n;
    n.p = &p;
    for (const auto& v : p.programs.variables) n.variables[lower(v.name)] = keyOf(ElementKind::Variable, v.id);
    for (const auto& f : p.programs.functions) n.functions[lower(f.name)] = keyOf(ElementKind::Function, f.id);
    for (const auto& t : p.programs.types) n.types[lower(t.name)] = keyOf(ElementKind::Type, t.id);
    for (const auto& v : p.views) { n.views[lower(v.name)] = keyOf(viewKind(v), v.id); n.viewNames[keyOf(viewKind(v), v.id)] = v.name; }
    for (const auto& s : p.programs.scripts) n.scripts[lower(s.name)] = keyOf(ElementKind::Script, s.id);
    for (const auto& r : p.assets.resources) n.resources[r.name] = keyOf(ElementKind::Resource, r.id);
    for (const auto& a : p.alarms) n.alarms[lower(a.name)] = keyOf(ElementKind::Alarm, a.id);
    // L'API : seulement ce que l'IHM utilise (une variable globale citee ; son type derive).
    std::unordered_map<std::string, const ApiInfo::Var*> apiByName;
    for (const auto& v : api.variables) apiByName[lower(v.name)] = &v;
    std::set<std::string> apiUsed;
    {
        std::string all;
        for (const auto& v : p.views) {
            for (const auto& o : v.objects) {
                for (const auto& pr : o.props) all += pr.expr + "\n" + (pr.key == "text" || pr.key == "variable" || pr.key == "params" ? pr.value + "\n" : std::string{});
                for (const auto& a : o.actions) all += a.target + "\n" + a.value + "\n" + a.watch + "\n" + a.guard + "\n";
            }
            for (const auto& a : v.actions) all += a.target + "\n" + a.value + "\n" + a.watch + "\n" + a.guard + "\n";
            for (const auto& s : v.scripts) all += decl::codeOf(s) + "\n";
            for (const auto& f : v.functions) all += decl::codeOf(f) + "\n";
            for (const auto& prm : v.params) all += prm.defaultValue + "\n";
        }
        for (const auto& s : p.programs.scripts) all += decl::codeOf(s) + "\n" + s.watch + "\n";
        for (const auto& f : p.programs.functions) all += decl::codeOf(f) + "\n";
        for (const auto& a : p.alarms) all += a.condition + "\n" + a.message + "\n";
        for (const auto& r : p.recipes) for (const auto& f : r.fields) all += f.variable + "\n";
        for (const auto& h : p.history.archived) all += h + "\n";
        for (const auto& c : p.comm.addresses) all += c.variable + "\n";
        for (const auto& id : identifiers(all))
            if (apiByName.count(lower(rootOf(id)))) apiUsed.insert(lower(rootOf(id)));
    }
    std::set<std::string> apiTypesUsed;
    for (const auto& name : apiUsed) {
        const auto* v = apiByName[name];
        n.apiVars[name] = "api-variable:" + v->name;
        for (const auto& t : api.types)
            if (lower(v->type).find(lower(t.name)) != std::string::npos) apiTypesUsed.insert(t.name);
    }
    for (const auto& t : api.types) if (apiTypesUsed.count(t.name)) n.apiTypes[lower(t.name)] = "api-type:" + t.name;

    const auto make = [&](ElementKind kind, std::string key, std::string name, std::string path, Id id, Id owner) -> Element& {
        Element e;
        e.kind = kind;
        e.key = std::move(key);
        e.name = std::move(name);
        e.path = std::move(path);
        e.id = id;
        e.owner = owner;
        out.push_back(std::move(e));
        return out.back();
    };
    // un symbole : "IHM/Symboles/<son dossier>/<nom>" ; ses fonctions, ses popups, ses scripts dessous
    const auto symbolPath = [](const View& v) { return "IHM/Symboles/" + (v.folder.empty() ? std::string{} : v.folder + "/") + v.name; };
    const auto dep = [](Element& e, std::set<std::string>& seen, const std::string& name, const std::string& key, DepMode mode, const char* via) {
        if (key.empty()) { e.deps.push_back({name, {}, mode, via}); return; }
        if (key == e.key || !seen.insert(key).second) return;
        e.deps.push_back({name, key, mode, via});
    };

    // ---- B. l'API ----
    {
        auto& e = make(ElementKind::ApiConfig, "api-config", "Configuration de l'API", "API/Configuration", kNoId, kNoId);
        e.content = api.config;
        e.iface = api.config;
    }
    for (const auto& name : apiUsed) {
        const auto* v = apiByName[name];
        auto& e = make(ElementKind::ApiVariable, "api-variable:" + v->name, v->name, "API/Variables/" + v->name, kNoId, kNoId);
        e.content = v->name + " : " + v->type + " @ " + v->address;
        e.iface = v->name + " : " + v->type;
        std::set<std::string> seen;
        for (const auto& t : api.types)
            if (apiTypesUsed.count(t.name) && lower(v->type).find(lower(t.name)) != std::string::npos)
                dep(e, seen, t.name, "api-type:" + t.name, DepMode::Content, "type");
    }
    for (const auto& t : api.types) {
        if (!apiTypesUsed.count(t.name)) continue;
        auto& e = make(ElementKind::ApiType, "api-type:" + t.name, t.name, "API/Types d\xC3\xA9riv\xC3\xA9s/" + t.name, kNoId, kNoId);
        e.content = t.definition;
        e.iface = t.name + " { " + t.definition + " }";
    }
    {
        auto& e = make(ElementKind::ApiExchange, "api-echanges", "Tables d'\xC3\xA9" "change", "API/Tables d'\xC3\xA9" "change", kNoId, kNoId);
        e.content = rec.exchange;
        e.iface = rec.exchange;
        std::set<std::string> seen;
        for (const auto& c : p.comm.addresses) {
            const std::string r = lower(rootOf(c.variable));
            if (auto it = n.variables.find(r); it != n.variables.end()) dep(e, seen, rootOf(c.variable), it->second, DepMode::Interface, "variable");
            else if (auto jt = n.apiVars.find(r); jt != n.apiVars.end()) dep(e, seen, rootOf(c.variable), jt->second, DepMode::Interface, "variable");
        }
    }

    // ---- C1. variables IHM ----
    for (const auto& v : p.programs.variables) {
        auto& e = make(ElementKind::Variable, keyOf(ElementKind::Variable, v.id), v.name,
                       "IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Variables IHM/" + (v.folder.empty() ? std::string{} : v.folder + "/") + v.name, v.id, kNoId);
        const auto it = rec.variables.find(std::to_string(v.id));
        e.content = it != rec.variables.end() ? it->second : v.name + ":" + v.type + ":" + v.initial;
        e.iface = v.name + " : " + v.type;
        std::set<std::string> seen;
        // son type (une structure, une enumeration), et ce que sa valeur initiale cite
        for (const auto& t : p.programs.types)
            if (lower(v.type).find(lower(t.name)) != std::string::npos) dep(e, seen, t.name, keyOf(ElementKind::Type, t.id), DepMode::Content, "type");
        if (!v.equipment.empty()) dep(e, seen, "Configuration de simulation", "simulation", DepMode::Interface, "\xC3\xA9quipement");
        resolveCode(n, nullptr, v.initial, e.deps, seen, {lower(v.name)});
    }
    {
        auto& e = make(ElementKind::Displays, "affichages", "Unit\xC3\xA9s et formats", "IHM/Configuration/Unit\xC3\xA9s et formats", kNoId, kNoId);
        e.content = rec.displays;
        e.iface = "affichages";
        std::set<std::string> seen;
        for (const auto& d : p.displays) resolveCode(n, nullptr, d.path, e.deps, seen);
    }
    // ---- C2. fonctions IHM ----
    for (const auto& f : p.programs.functions) {
        auto& e = make(ElementKind::Function, keyOf(ElementKind::Function, f.id), f.name, "IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Fonctions/" + f.name, f.id, kNoId);
        e.content = functionCanon(f);
        e.iface = functionIface(f.name, f);
        std::set<std::string> seen;
        resolveCode(n, nullptr, decl::codeOf(f), e.deps, seen, {lower(f.name)});
    }
    // ---- C3. types IHM ----
    for (const auto& t : p.programs.types) {
        auto& e = make(ElementKind::Type, keyOf(ElementKind::Type, t.id), t.name, "IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Types IHM/" + t.name, t.id, kNoId);
        const auto it = rec.types.find(std::to_string(t.id));
        e.content = it != rec.types.end() ? it->second : t.name;
        std::string iface = t.name + " {";
        for (const auto& m : t.members) iface += " " + m.name + " : " + m.type + ";";
        for (const auto& v : t.values) iface += " " + v.name + " = " + std::to_string(v.value) + ";";
        for (const auto& o : t.operators) iface += " op " + o.op + "(" + o.left + "," + o.right + ")->" + o.result + ";";
        e.iface = iface + " }";
        std::set<std::string> seen;
        for (const auto& m : t.members)
            for (const auto& u : p.programs.types)
                if (u.id != t.id && lower(m.type).find(lower(u.name)) != std::string::npos) dep(e, seen, u.name, keyOf(ElementKind::Type, u.id), DepMode::Content, "type");
        for (const auto& o : t.operators) resolveCode(n, nullptr, decl::codeOf(o), e.deps, seen, {"a", "b"});
    }
    // ---- C4. symboles, leurs fonctions ----
    for (const auto& v : p.views) {
        if (viewKind(v) != ElementKind::Symbol) continue;
        auto& e = make(ElementKind::Symbol, keyOf(ElementKind::Symbol, v.id), v.name, symbolPath(v), v.id, kNoId);
        e.content = serializeView(strippedView(v));
        e.config = viewConfig(v);
        e.iface = v.name + "(" + paramsText(v) + ")";
        std::set<std::string> seen;
        for (const auto& o : v.objects) {
            if (o.kind == Kind::SymbolInstance)
                if (const View* s = symbolOf(p, o)) dep(e, seen, s->name, keyOf(ElementKind::Symbol, s->id), DepMode::Content, "instance");
            for (const auto& pr : o.props)
                if (auto it = n.resources.find(pr.value); it != n.resources.end()) dep(e, seen, pr.value, it->second, DepMode::Interface, "ressource");
        }
        std::set<std::string> params;
        for (const auto& prm : v.params) params.insert(lower(prm.name));
        for (const auto& o : v.operators) resolveCode(n, &v, decl::codeOf(o), e.deps, seen, {"a", "b"});
        for (const auto& f : v.functions) {
            auto& fe = make(ElementKind::SymbolFunction, keyOf(ElementKind::SymbolFunction, f.id), f.name,
                            symbolPath(v) + "/Fonctions/" + f.name, f.id, v.id);
            fe.content = functionCanon(f);
            fe.iface = functionIface(v.name + "." + f.name, f);
            std::set<std::string> fseen;
            fe.deps.push_back({v.name, keyOf(ElementKind::Symbol, v.id), DepMode::Interface, "symbole"});
            fseen.insert(keyOf(ElementKind::Symbol, v.id));
            std::set<std::string> locals = params;
            locals.insert(lower(f.name));
            // ses soeurs, appelees par leur nom
            std::set<std::string> called;
            const std::string code = decl::codeOf(f);                 // 1.11.18 (lot 3) : ses declarations du modele
            for (const auto& x : identifiers(code)) called.insert(lower(rootOf(x)));
            for (const auto& g : v.functions)
                if (g.id != f.id && called.count(lower(g.name)))
                    dep(fe, fseen, g.name, keyOf(ElementKind::SymbolFunction, g.id), DepMode::Interface, "appel");
            resolveCode(n, &v, code, fe.deps, fseen, locals);
        }
    }
    // ---- C5. scripts generaux ----
    for (const auto& s : p.programs.scripts) {
        auto& e = make(ElementKind::Script, keyOf(ElementKind::Script, s.id), s.name,
                       "IHM/Programmation g\xC3\xA9n\xC3\xA9rale/Scripts/" + (s.folder.empty() ? std::string{} : s.folder + "/") + s.name, s.id, kNoId);
        e.content = scriptText(s);
        e.iface = s.name + " (" + s.event + ")";
        std::set<std::string> seen;
        resolveCode(n, nullptr, decl::codeOf(s) + "\n" + s.watch, e.deps, seen);
    }
    // ---- C6. ressources, fichiers externes, langues ----
    for (const auto& r : p.assets.resources) {
        auto& e = make(ElementKind::Resource, keyOf(ElementKind::Resource, r.id), r.name, "IHM/Ressources/" + (r.folder.empty() ? std::string{} : r.folder + "/") + r.name, r.id, kNoId);
        const auto it = rec.resources.find(std::to_string(r.id));
        e.content = (it != rec.resources.end() ? it->second : r.name) + " donnees=" + (r.data ? hash(std::string_view(reinterpret_cast<const char*>(r.data->data()), r.data->size())) : std::string("absentes"));
        e.iface = r.name + " (" + r.format + ")";
    }
    for (const auto& f : p.assets.files) {
        auto& e = make(ElementKind::Resource, "externe:" + std::to_string(f.id), f.name, "IHM/Fichiers externes/" + f.name, f.id, kNoId);
        const auto it = rec.externals.find(std::to_string(f.id));
        e.content = it != rec.externals.end() ? it->second : f.name;
        e.iface = f.name;
    }
    {
        auto& e = make(ElementKind::Languages, "langues", "Langues", "IHM/Configuration/Langues", kNoId, kNoId);
        e.content = rec.languages;
        e.iface = "langues";
    }
    // ---- C7. modeles de vues, styles ----
    for (const auto& v : p.views) {
        if (viewKind(v) != ElementKind::ViewTemplate) continue;
        auto& e = make(ElementKind::ViewTemplate, keyOf(ElementKind::ViewTemplate, v.id), v.name, "IHM/Mod\xC3\xA8les/" + v.name, v.id, kNoId);
        e.content = serializeView(strippedView(v));
        e.config = viewConfig(v);
        e.iface = v.name + "(" + paramsText(v) + ")";
    }
    for (const auto& m : p.viewTemplates) {
        auto& e = make(ElementKind::ViewTemplate, "paquet-modele:" + std::to_string(m.id), m.name, "IHM/Mod\xC3\xA8les de vues/" + m.name, m.id, kNoId);
        const auto it = rec.packages.find(std::to_string(m.id));
        e.content = (it != rec.packages.end() ? it->second : m.name) + " donnees=" + (m.data ? hash(std::string_view(reinterpret_cast<const char*>(m.data->data()), m.data->size())) : std::string("-"));
        e.iface = m.name;
    }
    {
        auto& e = make(ElementKind::Styles, "styles", "Styles", "IHM/Styles", kNoId, kNoId);
        e.content = rec.styles;
        e.iface = rec.styles;
    }
    // ---- C8 / C9. vues et popups, leurs scripts ; C10 / C11. animations et actions ----
    for (const auto& v : p.views) {
        const ElementKind vk = viewKind(v);
        const std::string vkey = keyOf(vk, v.id);
        const std::string base = vk == ElementKind::Symbol ? symbolPath(v)
                               : vk == ElementKind::Popup ? (v.ownerSymbol != kNoId ? (p.view(v.ownerSymbol) ? symbolPath(*p.view(v.ownerSymbol)) : std::string("IHM/Symboles/?")) + "/Popups/" + v.name
                                                                                     : "IHM/Popups/" + (v.folder.empty() ? std::string{} : v.folder + "/") + v.name)
                               : vk == ElementKind::ViewTemplate ? "IHM/Mod\xC3\xA8les/" + v.name
                               : "IHM/Vues/" + (v.folder.empty() ? std::string{} : v.folder + "/") + v.name;
        if (vk == ElementKind::View || vk == ElementKind::Popup) {
            auto& e = make(vk, vkey, v.name, base, v.id, v.ownerSymbol);
            e.content = serializeView(strippedView(v));
            e.config = viewConfig(v);
            e.iface = v.name + "(" + paramsText(v) + ")";
            std::set<std::string> seen;
            if (v.templateView != kNoId) if (const View* t = p.view(v.templateView)) dep(e, seen, t->name, keyOf(viewKind(*t), t->id), DepMode::Content, "mod\xC3\xA8le");
            if (v.showHeader) for (const auto& t : p.views) if ((v.header == kNoId ? t.role == "entete" : t.id == v.header)) { dep(e, seen, t.name, keyOf(viewKind(t), t.id), DepMode::Content, "en-t\xC3\xAAte"); break; }
            if (v.showFooter) for (const auto& t : p.views) if ((v.footer == kNoId ? t.role == "pied" : t.id == v.footer)) { dep(e, seen, t.name, keyOf(viewKind(t), t.id), DepMode::Content, "pied de page"); break; }
            if (v.ownerSymbol != kNoId) if (const View* s = p.view(v.ownerSymbol)) dep(e, seen, s->name, keyOf(ElementKind::Symbol, s->id), DepMode::Interface, "symbole");
            if (v.upView != kNoId) if (const View* u = p.view(v.upView)) dep(e, seen, u->name, keyOf(viewKind(*u), u->id), DepMode::Interface, "vue parente");
            bool styled = false;
            for (const auto& o : v.objects) {
                if (o.kind == Kind::SymbolInstance) {
                    if (const View* s = symbolOf(p, o)) dep(e, seen, s->name, keyOf(ElementKind::Symbol, s->id), DepMode::Content, "instance");
                    else dep(e, seen, o.text("symbol").empty() ? o.name : o.text("symbol"), {}, DepMode::Content, "instance");
                }
                for (const auto& pr : o.props) {
                    if (auto it = n.resources.find(pr.value); it != n.resources.end()) dep(e, seen, pr.value, it->second, DepMode::Interface, "ressource");
                    if (pr.key == "namedStyle" && !pr.value.empty()) styled = true;
                }
            }
            if (styled) dep(e, seen, "Styles", "styles", DepMode::Content, "style");
        }
        // les scripts de la vue (ou de la popup, du symbole, du modele)
        std::set<std::string> params;
        for (const auto& prm : v.params) params.insert(lower(prm.name));
        for (const auto& s : v.scripts) {
            auto& e = make(ElementKind::ViewScript, keyOf(ElementKind::ViewScript, s.id), s.name, base + "/Scripts/" + s.name, s.id, v.id);
            e.content = scriptText(s);
            e.iface = s.name;
            std::set<std::string> seen;
            dep(e, seen, v.name, vkey, DepMode::Interface, "vue");
            resolveCode(n, &v, decl::codeOf(s), e.deps, seen, params);
        }
        {
            auto& e = make(ElementKind::Animations, keyOf(ElementKind::Animations, v.id), "Animations", base + "/Animations", v.id, v.id);
            e.content = animationsText(v);
            e.iface = "animations";
            std::set<std::string> seen;
            dep(e, seen, v.name, vkey, DepMode::Interface, "vue");
            std::string code;
            for (const auto& o : v.objects)
                for (const auto& pr : o.props) {
                    if (!pr.expr.empty()) code += pr.expr + "\n";
                    if (pr.key == "text" || pr.key == "params" || pr.key == "variable" || pr.key == "cells" || pr.key == "states") code += "'" + pr.value + "'\n" + (pr.key == "text" ? std::string{} : pr.value + "\n");
                }
            for (const auto& prm : v.params) code += prm.defaultValue + "\n";
            std::set<std::string> locals = params;
            for (const auto& o : v.objects) locals.insert(lower(o.name));
            resolveCode(n, &v, code, e.deps, seen, locals);
        }
        {
            auto& e = make(ElementKind::Actions, keyOf(ElementKind::Actions, v.id), "Actions", base + "/Actions", v.id, v.id);
            e.content = actionsText(v);
            e.iface = "actions";
            std::set<std::string> seen;
            dep(e, seen, v.name, vkey, DepMode::Interface, "vue");
            std::string code;
            const auto scan = [&](const std::vector<Action>& list) {
                for (const auto& a : list) {
                    code += a.value + "\n" + a.watch + "\n" + a.guard + "\n" + a.params + "\n";
                    if (auto it = n.views.find(lower(a.target)); it != n.views.end()) dep(e, seen, a.target, it->second, DepMode::Interface, "cible");
                    else if (auto jt = n.scripts.find(lower(a.target)); jt != n.scripts.end()) dep(e, seen, a.target, jt->second, DepMode::Interface, "script");
                    else if (auto kt = n.resources.find(a.target); kt != n.resources.end()) dep(e, seen, a.target, kt->second, DepMode::Interface, "ressource");
                    else code += a.target + "\n";
                }
            };
            scan(v.actions);
            for (const auto& o : v.objects) scan(o.actions);
            std::set<std::string> locals = params;
            for (const auto& o : v.objects) locals.insert(lower(o.name));
            resolveCode(n, &v, code, e.deps, seen, locals);
        }
    }
    // ---- C12. alarmes ----
    for (const auto& a : p.alarms) {
        auto& e = make(ElementKind::Alarm, keyOf(ElementKind::Alarm, a.id), a.name, "IHM/Alarmes/" + a.name, a.id, kNoId);
        const auto it = rec.alarms.find(std::to_string(a.id));
        e.content = it != rec.alarms.end() ? it->second : a.name + ":" + a.condition;
        e.iface = a.name;
        std::set<std::string> seen;
        resolveCode(n, nullptr, a.condition + "\n'" + a.message + "'", e.deps, seen);
    }
    {
        auto& e = make(ElementKind::AlarmSettings, "alarmes-reglages", "R\xC3\xA9glages des alarmes", "IHM/Alarmes/R\xC3\xA9glages", kNoId, kNoId);
        e.content = rec.alarmSettings;
        e.iface = "alarmes";
    }
    // ---- C13. recettes ----
    for (const auto& r : p.recipes) {
        auto& e = make(ElementKind::Recipe, keyOf(ElementKind::Recipe, r.id), r.name, "IHM/Recettes/" + r.name, r.id, kNoId);
        const auto it = rec.recipes.find(std::to_string(r.id));
        e.content = it != rec.recipes.end() ? it->second : r.name;
        e.iface = r.name;
        std::set<std::string> seen;
        std::string code;
        for (const auto& f : r.fields) code += f.variable + "\n";
        for (const auto& rr : r.records) for (const auto& val : rr.values) code += val + "\n";
        resolveCode(n, nullptr, code, e.deps, seen);
    }
    // ---- C14. utilisateurs et securite ; C15. historiques ; C16. configuration de simulation ----
    {
        auto& e = make(ElementKind::Security, "securite", "Utilisateurs et s\xC3\xA9" "curit\xC3\xA9", "IHM/Utilisateurs et s\xC3\xA9" "curit\xC3\xA9", kNoId, kNoId);
        e.content = rec.security;
        e.iface = "securite";
        std::set<std::string> seen;
        std::string code;
        for (const auto& u : p.security.users) code += u.expression + "\n";
        resolveCode(n, nullptr, code, e.deps, seen);
    }
    {
        auto& e = make(ElementKind::History, "historiques", "Historiques", "IHM/Historiques", kNoId, kNoId);
        e.content = rec.history;
        e.iface = "historiques";
        std::set<std::string> seen;
        std::string code;
        for (const auto& h : p.history.archived) code += h + "\n";
        resolveCode(n, nullptr, code, e.deps, seen);
    }
    {
        auto& e = make(ElementKind::SimConfig, "simulation", "Configuration de simulation", "IHM/Configuration de simulation", kNoId, kNoId);
        e.content = rec.simConfig;
        std::string iface;
        for (const auto& q : p.equipments) iface += q.name + ";";
        e.iface = iface;
        std::set<std::string> seen;
        if (p.config.startView != kNoId) if (const View* sv = p.view(p.config.startView)) dep(e, seen, sv->name, keyOf(viewKind(*sv), sv->id), DepMode::Interface, "vue de d\xC3\xA9part");
    }
    // Un element cite par son nom mais introuvable : la dependance reste, sans cle.
    std::set<std::string> keys;
    for (const auto& e : out) keys.insert(e.key);
    for (auto& e : out)
        for (auto& d : e.deps)
            if (!d.key.empty() && !keys.count(d.key)) d.key.clear();
    // L'ordre du build : API puis IHM, etape par etape (stable dans une etape).
    std::stable_sort(out.begin(), out.end(), [](const Element& a, const Element& b) {
        const int ka = (areaOf(a.kind) == Area::Api ? 0 : 100) + stepOf(a.kind), kb = (areaOf(b.kind) == Area::Api ? 0 : 100) + stepOf(b.kind);
        return ka < kb;
    });
    return out;
}

// ============================================================== les etats ====
std::string_view stateLabel(State s) noexcept {
    switch (s) {
        case State::UpToDate: return "\xC3\x80 jour";
        case State::Modified: return "Modifi\xC3\xA9";
        case State::NotGenerated: return "Non g\xC3\xA9n\xC3\xA9r\xC3\xA9";
        case State::GenerationRequired: return "G\xC3\xA9n\xC3\xA9ration requise";
        case State::Generating: return "G\xC3\xA9n\xC3\xA9ration en cours";
        case State::Generated: return "G\xC3\xA9n\xC3\xA9r\xC3\xA9";
        case State::GenerationFailed: return "G\xC3\xA9n\xC3\xA9ration \xC3\xA9" "chou\xC3\xA9" "e";
        case State::CompilationRequired: return "Compilation requise";
        case State::Compiling: return "Compilation en cours";
        case State::Compiled: return "Compil\xC3\xA9";
        case State::CompilationFailed: return "Compilation \xC3\xA9" "chou\xC3\xA9" "e";
        case State::InvalidDependency: return "D\xC3\xA9pendance invalide";
        case State::Obsolete: return "Obsol\xC3\xA8te";
    }
    return "?";
}
std::string_view stateKey(State s) noexcept {
    switch (s) {
        case State::UpToDate: return "a_jour";
        case State::Modified: return "modifie";
        case State::NotGenerated: return "non_genere";
        case State::GenerationRequired: return "generation_requise";
        case State::Generating: return "generation_en_cours";
        case State::Generated: return "genere";
        case State::GenerationFailed: return "generation_echouee";
        case State::CompilationRequired: return "compilation_requise";
        case State::Compiling: return "compilation_en_cours";
        case State::Compiled: return "compile";
        case State::CompilationFailed: return "compilation_echouee";
        case State::InvalidDependency: return "dependance_invalide";
        case State::Obsolete: return "obsolete";
    }
    return "?";
}
std::optional<State> stateFromKey(std::string_view k) noexcept {
    for (int i = 0; i <= static_cast<int>(State::Obsolete); ++i)
        if (stateKey(static_cast<State>(i)) == k) return static_cast<State>(i);
    return std::nullopt;
}
std::string_view severityLabel(Severity s) noexcept {
    switch (s) {
        case Severity::Information: return "Information";
        case Severity::Success: return "Succ\xC3\xA8s";
        case Severity::Warning: return "Avertissement";
        case Severity::Error: return "Erreur";
        case Severity::Critical: return "Critique";
    }
    return "?";
}
Diagnostic fromIssue(const Issue& i, std::string_view step) {
    Diagnostic d;
    d.severity = i.severity == Issue::Severity::Error ? Severity::Error : i.severity == Issue::Severity::Warning ? Severity::Warning : Severity::Information;
    d.code = i.category;
    d.category = i.category;
    d.message = i.message;
    d.line = i.line;
    d.column = i.column;
    d.length = i.length;
    d.step = std::string(step);
    d.view = i.view;
    d.object = i.object;
    d.script = i.script;
    d.item = i.item;
    d.property = i.property;
    return d;
}

// ============================================================== le cache =====
namespace {
const std::string kCacheFile = "build-cache.txt";
const std::string kProjectOwner = "*projet*";
std::string diagLine(const std::string& owner, const Diagnostic& d) {
    return "diagnostic" + std::string(" de=") + quote(owner) + " gravite=" + quote(severityLabel(d.severity)) + " code=" + quote(d.code)
         + " categorie=" + quote(d.category) + " message=" + quote(d.message) + " ligne=" + std::to_string(d.line) + " colonne=" + std::to_string(d.column)
         + " longueur=" + std::to_string(d.length) + " etape=" + quote(d.step) + " date=" + quote(d.date) + " suggestion=" + quote(d.suggestion)
         + " vue=" + std::to_string(d.view) + " objet=" + std::to_string(d.object) + " script=" + std::to_string(d.script) + " item=" + std::to_string(d.item)
         + " propriete=" + quote(d.property) + " fichier=" + quote(d.file) + "\n";
}
Severity severityFrom(std::string_view s) {
    for (int i = 0; i <= static_cast<int>(Severity::Critical); ++i)
        if (severityLabel(static_cast<Severity>(i)) == s) return static_cast<Severity>(i);
    return Severity::Error;
}
} // namespace

std::string serialize(const Cache& c) {
    std::string s = "# XPGAnalyser - cache de build de l'IHM (ne pas modifier a la main)\n";
    s += "cache_build format=" + std::to_string(kCacheFormat) + " generateur=" + quote(c.generator.empty() ? std::string(kGeneratorVersion) : c.generator)
       + " ecrit=" + quote(c.written) + "\n";
    for (const auto& [key, e] : c.entries) {
        s += "element cle=" + quote(e.key) + " genre=" + quote(e.kind) + " chemin=" + quote(e.path) + " contenu=" + e.contentHash + " config=" + e.configHash
           + " dependances=" + e.depsHash + " interface=" + e.ifaceHash + " echec=" + quote(e.failedHash) + " valide=" + quote(e.lastValid)
           + " genere=" + quote(e.lastGenerated) + " compile=" + quote(e.lastCompiled) + " generateur=" + quote(e.generator)
           + " generation=" + std::string(stateKey(e.generation)) + " compilation=" + std::string(stateKey(e.compilation))
           + " artefact=" + quote(e.artifact) + " empreinte_artefact=" + quote(e.artifactHash) + "\n";
        for (const auto& [dk, dh] : e.depHashes) {
            const auto sig = e.depSigs.find(dk);
            s += "dependance de=" + quote(e.key) + " vers=" + quote(dk) + " empreinte=" + dh + " signature=" + quote(sig != e.depSigs.end() ? sig->second : std::string{}) + "\n";
        }
        for (const auto& d : e.diagnostics) s += diagLine(e.key, d);
    }
    for (const auto& d : c.project) s += diagLine(kProjectOwner, d);
    s += "fin\n";
    return s;
}

Cache parseCache(std::string_view text) {
    Cache c;
    if (text.empty()) { c.status = Cache::Status::Absent; c.why = "cache absent (premier build, ou fichier supprim\xC3\xA9)"; return c; }
    std::size_t at = 0, lineNo = 0;
    bool head = false, end = false;
    while (at < text.size()) {
        const auto e = text.find('\n', at);
        std::string line(text.substr(at, e == std::string_view::npos ? std::string_view::npos : e - at));
        at = e == std::string_view::npos ? text.size() : e + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line == "fin") { end = true; break; }
        Record r;
        std::string why;
        if (!parseRecord(line, r, why)) {
            c.status = Cache::Status::Corrupt;
            c.why = "cache illisible : build-cache.txt, ligne " + std::to_string(lineNo) + " (" + why + ")";
            c.entries.clear();
            return c;
        }
        const auto get = [&r](const char* k) { const auto* v = r.get(k); return v ? *v : std::string{}; };
        if (r.word == "cache_build") {
            head = true;
            if (get("format") != std::to_string(kCacheFormat)) {
                c.status = Cache::Status::Incompatible;
                c.why = "cache d'un autre format (" + get("format") + ", attendu " + std::to_string(kCacheFormat) + ")";
                return c;
            }
            c.generator = get("generateur");
            c.written = get("ecrit");
        } else if (r.word == "element") {
            CacheEntry en;
            en.key = get("cle");
            en.kind = get("genre");
            en.path = get("chemin");
            en.contentHash = get("contenu");
            en.configHash = get("config");
            en.depsHash = get("dependances");
            en.ifaceHash = get("interface");
            en.failedHash = get("echec");
            en.lastValid = get("valide");
            en.lastGenerated = get("genere");
            en.lastCompiled = get("compile");
            en.generator = get("generateur");
            en.generation = stateFromKey(get("generation")).value_or(State::NotGenerated);
            en.compilation = stateFromKey(get("compilation")).value_or(State::CompilationRequired);
            en.artifact = get("artefact");
            en.artifactHash = get("empreinte_artefact");
            c.entries[en.key] = std::move(en);
        } else if (r.word == "dependance") {
            auto it = c.entries.find(get("de"));
            if (it == c.entries.end()) continue;
            it->second.depHashes[get("vers")] = get("empreinte");
            it->second.depSigs[get("vers")] = get("signature");
        } else if (r.word == "diagnostic") {
            auto it = c.entries.find(get("de"));
            const bool project = get("de") == kProjectOwner;
            if (it == c.entries.end() && !project) continue;
            Diagnostic d;
            d.severity = severityFrom(get("gravite"));
            d.code = get("code");
            d.category = get("categorie");
            d.message = get("message");
            d.line = std::atoi(get("ligne").c_str());
            d.column = std::atoi(get("colonne").c_str());
            d.length = std::atoi(get("longueur").c_str());
            d.step = get("etape");
            d.date = get("date");
            d.suggestion = get("suggestion");
            d.view = static_cast<Id>(std::strtoul(get("vue").c_str(), nullptr, 10));
            d.object = static_cast<Id>(std::strtoul(get("objet").c_str(), nullptr, 10));
            d.script = static_cast<Id>(std::strtoul(get("script").c_str(), nullptr, 10));
            d.item = static_cast<Id>(std::strtoul(get("item").c_str(), nullptr, 10));
            d.property = get("propriete");
            d.file = get("fichier");
            if (project) { c.project.push_back(std::move(d)); continue; }
            d.element = it->second.key;
            d.path = it->second.path;
            it->second.diagnostics.push_back(std::move(d));
        }
    }
    if (!head || !end) {
        c.status = Cache::Status::Corrupt;
        c.why = !head ? "cache illisible : build-cache.txt sans en-t\xC3\xAAte" : "cache illisible : build-cache.txt coup\xC3\xA9 (pas de ligne \xC2\xAB fin \xC2\xBB)";
        c.entries.clear();
        return c;
    }
    c.status = Cache::Status::Ok;
    return c;
}

std::string buildFolderOf(const std::string& projectFolder) { return (fs::path(projectFolder) / ".xpg" / "build").string(); }

Cache loadCache(const std::string& buildFolder) {
    // Absent, coupe ou d'une autre version : tout est refait et la raison est dite
    // (le .bak n'est qu'une copie de secours, pour l'utilisateur).
    std::string text;
    if (!core::readFileAll(fs::path(buildFolder) / kCacheFile, text)) {
        Cache c;
        c.status = Cache::Status::Absent;
        c.why = "cache absent (premier build, ou fichier supprim\xC3\xA9)";
        return c;
    }
    return parseCache(text);
}

core::Status saveCache(const Cache& c, const std::string& buildFolder) {
    return core::writeFileAtomic(fs::path(buildFolder) / kCacheFile, serialize(c));
}

// ============================================================== l'analyse ====
const Item* Analysis::item(std::string_view key) const {
    const auto it = byKey.find(std::string(key));
    return it == byKey.end() ? nullptr : &items[it->second];
}
const Element* Analysis::element(std::string_view key) const {
    const auto it = byKey.find(std::string(key));
    return it == byKey.end() ? nullptr : &elements[it->second];
}
bool Analysis::upToDate() const {
    if (!fullReason.empty() || !deleted.empty()) return false;
    for (const auto& i : items) if (i.status != Status::UpToDate) return false;
    return true;
}

ArtifactCheck diskArtifacts(const std::string& buildFolder) {
    return [buildFolder](const std::string& rel, const std::string& h) {
        std::string text;
        return !rel.empty() && core::readFileAll(fs::path(buildFolder) / rel, text) && hash(text) == h;
    };
}

Analysis analyse(std::vector<Element> elements, const Cache& cache, const ArtifactCheck& artifacts) {
    Analysis a;
    a.elements = std::move(elements);
    a.items.resize(a.elements.size());
    for (std::size_t k = 0; k < a.elements.size(); ++k) a.byKey[a.elements[k].key] = k;
    if (cache.status != Cache::Status::Ok) a.fullReason = cache.why.empty() ? "cache absent" : cache.why;
    // les empreintes
    for (std::size_t k = 0; k < a.elements.size(); ++k) {
        const auto& e = a.elements[k];
        auto& it = a.items[k];
        it.element = k;
        it.contentHash = hash(e.content);
        it.configHash = hash(e.config);
        it.ifaceHash = hash(e.iface);
    }
    for (std::size_t k = 0; k < a.elements.size(); ++k) {
        const auto& e = a.elements[k];
        auto& it = a.items[k];
        std::string all;
        for (const auto& d : e.deps) {
            if (d.key.empty()) { it.missing.push_back(d.name); all += "?" + d.name + ";"; continue; }
            const auto j = a.byKey.find(d.key);
            if (j == a.byKey.end()) { it.missing.push_back(d.name); continue; }
            const auto& di = a.items[j->second];
            const std::string h = d.mode == DepMode::Content ? hash(di.contentHash + di.configHash + di.ifaceHash) : di.ifaceHash;
            it.depHashes[d.key] = h;
            it.depSigs[d.key] = a.elements[j->second].iface.size() > 160 ? a.elements[j->second].iface.substr(0, 160) + "\xE2\x80\xA6" : a.elements[j->second].iface;
        }
        for (const auto& [dk, dh] : it.depHashes) all += dk + "=" + dh + ";";
        it.depsHash = hash(all);
    }
    // la comparaison au cache
    for (std::size_t k = 0; k < a.elements.size(); ++k) {
        const auto& e = a.elements[k];
        auto& it = a.items[k];
        if (!a.fullReason.empty()) { it.status = Status::Full; it.reasons.push_back(a.fullReason); continue; }
        const auto ce = cache.entries.find(e.key);
        if (ce == cache.entries.end()) { it.status = Status::Added; it.reasons.push_back("nouvel \xC3\xA9l\xC3\xA9ment, jamais g\xC3\xA9n\xC3\xA9r\xC3\xA9"); ++a.counts.added; continue; }
        const auto& en = ce->second;
        if (en.path != e.path) { it.status = Status::Renamed; it.reasons.push_back("renomm\xC3\xA9 ou d\xC3\xA9plac\xC3\xA9 : " + en.path + " \xE2\x86\x92 " + e.path); ++a.counts.renamed; }
        const std::string& before = en.failedHash.empty() ? en.contentHash : en.failedHash;
        if (before != it.contentHash || en.configHash != it.configHash) {
            if (it.status != Status::Renamed) { it.status = Status::Modified; ++a.counts.modified; }
            it.reasons.push_back(before != it.contentHash ? (compilable(e.kind) ? "code ou contenu modifi\xC3\xA9" : "contenu modifi\xC3\xA9") : "configuration modifi\xC3\xA9" "e");
        }
        if (!it.missing.empty()) {
            if (it.status == Status::UpToDate) { it.status = Status::MissingDependency; ++a.counts.missing; }
            std::string m;
            for (const auto& x : it.missing) m += (m.empty() ? "" : ", ") + x;
            it.reasons.push_back("introuvable : " + m);
        }
        if (it.status == Status::UpToDate && (en.generation == State::GenerationFailed || en.compilation == State::CompilationFailed || en.generation == State::InvalidDependency)) {
            it.status = Status::Failed;
            it.reasons.push_back(en.generation == State::GenerationFailed ? "g\xC3\xA9n\xC3\xA9ration \xC3\xA9" "chou\xC3\xA9" "e au dernier build"
                                 : en.generation == State::InvalidDependency ? "d\xC3\xA9pendance invalide au dernier build" : "compilation \xC3\xA9" "chou\xC3\xA9" "e au dernier build");
            ++a.counts.failed;
        }
        if (it.status == Status::UpToDate && en.depsHash != it.depsHash) {
            it.status = Status::Obsolete;
            std::string why = "d\xC3\xA9pendances modifi\xC3\xA9" "es";
            for (const auto& [dk, dh] : it.depHashes) {
                const auto b = en.depHashes.find(dk);
                if (b != en.depHashes.end() && b->second == dh) continue;
                const Element* de = a.element(dk);
                const auto sig = en.depSigs.find(dk);
                const std::string now = it.depSigs[dk];
                if (b == en.depHashes.end()) why = "nouvelle d\xC3\xA9pendance : " + (de ? de->name : dk);
                else if (sig != en.depSigs.end() && !sig->second.empty() && sig->second != now) why = "d\xC3\xA9pendance modifi\xC3\xA9" "e : " + sig->second + " \xE2\x86\x92 " + now;
                else why = "d\xC3\xA9pendance modifi\xC3\xA9" "e : " + (de ? de->name : dk);
                break;
            }
            it.reasons.push_back(why);
            ++a.counts.obsolete;
        }
        if (it.status == Status::UpToDate && en.generator != kGeneratorVersion) {
            it.status = Status::Obsolete;
            it.reasons.push_back("g\xC3\xA9n\xC3\xA9rateur " + (en.generator.empty() ? std::string("?") : en.generator) + " \xE2\x86\x92 " + std::string(kGeneratorVersion));
            ++a.counts.obsolete;
        }
        if (it.status == Status::UpToDate && artifacts && !artifacts(en.artifact, en.artifactHash)) {
            it.status = Status::ArtifactMissing;
            it.reasons.push_back("artefact absent ou alt\xC3\xA9r\xC3\xA9 : " + en.artifact);
            ++a.counts.artifacts;
        }
        if (it.status == Status::UpToDate && compilable(e.kind) && en.compilation != State::Compiled) {
            it.status = Status::NeedsCompile;
            it.reasons.push_back("g\xC3\xA9n\xC3\xA9r\xC3\xA9, pas encore compil\xC3\xA9");
        }
        if (it.status == Status::UpToDate) ++a.counts.upToDate;
    }
    for (const auto& [key, en] : cache.entries)
        if (!a.byKey.count(key) && a.fullReason.empty()) { a.deleted.push_back(en); ++a.counts.deleted; }
    // une dependance en echec (ou introuvable) rend l'element invalide, jusqu'au point fixe
    for (bool changed = true; changed;) {
        changed = false;
        for (auto& it : a.items) {
            if (it.status != Status::UpToDate && it.status != Status::Obsolete && it.status != Status::NeedsCompile && it.status != Status::ArtifactMissing) continue;
            for (const auto& d : a.elements[it.element].deps) {
                if (d.key.empty()) continue;
                const Item* di = a.item(d.key);
                if (!di || (di->status != Status::Failed && di->status != Status::MissingDependency && di->status != Status::DependencyFailed)) continue;
                it.status = Status::DependencyFailed;
                it.reasons.push_back(d.name + " est en \xC3\xA9" "chec");
                changed = true;
                break;
            }
        }
    }
    return a;
}

Shown shown(const Analysis& a, const Cache& c, std::string_view key) {
    Shown s;
    const Item* it = a.item(key);
    const Element* e = a.element(key);
    if (!it || !e) return s;
    const auto ce = c.entries.find(std::string(key));
    const CacheEntry* en = ce == c.entries.end() ? nullptr : &ce->second;
    if (en)
        for (const auto& d : en->diagnostics) {
            if (d.blocking()) ++s.errors;
            else if (d.severity == Severity::Warning) ++s.warnings;
        }
    std::string reasons;
    for (const auto& r : it->reasons) reasons += (reasons.empty() ? "" : " ; ") + r;
    const bool code = compilable(e->kind);
    switch (it->status) {
        case Status::Full:   // jamais construit (pas de cache) : non genere ; un cache illisible : a regenerer
            s.state = c.status == Cache::Status::Absent && !en ? State::NotGenerated : State::GenerationRequired;
            s.detail = s.state == State::NotGenerated ? "jamais g\xC3\xA9n\xC3\xA9r\xC3\xA9" : "g\xC3\xA9n\xC3\xA9ration requise";
            break;
        case Status::Added: s.state = State::NotGenerated; s.detail = "jamais g\xC3\xA9n\xC3\xA9r\xC3\xA9"; break;
        case Status::Modified:
        case Status::Renamed: s.state = State::Modified; s.detail = code ? "compilation requise" : "g\xC3\xA9n\xC3\xA9ration requise"; break;
        case Status::MissingDependency:
        case Status::DependencyFailed: s.state = State::InvalidDependency; s.detail = "d\xC3\xA9pendance invalide"; break;
        case Status::Obsolete: s.state = State::Obsolete; s.detail = "g\xC3\xA9n\xC3\xA9r\xC3\xA9 depuis une version d\xC3\xA9pass\xC3\xA9" "e"; break;
        case Status::ArtifactMissing: s.state = State::GenerationRequired; s.detail = "artefact absent"; break;
        case Status::NeedsCompile: s.state = State::CompilationRequired; s.detail = "g\xC3\xA9n\xC3\xA9r\xC3\xA9, compilation requise"; break;
        case Status::Failed:
            s.state = en && en->generation == State::GenerationFailed ? State::GenerationFailed
                    : en && en->generation == State::InvalidDependency ? State::InvalidDependency : State::CompilationFailed;
            s.detail = std::to_string(s.errors) + (s.errors > 1 ? " erreurs" : " erreur");
            break;
        case Status::UpToDate: s.state = State::UpToDate; s.detail = code ? "g\xC3\xA9n\xC3\xA9r\xC3\xA9 et compil\xC3\xA9" : "g\xC3\xA9n\xC3\xA9r\xC3\xA9"; break;
    }
    s.tip = std::string(stateLabel(s.state)) + (s.detail.empty() ? "" : ", " + s.detail) + (reasons.empty() ? "" : " : " + reasons);
    if (en && !en->lastValid.empty() && it->status == Status::UpToDate) s.tip += " (dernier build valide : " + en->lastValid + ")";
    if (s.warnings && it->status == Status::UpToDate) s.tip += " \xC2\xB7 " + std::to_string(s.warnings) + (s.warnings > 1 ? " avertissements" : " avertissement");
    return s;
}

// ============================================================== le plan ======
std::string_view modeLabel(Mode m) noexcept {
    switch (m) {
        case Mode::Generate: return "G\xC3\xA9n\xC3\xA9rer";
        case Mode::Regenerate: return "R\xC3\xA9g\xC3\xA9n\xC3\xA9rer";
        case Mode::Compile: return "Compiler";
        case Mode::GenerateCompile: return "G\xC3\xA9n\xC3\xA9rer et compiler";
        case Mode::RegenerateCompile: return "R\xC3\xA9g\xC3\xA9n\xC3\xA9rer et compiler";
        case Mode::Start: return "D\xC3\xA9marrer";
        case Mode::Clean: return "Nettoyer les artefacts";
    }
    return "?";
}

bool inScope(const Element& e, const std::vector<std::string>& scope) {
    if (scope.empty()) return true;
    for (const auto& s : scope) {
        if (s == e.key) return true;
        if (e.path == s || (e.path.size() > s.size() && e.path.compare(0, s.size(), s) == 0 && e.path[s.size()] == '/')) return true;
    }
    return false;
}

Plan plan(const Analysis& a, const Request& r) {
    Plan p;
    const bool force = r.mode == Mode::Regenerate || r.mode == Mode::RegenerateCompile;
    const bool compile = r.mode != Mode::Generate && r.mode != Mode::Regenerate && r.mode != Mode::Clean;
    std::vector<char> gen(a.elements.size(), 0), comp(a.elements.size(), 0);
    const auto stale = [](Status s) { return s != Status::UpToDate && s != Status::NeedsCompile; };
    for (std::size_t k = 0; k < a.elements.size(); ++k) {
        const auto& e = a.elements[k];
        if (!inScope(e, r.scope)) continue;
        if (force || stale(a.items[k].status)) gen[k] = 1;
        if (compile && compilable(e.kind)) {
            const bool chosen = r.explicitSelection && !r.scope.empty() && std::find(r.scope.begin(), r.scope.end(), e.key) != r.scope.end();
            if (gen[k] || a.items[k].status == Status::NeedsCompile || chosen) comp[k] = 1;
        }
    }
    // les dependances obligatoires (hors de la selection) qui ne sont pas a jour
    std::vector<std::size_t> todo;
    for (std::size_t k = 0; k < a.elements.size(); ++k) if (gen[k] || comp[k]) todo.push_back(k);
    while (!todo.empty()) {
        const std::size_t k = todo.back();
        todo.pop_back();
        for (const auto& d : a.elements[k].deps) {
            if (d.key.empty()) continue;
            const auto j = a.byKey.find(d.key);
            if (j == a.byKey.end()) continue;
            const std::size_t m = j->second;
            bool added = false;
            if (!gen[m] && stale(a.items[m].status)) { gen[m] = 1; added = true; if (compile && compilable(a.elements[m].kind)) comp[m] = 1; }
            if (compile && compilable(a.elements[m].kind) && !comp[m] && a.items[m].status == Status::NeedsCompile) { comp[m] = 1; added = true; }
            if (added) todo.push_back(m);
        }
    }
    for (std::size_t k = 0; k < a.elements.size(); ++k) {
        if (gen[k]) p.generate.push_back(k);
        if (comp[k]) p.compile.push_back(k);
    }
    p.reused = a.elements.size() - p.generate.size();
    return p;
}

// ============================================================== l'execution ==
std::string_view phaseLabel(Phase ph) noexcept {
    switch (ph) {
        case Phase::Analyse: return "Analyse des modifications";
        case Phase::Api: return "G\xC3\xA9n\xC3\xA9ration de l'API";
        case Phase::Ihm: return "G\xC3\xA9n\xC3\xA9ration de l'IHM";
        case Phase::Compile: return "Compilation";
        case Phase::Validate: return "Validation";
        case Phase::Start: return "D\xC3\xA9marrage de la simulation";
        case Phase::Restore: return "Restauration des donn\xC3\xA9" "es r\xC3\xA9manentes";
        case Phase::Count: break;
    }
    return "?";
}

namespace {
std::string artifactPathOf(const Element& e) {
    char step[24];
    std::snprintf(step, sizeof step, "%02d-", stepOf(e.kind) + 1);
    const std::string dir = std::string(areaOf(e.kind) == Area::Api ? "api/" : "ihm/") + step + lower(sanitize(stepName(areaOf(e.kind), stepOf(e.kind))));
    return dir + "/" + sanitize(e.key) + "-" + sanitize(e.name) + ".txt";
}

// La forme generee d'un element : ce que la simulation charge, au format texte
// des fichiers IHM. Une vue : ses instances de symboles deballees.
std::string generatedText(const Project& p, const Element& e, const Item& it) {
    std::string s = "# XPGAnalyser - artefact g\xC3\xA9n\xC3\xA9r\xC3\xA9 (ne pas modifier a la main)\n";
    s += "artefact cle=" + quote(e.key) + " genre=" + quote(kindKey(e.kind)) + " chemin=" + quote(e.path) + " generateur=" + std::string(kGeneratorVersion)
       + " contenu=" + it.contentHash + " config=" + it.configHash + " dependances=" + it.depsHash + "\n";
    for (const auto& d : e.deps) s += "dependance nom=" + quote(d.name) + " cle=" + quote(d.key) + " mode=" + (d.mode == DepMode::Content ? "contenu" : "interface") + " via=" + quote(d.via) + "\n";
    switch (e.kind) {
        case ElementKind::View:
        case ElementKind::Popup:
            if (const View* v = p.view(e.id)) s += serializeView(expandInstances(p, *v));
            break;
        case ElementKind::Symbol:
        case ElementKind::ViewTemplate:
            if (const View* v = p.view(e.id)) { s += serializeView(*v); break; }
            s += e.content + "\n";
            break;
        default:
            s += "contenu " + quote(e.content) + "\n";
            if (!e.config.empty()) s += "config " + quote(e.config) + "\n";
            break;
    }
    s += "fin\n";
    return s;
}

// La validation (les controles de Generer) : ce qui empeche vraiment la
// simulation de tourner - la structure (vues, objets, calques, identifiants,
// references circulaires, la vue de demarrage), les ressources, la coherence
// API / IHM (une variable inexistante), le code. Le reste de Generer (bornes
// d'une recette, parametres d'une action, deux ecritures sur un registre,
// qualite, langues...) est dit en avertissement : la simulation tournait avec
// avant, elle tourne encore.
bool blockingValidation(const Issue& i) {
    static const std::set<std::string, std::less<>> kBlocking = {
        "Projet", "Vue", "Objet", "Ressource", "Variable", "Type", "Script", "Fonction", "Expression", "Symbole", "Popup"};
    return i.severity == Issue::Severity::Error && kBlocking.count(i.category) != 0;
}

// Ou va un constat de Compiler ou de Generer : la cle de son element.
std::string elementOfIssue(const Project& p, const Issue& i) {
    if (i.script != kNoId) {
        if (p.viewOfScript(i.script) != kNoId) return keyOf(ElementKind::ViewScript, i.script);
        return keyOf(ElementKind::Script, i.script);
    }
    const std::string& c = i.category;
    if (c == "Fonction" && i.item != kNoId) return keyOf(ElementKind::Function, i.item);
    if (c == "Alarme" && i.item != kNoId) return keyOf(ElementKind::Alarm, i.item);
    if (c == "Recette" && i.item != kNoId) return keyOf(ElementKind::Recipe, i.item);
    if (c == "Utilisateur" || c == "S\xC3\xA9" "curit\xC3\xA9" || c == "Groupe" || c == "R\xC3\xB4le") return "securite";
    if (c == "Historique" || c == "Rapport") return "historiques";
    if (c == "Unit\xC3\xA9") return "affichages";
    if (c == "Variable IHM" && !i.property.empty()) if (const Variable* v = p.variable(i.property)) return keyOf(ElementKind::Variable, v->id);
    if (c == "Op\xC3\xA9rateur") {
        if (i.view != kNoId) return keyOf(ElementKind::Symbol, i.view);
        for (const auto& t : p.programs.types) for (const auto& o : t.operators) if (o.id == i.item) return keyOf(ElementKind::Type, t.id);
    }
    if (i.view != kNoId) {
        const View* v = p.view(i.view);
        if (!v) return {};
        if (c == "Action") return keyOf(ElementKind::Actions, v->id);
        if (c == "Expression" || c == "Texte" || c == "Popup" || c == "Param\xC3\xA8tre" || c == "Symbole" || c == "Courbe") return keyOf(ElementKind::Animations, v->id);
        if (c == "Fonction" && i.item != kNoId) return keyOf(ElementKind::SymbolFunction, i.item);
        return keyOf(viewKind(*v), v->id);
    }
    return {};
}

std::string fileOfElement(const Project& p, const Element& e) {
    switch (e.kind) {
        case ElementKind::View: case ElementKind::Popup: case ElementKind::Symbol: case ElementKind::ViewTemplate:
            if (const View* v = p.view(e.id)) return "ihm/vues/" + viewFileName(*v);
            break;
        case ElementKind::ViewScript: case ElementKind::Animations: case ElementKind::Actions: case ElementKind::SymbolFunction:
            if (const View* v = p.view(e.owner)) return "ihm/vues/" + viewFileName(*v);
            break;
        case ElementKind::Script: case ElementKind::Function: return "ihm/scripts/";
        default: break;
    }
    return "ihm/ihm.txt";
}

// La compilation d'un ensemble d'elements : les controles de Compiler, limites a eux.
std::map<std::string, std::vector<Diagnostic>> compileElements(const Project& p, const Analysis& a, const std::vector<std::size_t>& list,
                                                              const Options& o) {
    CompileFocus f;
    std::set<std::string> wanted;
    std::vector<const Element*> symbolFns;
    for (const auto k : list) {
        const auto& e = a.elements[k];
        wanted.insert(e.key);
        switch (e.kind) {
            case ElementKind::Variable: f.variables = true; break;
            case ElementKind::Displays: case ElementKind::History: f.rest = true; break;
            case ElementKind::Function: f.functions.insert(e.id); break;
            case ElementKind::Type: f.operatorOwners.insert(e.id); break;
            case ElementKind::Symbol: f.operatorOwners.insert(e.id); break;
            case ElementKind::SymbolFunction: symbolFns.push_back(&e); break;
            case ElementKind::Script: case ElementKind::ViewScript: f.scripts.insert(e.id); break;
            case ElementKind::Animations: f.animations.insert(e.id); break;
            case ElementKind::Actions: f.actions.insert(e.id); break;
            case ElementKind::Alarm: f.alarms.insert(e.id); break;
            case ElementKind::Recipe: f.recipes.insert(e.id); break;
            case ElementKind::Security: f.users = true; break;
            default: break;
        }
    }
    std::map<std::string, std::vector<Diagnostic>> out;
    for (const auto& k : wanted) out[k];
    for (const auto& i : compileWith(p, o.plcHasName, o.plcPaths, f)) {
        if (i.category == "Bilan") continue;
        const std::string key = elementOfIssue(p, i);
        if (!wanted.count(key)) continue;
        auto d = fromIssue(i, "Compilation");
        d.element = key;
        out[key].push_back(std::move(d));
    }
    // Les fonctions des symboles : leur corps (declarations, retour) et leurs noms,
    // dans la portee du symbole (ses parametres, ses autres fonctions).
    const TypeKnown knownType = [&p](std::string_view t) { return !types::membersOf(p, t).empty() || findEnumeration(p, t) != nullptr; };
    for (const Element* e : symbolFns) {
        const View* sym = p.view(e->owner);
        const HmiFunction* fn = nullptr;
        if (sym) for (const auto& g : sym->functions) if (g.id == e->id) fn = &g;
        if (!fn) continue;
        for (const auto& d : checkFunction(*fn, knownType)) {
            Diagnostic x;
            x.severity = d.severity == ScriptDiagnostic::Severity::Error ? Severity::Error : Severity::Warning;
            x.code = "Fonction";
            x.category = "Fonction";
            x.message = d.message;
            x.line = d.line;
            x.step = "Compilation";
            x.view = sym->id;
            x.item = fn->id;
            x.property = fn->name;
            x.element = e->key;
            out[e->key].push_back(std::move(x));
        }
        scriptcheck::Scope s;
        s.project = &p;
        s.view = sym;
        s.function = fn;
        s.plcKnown = o.plcHasName;
        s.plc = o.plcPaths;
        for (const auto& fd : scriptcheck::check(s, fn->body, fn->decls, decl::Role::Function)) {
            Diagnostic x;
            x.severity = fd.severity == scriptcheck::Finding::Severity::Error ? Severity::Error : Severity::Warning;
            x.code = "Fonction";
            x.category = "Fonction";
            x.message = fd.message;
            x.line = fd.line;
            x.column = fd.column;
            x.length = fd.length;
            x.suggestion = fd.suggestion.empty() ? std::string{} : "Veux-tu dire " + fd.suggestion + " ?";
            x.step = "Compilation";
            x.view = sym->id;
            x.item = fn->id;
            x.property = fn->name;
            x.element = e->key;
            out[e->key].push_back(std::move(x));
        }
    }
    return out;
}

// Generer d'un element : ce qui peut le faire echouer avant d'ecrire.
std::vector<Diagnostic> generationProblems(const Analysis& a, const Element& e, const Item& it, const std::set<std::string>& failedNow) {
    std::vector<Diagnostic> out;
    const auto D = [&](std::string code, std::string msg, std::string sugg) {
        Diagnostic d;
        d.severity = Severity::Error;
        d.code = std::move(code);
        d.category = "G\xC3\xA9n\xC3\xA9ration";
        d.message = std::move(msg);
        d.suggestion = std::move(sugg);
        d.step = "G\xC3\xA9n\xC3\xA9ration";
        d.element = e.key;
        d.path = e.path;
        if (e.kind == ElementKind::View || e.kind == ElementKind::Popup || e.kind == ElementKind::Symbol || e.kind == ElementKind::ViewTemplate) d.view = e.id;
        out.push_back(std::move(d));
    };
    // un element sans code ne se genere pas s'il cite ce qui n'existe pas (le code le dit a la compilation)
    if (!compilable(e.kind))
        for (const auto& d : e.deps) {
            if (!d.key.empty()) continue;
            if (d.via == "instance") D("E120", "Symbole introuvable : " + d.name, "Recr\xC3\xA9" "e le symbole, ou retire ses instances de " + e.name + ".");
            else if (d.via == "mod\xC3\xA8le" || d.via == "en-t\xC3\xAAte" || d.via == "pied de page") D("E121", "Mod\xC3\xA8le de vue introuvable : " + d.name, "");
            else if (d.via == "ressource") D("E130", "Ressource introuvable : " + d.name, "Ajoute le fichier dans Ressources, ou retire la r\xC3\xA9" "f\xC3\xA9rence.");
            else D("E132", "R\xC3\xA9" "f\xC3\xA9rence introuvable : " + d.name + " (" + d.via + ")", "");
        }
    if (!out.empty()) return out;
    for (const auto& d : e.deps) {
        if (d.key.empty() || !failedNow.count(d.key)) continue;
        const Element* de = a.element(d.key);
        // le code d'une dependance en echec n'empeche pas de generer : la compilation dira l'appel
        if (compilable(e.kind) && d.via != "vue" && d.via != "symbole") continue;
        D("E140", "D\xC3\xA9pendance invalide : " + (de ? de->path : d.name) + " est en \xC3\xA9" "chec ; " + e.name + " n'est pas g\xC3\xA9n\xC3\xA9r\xC3\xA9",
          "Corrige d'abord " + (de ? de->name : d.name) + ".");
        out.back().code = "E140";
        break;
    }
    (void)it;
    return out;
}
} // namespace

Report run(const Project& p, const ApiInfo& api, const Request& req, const Options& o, const ProgressFn& onProgress, const std::atomic<bool>* cancel) {
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    Report rep;
    Progress pr;
    const std::string now = o.now.empty() ? nowText() : o.now;
    const auto log = [&rep](Severity s, std::string cat, std::string msg, std::string el = {}, int line = 0, int col = 0) {
        rep.log.push_back({s, std::move(cat), std::move(msg), std::move(el), line, col});
    };
    const auto publish = [&] { if (onProgress) onProgress(pr); };
    const auto cancelled = [cancel] { return cancel && cancel->load(); };
    // F et G d'un demarrage : ce que la fin du build en fait, jamais « a venir » apres
    // la fin. Valide, l'appelant demarre la simulation (le contrat de Mode::Start) ;
    // rien n'est encore restaure (la remanence de simulation n'existe pas encore).
    const auto settleStart = [&] {
        if (req.mode != Mode::Start) return;
        const int f = static_cast<int>(Phase::Start), g = static_cast<int>(Phase::Restore);
        if (rep.ok) {
            pr.phases[f] = PhaseState::Done;
            pr.phaseNotes[f] = "la simulation d\xC3\xA9marre";
            // 1.11.15 : la remanence de simulation - ce qu'elle rend au demarrage, ou pourquoi rien.
            pr.phases[g] = req.restore.empty() ? PhaseState::Skipped : PhaseState::Done;
            pr.phaseNotes[g] = !req.restore.empty() ? req.restore
                             : req.restoreOff      ? std::string("r\xC3\xA9manence d\xC3\xA9sactiv\xC3\xA9" "e")
                                                   : std::string("rien \xC3\xA0 restaurer");
        } else {
            pr.phases[f] = pr.phases[g] = PhaseState::Cancelled;
            pr.phaseNotes[f] = pr.phaseNotes[g] = rep.cancelled ? std::string("annul\xC3\xA9")
                                                                : "bloqu\xC3\xA9 : " + std::to_string(rep.errors) + (rep.errors > 1 ? " erreurs" : " erreur");
        }
        publish();
    };
    const bool disk = !o.buildFolder.empty();
    // ---- A. analyse ----
    pr.phases[static_cast<int>(Phase::Analyse)] = PhaseState::Running;
    pr.step = "A. Analyse des modifications";
    if (req.mode != Mode::Start) {
        pr.phases[static_cast<int>(Phase::Start)] = PhaseState::NotAsked;
        pr.phases[static_cast<int>(Phase::Restore)] = PhaseState::NotAsked;
    }
    publish();
    log(Severity::Information, "Analyse", std::string(modeLabel(req.mode)) + " : analyse des modifications\xE2\x80\xA6");
    Cache cache;
    if (o.cache) cache = *o.cache;
    else if (disk) cache = loadCache(o.buildFolder);
    else cache.status = Cache::Status::Absent, cache.why = "cache absent (premier build)";
    const ArtifactCheck artifacts = o.artifacts ? o.artifacts : (disk ? diskArtifacts(o.buildFolder) : ArtifactCheck{});
    rep.analysis = analyse(collect(p, api), cache, artifacts);
    const Analysis& a = rep.analysis;
    rep.fullReason = a.fullReason;
    if (!a.fullReason.empty()) log(Severity::Warning, "Analyse", "R\xC3\xA9g\xC3\xA9n\xC3\xA9ration compl\xC3\xA8te : " + a.fullReason + ". Le cache sera reconstruit.");
    {
        const auto& c = a.counts;
        std::string what;
        const auto part = [&what](std::size_t n, const char* one, const char* many) { if (n) what += (what.empty() ? "" : ", ") + std::to_string(n) + " " + (n > 1 ? many : one); };
        part(c.added, "ajout\xC3\xA9", "ajout\xC3\xA9s");
        part(c.modified, "modifi\xC3\xA9", "modifi\xC3\xA9s");
        part(c.renamed, "renomm\xC3\xA9", "renomm\xC3\xA9s");
        part(c.deleted, "supprim\xC3\xA9", "supprim\xC3\xA9s");
        part(c.obsolete, "obsol\xC3\xA8te", "obsol\xC3\xA8tes");
        part(c.artifacts, "artefact absent", "artefacts absents");
        part(c.failed, "en \xC3\xA9" "chec", "en \xC3\xA9" "chec");
        part(c.missing, "d\xC3\xA9pendance introuvable", "d\xC3\xA9pendances introuvables");
        if (a.fullReason.empty()) log(Severity::Information, "Analyse", what.empty() ? "Aucune modification depuis le dernier build." : what + " depuis le dernier build.");
        pr.phaseNotes[static_cast<int>(Phase::Analyse)] = what.empty() ? (a.fullReason.empty() ? "\xC3\xA0 jour" : "tout") : what;
    }
    log(Severity::Information, "Analyse", "Calcul des d\xC3\xA9pendances\xE2\x80\xA6");
    const Plan pl = plan(a, req);
    rep.reused = pl.reused;
    rep.tasks = pl.generate.size() + pl.compile.size();
    pr.total = rep.tasks;
    for (const auto k : pl.generate) {
        const auto& e = a.elements[k];
        (areaOf(e.kind) == Area::Api ? pr.api : pr.ihm)[stepOf(e.kind)].todo++;
    }
    pr.phases[static_cast<int>(Phase::Analyse)] = PhaseState::Done;
    // ---- le nettoyage ----
    if (req.mode == Mode::Clean) {
        std::size_t n = 0;
        for (auto it = cache.entries.begin(); it != cache.entries.end();) {
            const Element* e = a.element(it->first);
            const bool in = e ? inScope(*e, req.scope) : req.scope.empty();
            if (!in) { ++it; continue; }
            if (!it->second.artifact.empty()) {
                if (o.removeArtifact) o.removeArtifact(it->second.artifact);
                else if (disk) { std::error_code ec; fs::remove(fs::path(o.buildFolder) / it->second.artifact, ec); }
                ++n;
            }
            it = cache.entries.erase(it);
        }
        cache.generator = std::string(kGeneratorVersion);
        cache.written = now;
        if (disk) if (auto s = saveCache(cache, o.buildFolder); !s) log(Severity::Error, "Cache", "Le cache n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + s.error().message());
        log(Severity::Information, "Cache", "Nettoyage : " + std::to_string(n) + " artefact(s) supprim\xC3\xA9(s), entr\xC3\xA9" "es du cache retir\xC3\xA9" "es.");
        rep.ok = true;
        rep.cache = std::move(cache);
        rep.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        return rep;
    }
    // ---- rien a faire : « Projet a jour » ----
    if (rep.tasks == 0 && a.deleted.empty()) {
        // un element en echec (inchange) bloque encore le demarrage, comme une erreur du projet
        for (const auto& [key, en] : cache.entries)
            for (const auto& d : en.diagnostics) {
                const Element* el = a.element(key);
                if (d.blocking() && (!el || inScope(*el, req.scope))) ++rep.errors;
                else if (d.severity == Severity::Warning) ++rep.warnings;
                rep.diagnostics.push_back(d);
            }
        for (const auto& d : cache.project) {
            if (d.blocking() && req.scope.empty()) ++rep.errors;
            rep.diagnostics.push_back(d);
        }
        std::size_t toCompile = 0;
        for (std::size_t k = 0; k < a.elements.size(); ++k)
            if (a.items[k].status == Status::NeedsCompile && inScope(a.elements[k], req.scope)) ++toCompile;
        rep.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        rep.upToDate = rep.errors == 0 && toCompile == 0;
        rep.ok = rep.errors == 0;
        char ms[32];
        std::snprintf(ms, sizeof ms, "%.0f", std::max(1.0, rep.ms));
        if (rep.errors) log(Severity::Error, "Analyse", std::to_string(rep.errors) + " erreur(s) bloquante(s) du dernier build, toujours l\xC3\xA0 : corrige-les (double-clic sur l'erreur).");
        else if (toCompile) log(Severity::Success, "Analyse", "G\xC3\xA9n\xC3\xA9ration \xC3\xA0 jour (analyse en " + std::string(ms) + " ms) ; " + std::to_string(toCompile)
                                + " \xC3\xA9l\xC3\xA9ment(s) restent \xC3\xA0 compiler (Compiler, ou G\xC3\xA9n\xC3\xA9rer et compiler).");
        else log(Severity::Success, "Analyse", "Projet \xC3\xA0 jour : rien \xC3\xA0 g\xC3\xA9n\xC3\xA9rer ni \xC3\xA0 compiler (analyse en " + std::string(ms) + " ms, "
                                     + std::to_string(a.elements.size()) + " \xC3\xA9l\xC3\xA9ments r\xC3\xA9utilis\xC3\xA9s).");
        settleStart();
        rep.cache = std::move(cache);
        return rep;
    }
    log(Severity::Information, "Analyse", std::to_string(pl.generate.size()) + " \xC3\xA9l\xC3\xA9ment(s) \xC3\xA0 g\xC3\xA9n\xC3\xA9rer, " + std::to_string(pl.compile.size())
        + " \xC3\xA0 compiler ; " + std::to_string(pl.reused) + " r\xC3\xA9utilis\xC3\xA9(s) depuis le build pr\xC3\xA9" "c\xC3\xA9" "dent.");
    publish();
    if (!a.fullReason.empty()) { cache.entries.clear(); }
    cache.status = Cache::Status::Ok;
    std::set<std::string> failedNow;
    const auto writeArtifact = [&](const std::string& rel, const std::string& text) -> core::Status {
        if (o.writeArtifact) { o.writeArtifact(rel, text); return core::ok(); }
        if (!disk) return core::ok();
        return core::writeFileAtomic(fs::path(o.buildFolder) / rel, text, core::AtomicWrite{false, true});
    };
    // les elements supprimes : leur artefact part, leur entree aussi
    for (const auto& en : a.deleted) {
        if (!en.artifact.empty()) {
            if (o.removeArtifact) o.removeArtifact(en.artifact);
            else if (disk) { std::error_code ec; fs::remove(fs::path(o.buildFolder) / en.artifact, ec); }
        }
        cache.entries.erase(en.key);
        log(Severity::Information, "G\xC3\xA9n\xC3\xA9ration", "Supprim\xC3\xA9 : " + en.path + " (artefact retir\xC3\xA9)");
    }
    // ---- B. API, C. IHM : la generation ----
    for (const Area area : {Area::Api, Area::Ihm}) {
        const Phase ph = area == Area::Api ? Phase::Api : Phase::Ihm;
        std::vector<std::size_t> list;
        for (const auto k : pl.generate) if (areaOf(a.elements[k].kind) == area) list.push_back(k);
        if (list.empty()) { pr.phases[static_cast<int>(ph)] = PhaseState::Skipped; pr.phaseNotes[static_cast<int>(ph)] = "\xC3\xA0 jour, r\xC3\xA9utilis\xC3\xA9"; publish(); continue; }
        pr.phases[static_cast<int>(ph)] = PhaseState::Running;
        log(Severity::Information, "G\xC3\xA9n\xC3\xA9ration", area == Area::Api ? "G\xC3\xA9n\xC3\xA9ration de l'API\xE2\x80\xA6" : "G\xC3\xA9n\xC3\xA9ration de l'IHM\xE2\x80\xA6");
        for (const auto k : list) {
            if (cancelled()) break;
            const auto& e = a.elements[k];
            const auto& it = a.items[k];
            const int sub = stepOf(e.kind);
            pr.step = std::string(area == Area::Api ? "B. G\xC3\xA9n\xC3\xA9ration de l'API \xE2\x80\xBA " : "C. G\xC3\xA9n\xC3\xA9ration de l'IHM \xE2\x80\xBA ") + std::to_string(sub + 1) + ". " + std::string(stepName(area, sub));
            pr.element = e.path;
            pr.currentSub = area == Area::Ihm ? sub : -1;
            pr.live[e.key] = State::Generating;
            publish();
            auto& en = cache.entries[e.key];
            en.key = e.key;
            en.kind = std::string(kindKey(e.kind));
            en.path = e.path;
            auto problems = generationProblems(a, e, it, failedNow);
            if (problems.empty()) {
                const std::string text = generatedText(p, e, it);
                const std::string rel = artifactPathOf(e);
                if (auto s = writeArtifact(rel, text); !s) {
                    Diagnostic d;
                    d.severity = Severity::Error;
                    d.code = "E150";
                    d.category = "G\xC3\xA9n\xC3\xA9ration";
                    d.message = "artefact non \xC3\xA9" "crit : " + s.error().message();
                    d.step = "G\xC3\xA9n\xC3\xA9ration";
                    problems.push_back(std::move(d));
                } else {
                    en.contentHash = it.contentHash;
                    en.configHash = it.configHash;
                    en.depsHash = it.depsHash;
                    en.ifaceHash = it.ifaceHash;
                    en.depHashes = it.depHashes;
                    en.depSigs = it.depSigs;
                    en.failedHash.clear();
                    en.generator = std::string(kGeneratorVersion);
                    en.generation = State::Generated;
                    en.lastGenerated = now;
                    en.artifact = rel;
                    en.artifactHash = hash(text);
                    // les diagnostics de generation et de validation passes tombent ; ceux de la compilation attendent la leur
                    en.diagnostics.erase(std::remove_if(en.diagnostics.begin(), en.diagnostics.end(), [](const Diagnostic& d) { return d.step != "Compilation"; }), en.diagnostics.end());
                    if (!compilable(e.kind)) { en.compilation = State::Compiled; en.lastValid = now; }
                    else en.compilation = State::CompilationRequired;
                    ++rep.generated;
                    log(Severity::Information, "G\xC3\xA9n\xC3\xA9ration", std::string(area == Area::Api ? "G\xC3\xA9n\xC3\xA9ration API : " : "G\xC3\xA9n\xC3\xA9ration IHM : ") + e.path, e.key);
                    pr.live[e.key] = compilable(e.kind) ? State::CompilationRequired : State::Generated;
                    (area == Area::Api ? pr.api : pr.ihm)[sub].done++;
                }
            }
            if (!problems.empty()) {
                failedNow.insert(e.key);
                // une dependance introuvable (E120 a E132) ou en echec (E140) : Dependance invalide
                const bool depinv = problems.front().code == "E140" || problems.front().code == "E120" || problems.front().code == "E121"
                                 || problems.front().code == "E130" || problems.front().code == "E132";
                en.generation = depinv ? State::InvalidDependency : State::GenerationFailed;
                en.failedHash = it.contentHash;
                en.configHash = it.configHash;
                en.generator = std::string(kGeneratorVersion);
                en.diagnostics.clear();
                for (auto& d : problems) {
                    d.element = e.key;
                    d.path = e.path;
                    d.date = now;
                    if (d.file.empty()) d.file = fileOfElement(p, e);
                    log(Severity::Error, "G\xC3\xA9n\xC3\xA9ration", e.path + " : " + d.message + " [" + d.code + "]", e.key);
                    en.diagnostics.push_back(d);
                    rep.diagnostics.push_back(d);
                    ++rep.errors;
                    ++pr.errors;
                }
                pr.live[e.key] = depinv ? State::InvalidDependency : State::GenerationFailed;
                (area == Area::Api ? pr.api : pr.ihm)[sub].done++;
                (area == Area::Api ? pr.api : pr.ihm)[sub].failed++;
            }
            ++pr.done;
            publish();
        }
        pr.phases[static_cast<int>(ph)] = cancelled() ? PhaseState::Cancelled : PhaseState::Done;
        pr.phaseNotes[static_cast<int>(ph)] = std::to_string(list.size()) + " \xC3\xA9l\xC3\xA9ment(s)";
        pr.currentSub = -1;
        publish();
    }
    // ---- D. compilation ----
    if (!cancelled()) {
        if (pl.compile.empty()) {
            pr.phases[static_cast<int>(Phase::Compile)] = PhaseState::Skipped;
            pr.phaseNotes[static_cast<int>(Phase::Compile)] = req.mode == Mode::Generate || req.mode == Mode::Regenerate ? "non demand\xC3\xA9" "e" : "rien \xC3\xA0 compiler";
        } else {
            pr.phases[static_cast<int>(Phase::Compile)] = PhaseState::Running;
            pr.step = "D. Compilation des scripts et expressions";
            log(Severity::Information, "Compilation", "Compilation des scripts\xE2\x80\xA6");
            std::vector<std::size_t> todo;
            for (const auto k : pl.compile) {
                const auto& e = a.elements[k];
                if (failedNow.count(e.key)) { ++pr.done; continue; }   // pas genere : ni compile
                bool depFailed = false;
                for (const auto& d : e.deps) depFailed = depFailed || (!d.key.empty() && failedNow.count(d.key) && (d.via == "vue" || d.via == "symbole"));
                if (depFailed) { pr.live[e.key] = State::InvalidDependency; ++pr.done; continue; }
                todo.push_back(k);
                pr.live[e.key] = State::Compiling;
            }
            publish();
            const auto results = todo.empty() ? std::map<std::string, std::vector<Diagnostic>>{} : compileElements(p, a, todo, o);
            for (const auto k : todo) {
                if (cancelled()) break;
                const auto& e = a.elements[k];
                const auto& it = a.items[k];
                pr.element = e.path;
                auto& en = cache.entries[e.key];
                const auto found = results.find(e.key);
                std::vector<Diagnostic> diags = found == results.end() ? std::vector<Diagnostic>{} : found->second;
                int errs = 0, warns = 0;
                for (auto& d : diags) {
                    d.element = e.key;
                    d.path = e.path;
                    d.date = now;
                    if (d.file.empty()) d.file = fileOfElement(p, e);
                    if (d.blocking()) ++errs; else if (d.severity == Severity::Warning) ++warns;
                }
                en.diagnostics.erase(std::remove_if(en.diagnostics.begin(), en.diagnostics.end(), [](const Diagnostic& d) { return d.step == "Compilation"; }), en.diagnostics.end());
                for (const auto& d : diags) { en.diagnostics.push_back(d); rep.diagnostics.push_back(d); }
                if (errs == 0) {
                    en.compilation = State::Compiled;
                    en.lastCompiled = now;
                    en.lastValid = now;
                    en.failedHash.clear();
                    en.contentHash = it.contentHash;
                    ++rep.compiled;
                    log(warns ? Severity::Warning : Severity::Information, "Compilation", "Compilation : " + e.path + (warns ? " \xE2\x80\x94 " + std::to_string(warns) + " avertissement(s)" : std::string{}), e.key,
                        warns ? diags.front().line : 0, warns ? diags.front().column : 0);
                    pr.live[e.key] = State::Compiled;
                } else {
                    failedNow.insert(e.key);
                    en.compilation = State::CompilationFailed;
                    en.failedHash = it.contentHash;
                    for (const auto& d : diags)
                        if (d.blocking())
                            log(Severity::Error, "Compilation", e.path + (d.line ? ", ligne " + std::to_string(d.line) + (d.column ? ", colonne " + std::to_string(d.column) : std::string{}) : std::string{}) + " : " + d.message,
                                e.key, d.line, d.column);
                    pr.live[e.key] = State::CompilationFailed;
                }
                rep.errors += errs;
                rep.warnings += warns;
                pr.errors += errs;
                pr.warnings += warns;
                ++pr.done;
                publish();
            }
            pr.phases[static_cast<int>(Phase::Compile)] = cancelled() ? PhaseState::Cancelled : PhaseState::Done;
            pr.phaseNotes[static_cast<int>(Phase::Compile)] = std::to_string(pl.compile.size()) + " \xC3\xA9l\xC3\xA9ment(s)";
        }
        publish();
    }
    // ---- E. validation : les controles de Generer, sur tout le projet ----
    if (!cancelled()) {
        // 1.11.17 (refonte des scripts, lot 1) : une demande ciblee (Compiler le script actuel,
        // la fonction actuelle...) ne garde que ce qui touche sa portee - un element ailleurs en
        // erreur ne la fait plus echouer, et les remarques des autres elements restent au cache.
        // Demarrer, une demande sans portee ou de tout « IHM » (la racine de l'arbre) valident
        // tout le projet, comme avant (avec les remarques du projet lui-meme).
        const bool whole = std::any_of(req.scope.begin(), req.scope.end(), [](const std::string& s) { return s == "IHM" || s == "API"; });
        const bool scoped = !req.scope.empty() && !whole && req.mode != Mode::Start;
        const auto inRequest = [&](const std::string& key) {
            const Element* e = key.empty() ? nullptr : a.element(key);
            return e != nullptr && inScope(*e, req.scope);
        };
        pr.phases[static_cast<int>(Phase::Validate)] = PhaseState::Running;
        pr.step = scoped ? "E. Validation de la s\xC3\xA9lection" : "E. Validation du projet";
        pr.element = "r\xC3\xA9" "f\xC3\xA9rences, types, d\xC3\xA9pendances, coh\xC3\xA9rence API / IHM";
        publish();
        log(Severity::Information, "Validation", scoped ? "Validation de la s\xC3\xA9lection\xE2\x80\xA6" : "Validation du projet\xE2\x80\xA6");
        GenerateOptions go;
        go.projectFolder = o.projectFolder;
        go.plan = o.commPlan;
        go.plcPaths = o.plcPaths;
        int verrors = 0, vwarnings = 0;
        std::map<std::string, std::vector<Diagnostic>> perElement;
        if (!scoped) cache.project.clear();
        int quality = 0;
        // Ce que Compiler a deja dit autrement : « variable inexistante dans le programme : X »
        // (Generer) quand une expression ou une action de la meme vue dit deja « X n'existe pas ».
        const auto saidByCompile = [&cache](const Issue& i) {
            static constexpr std::string_view kPrefix = "variable inexistante dans le programme : ";
            if (i.category != "Variable" || i.view == kNoId || i.message.rfind(kPrefix, 0) != 0) return false;
            std::string name = i.message.substr(kPrefix.size());
            if (const auto cut = name.find_first_of(" :"); cut != std::string::npos) name.resize(cut);
            if (name.empty()) return false;
            for (const char* k : {"animations:", "actions:"}) {
                const auto it = cache.entries.find(std::string(k) + std::to_string(i.view));
                if (it == cache.entries.end()) continue;
                for (const auto& c : it->second.diagnostics)
                    if (c.step == "Compilation" && c.blocking() && c.message.find(name) != std::string::npos) return true;
            }
            return false;
        };
        for (const auto& i : generateWith(p, o.plcHasName, go)) {
            if (i.severity == Issue::Severity::Info) continue;
            if (saidByCompile(i)) continue;
            if (scoped && !inRequest(elementOfIssue(p, i))) continue;     // 1.11.17 : hors de la demande
            auto d = fromIssue(i, "Validation");
            d.date = now;
            if (i.severity == Issue::Severity::Error && !blockingValidation(i)) {
                d.severity = Severity::Warning;
                d.suggestion = "Non bloquant pour la simulation (une erreur pour G\xC3\xA9n\xC3\xA9rer) : " + i.category + ".";
                ++quality;
            }
            const std::string key = elementOfIssue(p, i);
            if (!key.empty() && cache.entries.count(key)) {
                auto& en = cache.entries[key];
                // deja dit par la compilation (les scripts : Generer refait leurs controles)
                const bool said = std::any_of(en.diagnostics.begin(), en.diagnostics.end(), [&d](const Diagnostic& c) {
                    return c.step == "Compilation" && c.message == d.message && c.line == d.line;
                });
                if (said) continue;
                d.element = key;
                d.path = en.path;
                if (const Element* e = a.element(key)) d.file = fileOfElement(p, *e);
                perElement[key].push_back(d);
            } else
                cache.project.push_back(d);
            if (d.blocking()) ++verrors; else ++vwarnings;
            rep.diagnostics.push_back(d);
        }
        // les diagnostics de validation : a leur element (un element en erreur n'est pas valide)
        for (auto& [key, en] : cache.entries) {
            if (scoped && !inRequest(key)) continue;                     // 1.11.17 : les remarques d'ailleurs restent
            en.diagnostics.erase(std::remove_if(en.diagnostics.begin(), en.diagnostics.end(), [](const Diagnostic& d) { return d.step == "Validation"; }), en.diagnostics.end());
            const auto f = perElement.find(key);
            if (f == perElement.end()) continue;
            bool blocking = false;
            for (const auto& d : f->second) { en.diagnostics.push_back(d); blocking = blocking || d.blocking(); }
            if (blocking && en.generation == State::Generated) { en.generation = State::GenerationFailed; en.failedHash = en.contentHash; failedNow.insert(key); }
        }
        // ce qui doit etre pret pour demarrer
        if (req.mode == Mode::Start)
            for (const auto& e : a.elements) {
                const auto f = cache.entries.find(e.key);
                if (f == cache.entries.end() || failedNow.count(e.key)) continue;
                if (f->second.generation != State::Generated || (compilable(e.kind) && f->second.compilation != State::Compiled)) {
                    Diagnostic d;
                    d.severity = Severity::Error;
                    d.code = f->second.generation != State::Generated ? "E402" : "E401";
                    d.category = "Validation";
                    d.message = e.path + (f->second.generation != State::Generated ? " n'est pas g\xC3\xA9n\xC3\xA9r\xC3\xA9" : " n'est pas compil\xC3\xA9");
                    d.step = "Validation";
                    d.element = e.key;
                    d.path = e.path;
                    d.date = now;
                    rep.diagnostics.push_back(d);
                    ++verrors;
                }
            }
        for (const auto& d : rep.diagnostics)
            if (d.step == "Validation") log(d.blocking() ? Severity::Error : Severity::Warning, "Validation", (d.path.empty() ? std::string{} : d.path + " : ") + d.message, d.element);
        if (quality)
            log(Severity::Information, "Validation", std::to_string(quality) + " erreur(s) de G\xC3\xA9n\xC3\xA9rer non bloquante(s) pour la simulation "
                                                     "(bornes, param\xC3\xA8tres d'action, communication, qualit\xC3\xA9\xE2\x80\xA6) : dites en avertissement.");
        rep.errors += verrors;
        rep.warnings += vwarnings;
        pr.errors += verrors;
        pr.warnings += vwarnings;
        pr.phases[static_cast<int>(Phase::Validate)] = verrors ? PhaseState::Failed : PhaseState::Done;
        pr.phaseNotes[static_cast<int>(Phase::Validate)] = verrors + vwarnings ? std::to_string(verrors + vwarnings) + " remarque(s)" : "rien \xC3\xA0 signaler";
        publish();
    }
    // ---- le cache, d'un bloc ----
    cache.generator = std::string(kGeneratorVersion);
    cache.written = now;
    if (disk) {
        if (auto s = saveCache(cache, o.buildFolder); !s) {
            log(Severity::Error, "Cache", "Le cache n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + s.error().message() + " (le dernier cache valide reste en place)");
            ++rep.errors;
        } else
            log(Severity::Information, "Cache", "Cache \xC3\xA9" "crit : .xpg/build/build-cache.txt (" + std::to_string(cache.entries.size())
                + " entr\xC3\xA9" "es) \xE2\x80\x94 fichier temporaire v\xC3\xA9rifi\xC3\xA9, puis remplac\xC3\xA9 ; l'ancien gard\xC3\xA9 en .bak.");
    }
    rep.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    rep.cancelled = cancelled();
    rep.ok = !rep.cancelled && rep.errors == 0;
    char ms[32];
    std::snprintf(ms, sizeof ms, "%.0f", rep.ms);
    if (rep.cancelled)
        log(Severity::Warning, "Build", "Build annul\xC3\xA9 : " + std::to_string(pr.done) + " t\xC3\xA2" "che(s) faite(s) sur " + std::to_string(pr.total)
            + " ; les artefacts valides pr\xC3\xA9" "c\xC3\xA9" "dents sont conserv\xC3\xA9s.");
    else
        log(rep.errors ? Severity::Error : Severity::Success, "Build", "Build termin\xC3\xA9 : " + std::to_string(rep.errors) + (rep.errors > 1 ? " erreurs, " : " erreur, ")
            + std::to_string(rep.warnings) + (rep.warnings > 1 ? " avertissements" : " avertissement") + " (" + ms + " ms).");
    settleStart();
    rep.cache = std::move(cache);
    return rep;
}

// ============================================================== le verrou ====
// ---- 1.11.15 : l'arret sur modification (voir le .hpp) ----
RunPrints runPrints(const Analysis& a) {
    RunPrints out;
    for (const auto& item : a.items)
        if (item.element < a.elements.size()) out[a.elements[item.element].key] = item.contentHash + "|" + item.configHash + "|" + item.ifaceHash;
    return out;
}

RunChange runChange(const RunPrints& atStart, const std::unordered_map<std::string, std::string>& pathsAtStart, const Analysis& now,
                    bool developer) {
    RunChange c;
    const auto prints = runPrints(now);
    for (const auto& [key, print] : prints) {
        const auto it = atStart.find(key);
        if (it == atStart.end() || it->second != print) c.changed.push_back(key);
    }
    for (const auto& [key, print] : atStart)
        if (!prints.count(key)) c.removed.push_back(key);
    std::sort(c.changed.begin(), c.changed.end());
    std::sort(c.removed.begin(), c.removed.end());
    if ((c.changed.empty() && c.removed.empty()) || !developer) return c;   // rien, ou la simulation seule : elle continue
    c.stop = true;
    // Ce qui a change, et ce que cela demande : une compilation (du code) ou une generation.
    std::vector<std::string> lines;
    for (const auto& key : c.changed) {
        const auto* e = now.element(key);
        if (!e) continue;
        const bool code = compilable(e->kind);
        ++(code ? c.compile : c.generate);
        if (lines.size() < 3)
            lines.push_back("\xC2\xB7 " + e->name + " (" + std::string(kindLabel(e->kind)) + (code ? ", \xC3\xA0 compiler)" : ", \xC3\xA0 g\xC3\xA9n\xC3\xA9rer)"));
        c.paths.push_back(e->path);
    }
    for (const auto& key : c.removed) {
        ++c.generate;
        const auto it = pathsAtStart.find(key);
        const std::string path = it != pathsAtStart.end() ? it->second : key;
        if (lines.size() < 3) lines.push_back("\xC2\xB7 " + path + " (supprim\xC3\xA9)");
        c.paths.push_back(path + " (supprim\xC3\xA9)");
    }
    const std::size_t total = c.changed.size() + c.removed.size();
    c.head = std::to_string(total) + (total > 1 ? " \xC3\xA9l\xC3\xA9ments modifi\xC3\xA9s" : " \xC3\xA9l\xC3\xA9ment modifi\xC3\xA9");
    std::vector<std::string> needs;
    if (c.compile) needs.push_back(std::to_string(c.compile) + " \xC3\xA0 compiler");
    if (c.generate) needs.push_back(std::to_string(c.generate) + " \xC3\xA0 g\xC3\xA9n\xC3\xA9rer");
    for (std::size_t i = 0; i < needs.size(); ++i) c.head += (i ? ", " : " : ") + needs[i];
    c.card = c.head;
    for (const auto& l : lines) c.card += "\n" + l;
    if (total > lines.size()) c.card += "\n\xC2\xB7 \xE2\x80\xA6 et " + std::to_string(total - lines.size()) + " autre(s)";
    return c;
}

Lock acquireLock(const std::string& buildFolder) {
    Lock l;
    std::error_code ec;
    fs::create_directories(buildFolder, ec);
    const fs::path file = fs::path(buildFolder) / "build.lock";
    std::string text;
    const long me = static_cast<long>(XPG_GETPID());
    if (core::readFileAll(file, text) && !text.empty()) {
        Record r;
        std::string why;
        if (parseRecord(trimmed(text), r, why)) {
            const auto* pid = r.get("pid");
            const auto* since = r.get("depuis");
            const long other = pid ? std::atol(pid->c_str()) : 0;
            const auto age = fs::last_write_time(file, ec);
            const bool stale = ec || (fs::file_time_type::clock::now() - age) > std::chrono::minutes(10);
            if (other != 0 && other != me && !stale) {
                l.held = false;
                l.owner = "PID " + std::to_string(other) + (since ? ", depuis " + *since : std::string{});
                return l;
            }
        }
    }
    if (auto s = core::writeFileAtomic(file, "verrou pid=" + std::to_string(me) + " depuis=" + quote(nowText()) + "\n", core::AtomicWrite{false, false}); !s) {
        l.held = false;
        l.owner = "dossier en lecture seule (" + s.error().message() + ")";
        return l;
    }
    l.held = true;
    return l;
}

void releaseLock(const std::string& buildFolder) {
    std::error_code ec;
    fs::remove(fs::path(buildFolder) / "build.lock", ec);
}

} // namespace hmi::pipeline
