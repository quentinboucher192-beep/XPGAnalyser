// =============================================================================
//  app/hmi/HmiDocPainter.cpp - 1.12.30 : LA VISIONNEUSE DE DOCUMENTS A L'ECRAN
// -----------------------------------------------------------------------------
//  Ce que HmiDocView.hpp dispose (la barre d'outils, la recherche, la colonne
//  des vignettes, les pages a leur zoom), dessine. En marche, l'etat est celui du
//  moteur (Runtime::docViewer : la page, le zoom, le defilement, la saisie) ;
//  dans l'editeur, un etat neuf a la page de sa propriete Page.
//
//  Les pages d'un PDF arrivent du fil des documents (HmiDocument.hpp) : on
//  dessine la meilleure image qu'on a (une autre echelle, une vignette) en
//  attendant la bonne ; seules les pages visibles sont demandees. Un texte
//  s'ecrit ligne a ligne a la taille du zoom. Une page tournee est l'image de la
//  page droite, tournee au dessin.
// =============================================================================
#include "HmiImages.hpp"
#include "HmiPaintKit.hpp"
#include "../../hmi/HmiDocView.hpp"
#include "../../hmi/HmiPdf.hpp"
#include "../../hmi/HmiRoles.hpp"
#include "../../hmi/HmiRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace app::paint {

namespace {

namespace dv = hmi::docview;
namespace dc = hmi::doc;

gfx::Color mix(gfx::Color a, gfx::Color b, float t) {
    const auto m = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), a.a};
}
gfx::Color withAlpha(gfx::Color c, float a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0.f, 1.f) * c.a)); }

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Un rectangle local (ses coins arrondis de `radius`), en points locaux.
std::vector<gfx::Point> rectAt(const hmi::Box& b, double radius) {
    return shapes::roundedRect(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(std::max(0.0, b.w)),
                               static_cast<float>(std::max(0.0, b.h)), static_cast<float>(std::clamp(radius, 0.0, std::min(b.w, b.h) / 2)));
}

// Un rectangle local plein ; trace.
void box(const Ctx& c, const hmi::Box& b, gfx::Color col, double radius = 0) {
    if (b.w <= 0 || b.h <= 0 || col.a == 0) return;
    shapes::fillPolygon(c.r, c.map(rectAt(b, radius)), col);
}
void frameOf(const Ctx& c, const hmi::Box& b, gfx::Color col, double width, double radius = 0) {
    if (b.w <= 0 || b.h <= 0 || col.a == 0) return;
    shapes::strokePolyline(c.r, c.map(rectAt(b, radius)), true, col, std::max(1.f, static_cast<float>(width) * c.vp.zoom));
}
void seg(const Ctx& c, double x0, double y0, double x1, double y1, gfx::Color col, double width) {
    c.r.line(c.map(x0, y0), c.map(x1, y1), col, std::max(1.f, static_cast<float>(width) * c.vp.zoom));
}
void tri(const Ctx& c, std::vector<gfx::Point> pts, gfx::Color col) { shapes::fillPolygon(c.r, c.map(pts), col); }

// Le cadre d'un rectangle local, a l'ecran (sa boite englobante) : la decoupe.
gfx::Rect screenRect(const Ctx& c, const hmi::Box& b) {
    const gfx::Point corners[4] = {c.map(b.x, b.y), c.map(b.x + b.w, b.y), c.map(b.x, b.y + b.h), c.map(b.x + b.w, b.y + b.h)};
    float x0 = corners[0].x, y0 = corners[0].y, x1 = x0, y1 = y0;
    for (const auto& p : corners) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

// Une ligne de texte telle quelle (un document : ni \n ni police de l'objet), son coin haut gauche en local.
void rawText(const Ctx& c, std::string_view s, double lx, double ly, double sizePx, gfx::Color col) {
    if (s.empty() || col.a == 0) return;
    const gfx::FontId f{static_cast<std::uint16_t>(std::clamp(std::lround(sizePx * c.vp.zoom), 4L, 400L))};
    const gfx::Point at = c.map(lx, ly);
    const float angle = static_cast<float>(c.o.rotation()) + c.vp.angle;
    if (std::fabs(angle) < 0.01f) c.r.drawText(at, s, f, col);
    else c.r.drawTextRotated(at, s, f, col, angle, at);
}
// Sa largeur, en pixels locaux.
double rawWidth(const Ctx& c, std::string_view s, double sizePx) {
    const gfx::FontId f{static_cast<std::uint16_t>(std::clamp(std::lround(sizePx * c.vp.zoom), 4L, 400L))};
    return c.r.measure(s, f).width / std::max(0.01, static_cast<double>(c.vp.zoomX()));
}

// Une image (une page, une vignette) dans un rectangle local, tournee de `quarter` quarts de tour : l'image est
// celle de la page droite, `upright` (sa taille droite, en local).
void drawPicture(const Ctx& c, const HmiImageCache::Image& img, const hmi::Box& at, int quarter, gfx::Color tint) {
    if (!img.valid()) return;
    const gfx::Point centre = c.map(at.x + at.w / 2, at.y + at.h / 2);
    const bool odd = (quarter & 1) != 0;
    const double uw = odd ? at.h : at.w, uh = odd ? at.w : at.h;
    const float sw = static_cast<float>(uw * c.vp.zoomX()), sh = static_cast<float>(uh * c.vp.zoomY());
    c.r.drawImage(img.tex, {centre.x - sw / 2, centre.y - sh / 2, sw, sh},
                  static_cast<float>(90 * (quarter % 4)) + static_cast<float>(c.o.rotation()) + c.vp.angle, false, false, tint);
}

// ---- les icones des boutons (dans leur case locale) ----------------------------------------------------
void icon(const Ctx& c, std::string_view part, const hmi::Box& b, gfx::Color col) {
    const double cx = b.x + b.w / 2, cy = b.y + b.h / 2, s = std::min(b.w, b.h) * 0.22;
    const double lw = 1.8;
    const auto left = [&](double x) { tri(c, {{static_cast<float>(x + s * 0.55), static_cast<float>(cy - s)}, {static_cast<float>(x + s * 0.55), static_cast<float>(cy + s)}, {static_cast<float>(x - s * 0.55), static_cast<float>(cy)}}, col); };
    const auto right = [&](double x) { tri(c, {{static_cast<float>(x - s * 0.55), static_cast<float>(cy - s)}, {static_cast<float>(x - s * 0.55), static_cast<float>(cy + s)}, {static_cast<float>(x + s * 0.55), static_cast<float>(cy)}}, col); };
    if (part == "doc:first") {
        seg(c, cx - s * 0.95, cy - s, cx - s * 0.95, cy + s, col, lw);
        left(cx + s * 0.25);
    } else if (part == "doc:prev") {
        left(cx);
    } else if (part == "doc:next") {
        right(cx);
    } else if (part == "doc:last") {
        right(cx - s * 0.25);
        seg(c, cx + s * 0.95, cy - s, cx + s * 0.95, cy + s, col, lw);
    } else if (part == "doc:zoomout" || part == "doc:zoomin") {
        seg(c, cx - s, cy, cx + s, cy, col, lw + 0.4);
        if (part == "doc:zoomin") seg(c, cx, cy - s, cx, cy + s, col, lw + 0.4);
    } else if (part == "doc:rotate") {
        // Un arc et sa fleche, dans le sens horaire.
        std::vector<gfx::Point> arc = shapes::arc({static_cast<float>(cx), static_cast<float>(cy)}, static_cast<float>(s * 1.05), -200.f, 60.f, 18);
        shapes::strokePolyline(c.r, c.map(arc), false, col, std::max(1.f, static_cast<float>(lw) * c.vp.zoom));
        const gfx::Point tip = arc.back();
        const double a = 60.0 * 3.14159265358979 / 180.0;
        const double tx = std::cos(a + 1.5708), ty = std::sin(a + 1.5708);   // la tangente, vers la fin de l'arc
        const double nx = std::cos(a), ny = std::sin(a);
        const double h = s * 0.55;
        tri(c, {{static_cast<float>(tip.x + tx * h), static_cast<float>(tip.y + ty * h)},
                {static_cast<float>(tip.x + nx * h * 0.7), static_cast<float>(tip.y + ny * h * 0.7)},
                {static_cast<float>(tip.x - nx * h * 0.7), static_cast<float>(tip.y - ny * h * 0.7)}}, col);
    } else if (part == "doc:thumbs") {
        // La colonne des vignettes : une bande a gauche, deux pages.
        frameOf(c, {cx - s * 1.1, cy - s * 1.05, s * 2.2, s * 2.1}, col, 1.4);
        seg(c, cx - s * 0.25, cy - s * 1.05, cx - s * 0.25, cy + s * 1.05, col, 1.4);
        box(c, {cx - s * 0.95, cy - s * 0.85, s * 0.5, s * 0.65}, col);
        box(c, {cx - s * 0.95, cy + s * 0.15, s * 0.5, s * 0.65}, col);
    } else if (part == "doc:search") {
        const double r = s * 0.72;
        std::vector<gfx::Point> ring = shapes::ellipse({static_cast<float>(cx - s * 0.25), static_cast<float>(cy - s * 0.25)}, static_cast<float>(r), static_cast<float>(r), 20);
        shapes::strokePolyline(c.r, c.map(ring), true, col, std::max(1.f, static_cast<float>(lw) * c.vp.zoom));
        seg(c, cx + s * 0.28, cy + s * 0.28, cx + s * 1.0, cy + s * 1.0, col, lw + 0.8);
    } else if (part == "doc:searchclose") {
        seg(c, cx - s * 0.8, cy - s * 0.8, cx + s * 0.8, cy + s * 0.8, col, lw);
        seg(c, cx - s * 0.8, cy + s * 0.8, cx + s * 0.8, cy - s * 0.8, col, lw);
    } else if (part == "doc:hitprev" || part == "doc:hitnext") {
        const double d = part == "doc:hitprev" ? -1 : 1;
        seg(c, cx - s, cy - d * s * 0.45, cx, cy + d * s * 0.45, col, lw);
        seg(c, cx, cy + d * s * 0.45, cx + s, cy - d * s * 0.45, col, lw);
    }
}

std::string tipOf(const dc::Document::Search& s, int hit) {
    if (s.query.empty()) return {};
    if (s.running && s.hits.empty()) return "recherche\xE2\x80\xA6";
    if (s.hits.empty()) return "aucun";
    if (hit >= 0) return std::to_string(hit + 1) + " / " + std::to_string(s.hits.size());
    return std::to_string(s.hits.size()) + (s.hits.size() > 1 ? " trouv\xC3\xA9s" : " trouv\xC3\xA9");
}

} // namespace

bool drawDocumentViewer(const Ctx& c) {
    if (c.o.kind != hmi::Kind::DocumentViewer) return false;
    const double w = c.w(), h = c.h();
    if (w < 4 || h < 4) return true;

    // ---- l'etat : celui du moteur (en marche), ou un etat neuf (l'editeur, un apercu)
    const hmi::Runtime::DocViewer* live = !c.opt.editor && c.opt.runtime ? c.opt.runtime->docViewer(c.o.id) : nullptr;
    dv::Settings settings;
    dv::State st;
    std::shared_ptr<dc::Document> d;
    std::string why;
    std::vector<dc::Size> sizes;
    bool navigation = true;
    if (live) {
        settings = live->settings;
        st = live->state;
        d = live->document;
        why = live->why;
        navigation = live->navigation;
        sizes = live->sizes;
        if (sizes.empty() && d && d->state() == dc::Document::State::Ready) sizes = d->pageSizes();
    } else {
        settings = dv::settingsOf(c.o);
        settings.toolbar = c.src.flag(c.o, "showToolbar", true);
        settings.thumbnails = c.src.flag(c.o, "showThumbnails", false);
        settings.navigation = c.src.flag(c.o, "allowNavigation", true);
        navigation = settings.navigation;
        st = dv::initialState(settings);
        const std::string spec = trimmed(hmi::roles::nameOf(c.src.text(c.o, "document")));
        if (!spec.empty() && c.opt.assets) d = HmiImageCache::instance().document(*c.opt.assets, spec, &why);
        else if (!spec.empty()) why = "ressources du projet indisponibles ici";
        if (d && d->state() == dc::Document::State::Ready) sizes = d->pageSizes();
    }
    const bool ready = d && d->state() == dc::Document::State::Ready && !sizes.empty();
    const dc::Type type = d ? d->type() : dc::Type::None;
    if (type == dc::Type::Text) st.quarter = 0;
    if (!live && ready) {
        const int n = static_cast<int>(sizes.size());
        const int page = std::clamp(static_cast<int>(std::lround(c.src.number(c.o, "page", 1))), 1, n);
        if (!navigation) st.single = page;
        const auto l0 = dv::layoutOf(w, h, settings, st);
        dv::settle(st, l0, sizes);
        if (navigation) (void)dv::goToPage(st, l0, sizes, page);
    }
    const dv::Layout l = dv::layoutOf(w, h, settings, st);
    if (ready) dv::settle(st, l, sizes);
    const dv::Content content = dv::contentOf(l, st, sizes);

    // ---- les couleurs
    const gfx::Color bg = c.color("fill", gfx::Color::rgb(0x4A5059));
    const gfx::Color accent = c.color("accent", gfx::Color::rgb(0x2F6FD6));
    const gfx::Color ink = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color bar = mix(bg, gfx::Color{0, 0, 0, bg.a}, 0.55f);
    const gfx::Color field = mix(bg, gfx::Color{0, 0, 0, bg.a}, 0.75f);
    const gfx::Color muted = withAlpha(ink, 0.6f);
    const gfx::Color paper = c.fixed(0xFFFFFF);

    box(c, {0, 0, w, h}, bg);

    // ---- les pages
    dc::Document::Search search = d ? d->searchState() : dc::Document::Search{};
    // Une seule page montree : ses occurrences seulement (le moteur les compte ainsi).
    if (st.single > 0) std::erase_if(search.hits, [&](const dc::Hit& hit) { return hit.page != st.single - 1; });
    if (l.area.w > 4 && l.area.h > 4) {
        c.r.pushClip(screenRect(c, l.area));
        if (ready) {
            const double screenScale = content.zoom * std::max(0.05, static_cast<double>(c.vp.zoom));
            for (const auto& placed : dv::visiblePages(l, content)) {
                const auto& r = placed.rect;
                box(c, {r.x + 2, r.y + 3, r.w, r.h}, withAlpha(c.fixed(0x000000), 0.28f));
                box(c, r, paper);
                const auto& size = sizes[static_cast<std::size_t>(placed.index)];
                if (type == dc::Type::Text) {
                    const auto lines = d->textLines(placed.index);
                    const double z = content.zoom;
                    // Trop petit pour se lire (moins de 5 pixels) : des traits gris a la place des lignes.
                    const bool tiny = dc::kTextFont * z * c.vp.zoom < 5.0;
                    for (std::size_t i = 0; i < lines.size(); ++i) {
                        const double ly = r.y + (dc::kTextMargin + dc::kTextLine * static_cast<double>(i)) * z;
                        if (ly > l.area.y + l.area.h) break;
                        if (ly + dc::kTextLine * z < l.area.y) continue;
                        if (tiny) {
                            if (trimmed(lines[i]).empty()) continue;
                            const double lw = std::min(r.w - 2 * dc::kTextMargin * z, static_cast<double>(lines[i].size()) * dc::kTextFont * 0.5 * z);
                            box(c, {r.x + dc::kTextMargin * z, ly + dc::kTextLine * z * 0.25, lw, std::max(0.6, dc::kTextFont * 0.45 * z)}, c.fixed(0xB8BEC8));
                        } else {
                            rawText(c, lines[i], r.x + dc::kTextMargin * z, ly, dc::kTextFont * z, c.fixedText(0x1E2328));
                        }
                    }
                } else {
                    const auto pic = d->page(placed.index, screenScale);
                    const auto img = pic.image ? HmiImageCache::instance().docPage(c.r, pic.image) : HmiImageCache::Image{};
                    if (img.valid()) drawPicture(c, img, r, st.quarter, gfx::Color{255, 255, 255, static_cast<std::uint8_t>(255 * c.alpha)});
                    else if (!pic.image)
                        text(c, "\xE2\x80\xA6", r.x, r.y, r.w, std::min(r.h, 60.0), c.fixed(0x9AA6B8), 24, "centre");
                }
                // Les occurrences de la recherche sur cette page ; celle qu'on montre, cerclee.
                for (std::size_t k = 0; k < search.hits.size(); ++k) {
                    const auto& hit = search.hits[k];
                    if (hit.page != placed.index) continue;
                    const bool current = static_cast<int>(k) == st.hit;
                    std::vector<hmi::Box> rects;
                    if (type == dc::Type::Text && hit.line >= 0) {
                        const auto lines = d->textLines(placed.index);
                        if (static_cast<std::size_t>(hit.line) < lines.size()) {
                            const std::string& line = lines[static_cast<std::size_t>(hit.line)];
                            // Le debut et la fin de l'occurrence, en octets (des caracteres UTF-8).
                            std::size_t a = 0, cps = 0;
                            while (a < line.size() && cps < static_cast<std::size_t>(hit.col)) {
                                ++a;
                                while (a < line.size() && (static_cast<unsigned char>(line[a]) & 0xC0) == 0x80) ++a;
                                ++cps;
                            }
                            std::size_t b = a;
                            for (int n = 0; n < hit.len && b < line.size(); ++n) {
                                ++b;
                                while (b < line.size() && (static_cast<unsigned char>(line[b]) & 0xC0) == 0x80) ++b;
                            }
                            const double z = content.zoom, fs = dc::kTextFont * z;
                            const double x0 = r.x + dc::kTextMargin * z + rawWidth(c, std::string_view(line).substr(0, a), fs);
                            const double x1 = r.x + dc::kTextMargin * z + rawWidth(c, std::string_view(line).substr(0, b), fs);
                            const double y0 = r.y + (dc::kTextMargin + dc::kTextLine * hit.line) * z;
                            rects.push_back({x0, y0, std::max(2.0, x1 - x0), dc::kTextLine * z});
                        }
                    } else {
                        for (const auto& rect : hit.rects) rects.push_back(dv::onScreen(rect, size, placed, st.quarter, content.zoom));
                    }
                    for (const auto& rect : rects) {
                        box(c, rect, current ? withAlpha(c.fixed(0xF2994A), 0.45f) : withAlpha(c.fixed(0xF2C94C), 0.38f));
                        if (current) frameOf(c, {rect.x - 1, rect.y - 1, rect.w + 2, rect.h + 2}, c.fixed(0xE67E22), 2);
                    }
                }
            }
        } else {
            // Rien a montrer encore : ce qui manque, au milieu de la zone.
            std::string message;
            gfx::Color col = muted;
            bool failed = false;
            if (!d) {
                message = why.empty() || why == "aucun document"
                              ? std::string(c.opt.editor ? "Visionneuse de documents : choisis un document - une ressource PDF, une image, un texte, "
                                                           "ou un chemin"
                                                         : "Aucun document")
                              : why;
                if (!why.empty() && why != "aucun document") { col = c.fixed(0xF2994A); failed = true; }
            } else if (d->state() == dc::Document::State::Loading) {
                message = "Ouverture de " + d->name() + "\xE2\x80\xA6";
            } else {
                message = d->error();
                col = c.fixed(0xF2994A);
                failed = true;
            }
            // Un document (une page cornee), puis le message.
            const double iw = std::min(54.0, l.area.w * 0.3), ih = iw * 1.3;
            const double ix = l.area.x + (l.area.w - iw) / 2, iy = l.area.y + std::max(8.0, l.area.h * 0.5 - ih - 12);
            if (l.area.h > ih + 70) {
                const float fx = static_cast<float>(ix), fy = static_cast<float>(iy), fw = static_cast<float>(iw), fh = static_cast<float>(ih);
                const float k = fw * 0.3f;
                shapes::fillPolygon(c.r, c.map({{fx, fy}, {fx + fw - k, fy}, {fx + fw, fy + k}, {fx + fw, fy + fh}, {fx, fy + fh}}), withAlpha(paper, 0.18f));
                shapes::strokePolyline(c.r, c.map({{fx, fy}, {fx + fw - k, fy}, {fx + fw, fy + k}, {fx + fw, fy + fh}, {fx, fy + fh}}), true, col,
                                       std::max(1.f, 1.6f * c.vp.zoom));
                if (d && d->needsEngine()) text(c, "PDF", ix, iy + ih * 0.35, iw, ih * 0.4, col, std::max(8.0, iw * 0.28), "centre");
            }
            const double ty = l.area.h > ih + 70 ? iy + ih + 10 : l.area.y + 8;
            // Une erreur "quoi : le detail" (Lecteur PDF absent : ...) : son titre plus gros, le detail dessous.
            std::string title, detail = message;
            if (const auto colon = message.find(" : "); failed && colon != std::string::npos && colon > 0 && colon <= 32) {
                title = message.substr(0, colon);
                detail = message.substr(colon + 3);
                if (title[0] >= 'a' && title[0] <= 'z') title[0] = static_cast<char>(title[0] - 'a' + 'A');
            }
            const double tw = l.area.w - 24, bottom = l.area.y + l.area.h - 8;
            if (title.empty() || ty + 24 + 20 > bottom) {
                text(c, message, l.area.x + 12, ty, tw, std::max(20.0, bottom - ty), col, 13, "centre", true);
            } else {
                text(c, title, l.area.x + 12, ty, tw, 22, col, 16, "centre");
                // La hauteur du detail, ses lignes estimees : il suit le titre, sans trou.
                const double lines = std::ceil(rawWidth(c, detail, 13) / std::max(40.0, tw - 16)) + 0.5;
                const double dy = ty + 24;
                text(c, detail, l.area.x + 12, dy, tw, std::min(bottom - dy, std::max(20.0, lines * 13 * 1.25)), col, 13, "centre", true);
            }
        }
        c.r.popClip();
    }

    // ---- la colonne des vignettes
    if (l.thumbs.w > 0 && l.thumbs.h > 0) {
        box(c, l.thumbs, mix(bar, bg, 0.35f));
        seg(c, l.thumbs.x + l.thumbs.w, l.thumbs.y, l.thumbs.x + l.thumbs.w, l.thumbs.y + l.thumbs.h, withAlpha(c.fixed(0x000000), 0.4f), 1);
        if (ready) {
            c.r.pushClip(screenRect(c, l.thumbs));
            for (const auto& t : dv::thumbsOf(l, st, sizes)) {
                if (t.cell.y > l.thumbs.y + l.thumbs.h || t.cell.y + t.cell.h < l.thumbs.y) continue;
                const bool current = t.index + 1 == st.page;
                if (current) box(c, t.cell, withAlpha(accent, 0.22f), 4);
                box(c, t.frame, paper);
                if (type == dc::Type::Text) {
                    // Un texte : des lignes grises, comme une page ecrite.
                    const auto lines = d->textLines(t.index);
                    const double k = t.frame.w / dc::kTextPageW;
                    for (std::size_t i = 0; i < lines.size() && i < 50; ++i) {
                        if (trimmed(lines[i]).empty()) continue;
                        const double ly = t.frame.y + (dc::kTextMargin + dc::kTextLine * static_cast<double>(i)) * k;
                        const double lw = std::min(t.frame.w - 2 * dc::kTextMargin * k, lines[i].size() * dc::kTextFont * 0.5 * k);
                        box(c, {t.frame.x + dc::kTextMargin * k, ly, lw, std::max(0.8, dc::kTextFont * 0.5 * k)}, c.fixed(0xB8BEC8));
                    }
                } else {
                    const auto pic = d->thumbnail(t.index);
                    const auto img = pic.image ? HmiImageCache::instance().docPage(c.r, pic.image) : HmiImageCache::Image{};
                    if (img.valid()) drawPicture(c, img, t.frame, st.quarter, gfx::Color{255, 255, 255, static_cast<std::uint8_t>(255 * c.alpha)});
                }
                frameOf(c, t.frame, current ? accent : withAlpha(c.fixed(0x000000), 0.35f), current ? 2.5 : 1);
                text(c, std::to_string(t.index + 1), t.cell.x, t.frame.y + t.frame.h + 2, t.cell.w, 20, current ? accent : muted, 12, "centre");
            }
            c.r.popClip();
        }
    }

    // ---- la barre d'outils et celle de la recherche
    const int count = static_cast<int>(sizes.size());
    if (l.toolbar.w > 0) {
        box(c, l.toolbar, bar);
        seg(c, 0, l.toolbar.h, w, l.toolbar.h, withAlpha(c.fixed(0x000000), 0.45f), 1);
    }
    if (l.searchBar.w > 0) {
        box(c, l.searchBar, mix(bar, bg, 0.2f));
        seg(c, 0, l.searchBar.y + l.searchBar.h, w, l.searchBar.y + l.searchBar.h, withAlpha(c.fixed(0x000000), 0.45f), 1);
    }
    for (const auto& b : l.buttons) {
        bool enabled = b.enabled && (ready || b.part == "doc:searchclose" || b.part == "doc:search");
        if (b.part == "doc:rotate" && type == dc::Type::Text) enabled = false;
        if (b.part == "doc:first" || b.part == "doc:prev") enabled = enabled && st.page > 1;
        if (b.part == "doc:next" || b.part == "doc:last") enabled = enabled && st.page < count;
        if (b.active) box(c, b.box, withAlpha(accent, 0.55f), 5);
        icon(c, b.part, b.box, enabled ? ink : withAlpha(ink, 0.3f));
    }
    if (l.pageField.w > 0) {
        box(c, l.pageField, field, 4);
        frameOf(c, l.pageField, st.editingPage ? accent : withAlpha(ink, 0.25f), st.editingPage ? 1.6 : 1, 4);
        const std::string shown = st.editingPage ? st.pageText + "|" : dv::pageLabel(st.page, ready ? count : 0);
        text(c, fitted(c, shown, l.pageField.w - 8, 14), l.pageField.x, l.pageField.y, l.pageField.w, l.pageField.h,
             navigation ? ink : withAlpha(ink, 0.55f), 14, "centre");
    }
    if (l.zoomField.w > 0) {
        box(c, l.zoomField, field, 4);
        frameOf(c, l.zoomField, st.zoomMenu ? accent : withAlpha(ink, 0.25f), st.zoomMenu ? 1.6 : 1, 4);
        const std::string z = ready ? dv::zoomLabel(content.zoom) : std::string("- %");
        text(c, z, l.zoomField.x + 2, l.zoomField.y, l.zoomField.w - 16, l.zoomField.h, ink, 14, "centre");
        const double ax = l.zoomField.x + l.zoomField.w - 11, ay = l.zoomField.y + l.zoomField.h / 2;
        tri(c, {{static_cast<float>(ax - 4), static_cast<float>(ay - 2)}, {static_cast<float>(ax + 4), static_cast<float>(ay - 2)},
                {static_cast<float>(ax), static_cast<float>(ay + 3)}}, withAlpha(ink, 0.8f));
    }
    if (l.searchField.w > 0) {
        box(c, l.searchField, field, 4);
        frameOf(c, l.searchField, st.editingSearch ? accent : withAlpha(ink, 0.25f), st.editingSearch ? 1.6 : 1, 4);
        if (st.query.empty() && !st.editingSearch)
            text(c, "Rechercher dans le document", l.searchField.x + 8, l.searchField.y, l.searchField.w - 16, l.searchField.h, withAlpha(ink, 0.45f), 14,
                 "gauche");
        else
            text(c, fitted(c, st.query + (st.editingSearch ? "|" : ""), l.searchField.w - 16, 14), l.searchField.x + 8, l.searchField.y,
                 l.searchField.w - 16, l.searchField.h, ink, 14, "gauche");
        const std::string tip = search.query == trimmed(st.query) ? tipOf(search, st.hit) : std::string{};
        text(c, tip, l.hitLabel.x, l.hitLabel.y, l.hitLabel.w, l.hitLabel.h, tip == "aucun" ? c.fixed(0xF2994A) : muted, 13, "centre");
    }
    // ---- le menu des zooms, par-dessus
    if (!l.zoomMenu.empty()) {
        hmi::Box all = l.zoomMenu.front();
        all.h = l.zoomMenu.back().y + l.zoomMenu.back().h - all.y;
        box(c, {all.x + 2, all.y + 3, all.w, all.h}, withAlpha(c.fixed(0x000000), 0.35f), 6);
        box(c, all, field, 6);
        frameOf(c, all, withAlpha(ink, 0.3f), 1, 6);
        for (std::size_t i = 0; i < l.zoomMenu.size(); ++i) {
            const auto& e = l.zoomMenu[i];
            dv::Fit f = dv::Fit::None;
            double z = 1;
            (void)dv::parseZoom(dv::kZoomChoices[i], f, z);
            const bool chosen = (f != dv::Fit::None && f == st.fit) || (f == dv::Fit::None && st.fit == dv::Fit::None && std::fabs(z - st.zoom) < 1e-3);
            if (chosen) box(c, {e.x + 3, e.y + 2, e.w - 6, e.h - 4}, withAlpha(accent, 0.5f), 4);
            text(c, std::string(dv::kZoomChoices[i]), e.x + 12, e.y, e.w - 24, e.h, ink, 14, "gauche");
        }
    }
    // ---- le cadre
    if (c.stroke() > 0) frameOf(c, {0, 0, w, h}, c.color("stroke", gfx::Color::rgb(0x3A4556)), c.src.number(c.o, "strokeWidth", 1));
    return true;
}

// =============================================================================
//  1.14.1 (lot N5) : L'EXPLORATEUR DE FICHIERS A L'ECRAN - la barre (remonter, le
//  fil d'Ariane, Nom contient, Rafraichir, Extraire), les titres des colonnes (la
//  fleche du tri), la ligne "..", les lignes (l'icone du genre, le nom, la taille,
//  la date ; la ligne choisie surlignee), la ligne d'etat (le compte, le filtre, la
//  reponse d'un geste ou ce qui ne se lit pas). Les rectangles sont ceux de
//  hmi::explorer::layoutOf : ce qui se dessine est ce qui se clique. Dans
//  l'editeur : quelques lignes d'exemple.
// =============================================================================
namespace {

namespace ex = hmi::explorer;

// L'icone d'une ligne : un dossier (jaune), ou une page avec l'etiquette de son genre (ZIP, PDF, CSV...).
void entryIcon(const Ctx& c, ex::Genre g, double x, double y, double s) {
    if (g == ex::Genre::Folder) {
        const gfx::Color f = c.fixed(0xE8B84A);
        box(c, {x, y + s * 0.12, s * 0.46, s * 0.22}, mix(f, gfx::Color{0, 0, 0, f.a}, 0.15f), 1.5);
        box(c, {x, y + s * 0.26, s, s * 0.62}, f, 2);
        return;
    }
    std::uint32_t band = 0x8A94A6;
    switch (g) {
        case ex::Genre::Zip:     band = 0xA77BD6; break;
        case ex::Genre::Image:   band = 0x3FA7D6; break;
        case ex::Genre::Text:    band = 0x7D8796; break;
        case ex::Genre::Pdf:     band = 0xE5484D; break;
        case ex::Genre::Sound:   band = 0x2E9E5B; break;
        case ex::Genre::Video:   band = 0xE08A1E; break;
        case ex::Genre::Csv:     band = 0x37A35F; break;
        case ex::Genre::Project: band = 0x2F6FD6; break;
        default: break;
    }
    const double pw = s * 0.8, px = x + (s - pw) / 2;
    box(c, {px, y, pw, s}, c.fixed(0xEEF1F5), 1.5);
    box(c, {px, y + s * 0.52, pw, s * 0.48}, c.fixed(band), 1.5);
    const std::string_view tag = ex::genreTag(g);
    if (!tag.empty()) text(c, std::string(tag), px, y + s * 0.52, pw, s * 0.48, c.fixedOn(0xFFFFFF), s * 0.3, "centre");
}

// Remonter : une fleche vers le haut ; Rafraichir : un cercle ouvert et sa pointe.
void upGlyph(const Ctx& c, const hmi::Box& b, gfx::Color col) {
    const double cx = b.cx(), cy = b.cy(), s = std::min(b.w, b.h) * 0.24;
    tri(c, {{static_cast<float>(cx), static_cast<float>(cy - s * 1.2)}, {static_cast<float>(cx + s), static_cast<float>(cy - s * 0.1)},
            {static_cast<float>(cx - s), static_cast<float>(cy - s * 0.1)}},
        col);
    seg(c, cx, cy - s * 0.2, cx, cy + s * 1.2, col, std::max(1.5, s * 0.35));
}
void refreshGlyph(const Ctx& c, const hmi::Box& b, gfx::Color col) {
    const double cx = b.cx(), cy = b.cy(), r = std::min(b.w, b.h) * 0.26;
    std::vector<gfx::Point> arc;
    for (int k = 0; k <= 20; ++k) {
        const double a = 0.6 + 4.9 * k / 20.0;
        arc.push_back({static_cast<float>(cx + r * std::cos(a)), static_cast<float>(cy + r * std::sin(a))});
    }
    shapes::strokePolyline(c.r, c.map(arc), false, col, std::max(1.5f, static_cast<float>(r * 0.28 * c.vp.zoom)));
    const double ax = cx + r * std::cos(5.5), ay = cy + r * std::sin(5.5);
    tri(c, {{static_cast<float>(ax + r * 0.5), static_cast<float>(ay - r * 0.1)}, {static_cast<float>(ax - r * 0.2), static_cast<float>(ay - r * 0.55)},
            {static_cast<float>(ax - r * 0.15), static_cast<float>(ay + r * 0.4)}},
        col);
}

} // namespace

bool drawFileExplorer(const Ctx& c) {
    if (c.o.kind != hmi::Kind::FileExplorer) return false;
    const double w = c.w(), h = c.h();
    if (w < 4 || h < 4) return true;
    const double fs = std::clamp(c.src.number(c.o, "fontSize", 13), 7.0, 40.0);
    const double rad = c.src.number(c.o, "radius", 6);
    const gfx::Color bg = c.color("fill", gfx::Color::rgb(0x1F252E));
    const gfx::Color tc = c.color("textColor", gfx::Color::rgb(0xDDE3EA));
    const gfx::Color accent = c.color("accent", gfx::Color::rgb(0x2F6FD6));
    const gfx::Color stroke = c.color("stroke", gfx::Color::rgb(0x3A4556));
    const gfx::Color muted = withAlpha(tc, 0.62f);
    box(c, {0, 0, w, h}, bg, rad);
    frameOf(c, {0, 0, w, h}, stroke, 1, rad);

    // ---- l'etat : celui du moteur (en marche) ; dans l'editeur, des lignes d'exemple
    const auto* live = !c.opt.editor && c.opt.runtime ? c.opt.runtime->fileExplorer(c.o.id) : nullptr;
    std::vector<ex::Entry> rows;
    std::vector<std::string> crumbs;
    bool search = c.src.flag(c.o, "showSearch", true), refresh = c.src.flag(c.o, "allowRefresh", true);
    bool extract = c.src.flag(c.o, "allowExtract", false), showSize = c.src.flag(c.o, "showSize", true), showDate = c.src.flag(c.o, "showDate", true);
    bool parent = false, descending = false, error = false;
    ex::SortKey sortBy = ex::sortKeyFrom(c.src.text(c.o, "sortBy", "nom"));
    int selected = -1;
    std::size_t first = 0;
    std::string status, searchText;
    bool typing = false;
    if (live) {
        rows = live->shown;
        crumbs = c.opt.runtime->explorerCrumbs(c.o.id);
        search = live->showSearch;
        refresh = live->allowRefresh;
        extract = live->allowExtract;
        showSize = live->showSize;
        showDate = live->showDate;
        parent = !live->loc.at.empty();
        sortBy = live->sortBy;
        descending = live->descending;
        selected = live->selected;
        first = live->first;
        searchText = live->search;
        const auto* f = c.opt.runtime->formState(c.o.id);
        typing = f && c.opt.runtime->focusedObject() == c.o.id && f->focus == "recherche";
        if (typing)
            if (const auto t = f->text.find("recherche"); t != f->text.end()) searchText = t->second;
        if (!live->message.empty()) {
            status = live->message;
            error = live->error;
        } else if (!live->listing.error.empty()) {
            status = live->listing.error;
            error = true;
        } else {
            std::size_t folders = 0;
            for (const auto& e : rows) folders += e.folder;
            status = std::to_string(rows.size()) + (rows.size() > 1 ? " \xC3\xA9l\xC3\xA9ments" : " \xC3\xA9l\xC3\xA9ment");
            if (folders > 0) status += " (dont " + std::to_string(folders) + (folders > 1 ? " dossiers)" : " dossier)");
            if (live->listing.inZip) status += " \xC2\xB7 dans un zip";
            if (!live->filter.empty()) status += " \xC2\xB7 filtre " + live->filter;
            if (!live->search.empty()) status += " \xC2\xB7 nom contient \xC2\xAB " + live->search + " \xC2\xBB";
            if (live->listing.truncated) status += " \xC2\xB7 les " + std::to_string(live->listing.entries.size()) + " premiers (il y en a plus)";
        }
    } else {
        const std::string src = c.src.text(c.o, "folder");
        crumbs = {src.empty() ? std::string("Ressources") : src};
        const auto sample = [&](const char* name, bool folder, std::uint64_t size, std::int64_t date) {
            ex::Entry e;
            e.name = name;
            e.folder = folder;
            e.genre = ex::genreOf(name, folder);
            e.size = size;
            e.modified = date;
            rows.push_back(e);
        };
        sample("Photos", true, 0, 1793350000);
        sample("Rapports", true, 0, 1793260000);
        sample("archive_2026.zip", false, 1258291, 1793340000);
        sample("mesures_ligne1.csv", false, 48210, 1793330000);
        sample("notice_vanne.pdf", false, 734003, 1793120000);
        sample("synoptique.png", false, 91320, 1793100000);
        sample("lisezmoi.txt", false, 2048, 1793050000);
        ex::sort(rows, sortBy, false);
        status = std::to_string(rows.size()) + " \xC3\xA9l\xC3\xA9ments (exemple : la liste se lit en marche)";
    }
    const auto l = ex::layoutOf(w, h, fs, crumbs, search, refresh, extract, parent, showSize, showDate, rows.size(), first);

    // ---- la barre : remonter, le fil d'Ariane, la recherche, Rafraichir, Extraire
    box(c, l.bar, mix(bg, tc, 0.06f), rad);
    seg(c, 0, l.bar.bottom(), w, l.bar.bottom(), withAlpha(stroke, 0.9f), 1);
    box(c, l.up, mix(bg, tc, parent ? 0.16f : 0.07f), 4);
    upGlyph(c, l.up, parent ? tc : withAlpha(tc, 0.35f));
    if (l.firstCrumb > 0 && l.firstCrumb < l.crumbs.size())
        text(c, "\xE2\x80\xA6", l.crumbs[l.firstCrumb].x - fs * 1.6, l.up.y, fs * 1.4, l.up.h, muted, fs, "centre");
    for (std::size_t i = l.firstCrumb; i < l.crumbs.size(); ++i) {
        const auto& b = l.crumbs[i];
        if (b.w <= 0) continue;
        const bool last = i + 1 == l.crumbs.size();
        text(c, fitted(c, crumbs[i], b.w, fs), b.x, b.y, b.w, b.h, last ? tc : accent, fs, "gauche");
        if (!last) text(c, "\xE2\x80\xBA", b.right(), b.y, fs * 1.1, b.h, muted, fs, "centre");
    }
    if (l.search.w > 0) {
        box(c, l.search, c.fixed(0x141820), 4);
        frameOf(c, l.search, typing ? accent : c.fixed(0x4A5568), typing ? 2 : 1, 4);
        const bool empty = searchText.empty();
        text(c, fitted(c, empty ? std::string("Nom contient\xE2\x80\xA6") : searchText, l.search.w - 12, fs * 0.95), l.search.x + 6, l.search.y,
             l.search.w - 12, l.search.h, empty ? withAlpha(tc, 0.45f) : tc, fs * 0.95, "gauche");
    }
    if (l.refresh.w > 0) {
        box(c, l.refresh, mix(bg, tc, 0.12f), 4);
        refreshGlyph(c, l.refresh, tc);
    }
    if (l.extract.w > 0) drawButtonBox(c, l.extract, "Extraire", accent, c.fixedOn(0xFFFFFF), fs * 0.95);

    // ---- les titres des colonnes, la fleche du tri
    box(c, l.header, mix(bg, tc, 0.03f));
    const auto title = [&](const hmi::Box& b, const char* label, ex::SortKey key, const char* align) {
        if (b.w <= 0) return;
        const bool on = sortBy == key;
        text(c, label, b.x + 26, b.y, b.w - 30, b.h, on ? tc : muted, fs * 0.9, align);
        if (on) {
            const double ax = std::string_view(align) == "droite" ? b.x + 10 : b.x + 14, ay = b.cy(), s = fs * 0.28;
            if (descending)
                tri(c, {{static_cast<float>(ax - s), static_cast<float>(ay - s * 0.5)}, {static_cast<float>(ax + s), static_cast<float>(ay - s * 0.5)},
                        {static_cast<float>(ax), static_cast<float>(ay + s * 0.6)}},
                    accent);
            else
                tri(c, {{static_cast<float>(ax - s), static_cast<float>(ay + s * 0.5)}, {static_cast<float>(ax + s), static_cast<float>(ay + s * 0.5)},
                        {static_cast<float>(ax), static_cast<float>(ay - s * 0.6)}},
                    accent);
        }
    };
    title(l.colName, "Nom", ex::SortKey::Name, "gauche");
    title(l.colSize, "Taille", ex::SortKey::Size, "droite");
    title(l.colDate, "Date", ex::SortKey::Date, "gauche");

    // ---- la ligne "..", les lignes, les fleches
    const double iconS = std::min(l.rowHeight - 6, fs * 1.35);
    if (l.parent.w > 0) {
        entryIcon(c, ex::Genre::Folder, l.colName.x + 4, l.parent.cy() - iconS / 2, iconS);
        text(c, "..", l.colName.x + iconS + 12, l.parent.y, 60, l.parent.h, tc, fs, "gauche");
        text(c, "dossier parent", l.colName.x + iconS + 40, l.parent.y, l.colName.w, l.parent.h, muted, fs * 0.85, "gauche");
    }
    for (std::size_t i = 0; i < l.rows.size(); ++i) {
        const std::size_t k = l.first + i;
        if (k >= rows.size()) break;
        const auto& r = l.rows[i];
        const auto& e = rows[k];
        if (static_cast<int>(k) == selected) box(c, r, withAlpha(accent, 0.45f));
        else if (k % 2 == 1) box(c, r, withAlpha(tc, 0.04f));
        entryIcon(c, e.genre, l.colName.x + 4, r.cy() - iconS / 2, iconS);
        const double nx = l.colName.x + iconS + 12;
        text(c, fitted(c, e.name, l.colName.right() - nx, fs), nx, r.y, l.colName.right() - nx, r.h, e.suspicious ? c.fixedText(0xF5A623) : tc, fs,
             "gauche");
        if (l.colSize.w > 0 && !e.folder) text(c, ex::sizeText(e.size), l.colSize.x, r.y, l.colSize.w - 6, r.h, muted, fs * 0.9, "droite");
        if (l.colDate.w > 0) text(c, ex::dateText(e.modified), l.colDate.x + 6, r.y, l.colDate.w - 6, r.h, muted, fs * 0.9, "gauche");
    }
    if (l.upArrow.w > 0)
        text(c, "\xE2\x96\xB2  " + std::to_string(l.first) + " au-dessus", l.upArrow.x, l.upArrow.y, l.upArrow.w, l.upArrow.h, muted, fs * 0.85, "centre");
    if (l.downArrow.w > 0) {
        const std::size_t below = rows.size() > l.first + l.rows.size() ? rows.size() - l.first - l.rows.size() : 0;
        text(c, "\xE2\x96\xBC  " + std::to_string(below) + " en dessous", l.downArrow.x, l.downArrow.y, l.downArrow.w, l.downArrow.h, muted, fs * 0.85,
             "centre");
    }
    if (rows.empty() && !error && live)
        text(c, live->search.empty() && live->filter.empty() ? "Dossier vide" : "Rien ne correspond", l.list.x, l.list.y + (l.parent.w > 0 ? l.rowHeight : 0),
             l.list.w, l.rowHeight * 2, muted, fs, "centre");

    // ---- la ligne d'etat
    seg(c, 0, l.status.y - 1, w, l.status.y - 1, withAlpha(stroke, 0.7f), 1);
    text(c, fitted(c, status, l.status.w, fs * 0.85), l.status.x, l.status.y, l.status.w, l.status.h,
         error ? c.fixedText(0xFF6B6B) : (live && !live->message.empty() ? c.fixedText(0x4CD08A) : muted), fs * 0.85, "gauche");
    return true;
}

} // namespace app::paint
