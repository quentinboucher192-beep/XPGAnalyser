// =============================================================================
//  hmi/HmiStyles.cpp - 1.23.0 : les styles par type d'objet (voir HmiStyles.hpp)
// =============================================================================
#include "HmiStyles.hpp"

#include "HmiDesign.hpp"
#include "HmiRoles.hpp"
#include "HmiSymbols.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace hmi::styles {

namespace {

constexpr std::string_view kBrokenKey = "styleBroken";
constexpr std::string_view kWasKey = "styleWas";
constexpr char kSep = '\x1E';     // entre deux cles de styleWas
constexpr char kEq = '\x1F';      // entre la cle et la valeur

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool sameName(std::string_view a, std::string_view b) { return upper(a) == upper(b); }

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

// Les proprietes qui ne se stylent pas : l'identite et l'etat du style lui-meme.
bool styleable(std::string_view key) {
    return key != "namedStyle" && key != kBrokenKey && key != kWasKey && key != "symbol" && key != "params";
}

// ---- l'etat d'un objet -------------------------------------------------------------------------------------------------
std::vector<std::string> splitComma(std::string_view s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const auto at = s.find(',', start);
        const std::string part = trim(s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
        if (!part.empty() && std::find(out.begin(), out.end(), part) == out.end()) out.push_back(part);
        if (at == std::string_view::npos) break;
        start = at + 1;
    }
    return out;
}
void writeBroken(Object& o, const std::vector<std::string>& keys) {
    std::string v;
    for (const auto& k : keys) v += (v.empty() ? "" : ",") + k;
    if (v.empty()) std::erase_if(o.props, [](const Prop& p) { return p.key == kBrokenKey; });
    else o.set(kBrokenKey, v);
}
std::map<std::string, std::string> readWas(const Object& o) {
    std::map<std::string, std::string> out;
    const Prop* p = o.find(kWasKey);
    if (!p) return out;
    std::size_t start = 0;
    const std::string& s = p->value;
    while (start < s.size()) {
        auto at = s.find(kSep, start);
        if (at == std::string::npos) at = s.size();
        const std::string item = s.substr(start, at - start);
        if (const auto eq = item.find(kEq); eq != std::string::npos) out[item.substr(0, eq)] = item.substr(eq + 1);
        start = at + 1;
    }
    return out;
}
void writeWas(Object& o, const std::map<std::string, std::string>& was) {
    std::string v;
    for (const auto& [k, val] : was) {
        if (!v.empty()) v += kSep;
        v += k;
        v += kEq;
        v += val;
    }
    if (v.empty()) std::erase_if(o.props, [](const Prop& p) { return p.key == kWasKey; });
    else o.set(kWasKey, v);
}

const View* symbolViewOf(const Project& p, const Object& o) {
    return o.kind == Kind::SymbolInstance ? symbolOf(p, o) : nullptr;
}
// Le rang d'un parametre du symbole (nullopt : pas un parametre).
std::optional<std::size_t> paramIndex(const View& sym, std::string_view name) {
    for (std::size_t i = 0; i < sym.params.size(); ++i)
        if (sameName(sym.params[i].name, name)) return i;
    return std::nullopt;
}

// Ecrire une cle sur l'objet : une propriete (la valeur statique ; une expression reste la sienne), ou un parametre.
bool writeValue(const Project& p, Object& o, std::string_view key, const std::string& value) {
    if (key.substr(0, kParamPrefix.size()) == kParamPrefix) {
        const View* sym = symbolViewOf(p, o);
        if (!sym) return false;
        const auto at = paramIndex(*sym, key.substr(kParamPrefix.size()));
        if (!at) return false;
        o.set("params", withArgument(*sym, o.text("params"), *at, value));
        return true;
    }
    Prop* mine = o.find(key);
    if (!mine) return false;
    mine->value = value;
    return true;
}

// Les valeurs d'une cible, en carte (la derniere gagne : une cle donnee deux fois).
void collect(const std::vector<Prop>& props, std::vector<Prop>& out, std::set<std::string>& seen) {
    for (const auto& pr : props)
        if (!pr.key.empty() && seen.insert(pr.key).second) out.push_back(pr);
}

// ---- les styles fournis --------------------------------------------------------------------------------------------------
using Values = std::vector<std::pair<const char*, const char*>>;
StyleTarget makeTarget(std::string_view type, const Values& values) {
    StyleTarget t;
    t.type = std::string(type);
    for (const auto& [k, v] : values) t.props.push_back({k, v, {}});
    return t;
}
// Le style d'origine : les valeurs d'un objet neuf du logiciel, pour les cles d'apparence de chaque type.
StyleTarget originalTarget(Kind kind, const std::vector<std::string>& keys) {
    StyleTarget t;
    t.type = std::string(kKindInfos[static_cast<std::size_t>(kind)].key);
    const Object o = makeObject(kind, 1, {}, 0, 0, kNoId);
    for (const auto& k : keys)
        if (const Prop* pr = o.find(k)) t.props.push_back({k, pr->value, {}});
    return t;
}

std::vector<Style> makeBuiltins() {
    std::vector<Style> out;
    const auto key = [](Kind k) { return kKindInfos[static_cast<std::size_t>(k)].key; };
    const std::vector<Kind> equipment = {Kind::Pump, Kind::Motor, Kind::Fan, Kind::Compressor, Kind::Valve};
    // ISA-101 : le gris neutre ; la couleur ne dit que les anomalies (le defaut, l'alarme). Un equipement en marche est
    // plein (gris fonce), a l'arret vide (gris tres clair) : l'etat se lit sans couleur.
    {
        Style s;
        s.name = "ISA-101";
        s.description = "IHM haute performance (ISA-101) : gris neutres, texte sombre ; la couleur est r\xC3\xA9serv\xC3\xA9" "e aux "
                        "anomalies (d\xC3\xA9" "faut, alarme). Un \xC3\xA9quipement en marche est plein, \xC3\xA0 l'arr\xC3\xAAt vide.";
        s.targets.push_back(makeTarget(key(Kind::Button), {{"fill", "#D4D4D4"}, {"stroke", "#8C8C8C"}, {"strokeWidth", "1"},
                                                           {"radius", "2"}, {"textColor", "#1C1C1C"}}));
        s.targets.push_back(makeTarget(key(Kind::Text), {{"textColor", "#1C1C1C"}}));
        s.targets.push_back(makeTarget(key(Kind::Rectangle), {{"fill", "#E0E0E0"}, {"stroke", "#A6A6A6"}, {"strokeWidth", "1"}}));
        s.targets.push_back(makeTarget(key(Kind::NumericDisplay), {{"fill", "#F5F5F5"}, {"stroke", "#A6A6A6"}, {"textColor", "#1C1C1C"},
                                                                   {"colorWarning", "#E8A33D"}, {"colorAlarm", "#D32F2F"}}));
        s.targets.push_back(makeTarget(key(Kind::InputField), {{"fill", "#FFFFFF"}, {"stroke", "#8C8C8C"}, {"textColor", "#1C1C1C"}}));
        s.targets.push_back(makeTarget(key(Kind::Indicator), {{"colorOn", "#4D4D4D"}, {"colorOff", "#E6E6E6"}, {"stroke", "#7A7A7A"}}));
        for (Kind k : equipment)
            s.targets.push_back(makeTarget(key(k), {{"colorOn", "#4D4D4D"}, {"colorOff", "#F5F5F5"}, {"colorFault", "#D32F2F"},
                                                    {"stroke", "#4D4D4D"}, {"textColor", "#1C1C1C"}}));
        s.targets.push_back(makeTarget(key(Kind::Tank), {{"fill", "#F0F0F0"}, {"fillColor", "#A6A6A6"}, {"colorFault", "#D32F2F"},
                                                         {"stroke", "#4D4D4D"}, {"textColor", "#1C1C1C"}}));
        s.targets.push_back(makeTarget(key(Kind::Pipe), {{"fill", "#9E9E9E"}, {"fluidColor", "#BDBDBD"}, {"stroke", "#7A7A7A"}}));
        s.targets.push_back(makeTarget(key(Kind::Bargraph), {{"zones", "0-100 = #8C8C8C"}, {"setpointColor", "#1C1C1C"},
                                                             {"textColor", "#1C1C1C"}}));
        out.push_back(std::move(s));
    }
    // Sombre (d'origine) : les valeurs d'un objet neuf du logiciel - pour revenir a l'aspect de depart.
    {
        Style s;
        s.name = "Sombre (d'origine)";
        s.description = "L'aspect d'un objet neuf du logiciel, sur fond sombre : pour revenir au d\xC3\xA9part.";
        s.targets.push_back(originalTarget(Kind::Button, {"fill", "stroke", "strokeWidth", "radius", "textColor"}));
        s.targets.push_back(originalTarget(Kind::Text, {"textColor"}));
        s.targets.push_back(originalTarget(Kind::Rectangle, {"fill", "stroke", "strokeWidth"}));
        s.targets.push_back(originalTarget(Kind::NumericDisplay, {"fill", "stroke", "textColor", "colorWarning", "colorAlarm"}));
        s.targets.push_back(originalTarget(Kind::InputField, {"fill", "stroke", "textColor"}));
        s.targets.push_back(originalTarget(Kind::Indicator, {"colorOn", "colorOff", "stroke"}));
        for (Kind k : equipment) s.targets.push_back(originalTarget(k, {"colorOn", "colorOff", "colorFault", "stroke", "textColor"}));
        s.targets.push_back(originalTarget(Kind::Tank, {"fill", "fillColor", "colorFault", "stroke", "textColor"}));
        s.targets.push_back(originalTarget(Kind::Pipe, {"fill", "fluidColor", "stroke"}));
        s.targets.push_back(originalTarget(Kind::Bargraph, {"zones", "setpointColor", "textColor"}));
        out.push_back(std::move(s));
    }
    // Clair : fond clair, accents bleus, les etats en couleur (vert, rouge).
    {
        Style s;
        s.name = "Clair";
        s.description = "Fond clair et accents bleus ; les \xC3\xA9tats en couleur (marche en vert, d\xC3\xA9" "faut en rouge).";
        s.targets.push_back(makeTarget(key(Kind::Button), {{"fill", "#1F5F99"}, {"stroke", "#184A77"}, {"strokeWidth", "1"},
                                                           {"radius", "4"}, {"textColor", "#FFFFFF"}}));
        s.targets.push_back(makeTarget(key(Kind::Text), {{"textColor", "#1A1D1B"}}));
        s.targets.push_back(makeTarget(key(Kind::Rectangle), {{"fill", "#F4F6F4"}, {"stroke", "#B4BAB5"}, {"strokeWidth", "1"}}));
        s.targets.push_back(makeTarget(key(Kind::NumericDisplay), {{"fill", "#FFFFFF"}, {"stroke", "#B4BAB5"}, {"textColor", "#1A1D1B"},
                                                                   {"colorWarning", "#8A5C00"}, {"colorAlarm", "#B3261E"}}));
        s.targets.push_back(makeTarget(key(Kind::InputField), {{"fill", "#FFFFFF"}, {"stroke", "#858C87"}, {"textColor", "#1A1D1B"}}));
        s.targets.push_back(makeTarget(key(Kind::Indicator), {{"colorOn", "#1C7A3A"}, {"colorOff", "#C3C9C4"}, {"stroke", "#858C87"}}));
        for (Kind k : equipment)
            s.targets.push_back(makeTarget(key(k), {{"colorOn", "#1C7A3A"}, {"colorOff", "#C3C9C4"}, {"colorFault", "#B3261E"},
                                                    {"stroke", "#4A514C"}, {"textColor", "#1A1D1B"}}));
        s.targets.push_back(makeTarget(key(Kind::Tank), {{"fill", "#FAFBFA"}, {"fillColor", "#2571B7"}, {"colorFault", "#B3261E"},
                                                         {"stroke", "#4A514C"}, {"textColor", "#1A1D1B"}}));
        s.targets.push_back(makeTarget(key(Kind::Pipe), {{"fill", "#99A09B"}, {"fluidColor", "#2571B7"}, {"stroke", "#858C87"}}));
        s.targets.push_back(makeTarget(key(Kind::Bargraph), {{"zones", "0-60 = #1C7A3A; 60-80 = #8A5C00; 80-100 = #B3261E"},
                                                             {"setpointColor", "#1A1D1B"}, {"textColor", "#1A1D1B"}}));
        out.push_back(std::move(s));
    }
    // Contraste eleve : noir et blanc, jaune pour ce qui agit (lisible de loin, en plein soleil).
    {
        Style s;
        s.name = "Contraste \xC3\xA9lev\xC3\xA9";
        s.description = "Noir et blanc, jaune pour ce qui agit et l'avertissement, rouge pour le d\xC3\xA9" "faut : lisible de loin.";
        s.targets.push_back(makeTarget(key(Kind::Button), {{"fill", "#000000"}, {"stroke", "#FFD500"}, {"strokeWidth", "2"},
                                                           {"radius", "0"}, {"textColor", "#FFD500"}}));
        s.targets.push_back(makeTarget(key(Kind::Text), {{"textColor", "#FFFFFF"}}));
        s.targets.push_back(makeTarget(key(Kind::Rectangle), {{"fill", "#000000"}, {"stroke", "#FFFFFF"}, {"strokeWidth", "2"}}));
        s.targets.push_back(makeTarget(key(Kind::NumericDisplay), {{"fill", "#000000"}, {"stroke", "#FFFFFF"}, {"textColor", "#FFFFFF"},
                                                                   {"colorWarning", "#FFD500"}, {"colorAlarm", "#FF3B30"}}));
        s.targets.push_back(makeTarget(key(Kind::InputField), {{"fill", "#000000"}, {"stroke", "#FFD500"}, {"textColor", "#FFFFFF"}}));
        s.targets.push_back(makeTarget(key(Kind::Indicator), {{"colorOn", "#FFFFFF"}, {"colorOff", "#3A3A3A"}, {"stroke", "#FFFFFF"}}));
        for (Kind k : equipment)
            s.targets.push_back(makeTarget(key(k), {{"colorOn", "#FFFFFF"}, {"colorOff", "#3A3A3A"}, {"colorFault", "#FF3B30"},
                                                    {"stroke", "#FFFFFF"}, {"textColor", "#FFFFFF"}}));
        s.targets.push_back(makeTarget(key(Kind::Tank), {{"fill", "#000000"}, {"fillColor", "#FFFFFF"}, {"colorFault", "#FF3B30"},
                                                         {"stroke", "#FFFFFF"}, {"textColor", "#FFFFFF"}}));
        s.targets.push_back(makeTarget(key(Kind::Pipe), {{"fill", "#3A3A3A"}, {"fluidColor", "#FFFFFF"}, {"stroke", "#FFFFFF"}}));
        s.targets.push_back(makeTarget(key(Kind::Bargraph), {{"zones", "0-80 = #FFFFFF; 80-100 = #FFD500"}, {"setpointColor", "#FFD500"},
                                                             {"textColor", "#FFFFFF"}}));
        out.push_back(std::move(s));
    }
    for (auto& s : out) s.builtin = true;
    return out;
}

} // namespace

// ================================================================== les styles fournis ===
const std::vector<Style>& builtins() {
    static const std::vector<Style> all = makeBuiltins();
    return all;
}

const Style* builtin(std::string_view name) noexcept {
    const std::string n = trim(name);
    for (const auto& s : builtins())
        if (sameName(s.name, n)) return &s;
    return nullptr;
}

const Style* find(const Project& p, std::string_view name) noexcept {
    const std::string n = roles::nameOf(name);
    if (n.empty()) return nullptr;
    if (const Style* mine = p.styleByName(n)) return mine;
    return builtin(n);
}

std::vector<const Style*> all(const Project& p) {
    std::vector<const Style*> out;
    for (const auto& s : p.styles) out.push_back(&s);
    for (const auto& s : builtins())
        if (!p.styleByName(s.name)) out.push_back(&s);
    return out;
}

// ======================================================================= les types ===
std::string typeOf(const Object& o) {
    if (o.kind == Kind::SymbolInstance) {
        const std::string sym = roles::nameOf(o.text("symbol"));
        if (!sym.empty()) return std::string(kSymbolPrefix) + sym;
    }
    return std::string(kKindInfos[static_cast<std::size_t>(o.kind)].key);
}

bool isSymbolType(std::string_view type) noexcept { return type.substr(0, kSymbolPrefix.size()) == kSymbolPrefix; }

std::string symbolName(std::string_view type) {
    return isSymbolType(type) ? std::string(type.substr(kSymbolPrefix.size())) : std::string{};
}

std::string typeLabel(std::string_view type) {
    if (type == kAnyType) return "Tous les objets";
    if (isSymbolType(type)) return "Symbole " + symbolName(type);
    if (const auto k = kindFromKey(type)) return std::string(kindLabel(*k));
    return std::string(type);
}

std::vector<std::string> availableTypes(const Project& p) {
    std::vector<std::string> out;
    for (const auto& info : kKindInfos) {
        if (info.kind == Kind::Group || info.kind == Kind::SymbolInstance) continue;
        out.emplace_back(info.key);
    }
    for (const View* sym : symbolsOf(p)) out.push_back(std::string(kSymbolPrefix) + sym->name);
    return out;
}

bool knownType(const Project& p, std::string_view type) {
    if (type == kAnyType) return true;
    if (isSymbolType(type)) {
        const View* v = p.viewByName(symbolName(type));
        return v && isSymbolView(*v);
    }
    return kindFromKey(type).has_value();
}

std::string propertyLabel(std::string_view key) {
    static const std::map<std::string, std::string, std::less<>> labels = {
        {"fill", "Remplissage"}, {"stroke", "Contour"}, {"strokeWidth", "\xC3\x89paisseur du contour"},
        {"radius", "Rayon des coins"}, {"opacity", "Opacit\xC3\xA9"}, {"textColor", "Couleur du texte"},
        {"font", "Police"}, {"fontSize", "Taille du texte"}, {"align", "Alignement"},
        {"colorOn", "Couleur en marche"}, {"colorOff", "Couleur \xC3\xA0 l'arr\xC3\xAAt"}, {"colorFault", "Couleur en d\xC3\xA9" "faut"},
        {"background", "Fond"}, {"fillColor", "Couleur du contenu"}, {"fluidColor", "Couleur du fluide"},
        {"colorWarning", "Couleur d'avertissement"}, {"colorAlarm", "Couleur d'alarme"}, {"zones", "Zones"},
        {"setpointColor", "Couleur de la consigne"}, {"text", "Texte"}, {"label", "Libell\xC3\xA9"},
        {"w", "Largeur"}, {"h", "Hauteur"}, {"x", "X"}, {"y", "Y"}, {"rot", "Rotation"}, {"format", "Format"},
        {"unit", "Unit\xC3\xA9"}, {"shape", "Forme"}, {"accent", "Accent"}, {"gasColor", "Couleur du gaz"}};
    if (key.substr(0, kParamPrefix.size()) == kParamPrefix) return "Param\xC3\xA8tre " + std::string(key.substr(kParamPrefix.size()));
    if (const auto it = labels.find(key); it != labels.end()) return it->second;
    return std::string(key);
}

std::vector<PropChoice> propertiesOf(const Project& p, std::string_view type) {
    std::vector<PropChoice> out;
    if (type == kAnyType) {
        for (const auto& k : design::styleKeys()) out.push_back({k, propertyLabel(k), {}});
        return out;
    }
    std::optional<Kind> kind = isSymbolType(type) ? std::optional<Kind>(Kind::SymbolInstance) : kindFromKey(type);
    if (!kind) return out;
    const Object o = makeObject(*kind, 1, {}, 0, 0, kNoId);
    for (const auto& pr : o.props)
        if (styleable(pr.key)) out.push_back({pr.key, propertyLabel(pr.key), pr.value});
    if (isSymbolType(type))
        if (const View* sym = p.viewByName(symbolName(type)); sym && isSymbolView(*sym))
            for (const auto& prm : sym->params)
                out.push_back({std::string(kParamPrefix) + prm.name, propertyLabel(std::string(kParamPrefix) + prm.name), prm.defaultValue});
    return out;
}

// ================================================================ ce qu'un style impose ===
const StyleTarget* target(const Style& s, std::string_view type) noexcept {
    for (const auto& t : s.targets)
        if (t.type == type || (isSymbolType(type) && isSymbolType(t.type) && sameName(t.type, type))) return &t;
    return nullptr;
}

StyleTarget& targetFor(Style& s, std::string_view type) {
    for (auto& t : s.targets)
        if (t.type == type) return t;
    s.targets.push_back({std::string(type), {}});
    return s.targets.back();
}

std::optional<std::string> valueOf(const Project& p, const Object& o, std::string_view key) {
    if (key.substr(0, kParamPrefix.size()) == kParamPrefix) {
        const View* sym = symbolViewOf(p, o);
        if (!sym) return std::nullopt;
        const auto at = paramIndex(*sym, key.substr(kParamPrefix.size()));
        if (!at) return std::nullopt;
        const auto given = givenArguments(*sym, o.text("params"));
        return *at < given.size() ? given[*at] : std::string{};
    }
    if (const Prop* pr = o.find(key)) return pr->value;
    return std::nullopt;
}

bool drivenByExpression(const Object& o, std::string_view key) {
    const Prop* pr = o.find(key);
    return pr && !pr->expr.empty();
}

std::vector<Prop> valuesFor(const Project& p, const Style& s, const Object& o) {
    std::vector<Prop> merged;
    std::set<std::string> seen;
    if (const StyleTarget* t = target(s, typeOf(o))) collect(t->props, merged, seen);
    collect(s.props, merged, seen);
    std::vector<Prop> out;
    for (auto& pr : merged)
        if (styleable(pr.key) && valueOf(p, o, pr.key)) out.push_back(std::move(pr));
    return out;
}

// ======================================================================= l'etat d'un objet ===
std::string citedStyle(const Object& o) {
    const Prop* pr = o.find("namedStyle");
    return pr ? roles::nameOf(pr->value) : std::string{};
}

std::vector<std::string> brokenKeys(const Object& o) {
    const Prop* pr = o.find(kBrokenKey);
    return pr ? splitComma(pr->value) : std::vector<std::string>{};
}

std::optional<std::string> valueBefore(const Object& o, std::string_view key) {
    const auto was = readWas(o);
    if (const auto it = was.find(std::string(key)); it != was.end()) return it->second;
    return std::nullopt;
}

bool imposes(const Project& p, const Object& o, std::string_view key, const Style** by, std::string* value) {
    const std::string name = citedStyle(o);
    if (name.empty() || drivenByExpression(o, key)) return false;
    const Style* s = find(p, name);
    if (!s) return false;
    const auto broken = brokenKeys(o);
    if (std::find(broken.begin(), broken.end(), key) != broken.end()) return false;
    for (const auto& pr : valuesFor(p, *s, o))
        if (pr.key == key) {
            if (by) *by = s;
            if (value) *value = pr.value;
            return true;
        }
    return false;
}

std::vector<std::string> imposedKeys(const Project& p, const Object& o) {
    std::vector<std::string> out;
    const std::string name = citedStyle(o);
    const Style* s = name.empty() ? nullptr : find(p, name);
    if (!s) return out;
    const auto broken = brokenKeys(o);
    for (const auto& pr : valuesFor(p, *s, o))
        if (!drivenByExpression(o, pr.key) && std::find(broken.begin(), broken.end(), pr.key) == broken.end()) out.push_back(pr.key);
    return out;
}

// ======================================================================= les gestes ===
std::size_t apply(const Project& p, Object& o, const Style& s) {
    // Un autre style avant : ses valeurs d'avant restent les vraies valeurs d'avant (on ne retient pas celles qu'il avait
    // imposees) ; ses cles brisees sont oubliees (ce style-ci impose tout).
    auto was = citedStyle(o).empty() ? std::map<std::string, std::string>{} : readWas(o);
    std::size_t changed = 0;
    for (const auto& pr : valuesFor(p, s, o)) {
        if (drivenByExpression(o, pr.key)) continue;
        const auto now = valueOf(p, o, pr.key);
        if (!now) continue;
        was.emplace(pr.key, *now);
        if (*now != pr.value && writeValue(p, o, pr.key, pr.value)) ++changed;
    }
    writeWas(o, was);
    writeBroken(o, {});
    o.set("namedStyle", s.name);
    return changed;
}

void detach(Object& o) {
    std::erase_if(o.props, [](const Prop& pr) { return pr.key == kBrokenKey || pr.key == kWasKey; });
    if (Prop* cite = o.find("namedStyle")) cite->value.clear();
}

bool breakKey(const Project& p, Object& o, std::string_view key, bool keepCurrent) {
    if (!imposes(p, o, key)) return false;
    if (!keepCurrent)
        if (const auto before = valueBefore(o, key)) (void)writeValue(p, o, key, *before);
    auto broken = brokenKeys(o);
    broken.emplace_back(key);
    writeBroken(o, broken);
    return true;
}

bool restoreKey(const Project& p, Object& o, std::string_view key) {
    auto broken = brokenKeys(o);
    const auto it = std::find(broken.begin(), broken.end(), key);
    if (it == broken.end()) return false;
    const Style* s = find(p, citedStyle(o));
    if (!s) return false;
    for (const auto& pr : valuesFor(p, *s, o))
        if (pr.key == key) {
            broken.erase(it);
            writeBroken(o, broken);
            auto was = readWas(o);
            if (const auto now = valueOf(p, o, key); now && !was.count(pr.key)) {
                was[pr.key] = *now;
                writeWas(o, was);
            }
            (void)writeValue(p, o, key, pr.value);
            return true;
        }
    return false;
}

bool edited(const Project& p, Object& o, std::string_view key) {
    // La saisie est deja ecrite : la cle est imposee si l'objet n'a pas encore ete marque. On la brise en gardant la saisie.
    const std::string name = citedStyle(o);
    if (name.empty()) return false;
    const Style* s = find(p, name);
    if (!s) return false;
    auto broken = brokenKeys(o);
    if (std::find(broken.begin(), broken.end(), key) != broken.end()) return false;
    for (const auto& pr : valuesFor(p, *s, o))
        if (pr.key == key) {
            if (const auto now = valueOf(p, o, key); now && *now == pr.value) return false;   // la meme valeur : toujours le style
            broken.emplace_back(key);
            writeBroken(o, broken);
            return true;
        }
    return false;
}

std::size_t restoreAll(const Project& p, Object& o) {
    std::size_t n = 0;
    for (const auto& k : brokenKeys(o)) n += restoreKey(p, o, k) ? 1u : 0u;
    return n;
}

std::size_t propagate(Project& p, const Style& before, const Style& after) {
    std::size_t changed = 0;
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            if (!sameName(citedStyle(o), before.name)) continue;
            const Object old = o;
            auto broken = brokenKeys(o);
            auto was = readWas(o);
            std::map<std::string, std::string> oldValues;
            for (const auto& pr : valuesFor(p, before, o)) oldValues[pr.key] = pr.value;
            for (const auto& pr : valuesFor(p, after, o)) {
                if (drivenByExpression(o, pr.key)) continue;
                if (std::find(broken.begin(), broken.end(), pr.key) != broken.end()) continue;
                const auto now = valueOf(p, o, pr.key);
                if (!now) continue;
                const auto it = oldValues.find(pr.key);
                if (it != oldValues.end() && *now != it->second) {
                    broken.push_back(pr.key);       // l'objet l'a changee lui-meme : la sienne, brisee
                    continue;
                }
                was.emplace(pr.key, *now);          // une cle nouvellement portee : sa valeur d'avant
                if (*now != pr.value) (void)writeValue(p, o, pr.key, pr.value);
            }
            writeBroken(o, broken);
            writeWas(o, was);
            if (Prop* cite = o.find("namedStyle"); cite && cite->value != after.name) cite->value = after.name;
            changed += o == old ? 0u : 1u;
        }
    return changed;
}

std::size_t users(const Project& p, std::string_view name) {
    std::size_t n = 0;
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            if (const std::string c = citedStyle(o); !c.empty() && sameName(c, name)) ++n;
    return n;
}

std::size_t forget(Project& p, std::string_view name) {
    std::size_t n = 0;
    for (auto& v : p.views)
        for (auto& o : v.objects)
            if (const std::string c = citedStyle(o); !c.empty() && sameName(c, name)) {
                detach(o);
                ++n;
            }
    std::erase_if(p.styles, [&](const Style& s) { return sameName(s.name, name); });
    return n;
}

std::size_t adopt(Project& p) {
    std::size_t n = 0;
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            const std::string name = citedStyle(o);
            if (name.empty() || o.find(kWasKey) || o.find(kBrokenKey)) continue;
            const Style* s = find(p, name);
            if (!s) continue;
            const Object fresh = o.kind == Kind::SymbolInstance ? o : makeObject(o.kind, o.id, {}, 0, 0, o.layer);
            std::vector<std::string> broken;
            std::map<std::string, std::string> was;
            for (const auto& pr : valuesFor(p, *s, o)) {
                if (drivenByExpression(o, pr.key)) continue;
                const auto now = valueOf(p, o, pr.key);
                if (!now) continue;
                if (*now != pr.value) broken.push_back(pr.key);
                else if (const auto def = valueOf(p, fresh, pr.key)) was[pr.key] = *def;
            }
            writeBroken(o, broken);
            writeWas(o, was);
            ++n;
        }
    return n;
}

// ======================================================================= faire un style ===
bool nameFree(const Project& p, std::string_view name, Id except) {
    const std::string n = trim(name);
    if (n.empty() || builtin(n)) return false;
    const Style* mine = p.styleByName(n);
    return !mine || mine->id == except;
}

namespace {
std::string freeName(const Project& p, std::string wanted) {
    if (wanted.empty()) wanted = "Style";
    if (nameFree(p, wanted)) return wanted;
    for (int k = 2; k < 1000; ++k) {
        std::string candidate = wanted + " " + std::to_string(k);
        if (nameFree(p, candidate)) return candidate;
    }
    return uniqueStyleName(p, wanted);
}
} // namespace

Style fromObject(Project& p, const Object& o, const std::string& name) {
    Style s;
    s.id = p.allocate();
    s.name = freeName(p, name.empty() ? std::string("Style") : name);
    StyleTarget& t = targetFor(s, typeOf(o));
    for (const auto& k : design::styleKeys())
        if (const Prop* pr = o.find(k); pr && pr->expr.empty()) t.props.push_back({k, pr->value, {}});
    s.description = "Fait de " + o.name + " (" + typeLabel(typeOf(o)) + ")";
    return s;
}

Style duplicate(Project& p, const Style& from, const std::string& name) {
    Style s = from;
    s.id = p.allocate();
    s.builtin = false;
    s.folder = from.builtin ? std::string{} : from.folder;
    s.name = freeName(p, name.empty() ? from.name + " (copie)" : name);
    if (from.builtin) s.description = "Copie de \xC2\xAB " + from.name + " \xC2\xBB (style fourni). " + from.description;
    return s;
}

// ======================================================================= verifier ===
std::vector<std::string> problems(const Project& p, const Style& s) {
    std::vector<std::string> out;
    for (const auto& t : s.targets) {
        if (!knownType(p, t.type)) {
            out.push_back("type inconnu : " + typeLabel(t.type) + (isSymbolType(t.type) ? " (symbole supprim\xC3\xA9 ?)" : ""));
            continue;
        }
        const auto choices = propertiesOf(p, t.type);
        for (const auto& pr : t.props) {
            const bool has = std::any_of(choices.begin(), choices.end(), [&](const PropChoice& c) { return c.key == pr.key; });
            if (!has) out.push_back(typeLabel(t.type) + " : la propri\xC3\xA9t\xC3\xA9 " + propertyLabel(pr.key) + " n'existe pas pour ce type");
        }
    }
    return out;
}

Object previewObject(const Project& p, const Style& s, std::string_view type, Id id) {
    std::optional<Kind> kind = isSymbolType(type) ? std::optional<Kind>(Kind::SymbolInstance) : kindFromKey(type);
    if (!kind) return {};
    Object o = makeObject(*kind, id, typeLabel(type), 0, 0, kNoId);
    if (isSymbolType(type)) {
        o.set("symbol", symbolName(type));
        if (const View* sym = p.viewByName(symbolName(type)); sym && isSymbolView(*sym)) {
            o.setNumber("w", sym->width);
            o.setNumber("h", sym->height);
        }
    }
    (void)apply(p, o, s);
    return o;
}

} // namespace hmi::styles
