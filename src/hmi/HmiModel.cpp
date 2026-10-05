#include "HmiModel.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace hmi {

namespace {


bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i]))
            != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

} // namespace

bool kindTakesText(Kind k) noexcept {
    return k == Kind::InputField || k == Kind::LoginPanel || k == Kind::PasswordChange;
}

bool kindWritesVariable(Kind k) noexcept {
    switch (k) {
        case Kind::PushButton: case Kind::Switch: case Kind::IlluminatedButton: case Kind::Selector: case Kind::Slider:
        case Kind::Knob: case Kind::ComboBox: case Kind::CheckBox: case Kind::RadioGroup: case Kind::DateTimePicker:
        case Kind::WeeklySchedule:
            return true;
        default:
            return false;
    }
}

bool kindShowsValue(Kind k) noexcept {
    switch (k) {
        case Kind::NumericDisplay: case Kind::MultiStateIndicator: case Kind::MultiStateText: case Kind::Bargraph:
        case Kind::Thermometer: case Kind::Dial: case Kind::SevenSegment: case Kind::TrendArrow:
            return true;
        default:
            return kindIsSynoptic(k);      // lot 10 : leur etat, leur niveau
    }
}


std::optional<Kind> kindFromKey(std::string_view s) noexcept {
    for (const auto& k : kKindInfos)
        if (iequals(k.key, s)) return k.kind;
    return std::nullopt;
}

std::string_view scriptLangKey(ScriptLang l) noexcept {
    switch (l) {
        case ScriptLang::ST:  return "ST";
        case ScriptLang::C:   return "C";
        case ScriptLang::Cpp: return "C++";
    }
    return "ST";
}

std::optional<ScriptLang> scriptLangFromKey(std::string_view s) noexcept {
    if (iequals(s, "ST"))  return ScriptLang::ST;
    if (iequals(s, "C"))   return ScriptLang::C;
    if (iequals(s, "C++") || iequals(s, "CPP")) return ScriptLang::Cpp;
    return std::nullopt;
}

// ------------------------------------------------------------ nombres ------
std::string formatNumber(double v) {
    if (!std::isfinite(v)) return "0";
    const double r = std::round(v);
    if (std::fabs(v - r) < 1e-9 && std::fabs(r) < 9.0e15) {
        char b[32];
        std::snprintf(b, sizeof b, "%lld", static_cast<long long>(r));
        return b;
    }
    char b[64];
    std::snprintf(b, sizeof b, "%.3f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

bool parseNumber(std::string_view s, double& out) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.empty()) return false;
    std::string tmp(s);
    for (auto& c : tmp) if (c == ',') c = '.';      // "12,5" tape a la francaise
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parseBool(std::string_view s, bool fallback) noexcept {
    if (iequals(s, "TRUE") || iequals(s, "VRAI") || s == "1" || iequals(s, "OUI")) return true;
    if (iequals(s, "FALSE") || iequals(s, "FAUX") || s == "0" || iequals(s, "NON")) return false;
    return fallback;
}

// ------------------------------------------------------------- Box ---------
Box Box::united(const Box& o) const noexcept {
    const double l = std::min(x, o.x), t = std::min(y, o.y);
    const double r = std::max(right(), o.right()), b = std::max(bottom(), o.bottom());
    return {l, t, r - l, b - t};
}

// ------------------------------------------------------------- Object ------
const Prop* Object::find(std::string_view key) const noexcept {
    for (const auto& p : props) if (p.key == key) return &p;
    return nullptr;
}
Prop* Object::find(std::string_view key) noexcept {
    for (auto& p : props) if (p.key == key) return &p;
    return nullptr;
}
std::string Object::text(std::string_view key, std::string_view fallback) const {
    const auto* p = find(key);
    return p ? p->value : std::string(fallback);
}
double Object::number(std::string_view key, double fallback) const {
    const auto* p = find(key);
    double v = fallback;
    if (p && parseNumber(p->value, v)) return v;
    return fallback;
}
bool Object::flag(std::string_view key, bool fallback) const {
    const auto* p = find(key);
    return p ? parseBool(p->value, fallback) : fallback;
}
std::string Object::expr(std::string_view key) const {
    const auto* p = find(key);
    return p ? p->expr : std::string{};
}
void Object::set(std::string_view key, std::string value) {
    if (auto* p = find(key)) { p->value = std::move(value); return; }
    props.push_back(Prop{std::string(key), std::move(value), {}});
}
void Object::setNumber(std::string_view key, double v) { set(key, formatNumber(v)); }
void Object::setFlag(std::string_view key, bool v)     { set(key, v ? "TRUE" : "FALSE"); }
void Object::setExpr(std::string_view key, std::string expression) {
    if (auto* p = find(key)) { p->expr = std::move(expression); return; }
    props.push_back(Prop{std::string(key), {}, std::move(expression)});
}

Box Object::box() const {
    return {number("x"), number("y"), number("w"), number("h")};
}
void Object::setBox(const Box& b) {
    setNumber("x", b.x);
    setNumber("y", b.y);
    setNumber("w", std::max(0.0, b.w));
    setNumber("h", std::max(0.0, b.h));
}

// --------------------------------------------------------------- View ------
const ViewParam* View::param(std::string_view wanted) const noexcept {
    for (const auto& p : params) if (iequals(p.name, wanted)) return &p;
    return nullptr;
}

Object* View::object(Id wanted) noexcept {
    for (auto& o : objects) if (o.id == wanted) return &o;
    return nullptr;
}
const Object* View::object(Id wanted) const noexcept {
    for (const auto& o : objects) if (o.id == wanted) return &o;
    return nullptr;
}
Object* View::objectByName(std::string_view n) noexcept {
    for (auto& o : objects) if (o.name == n) return &o;
    return nullptr;
}
const Object* View::objectByName(std::string_view n) const noexcept {
    for (const auto& o : objects) if (o.name == n) return &o;
    return nullptr;
}
Layer* View::layer(Id wanted) noexcept {
    for (auto& l : layers) if (l.id == wanted) return &l;
    return nullptr;
}
const Layer* View::layer(Id wanted) const noexcept {
    for (const auto& l : layers) if (l.id == wanted) return &l;
    return nullptr;
}
int View::layerRank(Id wanted) const noexcept {
    for (std::size_t i = 0; i < layers.size(); ++i) if (layers[i].id == wanted) return static_cast<int>(i);
    return -1;
}
int View::indexOf(Id wanted) const noexcept {
    for (std::size_t i = 0; i < objects.size(); ++i) if (objects[i].id == wanted) return static_cast<int>(i);
    return -1;
}
std::vector<Id> View::childrenOf(Id parent) const {
    std::vector<Id> out;
    for (const auto* o : paintOrder()) if (o->parent == parent) out.push_back(o->id);
    return out;
}
std::vector<Id> View::descendantsOf(Id parent) const {
    std::vector<Id> out;
    std::vector<Id> todo{parent};
    while (!todo.empty()) {
        const Id p = todo.back();
        todo.pop_back();
        for (const auto& o : objects)
            if (o.parent == p && o.id != p) { out.push_back(o.id); todo.push_back(o.id); }
    }
    return out;
}
std::vector<const Object*> View::paintOrder() const {
    std::vector<const Object*> out;
    out.reserve(objects.size());
    for (const auto& l : layers)
        for (const auto& o : objects) if (o.layer == l.id) out.push_back(&o);
    // Un objet dont le calque a disparu reste dessine, en dernier : le perdre
    // de vue serait la pire facon de signaler l'incoherence.
    for (const auto& o : objects) if (layerRank(o.layer) < 0) out.push_back(&o);
    return out;
}
bool View::effectivelyLocked(const Object& o) const noexcept {
    if (o.locked) return true;
    if (const auto* l = layer(o.layer); l && l->locked) return true;
    Id p = o.parent;
    for (int guard = 0; p != kNoId && guard < 64; ++guard) {
        const auto* g = object(p);
        if (!g) break;
        if (g->locked) return true;
        p = g->parent;
    }
    return false;
}
bool View::effectivelyHidden(const Object& o) const noexcept {
    if (o.hidden) return true;
    if (const auto* l = layer(o.layer); l && !l->visible) return true;
    // Lot 12 : un objet d'une autre page d'un conteneur a onglets (sa page :
    // "tabPage", 1 par defaut ; celle montree : "page" du conteneur), ou dans un
    // panneau replie, ne se voit pas - dans l'editeur comme en marche.
    const auto pageOf = [](const Object& x, std::string_view key) {
        return static_cast<long>(std::lround(std::max(1.0, x.number(key, 1))));
    };
    const Object* child = &o;
    Id p = o.parent;
    for (int guard = 0; p != kNoId && guard < 64; ++guard) {
        const auto* g = object(p);
        if (!g) break;
        if (g->hidden) return true;
        if (g->kind == Kind::TabContainer && pageOf(*child, "tabPage") != pageOf(*g, "page")) return true;
        if (g->kind == Kind::CollapsiblePanel && g->flag("collapsed")) return true;
        child = g;
        p = g->parent;
    }
    return false;
}

// ------------------------------------------------------------ Project -----
View* Project::view(Id wanted) noexcept {
    for (auto& v : views) if (v.id == wanted) return &v;
    return nullptr;
}
const View* Project::view(Id wanted) const noexcept {
    for (const auto& v : views) if (v.id == wanted) return &v;
    return nullptr;
}
View* Project::viewByName(std::string_view n) noexcept {
    for (auto& v : views) if (v.name == n) return &v;
    return nullptr;
}
const View* Project::viewByName(std::string_view n) const noexcept {
    for (const auto& v : views) if (v.name == n) return &v;
    return nullptr;
}

Resource* Project::resource(Id id) noexcept {
    for (auto& r : assets.resources) if (r.id == id) return &r;
    return nullptr;
}
const Resource* Project::resource(Id id) const noexcept {
    for (const auto& r : assets.resources) if (r.id == id) return &r;
    return nullptr;
}
const Resource* Project::resourceByName(std::string_view name) const noexcept {
    for (const auto& r : assets.resources) if (r.name == name) return &r;
    return nullptr;
}
ExternalFile* Project::externalFile(Id id) noexcept {
    for (auto& f : assets.files) if (f.id == id) return &f;
    return nullptr;
}
const ExternalFile* Project::externalFile(Id id) const noexcept {
    for (const auto& f : assets.files) if (f.id == id) return &f;
    return nullptr;
}
const ExternalFile* Project::externalByName(std::string_view name) const noexcept {
    for (const auto& f : assets.files) if (f.name == name) return &f;
    return nullptr;
}

std::string_view externalKindKey(ExternalKind k) noexcept {
    switch (k) {
        case ExternalKind::Excel:    return "Excel";
        case ExternalKind::Csv:      return "CSV";
        case ExternalKind::Text:     return "TXT";
        case ExternalKind::Json:     return "JSON";
        case ExternalKind::Xml:      return "XML";
        case ExternalKind::Sqlite:   return "SQLite";
        case ExternalKind::Database: return "Base externe";
        case ExternalKind::Document: return "Document";      // lot API 8
    }
    return "CSV";
}

std::optional<ExternalKind> externalKindFromKey(std::string_view k) noexcept {
    for (auto kind : {ExternalKind::Excel, ExternalKind::Csv, ExternalKind::Text, ExternalKind::Json, ExternalKind::Xml,
                      ExternalKind::Sqlite, ExternalKind::Database, ExternalKind::Document})   // lot API 8 : Document
        if (externalKindKey(kind) == k) return kind;
    return std::nullopt;
}

std::optional<ExternalKind> externalKindFromPath(std::string_view path) noexcept {
    const auto dot = path.rfind('.');
    if (path.empty()) return std::nullopt;
    if (dot == std::string_view::npos) return ExternalKind::Document;     // lot API 8 : sans extension, un document
    std::string ext(path.substr(dot + 1));
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "xlsx" || ext == "xlsm" || ext == "xls") return ExternalKind::Excel;
    if (ext == "csv" || ext == "tsv") return ExternalKind::Csv;       // lot API 8 : .tsv (le lecteur CSV lit la tabulation)
    if (ext == "txt" || ext == "log") return ExternalKind::Text;
    if (ext == "json") return ExternalKind::Json;
    if (ext == "xml") return ExternalKind::Xml;
    if (ext == "db" || ext == "sqlite" || ext == "sqlite3") return ExternalKind::Sqlite;
    return ExternalKind::Document;      // lot API 8 : tout autre fichier, un document
}

Stats Project::statistics() const {
    Stats s;
    s.views = views.size();
    s.resources = assets.resources.size();
    std::size_t bytes = sizeof(Project) + config.name.size() + config.description.size();
    for (const auto& r : assets.resources) bytes += sizeof(Resource) + static_cast<std::size_t>(r.data ? r.data->size() : 0);
    bytes += assets.files.size() * sizeof(ExternalFile);
    for (const auto& v : views) {
        bytes += sizeof(View) + v.name.size() + v.description.size();
        s.layers += v.layers.size();
        s.scripts += v.scripts.size();
        for (const auto& sc : v.scripts) bytes += sizeof(Script) + sc.body.size() + sc.name.size();
        s.actions += v.actions.size();
        bytes += v.actions.size() * sizeof(Action);
        for (const auto& o : v.objects) {
            ++s.objects;
            s.actions += o.actions.size();
            for (const auto& a : o.actions) bytes += sizeof(Action) + a.target.size() + a.value.size() + a.watch.size();
            if (o.kind == Kind::Group) ++s.groups;
            bytes += sizeof(Object) + o.name.size();
            for (const auto& p : o.props) {
                bytes += sizeof(Prop) + p.key.size() + p.value.size() + p.expr.size();
                if (!p.expr.empty()) ++s.expressions;
            }
        }
    }
    s.scripts += programs.scripts.size();
    for (const auto& sc : programs.scripts) bytes += sizeof(Script) + sc.body.size() + sc.name.size();
    bytes += programs.variables.size() * sizeof(Variable);
    s.alarms = alarms.size();
    for (const auto& a : alarms) bytes += sizeof(AlarmDef) + a.name.size() + a.condition.size() + a.message.size();
    s.recipes = recipes.size();
    for (const auto& r : recipes) {
        bytes += sizeof(Recipe) + r.fields.size() * sizeof(RecipeField);
        for (const auto& rec : r.records) {
            bytes += sizeof(RecipeRecord);
            for (const auto& v : rec.values) bytes += v.size();
        }
    }
    s.users = security.users.size();
    bytes += security.users.size() * sizeof(User) + security.groups.size() * sizeof(UserGroup);
    s.bytesInMemory = bytes;
    return s;
}

// ------------------------------------------------------------ fabriques ---
namespace {

void common(Object& o, double x, double y, double w, double h) {
    o.setNumber("x", x);
    o.setNumber("y", y);
    o.setNumber("w", w);
    o.setNumber("h", h);
    o.setNumber("rot", 0);
    o.setFlag("flipH", false);
    o.setFlag("flipV", false);
    o.setFlag("visible", true);
    o.setNumber("opacity", 100);
}

void textStyle(Object& o, std::string text, std::string color) {
    o.set("text", std::move(text));
    o.set("font", "Sans");
    o.setNumber("fontSize", 16);
    o.set("textColor", std::move(color));
    o.set("align", "centre");
    o.setFlag("wrap", false);
}

void communication(Object& o) {
    o.set("variable", "");
    o.setNumber("refresh", 500);
    o.setNumber("access", 0);
}

// Lot 10 : un symbole de synoptique - son etat ("value"), son defaut, ses
// couleurs (en marche, a l'arret, en defaut), son libelle, son animation.
void synoptic(Object& o, double x, double y, double w, double h, const char* value) {
    common(o, x, y, w, h);
    o.set("value", value);
    o.set("fault", "FALSE");
    o.set("colorOn", "#2ECC71");
    o.set("colorOff", "#5A6577");
    o.set("colorFault", "#E5534B");
    o.set("stroke", "#C8D0DC");
    o.setNumber("strokeWidth", 2);
    o.set("label", "");
    o.set("labelPosition", "dessous");
    o.setNumber("fontSize", 13);
    o.set("textColor", "#C8D0DC");
    o.setFlag("animate", true);
}
// ... un contenant : son niveau entre min et max, sa couleur, sa valeur ecrite.
void vessel(Object& o, const char* content, double max, const char* unit) {
    std::erase_if(o.props, [](const Prop& p) { return p.key == "colorOn" || p.key == "colorOff" || p.key == "animate"; });
    o.set("fill", "#1F252E");
    o.set("fillColor", content);
    o.setNumber("min", 0);
    o.setNumber("max", max);
    o.setFlag("showValue", true);
    o.set("unit", unit);
    o.set("format", "0");
    // Lot 11 : les seuils dessines sur le contenant (vides : aucun) - bas, haut,
    // et les alarmes tres basse et tres haute ; franchis, ils prennent leur couleur.
    o.set("lowAlarm", "");
    o.set("low", "");
    o.set("high", "");
    o.set("highAlarm", "");
    o.set("colorWarning", "#F2C94C");
    o.set("colorAlarm", "#E5534B");
}
// ... une image recolorable (lot 10) : une couleur remplace celles du SVG
// (toutes, les remplissages, les contours, ou une seule) ; un PNG en est teinte.
void recolorable(Object& o) {
    o.set("recolor", "");
    o.set("recolorMode", "tout");
    o.set("recolorFrom", "#000000");
}
void eraseKeys(Object& o, std::initializer_list<std::string_view> keys) {
    std::erase_if(o.props, [&](const Prop& p) { return std::find(keys.begin(), keys.end(), p.key) != keys.end(); });
}
// ... lot 11 : un graphique - son titre (texte a trous), son cadre.
void chartFrame(Object& o) {
    o.set("text", "");
    o.setNumber("fontSize", 12);
    o.set("textColor", "#9AA6B8");
    o.set("fill", "#1B2028");
    o.set("stroke", "#3A4556");
}

} // namespace

Object makeObject(Kind kind, Id id, std::string name, double x, double y, Id layer) {
    Object o;
    o.id = id;
    o.kind = kind;
    o.name = std::move(name);
    o.layer = layer;
    switch (kind) {
        case Kind::Text:
            common(o, x, y, 160, 32);
            textStyle(o, "Texte", "#E6EAF0");
            o.set("align", "gauche");
            o.set("fill", "");
            break;
        case Kind::Image:
            common(o, x, y, 128, 128);
            o.set("image", "");
            o.set("stretch", "ajuster");
            recolorable(o);
            break;
        case Kind::Button:
            common(o, x, y, 140, 44);
            textStyle(o, "Bouton", "#FFFFFF");
            o.set("fill", "#2F6FD6");
            o.set("stroke", "#1D4FA3");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            // Lot 9 : une commande sensible se confirme (un second clic, ou un appui maintenu).
            o.set("confirmMode", "aucune");
            o.setNumber("holdMs", 2000);
            break;
        case Kind::Rectangle:
            common(o, x, y, 160, 100);
            o.set("fill", "#3A4556");
            o.set("stroke", "#8A9BB0");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 0);
            break;
        case Kind::Ellipse:
            common(o, x, y, 120, 120);
            o.set("fill", "#3A4556");
            o.set("stroke", "#8A9BB0");
            o.setNumber("strokeWidth", 1);
            break;
        case Kind::Line:
            common(o, x, y, 160, 0);
            o.set("points", "0,0 160,0");
            o.set("stroke", "#D0D8E4");
            o.setNumber("strokeWidth", 2);
            break;
        case Kind::Polygon:
            common(o, x, y, 120, 100);
            o.set("points", "60,0 120,100 0,100");
            o.set("fill", "#3A4556");
            o.set("stroke", "#8A9BB0");
            o.setNumber("strokeWidth", 1);
            break;
        case Kind::Indicator:
            common(o, x, y, 32, 32);
            o.set("value", "FALSE");
            o.set("colorOn", "#2ECC71");
            o.set("colorOff", "#4A5261");
            o.set("stroke", "#1B1F26");
            o.setNumber("strokeWidth", 1);
            o.set("shape", "rond");
            break;
        case Kind::ProgressBar:
            common(o, x, y, 220, 24);
            o.set("value", "40");
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.set("fill", "#2F6FD6");
            o.set("background", "#2A313C");
            o.set("orientation", "horizontale");
            break;
        case Kind::Gauge:
            common(o, x, y, 180, 180);
            o.set("value", "40");
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.set("unit", "");
            o.set("fill", "#2F6FD6");
            o.set("background", "#2A313C");
            break;
        case Kind::Table:
            common(o, x, y, 420, 180);
            o.set("columns", "Nom;Valeur;Unit\xC3\xA9");
            o.setNumber("rows", 4);
            o.set("source", "");      // un fichier externe : le tableau montre ses lignes
            o.setNumber("fontSize", 14); // lot 6 : la taille du texte des cases (et la hauteur des lignes)
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            break;
        case Kind::History:
            common(o, x, y, 640, 240);
            o.set("source", "alarmes");
            o.set("group", "");
            o.setNumber("fontSize", 12);   // 1.11 (R111) : la taille du texte des lignes (l'en-tete et la hauteur des lignes suivent)
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            break;
        case Kind::Trend:
            common(o, x, y, 520, 260);
            o.set("variables", "");
            o.set("mode", "temps r\xC3\xA9" "el");
            o.set("source", "");
            o.setNumber("duration", 60);
            o.set("scale", "fixe");
            o.setNumber("ymin", 0);
            o.setNumber("ymax", 100);
            o.set("colors", "#4FA3FF;#F2994A;#2ECC71;#E5534B");
            o.set("interpolation", "lin\xC3\xA9" "aire");
            o.setFlag("legend", true);
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            break;
        case Kind::List:
            common(o, x, y, 200, 160);
            o.set("items", "\xC3\x89l\xC3\xA9ment 1;\xC3\x89l\xC3\xA9ment 2;\xC3\x89l\xC3\xA9ment 3");
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            break;
        case Kind::Video:
            common(o, x, y, 320, 180);
            o.set("video", "");
            o.set("poster", "");      // l'image d'attente (une ressource image)
            o.set("fill", "#101318");
            break;
        case Kind::Container:
            common(o, x, y, 400, 300);
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            o.setNumber("strokeWidth", 1);
            o.setFlag("clip", true);
            break;
        case Kind::Group:
            common(o, x, y, 0, 0);
            break;
        case Kind::RecipeManager:
            // Le gestionnaire de recettes : les jeux d'une recette en tableau
            // (une ligne par jeu, une colonne par element) et ses boutons.
            common(o, x, y, 560, 260);
            o.set("recipe", "");
            o.set("buttons", "Ajouter;Modifier;Supprimer;Appliquer;Lire");
            o.setNumber("fontSize", 13);
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            o.set("textColor", "#DDE3EA");
            break;
        case Kind::AnimatedImage:
            // L'image animee : des etats (une condition, une ou plusieurs images
            // qui defilent a leur periode), le premier vrai l'emporte ; sinon
            // l'image par defaut.
            common(o, x, y, 128, 128);
            o.set("states", "");
            o.set("image", "");
            o.setNumber("period", 500);
            o.set("stretch", "ajuster");
            recolorable(o);        // lot 10
            break;
        // ---- lot 8 ------------------------------------------------------------
        case Kind::InputField:
            // Le champ de saisie : un clic le prend, on tape, Entree ecrit la
            // variable (bornes verifiees), Echap annule.
            common(o, x, y, 180, 36);
            textStyle(o, "", "#E6EAF0");
            std::erase_if(o.props, [](const Prop& p) { return p.key == "text" || p.key == "wrap"; });
            o.set("align", "droite");
            o.set("mode", "num\xC3\xA9rique");
            o.set("format", "0.0");
            o.set("min", "");
            o.set("max", "");
            o.set("unit", "");
            o.setNumber("maxLength", 32);
            o.set("placeholder", "");
            o.set("keyboard", "aucun");
            o.setFlag("validateOnExit", false);
            o.set("fill", "#141820");
            o.set("stroke", "#4A5568");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 3);
            break;
        case Kind::LoginPanel:
            common(o, x, y, 340, 236);
            textStyle(o, "Connexion", "#E6EAF0");
            std::erase_if(o.props, [](const Prop& p) { return p.key == "align" || p.key == "wrap"; });   // un panneau
            o.setNumber("fontSize", 15);
            o.setFlag("userList", true);
            o.set("buttonText", "Se connecter");
            o.set("afterLogin", "");
            o.set("keyboard", "aucun");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            o.set("accent", "#2F6FD6");
            o.setNumber("radius", 6);
            break;
        // ---- lot 10 : les symboles de synoptique. "value" : l'etat (marche,
        // ouvert, sous tension) ou le niveau ; sans expression, la variable
        // (Communication > Variable API) ; "fault" : le defaut.
        case Kind::Valve:
            synoptic(o, x, y, 80, 64, "FALSE");
            eraseKeys(o, {"animate"});
            o.set("valveType", "manuelle");
            // Lot 11 : la vanne reglante - son ouverture (0 a 100 %, vide : tout
            // ou rien) et son mouvement (vrai : elle bouge, la tige clignote).
            o.set("opening", "");
            o.set("moving", "");
            break;
        case Kind::Pump:
            synoptic(o, x, y, 90, 80, "FALSE");
            break;
        case Kind::Motor:
            synoptic(o, x, y, 90, 70, "FALSE");
            break;
        case Kind::Pipe:
            synoptic(o, x, y, 200, 24, "FALSE");
            eraseKeys(o, {"colorOn", "colorOff"});
            o.set("pipeShape", "droit");
            o.setNumber("thickness", 14);
            o.set("fill", "#4A5568");
            o.set("fluidColor", "#4FA3FF");
            o.setNumber("speed", 60);
            break;
        case Kind::Tank:
            synoptic(o, x, y, 120, 160, "60");
            vessel(o, "#3C8DDB", 100, "%");
            break;
        case Kind::GasBottle:
            synoptic(o, x, y, 56, 150, "150");
            vessel(o, "#8FB3D9", 200, "bar");
            o.set("gasColor", "#1E6B3A");
            break;
        case Kind::Fan:
            synoptic(o, x, y, 80, 80, "FALSE");
            break;
        case Kind::Compressor:
            synoptic(o, x, y, 90, 80, "FALSE");
            break;
        case Kind::HeatExchanger:
            synoptic(o, x, y, 90, 90, "FALSE");
            break;
        case Kind::Filter:
            synoptic(o, x, y, 70, 80, "TRUE");
            eraseKeys(o, {"animate"});
            break;
        case Kind::Boiler:
            synoptic(o, x, y, 100, 130, "FALSE");
            break;
        case Kind::Conveyor:
            synoptic(o, x, y, 240, 56, "FALSE");
            o.setNumber("speed", 40);
            break;
        case Kind::Cylinder:
            synoptic(o, x, y, 190, 50, "0");
            eraseKeys(o, {"animate"});
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            break;
        case Kind::IsaInstrument:
            synoptic(o, x, y, 76, 100, "0");
            eraseKeys(o, {"colorOn", "colorOff", "animate"});
            o.set("fill", "#1F252E");
            o.set("function", "PT");
            o.set("loop", "101");
            o.set("mounting", "terrain");
            o.setFlag("showValue", true);
            o.set("unit", "bar");
            o.set("format", "0.0");
            break;
        case Kind::CircuitBreaker:
            synoptic(o, x, y, 50, 90, "TRUE");
            eraseKeys(o, {"animate"});
            break;
        case Kind::Disconnector:
            synoptic(o, x, y, 50, 90, "TRUE");
            eraseKeys(o, {"animate"});
            break;
        case Kind::Contactor:
            synoptic(o, x, y, 70, 90, "FALSE");
            eraseKeys(o, {"animate"});
            break;
        case Kind::Lamp:
            synoptic(o, x, y, 50, 50, "FALSE");
            eraseKeys(o, {"animate"});
            o.set("colorOn", "#F2C94C");
            break;
        case Kind::Transformer:
            synoptic(o, x, y, 60, 100, "TRUE");
            eraseKeys(o, {"animate"});
            break;
        case Kind::Silo:
            synoptic(o, x, y, 110, 180, "70");
            vessel(o, "#D9B26F", 100, "%");
            break;
        case Kind::Hopper:
            synoptic(o, x, y, 120, 110, "40");
            vessel(o, "#C9A25F", 100, "%");
            break;
        case Kind::Mixer:
            synoptic(o, x, y, 110, 150, "FALSE");
            o.set("fill", "#1F252E");
            o.set("fillColor", "#3C8DDB");
            break;
        case Kind::CheckValve:
            synoptic(o, x, y, 70, 44, "TRUE");
            eraseKeys(o, {"animate"});
            break;
        case Kind::ThreeWayValve:
            // 1.10.4 : la vanne 3 voies. "value" : la voie active - 0 fermee, 1 (ou
            // 12) la voie 1-2, 2 (ou 13) la voie 1-3, 3 (ou 23) la voie 2-3 ; ou,
            // "positionMode" = "deux bool\xC3\xA9" "ens", A + 2 x B ("positionA",
            // "positionB"). "valve3Function" : melangeuse (1 et 2 entrent, 3 sort)
            // ou repartitrice (3 entre, 1 et 2 sortent) ; "valve3Bore" : le
            // boisseau en T ou en L (1-3 et 2-3). Le defaut, le mouvement,
            // l'actionneur et le libelle comme la vanne. Comme la maquette 1.10.4 :
            // posee en 1-3 (valable en T et en L), le passage ambre en mouvement.
            synoptic(o, x, y, 90, 90, "2");
            eraseKeys(o, {"animate"});
            o.set("colorMoving", "#F0B429");
            o.set("valve3Function", "m\xC3\xA9langeuse");
            o.set("valve3Bore", "T");
            o.set("positionMode", "entier");
            o.set("positionA", "");
            o.set("positionB", "");
            o.set("valveType", "motoris\xC3\xA9" "e");
            o.set("moving", "");
            o.setFlag("showPorts", true);
            break;
        case Kind::FlowArrow:
            synoptic(o, x, y, 90, 36, "FALSE");
            o.set("colorOn", "#4FA3FF");
            break;
        case Kind::SymbolInstance:
            // Lot 10 : l'instance d'un symbole - son symbole ("symbol", une vue de
            // role symbole) et ses arguments ("params"). placeSymbol() et "Creer
            // un symbole" lui donnent la taille du symbole.
            common(o, x, y, 160, 100);
            o.set("symbol", "");
            o.set("params", "");
            break;
        // ---- lot 11 : les graphiques ---------------------------------------------------------
        //  Une liste d'expressions ("variables" : a;b;c), leurs noms ("names") et
        //  leurs couleurs ("colors") ; le titre ("text", a trous) en haut.
        case Kind::BarChart:
            common(o, x, y, 420, 260);
            chartFrame(o);
            o.set("variables", "");
            o.set("names", "");
            o.set("colors", "#4FA3FF;#F2994A;#2ECC71;#E5534B;#B98CFF;#F1C40F");
            o.set("orientation", "verticale");
            o.set("scale", "fixe");
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.set("format", "0.0");
            o.set("unit", "");
            o.setFlag("showValue", true);
            o.set("low", "");
            o.set("high", "");
            o.set("colorAlarm", "#E5534B");
            break;
        case Kind::XYChart:
            common(o, x, y, 440, 300);
            chartFrame(o);
            o.set("xVariable", "");
            o.set("variables", "");
            o.set("names", "");
            o.set("colors", "#4FA3FF;#F2994A;#2ECC71;#E5534B");
            o.setNumber("xmin", 0);
            o.setNumber("xmax", 100);
            o.setNumber("ymin", 0);
            o.setNumber("ymax", 100);
            o.set("scale", "fixe");
            o.set("xLabel", "");
            o.set("yLabel", "");
            o.set("reference", "");
            o.set("referenceColor", "#9AA6B8");
            o.setNumber("tolerance", 0);
            o.setNumber("maxPoints", 300);
            o.set("interpolation", "lin\xC3\xA9" "aire");
            o.set("reset", "");
            o.setFlag("legend", true);
            break;
        case Kind::StateChart:
            common(o, x, y, 560, 200);
            chartFrame(o);
            o.set("variables", "");
            o.set("names", "");
            o.setNumber("duration", 60);
            o.set("stateList", "0 = Arr\xC3\xAAt | #4A5261; 1 = Marche | #2ECC71");
            o.setFlag("showText", true);
            o.setFlag("legend", true);
            break;
        case Kind::PieChart:
            common(o, x, y, 320, 260);
            chartFrame(o);
            o.set("variables", "");
            o.set("names", "");
            o.set("colors", "#4FA3FF;#F2994A;#2ECC71;#E5534B;#B98CFF;#F1C40F");
            o.setNumber("hole", 0);
            o.set("labelMode", "pourcentage");
            o.set("format", "0");
            o.set("unit", "");
            o.setFlag("legend", true);
            break;
        case Kind::RadarChart:
            common(o, x, y, 340, 300);
            chartFrame(o);
            o.set("variables", "");
            o.set("names", "");
            o.set("colors", "#4FA3FF");
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.set("references", "");
            o.set("referenceColor", "#F2C94C");
            o.setNumber("rings", 4);
            o.setFlag("showValue", false);
            break;
        case Kind::Histogram:
            common(o, x, y, 440, 260);
            chartFrame(o);
            o.set("variable", "");
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.setNumber("bins", 10);
            o.setNumber("window", 600);
            o.setNumber("samplePeriod", 1000);
            o.setNumber("maxSamples", 5000);
            o.set("low", "");
            o.set("high", "");
            o.set("colors", "#4FA3FF");
            o.setFlag("showStats", true);
            o.set("format", "0.0");
            break;
        // ---- lot 11 : les objets des alarmes -------------------------------------------------
        case Kind::AlarmBanner:
            // La plus grave (ou la plus recente, ou chacune a son tour), son heure,
            // son message ; "+2" les autres ; Acquitter a droite.
            common(o, x, y, 900, 48);
            o.set("group", "");
            o.set("show", "la plus grave");
            o.setNumber("period", 4000);
            o.setFlag("ackButton", true);
            o.setFlag("showCount", true);
            o.setFlag("blinkUnacked", true);
            o.set("empty", "Aucune alarme en cours");
            o.setNumber("fontSize", 15);
            o.set("textColor", "#E6EAF0");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            break;
        case Kind::AlarmCounter:
            common(o, x, y, 150, 60);
            o.set("group", "");
            o.set("count", "\xC3\xA0 acquitter");
            o.set("label", "Alarmes");
            o.setFlag("icon", true);
            o.setFlag("blinkUnacked", true);
            o.setNumber("fontSize", 22);
            o.set("textColor", "#FFFFFF");
            o.set("fill", "#2A313C");
            o.set("stroke", "#3A4556");
            break;
        case Kind::AlarmSummary:
            // Une tuile par zone (groupe d'alarmes) : sa couleur est celle de sa
            // priorite la plus forte ; un clic la choisit.
            common(o, x, y, 540, 200);
            o.set("groups", "");
            o.setNumber("perRow", 3);
            o.set("variable", "");
            o.setFlag("blinkUnacked", true);
            o.set("colorOk", "#2E6B45");
            o.setNumber("fontSize", 14);
            o.set("textColor", "#FFFFFF");
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            break;
        case Kind::AlarmInstruction:
            common(o, x, y, 440, 220);
            o.set("alarm", "");
            o.setFlag("showMessage", true);
            o.set("empty", "Aucune alarme choisie : cliquer une alarme (bandeau, liste, r\xC3\xA9sum\xC3\xA9)");
            o.setNumber("fontSize", 14);
            o.set("textColor", "#E6EAF0");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            break;
        case Kind::AlarmStats:
            common(o, x, y, 540, 240);
            o.set("range", "depuis le lancement");
            o.setNumber("top", 5);
            o.set("sort", "nombre");
            o.set("group", "");
            o.set("colors", "#F2994A");
            o.setNumber("fontSize", 13);
            o.set("textColor", "#DDE3EA");
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            break;
        // ---- lot 11 : la production ---------------------------------------------------------
        case Kind::ProductionCounter:
            // Bons, rebuts, cadence, TRS (disponibilite x performance x qualite),
            // remis a zero a chaque debut de poste ("shifts").
            common(o, x, y, 560, 220);
            chartFrame(o);
            o.set("text", "Production");
            o.setNumber("fontSize", 14);
            o.set("good", "");
            o.set("bad", "");
            o.set("running", "");
            o.setNumber("idealRate", 600);
            o.setNumber("target", 0);
            o.set("shifts", "06:00;14:00;22:00");
            o.setNumber("rateWindow", 300);
            o.set("low", "60");
            o.set("high", "85");
            o.setFlag("showReset", true);
            break;
        case Kind::VariableTable:
            common(o, x, y, 480, 220);
            o.set("variables", "");
            o.set("names", "");
            o.set("units", "");
            o.setFlag("writable", true);
            o.set("format", "0.00");
            o.set("keyboard", "aucun");
            o.setNumber("fontSize", 13);
            o.set("textColor", "#DDE3EA");
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            break;
        case Kind::RecipeEditor:
            common(o, x, y, 620, 300);
            o.set("recipe", "");
            o.set("buttons", "Enregistrer;Appliquer;Lire;Annuler;Nouveau");
            o.setFlag("compare", true);
            o.set("keyboard", "aucun");
            o.setNumber("fontSize", 13);
            o.set("textColor", "#DDE3EA");
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            break;
        case Kind::ExportButton:
            common(o, x, y, 190, 44);
            textStyle(o, "Exporter", "#FFFFFF");
            o.set("exportSource", "alarmes");
            o.set("fileFormat", "CSV");
            o.set("fileName", "export_{SYS.Date}");
            o.setFlag("askWhere", true);         // Lot API 8 : demander ou enregistrer (absent : oui)
            o.setFlag("icon", true);
            o.set("fill", "#2F6FD6");
            o.set("stroke", "#1D4FA3");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            break;
        // ---- lot 12 : la navigation et la structure --------------------------------------------
        case Kind::NavBar:
            // Un bouton par vue ("views" ; vide : les vues ordinaires du projet), la
            // vue courante en surbrillance ; Precedent / Suivant en tete.
            common(o, x, y, 900, 48);
            o.set("views", "");
            o.set("labels", "");
            o.set("orientation", "horizontale");
            o.set("tabStyle", "onglets");
            o.setFlag("backForward", false);
            o.set("transition", "Instantan\xC3\xA9" "e");
            o.setNumber("gap", 4);
            o.setNumber("fontSize", 15);
            o.set("textColor", "#C8D0DC");
            o.set("activeTextColor", "#FFFFFF");
            o.set("buttonColor", "#2A313C");
            o.set("activeColor", "#2F6FD6");
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            break;
        case Kind::Breadcrumb:
            // Le chemin jusqu'a la vue courante : Accueil > Ligne 1 > Pompe 3.
            common(o, x, y, 700, 36);
            o.set("trail", "hi\xC3\xA9rarchie");
            o.set("separator", "\xE2\x80\xBA");
            o.setNumber("maxItems", 6);
            o.setFlag("home", true);
            o.setNumber("fontSize", 15);
            o.set("textColor", "#FFFFFF");
            o.set("linkColor", "#6FB1FF");
            o.set("fill", "");
            o.set("stroke", "");
            break;
        case Kind::TabContainer:
            // Des pages dans la meme zone : un onglet par page ("tabs"), la page
            // montree ("page", de 1 a N) ; chaque objet pose dedans porte sa page
            // ("tabPage").
            common(o, x, y, 520, 320);
            o.set("tabs", "Page 1;Page 2;Page 3");
            o.setNumber("page", 1);
            o.set("variable", "");
            o.set("tabPosition", "haut");
            o.setNumber("tabHeight", 34);
            o.setNumber("fontSize", 14);
            o.set("textColor", "#C8D0DC");
            o.set("activeTextColor", "#FFFFFF");
            o.set("tabColor", "#232A34");
            o.set("activeColor", "#2F6FD6");
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            break;
        case Kind::Frame:
            // Un cadre qui regroupe : son titre dans la bordure, ou en bandeau.
            common(o, x, y, 360, 220);
            o.set("title", "Groupe");
            o.set("titleStyle", "bordure");
            o.set("titleAlign", "gauche");
            o.setNumber("fontSize", 14);
            o.set("titleColor", "#C8D0DC");
            o.set("titleFill", "#2F3B4C");
            o.set("fill", "");
            o.set("stroke", "#4A5568");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 6);
            o.setFlag("clip", false);
            break;
        case Kind::ScrollPanel:
            // Un contenu plus grand que le cadre : il defile (molette, barre). Taille
            // du contenu : 0 = celle de ses objets.
            common(o, x, y, 360, 260);
            o.set("scrollDirection", "verticale");
            o.setNumber("contentWidth", 0);
            o.setNumber("contentHeight", 0);
            o.setNumber("scrollX", 0);
            o.setNumber("scrollY", 0);
            o.setNumber("wheelStep", 40);
            o.setFlag("showScrollbar", true);
            o.set("barColor", "#5B6B82");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            o.setNumber("strokeWidth", 1);
            break;
        case Kind::CollapsiblePanel:
            // Un bandeau de titre et son contenu ; un clic sur le bandeau le replie.
            common(o, x, y, 360, 220);
            o.set("title", "D\xC3\xA9tails");
            o.setFlag("collapsed", false);
            o.set("variable", "");
            o.setFlag("pushBelow", true);
            o.setNumber("headerHeight", 34);
            o.setNumber("fontSize", 14);
            o.set("textColor", "#FFFFFF");
            o.set("headerColor", "#2F3B4C");
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            break;
        case Kind::ZoneMap:
            // Un plan (une image, ou le fond) et ses zones : leur couleur suit leurs
            // alarmes, un clic choisit la zone (et ouvre sa vue).
            common(o, x, y, 640, 380);
            o.set("image", "");
            o.set("mapZones", "");
            o.set("variable", "");
            o.setFlag("showNames", true);
            o.setFlag("showCounts", true);
            o.setFlag("blinkUnacked", true);
            o.set("colorOk", "#2E6B45");
            o.set("selectedColor", "#FFFFFF");
            o.setNumber("fontSize", 13);
            o.set("textColor", "#FFFFFF");
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            break;
        case Kind::SystemButton:
            // Lot 10 : un clic ouvre le menu natif Parametres systeme, sur son
            // onglet ("tab") - sans action a ecrire.
            common(o, x, y, 200, 44);
            textStyle(o, "Param\xC3\xA8tres", "#FFFFFF");
            o.set("tab", "R\xC3\xA9glages");
            o.setFlag("icon", true);
            o.set("fill", "#3A4556");
            o.set("stroke", "#5B6B82");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            break;
        case Kind::LoginMenuButton:
            // Lot 12 : un clic ouvre le menu natif de connexion, sur son onglet
            // ("tab") - sans action a ecrire. "showUser" : l'utilisateur connecte
            // s'y ecrit (son nom, son niveau) ; personne : "text".
            common(o, x, y, 230, 44);
            textStyle(o, "Se connecter", "#FFFFFF");
            o.set("tab", "Connexion");
            o.setFlag("icon", true);
            o.setFlag("showUser", true);
            o.set("fill", "#2B4C6F");
            o.set("stroke", "#4F7AA8");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            break;
        case Kind::CommStatus:
            // Lot 14 : un voyant (vert : l'automate repond ; orange : connexion,
            // valeurs anciennes ; rouge : coupe ; bleu : le simulateur) et une ligne :
            // l'etat, l'adresse ("showAddress"), le temps de reponse ("showTime").
            // "compact" : le voyant et l'etat seuls.
            common(o, x, y, 420, 40);
            o.setFlag("showAddress", true);
            o.setFlag("showTime", true);
            o.setFlag("compact", false);
            o.setNumber("fontSize", 14);
            o.set("textColor", "#E6EAF0");
            o.set("colorGood", "#2ECC71");
            o.set("colorWarn", "#F2C94C");
            o.set("colorBad", "#E5534B");
            o.set("colorSim", "#56CCF2");
            o.set("fill", "#1E2530");
            o.set("stroke", "#3A4556");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            break;
        case Kind::PlcDiagnostic:
            // Lot 14 : la liaison avec l'automate en detail - adresse, etat et depuis
            // quand, identification, temps de reponse, requetes et erreurs, qualite
            // des variables ; les variables en defaut ("showBad") ; deux boutons
            // ("showButtons") : Reconnecter, Remettre a zero les compteurs.
            common(o, x, y, 600, 420);
            o.set("title", "Diagnostic automate");
            o.setFlag("showBad", true);
            o.setFlag("showButtons", true);
            o.setNumber("fontSize", 13);
            o.set("textColor", "#E6EAF0");
            o.set("labelColor", "#9AA6B8");
            o.set("headerColor", "#2F3B4C");
            o.set("buttonColor", "#2A313C");
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            break;
        case Kind::AnimatedGif:
            // Lot 16 : le GIF anime. "image" : une ressource GIF ; "play" : "en boucle",
            // "une fois", "N fois" ("count") ; "speed" : en % (100 : sa vitesse) ;
            // "start" : "a l'affichage", "sur action" (Jouer le GIF, IHM_GIF_JOUER),
            // "sur condition" (joue tant que "condition" est vraie) ; "end" : ce qui
            // reste a la fin - "derniere image", "premiere image", "cache".
            common(o, x, y, 128, 128);
            o.set("image", "");
            o.set("play", "en boucle");
            o.setNumber("count", 3);
            o.setNumber("speed", 100);
            o.set("start", "\xC3\xA0 l'affichage");
            o.set("condition", "");
            o.set("end", "derni\xC3\xA8re image");
            o.set("stretch", "ajuster");
            break;
        case Kind::ThemeSelector:
            // Lot 13 : deux boutons, Jour et Nuit ("themeLabels"), le theme en cours
            // en surbrillance ; un clic le change. "icon" : le soleil et la lune.
            common(o, x, y, 240, 44);
            o.set("themeLabels", "Jour;Nuit");
            o.setFlag("icon", true);
            o.set("orientation", "horizontale");
            o.setNumber("gap", 4);
            o.setNumber("fontSize", 15);
            o.set("textColor", "#C8D0DC");
            o.set("activeTextColor", "#FFFFFF");
            o.set("buttonColor", "#2A313C");
            o.set("activeColor", "#2F6FD6");
            o.set("fill", "");
            o.set("stroke", "#3A4556");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 5);
            break;
        case Kind::LanguageSelector:
            // Lot 13 : un bouton par langue ("languages" : lesquelles, vide : toutes
            // celles du projet), la langue en cours en surbrillance ; un clic la
            // change. "languageLabel" : code, nom, ou code et nom ; "languageStyle" :
            // boutons, ou bascule (un seul bouton - la langue en cours - qui passe a
            // la suivante).
            common(o, x, y, 360, 44);
            o.set("languages", "");
            o.set("languageStyle", "boutons");
            o.set("languageLabel", "code et nom");
            o.set("orientation", "horizontale");
            o.setNumber("gap", 4);
            o.setNumber("fontSize", 15);
            o.set("textColor", "#C8D0DC");
            o.set("activeTextColor", "#FFFFFF");
            o.set("buttonColor", "#2A313C");
            o.set("activeColor", "#2F6FD6");
            o.set("fill", "");
            o.set("stroke", "#3A4556");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 5);
            break;
        case Kind::LogoutButton:
            common(o, x, y, 190, 40);
            textStyle(o, "Se d\xC3\xA9" "connecter", "#FFFFFF");
            o.setFlag("showUser", true);
            o.setFlag("confirm", false);
            o.set("fill", "#8A3A3A");
            o.set("stroke", "#5E2626");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            break;
        case Kind::UserInfo:
            // Ce qu'il montre : un gabarit a lui ({nom}, {login}, {groupe},
            // {niveau}, {depuis}, {reste}), pas un texte a trous de variables.
            common(o, x, y, 300, 44);
            textStyle(o, "", "#E6EAF0");
            std::erase_if(o.props, [](const Prop& p) { return p.key == "text" || p.key == "wrap"; });
            o.set("display", "{nom}  ({groupe}, niveau {niveau})");
            o.set("align", "gauche");
            o.set("empty", "Aucun utilisateur connect\xC3\xA9");
            o.setFlag("icon", true);
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            o.setNumber("radius", 4);
            break;
        case Kind::PasswordChange:
            common(o, x, y, 340, 290);
            textStyle(o, "Changer le mot de passe", "#E6EAF0");
            std::erase_if(o.props, [](const Prop& p) { return p.key == "align" || p.key == "wrap"; });
            o.setNumber("fontSize", 15);
            o.setNumber("minLength", 6);
            o.setFlag("requireDigit", false);
            o.set("buttonText", "Changer");
            o.set("keyboard", "aucun");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            o.set("accent", "#2F6FD6");
            o.setNumber("radius", 6);
            break;
        case Kind::UserManager:
            // La gestion des utilisateurs : la liste (une ligne par utilisateur)
            // et ses boutons ; tout change le projet, sous la permission
            // Administrer.
            common(o, x, y, 640, 300);
            o.set("buttons", "Ajouter;Modifier;Supprimer;Activer;Mot de passe");
            o.setNumber("fontSize", 13);
            o.set("fill", "#262C36");
            o.set("stroke", "#3A4556");
            o.set("textColor", "#DDE3EA");
            break;
        // ---- lot 9 : les commandes ------------------------------------------------------
        //  "variable" : la variable ECRITE ; "state" (retour d'etat) : ce que l'objet
        //  montre, s'il differe de la commande (vide : la variable elle-meme).
        case Kind::PushButton:
            // Vrai tant qu'on appuie, faux au relachement : une marche par a-coups.
            common(o, x, y, 140, 44);
            textStyle(o, "Impulsion", "#FFFFFF");
            o.set("fill", "#2F6FD6");
            o.set("pressedColor", "#173F80");
            o.set("stroke", "#1D4FA3");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 4);
            o.setFlag("inverted", false);
            break;
        case Kind::Switch:
            // Un clic bascule la variable ; il montre son retour d'etat.
            common(o, x, y, 170, 40);
            o.set("textOn", "Marche");
            o.set("textOff", "Arr\xC3\xAAt");
            o.set("state", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 15);
            o.set("textColor", "#E6EAF0");
            o.set("colorOn", "#2ECC71");
            o.set("colorOff", "#4A5261");
            o.set("knobColor", "#F4F6FA");
            break;
        case Kind::IlluminatedButton:
            // Un bouton qui porte son voyant : la commande et l'etat au meme endroit.
            common(o, x, y, 150, 56);
            textStyle(o, "Marche", "#FFFFFF");
            o.set("operation", "basculer");
            o.set("lamp", "");
            o.set("colorOn", "#2ECC71");
            o.set("colorOff", "#3A4556");
            o.set("stroke", "#1B1F26");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 8);
            break;
        case Kind::Selector:
            // N positions (Manu / Arret / Auto) : rotatif, ou en boutons groupes.
            common(o, x, y, 240, 120);
            o.set("positions", "Manu;Arr\xC3\xAAt;Auto");
            o.set("values", "");
            o.set("style", "rotatif");
            o.set("state", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 14);
            o.set("textColor", "#E6EAF0");
            o.set("fill", "#2A313C");
            o.set("stroke", "#4A5568");
            o.set("accent", "#2F6FD6");
            break;
        case Kind::Slider:
            common(o, x, y, 260, 60);
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.setNumber("step", 1);
            o.set("orientation", "horizontale");
            o.set("format", "0");
            o.set("unit", "");
            o.setFlag("showValue", true);
            o.setNumber("ticks", 5);
            o.setFlag("continuous", false);
            o.set("state", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 13);
            o.set("textColor", "#E6EAF0");
            o.set("fill", "#2A313C");
            o.set("accent", "#2F6FD6");
            o.set("knobColor", "#F4F6FA");
            break;
        case Kind::Knob:
            common(o, x, y, 150, 150);
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.setNumber("step", 1);
            o.set("format", "0");
            o.set("unit", "");
            o.setFlag("showValue", true);
            o.setNumber("ticks", 10);
            o.setFlag("continuous", false);
            o.set("state", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 14);
            o.set("textColor", "#E6EAF0");
            o.set("fill", "#2A313C");
            o.set("accent", "#2F6FD6");
            o.set("knobColor", "#C8D0DC");
            break;
        case Kind::ComboBox:
            common(o, x, y, 210, 36);
            textStyle(o, "", "#E6EAF0");
            std::erase_if(o.props, [](const Prop& p) { return p.key == "text" || p.key == "wrap"; });
            o.set("align", "gauche");
            o.set("items", "Azote;Argon;H\xC3\xA9lium");
            o.set("values", "");
            o.set("placeholder", "(choisir)");
            o.setNumber("maxVisible", 6);
            o.set("state", "");
            o.set("fill", "#141820");
            o.set("stroke", "#4A5568");
            o.setNumber("strokeWidth", 1);
            o.setNumber("radius", 3);
            o.set("accent", "#2F6FD6");
            break;
        case Kind::CheckBox:
            common(o, x, y, 220, 32);
            textStyle(o, "Option", "#E6EAF0");
            std::erase_if(o.props, [](const Prop& p) { return p.key == "align" || p.key == "wrap"; });
            o.set("state", "");
            o.set("accent", "#2F6FD6");
            o.set("stroke", "#8A9BB0");
            break;
        case Kind::RadioGroup:
            common(o, x, y, 220, 100);
            o.set("items", "Petite vitesse;Grande vitesse;Arr\xC3\xAAt");
            o.set("values", "");
            o.set("orientation", "verticale");
            o.set("state", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 15);
            o.set("textColor", "#E6EAF0");
            o.set("accent", "#2F6FD6");
            o.set("stroke", "#8A9BB0");
            break;
        case Kind::DateTimePicker:
            // Jour, mois, annee, heure et minutes, chacun avec ses fleches ; Valider ecrit.
            common(o, x, y, 360, 96);
            o.set("fields", "date et heure");
            o.setFlag("seconds", false);
            o.set("font", "Sans");
            o.setNumber("fontSize", 16);
            o.set("textColor", "#E6EAF0");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            o.set("accent", "#2F6FD6");
            o.setNumber("radius", 6);
            break;
        case Kind::WeeklySchedule:
            // Les plages de la semaine : un clic allume ou eteint une case ; la
            // sortie est vraie pendant une plage.
            common(o, x, y, 580, 236);
            o.set("schedule", "Lu-Ve=07:00-12:00,13:30-17:00");
            o.set("resolution", "30 min");
            o.set("output", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 12);
            o.set("textColor", "#C8D0DC");
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            o.set("accent", "#2ECC71");
            o.set("nowColor", "#F2994A");
            break;
        // ---- lot 9 : les afficheurs -------------------------------------------------------
        //  "value" : ce qu'ils montrent (une expression) ; sans expression, la variable.
        case Kind::NumericDisplay:
            common(o, x, y, 180, 52);
            o.set("value", "0");
            o.set("format", "0.0");
            o.set("unit", "");
            o.set("label", "");
            o.set("lowAlarm", "");
            o.set("low", "");
            o.set("high", "");
            o.set("highAlarm", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 24);
            o.set("textColor", "#E6EAF0");
            o.set("align", "droite");
            o.set("colorWarning", "#F2C94C");
            o.set("colorAlarm", "#E5534B");
            o.set("fill", "#141820");
            o.set("stroke", "#3A4556");
            o.setNumber("radius", 4);
            break;
        case Kind::MultiStateIndicator:
            common(o, x, y, 170, 40);
            o.set("value", "0");
            o.set("stateList", "0 = Arr\xC3\xAAt | #4A5261; 1 = Marche | #2ECC71; 2 = D\xC3\xA9" "faut | #E5534B | clignote");
            o.set("shape", "rond");
            o.setFlag("showText", true);
            o.set("font", "Sans");
            o.setNumber("fontSize", 15);
            o.set("textColor", "#E6EAF0");
            o.set("colorOff", "#4A5261");
            o.set("stroke", "#1B1F26");
            o.setNumber("strokeWidth", 1);
            break;
        case Kind::MultiStateText:
            common(o, x, y, 220, 36);
            o.set("value", "0");
            o.set("stateList", "0 = Arr\xC3\xAAt; 1 = En marche | #2ECC71; 2 = En d\xC3\xA9" "faut | #E5534B | clignote; 3 = Maintenance | #F2C94C");
            o.set("unknownText", "\xC3\x89tat inconnu");
            o.set("font", "Sans");
            o.setNumber("fontSize", 18);
            o.set("textColor", "#E6EAF0");
            o.set("align", "centre");
            o.set("fill", "");
            o.set("stroke", "");
            break;
        case Kind::Bargraph:
            common(o, x, y, 80, 230);
            o.set("value", "40");
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.set("orientation", "verticale");
            o.set("zones", "0-60 = #2ECC71; 60-80 = #F2C94C; 80-100 = #E5534B");
            o.set("setpoint", "");
            o.set("setpointColor", "#FFFFFF");
            o.setNumber("ticks", 5);
            o.setFlag("showValue", true);
            o.set("format", "0");
            o.set("unit", "");
            o.set("font", "Sans");
            o.setNumber("fontSize", 12);
            o.set("textColor", "#C8D0DC");
            o.set("fill", "#2F6FD6");
            o.set("background", "#2A313C");
            break;
        case Kind::Thermometer:
            common(o, x, y, 90, 240);
            o.set("value", "20");
            o.setNumber("min", -20);
            o.setNumber("max", 60);
            o.set("unit", "\xC2\xB0" "C");
            o.set("format", "0.0");
            o.setNumber("ticks", 8);
            o.setFlag("showValue", true);
            o.set("font", "Sans");
            o.setNumber("fontSize", 11);
            o.set("textColor", "#C8D0DC");
            o.set("fill", "#E5534B");
            o.set("background", "#2A313C");
            o.set("stroke", "#8A9BB0");
            break;
        case Kind::Dial:
            common(o, x, y, 210, 210);
            o.set("value", "40");
            o.setNumber("min", 0);
            o.setNumber("max", 100);
            o.set("unit", "");
            o.set("format", "0");
            o.set("label", "");
            o.set("zones", "0-60 = #2ECC71; 60-85 = #F2C94C; 85-100 = #E5534B");
            o.setNumber("ticks", 10);
            o.set("font", "Sans");
            o.setNumber("fontSize", 13);
            o.set("textColor", "#E6EAF0");
            o.set("needleColor", "#F4F6FA");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            break;
        case Kind::Clock:
            common(o, x, y, 180, 180);
            o.set("clockStyle", "analogique");
            o.setFlag("seconds", true);
            o.setFlag("showDate", true);
            o.set("font", "Sans");
            o.setNumber("fontSize", 20);
            o.set("textColor", "#E6EAF0");
            o.set("fill", "#1F252E");
            o.set("stroke", "#3A4556");
            o.set("accent", "#E5534B");
            break;
        case Kind::HourMeter:
            // Il compte tant que sa condition est vraie ; sa variable garde le total.
            common(o, x, y, 230, 60);
            o.set("condition", "");
            o.set("durationFormat", "h:mm:ss");
            o.set("label", "Temps de marche");
            o.set("font", "Sans");
            o.setNumber("fontSize", 22);
            o.set("textColor", "#E6EAF0");
            o.set("colorOn", "#2ECC71");
            o.set("fill", "#141820");
            o.set("stroke", "#3A4556");
            o.setNumber("radius", 4);
            break;
        case Kind::SevenSegment:
            common(o, x, y, 210, 72);
            o.set("value", "0");
            o.setNumber("digits", 4);
            o.setNumber("decimals", 1);
            o.setFlag("leadingZeros", false);
            o.set("colorOn", "#FF3B30");
            o.set("colorOff", "#3A1E1E");
            o.set("fill", "#0B0D10");
            o.set("stroke", "#2A313C");
            break;
        case Kind::TrendArrow:
            common(o, x, y, 48, 48);
            o.set("value", "0");
            o.setNumber("window", 10);
            o.setNumber("deadband", 0.5);
            o.set("colorUp", "#2ECC71");
            o.set("colorDown", "#E5534B");
            o.set("colorSteady", "#9AA6B8");
            break;
        case Kind::Marquee:
            common(o, x, y, 440, 40);
            textStyle(o, "Bienvenue sur la ligne de production - consignes de s\xC3\xA9" "curit\xC3\xA9 au poste 3", "#F2C94C");
            std::erase_if(o.props, [](const Prop& p) { return p.key == "align" || p.key == "wrap"; });
            o.setNumber("fontSize", 18);
            o.setNumber("speed", 60);
            o.set("direction", "gauche");
            o.set("fill", "#1B2028");
            o.set("stroke", "#3A4556");
            break;
        case Kind::QrCode:
            common(o, x, y, 150, 150);
            o.set("text", "https://example.com/armoire-a");
            o.set("ecLevel", "M");
            o.setNumber("margin", 2);
            o.set("fill", "#FFFFFF");
            o.set("darkColor", "#000000");
            break;
    }
    if (kind != Kind::Group) communication(o);
    return o;
}

std::string uniqueObjectName(const View& v, std::string_view base) {
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = std::string(base) + "_" + std::to_string(i);
        bool used = false;
        for (const auto& o : v.objects) if (o.name == candidate) { used = true; break; }
        if (!used) return candidate;
    }
    return std::string(base);
}

std::string uniqueViewName(const Project& p, std::string_view base) {
    if (!p.viewByName(base)) return std::string(base);
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = std::string(base) + "_" + std::to_string(i);
        if (!p.viewByName(candidate)) return candidate;
    }
    return std::string(base);
}

namespace {
// "logo.png" -> ("logo", ".png")
std::pair<std::string, std::string> splitExtension(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return {std::string(name), {}};
    return {std::string(name.substr(0, dot)), std::string(name.substr(dot))};
}
} // namespace

std::string uniqueResourceName(const Project& p, std::string_view wanted) {
    if (!p.resourceByName(wanted)) return std::string(wanted);
    const auto [stem, ext] = splitExtension(wanted);
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = stem + "_" + std::to_string(i) + ext;
        if (!p.resourceByName(candidate)) return candidate;
    }
    return std::string(wanted);
}

std::string uniqueExternalName(const Project& p, std::string_view wanted) {
    if (!p.externalByName(wanted)) return std::string(wanted);
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = std::string(wanted) + "_" + std::to_string(i);
        if (!p.externalByName(candidate)) return candidate;
    }
    return std::string(wanted);
}

// ------------------------------------------------------------- actions ------
namespace {
struct TriggerName { Trigger t; const char* label; };
constexpr TriggerName kTriggerNames[] = {
    {Trigger::Click, "Clic"},
    {Trigger::DoubleClick, "Double clic"},
    {Trigger::RisingEdge, "Front montant"},
    {Trigger::FallingEdge, "Front descendant"},
    {Trigger::LongPress, "Appui long"},
    {Trigger::ValueChange, "Changement de valeur"},
    {Trigger::ViewOpen, "Ouverture de la vue"},
    {Trigger::ViewClose, "Fermeture de la vue"},
    {Trigger::Timer, "Timer"},
};
struct OperationName { Operation o; const char* label; };
constexpr OperationName kOperationNames[] = {
    {Operation::Toggle, "Basculer"},
    {Operation::Set, "Mettre \xC3\xA0 1"},
    {Operation::Reset, "Mettre \xC3\xA0 0"},
    {Operation::Increment, "Incr\xC3\xA9menter"},
    {Operation::Decrement, "D\xC3\xA9" "cr\xC3\xA9menter"},
    {Operation::Assign, "Affecter"},
    {Operation::Navigate, "Naviguer"},
    {Operation::Popup, "Ouvrir une popup"},
    {Operation::ClosePopup, "Fermer la popup"},
    {Operation::RunScript, "Ex\xC3\xA9" "cuter un script"},
    {Operation::CallScript, "Script g\xC3\xA9n\xC3\xA9ral"},
    {Operation::Log, "Journaliser"},
    {Operation::AckAlarm, "Acquitter une alarme"},
    {Operation::LoadRecipe, "Charger une recette"},
    {Operation::ChangeUser, "Changer d'utilisateur"},
    {Operation::RequestResource, "Demander une ressource"},
    {Operation::BindTable, "Lier un tableau"},
    {Operation::PlaySound, "Jouer un son"},
    {Operation::ShowSystem, "Param\xC3\xA8tres syst\xC3\xA8me"},
    // lot 8
    {Operation::ChangePopup, "Changer de popup"},
    {Operation::CenterPopup, "Centrer la popup"},
    {Operation::PreviousPopup, "Popup pr\xC3\xA9" "c\xC3\xA9" "dente"},
    {Operation::CloseAllPopups, "Fermer toutes les popups"},
    {Operation::Logout, "D\xC3\xA9" "connecter l'utilisateur"},
    // lot 11
    {Operation::ShelveAlarm, "Mettre de c\xC3\xB4t\xC3\xA9 une alarme"},
    {Operation::UnshelveAlarm, "Remettre une alarme en service"},
    {Operation::SilenceAlarms, "Faire taire les alarmes"},
    {Operation::Export, "Exporter des donn\xC3\xA9" "es"},
    // lot 12
    {Operation::NavigateBack, "Vue pr\xC3\xA9" "c\xC3\xA9" "dente"},
    {Operation::NavigateForward, "Vue suivante"},
    {Operation::NavigateHome, "Vue d'accueil"},
    {Operation::ShowLogin, "Menu de connexion"},
    // lot 13
    {Operation::SetLanguage, "Changer de langue"},
    {Operation::SetTheme, "Changer de th\xC3\xA8me"},
    // lot 16
    {Operation::GifPlay, "Jouer le GIF"},
    {Operation::GifPause, "Mettre le GIF en pause"},
    {Operation::GifStop, "Arr\xC3\xAAter le GIF"},
    {Operation::GifReplay, "Rejouer le GIF N fois"},
    // 1.9
    {Operation::ApplyCopy, "Appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence"},
    // 1.11.6
    {Operation::Maths, "Maths"},
    {Operation::Keyboard, "Clavier virtuel"},
};
struct PlacementName { const char* key; const char* label; };
constexpr PlacementName kPlacementNames[] = {
    {"centre", "Centr\xC3\xA9" "e"},
    {"objet", "Sous l'objet cliqu\xC3\xA9"},
    {"haut-gauche", "En haut \xC3\xA0 gauche"},
    {"haut-droite", "En haut \xC3\xA0 droite"},
    {"bas-gauche", "En bas \xC3\xA0 gauche"},
    {"bas-droite", "En bas \xC3\xA0 droite"},
    {"derniere", "\xC3\x80 sa derni\xC3\xA8re place"},
};
struct TransitionName { TransitionKind k; const char* label; };
constexpr TransitionName kTransitionNames[] = {
    {TransitionKind::Instant, "Instantan\xC3\xA9" "e"},
    {TransitionKind::Fade, "Fondu"},
    {TransitionKind::Slide, "Glissement"},
    {TransitionKind::Zoom, "Zoom"},
    {TransitionKind::Rotate, "Rotation"},
    {TransitionKind::Custom, "Personnalis\xC3\xA9" "e"},
};
std::string lowerTransitionWord(std::string_view s) {
    std::string out(s);
    // Les libelles commencent par une majuscule ASCII : la minuscule suffit.
    if (!out.empty() && out[0] >= 'A' && out[0] <= 'Z') out[0] = static_cast<char>(out[0] - 'A' + 'a');
    return out;
}
std::string trimCopy(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
} // namespace

std::string_view triggerLabel(Trigger t) noexcept {
    for (const auto& n : kTriggerNames) if (n.t == t) return n.label;
    return "Clic";
}
std::optional<Trigger> triggerFromLabel(std::string_view s) noexcept {
    for (const auto& n : kTriggerNames) if (iequals(n.label, s)) return n.t;
    return std::nullopt;
}
std::string_view operationLabel(Operation o) noexcept {
    for (const auto& n : kOperationNames) if (n.o == o) return n.label;
    return "Mettre \xC3\xA0 1";
}
std::optional<Operation> operationFromLabel(std::string_view s) noexcept {
    for (const auto& n : kOperationNames) if (iequals(n.label, s)) return n.o;
    return std::nullopt;
}
std::string_view transitionLabel(TransitionKind k) noexcept {
    for (const auto& n : kTransitionNames) if (n.k == k) return n.label;
    return "Instantan\xC3\xA9" "e";
}
std::optional<TransitionKind> transitionFromLabel(std::string_view s) noexcept {
    for (const auto& n : kTransitionNames) if (iequals(n.label, s)) return n.k;
    return std::nullopt;
}

bool triggerWatches(Trigger t) noexcept {
    return t == Trigger::RisingEdge || t == Trigger::FallingEdge || t == Trigger::ValueChange;
}
bool triggerWaits(Trigger t) noexcept { return t == Trigger::LongPress || t == Trigger::Timer; }
bool operationWritesVariable(Operation o) noexcept {
    return o == Operation::Toggle || o == Operation::Set || o == Operation::Reset || o == Operation::Increment
        || o == Operation::Decrement || o == Operation::Assign
        || o == Operation::Maths || o == Operation::Keyboard;           // 1.11.6
}
std::string targetVariable(std::string_view target) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!target.empty() && blank(target.front())) target.remove_prefix(1);
    while (!target.empty() && blank(target.back())) target.remove_suffix(1);
    if (!target.empty() && target.front() == '=') {
        target.remove_prefix(1);
        while (!target.empty() && blank(target.front())) target.remove_prefix(1);
    }
    return std::string(target);
}
bool operationOpensView(Operation o) noexcept {
    return o == Operation::Navigate || o == Operation::Popup || o == Operation::ChangePopup;
}
bool operationHasValue(Operation o) noexcept {
    return o == Operation::Increment || o == Operation::Decrement || o == Operation::Assign
        || o == Operation::Maths                                        // 1.11.6 : la formule
        || o == Operation::RunScript || o == Operation::Log
        || o == Operation::ShelveAlarm || o == Operation::Export      // lot 11 : "30; raison", le fichier
        || o == Operation::GifReplay;                                  // lot 16 : N fois
}
bool operationTakesArguments(Operation o) noexcept {
    return o == Operation::Navigate || o == Operation::Popup || o == Operation::ChangePopup;
}
std::string_view popupPlacementLabel(std::string_view placement) noexcept {
    for (const auto& n : kPlacementNames) if (iequals(n.key, placement)) return n.label;
    return placement.empty() ? std::string_view{kPlacementNames[0].label} : placement;
}
std::string popupPlacementFromLabel(std::string_view label) {
    for (const auto& n : kPlacementNames)
        if (iequals(n.label, label) || iequals(n.key, label)) return n.key;
    return std::string(label);   // "x,y", ou vide
}

std::string describeAction(const Action& a) {
    std::string s(triggerLabel(a.trigger));
    if (triggerWatches(a.trigger) && !a.watch.empty()) s += " de " + a.watch;
    if (a.trigger == Trigger::LongPress) s += " (" + std::to_string(a.delayMs > 0 ? a.delayMs : 800) + " ms)";
    if (a.trigger == Trigger::Timer) s += " (" + formatDuration((a.delayMs > 0 ? a.delayMs : 1000) / 1000.0) + ")";
    s += " \xE2\x86\x92 ";
    s += operationLabel(a.operation);
    const auto transition = [&] {
        if (a.transition.kind == TransitionKind::Instant) return std::string{};
        return " (" + lowerTransitionWord(transitionLabel(a.transition.kind)) + ", " + std::to_string(a.transition.durationMs)
             + " ms)";
    };
    switch (a.operation) {
        case Operation::Toggle: case Operation::Set: case Operation::Reset:
            s += " " + a.target;
            break;
        case Operation::Increment: case Operation::Decrement:
            s += " " + a.target + (a.value.empty() || a.value == "1" ? std::string{} : " de " + a.value);
            break;
        case Operation::Assign:
            s += " " + a.target + " := " + a.value;
            break;
        case Operation::Navigate:
            s += " vers " + a.target + (a.value.empty() ? std::string{} : " (" + a.value + ")") + transition();
            break;
        case Operation::Popup: case Operation::ChangePopup:
            s += " " + a.target + (a.value.empty() ? std::string{} : " (" + a.value + ")");
            if (a.operation == Operation::Popup && !a.placement.empty() && a.placement != "centre")
                s += ", " + std::string(popupPlacementLabel(a.placement));
            s += transition();
            break;
        case Operation::CenterPopup: case Operation::PreviousPopup:
            if (!a.target.empty()) s += " " + a.target;
            break;
        case Operation::CloseAllPopups: case Operation::Logout:
            s += transition();
            break;
        case Operation::ClosePopup:
            s += transition();
            break;
        case Operation::RunScript: {
            const auto lines = 1 + std::count(a.value.begin(), a.value.end(), '\n');
            s += " (" + std::to_string(lines) + " ligne" + (lines > 1 ? "s" : "") + ")";
            break;
        }
        case Operation::CallScript:
            s += " " + a.target;
            break;
        case Operation::Log:
            s += " \xC2\xAB " + a.value + " \xC2\xBB";
            break;
        case Operation::AckAlarm: case Operation::LoadRecipe: case Operation::ChangeUser:
            if (!a.target.empty()) s += " " + a.target;
            break;
        case Operation::RequestResource:
            if (!a.value.empty()) s += " (" + a.value + ")";
            if (!a.target.empty()) s += " \xE2\x86\x92 " + a.target;
            break;
        case Operation::BindTable:
            s += " " + a.target + (a.value.empty() ? std::string(" \xC3\xA0 sa source") : " \xC3\xA0 " + a.value);
            break;
        case Operation::PlaySound:
            if (!a.target.empty()) s += " " + a.target;
            break;
        // lot 16 : un GIF anime de la vue
        case Operation::GifPlay: case Operation::GifPause: case Operation::GifStop:
            if (!a.target.empty()) s += " " + a.target;
            break;
        case Operation::GifReplay:
            if (!a.target.empty()) s += " " + a.target;
            // Vide : une fois (comme le moteur) ; 0 : sans fin.
            s += trimCopy(a.value) == "0" ? std::string(" (sans fin)") : " (" + (trimCopy(a.value).empty() ? std::string("1") : trimCopy(a.value)) + " fois)";
            break;
        case Operation::ShowSystem:
            if (a.value.find("iagnostic") != std::string::npos) s += " (diagnostic)";   // lot 10 : l'onglet
            else if (a.value.find("imulation") != std::string::npos) s += " (simulation)";   // 1.9 : la page Simulation
            break;
        // lot 11
        case Operation::ShelveAlarm: {
            s += " " + (a.target.empty() ? std::string("(l'alarme choisie)") : a.target);
            const auto semi = a.value.find(';');
            const std::string minutes = trimCopy(a.value.substr(0, semi));
            const std::string reason = semi == std::string::npos ? std::string{} : trimCopy(a.value.substr(semi + 1));
            s += minutes.empty() || minutes == "0" ? std::string(" (sans limite)") : " (" + minutes + " min)";
            if (!reason.empty()) s += " \xC2\xAB " + reason + " \xC2\xBB";
            break;
        }
        case Operation::UnshelveAlarm:
            s += " " + (a.target.empty() ? std::string("(l'alarme choisie)") : a.target == "*" ? std::string("(toutes)") : a.target);
            break;
        case Operation::SilenceAlarms:
            break;
        case Operation::Export:
            s += " " + (a.target.empty() ? std::string("alarmes") : a.target) + (a.value.empty() ? std::string{} : " \xE2\x86\x92 " + a.value);
            if (!a.askWhere) s += " (sans question)";      // Lot API 8 : exports/ sans demander ou
            break;
        // lot 12 : l'historique de navigation, la vue d'accueil
        case Operation::NavigateBack: case Operation::NavigateForward: case Operation::NavigateHome:
            s += transition();
            break;
        case Operation::ShowLogin:
            if (!a.value.empty()) s += " (" + a.value + ")";    // l'onglet
            break;
        // lot 13 : la langue (un code, "suivante", une expression)
        case Operation::SetLanguage:
            s += " " + (a.target.empty() ? std::string("suivante") : a.target);
            break;
        case Operation::SetTheme:
            s += " " + (a.target.empty() ? std::string("(bascule)") : a.target);
            break;
        // 1.11.6 : Maths (la formule et ses references), le clavier virtuel
        case Operation::Maths: {
            s += " " + a.target + " := " + a.value;
            const auto n = a.params.empty() ? 0 : 1 + std::count(a.params.begin(), a.params.end(), ';');
            if (n > 0) s += " (" + std::to_string(n) + " r\xC3\xA9" "f\xC3\xA9rence" + (n > 1 ? "s" : "") + ")";
            break;
        }
        case Operation::Keyboard:
            s += " \xE2\x86\x92 " + a.target;
            break;
        // 1.9 : un parametre en mode Les deux, ou tous
        case Operation::ApplyCopy: {
            const std::string t = trimCopy(a.target);
            s += t.empty() || t == "*" ? std::string(" (tous les param\xC3\xA8tres Les deux)") : " " + t;
            break;
        }
    }
    if (!a.guard.empty()) s += "  si " + a.guard;
    return s;
}

std::string_view eventLabel(std::string_view e) noexcept {
    if (e == "OnOpen") return "OnOpen (ouverture)";
    if (e == "OnCycle") return "OnCycle (chaque cycle)";
    if (e == "OnClose") return "OnClose (fermeture)";
    if (e == "Demarrage") return "D\xC3\xA9marrage";
    if (e == "Cyclique") return "Cyclique";
    if (e == "Changement") return "Sur changement";
    if (e == "Appel") return "Appel\xC3\xA9";
    return e;
}

Script* Project::script(Id id) noexcept {
    for (auto& s : programs.scripts) if (s.id == id) return &s;
    for (auto& v : views) for (auto& s : v.scripts) if (s.id == id) return &s;
    return nullptr;
}
const Script* Project::script(Id id) const noexcept { return const_cast<Project*>(this)->script(id); }
const Script* Project::generalScript(std::string_view name) const noexcept {
    for (const auto& s : programs.scripts) if (iequals(s.name, name)) return &s;
    return nullptr;
}
const Variable* Project::variable(std::string_view name) const noexcept {
    for (const auto& v : programs.variables) if (iequals(v.name, name)) return &v;
    return nullptr;
}
Variable* Project::variableById(Id id) noexcept {
    for (auto& v : programs.variables) if (v.id == id) return &v;
    return nullptr;
}
const Variable* Project::variableById(Id id) const noexcept { return const_cast<Project*>(this)->variableById(id); }
HmiType* Project::hmiType(Id id) noexcept {
    for (auto& ty : programs.types) if (ty.id == id) return &ty;
    return nullptr;
}
const HmiType* Project::hmiType(Id id) const noexcept { return const_cast<Project*>(this)->hmiType(id); }
const HmiType* Project::hmiTypeByName(std::string_view name) const noexcept {
    for (const auto& ty : programs.types) if (iequals(ty.name, name)) return &ty;
    return nullptr;
}
HmiFunction* Project::function(Id id) noexcept {
    for (auto& f : programs.functions) if (f.id == id) return &f;
    return nullptr;
}
const HmiFunction* Project::function(Id id) const noexcept { return const_cast<Project*>(this)->function(id); }
const HmiFunction* Project::functionByName(std::string_view name) const noexcept {
    for (const auto& f : programs.functions) if (iequals(f.name, name)) return &f;
    return nullptr;
}
Id Project::viewOfScript(Id id) const noexcept {
    for (const auto& v : views) for (const auto& s : v.scripts) if (s.id == id) return v.id;
    return kNoId;
}

// ---- lot 4 : alarmes, recettes, utilisateurs ---------------------------------
AlarmDef* Project::alarm(Id id) noexcept {
    for (auto& a : alarms) if (a.id == id) return &a;
    return nullptr;
}
const AlarmDef* Project::alarm(Id id) const noexcept {
    for (const auto& a : alarms) if (a.id == id) return &a;
    return nullptr;
}
const AlarmDef* Project::alarmByName(std::string_view name) const noexcept {
    for (const auto& a : alarms) if (iequals(a.name, name)) return &a;
    return nullptr;
}
Recipe* Project::recipe(Id id) noexcept {
    for (auto& r : recipes) if (r.id == id) return &r;
    return nullptr;
}
const Recipe* Project::recipe(Id id) const noexcept {
    for (const auto& r : recipes) if (r.id == id) return &r;
    return nullptr;
}
const Recipe* Project::recipeByName(std::string_view name) const noexcept {
    for (const auto& r : recipes) if (iequals(r.name, name)) return &r;
    return nullptr;
}
User* Project::user(Id id) noexcept {
    for (auto& u : security.users) if (u.id == id) return &u;
    return nullptr;
}
const User* Project::user(Id id) const noexcept {
    for (const auto& u : security.users) if (u.id == id) return &u;
    return nullptr;
}
const User* Project::userByLogin(std::string_view login) const noexcept {
    for (const auto& u : security.users) if (iequals(u.login, login)) return &u;
    return nullptr;
}
UserGroup* Project::group(Id id) noexcept {
    for (auto& g : security.groups) if (g.id == id) return &g;
    return nullptr;
}
const UserGroup* Project::group(Id id) const noexcept {
    for (const auto& g : security.groups) if (g.id == id) return &g;
    return nullptr;
}
const UserGroup* Project::groupByName(std::string_view name) const noexcept {
    for (const auto& g : security.groups) if (iequals(g.name, name)) return &g;
    return nullptr;
}
const Role* Project::role(std::string_view name) const noexcept {
    for (const auto& r : security.roles) if (iequals(r.name, name)) return &r;
    return nullptr;
}
// Lot 12 : les styles nommes.
Style* Project::style(Id id) noexcept {
    for (auto& s : styles) if (s.id == id) return &s;
    return nullptr;
}
const Style* Project::style(Id id) const noexcept {
    for (const auto& s : styles) if (s.id == id) return &s;
    return nullptr;
}
const Style* Project::styleByName(std::string_view name) const noexcept {
    for (const auto& s : styles) if (iequals(s.name, name)) return &s;
    return nullptr;
}
const Language* Languages::find(std::string_view code) const noexcept {
    for (const auto& l : list) if (iequals(l.code, code)) return &l;
    return nullptr;
}
const std::string& Languages::source() const noexcept {
    static const std::string fr = "fr";
    return list.empty() ? fr : list.front().code;
}

TestScenario* Project::scenario(Id id) noexcept {
    for (auto& s : scenarios) if (s.id == id) return &s;
    return nullptr;
}
const TestScenario* Project::scenario(Id id) const noexcept {
    for (const auto& s : scenarios) if (s.id == id) return &s;
    return nullptr;
}
const TestScenario* Project::scenarioByName(std::string_view name) const noexcept {
    for (const auto& s : scenarios) if (iequals(s.name, name)) return &s;
    return nullptr;
}

// Lot 14 : les rapports periodiques, les destinataires des notifications.
Report* Project::report(Id id) noexcept {
    for (auto& r : reports) if (r.id == id) return &r;
    return nullptr;
}
const Report* Project::report(Id id) const noexcept {
    for (const auto& r : reports) if (r.id == id) return &r;
    return nullptr;
}
const Report* Project::reportByName(std::string_view name) const noexcept {
    for (const auto& r : reports) if (iequals(r.name, name)) return &r;
    return nullptr;
}
const NotifyRecipient* Project::recipientByName(std::string_view name) const noexcept {
    for (const auto& r : notify.recipients) if (iequals(r.name, name)) return &r;
    return nullptr;
}

// ---- lot 15 : les equipements ----------------------------------------------------
Equipment* Project::equipment(Id id) noexcept {
    for (auto& e : equipments) if (e.id == id) return &e;
    return nullptr;
}
const Equipment* Project::equipment(Id id) const noexcept {
    for (const auto& e : equipments) if (e.id == id) return &e;
    return nullptr;
}
const Equipment* Project::equipmentByName(std::string_view name) const noexcept {
    for (const auto& e : equipments) if (iequals(e.name, name)) return &e;
    return nullptr;
}

std::string_view equipmentTypeKey(EquipmentType t) noexcept {
    switch (t) {
        case EquipmentType::ModbusTcp: return "modbus_tcp";
        case EquipmentType::EthernetTcp: return "ethernet_tcp";
    }
    return "modbus_tcp";
}
std::string_view equipmentTypeLabel(EquipmentType t) noexcept {
    switch (t) {
        case EquipmentType::ModbusTcp: return "Modbus TCP/IP";
        case EquipmentType::EthernetTcp: return "Ethernet TCP/IP";
    }
    return "Modbus TCP/IP";
}
std::optional<EquipmentType> equipmentTypeFrom(std::string_view s) noexcept {
    for (const auto t : {EquipmentType::ModbusTcp, EquipmentType::EthernetTcp})
        if (iequals(s, equipmentTypeKey(t)) || iequals(s, equipmentTypeLabel(t))) return t;
    if (iequals(s, "modbus") || iequals(s, "Modbus TCP")) return EquipmentType::ModbusTcp;
    if (iequals(s, "ethernet") || iequals(s, "Ethernet TCP") || iequals(s, "ip")) return EquipmentType::EthernetTcp;
    return std::nullopt;
}
const std::vector<std::string>& equipmentTypeLabels() {
    static const std::vector<std::string> labels{std::string(equipmentTypeLabel(EquipmentType::ModbusTcp)),
                                                 std::string(equipmentTypeLabel(EquipmentType::EthernetTcp))};
    return labels;
}

// Lot 17 : le jumeau simule.
std::string_view twinStartKey(TwinStart s) noexcept {
    switch (s) {
        case TwinStart::Zeros: return "zeros";
        case TwinStart::Initial: return "initiales";
        case TwinStart::Saved: return "gardee";
    }
    return "initiales";
}
std::string_view twinStartLabel(TwinStart s) noexcept {
    switch (s) {
        case TwinStart::Zeros: return "des z\xC3\xA9ros";
        case TwinStart::Initial: return "valeurs initiales des variables";
        case TwinStart::Saved: return "la m\xC3\xA9moire gard\xC3\xA9" "e";
    }
    return "valeurs initiales des variables";
}
// ---- 1.9 : ce que l'IHM lit (l'esclave simule lie) ----
std::string_view readSourceKey(ReadSource s) noexcept {
    switch (s) {
        case ReadSource::Real: return "vrai";
        case ReadSource::Slave: return "esclave";
        case ReadSource::Auto: return "auto";
    }
    return "vrai";
}
std::string_view readSourceLabel(ReadSource s) noexcept {
    switch (s) {
        case ReadSource::Real: return "le vrai appareil";
        case ReadSource::Slave: return "l'esclave simul\xC3\xA9";
        case ReadSource::Auto: return "le vrai ; l'esclave s'il ne r\xC3\xA9pond pas";
    }
    return "le vrai appareil";
}
std::optional<ReadSource> readSourceFrom(std::string_view s) noexcept {
    for (const auto k : {ReadSource::Real, ReadSource::Slave, ReadSource::Auto})
        if (iequals(s, readSourceKey(k)) || iequals(s, readSourceLabel(k))) return k;
    // Les libelles d'avant (lot 17 : "au jumeau", "au vrai appareil") et les mots de la page Simulation.
    const std::string l = [&] {
        std::string out(s);
        for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }();
    if (l.find("auto") != std::string::npos || l.find("s'il ne r") != std::string::npos) return ReadSource::Auto;
    if (l.find("esclave") != std::string::npos || l.find("jumeau") != std::string::npos || l.find("simul") != std::string::npos) return ReadSource::Slave;
    if (l.find("vrai") != std::string::npos || l.find("reel") != std::string::npos || l.find("r\xC3\xA9" "el") != std::string::npos) return ReadSource::Real;
    if (l == "true" || l == "1") return ReadSource::Slave;
    if (l == "false" || l == "0") return ReadSource::Real;
    return std::nullopt;
}
std::string_view stationReadKey(StationRead s) noexcept {
    switch (s) {
        case StationRead::Real: return "vrai";
        case StationRead::Auto: return "auto";
        case StationRead::Admin: return "admin";
    }
    return "vrai";
}
std::string_view stationReadLabel(StationRead s) noexcept {
    switch (s) {
        case StationRead::Real: return "toujours le vrai appareil";
        case StationRead::Auto: return "automatique";
        case StationRead::Admin: return "au choix d'un administrateur";
    }
    return "toujours le vrai appareil";
}
std::optional<StationRead> stationReadFrom(std::string_view s) noexcept {
    for (const auto k : {StationRead::Real, StationRead::Auto, StationRead::Admin})
        if (iequals(s, stationReadKey(k)) || iequals(s, stationReadLabel(k))) return k;
    std::string l(s);
    for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l.find("admin") != std::string::npos || l.find("choix") != std::string::npos) return StationRead::Admin;
    if (l.find("auto") != std::string::npos) return StationRead::Auto;
    if (l.find("vrai") != std::string::npos || l.find("toujours") != std::string::npos) return StationRead::Real;
    return std::nullopt;
}
std::string slaveKey(std::string_view name) {
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c < 0x80) {
            out += std::isalnum(c) ? static_cast<char>(c) : '_';
            continue;
        }
        // Un caractere UTF-8 : les lettres accentuees du francais perdent leur accent.
        std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + len > name.size()) len = name.size() - i;
        const std::string_view ch = name.substr(i, len);
        static const std::pair<std::string_view, char> kPlain[] = {
            {"\xC3\xA0", 'a'}, {"\xC3\xA2", 'a'}, {"\xC3\xA4", 'a'}, {"\xC3\xA7", 'c'}, {"\xC3\xA8", 'e'}, {"\xC3\xA9", 'e'},
            {"\xC3\xAA", 'e'}, {"\xC3\xAB", 'e'}, {"\xC3\xAE", 'i'}, {"\xC3\xAF", 'i'}, {"\xC3\xB4", 'o'}, {"\xC3\xB6", 'o'},
            {"\xC3\xB9", 'u'}, {"\xC3\xBB", 'u'}, {"\xC3\xBC", 'u'}, {"\xC3\x80", 'A'}, {"\xC3\x82", 'A'}, {"\xC3\x87", 'C'},
            {"\xC3\x88", 'E'}, {"\xC3\x89", 'E'}, {"\xC3\x8A", 'E'}, {"\xC3\x8B", 'E'}, {"\xC3\x8E", 'I'}, {"\xC3\x94", 'O'},
            {"\xC3\x99", 'U'}, {"\xC3\x9B", 'U'}, {"\xC5\x93", 'o'}};
        char plain = '_';
        for (const auto& [u, a] : kPlain)
            if (u == ch) plain = a;
        out += plain;
        i += len - 1;
    }
    if (out.empty()) out = "_";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out = "E_" + out;
    return out;
}

std::string_view behaviorKindKey(BehaviorKind k) noexcept {
    switch (k) {
        case BehaviorKind::Constant: return "constante";
        case BehaviorKind::Sine: return "sinus";
        case BehaviorKind::Ramp: return "rampe";
        case BehaviorKind::Counter: return "compteur";
        case BehaviorKind::Blink: return "clignote";
        case BehaviorKind::Random: return "aleatoire";
        case BehaviorKind::Copy: return "recopie";
        case BehaviorKind::FollowPlc: return "automate";
        case BehaviorKind::Steps: return "etapes";
    }
    return "constante";
}
std::string_view behaviorKindLabel(BehaviorKind k) noexcept {
    switch (k) {
        case BehaviorKind::Constant: return "constante";
        case BehaviorKind::Sine: return "sinus";
        case BehaviorKind::Ramp: return "rampe";
        case BehaviorKind::Counter: return "compteur";
        case BehaviorKind::Blink: return "clignote";
        case BehaviorKind::Random: return "al\xC3\xA9" "atoire";
        case BehaviorKind::Copy: return "recopie";
        case BehaviorKind::FollowPlc: return "suit l'automate";
        case BehaviorKind::Steps: return "\xC3\xA9tapes";
    }
    return "constante";
}
std::optional<BehaviorKind> behaviorKindFrom(std::string_view s) noexcept {
    for (const auto k : kBehaviorKinds)
        if (iequals(s, behaviorKindKey(k)) || iequals(s, behaviorKindLabel(k))) return k;
    return std::nullopt;
}

const RecipeRecord* Recipe::record(std::string_view n) const noexcept {
    for (const auto& r : records) if (iequals(r.name, n)) return &r;
    return nullptr;
}
RecipeRecord* Recipe::record(Id recordId) noexcept {
    for (auto& r : records) if (r.id == recordId) return &r;
    return nullptr;
}
const RecipeRecord* Recipe::record(Id recordId) const noexcept {
    for (const auto& r : records) if (r.id == recordId) return &r;
    return nullptr;
}

std::string_view alarmPriorityLabel(int priority) noexcept {
    switch (priority) {
        case 1: return "Critique";
        case 2: return "Haute";
        case 3: return "Moyenne";
        case 4: return "Basse";
        default: return "?";
    }
}

const std::vector<std::string>& alarmCategories() {
    static const std::vector<std::string> c = {"D\xC3\xA9" "faut", "Alarme", "Avertissement", "Information"};
    return c;
}

const std::vector<std::string>& permissionNames() {
    static const std::vector<std::string> p = {"Naviguer", "Piloter", "Acquitter", "Recettes", "Scripts", "Administrer"};
    return p;
}

const std::vector<std::string>& protectionNames() {
    static const std::vector<std::string> p = {"classique", "dynamique", "expression"};
    return p;
}

// Les roles et les groupes de la specification. Les identifiants des groupes
// sont fixes (0xFFFFFE01...) : un projet neuf n'a pas encore de compteur, et
// ils ne croisent jamais ceux que le projet attribue (qui partent de 1).
Security Security::defaults() {
    Security s;
    s.roles = {
        {"Conduite", {"Naviguer", "Piloter", "Acquitter"}, "Conduire l'installation : changer de vue, commander, acquitter."},
        {"Maintenance", {"Naviguer", "Scripts"}, "Lancer les scripts de maintenance."},
        {"R\xC3\xA9glages", {"Recettes"}, "Charger une recette."},
        {"Administration", {"Administrer"}, "Tout."},
    };
    s.groups = {
        {0xFFFFFE01u, "Op\xC3\xA9rateur", 1, {"Conduite"}, "Conduite courante"},
        {0xFFFFFE02u, "Maintenance", 2, {"Conduite", "Maintenance"}, "Interventions"},
        {0xFFFFFE03u, "Superviseur", 3, {"Conduite", "Maintenance", "R\xC3\xA9glages"}, "Recettes et r\xC3\xA9glages"},
        {0xFFFFFE04u, "Administrateur", 4, {"Administration"}, "Tout"},
    };
    return s;
}

bool userHas(const Project& p, const User& u, std::string_view permission) {
    const auto* g = p.group(u.group);
    if (!g) return false;
    for (const auto& roleName : g->roles)
        if (const auto* r = p.role(roleName))
            for (const auto& perm : r->permissions)
                if (iequals(perm, permission) || iequals(perm, "Administrer")) return true;
    return false;
}

int userLevel(const Project& p, const User& u) {
    const auto* g = p.group(u.group);
    return g ? g->level : 0;
}

bool isIdentifier(std::string_view s) noexcept {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

std::string uniqueScriptName(const Project& p, std::string_view wanted) {
    if (!p.generalScript(wanted)) return std::string(wanted);
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = std::string(wanted) + "_" + std::to_string(i);
        if (!p.generalScript(candidate)) return candidate;
    }
    return std::string(wanted);
}

std::string uniqueVariableName(const Project& p, std::string_view wanted) {
    if (!p.variable(wanted)) return std::string(wanted);
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = std::string(wanted) + "_" + std::to_string(i);
        if (!p.variable(candidate)) return candidate;
    }
    return std::string(wanted);
}

std::string uniqueFunctionName(const Project& p, std::string_view wanted) {
    const auto taken = [&](std::string_view n) { return p.functionByName(n) || p.variable(n); };
    if (!taken(wanted)) return std::string(wanted);
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = std::string(wanted) + "_" + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
    return std::string(wanted);
}

namespace {
template <class Taken>
std::string uniqueName(std::string_view wanted, Taken&& taken) {
    if (!taken(wanted)) return std::string(wanted);
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = std::string(wanted) + "_" + std::to_string(i);
        if (!taken(candidate)) return candidate;
    }
    return std::string(wanted);
}
} // namespace

std::string uniqueAlarmName(const Project& p, std::string_view wanted) {
    return uniqueName(wanted, [&](std::string_view n) { return p.alarmByName(n) != nullptr; });
}
std::string uniqueRecipeName(const Project& p, std::string_view wanted) {
    return uniqueName(wanted, [&](std::string_view n) { return p.recipeByName(n) != nullptr; });
}
std::string uniqueLogin(const Project& p, std::string_view wanted) {
    return uniqueName(wanted, [&](std::string_view n) { return p.userByLogin(n) != nullptr; });
}
std::string uniqueStyleName(const Project& p, std::string_view wanted) {
    return uniqueName(wanted, [&](std::string_view n) { return p.styleByName(n) != nullptr; });
}

std::string uniqueEquipmentName(const Project& p, std::string_view wanted) {
    return uniqueName(wanted, [&](std::string_view n) { return p.equipmentByName(n) != nullptr; });
}

std::string uniqueLayerName(const View& v, std::string_view base) {
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = std::string(base) + " " + std::to_string(i);
        bool used = false;
        for (const auto& l : v.layers) if (l.name == candidate) { used = true; break; }
        if (!used) return candidate;
    }
    return std::string(base);
}

View makeView(Project& p, std::string name) {
    View v;
    v.id = p.allocate();
    v.name = std::move(name);
    v.width = p.config.width;
    v.height = p.config.height;
    Layer l;
    l.id = p.allocate();
    l.name = "Calque 1";
    v.layers.push_back(l);
    v.activeLayer = l.id;
    return v;
}

} // namespace hmi
