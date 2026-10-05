#include "HmiQuality.hpp"

#include "HmiControls.hpp"
#include "HmiExpr.hpp"
#include "HmiNavigation.hpp"
#include "HmiSymbols.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

namespace hmi {

namespace {

using S = Issue::Severity;

constexpr double kPi = 3.14159265358979323846;

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string px(double v) { return std::to_string(static_cast<long>(std::lround(v))); }

// La valeur d'une propriete quand elle est fixe (sans expression) ; nullopt :
// animee, ou absente.
std::optional<std::string> fixed(const Object& o, std::string_view key) {
    const Prop* p = o.find(key);
    if (!p || !p->expr.empty()) return std::nullopt;
    return p->value;
}
bool animated(const Object& o, std::string_view key) {
    const Prop* p = o.find(key);
    return p && !p->expr.empty();
}

// Une couleur fixe et son alpha (255 sans alpha) ; nullopt : animee, vide, illisible.
struct Rgba8 { std::uint32_t rgb{0}; int alpha{255}; };
std::optional<Rgba8> fixedColor(const Object& o, std::string_view key) {
    const auto v = fixed(o, key);
    if (!v) return std::nullopt;
    const std::string t = trim(*v);
    const auto rgb = parseRgb(t);
    if (!rgb) return std::nullopt;
    Rgba8 c{*rgb, 255};
    if (t.size() == 9) c.alpha = static_cast<int>(std::strtol(t.substr(7, 2).c_str(), nullptr, 16));
    return c;
}

// Le cadre de l'objet une fois tourne (sa boite englobante).
Box boundsOf(const Object& o) {
    const Box b = o.box();
    const double rot = std::fmod(o.rotation(), 360.0);
    if (std::fabs(rot) < 0.01) return b;
    const double a = rot * kPi / 180.0;
    const double c = std::fabs(std::cos(a)), s = std::fabs(std::sin(a));
    const double w = b.w * c + b.h * s, h = b.w * s + b.h * c;
    return {b.cx() - w / 2, b.cy() - h / 2, w, h};
}

// Les litteraux 'texte' et "texte" d'un code ST.
std::vector<std::string> literals(std::string_view code) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char q = code[i];
        if (q != '\'' && q != '"') continue;
        const auto end = code.find(q, i + 1);
        if (end == std::string_view::npos) break;
        out.emplace_back(code.substr(i + 1, end - i - 1));
        i = end;
    }
    return out;
}

// Le texte sans ses trous : {Variable:0.0} ne compte pas (sa longueur change en
// marche) ; ce qui reste deborde a coup sur.
std::string literalPart(std::string_view text) {
    std::string out;
    int depth = 0;
    for (const char c : text) {
        if (c == '{') { ++depth; continue; }
        if (c == '}' && depth > 0) { --depth; continue; }
        if (depth == 0) out += c;
    }
    return out;
}

std::vector<std::string> linesOf(const std::string& s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (true) {
        const auto nl = s.find('\n', from);
        out.push_back(s.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    return out;
}

std::string quoted(std::string_view s) {
    std::string t = trim(s);
    // Au plus une trentaine de caracteres, sans couper un caractere UTF-8.
    if (t.size() > 34) {
        std::size_t cut = 30;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;
        t = t.substr(0, cut) + "...";
    }
    return "\xC2\xAB " + t + " \xC2\xBB";
}

double widthOf(std::uint32_t cp) {
    if (cp == ' ') return 0.31;
    if (cp < 128) {
        const char c = static_cast<char>(cp);
        const std::string_view narrow = "il.,:;'|!", thin = "fjrtI()[]-", wide = "mwMW";
        if (narrow.find(c) != std::string_view::npos) return 0.28;
        if (thin.find(c) != std::string_view::npos) return 0.36;
        if (wide.find(c) != std::string_view::npos) return 0.86;
        if (c >= 'A' && c <= 'Z') return 0.66;
        if (c >= '0' && c <= '9') return 0.6;
        if (c >= 'a' && c <= 'z') return 0.55;
        return 0.55;
    }
    if (cp >= 0xC0 && cp <= 0x24F) return 0.57;     // les lettres accentuees
    return 0.8;
}

} // namespace

// ================================================================== couleurs ===
std::optional<std::uint32_t> parseRgb(std::string_view text) {
    const std::string t = trim(text);
    if (t.size() < 4 || t[0] != '#') return std::nullopt;
    const std::string_view hex = std::string_view(t).substr(1);
    for (const char c : hex)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    if (hex.size() == 3) {
        std::uint32_t v = 0;
        for (const char c : hex) {
            const auto d = static_cast<std::uint32_t>(std::strtoul(std::string(1, c).c_str(), nullptr, 16));
            v = (v << 8) | (d * 17);
        }
        return v;
    }
    if (hex.size() != 6 && hex.size() != 8) return std::nullopt;
    return static_cast<std::uint32_t>(std::strtoul(std::string(hex.substr(0, 6)).c_str(), nullptr, 16));
}

double relativeLuminance(std::uint32_t rgb) {
    const auto channel = [](std::uint32_t v) {
        const double c = static_cast<double>(v & 0xFFu) / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(rgb >> 16) + 0.7152 * channel(rgb >> 8) + 0.0722 * channel(rgb);
}

double contrastRatio(std::uint32_t a, std::uint32_t b) {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

std::string contrastText(double ratio) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f:1", ratio);
    std::string s = buf;
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}

double estimateTextWidth(std::string_view s, double sizePx) {
    double em = 0;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c;
        std::size_t len = 1;
        if (c >= 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12)
                                               | ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu); len = 4; }
        else if (c >= 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6)
                                                    | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu); len = 3; }
        else if (c >= 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu); len = 2; }
        em += widthOf(cp);
        i += len;
    }
    return em * sizePx;
}

QualityOptions qualityOptions(const Project& p, TextMeasure measure) {
    QualityOptions q;
    q.touchMin = p.config.touchMin;
    q.contrastMin = p.config.contrastMin;
    q.heavyObjects = p.config.heavyObjects;
    q.measure = std::move(measure);
    return q;
}

// ============================================================ la navigation ===
namespace {

const View* firstOfRole(const Project& p, std::string_view role) {
    for (const auto& v : p.views) if (v.role == role) return &v;
    return nullptr;
}

struct Reach {
    const Project&  p;
    std::set<Id>    reached;
    std::vector<Id> todo;
    bool            dynamic{false};

    void reach(const View* v) {
        if (v && (v->role == "vue" || v->role == "popup") && reached.insert(v->id).second) todo.push_back(v->id);
    }
    void name(std::string_view n) {
        const std::string t = trim(n);
        if (!t.empty()) reach(p.viewByName(t));
    }
    // Un script : chaque litteral qui nomme une vue y mene ; IHM_NAVIGUER(Nom)
    // sans litteral : une vue calculee.
    void script(std::string_view code) {
        for (const auto& lit : literals(code)) name(lit);
        const std::string u = upper(code);
        for (const std::string_view fn : {std::string_view("IHM_NAVIGUER"), std::string_view("IHM_POPUP"), std::string_view("IHM_CHANGER_POPUP")}) {
            for (auto at = u.find(fn); at != std::string::npos; at = u.find(fn, at + fn.size())) {
                std::size_t i = at + fn.size();
                if (at > 0 && (std::isalnum(static_cast<unsigned char>(u[at - 1])) || u[at - 1] == '_')) continue;
                while (i < u.size() && std::isspace(static_cast<unsigned char>(u[i]))) ++i;
                if (i >= u.size() || u[i] != '(') continue;
                ++i;
                while (i < u.size() && std::isspace(static_cast<unsigned char>(u[i]))) ++i;
                if (i < u.size() && u[i] != '\'' && u[i] != '"') dynamic = true;
            }
        }
    }
    void action(const Action& a) {
        switch (a.operation) {
            case Operation::Navigate: case Operation::Popup: case Operation::ChangePopup:
                name(a.target);
                break;
            case Operation::RunScript:
                script(a.value);
                break;
            default:
                break;
        }
    }
    // Ce que montre `c` (une vue, son modele, un symbole) quand `shown` est a l'ecran.
    void content(const View& c, const View& shown, int depth) {
        for (const auto& a : c.actions) action(a);
        for (const auto& s : c.scripts) script(s.body);
        for (const auto& o : c.objects) {
            for (const auto& a : o.actions) action(a);
            switch (o.kind) {
                case Kind::NavBar:
                    for (const auto& item : navItems(&p, o)) name(item.view);
                    break;
                case Kind::ZoneMap:
                    for (const auto& z : parseMapZones(o.text("mapZones"))) name(z.view);
                    break;
                case Kind::Breadcrumb:
                    // En hierarchie, chaque vue parente est un clic.
                    if (o.text("trail", "hi\xC3\xA9rarchie").rfind("hist", 0) != 0) {
                        std::set<Id> seen{shown.id};
                        for (const View* up = shown.upView != kNoId ? p.view(shown.upView) : nullptr; up && seen.insert(up->id).second;
                             up = up->upView != kNoId ? p.view(up->upView) : nullptr)
                            reach(up);
                    }
                    break;
                case Kind::LoginPanel:
                    name(o.text("afterLogin"));
                    break;
                case Kind::SymbolInstance:
                    if (depth < kMaxSymbolDepth)
                        if (const View* sym = symbolOf(p, o)) content(*sym, shown, depth + 1);
                    break;
                default:
                    break;
            }
        }
    }
    void run() {
        while (!todo.empty()) {
            const View* v = p.view(todo.back());
            todo.pop_back();
            if (!v) continue;
            content(*v, *v, 0);
            std::set<Id> seen{v->id};
            for (const View* t = v->templateView != kNoId ? p.view(v->templateView) : nullptr; t && seen.insert(t->id).second;
                 t = t->templateView != kNoId ? p.view(t->templateView) : nullptr)
                content(*t, *v, 0);
            if (v->showHeader)
                if (const View* h = v->header != kNoId ? p.view(v->header) : firstOfRole(p, "entete")) content(*h, *v, 0);
            if (v->showFooter)
                if (const View* f = v->footer != kNoId ? p.view(v->footer) : firstOfRole(p, "pied")) content(*f, *v, 0);
        }
    }
};

} // namespace

std::set<Id> reachableViews(const Project& p, bool* dynamic) {
    Reach r{p, {}, {}, false};
    if (p.config.startView != kNoId) r.reach(p.view(p.config.startView));
    else r.reach(firstOfRole(p, "vue"));
    for (const auto& g : p.security.groups) if (g.startView != kNoId) r.reach(p.view(g.startView));
    // Glisser pour changer de vue : de proche en proche, toutes les vues ordinaires.
    if (p.config.swipeNavigation)
        for (const auto& v : p.views) if (v.role == "vue") r.reach(&v);
    for (const auto& s : p.programs.scripts) r.script(s.body);
    for (const auto& f : p.programs.functions) r.script(f.body);
    r.run();
    if (dynamic) *dynamic = r.dynamic;
    return r.reached;
}

// ================================================================ la qualite ===
namespace {

struct Ctx {
    const Project&        p;
    const QualityOptions& opt;
    std::vector<Issue>&   out;
    std::string           code;    // tout le code du projet, en minuscules : Vue.Objet.Visible := ...

    void add(S s, const char* cat, const View& v, const Object* o, std::string key, std::string msg) {
        Issue i;
        i.severity = s;
        i.category = cat;
        i.view = v.id;
        i.object = o ? o->id : kNoId;
        i.property = std::move(key);
        i.message = std::move(msg);
        out.push_back(std::move(i));
    }
    [[nodiscard]] bool codeTouches(const Object& o, std::string_view prop) const {
        return code.find(lower(o.name + "." + std::string(prop))) != std::string::npos;
    }
    [[nodiscard]] double measure(std::string_view text, std::string_view font, double size) const {
        return opt.measure ? opt.measure(text, font, size) : estimateTextWidth(text, size);
    }
};

// Les objets d'une vue dans l'ordre du dessin : calque par calque, puis leur rang.
std::vector<const Object*> drawOrder(const View& v) {
    std::vector<std::pair<std::pair<int, std::size_t>, const Object*>> keyed;
    for (std::size_t i = 0; i < v.objects.size(); ++i) keyed.push_back({{v.layerRank(v.objects[i].layer), i}, &v.objects[i]});
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<const Object*> out;
    for (const auto& k : keyed) out.push_back(k.second);
    return out;
}

bool staticallyShown(const Object& o) {
    const auto vis = fixed(o, "visible");
    if (vis && !parseBool(*vis, true)) return false;
    if (const auto op = fixed(o, "opacity")) {
        double n = 100;
        if (parseNumber(*op, n) && n < 60) return false;
    }
    return true;
}

// Ce qui est sous le texte : l'objet plein le plus haut dessine avant lui et qui
// le contient ; sinon le fond de la vue. nullopt : une image, une video... on ne
// sait pas.
std::optional<std::uint32_t> backgroundUnder(const Project& p, const View& v, const Object& o, const std::vector<const Object*>& order) {
    const Box b = boundsOf(o);
    const double cx = b.cx(), cy = b.cy();
    const auto search = [&](const View& in, const std::vector<const Object*>& objs, bool stopAtSelf) -> std::optional<std::optional<std::uint32_t>> {
        const Object* found = nullptr;
        for (const Object* x : objs) {
            if (stopAtSelf && x == &o) break;
            if (x->kind == Kind::Line || x->kind == Kind::Group || !staticallyShown(*x)) continue;
            if (in.effectivelyHidden(*x) && !x->hidden) continue;       // une autre page d'onglets, un panneau replie
            if (!boundsOf(*x).contains(cx, cy)) continue;
            if (x->kind == Kind::Text && !fixedColor(*x, "fill")) continue;       // un texte sans fond laisse voir dessous
            found = x;
        }
        if (!found) return std::nullopt;
        switch (found->kind) {
            case Kind::Image: case Kind::Video: case Kind::AnimatedImage: case Kind::SymbolInstance: case Kind::Trend:
            case Kind::History: case Kind::ZoneMap: case Kind::QrCode:
                return std::optional<std::uint32_t>{};
            default:
                break;
        }
        if (const auto c = fixedColor(*found, "fill"); c && c->alpha >= 128) return std::optional<std::uint32_t>{c->rgb};
        return std::optional<std::uint32_t>{};
    };
    if (auto r = search(v, order, true)) return *r;
    // Le modele se dessine dessous.
    std::set<Id> seen{v.id};
    for (const View* t = v.templateView != kNoId ? p.view(v.templateView) : nullptr; t && seen.insert(t->id).second;
         t = t->templateView != kNoId ? p.view(t->templateView) : nullptr)
        if (auto r = search(*t, drawOrder(*t), false)) return *r;
    if (isSymbolView(v)) return std::nullopt;          // un symbole se pose sur un fond qu'on ne connait pas
    return parseRgb(v.background);
}

void checkContrast(Ctx& c, const View& v, const Object& o, const std::vector<const Object*>& order) {
    if (c.opt.contrastMin <= 0) return;
    const auto text = fixedColor(o, "textColor");
    if (!text) return;
    double size = 16;
    if (const auto fs = fixed(o, "fontSize")) (void)parseNumber(*fs, size);
    const double need = size >= 24 ? std::min(c.opt.contrastMin, 3.0) : c.opt.contrastMin;
    const auto judge = [&](std::uint32_t bg, const char* where) {
        const double r = contrastRatio(text->rgb, bg);
        if (r + 1e-9 < need)
            c.add(S::Warning, "Lisibilit\xC3\xA9", v, &o, "textColor",
                  "contraste faible " + std::string(where) + ": " + contrastText(r) + " (" + contrastText(need)
                      + " au moins) - le texte se lit mal sur son fond");
    };
    switch (o.kind) {
        case Kind::Button: case Kind::PushButton: case Kind::ExportButton: case Kind::LogoutButton: case Kind::LoginMenuButton:
        case Kind::InputField: case Kind::ComboBox: {
            if (const auto f = fixedColor(o, "fill"); f && f->alpha >= 128) judge(f->rgb, "");
            break;
        }
        case Kind::IlluminatedButton: {
            // Eteint : le texte a sa couleur ; allume, il fonce (90 % vers #0E1218) sur la couleur du voyant.
            if (const auto off = fixedColor(o, "colorOff")) judge(off->rgb, "(\xC3\xA9teint) ");
            if (const auto on = fixedColor(o, "colorOn")) {
                const auto mixed = [&](int shift) {
                    const double t = static_cast<double>((text->rgb >> shift) & 0xFFu), d = static_cast<double>((0x0E1218u >> shift) & 0xFFu);
                    return static_cast<std::uint32_t>(std::lround(t + (d - t) * 0.9)) << shift;
                };
                const std::uint32_t lit = mixed(16) | mixed(8) | mixed(0);
                const double r = contrastRatio(lit, on->rgb);
                if (r + 1e-9 < need)
                    c.add(S::Warning, "Lisibilit\xC3\xA9", v, &o, "colorOn",
                          "contraste faible (allum\xC3\xA9) : " + contrastText(r) + " (" + contrastText(need)
                              + " au moins) - le texte fonc\xC3\xA9 se lit mal sur la couleur du voyant");
            }
            break;
        }
        case Kind::Text: {
            if (literalPart(o.text("text")).find_first_not_of(" \t") == std::string::npos && !animated(o, "text")) return;   // rien d'ecrit
            if (const auto f = fixedColor(o, "fill"); f && f->alpha >= 128) { judge(f->rgb, ""); break; }
            if (const auto bg = backgroundUnder(c.p, v, o, order)) judge(*bg, "");
            break;
        }
        case Kind::CheckBox: case Kind::RadioGroup: case Kind::Switch: {
            if (const auto bg = backgroundUnder(c.p, v, o, order)) judge(*bg, "");
            break;
        }
        default:
            break;
    }
}

void checkOverflow(Ctx& c, const View& v, const Object& o) {
    const auto raw = fixed(o, "text");
    if (!raw) return;
    const std::string shown = unescapeText(literalPart(*raw));
    if (shown.find_first_not_of(" \t\n") == std::string::npos) return;
    const auto wv = fixed(o, "w"), hv = fixed(o, "h");
    double w = 0, h = 0, size = 16;
    if (!wv || !hv || !parseNumber(*wv, w) || !parseNumber(*hv, h) || w <= 0 || h <= 0) return;
    if (const auto fs = fixed(o, "fontSize")) {
        if (!parseNumber(*fs, size)) return;
    } else if (o.find("fontSize")) {
        return;       // une taille animee
    }
    const std::string font = trim(o.text("font", "Sans"));
    double avail = 0;
    bool wrap = o.flag("wrap");
    bool cut = false;              // le texte est raccourci ("...") plutot que de deborder
    switch (o.kind) {
        case Kind::Text: avail = o.text("align", "gauche") == "centre" ? w : w - 4; break;
        case Kind::Button: case Kind::PushButton: case Kind::IlluminatedButton: avail = w - 8; break;
        case Kind::CheckBox: {
            const double s = std::clamp(h * 0.62, 10.0, 26.0);
            avail = w - s - 12;
            wrap = false;
            cut = true;
            break;
        }
        default:
            return;
    }
    if (avail <= 0) return;
    const double lineH = size * 1.2;
    const auto lines = linesOf(shown);
    if (wrap) {
        // Le retour a la ligne automatique (a w - 8) : la hauteur deborde-t-elle ?
        std::size_t rows = 0;
        for (const auto& para : lines) {
            std::string line;
            std::size_t n = 1;
            std::size_t at = 0;
            while (at < para.size()) {
                while (at < para.size() && para[at] == ' ') ++at;
                if (at >= para.size()) break;
                auto e = para.find(' ', at);
                if (e == std::string::npos) e = para.size();
                const std::string word = para.substr(at, e - at);
                const std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && c.measure(candidate, font, size) > w - 8) { ++n; line = word; }
                else line = candidate;
                at = e;
            }
            rows += n;
        }
        const double need = static_cast<double>(rows) * lineH;
        if (need > h + 2)
            c.add(S::Warning, "Lisibilit\xC3\xA9", v, &o, "text",
                  "le texte d\xC3\xA9" "borde en bas : " + std::to_string(rows) + " lignes, ~" + px(need) + " px pour " + px(h)
                      + " px de haut (" + quoted(shown) + ")");
        return;
    }
    double widest = 0;
    std::string culprit;
    for (const auto& line : lines) {
        const double lw = c.measure(line, font, size);
        if (lw > widest) { widest = lw; culprit = line; }
    }
    if (widest > avail + 1) {
        c.add(S::Warning, "Lisibilit\xC3\xA9", v, &o, "text",
              std::string(cut ? "le texte est coup\xC3\xA9 : ~" : "le texte d\xC3\xA9" "borde : ~") + px(widest) + " px pour " + px(avail)
                  + " px de place (" + quoted(culprit) + ")");
        return;
    }
    const double tall = static_cast<double>(lines.size()) * lineH;
    if (lines.size() > 1 && tall > h + 2)
        c.add(S::Warning, "Lisibilit\xC3\xA9", v, &o, "text",
              "le texte d\xC3\xA9" "borde en hauteur : " + std::to_string(lines.size()) + " lignes, ~" + px(tall) + " px pour " + px(h) + " px");
}

bool clickableKind(Kind k) {
    switch (k) {
        case Kind::Button: case Kind::PushButton: case Kind::Switch: case Kind::IlluminatedButton: case Kind::Selector:
        case Kind::Slider: case Kind::Knob: case Kind::ComboBox: case Kind::CheckBox: case Kind::RadioGroup:
        case Kind::DateTimePicker: case Kind::WeeklySchedule: case Kind::InputField: case Kind::LogoutButton:
        case Kind::LoginMenuButton: case Kind::SystemButton: case Kind::ExportButton: case Kind::NavBar: case Kind::TabContainer:
        case Kind::LanguageSelector: case Kind::ThemeSelector:
        case Kind::PlcDiagnostic:   // lot 14 : ses deux boutons
            return true;
        default:
            return false;
    }
}

void checkTouch(Ctx& c, const View& v, const Object& o) {
    const double min = c.opt.touchMin;
    if (min <= 0 || o.kind == Kind::Line) return;
    const bool byAction = std::any_of(o.actions.begin(), o.actions.end(), [](const Action& a) {
        return a.trigger == Trigger::Click || a.trigger == Trigger::DoubleClick || a.trigger == Trigger::LongPress;
    });
    if (!clickableKind(o.kind) && !byAction) return;
    if (animated(o, "w") || animated(o, "h")) return;
    const double w = o.number("w"), h = o.number("h");
    const std::string need = " (" + px(min) + " px au moins, Configuration > Projet)";
    // Les parties : le plus petit cote de chacune.
    const auto parts = [&](const std::vector<Box>& boxes, const char* what) {
        double pw = 1e9, ph = 1e9;
        for (const auto& b : boxes) {
            if (b.w <= 0 || b.h <= 0) continue;
            if (std::min(b.w, b.h) < std::min(pw, ph)) { pw = b.w; ph = b.h; }
        }
        if (pw < 1e9 && std::min(pw, ph) + 0.5 < min)
            c.add(S::Info, "Ergonomie", v, &o, {}, std::string(what) + " font " + px(pw) + " \xC3\x97 " + px(ph)
                                                        + " px : petits pour un doigt" + need);
    };
    switch (o.kind) {
        case Kind::RadioGroup:
            parts(radioBoxes(o, w, h, choicesOf(o).size()), "les choix");
            return;
        case Kind::DateTimePicker: {
            const auto l = pickerLayout(o, w, h);
            std::vector<Box> boxes;
            for (const auto& f : l.fields) { boxes.push_back(f.up); boxes.push_back(f.down); }
            parts(boxes, "les fl\xC3\xA8" "ches");
            return;
        }
        case Kind::WeeklySchedule: {
            const auto l = scheduleLayout(o, w, h, resolutionMinutes(o.text("resolution", "30 min")));
            parts({Box{0, 0, l.cellW, l.cellH}}, "les cases");
            return;
        }
        case Kind::NavBar:
            parts(navBarLayout(o, w, h, navItems(&c.p, o).size()).items, "les boutons");
            return;
        case Kind::TabContainer:
            parts(tabLayout(o, w, h, tabLabels(o).size()).tabs, "les onglets");
            return;
        case Kind::Selector:
            if (o.text("style", "rotatif") != "rotatif") {
                parts(selectorLayout(o, w, h, choicesOf(o).size()).labels, "les positions");
                return;
            }
            break;
        default:
            break;
    }
    if (std::min(w, h) + 0.5 < min)
        c.add(S::Warning, "Ergonomie", v, &o, "w", "cible de " + px(w) + " \xC3\x97 " + px(h) + " px : trop petite pour un doigt" + need);
}

void checkPlacement(Ctx& c, const View& v, const Object& o, bool& never) {
    never = false;
    if (o.hidden)
        c.add(S::Info, "Visibilit\xC3\xA9", v, &o, {}, "cach\xC3\xA9 dans l'\xC3\xA9" "diteur seulement (l'\xC5\x93il du calque ou de l'objet) : il se verra en marche");
    const auto vis = fixed(o, "visible");
    if (vis && !parseBool(*vis, true) && !c.codeTouches(o, "visible")) {
        c.add(S::Warning, "Visibilit\xC3\xA9", v, &o, "visible",
              "toujours invisible en marche : Visible \xC3\xA0 FAUX, sans expression ni script qui le change");
        never = true;
        return;
    }
    if (const auto op = fixed(o, "opacity")) {
        double n = 100;
        if (parseNumber(*op, n) && n <= 0 && !c.codeTouches(o, "opacity")) {
            c.add(S::Warning, "Visibilit\xC3\xA9", v, &o, "opacity", "toujours invisible en marche : opacit\xC3\xA9 nulle, sans expression");
            never = true;
            return;
        }
    }
    const bool geometryFixed = !animated(o, "x") && !animated(o, "y") && !animated(o, "w") && !animated(o, "h") && !animated(o, "rot");
    if (!geometryFixed) return;
    if (o.kind != Kind::Line && o.kind != Kind::Group && (o.number("w") <= 0 || o.number("h") <= 0)) {
        c.add(S::Warning, "Visibilit\xC3\xA9", v, &o, "w", "taille nulle (" + px(o.number("w")) + " \xC3\x97 " + px(o.number("h")) + " px) : il ne se voit pas");
        never = true;
        return;
    }
    // Hors de la vue : les objets poses sur la vue (ou dans un groupe, un cadre),
    // pas ceux d'un panneau defilant (son contenu est fait pour deborder).
    for (Id pid = o.parent; pid != kNoId;) {
        const Object* parent = v.object(pid);
        if (!parent) break;
        if (parent->kind == Kind::ScrollPanel) return;
        pid = parent->parent;
    }
    if (o.kind == Kind::Group) return;          // ses objets le disent
    const Box b = boundsOf(o);
    const double W = v.width, H = v.height;
    // Une popup ne rogne pas : ce qui sort de son cadre se dessine sur la vue du
    // dessous, sans son fond. Une vue remplit l'ecran : ce qui en sort ne se voit pas.
    const bool popup = v.role == "popup";
    if (b.right() <= 0 || b.bottom() <= 0 || b.x >= W || b.y >= H) {
        c.add(S::Warning, "Visibilit\xC3\xA9", v, &o, "x",
              std::string(popup ? "enti\xC3\xA8rement hors du cadre de la popup (" : "enti\xC3\xA8rement hors de la vue (") + px(b.x) + ", " + px(b.y)
                  + (popup ? " pour une popup de " : " pour une vue de ") + px(W) + " \xC3\x97 " + px(H)
                  + (popup ? ") : il se dessine sur la vue du dessous, sans le fond de la popup" : ") : sur l'\xC3\xA9" "cran de l'IHM, il ne se verra pas"));
        if (!popup) never = true;
        return;
    }
    const double left = -b.x, top = -b.y, right = b.right() - W, bottom = b.bottom() - H;
    const double worst = std::max({left, top, right, bottom});
    if (worst > 2) {
        const char* side = worst == left ? "\xC3\xA0 gauche" : worst == top ? "en haut" : worst == right ? "\xC3\xA0 droite" : "en bas";
        c.add(S::Info, "Visibilit\xC3\xA9", v, &o, "x",
              std::string(popup ? "d\xC3\xA9passe du cadre de la popup de " : "d\xC3\xA9passe de la vue de ") + px(worst) + " px " + side
                  + (popup ? " : cette partie se dessine sur la vue du dessous" : " : la partie dehors est coup\xC3\xA9" "e"));
    }
}

} // namespace

void checkQuality(const Project& p, const QualityOptions& opt, std::vector<Issue>& out) {
    Ctx c{p, opt, out, {}};
    // Le code qui peut changer un objet en marche (Vue.Objet.Visible := TRUE).
    for (const auto& s : p.programs.scripts) c.code += lower(s.body) + "\n";
    for (const auto& f : p.programs.functions) c.code += lower(f.body) + "\n";
    for (const auto& v : p.views) {
        for (const auto& s : v.scripts) c.code += lower(s.body) + "\n";
        const auto actions = [&](const std::vector<Action>& list) {
            for (const auto& a : list) c.code += lower(a.target) + " " + lower(a.value) + "\n";
        };
        actions(v.actions);
        for (const auto& o : v.objects) actions(o.actions);
    }

    for (const auto& v : p.views) {
        const auto order = drawOrder(v);
        std::size_t expressions = 0;
        for (const Object* po : order) {
            const Object& o = *po;
            for (const auto& pr : o.props) expressions += !pr.expr.empty();
            bool never = false;
            checkPlacement(c, v, o, never);
            if (never) continue;
            if (v.effectivelyHidden(o) && !o.hidden) continue;       // une autre page, un panneau replie : on le verra ouvert
            checkOverflow(c, v, o);
            checkContrast(c, v, o, order);
            if (!isSymbolView(v)) checkTouch(c, v, o);          // un symbole se pose a l'echelle de son instance
        }
        if (opt.heavyObjects > 0 && v.objects.size() > static_cast<std::size_t>(opt.heavyObjects)) {
            Issue i;
            i.severity = S::Warning;
            i.category = "Performance";
            i.view = v.id;
            i.message = "vue charg\xC3\xA9" "e : " + std::to_string(v.objects.size()) + " objets (dont " + std::to_string(expressions)
                      + " expressions) - au-del\xC3\xA0 de " + std::to_string(opt.heavyObjects)
                      + ", le dessin et le cycle ralentissent : la d\xC3\xA9" "couper, ou r\xC3\xA9utiliser un symbole";
            out.push_back(std::move(i));
        }
    }

    // Les vues qu'on n'atteint pas.
    bool dynamic = false;
    const auto reached = reachableViews(p, &dynamic);
    for (const auto& v : p.views) {
        if ((v.role != "vue" && v.role != "popup") || reached.count(v.id)) continue;
        Issue i;
        i.severity = dynamic ? S::Info : S::Warning;
        i.category = "Navigation";
        i.view = v.id;
        i.message = v.role == "popup"
                        ? "popup jamais ouverte : aucune action ni script ne l'ouvre depuis les vues atteintes"
                        : "vue jamais atteinte : aucune action, barre de navigation, zone, fil d'Ariane ni script n'y m\xC3\xA8ne depuis la vue de d\xC3\xA9marrage";
        if (dynamic) i.message += " (un script ouvre une vue dont le nom se calcule : \xC3\xA0 v\xC3\xA9rifier)";
        out.push_back(std::move(i));
    }
}

} // namespace hmi
