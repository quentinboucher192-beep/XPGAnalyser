// =============================================================================
//  hmi/HmiLiveGeometry.cpp - 1.11.4 : la geometrie calculee en marche
//  (voir HmiLiveGeometry.hpp)
// =============================================================================
#include "HmiLiveGeometry.hpp"
#include "HmiEdit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

namespace hmi {

namespace {

constexpr std::array<std::string_view, 7> kGeometryKeys{"x", "y", "w", "h", "rot", "flipH", "flipV"};
// Ce que la pose change sur un objet : sa geometrie, ses points, et les tailles qui
// suivent l'echelle (place() dans HmiSymbols.cpp).
constexpr std::array<std::string_view, 12> kNotedKeys{"x",      "y",        "w",           "h",      "rot",      "flipH",
                                                      "flipV",  "points",   "fontSize",    "strokeWidth", "radius", "thickness"};
constexpr std::array<std::string_view, 4> kScaledKeys{"fontSize", "strokeWidth", "radius", "thickness"};
constexpr char kSep = '\x1F';

bool sameGeometry(const Object& a, const Object& b) {
    for (const char* k : {"x", "y", "w", "h", "rot"})
        if (std::fabs(a.number(k) - b.number(k)) > 1e-9) return false;
    return a.flipH() == b.flipH() && a.flipV() == b.flipV();
}

edit::Pt pivotOf(const Object& o) {
    const Box b = o.box();
    return {b.x + b.w * o.number("pivotX", 0.5), b.y + b.h * o.number("pivotY", 0.5)};
}

void putPivot(Object& o, edit::Pt target) {
    const edit::Pt now = pivotOf(o);
    o.setNumber("x", o.number("x") + target.x - now.x);
    o.setNumber("y", o.number("y") + target.y - now.y);
}

// Le passage d'un groupe de `from` a `to` (position, taille, rotation, miroirs), applique
// a un objet qu'il contient - les regles de edit::setBox (un groupe redimensionne) et de
// edit::rotate (un groupe tourne), en un seul pas.
void follow(const Object& from, const Object& to, Object& c) {
    const Box fb = from.box(), tb = to.box();
    const double sx = fb.w > 1e-9 ? tb.w / fb.w : 1.0;
    const double sy = fb.h > 1e-9 ? tb.h / fb.h : 1.0;
    const edit::Pt local = edit::toLocal(from, pivotOf(c));
    const edit::Pt target = edit::toView(to, {local.x * sx, local.y * sy});
    const double rel = edit::normalizeAngle(c.rotation() - from.rotation());
    const bool quarter = std::fabs(std::fmod(rel, 180.0) - 90.0) < 1.0;
    const double kx = quarter ? sy : sx, ky = quarter ? sx : sy;
    if (std::fabs(kx - 1.0) > 1e-9 || std::fabs(ky - 1.0) > 1e-9) {
        const Box b = c.box();
        c.setNumber("w", b.w * kx);
        c.setNumber("h", b.h * ky);
        if (c.kind == Kind::Line || c.kind == Kind::Polygon) {
            auto pts = edit::points(c);
            for (auto& p : pts) { p.x *= kx; p.y *= ky; }
            edit::setPoints(c, pts);
        }
    }
    double rot = c.rotation() + (to.rotation() - from.rotation());
    // Un miroir du groupe qui change : l'objet se retourne aussi (comme place()).
    if (from.flipH() != to.flipH()) { c.setFlag("flipH", !c.flipH()); rot = -rot; }
    if (from.flipV() != to.flipV()) { c.setFlag("flipV", !c.flipV()); rot = -rot; }
    c.setNumber("rot", edit::normalizeAngle(rot));
    putPivot(c, target);
}

} // namespace

bool isLiveGeometryKey(std::string_view key) noexcept {
    return std::find(kGeometryKeys.begin(), kGeometryKeys.end(), key) != kGeometryKeys.end();
}

std::string encodeLocalGeometry(const Object& o) {
    std::string s;
    for (const auto k : kNotedKeys)
        if (const Prop* p = o.find(k)) {
            s.append(k);
            s += '=';
            s += p->value;
            s += kSep;
        }
    return s;
}

void restoreLocalGeometry(Object& o, std::string_view note) {
    o.props.erase(std::remove_if(o.props.begin(), o.props.end(),
                                 [](const Prop& p) { return std::find(kNotedKeys.begin(), kNotedKeys.end(), p.key) != kNotedKeys.end(); }),
                  o.props.end());
    std::size_t at = 0;
    while (at < note.size()) {
        std::size_t end = note.find(kSep, at);
        if (end == std::string_view::npos) end = note.size();
        const std::string_view item = note.substr(at, end - at);
        if (const std::size_t eq = item.find('='); eq != std::string_view::npos) o.set(item.substr(0, eq), std::string(item.substr(eq + 1)));
        at = end + 1;
    }
}

void relayoutLive(View& out, const View& source, const std::set<std::pair<Id, std::string>>& applied) {
    if (applied.empty()) return;
    const auto computed = [&](Id id, std::string_view key) { return applied.count({id, std::string(key)}) > 0; };

    // La profondeur de chaque objet (ses parents) : les parents se reposent avant leurs enfants.
    std::unordered_map<Id, int> depth;
    const auto depthOf = [&](Id id) {
        int d = 0;
        for (const Object* o = out.object(id); o && o->parent != kNoId && d < 64; o = out.object(o->parent)) ++d;
        return d;
    };
    // L'instance qui a pose un objet : son plus proche ancetre instance.
    const auto ownerOf = [&](const Object& o) -> Id {
        int guard = 0;
        for (const Object* p = out.object(o.parent); p && guard < 64; p = out.object(p->parent), ++guard)
            if (p->kind == Kind::SymbolInstance) return p->id;
        return kNoId;
    };
    std::map<Id, std::vector<Id>> owned;   // instance -> les objets de son symbole (tous niveaux)
    std::vector<Id> frames;                // les instances (avec leur taille de symbole) et les groupes
    for (const auto& o : out.objects) {
        if (o.find(kLocalGeometryKey))
            if (const Id owner = ownerOf(o); owner != kNoId) owned[owner].push_back(o.id);
        if ((o.kind == Kind::SymbolInstance && o.find(kSymbolSizeKey)) || o.kind == Kind::Group) {
            frames.push_back(o.id);
            depth[o.id] = depthOf(o.id);
        }
    }
    std::stable_sort(frames.begin(), frames.end(), [&](Id a, Id b) { return depth[a] < depth[b]; });

    // La geometrie d'un groupe AVANT sa propre formule, dans le repere du moment : la
    // statique, deplacee avec ce qui le contient.
    std::map<Id, Object> before;
    const auto beforeOf = [&](const Object& o) -> Object {
        if (const auto it = before.find(o.id); it != before.end()) return it->second;
        if (const Object* s = source.object(o.id)) return *s;
        return o;
    };

    for (const Id fid : frames) {
        Object* f = out.object(fid);
        if (!f) continue;
        if (f->kind == Kind::SymbolInstance) {
            const auto it = owned.find(fid);
            if (it == owned.end()) continue;
            const Object* s = source.object(fid);
            bool redo = !s || !sameGeometry(*s, *f);
            for (const Id d : it->second) {
                if (redo) break;
                for (const auto k : kGeometryKeys)
                    if (computed(d, k)) { redo = true; break; }
            }
            if (!redo) continue;
            double sw = 0, sh = 0;
            {
                const std::string size = f->text(kSymbolSizeKey);
                const std::size_t semi = size.find(';');
                if (semi == std::string::npos || !parseNumber(size.substr(0, semi), sw) || !parseNumber(size.substr(semi + 1), sh)) continue;
            }
            const Object inst = *f;
            for (const Id d : it->second) {
                Object* o = out.object(d);
                if (!o) continue;
                const std::string note = o->text(kLocalGeometryKey);
                Object placed = *o;
                restoreLocalGeometry(placed, note);
                // Sans sa formule : ou il serait (le depart d'un groupe qui a la sienne).
                if (o->kind == Kind::Group) {
                    Object still = placed;
                    placeInInstance(still, inst, sw, sh);
                    before[d] = std::move(still);
                }
                // Sa formule donne une valeur dans le repere du symbole.
                for (const auto k : kGeometryKeys)
                    if (computed(d, k))
                        if (const Prop* p = o->find(k)) placed.set(k, p->value);
                placeInInstance(placed, inst, sw, sh);
                for (const auto k : kNotedKeys) {
                    const bool scaled = std::find(kScaledKeys.begin(), kScaledKeys.end(), k) != kScaledKeys.end();
                    const Prop* mine = o->find(k);
                    if (scaled && mine && !mine->expr.empty()) continue;   // sa formule decide
                    if (const Prop* p = placed.find(k)) o->set(k, p->value);
                    else if (mine && (k == "flipH" || k == "flipV")) o->setFlag(k, false);   // l'instance n'est plus retournee
                    else if (mine && k == "rot") o->setNumber("rot", 0);                    // ni tournee
                }
            }
            continue;
        }
        // Un groupe : ce qu'il contient suit le passage de sa geometrie d'avant a celle du moment.
        const Object from = beforeOf(*f);
        if (sameGeometry(from, *f)) continue;
        const Object to = *f;
        for (const Id d : out.descendantsOf(fid)) {
            Object* c = out.object(d);
            if (!c) continue;
            if (c->kind == Kind::Group) {
                Object b = beforeOf(*c);
                follow(from, to, b);
                before[d] = std::move(b);
            }
            follow(from, to, *c);
        }
    }
}

} // namespace hmi
