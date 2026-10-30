// =============================================================================
//  hmi/HmiDocView.cpp - 1.12.30 : la visionneuse de documents, sans ecran.
//                       Voir HmiDocView.hpp.
// =============================================================================
#include "HmiDocView.hpp"
#include "HmiMedia.hpp"     // 1.14.1 (lot N5) : l'explorateur - les dimensions d'une image, les tailles lisibles
#include "../../third_party/miniz.h"   // 1.14.1 (lot N5) : un zip se parcourt comme un dossier

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>   // 1.14.1 (lot N5) : la date de creation d'un fichier
#else
#  include <fcntl.h>
#  include <sys/stat.h>  // 1.14.1 (lot N5) : statx (la date de creation, quand le systeme la donne)
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

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

// =============================================================================
//  1.14.1 (lot N5) : L'EXPLORATEUR DE FICHIERS (voir HmiDocView.hpp, hmi::explorer)
// =============================================================================
namespace hmi::explorer {

namespace {

namespace fs = std::filesystem;

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::FILE* openRead(const std::string& path) {
#if defined(_WIN32)
    return _wfopen(P(path).wstring().c_str(), L"rb");
#else
    return std::fopen(P(path).string().c_str(), "rb");
#endif
}
std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Minuscules, sans accents (les lettres accentuees du Latin-1), pour la recherche Nom contient.
std::string folded(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            static const char* const base = "aaaaaaaceeeeiiiidnooooo.ouuuuy.y";
            out += base[(d >= 0xA0 ? d - 0xA0 : d - 0x80) & 0x1F];
            i += 2;
            continue;
        }
        out += static_cast<char>(std::tolower(c));
        ++i;
    }
    return out;
}
std::string extensionOf(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) return {};
    return lowerAscii(std::string(name.substr(dot + 1)));
}
std::size_t codepoints(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}
std::int64_t secondsOf(fs::file_time_type t) {
    const auto sys = std::chrono::system_clock::now()
                   + std::chrono::duration_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now());
    return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(sys));
}
// La date de creation, quand le systeme la donne (0 : inconnue).
std::int64_t createdOf(const fs::path& p) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(p.wstring().c_str(), GetFileExInfoStandard, &data)) return 0;
    const std::uint64_t t = (static_cast<std::uint64_t>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime;
    return t > 116444736000000000ull ? static_cast<std::int64_t>((t - 116444736000000000ull) / 10000000ull) : 0;
#elif defined(STATX_BTIME)
    struct statx sx{};
    if (statx(AT_FDCWD, p.c_str(), AT_STATX_SYNC_AS_STAT, STATX_BTIME, &sx) != 0 || !(sx.stx_mask & STATX_BTIME)) return 0;
    return static_cast<std::int64_t>(sx.stx_btime.tv_sec);
#else
    (void)p;
    return 0;
#endif
}
std::string sep() { return std::string(1, static_cast<char>(fs::path::preferred_separator)); }

// Un nom d'entree de zip ramene dans le zip : / et \ separent ; les etapes vides, . et .. tombent, comme un "C:"
// en tete. `suspicious` : le nom disait de sortir (un zip hostile, ou mal fait).
std::string cleanName(std::string_view raw, bool& suspicious) {
    suspicious = false;
    std::string out;
    std::size_t i = 0;
    bool firstStep = true;
    if (!raw.empty() && (raw.front() == '/' || raw.front() == '\\')) suspicious = true;
    while (i <= raw.size()) {
        std::size_t j = i;
        while (j < raw.size() && raw[j] != '/' && raw[j] != '\\') ++j;
        std::string step(raw.substr(i, j - i));
        i = j + 1;
        if (step.empty() || step == ".") continue;
        if (step == ".." || (firstStep && step.size() == 2 && step[1] == ':')) {
            suspicious = true;
            firstStep = false;
            continue;
        }
        firstStep = false;
        for (auto& c : step)
            if (static_cast<unsigned char>(c) < 0x20) {
                c = '_';
                suspicious = true;
            }
        if (!out.empty()) out += '/';
        out += step;
    }
    return out;
}

// Un zip ouvert (un fichier du disque, ou les octets d'un zip interieur) : sa table seulement est lue.
struct Archive {
    mz_zip_archive zip{};
    std::FILE*     file{nullptr};
    std::vector<unsigned char> memory;
    bool ok{false};
    Archive() { std::memset(&zip, 0, sizeof zip); }
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;
    ~Archive() {
        if (ok) mz_zip_reader_end(&zip);
        if (file) std::fclose(file);
    }
    bool openFile(const std::string& path) {
        file = openRead(path);
        if (!file) return false;
        ok = mz_zip_reader_init_cfile(&zip, file, 0, 0);
        return ok;
    }
    bool openMemory(std::vector<unsigned char> bytes) {
        memory = std::move(bytes);
        ok = !memory.empty() && mz_zip_reader_init_mem(&zip, memory.data(), memory.size(), 0);
        return ok;
    }
    [[nodiscard]] std::size_t count() const { return ok ? std::min<std::size_t>(mz_zip_reader_get_num_files(const_cast<mz_zip_archive*>(&zip)), kMaxZipEntries) : 0; }
    [[nodiscard]] bool truncated() const { return ok && mz_zip_reader_get_num_files(const_cast<mz_zip_archive*>(&zip)) > kMaxZipEntries; }
    bool stat(std::size_t i, mz_zip_archive_file_stat& st) { return mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(i), &st); }
    // Une entree en memoire, bornee : la taille annoncee d'abord, puis celle lue.
    bool read(std::size_t i, std::uint64_t limit, std::vector<unsigned char>& out) {
        mz_zip_archive_file_stat st;
        if (!stat(i, st) || st.m_is_directory || st.m_uncomp_size > limit) return false;
        std::size_t n = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(i), &n, 0);
        if (!data) return false;
        const bool fits = n <= limit;
        if (fits) out.assign(static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + n);
        mz_free(data);
        return fits;
    }
};

bool isZipName(std::string_view name) { return extensionOf(name) == "zip"; }

// Un chemin long, coupe a ses separateurs pour l'infobulle (elle ne coupe qu'aux espaces) : des lignes de 44 caracteres.
std::string wrappedPath(const std::string& path) {
    std::string out, line;
    std::size_t count = 0;
    std::string piece;
    const auto flush = [&] {
        const std::size_t n = codepoints(piece);
        if (count > 0 && count + n > 44) {
            out += line + "\n  ";
            line.clear();
            count = 2;
        }
        line += piece;
        count += n;
        piece.clear();
    };
    for (const char c : path) {
        piece += c;
        if (c == '/' || c == '\\') flush();
    }
    flush();
    return out + line;
}

// Ou en est-on : le dossier du disque, ou le dossier d'un zip (son prefixe) - et le chemin montre.
struct Place {
    bool                     zip{false};
    fs::path                 disk;      // le dossier courant (hors zip), ou le fichier zip ouvert
    std::unique_ptr<Archive> archive;
    std::string              prefix;    // le dossier dans le zip : "docs/notices/" ; vide : sa racine
    std::string              shown;     // le chemin montre du dossier courant
};

bool within(const fs::path& child, const fs::path& root) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(child, ec);
    if (ec) return false;
    const fs::path r = fs::weakly_canonical(root, ec);
    if (ec) return false;
    auto ci = c.begin();
    for (auto ri = r.begin(); ri != r.end(); ++ri, ++ci) {
        if (ri->empty()) continue;
        if (ci == c.end() || *ci != *ri) return false;
    }
    return true;
}

// Le rang de l'entree `name` (un fichier) du zip, ramene ; -1 : aucune.
long findEntry(Archive& a, const std::string& name) {
    mz_zip_archive_file_stat st;
    for (std::size_t i = 0, n = a.count(); i < n; ++i) {
        if (!a.stat(i, st) || st.m_is_directory) continue;
        bool s = false;
        if (cleanName(st.m_filename, s) == name) return static_cast<long>(i);
    }
    return -1;
}
bool zipHasFolder(Archive& a, const std::string& folder) {
    const std::string want = folder + "/";
    mz_zip_archive_file_stat st;
    for (std::size_t i = 0, n = a.count(); i < n; ++i) {
        if (!a.stat(i, st)) continue;
        bool s = false;
        const std::string name = cleanName(st.m_filename, s);
        if (name.rfind(want, 0) == 0 || (st.m_is_directory && name == folder)) return true;
    }
    return false;
}

bool resolve(const Location& loc, Place& pl, std::string& why) {
    const std::string root = loc.root;
    if (root.empty()) {
        why = "aucune source";
        return false;
    }
    std::error_code ec;
    const fs::path r = P(root);
    pl.shown = U(r);
    if (fs::is_directory(r, ec)) {
        pl.disk = r;
    } else if (fs::is_regular_file(r, ec) && isZipName(root)) {
        pl.disk = r;
        pl.zip = true;
        pl.archive = std::make_unique<Archive>();
        if (!pl.archive->openFile(root)) {
            why = "zip illisible : " + root;
            return false;
        }
    } else if (fs::exists(r, ec)) {
        why = "ni un dossier ni un .zip : " + root;
        return false;
    } else {
        why = "source introuvable : " + root;
        return false;
    }
    for (const auto& step : loc.at) {
        if (!validStep(step)) {
            why = "chemin refus\xC3\xA9 \xC2\xAB " + step + " \xC2\xBB (on ne remonte pas au-dessus de la source)";
            return false;
        }
        if (!pl.zip) {
            const fs::path next = pl.disk / P(step);
            if (fs::is_directory(next, ec)) {
                if (!within(next, r)) {
                    why = "\xC2\xAB " + step + " \xC2\xBB m\xC3\xA8ne hors de la source (un lien) : refus\xC3\xA9";
                    return false;
                }
                pl.disk = next;
            } else if (fs::is_regular_file(next, ec) && isZipName(step)) {
                pl.archive = std::make_unique<Archive>();
                if (!pl.archive->openFile(U(next))) {
                    why = "zip illisible : " + step;
                    return false;
                }
                pl.disk = next;
                pl.zip = true;
                pl.prefix.clear();
            } else {
                why = "introuvable : " + step;
                return false;
            }
            pl.shown += sep() + step;
            continue;
        }
        const std::string want = pl.prefix + step;
        if (zipHasFolder(*pl.archive, want)) {
            pl.prefix = want + "/";
        } else if (isZipName(step)) {
            const long at = findEntry(*pl.archive, want);
            std::vector<unsigned char> bytes;
            if (at < 0 || !pl.archive->read(static_cast<std::size_t>(at), kMaxNestedZip, bytes)) {
                why = at < 0 ? "introuvable : " + step : "zip int\xC3\xA9rieur trop gros (plus de 32 Mo) : " + step;
                return false;
            }
            auto inner = std::make_unique<Archive>();
            if (!inner->openMemory(std::move(bytes))) {
                why = "zip illisible : " + step;
                return false;
            }
            pl.archive = std::move(inner);
            pl.prefix.clear();
        } else {
            why = "introuvable dans le zip : " + step;
            return false;
        }
        pl.shown += sep() + step;
    }
    return true;
}

bool passes(const Entry& e, const Options& opt) {
    if (!e.folder && e.genre != Genre::Zip && !opt.extensions.empty()) {
        const std::string ext = extensionOf(e.name);
        if (std::find(opt.extensions.begin(), opt.extensions.end(), ext) == opt.extensions.end()) return false;
    }
    if (!opt.search.empty() && folded(e.name).find(folded(opt.search)) == std::string::npos) return false;
    return true;
}

// Les premiers octets d'une entree (disque ou zip), bornes : pour les dimensions d'une image, les premieres lignes.
bool headOf(const Location& loc, const Entry& e, std::size_t max, std::vector<unsigned char>& out) {
    if (!e.inZip) {
        std::FILE* f = openRead(e.path);
        if (!f) return false;
        out.resize(max);
        out.resize(std::fread(out.data(), 1, max, f));
        std::fclose(f);
        return true;
    }
    if (e.size > 4u * 1024u * 1024u) return false;   // dans un zip : seulement ce qui se decompresse vite
    Place pl;
    std::string why;
    if (!resolve(loc, pl, why) || !pl.zip) return false;
    const long at = findEntry(*pl.archive, pl.prefix + e.name);
    if (at < 0 || !pl.archive->read(static_cast<std::size_t>(at), 4u * 1024u * 1024u, out)) return false;
    if (out.size() > max) out.resize(max);
    return true;
}

} // namespace

Genre genreOf(std::string_view name, bool folder) {
    if (folder) return Genre::Folder;
    const std::string ext = extensionOf(name);
    static const std::pair<const char*, Genre> kTable[] = {
        {"zip", Genre::Zip}, {"png", Genre::Image}, {"jpg", Genre::Image}, {"jpeg", Genre::Image}, {"bmp", Genre::Image},
        {"gif", Genre::Image}, {"svg", Genre::Image}, {"ico", Genre::Image}, {"webp", Genre::Image},
        {"txt", Genre::Text}, {"log", Genre::Text}, {"ini", Genre::Text}, {"json", Genre::Text}, {"xml", Genre::Text},
        {"md", Genre::Text}, {"st", Genre::Text}, {"cfg", Genre::Text}, {"pdf", Genre::Pdf},
        {"wav", Genre::Sound}, {"mp3", Genre::Sound}, {"ogg", Genre::Sound}, {"flac", Genre::Sound},
        {"mp4", Genre::Video}, {"avi", Genre::Video}, {"webm", Genre::Video}, {"mkv", Genre::Video}, {"mov", Genre::Video},
        {"csv", Genre::Csv}, {"tsv", Genre::Csv}, {"xls", Genre::Csv}, {"xlsx", Genre::Csv},
        {"xpgproj", Genre::Project}, {"xpg", Genre::Project}, {"xpgihm", Genre::Project}, {"xpgbib", Genre::Project},
        {"xrec", Genre::Project}, {"xpgscripts", Genre::Project},
    };
    for (const auto& [e, g] : kTable)
        if (ext == e) return g;
    return Genre::Other;
}

std::string genreLabel(Genre g, std::string_view name) {
    const std::string ext = extensionOf(name);
    std::string up;
    for (const char c : ext) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (g) {
        case Genre::Folder:  return "Dossier";
        case Genre::Zip:     return "Archive zip";
        case Genre::Image:   return "Image " + up;
        case Genre::Text:    return "Texte " + up;
        case Genre::Pdf:     return "Document PDF";
        case Genre::Sound:   return "Son " + up;
        case Genre::Video:   return "Vid\xC3\xA9o " + up;
        case Genre::Csv:     return "Tableau " + up;
        case Genre::Project: return "Fichier de l'application (" + up + ")";
        case Genre::Other:   break;
    }
    return up.empty() ? std::string("Fichier") : "Fichier " + up;
}

std::string_view genreTag(Genre g) {
    switch (g) {
        case Genre::Folder:  return "";
        case Genre::Zip:     return "ZIP";
        case Genre::Image:   return "IMG";
        case Genre::Text:    return "TXT";
        case Genre::Pdf:     return "PDF";
        case Genre::Sound:   return "SON";
        case Genre::Video:   return "VID";
        case Genre::Csv:     return "CSV";
        case Genre::Project: return "XPG";
        case Genre::Other:   break;
    }
    return "";
}

bool nameContains(std::string_view name, std::string_view search) {
    return search.empty() || folded(name).find(folded(search)) != std::string::npos;
}

bool validStep(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 255) return false;
    for (const char c : name)
        if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) return false;
    return true;
}

bool splitSteps(std::string_view path, std::vector<std::string>& out) {
    out.clear();
    std::size_t i = 0;
    while (i <= path.size()) {
        std::size_t j = i;
        while (j < path.size() && path[j] != '/' && path[j] != '\\') ++j;
        const std::string_view step = path.substr(i, j - i);
        i = j + 1;
        if (step.empty()) continue;
        if (!validStep(step)) return false;
        out.emplace_back(step);
    }
    return true;
}

Listing list(const Location& loc, const Options& opt) {
    Listing out;
    Place pl;
    if (!resolve(loc, pl, out.error)) return out;
    out.inZip = pl.zip;
    const std::size_t maxItems = std::max<std::size_t>(1, opt.maxItems);
    const auto take = [&](Entry e) {
        ++out.total;
        if (!passes(e, opt)) return;
        if (out.entries.size() >= maxItems) {
            out.truncated = true;
            return;
        }
        out.entries.push_back(std::move(e));
    };
    if (!pl.zip) {
        std::error_code ec;
        fs::directory_iterator it(pl.disk, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            out.error = "dossier illisible : " + U(pl.disk) + " (" + ec.message() + ")";
            return out;
        }
        std::size_t seen = 0;
        for (const fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;
            if (++seen > 200000) {   // un dossier demesure : on cesse de compter
                out.truncated = true;
                break;
            }
            Entry e;
            const fs::path p = it->path();
            e.name = U(p.filename());
            e.path = U(p);
            std::error_code e2;
            e.folder = it->is_directory(e2);
            e.genre = genreOf(e.name, e.folder);
            if (!e.folder) {
                const auto sz = it->file_size(e2);
                if (!e2) e.size = sz;
            }
            std::error_code e3;
            const auto t = it->last_write_time(e3);
            if (!e3) e.modified = secondsOf(t);
            take(std::move(e));
        }
        return out;
    }
    // Dans un zip : les entrees directement sous le prefixe ; un nom plus profond fait naitre son dossier.
    Archive& a = *pl.archive;
    out.truncated = a.truncated();
    std::set<std::string> folders;
    mz_zip_archive_file_stat st;
    for (std::size_t i = 0, n = a.count(); i < n; ++i) {
        if (!a.stat(i, st)) continue;
        bool suspicious = false;
        const std::string name = cleanName(st.m_filename, suspicious);
        out.suspicious += suspicious;   // tout le zip : ce qu'il disait de sortir (la Console le dit)
        if (name.empty() || name.size() <= pl.prefix.size() || name.rfind(pl.prefix, 0) != 0) continue;
        const std::string rest = name.substr(pl.prefix.size());
        const auto slash = rest.find('/');
        if (slash != std::string::npos || st.m_is_directory) {
            const std::string folder = slash == std::string::npos ? rest : rest.substr(0, slash);
            if (!folders.insert(folder).second) continue;
            Entry e;
            e.name = folder;
            e.path = pl.shown + sep() + folder;
            e.folder = true;
            e.inZip = true;
            e.genre = Genre::Folder;
            if (st.m_is_directory && slash == std::string::npos) e.modified = static_cast<std::int64_t>(st.m_time);
            take(std::move(e));
            continue;
        }
        Entry e;
        e.name = rest;
        e.path = pl.shown + sep() + rest;
        e.inZip = true;
        e.genre = genreOf(rest, false);
        e.size = st.m_uncomp_size;
        e.packed = st.m_comp_size;
        e.modified = static_cast<std::int64_t>(st.m_time);
        e.suspicious = suspicious;
        take(std::move(e));
    }
    return out;
}

SortKey sortKeyFrom(std::string_view w) {
    const std::string s = lowerAscii(std::string(w));
    if (s.rfind("taille", 0) == 0 || s == "size") return SortKey::Size;
    if (s.rfind("date", 0) == 0) return SortKey::Date;
    if (s.rfind("genre", 0) == 0 || s.rfind("type", 0) == 0) return SortKey::Genre;
    return SortKey::Name;
}

std::string_view sortKeyWord(SortKey k) {
    switch (k) {
        case SortKey::Size:  return "taille";
        case SortKey::Date:  return "date";
        case SortKey::Genre: return "genre";
        case SortKey::Name:  break;
    }
    return "nom";
}

void sort(std::vector<Entry>& v, SortKey key, bool descending) {
    std::stable_sort(v.begin(), v.end(), [&](const Entry& a, const Entry& b) {
        if (a.folder != b.folder) return a.folder;    // les dossiers d'abord, dans les deux sens
        const auto byName = [&] {
            const std::string fa = folded(a.name), fb = folded(b.name);
            return fa != fb ? fa < fb : a.name < b.name;
        };
        bool less = false;
        switch (key) {
            case SortKey::Size:
                if (a.size == b.size) return byName();
                less = a.size < b.size;
                break;
            case SortKey::Date:
                if (a.modified == b.modified) return byName();
                less = a.modified < b.modified;
                break;
            case SortKey::Genre:
                if (a.genre == b.genre) return byName();
                less = a.genre < b.genre;
                break;
            case SortKey::Name: {
                const std::string fa = folded(a.name), fb = folded(b.name);
                if (fa == fb) return descending ? b.name < a.name : a.name < b.name;
                less = fa < fb;
                break;
            }
        }
        return descending ? !less : less;
    });
}

std::string filterProblem(std::string_view text) {
    std::string item;
    const auto check = [&](std::string it) -> std::string {
        while (!it.empty() && std::isspace(static_cast<unsigned char>(it.back()))) it.pop_back();
        std::size_t a = 0;
        while (a < it.size() && std::isspace(static_cast<unsigned char>(it[a]))) ++a;
        it = it.substr(a);
        if (it.empty()) return {};
        std::string e = it;
        if (e.rfind("*.", 0) == 0) e = e.substr(2);
        else if (e.rfind('.', 0) == 0) e = e.substr(1);
        if (e.empty() || e.size() > 12) return "\xC2\xAB " + it + " \xC2\xBB n'est pas une extension (*.csv, *.pdf)";
        for (const char c : e)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                return "\xC2\xAB " + it + " \xC2\xBB n'est pas une extension (*.csv, *.pdf)";
        return {};
    };
    for (const char c : text) {
        if (c == ';' || c == ',') {
            if (auto p = check(item); !p.empty()) return p;
            item.clear();
        } else {
            item += c;
        }
    }
    return check(item);
}

std::string sizeText(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " octet" + (bytes > 1 ? "s" : "");
    return formatBytes(bytes);
}

std::string dateText(std::int64_t s) {
    if (s <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(s);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return b;
}

Tip tipOf(const Location& loc, const Entry& e) {
    Tip t;
    t.title = e.name;
    std::string body = wrappedPath(e.path);
    const auto line = [&](const std::string& s) { body += "\n" + s; };
    line(genreLabel(e.genre, e.name) + (e.inZip ? " \xC2\xB7 dans un zip" : ""));
    if (!e.folder) {
        std::string size = "Taille : " + sizeText(e.size);
        if (e.size >= 1024) size += " (" + std::to_string(e.size) + " octets)";
        if (e.inZip) {
            size += " \xC2\xB7 compress\xC3\xA9" "e " + sizeText(e.packed);
            if (e.size > 0) {
                const double rate = 100.0 * (1.0 - static_cast<double>(e.packed) / static_cast<double>(e.size));
                char b[32];
                std::snprintf(b, sizeof b, "%.0f", std::max(0.0, rate));
                size += " (taux " + std::string(b) + " %)";
            }
        }
        line(size);
    }
    if (e.modified > 0) line("Modifi\xC3\xA9 le " + dateText(e.modified));
    const std::int64_t created = e.inZip ? 0 : createdOf(P(e.path));
    if (created > 0) line("Cr\xC3\xA9\xC3\xA9 le " + dateText(created));
    // Un dossier : ses elements et sa taille totale, comptes jusqu'a kFolderCount (au-dela : arrete, dit).
    if (e.folder) {
        std::size_t files = 0, folders = 0;
        std::uint64_t total = 0;
        bool stopped = false;
        if (!e.inZip) {
            std::error_code ec;
            fs::recursive_directory_iterator it(P(e.path), fs::directory_options::skip_permission_denied, ec);
            for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
                if (files + folders >= kFolderCount) {
                    stopped = true;
                    break;
                }
                std::error_code e2;
                if (it->is_directory(e2)) {
                    ++folders;
                } else {
                    ++files;
                    const auto sz = it->file_size(e2);
                    if (!e2) total += sz;
                }
            }
        } else {
            Place pl;
            std::string why;
            Location inner = loc;
            if (resolve(inner, pl, why) && pl.zip) {
                const std::string want = pl.prefix + e.name + "/";
                std::set<std::string> dirs;
                mz_zip_archive_file_stat st;
                for (std::size_t i = 0, n = pl.archive->count(); i < n; ++i) {
                    if (!pl.archive->stat(i, st)) continue;
                    bool s = false;
                    const std::string name = cleanName(st.m_filename, s);
                    if (name.rfind(want, 0) != 0 || name.size() == want.size()) continue;
                    const std::string rest = name.substr(want.size());
                    for (std::size_t k = rest.find('/'); k != std::string::npos; k = rest.find('/', k + 1)) dirs.insert(rest.substr(0, k));
                    if (st.m_is_directory) dirs.insert(rest);
                    else {
                        ++files;
                        total += st.m_uncomp_size;
                    }
                }
                folders = dirs.size();
            }
        }
        std::string what = std::to_string(files) + " fichier" + (files > 1 ? "s" : "");
        if (folders > 0) what += ", " + std::to_string(folders) + " dossier" + (folders > 1 ? "s" : "");
        line(stopped ? "Contenu : plus de " + std::to_string(kFolderCount) + " \xC3\xA9l\xC3\xA9ments (compte arr\xC3\xAAt\xC3\xA9), au moins " + sizeText(total)
                     : "Contenu : " + what + ", " + sizeText(total) + " en tout");
    }
    // Un zip sur le disque : ses entrees, ce qu'elles font une fois decompressees (sa table seule est lue).
    if (e.genre == Genre::Zip && !e.inZip) {
        Archive a;
        if (a.openFile(e.path)) {
            std::uint64_t total = 0;
            mz_zip_archive_file_stat st;
            std::size_t n = a.count(), files = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (a.stat(i, st) && !st.m_is_directory) {
                    ++files;
                    total += st.m_uncomp_size;
                }
            line("Archive : " + std::to_string(files) + " fichier" + (files > 1 ? "s" : "") + ", " + sizeText(total) + " d\xC3\xA9" "compress\xC3\xA9s"
                 + (a.truncated() ? " (plus de 100000 entr\xC3\xA9" "es : compte arr\xC3\xAAt\xC3\xA9)" : ""));
        } else {
            line("Archive illisible");
        }
    }
    // Une image : ses dimensions (ses premiers octets) ; un texte : ses premieres lignes.
    if (e.genre == Genre::Image) {
        std::vector<unsigned char> head;
        if (headOf(loc, e, 256u * 1024u, head)) {
            const Bytes bytes(head.begin(), head.end());
            const MediaInfo info = inspectMedia(bytes, e.name);
            if (info.width > 0 && info.height > 0) line("Image : " + std::to_string(info.width) + " \xC3\x97 " + std::to_string(info.height) + " pixels");
        }
    }
    if (e.genre == Genre::Text || e.genre == Genre::Csv) {
        std::vector<unsigned char> head;
        if (extensionOf(e.name) != "xls" && extensionOf(e.name) != "xlsx" && headOf(loc, e, 2048, head)) {
            std::string text(head.begin(), head.end());
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
                && static_cast<unsigned char>(text[2]) == 0xBF)
                text.erase(0, 3);
            int lines = 0;
            std::size_t from = 0;
            while (from < text.size() && lines < 4) {
                auto to = text.find('\n', from);
                if (to == std::string::npos) to = text.size();
                std::string l = text.substr(from, to - from);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                for (auto& c : l)
                    if (static_cast<unsigned char>(c) < 0x20) c = ' ';
                if (codepoints(l) > 70) {
                    std::size_t cut = 0, n = 0;
                    while (cut < l.size() && n < 70) {
                        ++cut;
                        while (cut < l.size() && (static_cast<unsigned char>(l[cut]) & 0xC0) == 0x80) ++cut;
                        ++n;
                    }
                    l = l.substr(0, cut) + "\xE2\x80\xA6";
                }
                line((lines == 0 ? "\xC2\xAB " : "  ") + l);
                ++lines;
                from = to + 1;
            }
            if (lines > 0) body += " \xC2\xBB";
        }
    }
    if (e.suspicious) line("Nom ramen\xC3\xA9 dans l'archive : le zip disait ../ ou un chemin absolu");
    t.body = std::move(body);
    return t;
}

bool extract(const Location& loc, const Entry& e, const std::string& folder, std::string& written, std::string& why) {
    if (!e.inZip || e.folder) {
        why = "seul un fichier d'un zip s'extrait";
        return false;
    }
    if (!validStep(e.name)) {
        why = "nom refus\xC3\xA9 : " + e.name;
        return false;
    }
    if (e.size > kMaxExtract) {
        why = "trop gros pour \xC3\xAAtre extrait (" + sizeText(e.size) + ", 256 Mo au plus)";
        return false;
    }
    Place pl;
    if (!resolve(loc, pl, why)) return false;
    if (!pl.zip) {
        why = "le dossier courant n'est pas dans un zip";
        return false;
    }
    const long at = findEntry(*pl.archive, pl.prefix + e.name);
    if (at < 0) {
        why = "introuvable dans le zip : " + e.name;
        return false;
    }
    std::error_code ec;
    const fs::path dir = P(folder);
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) {
        why = "dossier d'extraction impossible : " + folder;
        return false;
    }
    const fs::path target = dir / P(e.name);
    if (fs::exists(target, ec)) {
        why = U(target) + " existe d\xC3\xA9j\xC3\xA0 : rien n'est \xC3\xA9" "cras\xC3\xA9";
        return false;
    }
    std::vector<unsigned char> data;
    if (!pl.archive->read(static_cast<std::size_t>(at), kMaxExtract, data)) {
        why = "entr\xC3\xA9" "e illisible ou trop grosse : " + e.name;
        return false;
    }
    std::ofstream out(target, std::ios::binary);
    if (!out) {
        why = "\xC3\xA9" "criture impossible : " + U(target);
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) {
        why = "\xC3\xA9" "criture inachev\xC3\xA9" "e : " + U(target);
        return false;
    }
    written = U(target);
    return true;
}

Layout layoutOf(double w, double h, double fontSize, const std::vector<std::string>& crumbs, bool searchBox, bool refresh, bool extract,
                bool parent, bool showSize, bool showDate, std::size_t rows, std::size_t first, const TextWidth& measure) {
    Layout l;
    const double fs = std::clamp(fontSize, 7.0, 40.0);
    const double pad = 6;
    const double barH = std::max(40.0, fs * 2.6);       // des cibles de 32 pixels au moins (le doigt)
    const double bh = barH - 8, by = 4;
    l.rowHeight = std::max(24.0, fs * 1.9);
    l.bar = {0, 0, w, barH};
    double x = pad, right = w - pad;
    l.up = {x, by, bh, bh};
    x += bh + 6;
    if (extract) {
        const double ew = std::max(84.0, fs * 7.0);
        l.extract = {right - ew, by, ew, bh};
        right -= ew + 4;
    }
    if (refresh) {
        l.refresh = {right - bh, by, bh, bh};
        right -= bh + 4;
    }
    if (searchBox) {
        const double sw = std::clamp(w * 0.28, 110.0, 220.0);
        if (right - x > sw + 90) {
            l.search = {right - sw, by, sw, bh};
            right -= sw + 6;
        }
    }
    // Le fil d'Ariane : les dernieres etapes qui tiennent ; celles d'avant se resument en "..." devant. Une etape : son
    // texte mesure avec la police (le peintre et la souris passent `measure`), plus 4 pixels devant (le texte "gauche")
    // et 6 avant le separateur ; sans mesure (Generer), une estimation : 0,5 em par caractere.
    // 1.14.1 (lot N5, retouche) : 0,56 em par caractere laissait un grand vide apres un nom long (archives_2026.zip).
    const auto approx = [&](const std::string& s) {
        const double text = measure ? measure(s, fs) : static_cast<double>(codepoints(s)) * fs * 0.5;
        return std::max(0.0, text) + 10;
    };
    const double sepW = fs * 1.1, dots = fs * 1.6;
    const double avail = std::max(0.0, right - x);
    l.crumbs.assign(crumbs.size(), Box{});
    std::size_t firstCrumb = crumbs.size();
    double used = 0;
    while (firstCrumb > 0) {
        const double cw = approx(crumbs[firstCrumb - 1]) + (firstCrumb < crumbs.size() ? sepW : 0);
        const double room = avail - (firstCrumb > 1 ? dots : 0);
        if (used + cw > room && firstCrumb < crumbs.size()) break;
        used += cw;
        --firstCrumb;
    }
    l.firstCrumb = firstCrumb;
    double cx = x + (firstCrumb > 0 ? dots : 0);
    for (std::size_t i = firstCrumb; i < crumbs.size(); ++i) {
        const double cw = std::max(0.0, std::min(approx(crumbs[i]), right - cx));
        l.crumbs[i] = {cx, by, cw, bh};
        cx += cw + sepW;
    }
    // Les titres des colonnes, puis les lignes ; la ligne d'etat en bas.
    double y = barH;
    const double sizeW = showSize ? std::max(76.0, fs * 6.2) : 0.0, dateW = showDate ? std::max(124.0, fs * 9.6) : 0.0;
    l.header = {0, y, w, l.rowHeight};
    if (showDate) l.colDate = {w - pad - dateW, y, dateW, l.rowHeight};
    if (showSize) l.colSize = {w - pad - dateW - (showDate ? 8 : 0) - sizeW, y, sizeW, l.rowHeight};
    const double nameRight = showSize ? l.colSize.x : showDate ? l.colDate.x : w - pad;
    l.colName = {pad, y, std::max(0.0, nameRight - pad - 6), l.rowHeight};
    y += l.rowHeight;
    const double statusH = std::max(20.0, fs * 1.6);
    l.status = {pad, std::max(y, h - statusH), std::max(0.0, w - 2 * pad), statusH};
    l.list = {0, y, w, std::max(0.0, l.status.y - y)};
    double ry = y;
    if (parent) {
        l.parent = {0, ry, w, l.rowHeight};
        ry += l.rowHeight;
    }
    const auto slots = static_cast<std::size_t>(std::max(0.0, std::floor((l.status.y - ry) / l.rowHeight)));
    bool up = false, down = false;
    std::size_t shown = std::min(rows, slots);
    if (rows <= slots) {
        first = 0;
    } else if (slots >= 1) {
        const std::size_t lastFirst = rows - std::max<std::size_t>(1, slots - 1);
        first = std::min(first, lastFirst);
        up = first > 0;
        const std::size_t room = slots - (up ? 1 : 0);
        if (first + room >= rows) {
            shown = rows - first;
        } else {
            down = true;
            shown = room > 1 ? room - 1 : 0;
        }
    } else {
        shown = 0;
    }
    l.first = first;
    if (up) {
        l.upArrow = {0, ry, w, l.rowHeight};
        ry += l.rowHeight;
    }
    for (std::size_t i = 0; i < shown; ++i) {
        l.rows.push_back({0, ry, w, l.rowHeight});
        ry += l.rowHeight;
    }
    if (down) l.downArrow = {0, ry, w, l.rowHeight};
    return l;
}

std::string hit(const Layout& l, double x, double y) {
    const auto in = [&](const Box& b) { return b.w > 0 && b.h > 0 && b.contains(x, y); };
    if (in(l.bar)) {
        if (in(l.up)) return "monter";
        if (in(l.search)) return "recherche";
        if (in(l.refresh)) return "rafraichir";
        if (in(l.extract)) return "extraire";
        for (std::size_t i = 0; i < l.crumbs.size(); ++i)
            if (in(l.crumbs[i])) return "ariane:" + std::to_string(i);
        return {};
    }
    if (in(l.header)) {
        if (in(l.colSize)) return "colonne:taille";
        if (in(l.colDate)) return "colonne:date";
        return "colonne:nom";
    }
    if (in(l.parent)) return "parent";
    if (in(l.upArrow)) return "liste:haut";
    if (in(l.downArrow)) return "liste:bas";
    for (std::size_t i = 0; i < l.rows.size(); ++i)
        if (in(l.rows[i])) return "ligne:" + std::to_string(l.first + i);
    return {};
}

} // namespace hmi::explorer
