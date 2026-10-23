// =============================================================================
//  hmi/HmiDocView.cpp - 1.12.30 : la visionneuse de documents, sans ecran.
//                       Voir HmiDocView.hpp.
// =============================================================================
#include "HmiDocView.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace hmi::docview {

namespace {

// Sans casse ni accent (l'ASCII, et les lettres accentuees de l'UTF-8 a deux octets ramenees a leur base).
std::string plain(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto n = static_cast<unsigned char>(s[++i]);
            if ((n >= 0xA8 && n <= 0xAB) || (n >= 0x88 && n <= 0x8B)) out += 'e';
            else if ((n >= 0xA0 && n <= 0xA5) || (n >= 0x80 && n <= 0x85)) out += 'a';
            else if ((n >= 0xAC && n <= 0xAF) || (n >= 0x8C && n <= 0x8F)) out += 'i';
            else if ((n >= 0xB2 && n <= 0xB6) || (n >= 0x92 && n <= 0x96)) out += 'o';
            else if ((n >= 0xB9 && n <= 0xBC) || (n >= 0x99 && n <= 0x9C)) out += 'u';
            else if (n == 0xA7 || n == 0x87) out += 'c';
            continue;
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

double clampZoom(double z) { return std::clamp(std::isfinite(z) ? z : 1.0, kMinZoom, kMaxZoom); }

// La taille d'une page tournee de `quarter` quarts de tour.
doc::Size turned(const doc::Size& s, int quarter) { return (quarter & 1) ? doc::Size{s.h, s.w} : s; }

bool inside(const Box& b, double x, double y) { return b.w > 0 && b.h > 0 && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

// Les pages posees : toutes, ou la seule montree (navigation interdite).
std::vector<int> laidPages(const State& st, std::size_t n) {
    std::vector<int> out;
    if (n == 0) return out;
    if (st.single > 0) {
        out.push_back(std::clamp(st.single, 1, static_cast<int>(n)) - 1);
        return out;
    }
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<int>(i);
    return out;
}

// Les nombres separes par ':' apres un prefixe ("doc:zoomat:1.15:120:80") ; faux : il en manque, ou illisible.
bool numbersAfter(std::string_view part, std::string_view prefix, double* out, std::size_t count) {
    if (part.size() <= prefix.size() || part.substr(0, prefix.size()) != prefix) return false;
    std::string_view rest = part.substr(prefix.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto colon = rest.find(':');
        const std::string_view one = i + 1 < count ? rest.substr(0, colon) : rest;
        if (i + 1 < count && colon == std::string_view::npos) return false;
        if (!parseNumber(one, out[i]) || !std::isfinite(out[i])) return false;
        if (i + 1 < count) rest = rest.substr(colon + 1);
    }
    return true;
}
bool numberAfter(std::string_view part, std::string_view prefix, double& out) { return numbersAfter(part, prefix, &out, 1); }
bool twoAfter(std::string_view part, std::string_view prefix, double& a, double& b) {
    double v[2];
    if (!numbersAfter(part, prefix, v, 2)) return false;
    a = v[0];
    b = v[1];
    return true;
}
bool threeAfter(std::string_view part, std::string_view prefix, double& a, double& b, double& c) {
    double v[3];
    if (!numbersAfter(part, prefix, v, 3)) return false;
    a = v[0];
    b = v[1];
    c = v[2];
    return true;
}

} // namespace

// ================================================================== les reglages ===
bool parseZoom(std::string_view text, Fit& fit, double& zoom) {
    const std::string t = plain(text);
    if (t.find("larg") != std::string::npos) { fit = Fit::Width; return true; }
    if (t.find("enti") != std::string::npos || t.find("page") != std::string::npos) { fit = Fit::Page; return true; }
    std::string digits;
    for (const char c : t) {
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        else if ((c == ',' || c == '.') && digits.find('.') == std::string::npos) digits += '.';
        else if (!digits.empty() && c != ' ') break;
    }
    if (digits.empty()) return false;
    double pct = 0;
    if (!parseNumber(digits, pct) || !(pct > 0)) return false;
    fit = Fit::None;
    zoom = clampZoom(pct / 100.0);
    return true;
}

Settings settingsOf(const Object& o) {
    Settings s;
    s.toolbar = o.flag("showToolbar", true);
    s.thumbnails = o.flag("showThumbnails", false);
    s.navigation = o.flag("allowNavigation", true);
    Fit fit = Fit::Width;
    double zoom = 1.0;
    if (parseZoom(o.text("startZoom", "ajuster"), fit, zoom)) {
        s.startFit = fit;
        s.startZoom = zoom;
    }
    return s;
}

State initialState(const Settings& s) {
    State st;
    st.fit = s.startFit;
    st.zoom = s.startZoom;
    st.thumbs = s.thumbnails && s.navigation;
    return st;
}

// ================================================================== la barre ===
Layout layoutOf(double w, double h, const Settings& s, const State& st) {
    Layout l;
    l.w = std::max(0.0, w);
    l.h = std::max(0.0, h);
    double y = 0;
    if (s.toolbar && l.h >= kBar + 20) {
        l.toolbar = {0, 0, l.w, kBar};
        y = kBar;
        if (st.searchOpen && l.h >= 2 * kBar + 20) {
            l.searchBar = {0, y, l.w, kBar};
            y += kBar;
        }
    }
    const double rest = std::max(0.0, l.h - y);
    if (st.thumbs && l.w >= 260) {
        const double tw = std::clamp(l.w * 0.2, 96.0, 170.0);
        l.thumbs = {0, y, tw, rest};
        l.area = {tw, y, l.w - tw, rest};
    } else {
        l.area = {0, y, l.w, rest};
    }
    if (l.toolbar.w <= 0) return l;
    // ---- les boutons : ce qui ne tient pas part, du moins utile au plus utile
    struct Item { const char* part; double width; int keep; int group; bool field; };
    std::vector<Item> items = {
        {"doc:first", kButton, 3, 0, false}, {"doc:prev", kButton, 9, 0, false}, {"doc:page", 78, 9, 0, true},
        {"doc:next", kButton, 9, 0, false}, {"doc:last", kButton, 3, 0, false},
        {"doc:zoomout", kButton, 8, 1, false}, {"doc:zoommenu", 74, 7, 1, true}, {"doc:zoomin", kButton, 8, 1, false},
        {"doc:rotate", kButton, 4, 2, false}, {"doc:thumbs", kButton, 5, 2, false}, {"doc:search", kButton, 6, 2, false},
    };
    const auto widthOf = [](const std::vector<Item>& list) {
        double total = 8;
        for (std::size_t i = 0; i < list.size(); ++i) {
            total += list[i].width;
            if (i + 1 < list.size()) total += list[i + 1].group != list[i].group ? 12 : 2;
        }
        return total;
    };
    for (int level = 1; level <= 8 && widthOf(items) > l.w; ++level)
        items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& it) { return it.keep <= level; }), items.end());
    if (widthOf(items) > l.w)
        for (auto& it : items) if (it.field) it.width = 56;
    double x = 4;
    const double by = (kBar - kButton) / 2;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        const Box b{x, by, it.width, kButton};
        if (std::string_view(it.part) == "doc:page") l.pageField = b;
        else if (std::string_view(it.part) == "doc:zoommenu") l.zoomField = b;
        else {
            Button bt;
            bt.part = it.part;
            bt.box = b;
            const std::string_view p = it.part;
            bt.enabled = s.navigation || !(p == "doc:first" || p == "doc:prev" || p == "doc:next" || p == "doc:last" || p == "doc:thumbs");
            if (p == "doc:thumbs" && l.w < 260) bt.enabled = false;   // trop etroite pour la colonne des vignettes
            bt.active = (p == "doc:thumbs" && st.thumbs) || (p == "doc:search" && st.searchOpen);
            l.buttons.push_back(std::move(bt));
        }
        x += it.width;
        if (i + 1 < items.size()) x += items[i + 1].group != it.group ? 12 : 2;
    }
    // ---- la barre de la recherche : le champ, precedente, suivante, "3 / 12", fermer
    if (l.searchBar.w > 0) {
        const double sy = l.searchBar.y + (kBar - kButton) / 2;
        const double right = l.w - 4;
        Button close{"doc:searchclose", {right - kButton, sy, kButton, kButton}, true, false};
        const double labelW = 74;
        l.hitLabel = {close.box.x - 6 - labelW, sy, labelW, kButton};
        Button next{"doc:hitnext", {l.hitLabel.x - 4 - kButton, sy, kButton, kButton}, true, false};
        Button prev{"doc:hitprev", {next.box.x - 2 - kButton, sy, kButton, kButton}, true, false};
        l.searchField = {4, sy, std::max(40.0, prev.box.x - 8 - 4), kButton};
        l.buttons.push_back(prev);
        l.buttons.push_back(next);
        l.buttons.push_back(close);
    }
    // ---- le menu des zooms (ouvert), sous son champ
    if (st.zoomMenu && l.zoomField.w > 0) {
        const double top = l.toolbar.y + l.toolbar.h, mw = std::max(176.0, l.zoomField.w);
        const double mx = std::clamp(l.zoomField.x, 0.0, std::max(0.0, l.w - mw));
        const int fit = static_cast<int>(std::floor((l.h - top - 4) / kButton));
        const int n = std::min<int>(static_cast<int>(std::size(kZoomChoices)), std::max(0, fit));
        for (int i = 0; i < n; ++i) l.zoomMenu.push_back({mx, top + 2 + kButton * i, mw, kButton});
    }
    return l;
}

// ================================================================== le contenu ===
Content contentOf(const Layout& l, const State& st, const std::vector<doc::Size>& sizes) {
    Content c;
    c.pages = laidPages(st, sizes.size());
    if (c.pages.empty()) return c;
    double maxW = 0, maxH = 0;
    for (const int i : c.pages) {
        const auto t = turned(sizes[static_cast<std::size_t>(i)], st.quarter);
        maxW = std::max(maxW, t.w);
        maxH = std::max(maxH, t.h);
    }
    maxW = std::max(1.0, maxW);
    maxH = std::max(1.0, maxH);
    const double aw = std::max(10.0, l.area.w - 2 * kMargin), ah = std::max(10.0, l.area.h - 2 * kMargin);
    if (st.fit == Fit::Width) c.zoom = clampZoom(aw / maxW);
    else if (st.fit == Fit::Page) c.zoom = clampZoom(std::min(aw / maxW, ah / maxH));
    else c.zoom = clampZoom(st.zoom);
    double y = kMargin, widest = 0;
    for (const int i : c.pages) {
        const auto t = turned(sizes[static_cast<std::size_t>(i)], st.quarter);
        c.tops.push_back(y);
        c.widths.push_back(t.w * c.zoom);
        c.heights.push_back(t.h * c.zoom);
        widest = std::max(widest, t.w * c.zoom);
        y += t.h * c.zoom + kGap;
    }
    c.width = widest + 2 * kMargin;
    c.height = y - kGap + kMargin;
    c.maxX = std::max(0.0, c.width - l.area.w);
    c.maxY = std::max(0.0, c.height - l.area.h);
    c.originX = l.area.x + (c.width <= l.area.w ? (l.area.w - c.width) / 2 : -std::clamp(st.scrollX, 0.0, c.maxX));
    c.originY = l.area.y + (c.height <= l.area.h ? (l.area.h - c.height) / 2 : -std::clamp(st.scrollY, 0.0, c.maxY));
    return c;
}

std::vector<Placed> visiblePages(const Layout& l, const Content& c) {
    std::vector<Placed> out;
    for (std::size_t k = 0; k < c.pages.size(); ++k) {
        const Box r{c.originX + (c.width - c.widths[k]) / 2, c.originY + c.tops[k], c.widths[k], c.heights[k]};
        // Un demi-pixel de tolerance : le bas de la page d'avant, pile au bord de la zone, ne compte pas.
        if (r.y >= l.area.y + l.area.h - 0.5) break;
        if (r.y + r.h <= l.area.y + 0.5 || r.x >= l.area.x + l.area.w - 0.5 || r.x + r.w <= l.area.x + 0.5) continue;
        out.push_back({c.pages[k], r});
    }
    return out;
}

int pageAt(const Layout&, const Content& c, double lx, double ly) {
    if (c.pages.empty()) return -1;
    int best = c.pages.front();
    double gap = 1e300;
    for (std::size_t k = 0; k < c.pages.size(); ++k) {
        const double top = c.originY + c.tops[k], bottom = top + c.heights[k];
        if (ly >= top && ly <= bottom) return c.pages[k];
        const double d = ly < top ? top - ly : ly - bottom;
        if (d < gap) {
            gap = d;
            best = c.pages[k];
        }
    }
    (void)lx;
    return best;
}

Box onScreen(const Box& in, const doc::Size& s, const Placed& at, int quarter, double zoom) {
    // Les deux coins dans la page tournee.
    const auto map = [&](double x, double y) -> std::pair<double, double> {
        switch (((quarter % 4) + 4) % 4) {
            case 1: return {s.h - y, x};
            case 2: return {s.w - x, s.h - y};
            case 3: return {y, s.w - x};
            default: return {x, y};
        }
    };
    const auto a = map(in.x, in.y), b = map(in.x + in.w, in.y + in.h);
    const double x0 = std::min(a.first, b.first), y0 = std::min(a.second, b.second);
    const double x1 = std::max(a.first, b.first), y1 = std::max(a.second, b.second);
    return {at.rect.x + x0 * zoom, at.rect.y + y0 * zoom, (x1 - x0) * zoom, (y1 - y0) * zoom};
}

std::vector<Thumb> thumbsOf(const Layout& l, const State& st, const std::vector<doc::Size>& sizes, double* total) {
    std::vector<Thumb> out;
    if (l.thumbs.w <= 0) {
        if (total) *total = 0;
        return out;
    }
    const double tw = std::max(20.0, l.thumbs.w - 28);
    double y = 8;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const auto t = turned(sizes[i], st.quarter);
        const double th = std::clamp(tw * t.h / std::max(1.0, t.w), 12.0, tw * 1.6);
        Thumb th1;
        th1.index = static_cast<int>(i);
        th1.cell = {l.thumbs.x + 4, l.thumbs.y + y - st.thumbScroll, l.thumbs.w - 8, th + 26};
        th1.frame = {l.thumbs.x + (l.thumbs.w - tw) / 2, th1.cell.y + 4, tw, th};
        out.push_back(th1);
        y += th + 30;
    }
    if (total) *total = y;
    return out;
}

std::string hitPart(const Layout& l, const State& st, const std::vector<doc::Size>& sizes, double lx, double ly) {
    for (std::size_t i = 0; i < l.zoomMenu.size(); ++i)
        if (inside(l.zoomMenu[i], lx, ly)) return "doc:zoom:" + std::to_string(i);
    if (st.zoomMenu && !l.zoomMenu.empty() && !inside(l.zoomField, lx, ly)) return "doc:zoommenu";   // dehors : le menu se ferme
    for (const auto& b : l.buttons)
        if (inside(b.box, lx, ly)) return b.enabled ? b.part : std::string{};
    if (inside(l.pageField, lx, ly)) return "doc:page";
    if (inside(l.zoomField, lx, ly)) return "doc:zoommenu";
    if (inside(l.searchField, lx, ly)) return "doc:searchfield";
    if (inside(l.thumbs, lx, ly)) {
        for (const auto& t : thumbsOf(l, st, sizes))
            if (inside(t.cell, lx, ly)) return "doc:thumb:" + std::to_string(t.index + 1);
        return {};
    }
    if (inside(l.area, lx, ly)) return "doc:area";
    return {};
}

// ================================================================== les mouvements ===
void settle(State& st, const Layout& l, const std::vector<doc::Size>& sizes) {
    const auto c = contentOf(l, st, sizes);
    if (st.fit != Fit::None) st.zoom = c.zoom;
    st.zoom = clampZoom(st.zoom);
    st.scrollX = std::clamp(st.scrollX, 0.0, c.maxX);
    st.scrollY = std::clamp(st.scrollY, 0.0, c.maxY);
    if (c.pages.empty()) {
        st.page = 1;
        return;
    }
    if (st.single > 0) {
        st.single = std::clamp(st.single, 1, static_cast<int>(sizes.size()));
        st.page = st.single;
    } else {
        int idx = 0;
        if (c.maxY > 0 && st.scrollY >= c.maxY - 0.5) {
            idx = static_cast<int>(c.pages.size()) - 1;
        } else {
            const double line = st.scrollY + std::min(l.area.h, c.height) * 0.3;
            for (std::size_t k = 0; k < c.pages.size(); ++k)
                if (c.tops[k] <= line) idx = static_cast<int>(k);
        }
        st.page = c.pages[static_cast<std::size_t>(idx)] + 1;
    }
    // La vignette de la page courante reste dans sa colonne.
    if (l.thumbs.w > 0) {
        double total = 0;
        const auto thumbs = thumbsOf(l, State{st}, sizes, &total);
        const double maxT = std::max(0.0, total - l.thumbs.h);
        st.thumbScroll = std::clamp(st.thumbScroll, 0.0, maxT);
    }
}

void relayout(State& st, const Layout& from, const Layout& to, const std::vector<doc::Size>& sizes) {
    if (sizes.empty()) return;
    const auto a = contentOf(from, st, sizes);
    if (a.pages.empty()) return;
    // Le point du contenu en haut de la zone : sa page (sa bande : l'intervalle au-dessus d'elle, puis elle), et ou dans
    // sa bande, en fraction.
    const double y = from.area.y - a.originY;
    std::size_t k = 0;
    for (std::size_t i = 0; i < a.pages.size(); ++i)
        if (a.tops[i] - kGap <= y + 1e-9) k = i;
    const double frac = std::clamp((y - (a.tops[k] - kGap)) / std::max(1e-9, a.heights[k] + kGap), 0.0, 1.0);
    const double xfrac = a.maxX > 0 ? std::clamp(st.scrollX / a.maxX, 0.0, 1.0) : 0.5;
    const auto b = contentOf(to, st, sizes);
    st.scrollY = b.tops[k] - kGap + frac * (b.heights[k] + kGap);
    st.scrollX = xfrac * b.maxX;
    settle(st, to, sizes);
}

bool goToPage(State& st, const Layout& l, const std::vector<doc::Size>& sizes, int page) {
    const int n = static_cast<int>(sizes.size());
    if (page < 1 || page > n) return false;
    if (st.single > 0) {
        st.single = page;
        st.scrollY = 0;
        st.scrollX = 0;
        settle(st, l, sizes);
        return true;
    }
    const auto c = contentOf(l, st, sizes);
    st.scrollY = c.tops[static_cast<std::size_t>(page - 1)] - kMargin;
    settle(st, l, sizes);
    // Une derniere page plus courte que la zone : le defilement bute, la page courante reste celle voulue.
    st.page = page;
    // La vignette de la page voulue se voit.
    if (l.thumbs.w > 0) {
        const auto thumbs = thumbsOf(l, st, sizes);
        const auto& t = thumbs[static_cast<std::size_t>(page - 1)];
        if (t.cell.y < l.thumbs.y) st.thumbScroll -= l.thumbs.y - t.cell.y + 4;
        else if (t.cell.y + t.cell.h > l.thumbs.y + l.thumbs.h) st.thumbScroll += t.cell.y + t.cell.h - (l.thumbs.y + l.thumbs.h) + 4;
        st.thumbScroll = std::max(0.0, st.thumbScroll);
    }
    return true;
}

void zoomAround(State& st, const Layout& l, const std::vector<doc::Size>& sizes, double newZoom, double lx, double ly) {
    const auto before = contentOf(l, st, sizes);
    st.fit = Fit::None;
    if (before.pages.empty()) {
        st.zoom = clampZoom(newZoom);
        return;
    }
    // Le point sous le curseur, dans sa page (en fraction de la page).
    std::size_t k = 0;
    {
        const int idx = pageAt(l, before, lx, ly);
        for (std::size_t i = 0; i < before.pages.size(); ++i)
            if (before.pages[i] == idx) k = i;
    }
    const double px = before.originX + (before.width - before.widths[k]) / 2, py = before.originY + before.tops[k];
    const double u = (lx - px) / std::max(1e-9, before.widths[k]), v = (ly - py) / std::max(1e-9, before.heights[k]);
    st.zoom = clampZoom(newZoom);
    const auto after = contentOf(l, st, sizes);
    const double cx = (after.width - after.widths[k]) / 2 + u * after.widths[k];
    const double cy = after.tops[k] + v * after.heights[k];
    st.scrollX = cx - (lx - l.area.x);
    st.scrollY = cy - (ly - l.area.y);
    settle(st, l, sizes);
}

double nextZoom(double zoom, bool up) {
    static const double steps[] = {0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0};
    if (up) {
        for (const double s : steps) if (s > zoom * 1.001) return s;
        return kMaxZoom;
    }
    for (auto it = std::rbegin(steps); it != std::rend(steps); ++it) if (*it < zoom * 0.999) return *it;
    return kMinZoom;
}

std::string zoomLabel(double zoom) { return std::to_string(static_cast<int>(std::lround(zoom * 100))) + " %"; }

std::string pageLabel(int page, int count) {
    if (count <= 0) return "- / -";
    return std::to_string(std::clamp(page, 1, count)) + " / " + std::to_string(count);
}

bool showHit(State& st, const Layout& l, const std::vector<doc::Size>& sizes, const std::vector<doc::Hit>& hits, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= hits.size()) return false;
    const auto& h = hits[static_cast<std::size_t>(index)];
    if (h.page < 0 || static_cast<std::size_t>(h.page) >= sizes.size()) return false;
    if (st.single > 0 && st.single != h.page + 1) return false;
    st.hit = index;
    if (h.rects.empty()) return goToPage(st, l, sizes, h.page + 1);
    const auto c = contentOf(l, st, sizes);
    std::size_t k = 0;
    for (std::size_t i = 0; i < c.pages.size(); ++i)
        if (c.pages[i] == h.page) k = i;
    const Placed at{h.page, {(c.width - c.widths[k]) / 2, c.tops[k], c.widths[k], c.heights[k]}};
    const Box r = onScreen(h.rects.front(), sizes[static_cast<std::size_t>(h.page)], at, st.quarter, c.zoom);
    st.scrollY = r.y + r.h / 2 - l.area.h / 2;
    st.scrollX = r.x + r.w / 2 - l.area.w / 2;
    settle(st, l, sizes);
    return true;
}

// ================================================================== les gestes ===
Effect gesture(State& st, const Layout& l, const std::vector<doc::Size>& sizes, std::string_view part, bool navigation,
               const std::vector<doc::Hit>& hits) {
    Effect fx;
    const int before = st.page;
    const State old = st;
    const int n = static_cast<int>(sizes.size());
    const double cx = l.area.x + l.area.w / 2, cy = l.area.y + l.area.h / 2;
    const auto nav = [&](int page) {
        if (!navigation) { fx.refused = true; return; }
        (void)goToPage(st, l, sizes, std::clamp(page, 1, std::max(1, n)));
    };
    double a = 0, b = 0, c = 0;
    if (part != "doc:zoommenu" && part.rfind("doc:zoom:", 0) != 0) st.zoomMenu = false;
    if (part == "doc:first") nav(1);
    else if (part == "doc:prev") nav(st.page - 1);
    else if (part == "doc:next") nav(st.page + 1);
    else if (part == "doc:last") nav(n);
    else if (part == "doc:page") {
        if (!navigation) fx.refused = true;
        else {
            st.editingPage = true;
            st.editingSearch = false;
            st.pageText.clear();
            fx.focus = true;
        }
    } else if (part == "doc:zoomin" || part == "doc:zoomout") {
        const double z = contentOf(l, st, sizes).zoom;
        zoomAround(st, l, sizes, nextZoom(z, part == "doc:zoomin"), cx, cy);
    } else if (part == "doc:zoommenu") {
        st.zoomMenu = !st.zoomMenu;
    } else if (numberAfter(part, "doc:zoom:", a)) {
        const int i = static_cast<int>(a);
        st.zoomMenu = false;
        if (i == 0) st.fit = Fit::Width;
        else if (i == 1) st.fit = Fit::Page;
        else if (i > 1 && static_cast<std::size_t>(i) < std::size(kZoomChoices)) {
            Fit f = Fit::None;
            double z = 1;
            if (parseZoom(kZoomChoices[i], f, z)) zoomAround(st, l, sizes, z, cx, l.area.y);
        }
        if (i <= 1) {
            settle(st, l, sizes);
            if (st.single == 0 && n > 0) (void)goToPage(st, l, sizes, old.page);
        }
    } else if (part == "doc:rotate") {
        st.quarter = (st.quarter + 1) % 4;
        if (n > 0) (void)goToPage(st, l, sizes, old.page);
    } else if (part == "doc:thumbs") {
        if (!navigation) fx.refused = true;
        else st.thumbs = !st.thumbs;
    } else if (part == "doc:search") {
        st.searchOpen = !st.searchOpen;
        st.editingSearch = st.searchOpen;
        st.editingPage = false;
        fx.focus = st.searchOpen;
        if (!st.searchOpen && !st.query.empty()) {
            st.query.clear();
            st.hit = -1;
            fx.startSearch = true;
        }
    } else if (part == "doc:searchfield") {
        st.editingSearch = true;
        st.editingPage = false;
        fx.focus = true;
    } else if (part == "doc:hitnext" || part == "doc:hitprev") {
        if (!hits.empty()) {
            const int k = static_cast<int>(hits.size());
            const int next = st.hit < 0 ? (part == "doc:hitnext" ? 0 : k - 1) : ((st.hit + (part == "doc:hitnext" ? 1 : -1)) % k + k) % k;
            if (!showHit(st, l, sizes, hits, next)) fx.refused = st.single > 0;
        }
    } else if (part == "doc:searchclose") {
        st.searchOpen = false;
        st.editingSearch = false;
        if (!st.query.empty()) {
            st.query.clear();
            st.hit = -1;
            fx.startSearch = true;
        }
    } else if (numberAfter(part, "doc:thumb:", a)) {
        nav(static_cast<int>(a));
    } else if (part == "doc:area") {
        st.editingPage = false;
        st.editingSearch = false;
        fx.focus = true;
    } else if (numberAfter(part, "doc:wheel:", a)) {
        st.scrollY += a;
    } else if (numberAfter(part, "doc:wheelx:", a)) {
        st.scrollX += a;
    } else if (numberAfter(part, "doc:thumbwheel:", a)) {
        st.thumbScroll += a;
    } else if (threeAfter(part, "doc:zoomat:", a, b, c)) {
        const double z = contentOf(l, st, sizes).zoom;
        zoomAround(st, l, sizes, z * a, b, c);
    } else if (twoAfter(part, "doc:pan:", a, b)) {
        st.scrollX -= a;
        st.scrollY -= b;
    } else if (twoAfter(part, "doc:dbl:", a, b)) {
        // Ajuste -> 100 % autour du point ; sinon -> ajuste a la largeur.
        if (st.fit != Fit::None) zoomAround(st, l, sizes, 1.0, a, b);
        else {
            st.fit = Fit::Width;
            settle(st, l, sizes);
            if (st.single == 0 && n > 0) (void)goToPage(st, l, sizes, old.page);
        }
        fx.focus = true;
    } else if (part.rfind("doc:key:", 0) == 0) {
        const std::string_view key = part.substr(8);
        if (key == "PageDown") nav(st.page + 1);
        else if (key == "PageUp") nav(st.page - 1);
        else if (key == "Home") nav(1);
        else if (key == "End") nav(n);
        else if (key == "Down") st.scrollY += kWheelStep;
        else if (key == "Up") st.scrollY -= kWheelStep;
        else if (key == "Right") st.scrollX += kWheelStep;
        else if (key == "Left") st.scrollX -= kWheelStep;
        else if (key == "Plus" || key == "Minus") {
            const double z = contentOf(l, st, sizes).zoom;
            zoomAround(st, l, sizes, nextZoom(z, key == "Plus"), cx, cy);
        } else if (key == "Zero") {
            st.fit = Fit::Page;
            settle(st, l, sizes);
            if (st.single == 0 && n > 0) (void)goToPage(st, l, sizes, old.page);
        } else return fx;
    } else {
        return fx;
    }
    settle(st, l, sizes);
    fx.pageChanged = st.page != before;
    fx.changed = fx.pageChanged || st.zoom != old.zoom || st.fit != old.fit || st.scrollX != old.scrollX || st.scrollY != old.scrollY
              || st.quarter != old.quarter || st.thumbs != old.thumbs || st.searchOpen != old.searchOpen || st.zoomMenu != old.zoomMenu
              || st.editingPage != old.editingPage || st.editingSearch != old.editingSearch || st.thumbScroll != old.thumbScroll
              || st.hit != old.hit || st.single != old.single;
    return fx;
}

Effect typeText(State& st, std::string_view text) {
    Effect fx;
    if (st.editingPage) {
        for (const char ch : text)
            if (std::isdigit(static_cast<unsigned char>(ch)) && st.pageText.size() < 5) st.pageText += ch;
        fx.changed = true;
    } else if (st.editingSearch) {
        if (st.query.size() + text.size() <= 80) st.query += std::string(text);
        fx.changed = true;
    }
    return fx;
}

Effect typeKey(State& st, const Layout& l, const std::vector<doc::Size>& sizes, std::string_view key) {
    Effect fx;
    const int before = st.page;
    if (!st.editingPage && !st.editingSearch) return fx;
    std::string& field = st.editingPage ? st.pageText : st.query;
    if (key == "Backspace") {
        if (!field.empty()) {
            std::size_t cut = field.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(field[cut]) & 0xC0) == 0x80) --cut;   // un caractere UTF-8 entier
            field.erase(cut);
        }
        fx.changed = true;
    } else if (key == "Escape") {
        if (st.editingPage) st.pageText.clear();
        st.editingPage = false;
        st.editingSearch = false;
        fx.changed = true;
    } else if (key == "Enter") {
        if (st.editingPage) {
            const int page = st.pageText.empty() ? 0 : std::atoi(st.pageText.c_str());
            st.editingPage = false;
            st.pageText.clear();
            if (page >= 1) (void)goToPage(st, l, sizes, std::min(page, static_cast<int>(sizes.size())));
        } else {
            // Entree dans la recherche : chercher (le moteur passe a l'occurrence suivante si c'est deja fait).
            fx.startSearch = true;
        }
        fx.changed = true;
    } else {
        return fx;
    }
    settle(st, l, sizes);
    fx.pageChanged = st.page != before;
    return fx;
}

} // namespace hmi::docview
