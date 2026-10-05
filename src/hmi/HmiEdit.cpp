#include "HmiEdit.hpp"
#include "HmiNavigation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace hmi::edit {

namespace {

constexpr double kPi = 3.14159265358979323846;

double rad(double deg) { return deg * kPi / 180.0; }

bool contains(const std::vector<Id>& v, Id id) { return std::find(v.begin(), v.end(), id) != v.end(); }

// Deplace un objet ET tout ce qu'il contient.
void shift(View& v, Id id, double dx, double dy) {
    std::vector<Id> all{id};
    for (Id d : v.descendantsOf(id)) all.push_back(d);
    for (Id a : all) {
        auto* o = v.object(a);
        if (!o) continue;
        o->setNumber("x", o->number("x") + dx);
        o->setNumber("y", o->number("y") + dy);
    }
}

Pt pivotLocal(const Object& o) {
    const Box b = o.box();
    return {b.w * o.number("pivotX", 0.5), b.h * o.number("pivotY", 0.5)};
}

Pt pivotView(const Object& o) {
    const Box b = o.box();
    const Pt p = pivotLocal(o);
    return {b.x + p.x, b.y + p.y};
}

Pt rotateAround(Pt p, Pt c, double deg) {
    const double a = rad(deg), cs = std::cos(a), sn = std::sin(a);
    const double dx = p.x - c.x, dy = p.y - c.y;
    return {c.x + dx * cs - dy * sn, c.y + dx * sn + dy * cs};
}

// Place le pivot de l'objet en `p` sans changer sa taille.
void putPivotAt(Object& o, Pt p) {
    const Pt pl = pivotLocal(o);
    o.setNumber("x", p.x - pl.x);
    o.setNumber("y", p.y - pl.y);
}

double segmentDistance(Pt p, Pt a, Pt b) {
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double len2 = vx * vx + vy * vy;
    double t = len2 > 0 ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double qx = a.x + t * vx - p.x, qy = a.y + t * vy - p.y;
    return std::sqrt(qx * qx + qy * qy);
}

bool insidePolygon(Pt p, const std::vector<Pt>& poly) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y))
            && (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
            in = !in;
    }
    return in;
}

std::string baseName(const std::string& name) {
    std::size_t end = name.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
    if (end < name.size() && end > 0 && name[end - 1] == '_') return name.substr(0, end - 1);
    return name;
}

// Les objets d'un calque, rang dans `objects`, dans l'ordre.
std::vector<int> slotsOfLayer(const View& v, Id layer) {
    std::vector<int> out;
    for (std::size_t i = 0; i < v.objects.size(); ++i)
        if (v.objects[i].layer == layer) out.push_back(static_cast<int>(i));
    return out;
}

} // namespace

double normalizeAngle(double a) noexcept {
    double r = std::fmod(a, 360.0);
    if (r < 0) r += 360.0;
    if (std::fabs(r - 360.0) < 1e-9) r = 0;
    return r;
}

// -------------------------------------------------------------- repere ------
Pt toView(const Object& o, Pt local) {
    const Box b = o.box();
    double u = local.x, w = local.y;
    if (o.flipH()) u = b.w - u;
    if (o.flipV()) w = b.h - w;
    const Pt pl = pivotLocal(o);
    const Pt r = rotateAround({u, w}, pl, o.rotation());
    return {b.x + r.x, b.y + r.y};
}

Pt toLocal(const Object& o, Pt p) {
    const Box b = o.box();
    const Pt pl = pivotLocal(o);
    Pt r = rotateAround({p.x - b.x, p.y - b.y}, pl, -o.rotation());
    if (o.flipH()) r.x = b.w - r.x;
    if (o.flipV()) r.y = b.h - r.y;
    return r;
}

std::vector<Pt> corners(const Object& o) {
    const Box b = o.box();
    return {toView(o, {0, 0}), toView(o, {b.w, 0}), toView(o, {b.w, b.h}), toView(o, {0, b.h})};
}

Box rotatedBounds(const Object& o) {
    const auto c = corners(o);
    double l = c[0].x, t = c[0].y, r = c[0].x, bt = c[0].y;
    for (const auto& p : c) {
        l = std::min(l, p.x); r = std::max(r, p.x);
        t = std::min(t, p.y); bt = std::max(bt, p.y);
    }
    return {l, t, r - l, bt - t};
}

std::vector<Pt> points(const Object& o) {
    std::vector<Pt> out;
    std::istringstream in(o.text("points"));
    std::string pair;
    while (in >> pair) {
        const auto comma = pair.find(',');
        if (comma == std::string::npos) continue;
        double x = 0, y = 0;
        if (parseNumber(pair.substr(0, comma), x) && parseNumber(pair.substr(comma + 1), y)) out.push_back({x, y});
    }
    return out;
}

void setPoints(Object& o, const std::vector<Pt>& pts) {
    std::string s;
    for (const auto& p : pts) {
        if (!s.empty()) s += ' ';
        s += formatNumber(p.x) + "," + formatNumber(p.y);
    }
    o.set("points", s);
}

bool hit(const Object& o, double x, double y, double tolerance) {
    const Box b = o.box();
    const Pt l = toLocal(o, {x, y});
    switch (o.kind) {
        case Kind::Line: {
            const auto pts = points(o);
            for (std::size_t i = 1; i < pts.size(); ++i)
                if (segmentDistance(l, pts[i - 1], pts[i]) <= tolerance + o.number("strokeWidth", 1) / 2) return true;
            return false;
        }
        case Kind::Polygon: {
            const auto pts = points(o);
            if (pts.size() >= 3 && insidePolygon(l, pts)) return true;
            for (std::size_t i = 0; i < pts.size(); ++i)
                if (segmentDistance(l, pts[i], pts[(i + 1) % pts.size()]) <= tolerance) return true;
            return false;
        }
        case Kind::Ellipse:
        case Kind::Indicator: {
            if (o.kind == Kind::Indicator && o.text("shape") != "rond") break;
            const double rx = b.w / 2 + tolerance, ry = b.h / 2 + tolerance;
            if (rx <= 0 || ry <= 0) return false;
            const double nx = (l.x - b.w / 2) / rx, ny = (l.y - b.h / 2) / ry;
            return nx * nx + ny * ny <= 1.0;
        }
        default: break;
    }
    return l.x >= -tolerance && l.y >= -tolerance && l.x <= b.w + tolerance && l.y <= b.h + tolerance;
}

// ------------------------------------------------------------ selection ----
Id topLevelOf(const View& v, Id id) {
    Id cur = id;
    for (int guard = 0; guard < 64; ++guard) {
        const auto* o = v.object(cur);
        if (!o || o->parent == kNoId) return cur;
        cur = o->parent;
    }
    return cur;
}

std::vector<Id> units(const View& v, const std::vector<Id>& ids) {
    std::vector<Id> out;
    for (Id id : ids) {
        if (!v.object(id) || contains(out, id)) continue;
        bool ancestorSelected = false;
        Id p = v.object(id)->parent;
        for (int guard = 0; p != kNoId && guard < 64; ++guard) {
            if (contains(ids, p)) { ancestorSelected = true; break; }
            const auto* po = v.object(p);
            p = po ? po->parent : kNoId;
        }
        if (!ancestorSelected) out.push_back(id);
    }
    return out;
}

Box selectionBounds(const View& v, const std::vector<Id>& ids) {
    bool first = true;
    Box out;
    for (Id id : units(v, ids)) {
        const auto* o = v.object(id);
        if (!o) continue;
        const Box b = rotatedBounds(*o);
        out = first ? b : out.united(b);
        first = false;
    }
    return out;
}

Id hitTest(const View& v, double x, double y, Id insideGroup, double tolerance) {
    const auto order = v.paintOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Object& o = **it;
        if (o.kind == Kind::Group) continue;              // un groupe se prend par ses enfants
        if (v.effectivelyHidden(o) || v.effectivelyLocked(o)) continue;
        // "visible" a FAUX ne compte pas ici : un objet invisible en marche se
        // prend quand meme dans l'editeur, sinon on ne le rattraperait plus.
        if (!hit(o, x, y, tolerance)) continue;
        // Lot 12 : hors de la page d'un conteneur a onglets, du cadre d'un panneau :
        // il ne se voit pas, il ne se prend pas (sauf dans le panneau ou l'on est entre).
        if (clippedAt(v, o, x, y, insideGroup)) continue;
        // Remonter jusqu'au niveau ou l'on edite. Lot 12 : un conteneur a onglets, un
        // cadre, un panneau laissent prendre leurs enfants directement.
        // (Un groupe au-dessus reste pris en entier.)
        const auto groupAbove = [&](Id from) {
            int depth = 0;
            for (Id p = from; p != kNoId && p != insideGroup && depth < 64; ++depth) {
                const auto* po = v.object(p);
                if (!po) break;
                if (po->kind == Kind::Group) return true;
                p = po->parent;
            }
            return false;
        };
        Id cur = o.id;
        for (int guard = 0; guard < 64; ++guard) {
            const auto* co = v.object(cur);
            if (!co || co->parent == insideGroup) return cur;
            const auto* parent = v.object(co->parent);
            if (parent && parent->kind != Kind::Container && kindHoldsChildren(parent->kind) && !groupAbove(parent->id)) return cur;
            cur = co->parent;
        }
        return o.id;
    }
    return kNoId;
}

std::vector<Id> inRect(const View& v, const Box& r, Id insideGroup) {
    std::vector<Id> out;
    for (const auto* o : v.paintOrder()) {
        if (o->parent != insideGroup) continue;
        if (v.effectivelyHidden(*o) || v.effectivelyLocked(*o)) continue;
        const Box b = rotatedBounds(*o);
        if (b.x >= r.x && b.y >= r.y && b.right() <= r.right() && b.bottom() <= r.bottom()) out.push_back(o->id);
    }
    return out;
}

// ------------------------------------------------------------ creation -----
Id addObject(View& v, Object o) {
    if (v.layerRank(o.layer) < 0) o.layer = v.activeLayer;
    const Id id = o.id;
    v.objects.push_back(std::move(o));
    return id;
}

Id add(Project& p, View& v, Kind k, double x, double y) {
    Object o = makeObject(k, p.allocate(), uniqueObjectName(v, kindKey(k)), x, y, v.activeLayer);
    return addObject(v, std::move(o));
}

std::vector<Id> duplicate(Project& p, View& v, const std::vector<Id>& ids, double dx, double dy) {
    std::vector<Id> created;
    for (Id id : units(v, ids)) {
        std::vector<Id> tree{id};
        for (Id d : v.descendantsOf(id)) tree.push_back(d);
        std::map<Id, Id> renamed;
        for (Id t : tree) renamed[t] = p.allocate();
        std::vector<Object> copies;
        // Dans l'ordre de dessin d'origine, pour que la copie se superpose
        // comme l'original.
        for (const auto& o : v.objects) {
            if (!renamed.count(o.id)) continue;
            Object c = o;
            c.id = renamed[o.id];
            c.parent = renamed.count(o.parent) ? renamed[o.parent] : o.parent;
            c.locked = false;
            copies.push_back(std::move(c));
        }
        for (auto& c : copies) {
            c.name = uniqueObjectName(v, baseName(c.name));
            c.setNumber("x", c.number("x") + dx);
            c.setNumber("y", c.number("y") + dy);
            v.objects.push_back(c);
        }
        created.push_back(renamed[id]);
    }
    return created;
}

void remove(View& v, const std::vector<Id>& ids) {
    std::set<Id> doomed;
    for (Id id : ids) {
        if (!v.object(id)) continue;
        doomed.insert(id);
        for (Id d : v.descendantsOf(id)) doomed.insert(d);
    }
    std::erase_if(v.objects, [&](const Object& o) { return doomed.count(o.id) != 0; });
    // Un groupe qui n'a plus d'enfant n'a plus de sens.
    for (bool again = true; again;) {
        again = false;
        for (std::size_t i = 0; i < v.objects.size(); ++i) {
            if (v.objects[i].kind != Kind::Group) continue;
            if (!v.childrenOf(v.objects[i].id).empty()) continue;
            v.objects.erase(v.objects.begin() + static_cast<std::ptrdiff_t>(i));
            again = true;
            break;
        }
    }
    refreshGroupBounds(v);
}

bool validName(const std::string& n) noexcept {
    if (n.empty() || n.size() > 64) return false;
    if (!(std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_')) return false;
    for (char c : n)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool rename(View& v, Id id, const std::string& name, std::string* error) {
    auto* o = v.object(id);
    if (!o) { if (error) *error = "objet introuvable"; return false; }
    if (!validName(name)) {
        if (error) *error = "'" + name + "' : lettres, chiffres et _ seulement, sans commencer par un chiffre";
        return false;
    }
    for (const auto& other : v.objects)
        if (other.id != id && other.name == name) {
            if (error) *error = "'" + name + "' existe d\xC3\xA9j\xC3\xA0 dans la vue";
            return false;
        }
    o->name = name;
    return true;
}

// ------------------------------------------------------------ geometrie ----
void move(View& v, const std::vector<Id>& ids, double dx, double dy) {
    for (Id id : units(v, ids)) {
        const auto* o = v.object(id);
        if (!o || v.effectivelyLocked(*o)) continue;
        shift(v, id, dx, dy);
    }
    refreshGroupBounds(v);
}

void setBox(View& v, Id id, const Box& nb) {
    auto* o = v.object(id);
    if (!o || v.effectivelyLocked(*o)) return;
    const Box ob = o->box();
    const double sx = ob.w > 1e-9 ? nb.w / ob.w : 1.0;
    const double sy = ob.h > 1e-9 ? nb.h / ob.h : 1.0;

    if (o->kind == Kind::Group) {
        // Les enfants suivent, dans le repere du groupe : leur centre est mis a
        // l'echelle, leur taille aussi (echangee si l'enfant est tourne d'un
        // quart de tour par rapport au groupe).
        const Object group = *o;
        for (Id d : v.descendantsOf(id)) {
            auto* c = v.object(d);
            if (!c || c->kind == Kind::Group) continue;
            const Pt centre = pivotView(*c);
            const Pt local = toLocal(group, centre);
            Object resized = group;
            resized.setBox(nb);
            const Pt target = toView(resized, {local.x * sx, local.y * sy});
            const double rel = normalizeAngle(c->rotation() - group.rotation());
            const bool quarter = std::fabs(std::fmod(rel, 180.0) - 90.0) < 1.0;
            Box cb = c->box();
            cb.w *= quarter ? sy : sx;
            cb.h *= quarter ? sx : sy;
            c->setNumber("w", cb.w);
            c->setNumber("h", cb.h);
            if (c->kind == Kind::Line || c->kind == Kind::Polygon) {
                auto pts = points(*c);
                for (auto& p : pts) { p.x *= quarter ? sy : sx; p.y *= quarter ? sx : sy; }
                setPoints(*c, pts);
            }
            putPivotAt(*c, target);
        }
        o = v.object(id);
        o->setBox(nb);
        refreshGroupBounds(v);
        return;
    }

    if (o->kind == Kind::Line || o->kind == Kind::Polygon) {
        auto pts = points(*o);
        for (auto& p : pts) { p.x *= sx; p.y *= sy; }
        setPoints(*o, pts);
    }
    o->setBox(nb);
    refreshGroupBounds(v);
}

void scaleSelection(View& v, const std::vector<Id>& ids, const Box& from, const Box& to) {
    if (from.w <= 1e-9 || from.h <= 1e-9) return;
    const double sx = to.w / from.w, sy = to.h / from.h;
    for (Id id : units(v, ids)) {
        auto* o = v.object(id);
        if (!o || v.effectivelyLocked(*o)) continue;
        const Pt c = pivotView(*o);
        const Pt target{to.x + (c.x - from.x) * sx, to.y + (c.y - from.y) * sy};
        const double rel = normalizeAngle(o->rotation());
        const bool quarter = std::fabs(std::fmod(rel, 180.0) - 90.0) < 1.0;
        Box b = o->box();
        const double nw = b.w * (quarter ? sy : sx), nh = b.h * (quarter ? sx : sy);
        // setBox garde le coin : on le recalcule pour que le pivot tombe sur
        // la cible une fois la taille changee.
        setBox(v, id, {b.x, b.y, nw, nh});
        // Puis l'unite entiere (un groupe emmene ses membres) vient poser son
        // pivot sur la cible.
        const Pt now = pivotView(*v.object(id));
        shift(v, id, target.x - now.x, target.y - now.y);
    }
    refreshGroupBounds(v);
}

void rotate(View& v, const std::vector<Id>& ids, double delta, bool aroundSelectionCenter) {
    const auto list = units(v, ids);
    const Box sel = selectionBounds(v, list);
    const Pt centre{sel.cx(), sel.cy()};
    for (Id id : list) {
        auto* o = v.object(id);
        if (!o || v.effectivelyLocked(*o)) continue;
        if (o->kind == Kind::Group) {
            const Pt gc = pivotView(*o);
            for (Id d : v.descendantsOf(id)) {
                auto* c = v.object(d);
                if (!c) continue;
                putPivotAt(*c, rotateAround(pivotView(*c), gc, delta));
                c->setNumber("rot", normalizeAngle(c->rotation() + delta));
            }
            o = v.object(id);
        }
        o->setNumber("rot", normalizeAngle(o->rotation() + delta));
        if (aroundSelectionCenter && list.size() > 1) {
            const Pt before = pivotView(*o);
            const Pt after = rotateAround(before, centre, delta);
            shift(v, id, after.x - before.x, after.y - before.y);
        }
    }
    refreshGroupBounds(v);
}

void setRotation(View& v, const std::vector<Id>& ids, double angle) {
    for (Id id : units(v, ids)) {
        const auto* o = v.object(id);
        if (!o) continue;
        rotate(v, {id}, normalizeAngle(angle) - o->rotation(), false);
    }
}

void mirror(View& v, const std::vector<Id>& ids, bool horizontal) {
    const auto list = units(v, ids);
    const Box sel = selectionBounds(v, list);
    const double axis = horizontal ? sel.cx() : sel.cy();
    auto flipOne = [&](Object& o) {
        const Pt c = pivotView(o);
        const Pt target = horizontal ? Pt{2 * axis - c.x, c.y} : Pt{c.x, 2 * axis - c.y};
        o.setFlag(horizontal ? "flipH" : "flipV", !(horizontal ? o.flipH() : o.flipV()));
        o.setNumber("rot", normalizeAngle(-o.rotation()));
        putPivotAt(o, target);
    };
    for (Id id : list) {
        auto* o = v.object(id);
        if (!o || v.effectivelyLocked(*o)) continue;
        if (o->kind == Kind::Group)
            for (Id d : v.descendantsOf(id))
                if (auto* c = v.object(d)) flipOne(*c);
        flipOne(*v.object(id));
    }
    refreshGroupBounds(v);
}

void align(View& v, const std::vector<Id>& ids, Align mode) {
    const auto list = units(v, ids);
    if (list.empty()) return;
    // Un objet seul s'aligne sur la vue ; plusieurs, sur leur cadre commun.
    const Box ref = list.size() == 1 ? Box{0, 0, static_cast<double>(v.width), static_cast<double>(v.height)}
                                     : selectionBounds(v, list);
    for (Id id : list) {
        const auto* o = v.object(id);
        if (!o || v.effectivelyLocked(*o)) continue;
        const Box b = rotatedBounds(*o);
        double dx = 0, dy = 0;
        switch (mode) {
            case Align::Left:    dx = ref.x - b.x; break;
            case Align::Right:   dx = ref.right() - b.right(); break;
            case Align::Top:     dy = ref.y - b.y; break;
            case Align::Bottom:  dy = ref.bottom() - b.bottom(); break;
            case Align::CenterH: dx = ref.cx() - b.cx(); break;
            case Align::CenterV: dy = ref.cy() - b.cy(); break;
        }
        shift(v, id, dx, dy);
    }
    refreshGroupBounds(v);
}

void distribute(View& v, const std::vector<Id>& ids, bool horizontal) {
    auto list = units(v, ids);
    std::erase_if(list, [&](Id id) { const auto* o = v.object(id); return !o || v.effectivelyLocked(*o); });
    if (list.size() < 3) return;
    std::sort(list.begin(), list.end(), [&](Id a, Id b) {
        const Box ba = rotatedBounds(*v.object(a)), bb = rotatedBounds(*v.object(b));
        return horizontal ? ba.cx() < bb.cx() : ba.cy() < bb.cy();
    });
    const Box first = rotatedBounds(*v.object(list.front()));
    const Box last = rotatedBounds(*v.object(list.back()));
    double sizes = 0;
    for (Id id : list) { const Box b = rotatedBounds(*v.object(id)); sizes += horizontal ? b.w : b.h; }
    const double span = horizontal ? last.right() - first.x : last.bottom() - first.y;
    const double gap = (span - sizes) / static_cast<double>(list.size() - 1);
    double cursor = horizontal ? first.x : first.y;
    for (Id id : list) {
        const Box b = rotatedBounds(*v.object(id));
        const double d = cursor - (horizontal ? b.x : b.y);
        shift(v, id, horizontal ? d : 0, horizontal ? 0 : d);
        cursor += (horizontal ? b.w : b.h) + gap;
    }
    refreshGroupBounds(v);
}

void space(View& v, const std::vector<Id>& ids, bool horizontal, double gap) {
    auto list = units(v, ids);
    std::erase_if(list, [&](Id id) { const auto* o = v.object(id); return !o || v.effectivelyLocked(*o); });
    if (list.size() < 2) return;
    std::sort(list.begin(), list.end(), [&](Id a, Id b) {
        const Box ba = rotatedBounds(*v.object(a)), bb = rotatedBounds(*v.object(b));
        return horizontal ? ba.x < bb.x : ba.y < bb.y;
    });
    const Box first = rotatedBounds(*v.object(list.front()));
    double cursor = horizontal ? first.x : first.y;
    for (Id id : list) {
        const Box b = rotatedBounds(*v.object(id));
        const double d = cursor - (horizontal ? b.x : b.y);
        shift(v, id, horizontal ? d : 0, horizontal ? 0 : d);
        cursor += (horizontal ? b.w : b.h) + gap;
    }
    refreshGroupBounds(v);
}

void zorder(View& v, const std::vector<Id>& ids, ZMove how) {
    const auto list = units(v, ids);
    if (list.empty()) return;
    const auto* firstObj = v.object(list.front());
    if (!firstObj) return;
    const Id layer = firstObj->layer;
    const Id level = firstObj->parent;

    // Les unites du niveau : chaque objet de ce parent dans ce calque, avec
    // ses descendants. Les autres objets gardent leur place.
    const auto slots = slotsOfLayer(v, layer);
    std::vector<std::vector<Object>> unitsAtLevel;
    std::vector<bool> selected;
    std::vector<int> usedSlots;
    std::set<Id> taken;
    for (int s : slots) {
        const Object& o = v.objects[static_cast<std::size_t>(s)];
        if (o.parent != level || taken.count(o.id)) continue;
        std::vector<Object> unit{o};
        taken.insert(o.id);
        for (Id d : v.descendantsOf(o.id)) {
            taken.insert(d);
        }
        unitsAtLevel.push_back(std::move(unit));
        selected.push_back(contains(list, o.id));
    }
    // Les descendants, dans l'ordre du vecteur, rattaches a leur unite.
    for (auto& u : unitsAtLevel) {
        const auto desc = v.descendantsOf(u.front().id);
        std::set<Id> ds(desc.begin(), desc.end());
        for (int s : slots) {
            const Object& o = v.objects[static_cast<std::size_t>(s)];
            if (ds.count(o.id)) u.push_back(o);
        }
    }
    for (int s : slots) if (taken.count(v.objects[static_cast<std::size_t>(s)].id)) usedSlots.push_back(s);
    if (unitsAtLevel.size() < 2) return;

    std::vector<std::size_t> order(unitsAtLevel.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    switch (how) {
        case ZMove::Front:
            std::stable_partition(order.begin(), order.end(), [&](std::size_t i) { return !selected[i]; });
            break;
        case ZMove::Back:
            std::stable_partition(order.begin(), order.end(), [&](std::size_t i) { return selected[i]; });
            break;
        case ZMove::Forward:
            for (std::size_t k = order.size() - 1; k-- > 0;)
                if (selected[order[k]] && !selected[order[k + 1]]) std::swap(order[k], order[k + 1]);
            break;
        case ZMove::Backward:
            for (std::size_t k = 1; k < order.size(); ++k)
                if (selected[order[k]] && !selected[order[k - 1]]) std::swap(order[k], order[k - 1]);
            break;
    }
    std::vector<Object> flat;
    for (std::size_t i : order) for (auto& o : unitsAtLevel[i]) flat.push_back(o);
    std::sort(usedSlots.begin(), usedSlots.end());
    for (std::size_t k = 0; k < usedSlots.size() && k < flat.size(); ++k)
        v.objects[static_cast<std::size_t>(usedSlots[k])] = flat[k];
}

// ------------------------------------------------------------ groupes ------
Id group(Project& p, View& v, const std::vector<Id>& ids) {
    auto list = units(v, ids);
    std::erase_if(list, [&](Id id) { const auto* o = v.object(id); return !o || v.effectivelyLocked(*o); });
    if (list.size() < 2) return kNoId;

    // Le calque et la place : ceux du membre le plus haut.
    const auto order = v.paintOrder();
    const Object* top = nullptr;
    for (const auto* o : order) if (contains(list, o->id)) top = o;
    const Id layer = top->layer;
    Id parent = v.object(list.front())->parent;
    for (Id id : list) if (v.object(id)->parent != parent) { parent = kNoId; break; }

    Object g = makeObject(Kind::Group, p.allocate(), uniqueObjectName(v, "Groupe"), 0, 0, layer);
    g.parent = parent;
    g.setBox(selectionBounds(v, list));

    // Le groupe et ses membres deviennent contigus, a la place du plus haut.
    std::vector<Id> members;
    for (Id id : list) {
        members.push_back(id);
        for (Id d : v.descendantsOf(id)) members.push_back(d);
    }
    std::vector<Object> moved;
    for (const auto* o : order)
        if (contains(members, o->id)) moved.push_back(*o);
    const int anchor = v.indexOf(top->id);
    std::vector<Object> rest;
    int insertAt = 0;
    for (int i = 0; i < static_cast<int>(v.objects.size()); ++i) {
        const auto& o = v.objects[static_cast<std::size_t>(i)];
        if (contains(members, o.id)) {
            if (i <= anchor) insertAt = static_cast<int>(rest.size());
            continue;
        }
        rest.push_back(o);
    }
    const Id gid = g.id;
    std::vector<Object> block{g};
    for (auto& m : moved) {
        if (contains(list, m.id)) m.parent = gid;
        m.layer = layer;
        block.push_back(m);
    }
    rest.insert(rest.begin() + insertAt, block.begin(), block.end());
    v.objects = std::move(rest);
    return gid;
}

std::vector<Id> ungroup(View& v, Id gid) {
    const auto* g = v.object(gid);
    if (!g || g->kind != Kind::Group) return {};
    const Id parent = g->parent;
    auto children = v.childrenOf(gid);
    for (Id c : children) v.object(c)->parent = parent;
    std::erase_if(v.objects, [&](const Object& o) { return o.id == gid; });
    refreshGroupBounds(v);
    return children;
}

void refreshGroupBounds(View& v) {
    // Du plus profond au plus haut : un groupe dans un groupe se recalcule
    // avant son parent.
    std::vector<std::pair<int, Id>> groups;
    for (const auto& o : v.objects) {
        if (o.kind != Kind::Group) continue;
        int depth = 0;
        for (Id p = o.parent; p != kNoId && depth < 64; ++depth) {
            const auto* po = v.object(p);
            p = po ? po->parent : kNoId;
        }
        groups.emplace_back(depth, o.id);
    }
    std::sort(groups.begin(), groups.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (const auto& [depth, gid] : groups) {
        auto* g = v.object(gid);
        if (!g || std::fabs(g->rotation()) > 1e-9) continue;   // un groupe tourne garde son cadre
        const auto kids = v.childrenOf(gid);
        if (kids.empty()) continue;
        bool first = true;
        Box b;
        for (Id k : kids) {
            const Box kb = rotatedBounds(*v.object(k));
            b = first ? kb : b.united(kb);
            first = false;
        }
        g->setBox(b);
    }
}

void setLocked(View& v, const std::vector<Id>& ids, bool on) {
    for (Id id : ids) if (auto* o = v.object(id)) o->locked = on;
}
void setHidden(View& v, const std::vector<Id>& ids, bool on) {
    for (Id id : ids) if (auto* o = v.object(id)) o->hidden = on;
}

// ------------------------------------------------------------ calques ------
Id addLayer(Project& p, View& v, std::string name) {
    Layer l;
    l.id = p.allocate();
    l.name = name.empty() ? uniqueLayerName(v, "Calque") : std::move(name);
    const int rank = v.layerRank(v.activeLayer);
    const auto at = rank < 0 ? v.layers.end() : v.layers.begin() + rank + 1;
    v.layers.insert(at, l);
    v.activeLayer = l.id;
    return l.id;
}

bool removeLayer(View& v, Id layer, std::string* error) {
    const int rank = v.layerRank(layer);
    if (rank < 0) { if (error) *error = "calque introuvable"; return false; }
    if (v.layers.size() == 1) { if (error) *error = "une vue garde au moins un calque"; return false; }
    // Ses objets descendent d'un calque (ou montent, s'il etait tout en bas) :
    // supprimer un calque ne supprime pas ce qu'on y a dessine.
    const Id target = rank > 0 ? v.layers[static_cast<std::size_t>(rank - 1)].id
                               : v.layers[1].id;
    for (auto& o : v.objects) if (o.layer == layer) o.layer = target;
    v.layers.erase(v.layers.begin() + rank);
    if (v.activeLayer == layer) v.activeLayer = target;
    return true;
}

bool renameLayer(View& v, Id layer, const std::string& name, std::string* error) {
    auto* l = v.layer(layer);
    if (!l) { if (error) *error = "calque introuvable"; return false; }
    if (name.empty()) { if (error) *error = "un calque a un nom"; return false; }
    for (const auto& o : v.layers)
        if (o.id != layer && o.name == name) { if (error) *error = "'" + name + "' existe d\xC3\xA9j\xC3\xA0"; return false; }
    l->name = name;
    return true;
}

void moveLayer(View& v, Id layer, int delta) {
    const int rank = v.layerRank(layer);
    if (rank < 0) return;
    const int to = std::clamp(rank + delta, 0, static_cast<int>(v.layers.size()) - 1);
    if (to == rank) return;
    Layer l = v.layers[static_cast<std::size_t>(rank)];
    v.layers.erase(v.layers.begin() + rank);
    v.layers.insert(v.layers.begin() + to, l);
}

void setLayerVisible(View& v, Id layer, bool on) { if (auto* l = v.layer(layer)) l->visible = on; }
void setLayerLocked(View& v, Id layer, bool on)  { if (auto* l = v.layer(layer)) l->locked = on; }

void moveToLayer(View& v, const std::vector<Id>& ids, Id layer) {
    if (!v.layer(layer)) return;
    std::vector<Id> all;
    for (Id id : units(v, ids)) {
        all.push_back(id);
        for (Id d : v.descendantsOf(id)) all.push_back(d);
    }
    // En haut du calque d'arrivee, dans l'ordre ou ils etaient.
    std::vector<Object> moved, rest;
    for (auto& o : v.objects) (contains(all, o.id) ? moved : rest).push_back(o);
    for (auto& o : moved) o.layer = layer;
    rest.insert(rest.end(), moved.begin(), moved.end());
    v.objects = std::move(rest);
}

std::vector<Id> objectsInLayer(const View& v, Id layer) {
    std::vector<Id> out;
    for (const auto& o : v.objects) if (o.layer == layer && o.parent == kNoId) out.push_back(o.id);
    return out;
}

// ------------------------------------------------------------ style --------
std::vector<Prop> copyStyle(const Object& o) {
    static const char* kStyleKeys[] = {"fill", "stroke", "strokeWidth", "radius", "opacity", "textColor",
                                       "font", "fontSize", "align", "colorOn", "colorOff", "background"};
    std::vector<Prop> out;
    for (const char* k : kStyleKeys)
        if (const auto* p = o.find(k)) out.push_back(*p);
    return out;
}

void pasteStyle(View& v, const std::vector<Id>& ids, const std::vector<Prop>& style) {
    for (Id id : ids) {
        auto* o = v.object(id);
        if (!o || v.effectivelyLocked(*o)) continue;
        for (const auto& p : style)
            if (auto* mine = o->find(p.key)) { mine->value = p.value; mine->expr = p.expr; }
    }
}

// ------------------------------------------------------------ magnetisme ---
double snapToGrid(double value, int step) noexcept {
    if (step <= 1) return value;
    return std::round(value / step) * step;
}

SnapResult snapMove(const View& v, const Box& moving, double dx, double dy, const std::vector<Id>& exclude,
                    double threshold) {
    SnapResult r;
    r.dx = dx;
    r.dy = dy;
    Box m{moving.x + dx, moving.y + dy, moving.w, moving.h};

    std::vector<double> tx, ty;
    std::vector<Box> others;                 // lot 12 : les voisins, pour l'espacement egal
    if (v.grid.snapGuides)
        for (const auto& g : v.guides) (g.vertical ? tx : ty).push_back(g.position);
    if (v.grid.snapObjects) {
        tx.insert(tx.end(), {0.0, v.width / 2.0, static_cast<double>(v.width)});
        ty.insert(ty.end(), {0.0, v.height / 2.0, static_cast<double>(v.height)});
        std::set<Id> skip;
        for (Id e : exclude) {
            skip.insert(e);
            for (Id d : v.descendantsOf(e)) skip.insert(d);
        }
        for (const auto& o : v.objects) {
            if (skip.count(o.id) || v.effectivelyHidden(o) || o.parent != kNoId) continue;
            const Box b = rotatedBounds(o);
            tx.insert(tx.end(), {b.x, b.cx(), b.right()});
            ty.insert(ty.end(), {b.y, b.cy(), b.bottom()});
            others.push_back(b);
        }
    }
    struct Candidate {
        double d{1e18}, adjust{0};
        std::vector<double> lines;
        std::vector<SpacingMark> marks;
    };
    // L'alignement : un bord ou le centre sur un repere.
    const auto align = [&](const double* mine, const std::vector<double>& targets) {
        Candidate c;
        for (int i = 0; i < 3; ++i)
            for (double t : targets) {
                const double d = std::fabs(t - mine[i]);
                if (d < c.d) { c.d = d; c.adjust = t - mine[i]; c.lines = {t}; }
            }
        return c;
    };
    // L'espacement egal sur un axe (horizontal : la rangee, les objets qui
    // chevauchent le deplace en hauteur ; vertical : la colonne).
    const auto spacing = [&](bool horizontal) {
        Candidate c;
        const auto lo = [&](const Box& b) { return horizontal ? b.x : b.y; };
        const auto hi = [&](const Box& b) { return horizontal ? b.right() : b.bottom(); };
        const auto crossLo = [&](const Box& b) { return horizontal ? b.y : b.x; };
        const auto crossHi = [&](const Box& b) { return horizontal ? b.bottom() : b.right(); };
        const auto overlapMid = [&](const Box& a, const Box& b) {
            return (std::max(crossLo(a), crossLo(b)) + std::min(crossHi(a), crossHi(b))) / 2;
        };
        std::vector<Box> line;
        for (const auto& b : others)
            if (crossLo(b) < crossHi(m) && crossHi(b) > crossLo(m)) line.push_back(b);
        if (line.empty()) return c;
        std::sort(line.begin(), line.end(), [&](const Box& a, const Box& b) { return lo(a) < lo(b); });
        const double center = horizontal ? m.cx() : m.cy(), size = horizontal ? m.w : m.h, mine = lo(m);
        const Box* before = nullptr;
        const Box* after = nullptr;
        for (const auto& b : line) {
            const double bc = horizontal ? b.cx() : b.cy();
            if (bc < center && (!before || hi(b) > hi(*before))) before = &b;
            if (bc > center && (!after || lo(b) < lo(*after))) after = &b;
        }
        const auto mark = [&](double from, double to, double at) {
            SpacingMark s;
            s.horizontal = horizontal;
            s.from = from;
            s.to = to;
            s.at = at;
            s.gap = to - from;
            return s;
        };
        const auto consider = [&](double target, std::vector<SpacingMark> marks) {
            const double d = std::fabs(target - mine);
            if (d < c.d) { c.d = d; c.adjust = target - mine; c.marks = std::move(marks); }
        };
        // Les ecarts deja pris entre deux voisins de la rangee.
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {
            const Box& a = line[i];
            const Box& b = line[i + 1];
            const double g = lo(b) - hi(a);
            if (g <= 0.5 || (&a == before && &b == after)) continue;   // l'ecart ou il se pose : pas un modele
            const SpacingMark model = mark(hi(a), lo(b), overlapMid(a, b));
            if (before) {
                Box placed = m;
                (horizontal ? placed.x : placed.y) = hi(*before) + g;
                consider(hi(*before) + g, {model, mark(hi(*before), hi(*before) + g, overlapMid(*before, placed))});
            }
            if (after) {
                Box placed = m;
                (horizontal ? placed.x : placed.y) = lo(*after) - g - size;
                consider(lo(*after) - g - size, {model, mark(lo(*after) - g, lo(*after), overlapMid(placed, *after))});
            }
        }
        // Au milieu de ses deux voisins.
        if (before && after && lo(*after) - hi(*before) > size + 1) {
            const double target = (hi(*before) + lo(*after) - size) / 2;
            Box placed = m;
            (horizontal ? placed.x : placed.y) = target;
            consider(target, {mark(hi(*before), target, overlapMid(*before, placed)),
                              mark(target + size, lo(*after), overlapMid(placed, *after))});
        }
        return c;
    };

    bool gotX = false, gotY = false;
    {
        const double mx[3] = {m.x, m.cx(), m.right()};
        const Candidate a = align(mx, tx);
        const Candidate s = v.grid.snapObjects ? spacing(true) : Candidate{};
        const Candidate& best = s.d < a.d ? s : a;
        if (best.d <= threshold) {
            r.dx += best.adjust;
            m.x += best.adjust;
            r.xLines = best.lines;
            r.spacing.insert(r.spacing.end(), best.marks.begin(), best.marks.end());
            gotX = true;
        }
    }
    {
        const double my[3] = {m.y, m.cy(), m.bottom()};
        const Candidate a = align(my, ty);
        const Candidate s = v.grid.snapObjects ? spacing(false) : Candidate{};
        const Candidate& best = s.d < a.d ? s : a;
        if (best.d <= threshold) {
            r.dy += best.adjust;
            m.y += best.adjust;
            r.yLines = best.lines;
            r.spacing.insert(r.spacing.end(), best.marks.begin(), best.marks.end());
            gotY = true;
        }
    }
    if (v.grid.snapGrid && v.grid.step > 1) {
        if (!gotX) r.dx = snapToGrid(moving.x + dx, v.grid.step) - moving.x;
        if (!gotY) r.dy = snapToGrid(moving.y + dy, v.grid.step) - moving.y;
    }
    return r;
}

} // namespace hmi::edit
