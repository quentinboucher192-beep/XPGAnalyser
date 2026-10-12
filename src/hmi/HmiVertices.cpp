#include "HmiVertices.hpp"

#include "HmiCharts.hpp"       // la courbe XY : ses courbes (la legende)
#include "HmiMarkers.hpp"
#include "HmiNavigation.hpp"   // les zones d'un plan
#include "HmiTypeForms.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace hmi::vertices {

namespace {

std::string trimmedOf(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::string lowered(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Un nombre ecrit, ses reperes transparents.
bool numberOf(std::string_view text, double& out) { return parseNumber(markers::strip(text, markers::Mode::Text), out); }

edit::Pt nearestOn(edit::Pt p, edit::Pt a, edit::Pt b) {
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double len2 = vx * vx + vy * vy;
    const double t = len2 > 0 ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / len2, 0.0, 1.0) : 0.0;
    return {a.x + t * vx, a.y + t * vy};
}

double distance(edit::Pt a, edit::Pt b) { return std::hypot(a.x - b.x, a.y - b.y); }

// LE CADRE SUIT LES POINTS : le plus petit cadre qui tient `pts` (repere de l'objet tel qu'il est), les
// points ramenes a son coin - rien ne bouge a l'ecran, rotation et miroirs compris (le premier point garde
// sa place dans la vue ; les ecarts entre points ne dependent que de la rotation et des miroirs).
void fit(Object& o, std::vector<Vertex> pts) {
    // Les morceaux illisibles restent tels quels, a leur rang ; le cadre se fait sur les points lus.
    const auto first = std::find_if(pts.begin(), pts.end(), [](const Vertex& p) { return p.ok; });
    if (first == pts.end()) {
        o.set("points", vertices::format(pts));
        return;
    }
    double x0 = first->x, y0 = first->y, x1 = x0, y1 = y0;
    for (const auto& p : pts) {
        if (!p.ok) continue;
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    const edit::Pt anchorLocal{first->x, first->y};
    const edit::Pt anchor = edit::toView(o, anchorLocal);
    for (auto& p : pts) {
        if (!p.ok) continue;
        // Un nombre qui ne bouge pas garde son texte (un repere) ; deplace, il s'ecrit.
        if (std::fabs(x0) > 1e-12) {
            p.x -= x0;
            p.xText.clear();
        }
        if (std::fabs(y0) > 1e-12) {
            p.y -= y0;
            p.yText.clear();
        }
    }
    Object placed = o;
    placed.setNumber("w", x1 - x0);
    placed.setNumber("h", y1 - y0);
    placed.setNumber("x", 0);
    placed.setNumber("y", 0);
    const edit::Pt q = edit::toView(placed, {anchorLocal.x - x0, anchorLocal.y - y0});
    o.setNumber("w", x1 - x0);
    o.setNumber("h", y1 - y0);
    o.setNumber("x", anchor.x - q.x);
    o.setNumber("y", anchor.y - q.y);
    o.set("points", vertices::format(pts));
}

Object* shapeOf(View& v, const Target& t) {
    if (t.what != Target::What::Shape) return nullptr;
    Object* o = v.object(t.object);
    return o && isShape(*o) ? o : nullptr;
}
const Object* shapeOf(const View& v, const Target& t) {
    if (t.what != Target::What::Shape) return nullptr;
    const Object* o = v.object(t.object);
    return o && isShape(*o) ? o : nullptr;
}
const Object* mapOf(const View& v, const Target& t) {
    if (t.what != Target::What::Zone) return nullptr;
    const Object* o = v.object(t.object);
    return o && o->kind == Kind::ZoneMap ? o : nullptr;
}

// Un point de la vue en % du plan (le repere de l'objet, sa taille), au dixieme, dans le plan.
bool percentOf(const Object& map, edit::Pt at, std::pair<double, double>& out) {
    const Box b = map.box();
    if (b.w <= 1e-9 || b.h <= 1e-9) return false;
    const edit::Pt l = edit::toLocal(map, at);
    out = {std::clamp(std::round(l.x / b.w * 1000.0) / 10.0, 0.0, 100.0), std::clamp(std::round(l.y / b.h * 1000.0) / 10.0, 0.0, 100.0)};
    return true;
}

const Object* chartOf(const View& v, const Target& t) {
    if (t.what != Target::What::Reference) return nullptr;
    const Object* o = v.object(t.object);
    return o && o->kind == Kind::XYChart ? o : nullptr;
}

// Les points de la reference, ranges par x croissant (comme le dessin les lit).
std::vector<Vertex> referencePoints(const Object& o) { return listOf(o).points; }

// Un nombre arrondi a un millieme de l'etendue de son axe (10 sur 0..10 000, 0,1 sur 0..100).
double roundOn(double value, double span) {
    if (!(span > 0)) return value;
    const double q = std::pow(10.0, std::floor(std::log10(span)) - 3);
    return std::round(value / q) * q;
}

// Un point de la vue dans les unites des axes d'une courbe XY (dans les axes).
bool dataOf(const Object& chart, edit::Pt at, double& x, double& y) {
    XYPlot pl;
    if (!xyPlot(chart, pl) || pl.area.w <= 0 || pl.area.h <= 0) return false;
    const edit::Pt l = edit::toLocal(chart, at);
    x = std::clamp(pl.x0 + (l.x - pl.area.x) / pl.area.w * (pl.x1 - pl.x0), pl.x0, pl.x1);
    y = std::clamp(pl.y0 + (1.0 - (l.y - pl.area.y) / pl.area.h) * (pl.y1 - pl.y0), pl.y0, pl.y1);
    x = roundOn(x, pl.x1 - pl.x0);
    y = roundOn(y, pl.y1 - pl.y0);
    return true;
}

// Les zones du plan et celle de la cible ; faux : elle n'existe pas.
bool zoneOf(const Object& map, const Target& t, std::vector<MapZone>& zones) {
    zones = parseMapZones(map.text("mapZones"));
    return t.zone >= 0 && static_cast<std::size_t>(t.zone) < zones.size();
}

} // namespace

// ============================================================== le texte ===
Parsed parse(std::string_view text) {
    Parsed out;
    // Les morceaux : coupes aux blancs (pas dans un repere $...$), recolles autour d'une virgule.
    const auto spans = markers::find(text, markers::Mode::Text);
    std::vector<std::string> raw;
    std::string cur;
    std::size_t s = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        while (s < spans.size() && spans[s].end() <= i) ++s;
        const bool inMarker = s < spans.size() && i >= spans[s].at && i < spans[s].end();
        const char c = text[i];
        if (!inMarker && std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) raw.push_back(std::move(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) raw.push_back(std::move(cur));
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        std::string t = raw[i];
        // 1.12.12 : "10, 20", "10 ,20", "10 , 20" - un seul point (avant : ignore sans un mot).
        while (i + 1 < raw.size() && (t.back() == ',' || raw[i + 1].front() == ',')) t += raw[++i];
        tokens.push_back(std::move(t));
    }
    for (std::size_t k = 0; k < tokens.size(); ++k) {
        const std::string& t = tokens[k];
        Problem pb;
        pb.position = k + 1;
        pb.written = t;
        const auto comma = t.find(',');
        if (comma == std::string::npos) {
            double lone = 0;
            pb.why = numberOf(t, lone) ? "un nombre seul (il faut x,y) : ignor\xC3\xA9" : "illisible (il faut x,y) : ignor\xC3\xA9";
            out.problems.push_back(std::move(pb));
            out.entries.push_back(Vertex{0, 0, t, {}, false, t});
            continue;
        }
        const std::string xs = t.substr(0, comma), ys = t.substr(comma + 1);
        double x = 0, y = 0;
        // Comme avant : x avant la premiere virgule, y apres (10,5,20 : y = 5,2).
        const bool ok = numberOf(xs, x) && numberOf(ys, y);
        if (ok) out.points.push_back(Vertex{x, y, xs, ys});
        out.entries.push_back(ok ? Vertex{x, y, xs, ys} : Vertex{0, 0, xs, ys, false, t});
        if (ys.find(',') != std::string::npos) {
            pb.kept = ok;
            pb.why = ok ? "trois nombres, lu x " + formatNumber(x) + " ; y " + formatNumber(y)
                              + " - une virgule d\xC3\xA9" "cimale ? \xC3\xA9" "cris les d\xC3\xA9" "cimales avec un point (10.5,20)"
                        : "plus de deux nombres (une virgule d\xC3\xA9" "cimale ? \xC3\xA9" "cris 10.5,20) : ignor\xC3\xA9";
            out.problems.push_back(std::move(pb));
        } else if (!ok) {
            pb.why = "illisible (il faut deux nombres x,y) : ignor\xC3\xA9";
            out.problems.push_back(std::move(pb));
        }
    }
    return out;
}

std::string format(const std::vector<Vertex>& pts) {
    std::string s;
    for (const auto& p : pts) {
        if (!p.ok && p.raw.empty()) continue;
        if (!s.empty()) s += ' ';
        if (!p.ok) s += p.raw;
        else s += (p.xText.empty() ? formatNumber(p.x) : p.xText) + "," + (p.yText.empty() ? formatNumber(p.y) : p.yText);
    }
    return s;
}

int entryOfPoint(const Parsed& parsed, int point) {
    int seen = -1;
    for (std::size_t e = 0; e < parsed.entries.size(); ++e)
        if (parsed.entries[e].ok && ++seen == point) return static_cast<int>(e);
    return -1;
}

int pointOfEntry(const Parsed& parsed, int entry) {
    if (entry < 0 || static_cast<std::size_t>(entry) >= parsed.entries.size() || !parsed.entries[static_cast<std::size_t>(entry)].ok) return -1;
    int point = 0;
    for (int e = 0; e < entry; ++e) point += parsed.entries[static_cast<std::size_t>(e)].ok ? 1 : 0;
    return point;
}

void setCell(Vertex& v, bool xField, const std::string& text) {
    (xField ? v.xText : v.yText) = text;
    double x = 0, y = 0;
    const bool ok = numberOf(v.xText, x) && numberOf(v.yText, y);
    v.ok = ok;
    if (ok) {
        v.x = x;
        v.y = y;
        v.raw.clear();
    } else {
        v.raw = v.xText + "," + v.yText;
    }
}

std::string describe(const Problem& p) { return "point " + std::to_string(p.position) + " (" + p.written + ") : " + p.why; }

std::optional<std::string> cellText(std::string_view typed) {
    const std::string t = trimmedOf(typed);
    if (t.empty() || t.find_first_of(" \t") != std::string::npos) return std::nullopt;   // un blanc couperait la liste
    double v = 0;
    // 12,5 tape a la francaise : ecrit 12.5 (une virgule couperait le point x,y en deux).
    if (parseNumber(t, v)) return t.find(',') == std::string::npos ? t : formatNumber(v);
    if (t.find(',') != std::string::npos) return std::nullopt;
    // Un repere ($Cote_X$) : garde tel quel, Dupliquer le remplacera.
    if (!markers::find(t, markers::Mode::Text).empty()) return t;
    return std::nullopt;
}

// ============================================================ les formes ===
bool isShape(const Object& o) noexcept { return o.kind == Kind::Line || o.kind == Kind::Polygon; }
std::size_t minimum(const Object& o) noexcept { return o.kind == Kind::Line ? 2 : 3; }

bool editable(const Object& o) {
    if (!isShape(o) || hasPointsFrom(o)) return false;
    const Prop* p = o.find("points");
    return !p || p->expr.empty();
}

void setVertices(Object& o, std::vector<Vertex> pts) { fit(o, std::move(pts)); }

// ===================================================== la courbe de reference ===
bool referenceEditable(const Object& o) {
    if (o.kind != Kind::XYChart) return false;
    const Prop* r = o.find("reference");
    if (!r) return true;
    if (!r->expr.empty() || sources::xyReferenceSource(r->value)) return false;
    // Le dessin la lit en entier (un morceau illisible : il n'en montre rien) - sinon, elle se corrige dans Proprietes.
    std::vector<std::pair<double, double>> xy;
    return parseXYPoints(r->value, xy) && listOf(o).problems.empty();
}

void setReference(Object& o, std::vector<Vertex> pts) {
    // Les points lus, par x croissant ; un morceau illisible garde son texte, a la fin.
    std::stable_partition(pts.begin(), pts.end(), [](const Vertex& p) { return p.ok; });
    const auto end = std::find_if(pts.begin(), pts.end(), [](const Vertex& p) { return !p.ok; });
    std::stable_sort(pts.begin(), end, [](const Vertex& a, const Vertex& b) { return a.x < b.x; });
    o.set("reference", vertices::format(pts));
}

bool xyPlot(const Object& o, XYPlot& out) {
    // La mise en page de drawXYChart (HmiChartsPainter.cpp) : le titre, la legende, les titres des axes.
    if (o.kind != Kind::XYChart || o.text("scale", "fixe") == "auto") return false;
    const Box b = o.box();
    const double fs = std::clamp(o.number("fontSize", 12), 7.0, 40.0);
    const double top0 = o.text("text").empty() ? 6 : fs * 1.35 * 1.6 + 2;
    const bool legend = o.flag("legend", true) && !chartItems(o).empty();
    const double top = top0 + (legend ? fs * 1.6 : 4);
    const bool xLabel = !o.text("xLabel").empty(), yLabel = !o.text("yLabel").empty();
    const double left = 50, right = 12, bottom = fs * 1.6 + (xLabel ? fs * 1.6 + 4 : 6);
    const double pw = std::max(10.0, b.w - left - right), ph = std::max(10.0, b.h - top - bottom - (yLabel ? fs * 1.4 : 0));
    out.area = {left, top + (yLabel ? fs * 1.4 : 0), pw, ph};
    out.x0 = o.number("xmin", 0);
    out.x1 = o.number("xmax", 100);
    out.y0 = o.number("ymin", 0);
    out.y1 = o.number("ymax", 100);
    if (out.x1 <= out.x0) out.x1 = out.x0 + 1;
    if (out.y1 <= out.y0) out.y1 = out.y0 + 1;
    return true;
}

// ============================================================= Contenu ===
bool hasList(const Object& o) noexcept { return isShape(o) || o.kind == Kind::XYChart; }

Parsed listOf(const Object& o) {
    if (o.kind != Kind::XYChart) return parse(o.text("points"));
    // La reference : un point-virgule y separe aussi les points (parseXYPoints) ; rangee comme le dessin.
    std::string text = o.text("reference");
    std::replace(text.begin(), text.end(), ';', ' ');
    Parsed p = parse(text);
    const auto byX = [](const Vertex& a, const Vertex& b) { return a.x < b.x; };
    std::stable_partition(p.entries.begin(), p.entries.end(), [](const Vertex& v) { return v.ok; });
    const auto end = std::find_if(p.entries.begin(), p.entries.end(), [](const Vertex& v) { return !v.ok; });
    std::stable_sort(p.entries.begin(), end, byX);
    std::stable_sort(p.points.begin(), p.points.end(), byX);
    return p;
}

bool listEditable(const Object& o) { return o.kind == Kind::XYChart ? referenceEditable(o) : editable(o); }

std::size_t listMinimum(const Object& o) noexcept { return o.kind == Kind::XYChart ? 0 : minimum(o); }

void setList(Object& o, std::vector<Vertex> pts) {
    if (o.kind == Kind::XYChart) setReference(o, std::move(pts));
    else setVertices(o, std::move(pts));
}

// ============================================================ les cibles ===
std::vector<edit::Pt> points(const View& v, const Target& t) {
    std::vector<edit::Pt> out;
    if (const Object* o = shapeOf(v, t)) {
        for (const auto& p : parse(o->text("points")).points) out.push_back(edit::toView(*o, {p.x, p.y}));
    } else if (const Object* m = mapOf(v, t)) {
        std::vector<MapZone> zones;
        if (!zoneOf(*m, t, zones)) return out;
        const Box b = m->box();
        for (const auto& [px, py] : zones[static_cast<std::size_t>(t.zone)].points)
            out.push_back(edit::toView(*m, {px * b.w / 100.0, py * b.h / 100.0}));
    } else if (const Object* c = chartOf(v, t)) {
        XYPlot pl;
        if (!xyPlot(*c, pl)) return out;
        for (const auto& p : referencePoints(*c)) {
            const double fx = (std::clamp(p.x, pl.x0, pl.x1) - pl.x0) / (pl.x1 - pl.x0);
            const double fy = (std::clamp(p.y, pl.y0, pl.y1) - pl.y0) / (pl.y1 - pl.y0);
            out.push_back(edit::toView(*c, {pl.area.x + fx * pl.area.w, pl.area.y + (1.0 - fy) * pl.area.h}));
        }
    }
    return out;
}

bool closed(const View& v, const Target& t) {
    if (const Object* o = shapeOf(v, t)) return o->kind == Kind::Polygon;
    return mapOf(v, t) != nullptr;
}

std::size_t minimum(const View& v, const Target& t) {
    if (const Object* o = shapeOf(v, t)) return minimum(*o);
    return chartOf(v, t) ? 2 : 3;
}

bool editable(const View& v, const Target& t) {
    if (const Object* o = shapeOf(v, t)) return editable(*o) && !v.effectivelyLocked(*o);
    if (const Object* m = mapOf(v, t)) {
        const Prop* z = m->find("mapZones");
        std::vector<MapZone> zones;
        return (!z || z->expr.empty()) && !v.effectivelyLocked(*m) && zoneOf(*m, t, zones);
    }
    if (const Object* c = chartOf(v, t)) {
        XYPlot pl;
        return referenceEditable(*c) && xyPlot(*c, pl) && !v.effectivelyLocked(*c);
    }
    return false;
}

int vertexAt(const View& v, const Target& t, edit::Pt at, double tolerance) {
    const auto pts = points(v, t);
    int best = -1;
    double bestD = tolerance;
    for (std::size_t i = 0; i < pts.size(); ++i)
        if (const double d = distance(pts[i], at); d <= bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    return best;
}

int segmentAt(const View& v, const Target& t, edit::Pt at, double tolerance, edit::Pt* foot) {
    const auto pts = points(v, t);
    if (pts.size() < 2) return -1;
    const std::size_t n = closed(v, t) ? pts.size() : pts.size() - 1;
    int best = -1;
    double bestD = tolerance;
    for (std::size_t i = 0; i < n; ++i) {
        const edit::Pt q = nearestOn(at, pts[i], pts[(i + 1) % pts.size()]);
        if (const double d = distance(q, at); d <= bestD) {
            bestD = d;
            best = static_cast<int>(i);
            if (foot) *foot = q;
        }
    }
    return best;
}

bool moveVertex(View& v, const Target& t, std::size_t index, edit::Pt at) {
    if (!editable(v, t)) return false;
    if (Object* o = shapeOf(v, t)) {
        auto parsed = parse(o->text("points"));
        const int e = entryOfPoint(parsed, static_cast<int>(index));
        if (e < 0) return false;
        auto& pt = parsed.entries[static_cast<std::size_t>(e)];
        const edit::Pt l = edit::toLocal(*o, at);
        if (std::fabs(l.x - pt.x) < 1e-9 && std::fabs(l.y - pt.y) < 1e-9) return false;
        pt = Vertex{l.x, l.y, {}, {}};
        fit(*o, std::move(parsed.entries));
        edit::refreshGroupBounds(v);
        return true;
    }
    if (t.what == Target::What::Reference) {
        Object* c = v.object(t.object);
        double x = 0, y = 0;
        if (!c || !dataOf(*c, at, x, y)) return false;
        auto pts = referencePoints(*c);
        if (index >= pts.size()) return false;
        // Une courbe y(x) : le point reste entre ses voisins.
        if (index > 0) x = std::max(x, pts[index - 1].x);
        if (index + 1 < pts.size()) x = std::min(x, pts[index + 1].x);
        if (std::fabs(x - pts[index].x) < 1e-12 && std::fabs(y - pts[index].y) < 1e-12) return false;
        pts[index] = Vertex{x, y, {}, {}};
        setReference(*c, std::move(pts));
        return true;
    }
    Object* m = v.object(t.object);
    std::vector<MapZone> zones;
    std::pair<double, double> pc;
    if (!m || !zoneOf(*m, t, zones) || !percentOf(*m, at, pc)) return false;
    auto& z = zones[static_cast<std::size_t>(t.zone)];
    if (index >= z.points.size() || z.points[index] == pc) return false;
    z.points[index] = pc;
    m->set("mapZones", formatMapZones(zones));
    return true;
}

int insertVertex(View& v, const Target& t, std::size_t after, edit::Pt at) {
    if (!editable(v, t)) return -1;
    if (Object* o = shapeOf(v, t)) {
        auto parsed = parse(o->text("points"));
        const int e = entryOfPoint(parsed, static_cast<int>(after));
        if (e < 0) return -1;
        const edit::Pt l = edit::toLocal(*o, at);
        parsed.entries.insert(parsed.entries.begin() + e + 1, Vertex{l.x, l.y, {}, {}});
        fit(*o, std::move(parsed.entries));
        edit::refreshGroupBounds(v);
        return static_cast<int>(after + 1);
    }
    if (t.what == Target::What::Reference) {
        Object* c = v.object(t.object);
        double x = 0, y = 0;
        if (!c || !dataOf(*c, at, x, y)) return -1;
        auto pts = referencePoints(*c);
        if (after >= pts.size()) return -1;
        x = std::max(x, pts[after].x);
        if (after + 1 < pts.size()) x = std::min(x, pts[after + 1].x);
        pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(after + 1), Vertex{x, y, {}, {}});
        setReference(*c, std::move(pts));
        return static_cast<int>(after + 1);
    }
    Object* m = v.object(t.object);
    std::vector<MapZone> zones;
    std::pair<double, double> pc;
    if (!m || !zoneOf(*m, t, zones) || !percentOf(*m, at, pc)) return -1;
    auto& z = zones[static_cast<std::size_t>(t.zone)];
    if (after >= z.points.size()) return -1;
    z.points.insert(z.points.begin() + static_cast<std::ptrdiff_t>(after + 1), pc);
    m->set("mapZones", formatMapZones(zones));
    return static_cast<int>(after + 1);
}

bool removeVertex(View& v, const Target& t, std::size_t index) {
    if (!editable(v, t)) return false;
    if (Object* o = shapeOf(v, t)) {
        auto parsed = parse(o->text("points"));
        const int e = entryOfPoint(parsed, static_cast<int>(index));
        if (e < 0 || parsed.points.size() <= minimum(*o)) return false;
        parsed.entries.erase(parsed.entries.begin() + e);
        fit(*o, std::move(parsed.entries));
        edit::refreshGroupBounds(v);
        return true;
    }
    if (t.what == Target::What::Reference) {
        Object* c = v.object(t.object);
        if (!c) return false;
        auto pts = referencePoints(*c);
        if (index >= pts.size() || pts.size() <= 2) return false;
        pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(index));
        setReference(*c, std::move(pts));
        return true;
    }
    Object* m = v.object(t.object);
    std::vector<MapZone> zones;
    if (!m || !zoneOf(*m, t, zones)) return false;
    auto& z = zones[static_cast<std::size_t>(t.zone)];
    if (index >= z.points.size() || z.points.size() <= 3) return false;
    z.points.erase(z.points.begin() + static_cast<std::ptrdiff_t>(index));
    m->set("mapZones", formatMapZones(zones));
    return true;
}

int addZone(View& v, Id map, const std::vector<edit::Pt>& at) {
    Object* m = v.object(map);
    if (!m || m->kind != Kind::ZoneMap || at.size() < 3 || v.effectivelyLocked(*m)) return -1;
    if (const Prop* z = m->find("mapZones"); z && !z->expr.empty()) return -1;
    auto zones = parseMapZones(m->text("mapZones"));
    MapZone z;
    z.name = "Zone " + std::to_string(zones.size() + 1);
    for (const auto& p : at) {
        std::pair<double, double> pc;
        if (!percentOf(*m, p, pc)) return -1;
        z.points.push_back(pc);
    }
    zones.push_back(std::move(z));
    m->set("mapZones", formatMapZones(zones));
    return static_cast<int>(zones.size()) - 1;
}

// ========================================================== le magnetisme ===
Snap snapPoint(const View& v, edit::Pt p, const std::vector<edit::Pt>& siblings, Id exclude, double threshold) {
    Snap r;
    r.at = p;
    std::vector<double> tx, ty;
    if (v.grid.snapGuides)
        for (const auto& g : v.guides) (g.vertical ? tx : ty).push_back(g.position);
    if (v.grid.snapObjects) {
        tx.insert(tx.end(), {0.0, v.width / 2.0, static_cast<double>(v.width)});
        ty.insert(ty.end(), {0.0, v.height / 2.0, static_cast<double>(v.height)});
        std::set<Id> skip{exclude};
        if (exclude != kNoId)
            for (Id d : v.descendantsOf(exclude)) skip.insert(d);
        for (const auto& o : v.objects) {
            if (skip.count(o.id) || v.effectivelyHidden(o) || o.parent != kNoId) continue;
            const Box b = edit::rotatedBounds(o);
            tx.insert(tx.end(), {b.x, b.cx(), b.right()});
            ty.insert(ty.end(), {b.y, b.cy(), b.bottom()});
        }
        for (const auto& s : siblings) {
            tx.push_back(s.x);
            ty.push_back(s.y);
        }
    }
    const auto nearest = [&](double mine, const std::vector<double>& targets, double& to) {
        double best = threshold;
        bool found = false;
        for (double t : targets)
            if (const double d = std::fabs(t - mine); d <= best) {
                best = d;
                to = t;
                found = true;
            }
        return found;
    };
    double sx = 0, sy = 0;
    const bool gotX = nearest(p.x, tx, sx), gotY = nearest(p.y, ty, sy);
    if (gotX) {
        r.at.x = sx;
        r.xLines = {sx};
    }
    if (gotY) {
        r.at.y = sy;
        r.yLines = {sy};
    }
    if (v.grid.snapGrid && v.grid.step > 1) {
        if (!gotX) r.at.x = edit::snapToGrid(p.x, v.grid.step);
        if (!gotY) r.at.y = edit::snapToGrid(p.y, v.grid.step);
    }
    return r;
}

// ======================================================== points depuis ===
bool hasPointsFrom(const Object& o) {
    if (!isShape(o)) return false;
    const Prop* p = o.find(kPointsFromKey);
    return p && p->expr.empty() && !trimmedOf(p->value).empty();
}

std::optional<PointsSource> pointsSourceOf(std::string_view text) {
    const std::string t = trimmedOf(text);
    if (t.empty()) return std::nullopt;
    // Deux formules (Profil[*].X, Profil[*].Y) : la virgule de premier niveau, comme la reference d'une courbe XY.
    if (const auto xy = sources::xyReferenceSource(t)) return PointsSource{xy->first, xy->second, false};
    if (t.find(',') != std::string::npos) return std::nullopt;
    if (sources::jokerOf(t)) return PointsSource{t, {}, true};
    // Un chemin seul (Courbe, Lignes[2].Valeurs) : ses elements, x et y alternes.
    for (const char c : t)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '[' || c == ']' || c == '$'
              || static_cast<unsigned char>(c) >= 0x80))
            return std::nullopt;
    return PointsSource{t + "[*]", {}, true};
}

std::optional<std::vector<edit::Pt>> pointsFromSource(const Object& o, const sources::SourceFn& source, const sources::TextFn& text) {
    const auto ps = pointsSourceOf(o.text(kPointsFromKey));
    if (!ps || !source || !text) return std::nullopt;
    const auto xj = sources::jokerOf(ps->x);
    if (!xj) return std::nullopt;
    const auto src = source(sources::sourceName(*xj));
    if (!src) return std::nullopt;
    const auto read = [&](const std::string& formula, double& out) {
        const auto t = text(formula);
        return t && parseNumber(*t, out);
    };
    std::vector<std::pair<double, double>> data;
    if (ps->alternate) {
        // x0, y0, x1, y1... : une paire qui a un nombre illisible tombe, les suivantes gardent leur rang.
        for (std::size_t k = 0; k + 1 < src->indices.size(); k += 2) {
            double x = 0, y = 0;
            if (read(sources::itemAt(*xj, src->indices[k], src->enumeration), x)
                && read(sources::itemAt(*xj, src->indices[k + 1], src->enumeration), y))
                data.emplace_back(x, y);
        }
    } else {
        const auto yj = sources::jokerOf(ps->y);
        for (const auto& index : src->indices) {
            double x = 0, y = 0;
            if (read(sources::itemAt(*xj, index, src->enumeration), x) && read(yj ? sources::itemAt(*yj, index, src->enumeration) : ps->y, y))
                data.emplace_back(x, y);
        }
    }
    // L'echelle : ecrite (une formule se calcule ; un repere $Max$ est transparent), sinon celle des donnees.
    const auto bound = [&](std::string_view key, double& out) {
        const Prop* p = o.find(key);
        if (!p) return false;
        if (!p->expr.empty()) return read(markers::strip(p->expr, markers::Mode::Expression), out);
        return parseNumber(trimmedOf(markers::strip(p->value, markers::Mode::Text)), out);
    };
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    if (!data.empty()) {
        x0 = x1 = data.front().first;
        y0 = y1 = data.front().second;
        for (const auto& [x, y] : data) {
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    double v = 0;
    if (bound(kPointsXMinKey, v)) x0 = v;
    if (bound(kPointsXMaxKey, v)) x1 = v;
    if (bound(kPointsYMinKey, v)) y0 = v;
    if (bound(kPointsYMaxKey, v)) y1 = v;
    const Box b = o.box();
    std::vector<edit::Pt> out;
    for (const auto& [x, y] : data) {
        // Une echelle nulle (un seul point, ou min = max) : au milieu.
        const double fx = x1 > x0 ? (std::clamp(x, x0, x1) - x0) / (x1 - x0) : 0.5;
        const double fy = y1 > y0 ? (std::clamp(y, y0, y1) - y0) / (y1 - y0) : 0.5;
        out.push_back({fx * b.w, (1.0 - fy) * b.h});
    }
    return out;
}

sources::TextFn previewText(const Project& p) {
    // Les cases d'une variable IHM (Courbe[2], Profil[1].X) et leur valeur initiale, une fois par variable.
    auto cache = std::make_shared<std::map<std::string, std::map<std::string, std::string>>>();
    return [&p, cache](std::string_view formula) -> std::optional<std::string> {
        std::string f;
        for (const char c : markers::strip(formula, markers::Mode::Expression))
            if (!std::isspace(static_cast<unsigned char>(c))) f += c;
        const std::string root = f.substr(0, f.find_first_of(".["));
        const Variable* var = p.variable(root);
        if (!var) return std::nullopt;
        f = var->name + f.substr(root.size());
        const auto shape = typeform::decompose(var->type);
        if (shape.form == typeform::Form::List || shape.form == typeform::Form::Vector) {
            // Une LIST : l'element de son litteral ([1.5, 2, 3] ; une structure (X := 1, Y := 2) : son membre).
            if (f.size() <= root.size() + 2 || f[root.size()] != '[') return std::nullopt;
            const auto close = f.find(']', root.size());
            if (close == std::string::npos) return std::nullopt;
            double at = 0;
            if (!parseNumber(f.substr(root.size() + 1, close - root.size() - 1), at) || at < 0) return std::nullopt;
            std::vector<std::string> items;
            if (!typeform::literalItems(var->initial, items) || static_cast<std::size_t>(at) >= items.size()) return std::nullopt;
            std::string item = trimmedOf(items[static_cast<std::size_t>(at)]);
            const std::string member = close + 1 < f.size() && f[close + 1] == '.' ? f.substr(close + 2) : std::string{};
            if (member.empty()) return item;
            std::vector<std::string> fields;
            if (!typeform::literalItems(item, fields)) return std::nullopt;
            for (const auto& field : fields) {
                const auto def = field.find(":=");
                if (def == std::string::npos) continue;
                std::string name = trimmedOf(std::string_view(field).substr(0, def));
                if (name.size() == member.size()
                    && std::equal(name.begin(), name.end(), member.begin(),
                                  [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
                    return trimmedOf(std::string_view(field).substr(def + 2));
            }
            return std::nullopt;
        }
        if (types::isRich(var->type)) return std::nullopt;
        auto& leaves = (*cache)[var->name];
        if (leaves.empty()) {
            const auto flat = types::flatten(p, var->name, var->type, var->initial);
            for (const auto& l : flat.leaves) leaves[lowered(l.path)] = l.initial.empty() ? std::string("0") : l.initial;
            if (leaves.empty()) leaves[""] = "";
        }
        const auto it = leaves.find(lowered(f));
        if (it == leaves.end()) return std::nullopt;
        return it->second;
    };
}

std::vector<edit::Pt> previewPoints(const Project& p, const Object& o) {
    if (!hasPointsFrom(o)) return {};
    const auto pts = pointsFromSource(o, sources::previewSources(p), previewText(p));
    return pts ? *pts : std::vector<edit::Pt>{};
}

} // namespace hmi::vertices
