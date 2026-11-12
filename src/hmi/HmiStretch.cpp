// hmi/HmiStretch.cpp - 1.50.0 : Etirer un objet de la vue (voir HmiStretch.hpp).
#include "HmiStretch.hpp"
#include "HmiEdit.hpp"
#include "HmiVertices.hpp"   // une ligne dont les points viennent d'une source se prend dans son cadre

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace hmi::stretch {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 0.01;

bool contains(const std::vector<Id>& v, Id id) { return std::find(v.begin(), v.end(), id) != v.end(); }

bool sameBox(const Box& a, const Box& b) {
    return std::fabs(a.x - b.x) < kEps && std::fabs(a.y - b.y) < kEps && std::fabs(a.w - b.w) < kEps && std::fabs(a.h - b.h) < kEps;
}

// Un nombre a l'ecran : entier quand il l'est (a 0,1 pres), sinon une decimale.
std::string num(double v) { return formatNumber(std::round(v * 10.0) / 10.0); }

bool isLine(const Object& o) { return o.kind == Kind::Line && !vertices::hasPointsFrom(o); }

// UNE LIGNE S'ETIRE DANS SON SENS : elle s'allonge a l'echelle (son angle garde), autour du bout qui ne bouge
// pas. `want` : la boite voulue (un axe ou les deux) ; les deux : l'axe de la ligne. Rien : la ligne est en
// travers de l'axe (une ligne verticale ne s'etire pas en largeur).
std::optional<Box> alongLine(const Object& o, const Box& was, const Box& want) {
    const bool lMoved = std::fabs(want.x - was.x) > kEps, rMoved = std::fabs(want.right() - was.right()) > kEps;
    const bool tMoved = std::fabs(want.y - was.y) > kEps, bMoved = std::fabs(want.bottom() - was.bottom()) > kEps;
    bool horiz = lMoved || rMoved, vert = tMoved || bMoved;
    if (!horiz && !vert) return want;
    std::vector<edit::Pt> pts;
    for (const auto& p : edit::points(o)) pts.push_back(edit::toView(o, p));
    if (pts.size() < 2) return want;
    const double dx = pts.back().x - pts.front().x, dy = pts.back().y - pts.front().y;
    if (horiz && vert) {
        if (std::fabs(dx) >= std::fabs(dy)) vert = false;
        else horiz = false;
    }
    const auto extreme = [&](bool alongX, bool max) {
        edit::Pt best = pts.front();
        for (const auto& p : pts) {
            const double a = alongX ? p.x : p.y, b = alongX ? best.x : best.y;
            if (max ? a > b : a < b) best = p;
        }
        return best;
    };
    Box out;
    if (horiz) {
        if (was.w < 0.5) return std::nullopt;
        const double nl = lMoved ? want.x : was.x, nr = rMoved ? want.right() : was.right();
        const double k = (nr - nl) / was.w;
        if (k <= 0) return std::nullopt;
        // Le bout fixe : celui du cote qui ne bouge pas ; les deux bougent : le bout de gauche, pose sur le nouveau bord.
        const edit::Pt p = (lMoved && !rMoved) ? extreme(true, true) : extreme(true, false);
        out = {nl, p.y + k * (was.y - p.y), was.w * k, was.h * k};
    } else {
        if (was.h < 0.5) return std::nullopt;
        const double nt = tMoved ? want.y : was.y, nb = bMoved ? want.bottom() : was.bottom();
        const double k = (nb - nt) / was.h;
        if (k <= 0) return std::nullopt;
        const edit::Pt p = (tMoved && !bMoved) ? extreme(false, true) : extreme(false, false);
        out = {p.x + k * (was.x - p.x), nt, was.w * k, was.h * k};
    }
    return out;
}

std::string sideWord(unsigned edges) {
    switch (edges) {
        case kLeft:   return "\xC3\xA0 gauche";
        case kRight:  return "\xC3\xA0 droite";
        case kTop:    return "au-dessus";
        case kBottom: return "au-dessous";
        default:      return "autour";
    }
}

std::string edgesList(unsigned edges) {
    std::vector<std::string> w;
    if (edges & kLeft) w.emplace_back("gauche");
    if (edges & kRight) w.emplace_back("droit");
    if (edges & kTop) w.emplace_back("haut");
    if (edges & kBottom) w.emplace_back("bas");
    std::string s;
    for (std::size_t i = 0; i < w.size(); ++i) s += (i == 0 ? "" : i + 1 == w.size() ? " et " : ", ") + w[i];
    return s;
}

} // namespace

double viewMargin(const View& v) {
    if (v.columns.visible) return std::max(0.0, v.columns.margin);
    return static_cast<double>(std::max(1, v.grid.step));
}

double neighbourGap(const View& v) { return static_cast<double>(std::max(1, v.grid.step)); }

std::string edgeWord(unsigned edge) {
    switch (edge) {
        case kLeft:   return "gauche";
        case kRight:  return "droite";
        case kTop:    return "en haut";
        case kBottom: return "en bas";
        default:      return {};
    }
}

Id neighbour(const View& v, Id unit, unsigned edge, const std::vector<Id>& exclude) {
    const auto* u = v.object(unit);
    if (!u) return kNoId;
    const Box b = edit::rotatedBounds(*u);
    constexpr double e = 0.5;
    Id best = kNoId;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& o : v.objects) {
        if (o.id == unit || o.parent != u->parent || contains(exclude, o.id) || v.effectivelyHidden(o)) continue;
        const Box c = edit::rotatedBounds(o);
        const bool overlapY = c.y < b.bottom() - e && c.bottom() > b.y + e;
        const bool overlapX = c.x < b.right() - e && c.right() > b.x + e;
        double dist = 0;
        switch (edge) {
            case kRight:  if (c.x < b.right() - e || !overlapY) continue; dist = c.x - b.right(); break;
            case kLeft:   if (c.right() > b.x + e || !overlapY) continue; dist = b.x - c.right(); break;
            case kBottom: if (c.y < b.bottom() - e || !overlapX) continue; dist = c.y - b.bottom(); break;
            case kTop:    if (c.bottom() > b.y + e || !overlapX) continue; dist = b.y - c.bottom(); break;
            default: return kNoId;
        }
        if (dist < bestDist) {
            bestDist = dist;
            best = o.id;
        }
    }
    return best;
}

Plan plan(const View& v, const std::vector<Id>& selection, const Request& req) {
    Plan p;
    p.to = req.to;
    const auto list = edit::units(v, selection);
    if (list.empty()) {
        p.why = "rien de choisi";
        return p;
    }
    std::vector<edit::ColumnSpan> spans;
    switch (req.to) {
        case To::Selection:
            if (list.size() < 2) { p.why = "deux objets ou plus"; return p; }
            break;
        case To::Guide:
            if (v.guides.empty()) { p.why = "aucun rep\xC3\xA8re"; return p; }
            if (req.index >= static_cast<int>(v.guides.size())) { p.why = "ce rep\xC3\xA8re n'existe plus"; return p; }
            break;
        case To::Column:
            if (!v.columns.visible) { p.why = "pas de grille de colonnes"; return p; }
            spans = edit::columnSpans(v);
            if (spans.empty()) { p.why = "la grille de colonnes ne tient pas dans la vue"; return p; }
            if (req.index < 0 || req.index >= static_cast<int>(spans.size())) { p.why = "cette colonne n'existe plus"; return p; }
            break;
        default: break;
    }
    const bool byEdges = req.to == To::View || req.to == To::Neighbour || req.to == To::Selection || (req.to == To::Guide && req.index < 0);
    if (byEdges && (req.edges & kAll) == 0) {
        p.why = "aucun bord choisi";
        return p;
    }
    double m = 0;
    if (req.keepMargins && (req.to == To::View || req.to == To::Neighbour))
        m = req.margin >= 0 ? req.margin : (req.to == To::View ? viewMargin(v) : neighbourGap(v));
    p.margin = m;

    const Box sel = edit::selectionBounds(v, list);
    const unsigned e = req.edges;
    int freeCount = 0, across = 0, tooSmall = 0, inside = 0;
    bool anyNeighbour = false;
    for (const Id id : list) {
        const auto* o = v.object(id);
        if (!o) continue;
        if (v.effectivelyLocked(*o)) {
            p.skipped.push_back(o->name + " (verrouill\xC3\xA9)");
            continue;
        }
        ++freeCount;
        const Box was = edit::rotatedBounds(*o);
        double l = was.x, r = was.right(), t = was.y, bo = was.bottom();
        switch (req.to) {
            case To::View:
                if (e & kLeft) l = m;
                if (e & kRight) r = static_cast<double>(v.width) - m;
                if (e & kTop) t = m;
                if (e & kBottom) bo = static_cast<double>(v.height) - m;
                break;
            case To::Selection:
                if (e & kLeft) l = sel.x;
                if (e & kRight) r = sel.right();
                if (e & kTop) t = sel.y;
                if (e & kBottom) bo = sel.bottom();
                break;
            case To::Neighbour:
                for (const unsigned one : {kLeft, kRight, kTop, kBottom}) {
                    if (!(e & one)) continue;
                    const Id n = neighbour(v, id, one, list);
                    const auto* no = n != kNoId ? v.object(n) : nullptr;
                    if (!no) continue;
                    anyNeighbour = true;
                    if (list.size() == 1 && p.neighbour.empty()) p.neighbour = no->name;
                    const Box c = edit::rotatedBounds(*no);
                    // Il s'arrete avant lui ; un voisin deja contre lui (ou dessus) : ce bord ne bouge pas.
                    if (one == kLeft) l = std::min(l, c.right() + m);
                    if (one == kRight) r = std::max(r, c.x - m);
                    if (one == kTop) t = std::min(t, c.bottom() + m);
                    if (one == kBottom) bo = std::max(bo, c.y - m);
                }
                break;
            case To::Guide:
                if (req.index >= 0) {
                    const Guide& g = v.guides[static_cast<std::size_t>(req.index)];
                    const double lo = g.vertical ? l : t, hi = g.vertical ? r : bo;
                    if (g.position >= hi) (g.vertical ? r : bo) = g.position;
                    else if (g.position <= lo) (g.vertical ? l : t) = g.position;
                    else ++inside;
                } else {
                    // La fenetre : le repere le plus proche dans le sens de chaque bord choisi.
                    std::optional<double> gl, gr, gt, gb;
                    for (const auto& g : v.guides) {
                        const double pos = g.position;
                        if (g.vertical) {
                            if ((e & kRight) && pos > was.right() + 0.5 && (!gr || pos < *gr)) gr = pos;
                            if ((e & kLeft) && pos < was.x - 0.5 && (!gl || pos > *gl)) gl = pos;
                        } else {
                            if ((e & kBottom) && pos > was.bottom() + 0.5 && (!gb || pos < *gb)) gb = pos;
                            if ((e & kTop) && pos < was.y - 0.5 && (!gt || pos > *gt)) gt = pos;
                        }
                    }
                    if (gl) l = *gl;
                    if (gr) r = *gr;
                    if (gt) t = *gt;
                    if (gb) bo = *gb;
                }
                break;
            case To::Column: {
                const auto& s = spans[static_cast<std::size_t>(req.index)];
                l = std::min(l, s.x);
                r = std::max(r, s.x + s.w);
                break;
            }
        }
        // Trop petit : un axe qui change et passe sous 1 px (une ligne horizontale a 0 de haut reste permise).
        const bool hMoved = std::fabs(l - was.x) > kEps || std::fabs(r - was.right()) > kEps;
        const bool vMoved = std::fabs(t - was.y) > kEps || std::fabs(bo - was.bottom()) > kEps;
        if ((hMoved && r - l < 1.0) || (vMoved && bo - t < 1.0)) {
            ++tooSmall;
            p.skipped.push_back(o->name + " (trop petit)");
            continue;
        }
        Box want{l, t, r - l, bo - t};
        if (isLine(*o)) {
            const auto along = alongLine(*o, was, want);
            if (!along) {
                ++across;
                p.skipped.push_back(o->name + " (une ligne s'\xC3\xA9tire dans son sens)");
                continue;
            }
            want = *along;
        }
        if (sameBox(was, want)) continue;
        p.changes.push_back({id, o->name, was, want});
    }
    if (p.changes.empty()) {
        if (freeCount == 0) p.why = list.size() == 1 ? "verrouill\xC3\xA9" : "tous verrouill\xC3\xA9s";
        else if (across == freeCount) p.why = "une ligne s'\xC3\xA9tire dans son sens";
        else if (tooSmall == freeCount) p.why = "trop petit";
        else if (req.to == To::Neighbour && !anyNeighbour) p.why = "aucun objet " + sideWord(e);
        else if (req.to == To::Guide && inside > 0) p.why = "le rep\xC3\xA8re traverse l'objet";
        else if (req.to == To::Guide && req.index < 0) p.why = "aucun rep\xC3\xA8re de ce c\xC3\xB4t\xC3\xA9";
        else if (req.to == To::View) p.why = "d\xC3\xA9j\xC3\xA0 au bord";
        else p.why = "d\xC3\xA9j\xC3\xA0 l\xC3\xA0";
    }
    return p;
}

void apply(View& v, const Plan& p) {
    for (const auto& c : p.changes) {
        const auto* o = v.object(c.id);
        if (!o || v.effectivelyLocked(*o)) continue;
        const Box was = edit::rotatedBounds(*o);
        const Box b = o->box();
        const Box& want = c.after;
        const double a = o->rotation() * kPi / 180.0;
        const double cs = std::fabs(std::cos(a)), sn = std::fabs(std::sin(a));
        const bool hCh = std::fabs(want.w - was.w) > 1e-6, vCh = std::fabs(want.h - was.h) > 1e-6;
        double w = b.w, h = b.h;
        bool exact = true;
        if (isLine(*o)) {
            // Dans son sens : les deux cotes a la meme echelle.
            const double k = was.w > 0.5 ? want.w / was.w : (was.h > 0.5 ? want.h / was.h : 1.0);
            w = b.w * k;
            h = b.h * k;
        } else if (sn < 1e-6) {
            w = want.w;
            h = want.h;
        } else if (cs < 1e-6) {   // un quart de tour : la largeur a l'ecran est sa hauteur
            w = want.h;
            h = want.w;
        } else {
            // Tourne d'un angle quelconque : sa boite a l'ecran fait w.cos + h.sin de large. Un seul axe : le cote le plus
            // dans son sens prend l'ecart (l'autre axe grandit un peu, centre) ; les deux : le systeme, s'il se resout.
            exact = false;
            if (hCh && vCh) {
                const double det = cs * cs - sn * sn;
                double w2 = -1, h2 = -1;
                if (std::fabs(det) > 0.05) {
                    w2 = (want.w * cs - want.h * sn) / det;
                    h2 = (want.h * cs - want.w * sn) / det;
                }
                if (w2 >= 1 && h2 >= 1) {
                    w = w2;
                    h = h2;
                    exact = true;
                } else {
                    const double kx = was.w > 1e-9 ? want.w / was.w : 1.0, ky = was.h > 1e-9 ? want.h / was.h : 1.0;
                    w = b.w * (cs >= sn ? kx : ky);
                    h = b.h * (cs >= sn ? ky : kx);
                }
            } else if (hCh) {
                if (cs >= sn) w = (want.w - b.h * sn) / cs;
                else h = (want.w - b.w * cs) / sn;
            } else if (vCh) {
                if (cs >= sn) h = (want.h - b.w * sn) / cs;
                else w = (want.h - b.h * cs) / sn;
            }
            w = std::max(w, 1.0);
            h = std::max(h, 1.0);
        }
        edit::setBox(v, c.id, {b.x, b.y, w, h});
        const auto* now = v.object(c.id);
        if (!now) continue;
        const Box n = edit::rotatedBounds(*now);
        const double dx = (exact || hCh) ? want.x - n.x : was.cx() - n.cx();
        const double dy = (exact || vCh) ? want.y - n.y : was.cy() - n.cy();
        if (std::fabs(dx) > 1e-9 || std::fabs(dy) > 1e-9) edit::move(v, {c.id}, dx, dy);
    }
    edit::refreshGroupBounds(v);
}

std::string targetText(const View& v, const Request& req, const Plan& p) {
    const unsigned e = req.edges & kAll;
    switch (req.to) {
        case To::View:
            switch (e) {
                case kLeft:   return "jusqu'au bord gauche de la vue";
                case kRight:  return "jusqu'au bord droit de la vue";
                case kTop:    return "jusqu'en haut de la vue";
                case kBottom: return "jusqu'en bas de la vue";
                case kWidth:  return "sur toute la largeur de la vue";
                case kHeight: return "sur toute la hauteur de la vue";
                case kAll:    return "sur toute la vue";
                default:      return "jusqu'aux bords de la vue (" + edgesList(e) + ")";
            }
        case To::Neighbour:
            return "jusqu'\xC3\xA0 l'objet voisin " + sideWord(e) + (p.neighbour.empty() ? std::string{} : " (" + p.neighbour + ")");
        case To::Selection:
            switch (e) {
                case kLeft:   return "jusqu'au bord gauche de la s\xC3\xA9lection";
                case kRight:  return "jusqu'au bord droit de la s\xC3\xA9lection";
                case kTop:    return "jusqu'en haut de la s\xC3\xA9lection";
                case kBottom: return "jusqu'en bas de la s\xC3\xA9lection";
                default:      return "jusqu'aux bords de la s\xC3\xA9lection (" + edgesList(e) + ")";
            }
        case To::Guide:
            if (req.index >= 0 && req.index < static_cast<int>(v.guides.size())) {
                const auto& g = v.guides[static_cast<std::size_t>(req.index)];
                return std::string("jusqu'au rep\xC3\xA8re ") + (g.vertical ? "x = " : "y = ") + num(g.position);
            }
            return "jusqu'aux rep\xC3\xA8res les plus proches (" + edgesList(e) + ")";
        case To::Column:
            return "jusqu'\xC3\xA0 la colonne " + std::to_string(req.index + 1) + " de la grille";
    }
    return {};
}

std::string deltaText(const Change& c) {
    const bool dw = std::fabs(c.after.w - c.before.w) > kEps, dh = std::fabs(c.after.h - c.before.h) > kEps;
    const auto signedPx = [](double d) {
        return (d >= 0 ? std::string("+ ") : std::string("\xE2\x88\x92 ")) + num(std::fabs(d)) + " px";
    };
    const std::string arrow = " \xE2\x86\x92 ";
    if (dw && !dh) return signedPx(c.after.w - c.before.w) + " \xC2\xB7 largeur " + num(c.before.w) + arrow + num(c.after.w);
    if (dh && !dw) return signedPx(c.after.h - c.before.h) + " \xC2\xB7 hauteur " + num(c.before.h) + arrow + num(c.after.h);
    if (dw && dh)
        return "largeur " + num(c.before.w) + arrow + num(c.after.w) + " \xC2\xB7 hauteur " + num(c.before.h) + arrow + num(c.after.h);
    return "x " + num(c.after.x) + " \xC2\xB7 y " + num(c.after.y);
}

std::string sizeText(const Change& c) {
    const std::string arrow = " \xE2\x86\x92 ";
    const auto part = [&](const char* name, double a, double b, bool size) {
        if (std::fabs(a - b) <= kEps) return std::string(name) + " " + num(a) + (size ? " (ne change pas)" : "");
        return std::string(name) + " " + num(a) + arrow + num(b);
    };
    std::string s = part("x", c.before.x, c.after.x, false) + " \xC2\xB7 " + part("largeur", c.before.w, c.after.w, true);
    if (std::fabs(c.before.y - c.after.y) > kEps) s += " \xC2\xB7 " + part("y", c.before.y, c.after.y, false);
    return s + " \xC2\xB7 " + part("hauteur", c.before.h, c.after.h, true);
}

std::string sentence(const View& v, const Request& req, const Plan& p) {
    if (!p.possible()) return "Rien \xC3\xA0 \xC3\xA9tirer : " + p.why + ".";
    std::string s = "\xC3\x89tirement de " + (p.changes.size() == 1 ? p.changes.front().name : std::to_string(p.changes.size()) + " objets")
                    + " " + targetText(v, req, p);
    if (p.changes.size() == 1) {
        const auto& c = p.changes.front();
        const bool dw = std::fabs(c.after.w - c.before.w) > kEps, dh = std::fabs(c.after.h - c.before.h) > kEps;
        const std::string arrow = " \xE2\x86\x92 ";
        if (dw && dh) s += " : largeur " + num(c.before.w) + arrow + num(c.after.w) + ", hauteur " + num(c.before.h) + arrow + num(c.after.h) + " px";
        else if (dw) s += " : largeur " + num(c.before.w) + arrow + num(c.after.w) + " px";
        else if (dh) s += " : hauteur " + num(c.before.h) + arrow + num(c.after.h) + " px";
    }
    if (req.to == To::View) s += p.margin > 0 ? " (marge " + num(p.margin) + " px gard\xC3\xA9" "e)" : std::string(" (sans marge)");
    else if (req.to == To::Neighbour) s += p.margin > 0 ? " (espacement " + num(p.margin) + " px gard\xC3\xA9)" : std::string(" (contre lui)");
    s += ". Ctrl+Z l'annule.";
    if (!p.skipped.empty()) {
        s += " Pas touch\xC3\xA9 : ";
        for (std::size_t i = 0; i < p.skipped.size(); ++i) s += (i ? ", " : "") + p.skipped[i];
        s += ".";
    }
    return s;
}

} // namespace hmi::stretch
