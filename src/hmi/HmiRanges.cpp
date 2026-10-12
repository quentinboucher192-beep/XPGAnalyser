// =============================================================================
//  hmi/HmiRanges.cpp - 1.12.10 (lot F) : la plage et les seuils de la variable
//                      (voir HmiRanges.hpp)
// =============================================================================
#include "HmiRanges.hpp"

#include "HmiDisplay.hpp"
#include "HmiLive.hpp"       // autoValueSource
#include "HmiControls.hpp"   // parseZones
#include "HmiMarkers.hpp"
#include "HmiPublicVars.hpp"
#include "HmiRoles.hpp"
#include "HmiRuntime.hpp"     // isVariablePath
#include "HmiWidgets.hpp"     // splitSemicolons

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <utility>

namespace hmi::ranges {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Le chemin sans ses reperes ($Cuve$.Seuil -> Cuve.Seuil), sans les blancs du bord.
std::string bare(std::string_view text) { return trim(markers::strip(text, markers::Mode::Expression)); }

std::string rootOf(std::string_view path) {
    std::size_t end = 0;
    while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
    return trim(path.substr(0, end));
}

bool number(std::string_view text, double& out) { return parseNumber(trim(text), out); }

// Les cles de la vanne : la plage de son ouverture.
constexpr std::string_view kOpeningMin = "openingMin";
constexpr std::string_view kOpeningMax = "openingMax";

// Une cle de seuil.
bool thresholdKey(std::string_view key) {
    return key == "lowAlarm" || key == "low" || key == "high" || key == "highAlarm";
}

// La cle de la ligne d'Unites et formats que lit une cle d'objet : ymin -> min, openingMax -> max.
std::string_view lineKey(std::string_view key) {
    if (key == "ymin" || key == kOpeningMin) return "min";
    if (key == "ymax" || key == kOpeningMax) return "max";
    return key;
}

// Ce que apply changerait sur une cle : sa valeur fixe, et sa formule (vide : aucune).
struct Change {
    std::string key;
    std::string value;
    std::string expr;
};

// La valeur fixe d'une cle telle que l'objet la lit : la sienne ; absente, celle d'un objet neuf
// (un objet d'avant qui n'a pas la cle la lit a sa valeur par defaut).
std::string ownValue(const Object& o, std::string_view key) {
    if (const auto* p = o.find(key)) return trim(p->value);
    return defaultOf(o.kind, key);
}

// Les zones d'un objet neuf de ce genre (bargraphe, cadran), en % de l'echelle : 0..100 les dessine comme les
// zones ecrites d'avant, une plage prise a la variable les garde a leur place.
std::string percentZones(Kind k) {
    std::string out;
    for (const auto& z : parseZones(defaultOf(k, "zones")))
        out += (out.empty() ? "" : "; ") + formatNumber(z.from) + " %.." + formatNumber(z.to) + " % = " + z.color;
    return out;
}

// Une borne de l'objet telle qu'il la lira (la sienne, sinon celle de sa variable, sinon celle d'un objet neuf).
bool boundOf(const Project& p, const Object& o, std::string_view key, const View* in, double& out) {
    const auto* prop = o.find(key);
    if (prop && !prop->expr.empty()) return false;
    std::string text = prop ? trim(prop->value) : defaultOf(o.kind, key);
    if (text.empty() && followsVariableFormat(o)) text = inheritedFor(p, o, key, in).value;
    if (text.empty()) text = defaultOf(o.kind, key);
    return parseNumber(text, out);
}

// Les zones que donnent les seuils de la variable (en nombres) sur l'echelle de l'objet : sous l'alarme basse et
// au-dessus de l'alarme haute, la couleur des alarmes ; entre une alarme et son seuil, celle des seuils ; entre les
// seuils, le vert. Vide : la variable n'a pas de seuil en nombre (ou l'echelle ne se lit pas).
std::string thresholdZones(const Project& p, const Object& o, const View* in) {
    double lo = 0, hi = 0;
    if (!boundOf(p, o, "min", in, lo) || !boundOf(p, o, "max", in, hi) || lo >= hi) return {};
    double t[4] = {0, 0, 0, 0};
    bool has[4] = {false, false, false, false};
    bool any = false;
    for (int k = 0; k < 4; ++k) {
        const Inherited inh = inheritedFor(p, o, kThresholds[k], in);
        has[k] = !inh.value.empty() && parseNumber(inh.value, t[k]);
        any = any || has[k];
    }
    if (!any) return {};
    std::vector<double> cuts{lo, hi};
    for (int k = 0; k < 4; ++k)
        if (has[k] && t[k] > lo && t[k] < hi) cuts.push_back(t[k]);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    const auto colorAt = [&](double mid) -> std::string {
        if ((has[0] && mid < t[0]) || (has[3] && mid > t[3])) return "#E5534B";
        if ((has[1] && mid < t[1]) || (has[2] && mid > t[2])) return "#F2C94C";
        return "#2ECC71";
    };
    std::string out, color;
    double from = cuts.front();
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const std::string c = colorAt((cuts[i] + cuts[i + 1]) / 2);
        if (i > 0 && c != color) {
            out += (out.empty() ? "" : "; ") + formatNumber(from) + ".." + formatNumber(cuts[i]) + " = " + color;
            from = cuts[i];
        }
        color = c;
    }
    out += (out.empty() ? "" : "; ") + formatNumber(from) + ".." + formatNumber(cuts.back()) + " = " + color;
    return out;
}

// Les changements d'un objet (sans la vanne : son ouverture a part).
std::vector<Change> changesOf(const Project& p, const Object& o, const View* in) {
    std::vector<Change> out;
    std::vector<std::string_view> keys = inheritedKeys(o.kind);
    if (o.kind == Kind::Bargraph) keys.push_back("setpoint");
    const bool follows = followsVariableFormat(o);
    for (const auto key : keys) {
        const auto* prop = o.find(key);
        if (prop && !prop->expr.empty()) continue;            // une formule : la sienne
        const std::string own = ownValue(o, key);
        const std::string def = defaultOf(o.kind, key);
        // Le champ de saisie lit lui-meme un nom dans ses bornes (1.11.24) : il le garde en valeur.
        const bool namesAsValue = o.kind == Kind::InputField;
        if (!own.empty()) {
            // Un nom de variable tape sans = : la variable (sa valeur fixe, si la formule ne se lit pas : le defaut).
            if (isName(own) && !namesAsValue) out.push_back({std::string(key), def, own});
            continue;
        }
        if (key == "setpoint") continue;
        if (follows) {
            const Inherited inh = inheritedFor(p, o, key, in);
            if (!inh.value.empty()) {
                if (isName(inh.value) && !namesAsValue) out.push_back({std::string(key), def, inh.value});
                else out.push_back({std::string(key), inh.value, {}});
                continue;
            }
        }
        // Rien de la variable : la valeur d'un objet neuf (une jauge neuve : 0..100).
        if (!def.empty() && (!prop || prop->value != def)) out.push_back({std::string(key), def, {}});
    }
    // Les zones laissees vides d'un bargraphe, d'un cadran (un objet pose depuis la palette) : les seuils de la variable,
    // sinon celles d'un objet neuf, en % de l'echelle.
    if (o.kind == Kind::Bargraph || o.kind == Kind::Dial)
        if (const auto* zones = o.find("zones"); zones && zones->expr.empty() && trim(zones->value).empty()) {
            std::string z = follows ? thresholdZones(p, o, in) : std::string{};
            if (z.empty()) z = percentZones(o.kind);
            if (!z.empty()) out.push_back({"zones", z, {}});
        }
    return out;
}

// La vanne : l'ouverture lue en % de sa plage (openingMin, openingMax, sinon celle de sa variable).
// Vrai (et `out`) : l'ouverture change.
bool valveOpening(const Project& p, const Object& o, const View* in, Prop& out) {
    const auto* op = o.find("opening");
    if (!op) return false;
    const std::string source = !op->expr.empty() ? trim(op->expr) : trim(op->value);
    if (source.empty()) return false;
    std::string ends[2];
    const std::string_view keys[2] = {kOpeningMin, kOpeningMax};
    for (int k = 0; k < 2; ++k) {
        const auto* prop = o.find(keys[k]);
        if (prop && !prop->expr.empty()) { ends[k] = "(" + prop->expr + ")"; continue; }
        ends[k] = ownValue(o, keys[k]);
        if (ends[k].empty() && followsVariableFormat(o)) ends[k] = inheritedFor(p, o, keys[k], in).value;
    }
    if (ends[0].empty() && ends[1].empty()) return false;
    if (ends[0].empty()) ends[0] = "0";
    if (ends[1].empty()) ends[1] = "100";
    double lo = 0, hi = 0, v = 0;
    const bool fixedEnds = number(ends[0], lo) && number(ends[1], hi);
    if (fixedEnds && lo == 0 && hi == 100) return false;                // 0..100 : deja des %
    out = *op;
    if (op->expr.empty() && number(source, v)) {
        if (!fixedEnds) return false;                                    // une valeur fixe et des bornes calculees : telle quelle
        out.value = hi != lo ? formatNumber((v - lo) * 100.0 / (hi - lo)) : std::string("0");
        return true;
    }
    if (op->expr.empty() && !isName(source)) return false;
    const auto term = [](const std::string& t) {
        double x = 0;
        return parseNumber(t, x) ? formatNumber(x) : "(" + t + ")";
    };
    out.expr = "((" + source + ") - " + term(ends[0]) + ") * 100.0 / (" + term(ends[1]) + " - " + term(ends[0]) + ")";
    return true;
}

// Les noms (racines) que le projet connait : une variable IHM, un parametre de la vue, une vue
// (Vue.Objet.Cle), SYS, l'automate.
bool knownName(const Project& p, const NameExists& plc, std::string_view path, const View* in) {
    const std::string root = rootOf(bare(path));
    if (root.empty()) return false;
    if (p.variable(root) || pub::isSysRoot(root) || pub::viewNamed(p, root)) return true;
    if (in) {
        if (in->param(root)) return true;
        if (in->ownerSymbol != kNoId)
            if (const View* owner = p.view(in->ownerSymbol); owner && owner->param(root)) return true;
    }
    return !plc || plc(root);
}

} // namespace

// ============================================================ les champs =====
const std::string& field(const VariableDisplay& d, std::string_view key) {
    static const std::string none;
    if (key == "min") return d.min;
    if (key == "max") return d.max;
    if (key == "low") return d.low;
    if (key == "high") return d.high;
    if (key == "lowAlarm") return d.lowAlarm;
    if (key == "highAlarm") return d.highAlarm;
    return none;
}

std::string* fieldOf(VariableDisplay& d, std::string_view key) {
    if (key == "min") return &d.min;
    if (key == "max") return &d.max;
    if (key == "low") return &d.low;
    if (key == "high") return &d.high;
    if (key == "lowAlarm") return &d.lowAlarm;
    if (key == "highAlarm") return &d.highAlarm;
    return nullptr;
}

std::string label(std::string_view key) {
    if (key == "min" || key == "ymin" || key == kOpeningMin) return "Min";
    if (key == "max" || key == "ymax" || key == kOpeningMax) return "Max";
    if (key == "low") return "Seuil bas";
    if (key == "high") return "Seuil haut";
    if (key == "lowAlarm") return "Alarme basse";
    if (key == "highAlarm") return "Alarme haute";
    if (key == "setpoint") return "Consigne";
    return std::string(key);
}

bool isName(std::string_view text) {
    const std::string t = bare(text);
    double x = 0;
    if (t.empty() || parseNumber(t, x)) return false;
    return isVariablePath(t);
}

bool validValue(std::string_view text) {
    const std::string t = trim(text);
    double x = 0;
    return t.empty() || parseNumber(t, x) || isName(t);
}

// ============================================================ la variable ====
bool scalingOf(const Project& p, std::string_view path, double& lo, double& hi) {
    const std::string root = rootOf(bare(path));
    if (root.empty()) return false;
    const Variable* v = p.variable(root);
    if (!v || !v->bound() || !v->scaled() || v->engMin == v->engMax) return false;
    lo = std::min(v->engMin, v->engMax);
    hi = std::max(v->engMin, v->engMax);
    return true;
}

Inherited inheritedOf(const Project& p, std::string_view path, std::string_view key, const View* in, bool scaling) {
    Inherited out;
    const std::string wanted = bare(path);
    if (wanted.empty()) return out;
    const std::string_view k = lineKey(key);
    if (const auto* d = displayOf(p, wanted, in)) {
        const std::string v = trim(field(*d, k));
        if (!v.empty()) {
            out.value = v;
            out.origin = trim(d->path);
            return out;
        }
    }
    double lo = 0, hi = 0;
    if (scaling && (k == "min" || k == "max") && scalingOf(p, wanted, lo, hi)) {
        out.value = formatNumber(k == "min" ? lo : hi);
        out.origin = "mise \xC3\xA0 l'\xC3\xA9" "chelle de " + rootOf(wanted);
        out.scaling = true;
    }
    return out;
}

// ============================================================ les objets =====
std::vector<std::string_view> inheritedKeys(Kind k) {
    switch (k) {
        case Kind::ProgressBar: case Kind::Bargraph: case Kind::Dial:
        case Kind::Slider: case Kind::Knob: case Kind::InputField: case Kind::Cylinder:
            return {"min", "max"};
        case Kind::NumericDisplay: case Kind::IsaInstrument:
            return {"lowAlarm", "low", "high", "highAlarm"};
        case Kind::Tank: case Kind::GasBottle: case Kind::Silo: case Kind::Hopper:
        case Kind::Gauge: case Kind::Thermometer:   // la jauge et le thermometre : leurs seuils, ceux de la variable
            return {"min", "max", "lowAlarm", "low", "high", "highAlarm"};
        case Kind::Trend:
            return {"ymin", "ymax"};
        case Kind::Valve:
            return {kOpeningMin, kOpeningMax};
        default:
            return {};
    }
}

bool inherits(Kind k) { return !inheritedKeys(k).empty(); }

bool takesName(Kind k, std::string_view key) { return roles::rangeNameKey(k, key); }

std::string defaultOf(Kind k, std::string_view key) {
    // Une fois par genre : un objet neuf (makeObject) dit ses valeurs.
    static std::mutex guard;
    static std::map<Kind, Object> fresh;
    const std::lock_guard<std::mutex> lock(guard);
    auto it = fresh.find(k);
    if (it == fresh.end()) it = fresh.emplace(k, makeObject(k, kNoId, {}, 0, 0, kNoId)).first;
    return trim(it->second.text(key));
}

std::string variableOf(const Object& o) {
    if (o.kind == Kind::Valve) {
        const auto* op = o.find("opening");
        if (!op) return {};
        const std::string src = !op->expr.empty() ? op->expr : op->value;
        return isName(src) ? bare(src) : std::string{};
    }
    if (const auto* v = o.find("value"); v && !v->expr.empty()) return isName(v->expr) ? bare(v->expr) : std::string{};
    if (const std::string shown = autoValueSource(o); !shown.empty()) return isName(shown) ? bare(shown) : std::string{};
    // Une Valeur de symbole tapee sans = (Niveau_1) se lit comme la variable (1.12.5, D13).
    if (kindIsSynoptic(o.kind))
        if (const auto* v = o.find("value"); v && v->expr.empty() && isName(v->value)) return bare(v->value);
    return {};
}

Inherited inheritedFor(const Project& p, const Object& o, std::string_view key, const View* in) {
    // La mise a l'echelle : seulement pour une cle qui avait une valeur par defaut (une jauge neuve) -
    // pas pour les bornes vides d'un champ de saisie, ni l'ouverture d'une vanne.
    const bool scaling = !defaultOf(o.kind, key).empty();
    if (o.kind == Kind::Trend) {
        // La courbe : l'union des plages de ses plumes (celles qui en ont une, en nombres).
        const bool low = key == "ymin";
        Inherited out;
        bool any = false;
        double acc = 0;
        std::string first;
        int count = 0;
        for (const auto& pen : splitSemicolons(o.text("variables"))) {
            if (!isName(pen)) continue;
            const Inherited one = inheritedOf(p, pen, low ? "min" : "max", in, scaling);
            double x = 0;
            if (one.value.empty() || !parseNumber(one.value, x)) continue;
            acc = !any ? x : low ? std::min(acc, x) : std::max(acc, x);
            if (!any) {
                first = one.origin;
                out.scaling = one.scaling;
            }
            any = true;
            ++count;
        }
        if (!any) return out;
        out.value = formatNumber(acc);
        out.origin = count > 1 ? first + " et " + std::to_string(count - 1) + " autre(s)" : first;
        return out;
    }
    const std::string var = variableOf(o);
    if (var.empty()) return {};
    return inheritedOf(p, var, key, in, scaling);
}

bool apply(Object& o, const Project& p, const View* in) {
    if (!inherits(o.kind)) return false;
    bool changed = false;
    if (o.kind == Kind::Valve) {
        Prop opening;
        if (valveOpening(p, o, in, opening)) {
            *o.find("opening") = std::move(opening);
            changed = true;
        }
        return changed;
    }
    for (auto& c : changesOf(p, o, in)) {
        if (!c.expr.empty()) {
            o.set(c.key, c.value);
            o.setExpr(c.key, c.expr);
        } else {
            o.set(c.key, c.value);
        }
        changed = true;
    }
    return changed;
}

bool apply(View& v, const Project& p) {
    bool changed = false;
    for (auto& o : v.objects) changed = apply(o, p, &v) || changed;
    return changed;
}

bool needed(const Project& p, const View& v) {
    for (const auto& o : v.objects) {
        if (!inherits(o.kind)) continue;
        if (o.kind == Kind::Valve) {
            Prop dummy;
            if (valveOpening(p, o, &v, dummy)) return true;
            continue;
        }
        if (!changesOf(p, o, &v).empty()) return true;
    }
    return false;
}

bool preview(const Project& p, const View& v, const Object& o, Object& out) {
    if (!inherits(o.kind)) return false;
    if (o.kind == Kind::Valve) {
        Prop opening;
        if (!valveOpening(p, o, &v, opening) || !opening.expr.empty()) return false;   // une formule : l'editeur ne la calcule pas
        out = o;
        *out.find("opening") = std::move(opening);
        // Deja en % : la copie dessinee ne se recalcule pas (le dessin la repasse ici).
        out.set(std::string(kOpeningMin), "0");
        out.set(std::string(kOpeningMax), "100");
        return true;
    }
    const auto changes = changesOf(p, o, &v);
    bool any = false;
    for (const auto& c : changes) any = any || c.expr.empty();
    if (!any) return false;
    out = o;
    // Dans l'editeur, une variable ne se lit pas : une borne qui en vient garde sa valeur fixe.
    for (const auto& c : changes)
        if (c.expr.empty()) out.set(c.key, c.value);
    return true;
}

std::string hint(const Project& p, const View& v, const Object& o, std::string_view key) {
    if (key == "zones" && (o.kind == Kind::Bargraph || o.kind == Kind::Dial)) {
        const auto* zones = o.find("zones");
        if (!zones || !zones->expr.empty() || !trim(zones->value).empty()) return {};
        const std::string var = variableOf(o);
        if (followsVariableFormat(o) && !thresholdZones(p, o, &v).empty()) return "les seuils de " + var;
        return percentZones(o.kind) + " (par d\xC3\xA9" "faut)";
    }
    bool ours = false;
    for (const auto kk : inheritedKeys(o.kind)) ours = ours || kk == key;
    if (!ours) return {};
    const auto* prop = o.find(key);
    if (prop && (!prop->expr.empty() || !trim(prop->value).empty())) return {};
    if (!prop && !defaultOf(o.kind, key).empty()) return {};
    if (followsVariableFormat(o)) {
        const Inherited inh = inheritedFor(p, o, key, &v);
        if (!inh.value.empty())
            return inh.value + (inh.scaling ? " (" + inh.origin + ")" : " (de " + inh.origin + ")");
    }
    const std::string def = defaultOf(o.kind, key);
    if (!def.empty()) return def + " (par d\xC3\xA9" "faut)";
    if (o.kind == Kind::Valve) return key == kOpeningMin ? "0 (par d\xC3\xA9" "faut)" : "100 (par d\xC3\xA9" "faut)";
    if (o.kind == Kind::InputField) return "sans borne";
    return thresholdKey(key) ? std::string("aucun") : std::string{};
}

void followVariable(Object& o) {
    for (const char* key : {"min", "max", "ymin", "ymax"})
        if (auto* prop = o.find(key); prop && takesName(o.kind, key)) {
            prop->value.clear();
            prop->expr.clear();
        }
    // Les zones d'un bargraphe, d'un cadran neufs : vides - les seuils de la variable, sinon celles d'un objet neuf en %
    // de l'echelle (une plage 0..10 prise a la variable les garde a leur place ; avant, 0-60 couvrait tout).
    if (auto* zones = o.find("zones"); zones && zones->expr.empty() && (o.kind == Kind::Bargraph || o.kind == Kind::Dial)
                                       && trim(zones->value) == defaultOf(o.kind, "zones"))
        zones->value.clear();
}

// ============================================================ Generer ========
std::vector<Issue> check(const Project& p, const NameExists& plc) {
    using S = Issue::Severity;
    std::vector<Issue> out;
    const auto add = [&](S sev, std::string category, Id view, Id object, std::string key, std::string message) {
        Issue i;
        i.severity = sev;
        i.category = std::move(category);
        i.view = view;
        i.object = object;
        i.property = std::move(key);
        i.message = std::move(message);
        out.push_back(std::move(i));
    };
    // ---- Unites et formats : chaque ligne
    for (const auto& d : p.displays) {
        const auto issue = [&](std::string msg) { add(S::Warning, "Unit\xC3\xA9", kNoId, kNoId, d.path, d.path + " : " + std::move(msg)); };
        double val[6] = {0, 0, 0, 0, 0, 0};
        bool has[6] = {false, false, false, false, false, false};
        for (int k = 0; k < 6; ++k) {
            const std::string t = trim(field(d, kKeys[k]));
            if (t.empty()) continue;
            if (number(t, val[k])) { has[k] = true; continue; }
            if (!isName(t)) issue(label(kKeys[k]) + " \xC2\xAB " + t + " \xC2\xBB : ni un nombre ni une variable");
            else if (!knownName(p, plc, t, nullptr))
                issue(label(kKeys[k]) + " \xC2\xAB " + t + " \xC2\xBB : variable inconnue (un nom tap\xC3\xA9 sans = se lit comme la variable)");
        }
        // Min et Max : ceux de la ligne, sinon la mise a l'echelle.
        double lo = val[0], hi = val[1];
        bool hasLo = has[0], hasHi = has[1];
        double sLo = 0, sHi = 0;
        if (scalingOf(p, d.path, sLo, sHi)) {
            if (!hasLo && trim(d.min).empty()) { lo = sLo; hasLo = true; }
            if (!hasHi && trim(d.max).empty()) { hi = sHi; hasHi = true; }
        }
        if (hasLo && hasHi && lo >= hi)
            issue("Min " + formatNumber(lo) + " \xE2\x89\xA5 Max " + formatNumber(hi) + " : la plage est vide (les objets qui la prennent ne montreront rien)");
        for (int k = 2; k < 6; ++k)
            if (has[k] && hasLo && hasHi && lo < hi && (val[k] < lo || val[k] > hi))
                issue(label(kKeys[k]) + " " + formatNumber(val[k]) + " hors de la plage " + formatNumber(lo) + ".." + formatNumber(hi)
                      + " : les objets le montrent au bord");
        // L'ordre : alarme basse <= seuil bas <= seuil haut <= alarme haute (k : 4, 2, 3, 5).
        const int order[4] = {4, 2, 3, 5};
        bool told = false;
        for (int a = 0; a < 4 && !told; ++a)
            for (int b = a + 1; b < 4 && !told; ++b)
                if (has[order[a]] && has[order[b]] && val[order[a]] > val[order[b]]) {
                    issue("les seuils ne sont pas dans l'ordre : alarme basse \xE2\x89\xA4 seuil bas \xE2\x89\xA4 seuil haut \xE2\x89\xA4 alarme haute ("
                          + label(kKeys[order[a]]) + " " + formatNumber(val[order[a]]) + ", " + label(kKeys[order[b]]) + " "
                          + formatNumber(val[order[b]]) + ")");
                    told = true;
                }
    }
    // ---- les objets : ce qu'ils liront (le leur, sinon celui de leur variable)
    for (const auto& v : p.views)
        for (const auto& o : v.objects) {
            if (!inherits(o.kind)) continue;
            const auto issue = [&](const std::string& key, std::string msg) {
                add(S::Warning, "Objet", v.id, o.id, key, std::move(msg));
            };
            std::vector<std::string_view> keys = inheritedKeys(o.kind);
            if (o.kind == Kind::Bargraph) keys.push_back("setpoint");
            struct Seen {
                double value{0};
                bool   has{false};
                bool   inherited{false};
                std::string origin;
            };
            std::map<std::string, Seen, std::less<>> seen;
            for (const auto key : keys) {
                const auto* prop = o.find(key);
                if (prop && !prop->expr.empty()) continue;
                const std::string own = ownValue(o, key);
                Seen s;
                if (!own.empty()) {
                    if (isName(own)) {
                        // (Le champ de saisie : ses bornes en nom sont deja verifiees, 1.11.24.)
                        if (o.kind != Kind::InputField && !knownName(p, plc, own, &v))
                            issue(std::string(key), label(key) + " \xC2\xAB " + own + " \xC2\xBB : variable inconnue (un nom tap\xC3\xA9 sans = se lit comme la variable)");
                        continue;
                    }
                    s.has = number(own, s.value);
                } else if (key != "setpoint" && followsVariableFormat(o)) {
                    const Inherited inh = inheritedFor(p, o, key, &v);
                    if (!inh.value.empty() && number(inh.value, s.value)) {
                        s.has = true;
                        s.inherited = true;
                        s.origin = inh.scaling ? inh.origin : "de " + inh.origin;
                    }
                }
                if (!s.has && own.empty()) {
                    const std::string def = defaultOf(o.kind, key);
                    s.has = !def.empty() && number(def, s.value);
                }
                seen[std::string(key)] = s;
            }
            const auto shown = [&](const std::string& key) {
                const auto& s = seen[key];
                return label(key) + " " + formatNumber(s.value) + (s.inherited ? " (" + s.origin + ")" : std::string{});
            };
            const std::string loKey = o.kind == Kind::Trend ? "ymin" : o.kind == Kind::Valve ? std::string(kOpeningMin) : "min";
            const std::string hiKey = o.kind == Kind::Trend ? "ymax" : o.kind == Kind::Valve ? std::string(kOpeningMax) : "max";
            const auto& lo = seen[loKey];
            const auto& hi = seen[hiKey];
            // Une plage ecrite sur l'objet qui n'est pas celle de sa variable (un objet d'avant, 0..100 sur une variable
            // mise a l'echelle 0..10) : une information - vider Min et Max lui fait suivre la variable.
            if (lo.has && hi.has && !lo.inherited && !hi.inherited && followsVariableFormat(o) && o.kind != Kind::InputField
                && o.kind != Kind::Trend && o.kind != Kind::Valve) {
                const Inherited vlo = inheritedFor(p, o, loKey, &v), vhi = inheritedFor(p, o, hiKey, &v);
                double a = 0, b = 0;
                if (number(vlo.value, a) && number(vhi.value, b) && a < b && (a != lo.value || b != hi.value))
                    add(S::Info, "Objet", v.id, o.id, loKey,
                        "Min, Max " + formatNumber(lo.value) + ".." + formatNumber(hi.value) + " : sa variable va de " + formatNumber(a) + " \xC3\xA0 "
                            + formatNumber(b) + " (" + (vhi.scaling ? vhi.origin : "de " + vhi.origin)
                            + ") - videz Min et Max pour qu'il la suive");
            }
            // Min >= Max : deja une erreur pour une valeur ecrite sur un curseur, un bargraphe, une cuve... ;
            // ici, une borne heritee, ou une jauge et une barre de progression (rien ne le disait).
            const bool plainCheck = o.kind == Kind::Gauge || o.kind == Kind::ProgressBar || o.kind == Kind::Trend || o.kind == Kind::Valve
                                 || o.kind == Kind::InputField;
            // (Ce qui vient tout entier de la ligne de la variable est dit une fois, sur la ligne ; ici, le melange de
            // l'objet et de sa variable.)
            if (lo.has && hi.has && lo.value >= hi.value && (lo.inherited != hi.inherited || (!lo.inherited && plainCheck)))
                issue(loKey, shown(loKey) + " \xE2\x89\xA5 " + shown(hiKey) + " : la plage est vide");
            // Les seuils : hors de la plage, dans le desordre - quand l'un d'eux (ou la plage) vient de la variable
            // (les valeurs ecrites sur l'objet : deja dites, 1.12.6).
            if (lo.has && hi.has && lo.value < hi.value)
                for (const auto key : kThresholds) {
                    const auto it = seen.find(key);
                    if (it == seen.end() || !it->second.has) continue;
                    const int from = (it->second.inherited ? 1 : 0) + (lo.inherited ? 1 : 0) + (hi.inherited ? 1 : 0);
                    if (from == 0 || from == 3) continue;
                    if (it->second.value < lo.value || it->second.value > hi.value)
                        issue(std::string(key), shown(std::string(key)) + " hors de la plage " + formatNumber(lo.value) + ".." + formatNumber(hi.value)
                                                    + " : il se dessine au bord");
                }
            bool told = false;
            for (int a = 0; a < 4 && !told; ++a)
                for (int b = a + 1; b < 4 && !told; ++b) {
                    const auto ia = seen.find(kThresholds[a]), ib = seen.find(kThresholds[b]);
                    if (ia == seen.end() || ib == seen.end() || !ia->second.has || !ib->second.has) continue;
                    if (ia->second.inherited == ib->second.inherited) continue;
                    if (ia->second.value > ib->second.value) {
                        issue(std::string(kThresholds[b]), "les seuils ne sont pas dans l'ordre : alarme basse \xE2\x89\xA4 seuil bas \xE2\x89\xA4 seuil haut "
                                                           "\xE2\x89\xA4 alarme haute (" + shown(std::string(kThresholds[a])) + ", "
                                                           + shown(std::string(kThresholds[b])) + ")");
                        told = true;
                    }
                }
        }
    return out;
}

} // namespace hmi::ranges
