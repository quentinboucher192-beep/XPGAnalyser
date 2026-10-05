#include "HmiNavigation.hpp"

#include "HmiControls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <tuple>

namespace hmi {

namespace {

// "a;;b" -> {"a", "", "b"} : les vides gardent leur rang (des libelles).
std::vector<std::string> splitKeepEmpty(std::string_view text, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : text) {
        if (ch == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out.push_back(cur);
    for (auto& s : out) {
        const auto a = s.find_first_not_of(" \t\r\n");
        const auto b = s.find_last_not_of(" \t\r\n");
        s = a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
    }
    return out;
}

std::string trimmed(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Le nombre de caracteres (UTF-8 : les octets de suite ne comptent pas).
std::size_t glyphs(std::string_view s) {
    std::size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

double clampd(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

bool ordinaryView(const View& v) { return v.role == "vue"; }

} // namespace

// ============================================================ barre de navigation ===
std::string viewCaption(std::string_view name) {
    std::string s(name);
    if (s.size() > 4 && iequal(std::string_view(s).substr(0, 4), "vue_")) s = s.substr(4);
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}

std::vector<NavItem> navItems(const Project* project, const Object& bar) {
    std::vector<std::string> names = listItems(bar.text("views"));
    if (names.empty() && project)
        for (const auto& v : project->views)
            if (ordinaryView(v)) names.push_back(v.name);
    const auto labels = splitKeepEmpty(bar.text("labels"), ';');
    std::vector<NavItem> out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        NavItem it;
        it.view = names[i];
        it.label = i < labels.size() && !labels[i].empty() ? labels[i] : viewCaption(names[i]);
        out.push_back(std::move(it));
    }
    return out;
}

NavLayout navBarLayout(const Object& o, double w, double h, std::size_t count) {
    NavLayout l;
    l.vertical = o.text("orientation", "horizontale").rfind("vert", 0) == 0;
    l.fontSize = clampd(o.number("fontSize", 15), 7, 60);
    const double gap = clampd(o.number("gap", 4), 0, 40);
    const bool buttons = o.text("tabStyle", "onglets") == "boutons";
    const double pad = buttons ? 4 : 0;
    double x = pad, y = pad;
    double availW = std::max(0.0, w - 2 * pad), availH = std::max(0.0, h - 2 * pad);
    if (o.flag("backForward")) {
        if (!l.vertical) {
            const double side = std::min(availH, 46.0);
            l.back = {x, y, side, availH};
            l.forward = {x + side + 2, y, side, availH};
            x += 2 * side + 2 + gap;
            availW = std::max(0.0, availW - (2 * side + 2 + gap));
        } else {
            const double side = std::min(availW / 2 - 1, 46.0);
            l.back = {x, y, std::max(0.0, availW / 2 - 1), side};
            l.forward = {x + availW / 2 + 1, y, std::max(0.0, availW / 2 - 1), side};
            y += side + gap;
            availH = std::max(0.0, availH - side - gap);
        }
    }
    if (count == 0) return l;
    const double n = static_cast<double>(count);
    if (!l.vertical) {
        const double each = std::max(0.0, (availW - gap * (n - 1)) / n);
        for (std::size_t i = 0; i < count; ++i) l.items.push_back({x + static_cast<double>(i) * (each + gap), y, each, availH});
    } else {
        const double each = std::min(std::max(0.0, (availH - gap * (n - 1)) / n), std::max(28.0, l.fontSize * 3.0));
        for (std::size_t i = 0; i < count; ++i) l.items.push_back({x, y + static_cast<double>(i) * (each + gap), availW, each});
    }
    return l;
}

std::string navBarHit(const Object& o, double w, double h, double x, double y, std::size_t count) {
    const auto l = navBarLayout(o, w, h, count);
    if (l.back.w > 0 && l.back.contains(x, y)) return "precedent";
    if (l.forward.w > 0 && l.forward.contains(x, y)) return "suivant";
    for (std::size_t i = 0; i < l.items.size(); ++i)
        if (l.items[i].contains(x, y)) return "vue:" + std::to_string(i);
    return {};
}

// ================================================================ fil d'Ariane ===
double approxTextWidth(std::string_view text, double fontSize) {
    return static_cast<double>(glyphs(text)) * fontSize * 0.56;
}

std::vector<Crumb> breadcrumbTrail(const Project& p, const Object& o, std::string_view current,
                                   const std::vector<std::string>& history, std::string_view home) {
    std::vector<Crumb> out;
    const bool byHistory = o.text("trail", "hi\xC3\xA9rarchie").rfind("hist", 0) == 0;
    if (byHistory) {
        const int n = static_cast<int>(history.size());
        for (int i = 0; i < n; ++i) out.push_back({history[static_cast<std::size_t>(i)], viewCaption(history[static_cast<std::size_t>(i)]), n - i});
        if (!current.empty()) out.push_back({std::string(current), viewCaption(current), 0});
    } else {
        std::vector<const View*> chain;
        const View* v = p.viewByName(current);
        std::set<Id> seen;
        while (v && !seen.count(v->id) && chain.size() < 32) {
            seen.insert(v->id);
            chain.push_back(v);
            v = v->upView != kNoId ? p.view(v->upView) : nullptr;
        }
        std::reverse(chain.begin(), chain.end());
        if (o.flag("home", true) && !home.empty()) {
            bool present = false;
            for (const auto* c : chain) present = present || iequal(c->name, home);
            if (!present && p.viewByName(home)) out.push_back({std::string(home), viewCaption(home), -1});
        }
        for (const auto* c : chain) out.push_back({c->name, viewCaption(c->name), -1});
        if (chain.empty() && !current.empty()) out.push_back({std::string(current), viewCaption(current), -1});
    }
    const auto maxItems = static_cast<std::size_t>(std::max(1.0, o.number("maxItems", 6)));
    if (out.size() > maxItems) out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(out.size() - maxItems));
    return out;
}

CrumbLayout breadcrumbLayout(const Object& o, double w, double h, const std::vector<Crumb>& trail) {
    CrumbLayout l;
    l.fontSize = clampd(o.number("fontSize", 15), 7, 60);
    const std::string sep = o.text("separator", "\xE2\x80\xBA");
    const double sepW = approxTextWidth(sep, l.fontSize) + 14;
    double x = 6;
    for (std::size_t i = 0; i < trail.size(); ++i) {
        if (i > 0) {
            l.separators.push_back({x, 0, sepW, h});
            x += sepW;
        }
        const double cw = approxTextWidth(trail[i].label, l.fontSize) + 8;
        l.crumbs.push_back({x, 0, cw, h});
        x += cw;
    }
    // Trop long : le debut sort a gauche (la vue courante reste visible).
    const double over = x + 6 - w;
    if (over > 0) {
        for (auto& b : l.crumbs) b.x -= over;
        for (auto& b : l.separators) b.x -= over;
        for (auto& b : l.crumbs) if (b.x < 0) b.w = 0;
        for (auto& b : l.separators) if (b.x < 0) b.w = 0;
    }
    return l;
}

std::string breadcrumbHit(const Object& o, double w, double h, double x, double y, const std::vector<Crumb>& trail) {
    const auto l = breadcrumbLayout(o, w, h, trail);
    for (std::size_t i = 0; i < l.crumbs.size(); ++i)
        if (l.crumbs[i].w > 0 && l.crumbs[i].contains(x, y)) return "etape:" + std::to_string(i);
    return {};
}

// ============================================================ conteneur a onglets ===
std::vector<std::string> tabLabels(const Object& o) {
    auto labels = listItems(o.text("tabs"));
    if (labels.empty()) labels.push_back("Page 1");
    return labels;
}

int tabPageOf(const Object& child) { return static_cast<int>(std::lround(std::max(1.0, child.number("tabPage", 1)))); }

int shownTabPage(const Object& tabs) {
    const int n = static_cast<int>(tabLabels(tabs).size());
    return std::clamp(static_cast<int>(std::lround(tabs.number("page", 1))), 1, std::max(1, n));
}

TabLayout tabLayout(const Object& o, double w, double h, std::size_t count) {
    TabLayout l;
    l.fontSize = clampd(o.number("fontSize", 14), 7, 60);
    const double th = clampd(o.number("tabHeight", 34), 16, std::max(16.0, h / 2));
    const bool bottom = o.text("tabPosition", "haut") == "bas";
    const double y = bottom ? h - th : 0;
    const auto labels = tabLabels(o);
    std::vector<double> widths;
    double total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string label = i < labels.size() ? labels[i] : std::string{};
        const double tw = std::max(64.0, approxTextWidth(label, l.fontSize) + 30);
        widths.push_back(tw);
        total += tw + 2;
    }
    const double scale = total > w && total > 0 ? w / total : 1.0;
    double x = 0;
    for (double tw : widths) {
        l.tabs.push_back({x, y, tw * scale, th});
        x += (tw + 2) * scale;
    }
    l.content = bottom ? Box{0, 0, w, std::max(0.0, h - th)} : Box{0, th, w, std::max(0.0, h - th)};
    return l;
}

std::string tabHit(const Object& o, double w, double h, double x, double y, std::size_t count) {
    const auto l = tabLayout(o, w, h, count);
    for (std::size_t i = 0; i < l.tabs.size(); ++i)
        if (l.tabs[i].contains(x, y)) return "onglet:" + std::to_string(i + 1);
    return {};
}

// ================================================================= cadre ===
FrameLayout frameLayout(const Object& o, double w, double h) {
    FrameLayout l;
    const double fs = clampd(o.number("fontSize", 14), 7, 60);
    const std::string title = o.text("title");
    const bool band = o.text("titleStyle", "bordure") == "bandeau";
    const std::string align = o.text("titleAlign", "gauche");
    const double tw = std::min(std::max(0.0, w - 24), approxTextWidth(title, fs) + 12);
    if (band) {
        const double bh = std::min(h, fs * 2.0);
        l.band = {0, 0, w, bh};
        l.title = {10, 0, std::max(0.0, w - 20), bh};
        l.top = 0;
        l.content = {0, bh, w, std::max(0.0, h - bh)};
    } else {
        const double th = title.empty() ? 0 : fs * 1.4;
        double tx = 12;
        if (align == "centre") tx = (w - tw) / 2;
        else if (align == "droite") tx = w - 12 - tw;
        l.title = {tx, 0, tw, th};
        l.top = th / 2;
        l.content = {0, th, w, std::max(0.0, h - th)};
    }
    return l;
}

// ======================================================== panneau defilant ===
ScrollLayout scrollLayout(const View& v, const Object& panel) {
    ScrollLayout l;
    const Box pb = panel.box();
    const std::string dir = panel.text("scrollDirection", "verticale");
    const bool canV = dir != "horizontale", canH = dir != "verticale";
    double cw = panel.number("contentWidth", 0), ch = panel.number("contentHeight", 0);
    // En marche, la mise en page a deja mesure le contenu (avant de le decaler).
    if (const auto* sw = panel.find("shownContentWidth")) (void)parseNumber(sw->value, cw);
    if (const auto* sh = panel.find("shownContentHeight")) (void)parseNumber(sh->value, ch);
    if (cw <= 0 || ch <= 0) {
        double mw = 0, mh = 0;
        for (Id d : v.descendantsOf(panel.id)) {
            const auto* c = v.object(d);
            if (!c || c->kind == Kind::Group) continue;
            const Box b = c->box();
            mw = std::max(mw, b.right() - pb.x);
            mh = std::max(mh, b.bottom() - pb.y);
        }
        if (cw <= 0) cw = mw + 8;
        if (ch <= 0) ch = mh + 8;
    }
    l.contentW = canH ? std::max(cw, pb.w) : pb.w;
    l.contentH = canV ? std::max(ch, pb.h) : pb.h;
    const bool bars = panel.flag("showScrollbar", true);
    double vw = pb.w, vh = pb.h;
    bool needV = canV && l.contentH > vh + 0.5, needH = canH && l.contentW > vw + 0.5;
    if (bars && needV) vw -= kScrollBarW;
    if (bars && canH && !needH && l.contentW > vw + 0.5) needH = true;
    if (bars && needH) vh -= kScrollBarW;
    if (bars && canV && !needV && l.contentH > vh + 0.5) {
        needV = true;
        vw -= kScrollBarW;
    }
    vw = std::max(0.0, vw);
    vh = std::max(0.0, vh);
    l.viewport = {0, 0, vw, vh};
    l.maxX = needH ? std::max(0.0, l.contentW - vw) : 0.0;
    l.maxY = needV ? std::max(0.0, l.contentH - vh) : 0.0;
    l.scrollX = clampd(panel.number("scrollX", 0), 0, l.maxX);
    l.scrollY = clampd(panel.number("scrollY", 0), 0, l.maxY);
    if (bars && needV && vh > 0) {
        l.vbar = {vw, 0, kScrollBarW, vh};
        const double th = std::max(24.0, std::min(vh, vh * vh / std::max(vh, l.contentH)));
        const double ty = l.maxY > 0 ? (vh - th) * l.scrollY / l.maxY : 0;
        l.vthumb = {vw + 2, ty, kScrollBarW - 4, th};
    }
    if (bars && needH && vw > 0) {
        l.hbar = {0, vh, vw, kScrollBarW};
        const double tw = std::max(24.0, std::min(vw, vw * vw / std::max(vw, l.contentW)));
        const double tx = l.maxX > 0 ? (vw - tw) * l.scrollX / l.maxX : 0;
        l.hthumb = {tx, vh + 2, tw, kScrollBarW - 4};
    }
    return l;
}

std::string scrollHit(const ScrollLayout& l, double x, double y) {
    char buf[48];
    if (l.vbar.w > 0 && l.vbar.contains(x, y)) {
        const double run = l.vbar.h - l.vthumb.h;
        const double f = run > 0 ? clampd((y - l.vthumb.h / 2) / run, 0, 1) : 0;
        std::snprintf(buf, sizeof buf, "vbarre:%.4f", f);
        return buf;
    }
    if (l.hbar.w > 0 && l.hbar.contains(x, y)) {
        const double run = l.hbar.w - l.hthumb.w;
        const double f = run > 0 ? clampd((x - l.hthumb.w / 2) / run, 0, 1) : 0;
        std::snprintf(buf, sizeof buf, "hbarre:%.4f", f);
        return buf;
    }
    return {};
}

// ======================================================== panneau repliable ===
double collapsibleHeaderHeight(const Object& o) {
    return clampd(o.number("headerHeight", 34), 16, std::max(16.0, o.number("h", 34)));
}

std::string collapsibleHit(const Object& o, double w, double, double x, double y) {
    return x >= 0 && x <= w && y >= 0 && y <= collapsibleHeaderHeight(o) ? std::string("entete") : std::string{};
}

// ============================================================== plan a zones ===
std::vector<std::pair<double, double>> parseZonePoints(std::string_view text) {
    std::vector<std::pair<double, double>> out;
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) tokens.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    const auto numbers = [](const std::string& t) {
        std::vector<double> v;
        std::size_t start = 0;
        while (start <= t.size()) {
            const auto comma = t.find(',', start);
            const std::string part = t.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            double d = 0;
            if (!parseNumber(part, d)) return std::vector<double>{};
            v.push_back(d);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return v;
    };
    // "x,y,l,h" seul : un rectangle.
    if (tokens.size() == 1) {
        const auto v = numbers(tokens[0]);
        if (v.size() == 4) {
            out = {{v[0], v[1]}, {v[0] + v[2], v[1]}, {v[0] + v[2], v[1] + v[3]}, {v[0], v[1] + v[3]}};
            return out;
        }
    }
    for (const auto& t : tokens) {
        const auto v = numbers(t);
        if (v.size() != 2) return {};
        out.emplace_back(v[0], v[1]);
    }
    return out;
}

std::string formatZonePoints(const std::vector<std::pair<double, double>>& pts) {
    std::string s;
    for (const auto& [x, y] : pts) {
        if (!s.empty()) s += ' ';
        s += formatNumber(std::round(x * 10) / 10) + "," + formatNumber(std::round(y * 10) / 10);
    }
    return s;
}

std::vector<MapZone> parseMapZones(std::string_view text) {
    std::vector<MapZone> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        const std::string line = trimmed(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
        if (!line.empty()) {
            const auto parts = splitKeepEmpty(line, '|');
            MapZone z;
            z.name = parts.size() > 0 ? parts[0] : std::string{};
            z.points = parts.size() > 1 ? parseZonePoints(parts[1]) : std::vector<std::pair<double, double>>{};
            z.group = parts.size() > 2 ? parts[2] : std::string{};
            z.view = parts.size() > 3 ? parts[3] : std::string{};
            z.color = parts.size() > 4 ? parts[4] : std::string{};
            if (!z.name.empty() || !z.points.empty()) out.push_back(std::move(z));
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return out;
}

std::string formatMapZones(const std::vector<MapZone>& zones) {
    std::string s;
    for (const auto& z : zones) {
        if (!s.empty()) s += '\n';
        s += z.name + " | " + formatZonePoints(z.points) + " | " + z.group + " | " + z.view + " | " + z.color;
        while (!s.empty() && (s.back() == ' ' || s.back() == '|')) s.pop_back();
    }
    return s;
}

bool pointInPolygon(const std::vector<std::pair<double, double>>& poly, double x, double y) {
    bool inside = false;
    const std::size_t n = poly.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto [xi, yi] = poly[i];
        const auto [xj, yj] = poly[j];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

std::pair<double, double> zoneCenter(const MapZone& z, double w, double h) {
    if (z.points.empty()) return {0, 0};
    double sx = 0, sy = 0;
    for (const auto& [x, y] : z.points) {
        sx += x;
        sy += y;
    }
    const double n = static_cast<double>(z.points.size());
    return {sx / n * w / 100.0, sy / n * h / 100.0};
}

Box zoneBounds(const MapZone& z, double w, double h) {
    if (z.points.empty()) return {};
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& [x, y] : z.points) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
    return {x0 * w / 100.0, y0 * h / 100.0, (x1 - x0) * w / 100.0, (y1 - y0) * h / 100.0};
}

std::string zoneMapHit(const Object& o, double w, double h, double x, double y) {
    if (w <= 0 || h <= 0) return {};
    const auto zones = parseMapZones(o.text("mapZones"));
    const double px = x / w * 100.0, py = y / h * 100.0;
    for (std::size_t i = zones.size(); i-- > 0;)
        if (pointInPolygon(zones[i].points, px, py)) return "zone:" + std::to_string(i);
    return {};
}

// ============================================================ mise en page ===
bool childClip(const View& v, const Object& holder, Box& out) {
    (void)v;
    if (std::fabs(holder.rotation()) > 1e-9) return false;
    const Box b = holder.box();
    Box local{};
    switch (holder.kind) {
        case Kind::Container:
            if (!holder.flag("clip", true)) return false;
            local = {0, 0, b.w, b.h};
            break;
        case Kind::Frame:
            if (!holder.flag("clip", false)) return false;
            local = frameLayout(holder, b.w, b.h).content;
            break;
        case Kind::TabContainer:
            local = tabLayout(holder, b.w, b.h, tabLabels(holder).size()).content;
            break;
        case Kind::ScrollPanel:
            local = scrollLayout(v, holder).viewport;
            break;
        case Kind::CollapsiblePanel: {
            const double hh = collapsibleHeaderHeight(holder);
            local = {0, hh, b.w, std::max(0.0, b.h - hh)};
            break;
        }
        default:
            return false;
    }
    out = {b.x + local.x, b.y + local.y, local.w, local.h};
    return true;
}

bool clippedAt(const View& v, const Object& o, double x, double y, Id except) {
    Id p = o.parent;
    for (int guard = 0; p != kNoId && guard < 64; ++guard) {
        const auto* g = v.object(p);
        if (!g) break;
        Box clip{};
        if (g->id != except && childClip(v, *g, clip) && !clip.contains(x, y)) return true;
        p = g->parent;
    }
    return false;
}

bool adopt(View& v, Id id, Id holder) {
    Object* o = v.object(id);
    if (!o || id == holder) return false;
    const Object* h = holder != kNoId ? v.object(holder) : nullptr;
    if (holder != kNoId && !h) return false;
    // Pas dans un de ses propres descendants.
    for (Id p = holder; p != kNoId;) {
        if (p == id) return false;
        const auto* po = v.object(p);
        p = po ? po->parent : kNoId;
    }
    const int page = h && h->kind == Kind::TabContainer ? shownTabPage(*h) : 0;
    if (o->parent == holder && (page == 0 || tabPageOf(*o) == page)) return false;
    o->parent = holder;
    if (page > 0) o->setNumber("tabPage", page);
    if (!h) return true;
    // Au-dessus de son parent : l'objet et ses descendants passent dans son calque,
    // en haut de la pile.
    const Id layer = h->layer;
    std::vector<Id> moving{id};
    for (Id d : v.descendantsOf(id)) moving.push_back(d);
    std::vector<Object> kept, moved;
    for (auto& x : v.objects) (std::find(moving.begin(), moving.end(), x.id) != moving.end() ? moved : kept).push_back(std::move(x));
    for (auto& x : moved) x.layer = layer;
    kept.insert(kept.end(), std::make_move_iterator(moved.begin()), std::make_move_iterator(moved.end()));
    v.objects = std::move(kept);
    return true;
}

Id holderAt(const View& v, double x, double y, Id exclude) {
    const auto order = v.paintOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Object& o = **it;
        if (o.id == exclude || o.kind == Kind::Container || !kindHoldsChildren(o.kind)) continue;
        if (v.effectivelyHidden(o) || v.effectivelyLocked(o)) continue;
        // Un parent exclu (on deplace un cadre) : pas dans l'un de ses enfants.
        bool underExcluded = false;
        for (Id p = o.parent; p != kNoId && !underExcluded;) {
            if (p == exclude) underExcluded = true;
            const auto* po = v.object(p);
            p = po ? po->parent : kNoId;
        }
        if (underExcluded) continue;
        Box clip{};
        if (!childClip(v, o, clip)) clip = o.box();
        if (clip.contains(x, y) && !clippedAt(v, o, x, y)) return o.id;
    }
    return kNoId;
}

void layoutStructures(View& shown) {
    bool any = false;
    for (const auto& o : shown.objects)
        if (o.kind == Kind::TabContainer || o.kind == Kind::ScrollPanel || o.kind == Kind::CollapsiblePanel
            || o.kind == Kind::Frame) {
            any = true;
            break;
        }
    if (!any) return;

    // 1. Les pages des onglets, les panneaux replies, les structures invisibles :
    //    leurs enfants se cachent.
    std::vector<Id> hide;
    for (const auto& o : shown.objects) {
        const Object* child = &o;
        Id p = o.parent;
        for (int guard = 0; p != kNoId && guard < 64; ++guard) {
            const auto* g = shown.object(p);
            if (!g) break;
            const bool structure = g->kind == Kind::TabContainer || g->kind == Kind::ScrollPanel
                                || g->kind == Kind::CollapsiblePanel || g->kind == Kind::Frame;
            if ((g->kind == Kind::TabContainer && tabPageOf(*child) != shownTabPage(*g))
                || (g->kind == Kind::CollapsiblePanel && g->flag("collapsed"))
                || (structure && !g->flag("visible", true))) {
                hide.push_back(o.id);
                break;
            }
            child = g;
            p = g->parent;
        }
    }
    for (Id id : hide)
        if (auto* o = shown.object(id)) o->setFlag("visible", false);

    // 2. Les panneaux replies prennent la hauteur de leur bandeau ; ceux qui
    //    poussent font remonter les objets du dessous (meme parent, chevauchement
    //    en largeur), mesures avant tout deplacement.
    std::map<Id, double> own;     // le decalage propre de chaque objet (par ses freres)
    std::vector<std::pair<Id, double>> heights;
    for (const auto& pnl : shown.objects) {
        if (pnl.kind != Kind::CollapsiblePanel || !pnl.flag("collapsed") || !pnl.flag("visible", true)) continue;
        const Box pb = pnl.box();
        const double hh = collapsibleHeaderHeight(pnl);
        const double delta = std::max(0.0, pb.h - hh);
        heights.emplace_back(pnl.id, hh);
        if (!pnl.flag("pushBelow", true) || delta <= 0) continue;
        for (const auto& o : shown.objects) {
            if (o.id == pnl.id || o.parent != pnl.parent || o.kind == Kind::Group) continue;
            // Sous le panneau, dans sa largeur (un accordeon : les panneaux empiles, les
            // objets poses dessous) - pas un objet plus large qui ne fait que le deborder.
            const Box b = o.box();
            if (b.y + 0.5 < pb.bottom() || b.x < pb.x - 2 || b.right() > pb.right() + 2) continue;
            own[o.id] += delta;
        }
    }
    // Les groupes : un groupe sous le panneau remonte avec ses enfants (son cadre
    // est celui de ses enfants).
    if (!own.empty()) {
        std::map<Id, double> total;
        const auto shiftOf = [&](Id id) {
            double s = 0;
            int guard = 0;
            for (Id cur = id; cur != kNoId && guard < 64; ++guard) {
                if (const auto it = own.find(cur); it != own.end()) s += it->second;
                const auto* c = shown.object(cur);
                cur = c ? c->parent : kNoId;
            }
            return s;
        };
        for (const auto& o : shown.objects) total[o.id] = shiftOf(o.id);
        for (auto& o : shown.objects)
            if (const double s = total[o.id]; s > 0) o.setNumber("y", o.number("y") - s);
    }
    for (const auto& [id, hh] : heights)
        if (auto* o = shown.object(id)) o->setNumber("h", hh);

    // 3. Les panneaux defilants : le contenu mesure (avant decalage), le
    //    defilement borne, puis leurs descendants decales.
    std::vector<std::tuple<Id, double, double>> scrolls;
    for (auto& o : shown.objects) {
        if (o.kind != Kind::ScrollPanel) continue;
        const auto l = scrollLayout(shown, o);
        o.setNumber("shownContentWidth", l.contentW);
        o.setNumber("shownContentHeight", l.contentH);
        o.setNumber("scrollX", l.scrollX);
        o.setNumber("scrollY", l.scrollY);
        if (l.scrollX > 0 || l.scrollY > 0) scrolls.emplace_back(o.id, l.scrollX, l.scrollY);
    }
    for (const auto& [id, sx, sy] : scrolls)
        for (Id d : shown.descendantsOf(id))
            if (auto* c = shown.object(d)) {
                c->setNumber("x", c->number("x") - sx);
                c->setNumber("y", c->number("y") - sy);
            }
}

} // namespace hmi
