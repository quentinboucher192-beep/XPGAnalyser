// =============================================================================
//  app/hmi/HmiValueKind.cpp - voir HmiValueKind.hpp
// =============================================================================
#include "HmiValueKind.hpp"
#include "../../core/Edition.hpp"   // 1.12.0 : XPGAnalyser IHM - pas de variable API

#include "HmiAssist.hpp"
#include "HmiPanels.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiDuplicate.hpp"
#include "../../hmi/HmiEnums.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiExprCheck.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiPopupParams.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../ui/widgets/ExprField.hpp"
#include "../../hmi/HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6)

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace app::valuekind {

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

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(uc(a[i])) != std::toupper(uc(b[i]))) return false;
    return true;
}

// La tete d'un chemin : Armoires de Armoires[1].Pression.
std::string rootOf(std::string_view path) {
    const std::string t = trimmed(path);
    std::size_t i = 0;
    while (i < t.size() && (std::isalnum(uc(t[i])) || t[i] == '_')) ++i;
    return t.substr(0, i);
}

// Les globales de l'automate (nom tel qu'ecrit, cle en majuscules), en cache : refait
// quand le programme change (un autre objet, un autre nombre de variables).
struct PlcRoots {
    const domain::Project*             of{nullptr};
    std::size_t                        count{0};
    std::map<std::string, std::string> names;   // MAJUSCULES -> nom ecrit
    std::map<std::string, std::string> types;   // MAJUSCULES -> type
    std::map<std::string, std::string> details; // MAJUSCULES -> "%MW100 - commentaire"
};
std::shared_ptr<const PlcRoots> plcRoots(const domain::Project* plc) {
    static std::mutex guard;
    static std::shared_ptr<const PlcRoots> last;
    const std::lock_guard<std::mutex> lock(guard);
    if (!plc) return nullptr;
    if (last && last->of == plc && last->count == plc->variables.size()) return last;
    auto made = std::make_shared<PlcRoots>();
    made->of = plc;
    made->count = plc->variables.size();
    for (const auto& v : plc->variables) {
        if (v.scope != domain::VariableScope::Global) continue;
        const std::string name(plc->strings.text(v.name));
        const std::string key = upper(name);
        made->names[key] = name;
        made->types[key] = std::string(plc->strings.text(v.type.name));
        std::string d = v.address.raw;
        const std::string comment(plc->strings.text(v.comment));
        if (!comment.empty()) d += (d.empty() ? "" : " \xC2\xB7 ") + comment;
        made->details[key] = d;
    }
    last = made;
    return last;
}

bool isPlcRoot(const Env& env, std::string_view root) {
    const auto roots = plcRoots(env.plc);
    return roots && roots->names.count(upper(root)) > 0;
}

bool isObjectOfView(const Env& env, std::string_view root) {
    if (!env.view) return false;
    return std::any_of(env.view->objects.begin(), env.view->objects.end(), [&](const hmi::Object& o) { return sameName(o.name, root); });
}

// Le nom est-il connu (racine d'un chemin, d'un appel) - les memes regles que
// hmiExpressionError, plus THIS et les objets de la vue (leurs variables publiques).
bool knownRoot(const Env& env, std::string_view root) {
    if (root.empty()) return false;
    if (sameName(root, "THIS") || (core::hasApi() && sameName(root, "API")) || hmi::pub::isSysRoot(root)) return true;   // 1.12.0
    if (hmi::isHmiFunction(root) || hmi::isStandardFunction(root)) return true;
    if (env.view && env.view->param(root)) return true;
    if (env.project) {
        if (env.project->variable(root) || env.project->functionByName(root) || env.project->hmiTypeByName(root)) return true;
        if (hmi::pub::viewNamed(*env.project, root)) return true;
    }
    if (isObjectOfView(env, root)) return true;
    if (!env.plc) return true;   // sans automate : un autre nom est peut-etre a lui (comme hmiExpressionError)
    return isPlcRoot(env, root);
}

// 1.11.19 (refonte, lot 6) : les nombres et les entiers du registre des types.
bool isNumericType(std::string_view t) { return hmi::typereg::isNumber(t); }
bool isIntegerType(std::string_view t) { return hmi::typereg::isInteger(t); }

// Le texte sans ses $ de repere (hors chaines) : ce qu'on analyse.
std::string withoutMarkers(std::string_view t) {
    std::string out;
    char quote = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (quote) {
            out += c;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; out += c; continue; }
        if (c == '$') {
            if (i + 1 < t.size() && t[i + 1] == '$') { out += '$'; ++i; }
            continue;
        }
        out += c;
    }
    return out;
}

hmi::exprcheck::Want wantOf(std::string_view expected) {
    using W = hmi::exprcheck::Want;
    const std::string e = upper(trimmed(expected));
    if (e == "BOOL") return W::Bool;
    if (e == "NOMBRE" || isNumericType(e)) return W::Number;
    if (e == "COULEUR") return W::Color;
    return W::Any;
}

// Remplacer `from` (en tete de chemin, hors chaines) par `to`.
std::string replaceRoot(std::string_view expr, std::string_view from, std::string_view to) {
    const auto ident = [](char ch) { return std::isalnum(uc(ch)) != 0 || ch == '_'; };
    std::string out;
    char quote = 0;
    for (std::size_t i = 0; i < expr.size();) {
        const char ch = expr[i];
        if (quote) { out += ch; if (ch == quote) quote = 0; ++i; continue; }
        if (ch == '\'' || ch == '"') { quote = ch; out += ch; ++i; continue; }
        if ((i == 0 || (!ident(expr[i - 1]) && expr[i - 1] != '.')) && i + from.size() <= expr.size()
            && sameName(expr.substr(i, from.size()), from) && (i + from.size() >= expr.size() || !ident(expr[i + from.size()]))) {
            out += to;
            i += from.size();
            continue;
        }
        out += ch;
        ++i;
    }
    return out;
}

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const bool same = std::tolower(uc(a[i - 1])) == std::tolower(uc(b[j - 1]));
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (same ? 0u : 1u)});
        }
        prev.swap(cur);
    }
    return prev[b.size()];
}

std::string zoneLabel(Style z) {
    switch (z) {
        case Style::Api: return "API";
        case Style::Hmi: return "IHM";
        case Style::System: return "syst\xC3\xA8me";
        case Style::Local: return "symbole / vue";
        default: break;
    }
    return {};
}
std::string zoneLong(Style z) {
    switch (z) {
        case Style::Api: return "Variable API";
        case Style::Hmi: return "Variable IHM";
        case Style::System: return "Variable syst\xC3\xA8me";
        case Style::Local: return "Param\xC3\xA8tre ou variable publique";
        default: break;
    }
    return "Variable";
}

// Une conversion qui rend `expr` (de type `actual`) acceptable pour `expected`.
bool conversionFix(std::string_view expr, std::string_view actual, std::string_view expected, Fix& out) {
    const std::string a = upper(trimmed(actual)), e = upper(trimmed(expected));
    if ((e == "STRING" || e == "TEXTE" || hmi::isTextType(e)) && (isNumericType(a) || a == "BOOL" || a == "TIME")) {
        out = {"Convertir : " + a + "_TO_STRING(" + std::string(expr) + ")", a + "_TO_STRING(" + std::string(expr) + ")", true, {}};
        return true;
    }
    if (e == "BOOL" && isNumericType(a)) {
        out = {"Comparer : " + std::string(expr) + " <> 0", std::string(expr) + " <> 0", true, {}};
        return true;
    }
    if (isIntegerType(e) && (a == "REAL" || a == "LREAL")) {
        out = {"Arrondir : " + a + "_TO_" + e + "(" + std::string(expr) + ")", a + "_TO_" + e + "(" + std::string(expr) + ")", true, {}};
        return true;
    }
    if ((e == "REAL" || e == "LREAL" || e == "NOMBRE") && a == "BOOL") {
        out = {"Convertir : BOOL_TO_INT(" + std::string(expr) + ")", "BOOL_TO_INT(" + std::string(expr) + ")", true, {}};
        return true;
    }
    return false;
}

// Les variables (IHM, puis API) de ce type exactement : ce qu'un tableau ou une structure attend.
std::vector<std::pair<std::string, Style>> variablesOfType(const Env& env, std::string_view type, std::size_t limit = 4) {
    std::vector<std::pair<std::string, Style>> out;
    const std::string want = hmi::params::normalizedType(type);
    if (env.project)
        for (const auto& v : env.project->programs.variables)
            if (out.size() < limit && hmi::params::normalizedType(v.type) == want) out.emplace_back(v.name, Style::Hmi);
    if (const auto roots = plcRoots(env.plc))
        for (const auto& [key, t] : roots->types)
            if (out.size() < limit && hmi::params::normalizedType(t) == want) out.emplace_back(roots->names.at(key), Style::Api);
    return out;
}

// Un nom inconnu : le dire, et proposer le plus proche, le texte, la creation.
Diag unknownDiag(const Env& env, std::string_view text, bool fx, std::string_view name, std::string_view expected, bool single,
                 std::string_view suggestion, std::vector<std::string>& unknown) {
    Diag d;
    const bool dotted = std::string_view(name).find('.') != std::string_view::npos;
    d.message = std::string(name) + " n'existe pas" + (dotted ? " (le membre ou le chemin)" : "") + ".";
    std::vector<std::string> near;
    if (!suggestion.empty()) near.emplace_back(suggestion);
    for (auto& n : closestNames(env, name, 2))
        if (std::none_of(near.begin(), near.end(), [&](const std::string& x) { return sameName(x, n); })) near.push_back(std::move(n));
    if (near.size() > 2) near.resize(2);
    if (!near.empty() && !suggestion.empty()) d.message = std::string(name) + " n'existe pas : veux-tu dire " + near.front() + " ?";
    for (const auto& n : near) {
        Style z{};
        std::string t;
        std::string label = "Remplacer par " + n;
        if (pathInfo(env, n, z, t)) label += " (" + zoneLabel(z) + (t.empty() ? "" : ", " + t) + ")";
        d.fixes.push_back({label, replaceRoot(text, name, n), fx, {}});
    }
    const std::string e = upper(trimmed(expected));
    const bool word = !dotted && std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(uc(c)) || c == '_'; });
    if (single && word && (e == "STRING" || e == "TEXTE" || hmi::isTextType(e) || e.empty() || e == "ANY"))
        d.fixes.push_back({"Prendre comme le texte '" + std::string(name) + "' (constante)", std::string(name), false, {}});
    if (word && !name.empty() && !sameName(name, "SYS") && !sameName(name, "API")) {
        d.fixes.push_back({"Cr\xC3\xA9" "er \xC2\xAB " + std::string(name) + " \xC2\xBB\xE2\x80\xA6", {}, fx, std::string(name)});
        unknown.emplace_back(name);
    }
    return d;
}

} // namespace

// ---------------------------------------------------------------- les carres ---
const std::vector<KindInfo>& kinds() {
    // 1.12.0 : XPGAnalyser IHM n'a pas de variable API - pas de carre A dans la legende.
    if (!core::hasApi()) {
        static const std::vector<KindInfo> h = [] {
            std::vector<KindInfo> all = kindsWithApi();
            std::erase_if(all, [](const KindInfo& k) { return k.style == Style::Api; });
            return all;
        }();
        return h;
    }
    return kindsWithApi();
}

const std::vector<KindInfo>& kindsWithApi() {
    static const std::vector<KindInfo> k{
        {Style::Constant, "C", "Constante", "Une valeur fixe, convertie dans le type du champ"},
        {Style::Formula, "fx", "Formule", "Un calcul, un appel ou plusieurs sources"},
        {Style::Markers, "$", "Formule + rep\xC3\xA8res", "Des rep\xC3\xA8res $\xE2\x80\xA6$ : ce qui varie quand on duplique"},
        {Style::Api, "A", "Variable API", "Une variable de l'automate"},
        {Style::Hmi, "I", "Variable IHM", "Une variable de l'IHM"},
        {Style::System, "S", "Variable syst\xC3\xA8me", "SYS. : l'heure, la date, l'utilisateur\xE2\x80\xA6"},
        {Style::Local, "V", "Variable du symbole ou de la vue", "Param\xC3\xA8tre, variable publique, THIS"},
        {Style::Error, "!", "Erreur", "Nom inconnu, conversion impossible, hors bornes"},
    };
    return k;
}

const KindInfo& info(Style s) {
    for (const auto& k : kinds())
        if (k.style == s) return k;
    static const KindInfo none{Style::Empty, "", "Aucune valeur", "Vide : la valeur par d\xC3\xA9" "faut du champ"};
    return none;
}

ui::PropertyGrid::Legend legendOf(Style s) {
    ui::PropertyGrid::Legend l;
    l.style = s;
    l.text = std::string(info(s).letter);
    l.tip = std::string(info(s).name) + " : " + std::string(info(s).meaning);
    return l;
}

// ---------------------------------------------------------------- les chemins ---
bool pathInfo(const Env& env, std::string_view path, Style& zone, std::string& type, std::string* detail) {
    const std::string p = trimmed(path);
    const std::string root = rootOf(p);
    if (root.empty()) return false;
    type.clear();
    const auto describe = [&]() -> bool {
        if (!env.project) return false;
        const auto d = assist::describe(*env.project, env.plc, p);
        if (!d.found) return false;
        type = d.type;
        if (detail) *detail = d.line;
        return true;
    };
    if (hmi::pub::isSysRoot(root)) {
        zone = Style::System;
        if (const auto* sv = p.size() > 4 ? hmi::pub::sysVar(std::string_view(p).substr(4)) : nullptr) {
            type = std::string(sv->type);
            if (detail) *detail = std::string(sv->text);
            return true;
        }
        return describe();
    }
    if (sameName(root, "THIS")) {
        zone = Style::Local;
        (void)describe();
        return true;
    }
    if (env.view)
        if (const auto* prm = env.view->param(root)) {
            zone = Style::Local;
            if (p.size() == root.size()) {
                type = prm->type;
                if (detail) *detail = "Param\xC3\xA8tre de " + env.view->name + (prm->defaultValue.empty() ? "" : " (d\xC3\xA9" "faut : " + prm->defaultValue + ")");
            } else {
                (void)describe();
            }
            return true;
        }
    if (env.project) {
        if (const auto* v = env.project->variable(root)) {
            zone = Style::Hmi;
            type = hmi::types::typeOfPath(*env.project, p);
            if (type.empty() && p.size() == root.size()) type = v->type;
            if (detail) {
                *detail = "Initiale " + (v->initial.empty() ? std::string("0") : v->initial);
                if (!v->description.empty()) *detail += " \xC2\xB7 " + v->description;
            }
            if (type.empty()) (void)describe();
            return true;
        }
        if (hmi::pub::viewNamed(*env.project, root)) {
            zone = Style::Local;
            return describe() || true;
        }
    }
    if (isObjectOfView(env, root)) {
        zone = Style::Local;
        return describe() || true;
    }
    if ((core::hasApi() && sameName(root, "API")) || isPlcRoot(env, root)) {   // 1.12.0 : pas dans XPGAnalyser IHM
        zone = Style::Api;
        if (const auto roots = plcRoots(env.plc); roots && p.size() == root.size()) {
            const std::string key = upper(root);
            type = roots->types.count(key) ? roots->types.at(key) : std::string{};
            if (detail && roots->details.count(key)) *detail = roots->details.at(key);
            return true;
        }
        if (!describe()) {
            // Un membre que la description ne connait pas : la variable est la, le chemin peut-etre pas.
            if (!env.project) return true;
            return false;
        }
        return true;
    }
    return false;
}

bool compatible(std::string_view expected, std::string_view actual) {
    const std::string e = upper(trimmed(expected));
    const std::string a = upper(trimmed(actual));
    if (e.empty() || e == "ANY" || a.empty() || e == "TEXTE") return true;
    if (e == "COULEUR") return hmi::isTextType(a);
    if (e == "NOMBRE") return isNumericType(a);
    if (hmi::isTextType(e)) return hmi::isTextType(a);
    return hmi::params::typeAccepts(expected, actual);
}

std::vector<std::string> closestNames(const Env& env, std::string_view name, std::size_t limit) {
    std::vector<std::pair<std::size_t, std::string>> scored;
    const std::size_t budget = std::max<std::size_t>(2, name.size() / 4);
    const auto consider = [&](const std::string& candidate) {
        if (candidate.empty() || sameName(candidate, name)) return;
        const std::size_t d = distance(name, candidate);
        if (d <= budget) scored.emplace_back(d, candidate);
    };
    if (env.project) {
        for (const auto& v : env.project->programs.variables) consider(v.name);
    }
    if (env.view)
        for (const auto& prm : env.view->params) consider(prm.name);
    if (const auto roots = plcRoots(env.plc))
        for (const auto& [key, n] : roots->names) consider(n);
    if (std::string_view(name).substr(0, 4) == "SYS." || std::string_view(name).substr(0, 4) == "sys.")
        for (const auto& sv : hmi::pub::kSysVars) consider("SYS." + std::string(sv.name));
    std::stable_sort(scored.begin(), scored.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<std::string> out;
    for (auto& [d, n] : scored) {
        if (out.size() >= limit) break;
        out.push_back(std::move(n));
    }
    return out;
}

std::string expectedOfProperty(const ui::PropertyGrid::Property& p) {
    using E = ui::exprfield::Expect;
    if (p.type == ui::PropertyGrid::ValueType::Boolean) return "BOOL";
    switch (ui::exprfield::expectOf(p)) {
        case E::Bool: return "BOOL";
        case E::Number: return "NOMBRE";
        case E::Text: return "STRING";
        case E::Template: return "TEXTE";
        case E::Color: return "COULEUR";
        case E::Time: return "TIME";
        case E::View: return "STRING";
        case E::List: case E::Value: break;
    }
    if (p.type == ui::PropertyGrid::ValueType::Integer || p.type == ui::PropertyGrid::ValueType::Real) return "NOMBRE";
    if (p.type == ui::PropertyGrid::ValueType::Color) return "COULEUR";
    return {};
}

std::string expectedLabel(std::string_view expected) {
    const std::string e = upper(trimmed(expected));
    if (e.empty() || e == "ANY") return "tout (ANY)";
    if (e == "TEXTE") return "texte";
    if (e == "NOMBRE") return "nombre";
    if (e == "COULEUR") return "couleur";
    return trimmed(expected);
}

std::string firstProblem(const Result& r) { return r.diags.empty() ? std::string{} : r.diags.front().message; }

// ---------------------------------------------------------------- le classement ---
Result classify(const Env& env, std::string_view text, bool fx, std::string_view expected) {
    Result r;
    const std::string t = trimmed(text);
    if (t.empty()) return r;
    r.empty = false;
    const bool markers = !hmi::dup::markersIn(t, fx).empty();
    const std::string clean = markers ? withoutMarkers(t) : t;
    const std::string e = upper(trimmed(expected));
    const auto addSource = [&r](Style z) {
        if (std::find(r.sources.begin(), r.sources.end(), z) == r.sources.end()) r.sources.push_back(z);
    };

    // ---- un texte a trous ({Variable}) : constant autour, des formules dans les trous ----
    if (!fx && e == "TEXTE") {
        const auto holes = hmi::exprcheck::templateExpressions(clean);
        if (holes.empty()) {
            r.style = markers ? Style::Markers : Style::Constant;
            r.literal = t;
            r.info = markers ? "Texte avec rep\xC3\xA8res" : "Texte fixe";
            return r;
        }
        for (const auto& h : holes) {
            const Result in = classify(env, h, true, {});
            for (const auto& d : in.diags) {
                Diag copy = d;
                for (auto& f : copy.fixes)
                    if (f.create.empty()) {
                        // La correction du trou, remise dans le texte entier.
                        const auto at = t.find(h);
                        f.value = at == std::string::npos ? t : std::string(t).replace(at, h.size(), f.fx ? f.value : "'" + f.value + "'");
                        f.fx = false;
                    }
                r.diags.push_back(std::move(copy));
            }
            for (const auto& u : in.unknown) r.unknown.push_back(u);
            for (const auto z : in.sources) addSource(z);
            if (in.style == Style::Api || in.style == Style::Hmi || in.style == Style::System || in.style == Style::Local) addSource(in.style);
        }
        r.style = !r.diags.empty() ? Style::Error : markers ? Style::Markers : Style::Formula;
        r.info = "Texte \xC3\xA0 trous : " + std::to_string(holes.size()) + " valeur" + (holes.size() > 1 ? "s" : "") + " lue"
               + (holes.size() > 1 ? "s" : "");
        return r;
    }

    // ---- sans fx : une constante, convertie dans le type ----
    if (!fx) {
        if (e.empty() || e == "ANY" || e == "TEXTE" || e == "COULEUR" || e == "STRING" || hmi::isTextType(e)) {
            r.style = markers ? Style::Markers : Style::Constant;
            r.literal = (e.empty() || e == "ANY") ? hmi::argumentLiteral(env.project, "ANY", t) : t;
            if (hmi::isTextType(e) && e != "TEXTE") r.literal = hmi::argumentLiteral(env.project, e, t);
            r.info = "Constante" + (r.literal.empty() ? std::string{} : " : " + r.literal);
            if (hmi::isTextType(e) && !hmi::isLiteralArgument(t)) r.info += " (converti en texte)";
            return r;
        }
        if (e == "NOMBRE") {
            double n = 0;
            if (hmi::parseNumber(clean, n)) {
                r.style = Style::Constant;
                r.literal = clean;
                r.info = "Constante : " + clean;
                return r;
            }
        } else {
            std::string why;
            const std::string lit = hmi::argumentLiteral(env.project, e, clean, &why);
            if (!lit.empty()) {
                r.style = markers ? Style::Markers : Style::Constant;
                r.literal = lit;
                r.info = "Constante : " + lit + " (" + trimmed(expected) + ")";
                return r;
            }
            // Pas une constante de ce type. Un nom de variable connu : la correction est de la
            // prendre en formule (l'inspecteur d'un parametre le fait seul a la saisie).
            r.style = Style::Error;
            Diag d{why.empty() ? "\xC2\xAB " + t + " \xC2\xBB ne se convertit pas en " + expectedLabel(expected) + "." : why, {}};
            // Les corrections : la valeur dans les bornes, les valeurs d'une enumeration, TRUE/FALSE.
            if (e == "BOOL") {
                d.fixes.push_back({"TRUE", "TRUE", false, {}});
                d.fixes.push_back({"FALSE", "FALSE", false, {}});
            } else if (const auto* en = env.project ? hmi::findEnumeration(*env.project, trimmed(expected)) : nullptr) {
                for (const auto& v : en->values) {
                    if (d.fixes.size() >= 4) break;
                    d.fixes.push_back({hmi::enumLiteral(*en, v), v.name, false, {}});
                }
            } else if (isIntegerType(e)) {
                const auto bound = why.rfind(".."), open = why.rfind('(');
                double n = 0;
                if (hmi::parseNumber(clean, n) && bound != std::string::npos && open != std::string::npos && open < bound) {
                    const std::string lo = why.substr(open + 1, bound - open - 1);
                    const std::string hi = why.substr(bound + 2, why.find(')', bound) - bound - 2);
                    const std::string b = n < 0 && !lo.empty() ? lo : hi;
                    d.fixes.push_back({"Ramener \xC3\xA0 " + b, b, false, {}});
                }
            } else if (hmi::isAggregateType(env.project, expected)) {
                for (const auto& [name, z2] : variablesOfType(env, expected))
                    d.fixes.push_back({"Utiliser " + name + " (" + zoneLabel(z2) + ") en fx", name, true, {}});
            }
            Style vz{};
            std::string vtype;
            if (hmi::isVariablePath(clean) && pathInfo(env, clean, vz, vtype)) {
                d.fixes.insert(d.fixes.begin(), Fix{"Utiliser la variable " + clean + " (" + zoneLabel(vz) + (vtype.empty() ? "" : ", " + vtype)
                                                        + ") en fx",
                                                    clean, true, {}});
            } else if (hmi::isVariablePath(clean) && !knownRoot(env, rootOf(clean))) {
                std::vector<std::string> unknown;
                Diag u = unknownDiag(env, t, true, rootOf(clean), expected, true, {}, unknown);
                for (auto& f : u.fixes) d.fixes.push_back(std::move(f));
                for (auto& n : unknown) r.unknown.push_back(std::move(n));
            } else {
                d.fixes.push_back({"En faire une formule (fx)", t, true, {}});
            }
            r.diags.push_back(std::move(d));
            r.info = "Constante impossible \xC3\xA0 convertir.";
            return r;
        }
        // NOMBRE, pas un nombre : une variable se prend en formule.
        r.style = Style::Error;
        r.diags.push_back({"\xC2\xAB " + t + " \xC2\xBB n'est pas un nombre.", {{"En faire une formule (fx)", t, true, {}}}});
        return r;
    }

    // ---- avec fx : une formule ----
    const hmi::Expression ex = hmi::Expression::compile(clean);
    if (!ex.valid()) {
        r.style = Style::Error;
        Diag d{ex.error().empty() ? std::string("La formule ne se lit pas.") : ex.error(), {}};
        if (e == "STRING" || e == "TEXTE" || hmi::isTextType(e))
            d.fixes.push_back({"Prendre comme texte (constante)", t, false, {}});
        r.diags.push_back(std::move(d));
        r.info = "Formule illisible.";
        return r;
    }
    const bool single = hmi::isVariablePath(clean);
    hmi::exprcheck::Context ctx;
    ctx.project = env.project;
    ctx.view = env.view;
    ctx.known = [&env](std::string_view root) { return knownRoot(env, root); };
    if (env.plc) {
        static std::mutex guard;
        static const domain::Project* pathsOf = nullptr;
        static std::size_t pathsCount = 0;
        static std::shared_ptr<const hmi::exprcheck::PlcPaths> paths;
        const std::lock_guard<std::mutex> lock(guard);
        if (!paths || pathsOf != env.plc || pathsCount != env.plc->variables.size()) {
            paths = std::make_shared<const hmi::exprcheck::PlcPaths>(hmiPlcPaths(env.plc));
            pathsOf = env.plc;
            pathsCount = env.plc->variables.size();
        }
        ctx.plc = *paths;
    }
    std::vector<hmi::exprcheck::Problem> problems;
    if (env.project) problems = hmi::exprcheck::check(ctx, clean, wantOf(expected));
    for (const auto& pb : problems) {
        if (pb.warning) continue;                 // 1.11.21 : un avertissement - Compiler le dit, la case n'est pas fautive
        if (!pb.unknownName.empty()) {
            Diag d = unknownDiag(env, t, true, pb.unknownName, expected, single, pb.suggestion, r.unknown);
            r.diags.push_back(std::move(d));
        } else {
            r.diags.push_back({pb.message, {}});
        }
    }
    for (const auto& root : ex.roots()) {
        Style z{};
        std::string ty;
        if (pathInfo(env, root, z, ty)) addSource(z);
        else if (env.plc && env.project && !knownRoot(env, root)
                 && std::none_of(r.unknown.begin(), r.unknown.end(), [&](const std::string& u) { return sameName(u, root); }))
            // Un nom que la verification n'a pas signale : on le dit quand meme.
            r.diags.push_back(unknownDiag(env, t, true, root, expected, single, {}, r.unknown));
    }
    if (!r.diags.empty()) {
        r.style = Style::Error;
        r.info = r.unknown.empty() ? "Formule en erreur." : "Formule avec un nom inconnu.";
        return r;
    }
    if (single) {
        Style z{};
        std::string type, detail;
        if (pathInfo(env, clean, z, type, &detail)) {
            r.type = type;
            if (!compatible(expected, type)) {
                r.style = Style::Error;
                Diag d{clean + " est " + type + " ; ce champ attend " + expectedLabel(expected) + ".", {}};
                Fix f;
                if (conversionFix(clean, type, expected, f)) d.fixes.push_back(f);
                if (hmi::isAggregateType(env.project, expected))
                    for (const auto& [name, z2] : variablesOfType(env, expected))
                        d.fixes.push_back({"Remplacer par " + name + " (" + zoneLabel(z2) + ")", name, true, {}});
                r.diags.push_back(std::move(d));
                r.info = zoneLong(z) + " : " + clean;
                return r;
            }
            r.style = markers ? Style::Markers : z;
            if (markers) addSource(z);
            r.info = zoneLong(z) + " : " + clean + (type.empty() ? "" : " \xC2\xB7 " + type) + (detail.empty() ? "" : " \xC2\xB7 " + detail);
            return r;
        }
    }
    r.style = markers ? Style::Markers : Style::Formula;
    if (r.sources.empty()) {
        r.info = "Formule sans variable : un calcul fixe.";
    } else {
        r.info = "Formule \xC2\xB7 sources : ";
        for (std::size_t i = 0; i < r.sources.size(); ++i) r.info += (i ? ", " : "") + zoneLabel(r.sources[i]);
    }
    return r;
}

std::string summary(const Result& r) {
    if (r.empty) return "Vide : la valeur par d\xC3\xA9" "faut du champ";
    const std::string name(info(r.style).name);
    if (r.info.empty()) return name;
    if (r.style == Style::Error) return name + " \xC2\xB7 " + r.info;
    return r.info;
}

ui::PropertyGrid::Legend legendOf(const Result& r) {
    ui::PropertyGrid::Legend l = legendOf(r.style);
    if (r.empty) {
        l.style = Style::Empty;
        l.text.clear();
        l.tip = "Aucune valeur : la valeur par d\xC3\xA9" "faut du champ.";
        return l;
    }
    if (r.style == Style::Formula || r.style == Style::Markers) l.dots = r.sources;
    l.tip = summary(r);
    if (!r.diags.empty()) l.tip += "\n" + r.diags.front().message;
    return l;
}

} // namespace app::valuekind
