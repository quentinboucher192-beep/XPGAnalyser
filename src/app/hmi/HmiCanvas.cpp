#include "HmiCanvas.hpp"
#include "../TablePaste.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "HmiIcons.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiDuplicate.hpp"   // 1.10.2 (D) : les modeles d'objets

#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace app {

using hmi::Box;
using hmi::Id;
using hmi::kNoId;
using hmi::edit::Pt;
namespace shapes = ui::shapes;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kHandle = 7.f;          // cote d'une poignee, en pixels ecran
constexpr float kRotateReach = 26.f;    // distance de la poignee de rotation

// Le presse-papiers de l'editeur : les objets, avec leurs expressions et leurs
// liaisons, et un style a part. Il vit le temps de l'application, d'une vue a
// l'autre.
struct Clipboard {
    std::vector<hmi::Object> objects;
    Id                       fromView{kNoId};
    int                      pastes{0};
    std::vector<hmi::Prop>   style;
    bool                     hasStyle{false};
    // Lot 20 : ce que la copie a mis dans le presse-papiers du systeme (la liste
    // des objets, pour Excel). S'il y est encore, Ctrl+V colle les objets ; s'il
    // a change (une copie dans Excel), Ctrl+V colle le texte : des variables.
    std::string              systemText;
};
Clipboard& clipboard() {
    static Clipboard c;
    return c;
}

std::string fmt(double v) { return hmi::formatNumber(std::round(v * 10) / 10); }

double angleOf(Pt c, Pt p) { return std::atan2(p.y - c.y, p.x - c.x) * 180.0 / kPi; }

} // namespace

HmiCanvas::HmiCanvas(std::string id, hmi::DocumentPtr doc, Id view, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), viewId_(view), apply_(std::move(apply)) {
    setFocusPolicy(true);
}

ui::SizeHint HmiCanvas::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {640.f, 400.f};
    h.minimum = {160.f, 120.f};
    h.stretchX = 1.f;
    h.stretchY = 1.f;
    return h;
}

const hmi::View* HmiCanvas::view() const { return doc_ ? doc_->project.view(viewId_) : nullptr; }

HmiViewport HmiCanvas::viewport() const noexcept {
    const auto r = contentRect();
    return {r.x + panX_, r.y + panY_, zoom_};
}

Pt HmiCanvas::toView(gfx::Point s) const {
    const auto vp = viewport();
    return {vp.toViewX(s.x), vp.toViewY(s.y)};
}

// ---------------------------------------------------------------- selection -
void HmiCanvas::setSelection(std::vector<Id> ids, bool notify) {
    std::vector<Id> clean;
    if (const auto* v = view())
        for (Id id : ids)
            if (v->object(id) && std::find(clean.begin(), clean.end(), id) == clean.end()) clean.push_back(id);
    if (clean == selection_) return;
    selection_ = std::move(clean);
    invalidate();
    if (notify) selectionChanged->emit();
}

void HmiCanvas::selectAll() {
    const auto* v = view();
    if (!v) return;
    std::vector<Id> all;
    for (const auto* o : v->paintOrder())
        if (o->parent == insideGroup_ && !v->effectivelyHidden(*o) && !v->effectivelyLocked(*o)) all.push_back(o->id);
    setSelection(all);
}

void HmiCanvas::enterGroup(Id g) {
    const auto* v = view();
    // Un groupe ; lot 12 : aussi un conteneur, un conteneur a onglets, un cadre, un panneau.
    if (!v || !v->object(g) || (v->object(g)->kind != hmi::Kind::Group && !hmi::kindHoldsChildren(v->object(g)->kind))) return;
    insideGroup_ = g;
    setSelection({});
    invalidate();
}

void HmiCanvas::leaveGroup() {
    if (insideGroup_ == kNoId) return;
    const Id was = insideGroup_;
    const auto* v = view();
    insideGroup_ = kNoId;
    if (v) if (const auto* g = v->object(was)) insideGroup_ = g->parent;
    setSelection({was});
}

void HmiCanvas::setPlaceKind(std::optional<hmi::Kind> kind) {
    placeKind_ = kind;
    placeSymbol_.clear();
    placeVariable_.reset();
    invalidate();
}

void HmiCanvas::setPlaceSymbol(std::string symbol) {
    placeSymbol_ = std::move(symbol);
    placeKind_ = placeSymbol_.empty() ? std::nullopt : std::optional<hmi::Kind>(hmi::Kind::SymbolInstance);
    placeVariable_.reset();
    invalidate();
}

void HmiCanvas::setPlaceVariable(std::optional<hmi::design::VarInfo> variable) {
    placeSymbol_.clear();
    placeVariable_ = std::move(variable);
    placeKind_ = placeVariable_ ? std::optional<hmi::Kind>(hmi::design::kindForVariable(*placeVariable_)) : std::nullopt;
    invalidate();
}

void HmiCanvas::documentChanged() {
    const auto* v = view();
    if (!v) { selection_.clear(); invalidate(); return; }
    if (insideGroup_ != kNoId && !v->object(insideGroup_)) insideGroup_ = kNoId;
    std::vector<Id> keep;
    for (Id id : selection_) if (v->object(id)) keep.push_back(id);
    // 1.11 (R111) : les copies de Dupliquer reviennent (Ctrl+Y) : la selection aussi.
    if (!copies_.empty()) {
        const bool here = std::all_of(copies_.begin(), copies_.end(), [&](Id id) { return v->object(id) != nullptr; });
        if (here && !copiesHere_) keep = copies_;
        copiesHere_ = here;
    }
    if (keep != selection_) {
        selection_ = std::move(keep);
        selectionChanged->emit();
    }
    if (hover_ != kNoId && !v->object(hover_)) hover_ = kNoId;
    invalidate();
}

// -------------------------------------------------------------------- zoom --
void HmiCanvas::setZoom(float z) { zoomBy(z / zoom_); }

void HmiCanvas::zoomBy(float factor, std::optional<gfx::Point> anchor) {
    const float nz = std::clamp(zoom_ * factor, 0.05f, 16.f);
    const auto r = contentRect();
    const gfx::Point a = anchor.value_or(gfx::Point{r.x + r.w / 2, r.y + r.h / 2});
    // Le point sous l'ancre reste sous l'ancre.
    const double vx = (a.x - r.x - panX_) / zoom_, vy = (a.y - r.y - panY_) / zoom_;
    zoom_ = nz;
    panX_ = static_cast<float>(a.x - r.x - vx * zoom_);
    panY_ = static_cast<float>(a.y - r.y - vy * zoom_);
    invalidate();
    if (pointerIn_) emitStatus(pointer_);
}

void HmiCanvas::scrollTo(double x, double y, float margin) {
    panX_ = margin - static_cast<float>(x) * zoom_;
    panY_ = margin - static_cast<float>(y) * zoom_;
    fitted_ = true;
    invalidate();
    if (pointerIn_) emitStatus(pointer_);
}

void HmiCanvas::zoomToFit() {
    const auto* v = view();
    const auto r = contentRect();
    if (!v || r.w < 10 || r.h < 10 || v->width <= 0 || v->height <= 0) return;
    const float margin = 32.f;
    zoom_ = std::clamp(std::min((r.w - 2 * margin) / static_cast<float>(v->width),
                                (r.h - 2 * margin) / static_cast<float>(v->height)), 0.05f, 16.f);
    panX_ = (r.w - static_cast<float>(v->width) * zoom_) / 2;
    panY_ = (r.h - static_cast<float>(v->height) * zoom_) / 2;
    fitted_ = true;
    invalidate();
}

void HmiCanvas::onLayout() {
    if (!fitted_) zoomToFit();
}

// ---------------------------------------------------------------- edition --
bool HmiCanvas::edit(const std::string& label, const hmi::ViewChange& fn, std::string mergeKey) {
    auto cmd = hmi::changeView(doc_, viewId_, label, fn, std::move(mergeKey));
    if (!cmd) return false;
    apply_(std::move(cmd));
    documentChanged();
    return true;
}

void HmiCanvas::deleteSelection() {
    if (selection_.empty()) return;
    const auto sel = selection_;
    edit("Supprimer", [&](hmi::Project&, hmi::View& v) { hmi::edit::remove(v, sel); });
    setSelection({});
}

void HmiCanvas::duplicateSelection(double offset) {
    if (selection_.empty()) return;
    const auto sel = selection_;
    std::vector<Id> made;
    edit("Dupliquer", [&](hmi::Project& p, hmi::View& v) { made = hmi::edit::duplicate(p, v, sel, offset, offset); });
    selectCopies(made);
}

void HmiCanvas::selectCopies(std::vector<Id> ids) {
    setSelection(ids);
    copies_ = std::move(ids);
    copiesHere_ = !copies_.empty();
}

void HmiCanvas::copySelection() {
    const auto* v = view();
    if (!v || selection_.empty()) return;
    auto& c = clipboard();
    c.objects.clear();
    c.fromView = viewId_;
    c.pastes = 0;
    std::set<Id> wanted;
    for (Id id : hmi::edit::units(*v, selection_)) {
        wanted.insert(id);
        for (Id d : v->descendantsOf(id)) wanted.insert(d);
    }
    for (const auto& o : v->objects) if (wanted.count(o.id)) c.objects.push_back(o);
    // Lot 20 : pour Excel, la liste des objets copies (nom, genre, place, taille,
    // ce qu'ils montrent) ; collee dans une vue, elle redonne les objets.
    std::string list = "Nom\tGenre\tX\tY\tLargeur\tHauteur\tValeur\r\n";
    const auto num = [](double d) { return hmi::formatNumber(std::round(d * 10) / 10); };
    for (const auto& o : c.objects) {
        if (o.parent != kNoId && wanted.count(o.parent)) continue;      // le contenu d'un groupe : le groupe suffit
        std::string value = o.expr("value");
        if (value.empty()) value = o.text("text");
        for (auto& ch : value) if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
        list += o.name + "\t" + std::string(hmi::kindLabel(o.kind)) + "\t" + num(o.number("x")) + "\t" + num(o.number("y")) + "\t"
              + num(o.number("w")) + "\t" + num(o.number("h")) + "\t" + value + "\r\n";
    }
    c.systemText = list;
    ui::setClipboardText(list);
}

void HmiCanvas::cutSelection() {
    copySelection();
    const auto sel = selection_;
    edit("Couper", [&](hmi::Project&, hmi::View& v) { hmi::edit::remove(v, sel); });
    setSelection({});
}

void HmiCanvas::paste() {
    auto& c = clipboard();
    // Lot 20 : le presse-papiers du systeme a change depuis la derniere copie
    // d'objets (une colonne copiee dans Excel) : ce sont des variables.
    if (const std::string sys = ui::clipboardText(); !sys.empty() && sys != c.systemText) {
        if (pasteNames(sys) > 0) return;
        if (c.objects.empty()) {
            status->emit("Coller : le presse-papiers ne contient ni objets ni noms de variables");
            return;
        }
    }
    if (c.objects.empty()) return;
    const bool sameView = c.fromView == viewId_;
    const double off = sameView ? 20.0 * (c.pastes + 1) : 0.0;
    std::vector<Id> top;
    const bool done = edit("Coller", [&](hmi::Project& p, hmi::View& v) {
        std::map<Id, Id> ids;
        for (const auto& o : c.objects) ids[o.id] = p.allocate();
        for (auto o : c.objects) {
            const bool isTop = !ids.count(o.parent);
            o.id = ids[o.id];
            o.parent = isTop ? insideGroup_ : ids[o.parent];
            if (!v.layer(o.layer)) o.layer = v.activeLayer;
            o.locked = false;
            std::string base = o.name;
            while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
            if (!base.empty() && base.back() == '_') base.pop_back();
            o.name = hmi::uniqueObjectName(v, base.empty() ? "Objet" : base);
            o.setNumber("x", o.number("x") + off);
            o.setNumber("y", o.number("y") + off);
            if (isTop) top.push_back(o.id);
            v.objects.push_back(std::move(o));
        }
        hmi::edit::refreshGroupBounds(v);
    });
    if (done) {
        ++c.pastes;
        setSelection(top);
    }
}

// ---- lot 20 : une colonne de noms devient des etiquettes et des afficheurs ------------
std::size_t HmiCanvas::pasteNames(const std::string& text) {
    const auto* v = view();
    if (!v) return 0;
    const auto grid = paste::parseGrid(text);
    std::vector<std::string> names;
    for (std::size_t i = 0; i < grid.size(); ++i) {
        if (grid[i].empty()) continue;
        std::string n = grid[i][0];
        while (!n.empty() && (n.back() == ' ' || n.back() == '\r')) n.pop_back();
        while (!n.empty() && n.front() == ' ') n.erase(0, 1);
        if (n.empty()) continue;
        // La ligne de titres d'un tableau copie (Nom, Variable...) ne se pose pas.
        const std::string title = paste::normalizedTitle(n);
        if (i == 0 && (title == "nom" || title == "name" || title == "variable" || title == "variables" || title == "tag"
                       || title == "mnemonique" || title == "symbole"))
            continue;
        // Un nom de variable : lettres, chiffres, _ . [ ] (un chemin).
        const bool path = std::all_of(n.begin(), n.end(), [](char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == '[' || ch == ']';
        });
        if (!path || std::isdigit(static_cast<unsigned char>(n.front()))) continue;
        names.push_back(n);
    }
    if (names.empty()) return 0;
    std::vector<hmi::design::VarInfo> known;
    if (variableSource_) known = variableSource_();
    const auto same = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        return true;
    };
    // Depuis le dernier clic (au pas de la grille), dans la vue.
    constexpr double kRow = 64.0, kLabelW = 170.0;
    double x0 = std::clamp(pressView_.x, 0.0, std::max(0.0, static_cast<double>(v->width) - 400.0));
    double y0 = std::clamp(pressView_.y, 0.0, std::max(0.0, static_cast<double>(v->height) - kRow));
    if (v->grid.snapGrid) {
        x0 = hmi::edit::snapToGrid(x0, v->grid.step);
        y0 = hmi::edit::snapToGrid(y0, v->grid.step);
    }
    std::vector<Id> made;
    std::size_t unknown = 0;
    const Id vid = viewId_;
    const std::string label = "Coller depuis Excel : " + std::to_string(names.size()) + (names.size() == 1 ? " variable" : " variables");
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            hmi::design::VarInfo info;
            bool found = false;
            for (const auto& k : known)
                if (same(k.name, names[i])) { info = k; found = true; break; }
            if (!found) {
                info.name = names[i];
                info.type = "REAL";
                info.plc = !p.variable(names[i]);
                if (const auto* hv = p.variable(names[i])) { info.type = hv->type; info.plc = false; found = true; }
            }
            if (!found) ++unknown;
            const double y = y0 + static_cast<double>(i) * kRow;
            hmi::View* vv = p.view(vid);
            if (!vv) return;
            const Id lab = hmi::edit::add(p, *vv, hmi::Kind::Text, x0, y + 12.0);
            if (auto* o = vv->object(lab)) {
                o->name = hmi::uniqueObjectName(*vv, "Etiquette_" + hmi::design::nameStem(names[i]));
                o->set("text", names[i]);
                o->setNumber("w", kLabelW - 10.0);
                made.push_back(lab);
            }
            const Id d = hmi::design::placeVariable(p, vid, info, x0 + kLabelW, y);
            if (d != kNoId) {
                made.push_back(d);
                // L'etiquette dit deja le nom : l'afficheur n'en repete pas un.
                if (hmi::View* v2 = p.view(vid))
                    if (auto* o = v2->object(d); o && o->kind == hmi::Kind::NumericDisplay) o->set("label", "");
            }
        }
    });
    if (!cmd) return 0;
    apply_(std::move(cmd));
    documentChanged();
    setSelection(made);
    std::string msg = "Coll\xC3\xA9 depuis Excel : " + std::to_string(names.size()) + " variable(s), une \xC3\xA9tiquette et "
                      "un afficheur chacune, depuis (" + hmi::formatNumber(x0) + ", " + hmi::formatNumber(y0) + ")";
    if (unknown > 0) msg += " \xE2\x80\x94 " + std::to_string(unknown) + " inconnue(s) du projet (G\xC3\xA9n\xC3\xA9rer les signalera)";
    status->emit(msg + ". Ctrl+Z pour tout annuler.");
    paste::notify(msg + " \xE2\x80\x94 Ctrl+Z pour tout annuler");
    return names.size();
}

void HmiCanvas::copyStyle() {
    const auto* v = view();
    if (!v || selection_.empty()) return;
    if (const auto* o = v->object(selection_.front())) {
        clipboard().style = hmi::edit::copyStyle(*o);
        clipboard().hasStyle = true;
        status->emit("Style copi\xC3\xA9 depuis " + o->name);
    }
}

void HmiCanvas::pasteStyle() {
    if (!clipboard().hasStyle || selection_.empty()) return;
    const auto sel = selection_;
    edit("Coller le style", [&](hmi::Project&, hmi::View& v) { hmi::edit::pasteStyle(v, sel, clipboard().style); });
}

void HmiCanvas::groupSelection() {
    if (selection_.size() < 2) return;
    const auto sel = selection_;
    Id g = kNoId;
    edit("Grouper", [&](hmi::Project& p, hmi::View& v) { g = hmi::edit::group(p, v, sel); });
    if (g != kNoId) setSelection({g});
}

void HmiCanvas::ungroupSelection() {
    const auto* v = view();
    if (!v) return;
    std::vector<Id> groups;
    for (Id id : selection_) if (const auto* o = v->object(id); o && o->kind == hmi::Kind::Group) groups.push_back(id);
    if (groups.empty()) return;
    std::vector<Id> freed;
    edit("D\xC3\xA9grouper", [&](hmi::Project&, hmi::View& vv) {
        for (Id g : groups) for (Id c : hmi::edit::ungroup(vv, g)) freed.push_back(c);
    });
    setSelection(freed);
}

void HmiCanvas::align(hmi::edit::Align a) {
    const auto sel = selection_;
    edit("Aligner", [&](hmi::Project&, hmi::View& v) { hmi::edit::align(v, sel, a); });
}
void HmiCanvas::distribute(bool h) {
    const auto sel = selection_;
    edit("Distribuer", [&](hmi::Project&, hmi::View& v) { hmi::edit::distribute(v, sel, h); });
}
void HmiCanvas::zorder(hmi::edit::ZMove m) {
    const auto sel = selection_;
    edit("Ordre", [&](hmi::Project&, hmi::View& v) { hmi::edit::zorder(v, sel, m); });
}
void HmiCanvas::rotateSelection(double delta) {
    const auto sel = selection_;
    edit("Rotation", [&](hmi::Project&, hmi::View& v) { hmi::edit::rotate(v, sel, delta, sel.size() > 1); });
}
void HmiCanvas::mirrorSelection(bool h) {
    const auto sel = selection_;
    edit(h ? "Miroir horizontal" : "Miroir vertical",
         [&](hmi::Project&, hmi::View& v) { hmi::edit::mirror(v, sel, h); });
}
void HmiCanvas::lockSelection(bool on) {
    const auto sel = selection_;
    edit(on ? "Verrouiller" : "D\xC3\xA9verrouiller", [&](hmi::Project&, hmi::View& v) { hmi::edit::setLocked(v, sel, on); });
}
void HmiCanvas::hideSelection(bool on) {
    const auto sel = selection_;
    edit(on ? "Cacher" : "Montrer", [&](hmi::Project&, hmi::View& v) { hmi::edit::setHidden(v, sel, on); });
}
void HmiCanvas::nudge(double dx, double dy) {
    const auto sel = selection_;
    edit("D\xC3\xA9placer", [&](hmi::Project&, hmi::View& v) { hmi::edit::move(v, sel, dx, dy); }, "nudge");
}
void HmiCanvas::addGuide(bool vertical) {
    edit(vertical ? "Guide vertical" : "Guide horizontal", [&](hmi::Project&, hmi::View& v) {
        v.guides.push_back({vertical, vertical ? v.width / 2.0 : v.height / 2.0});
    });
}
void HmiCanvas::toggleGrid() {
    edit("Grille", [&](hmi::Project&, hmi::View& v) { v.grid.visible = !v.grid.visible; });
}
void HmiCanvas::toggleSnap() {
    edit("Magn\xC3\xA9tisme", [&](hmi::Project&, hmi::View& v) {
        const bool on = !(v.grid.snapGrid || v.grid.snapObjects || v.grid.snapGuides);
        v.grid.snapGrid = v.grid.snapObjects = v.grid.snapGuides = on;
    });
}

// ---------------------------------------------------------------- poignees -
bool HmiCanvas::singleTransformable() const {
    const auto* v = view();
    if (!v) return false;
    const auto u = hmi::edit::units(*v, selection_);
    if (u.size() != 1) return false;
    const auto* o = v->object(u.front());
    return o && !v->effectivelyLocked(*o);
}

std::vector<gfx::Point> HmiCanvas::handlePoints() const {
    std::vector<gfx::Point> out;
    const auto* v = view();
    if (!v || selection_.empty()) return out;
    const auto vp = viewport();
    const auto units = hmi::edit::units(*v, selection_);
    bool anyUnlocked = false;
    for (Id id : units) if (const auto* o = v->object(id); o && !v->effectivelyLocked(*o)) anyUnlocked = true;
    if (!anyUnlocked) return out;
    if (singleTransformable()) {
        const auto& o = *v->object(units.front());
        const Box b = o.box();
        const Pt local[8] = {{0, 0}, {b.w / 2, 0}, {b.w, 0}, {b.w, b.h / 2}, {b.w, b.h}, {b.w / 2, b.h}, {0, b.h}, {0, b.h / 2}};
        for (const auto& l : local) {
            const Pt p = hmi::edit::toView(o, l);
            out.push_back(vp.toScreen(p.x, p.y));
        }
        // La poignee de rotation, au-dessus du milieu du bord haut, dans le
        // repere tourne de l'objet.
        const Pt top = hmi::edit::toView(o, {b.w / 2, 0});
        const Pt topOut = hmi::edit::toView(o, {b.w / 2, o.flipV() ? b.h : 0.0});
        (void)topOut;
        const Pt centre = hmi::edit::toView(o, {b.w / 2, b.h / 2});
        const double dx = top.x - centre.x, dy = top.y - centre.y;
        const double len = std::max(1e-9, std::sqrt(dx * dx + dy * dy));
        const double reach = kRotateReach / zoom_;
        out.push_back(vp.toScreen(top.x + dx / len * reach, top.y + dy / len * reach));
        return out;
    }
    const Box b = hmi::edit::selectionBounds(*v, units);
    const Pt pts[8] = {{b.x, b.y}, {b.cx(), b.y}, {b.right(), b.y}, {b.right(), b.cy()},
                       {b.right(), b.bottom()}, {b.cx(), b.bottom()}, {b.x, b.bottom()}, {b.x, b.cy()}};
    for (const auto& p : pts) out.push_back(vp.toScreen(p.x, p.y));
    out.push_back(vp.toScreen(b.cx(), b.y - kRotateReach / zoom_));
    return out;
}

int HmiCanvas::handleAt(gfx::Point s) const {
    const auto pts = handlePoints();
    for (int i = static_cast<int>(pts.size()) - 1; i >= 0; --i) {
        const float reach = i == 8 ? 8.f : kHandle / 2 + 3.f;
        if (std::fabs(pts[static_cast<std::size_t>(i)].x - s.x) <= reach
            && std::fabs(pts[static_cast<std::size_t>(i)].y - s.y) <= reach) return i;
    }
    return -1;
}

int HmiCanvas::guideAt(gfx::Point s) const {
    const auto* v = view();
    if (!v) return -1;
    const auto vp = viewport();
    for (std::size_t i = 0; i < v->guides.size(); ++i) {
        const auto& g = v->guides[i];
        const gfx::Point p = vp.toScreen(g.vertical ? g.position : 0, g.vertical ? 0 : g.position);
        if (g.vertical ? std::fabs(p.x - s.x) <= 3.f : std::fabs(p.y - s.y) <= 3.f) return static_cast<int>(i);
    }
    return -1;
}

void HmiCanvas::emitStatus(gfx::Point s) {
    const auto* v = view();
    if (!v) return;
    const Pt p = toView(s);
    std::string text = "x " + fmt(p.x) + "   y " + fmt(p.y) + "   zoom " + fmt(zoom_ * 100) + " %";
    if (selection_.size() == 1) {
        if (const auto* o = v->object(selection_.front())) {
            const Box b = o->box();
            text += "   |   " + o->name + " (" + std::string(hmi::kindLabel(o->kind)) + ")  x " + fmt(b.x)
                  + "  y " + fmt(b.y) + "  l " + fmt(b.w) + "  h " + fmt(b.h) + "  r " + fmt(o->rotation()) + "\xC2\xB0";
            if (v->effectivelyLocked(*o)) text += "  [verrouill\xC3\xA9]";
        }
    } else if (selection_.size() > 1) {
        text += "   |   " + std::to_string(selection_.size()) + " objets";
    }
    if (insideGroup_ != kNoId) if (const auto* g = v->object(insideGroup_)) text += "   |   dans " + g->name;
    if (placeKind_)
        text += placeSymbol_.empty() ? "   |   clic : poser un(e) " + std::string(hmi::kindLabel(*placeKind_))
                                     : "   |   clic : poser une instance de " + placeSymbol_;
    status->emit(text);
}

// ------------------------------------------------------------------ dessin -
void HmiCanvas::paintGrid(const ui::PaintContext& ctx, const HmiViewport& vp, const hmi::View& v) {
    if (!v.grid.visible || v.grid.step <= 1) return;
    const float step = static_cast<float>(v.grid.step) * vp.zoom;
    if (step < 5.f) return;
    const auto base = ctx.theme.color.text;
    const gfx::Color minor = base.withAlpha(22), major = base.withAlpha(46);
    const gfx::Point a = vp.toScreen(0, 0);
    const float w = static_cast<float>(v.width) * vp.zoom, h = static_cast<float>(v.height) * vp.zoom;
    const auto clip = ctx.clip;
    for (int k = 0; static_cast<float>(k) * step <= w; ++k) {
        const float x = a.x + static_cast<float>(k) * step;
        if (x < clip.x - 1 || x > clip.x + clip.w + 1) continue;
        ctx.r.fillRect({x, a.y, 1, h}, k % 5 == 0 ? major : minor);
    }
    for (int k = 0; static_cast<float>(k) * step <= h; ++k) {
        const float y = a.y + static_cast<float>(k) * step;
        if (y < clip.y - 1 || y > clip.y + clip.h + 1) continue;
        ctx.r.fillRect({a.x, y, w, 1}, k % 5 == 0 ? major : minor);
    }
}

void HmiCanvas::paintSelection(const ui::PaintContext& ctx, const HmiViewport& vp) {
    const auto* v = view();
    if (!v) return;
    const auto accent = ctx.theme.color.accent;
    auto outline = [&](const hmi::Object& o, gfx::Color c, float t) {
        std::vector<gfx::Point> pts;
        for (const auto& p : hmi::edit::corners(o)) pts.push_back(vp.toScreen(p.x, p.y));
        shapes::strokePolyline(ctx.r, pts, true, c, t);
    };
    // Le groupe en edition interne : un cadre pointille.
    if (insideGroup_ != kNoId)
        if (const auto* g = v->object(insideGroup_)) {
            const Box b = hmi::edit::rotatedBounds(*g);
            const gfx::Point a = vp.toScreen(b.x, b.y), c = vp.toScreen(b.right(), b.bottom());
            for (float x = a.x; x < c.x; x += 8) {
                ctx.r.fillRect({x, a.y, 4, 1}, accent);
                ctx.r.fillRect({x, c.y, 4, 1}, accent);
            }
            for (float y = a.y; y < c.y; y += 8) {
                ctx.r.fillRect({a.x, y, 1, 4}, accent);
                ctx.r.fillRect({c.x, y, 1, 4}, accent);
            }
        }
    if (hover_ != kNoId && std::find(selection_.begin(), selection_.end(), hover_) == selection_.end())
        if (const auto* h = v->object(hover_)) outline(*h, accent.withAlpha(150), 1.f);

    for (Id id : selection_) {
        const auto* o = v->object(id);
        if (!o) continue;
        const bool locked = v->effectivelyLocked(*o);
        outline(*o, locked ? ctx.theme.color.textMuted : accent, 1.5f);
        if (o->kind == hmi::Kind::Group)
            for (Id c : v->childrenOf(id))
                if (const auto* co = v->object(c)) outline(*co, accent.withAlpha(90), 1.f);
    }
    const auto pts = handlePoints();
    if (pts.size() == 9) {
        if (selection_.size() > 1 || !singleTransformable()) {
            // Le cadre commun d'une selection multiple.
            shapes::strokePolyline(ctx.r, {pts[0], pts[2], pts[4], pts[6]}, true, accent.withAlpha(170), 1.f);
        }
        ctx.r.line(pts[1], pts[8], accent, 1.f);
        for (int i = 0; i < 8; ++i) {
            const auto& p = pts[static_cast<std::size_t>(i)];
            const gfx::Rect r{p.x - kHandle / 2, p.y - kHandle / 2, kHandle, kHandle};
            ctx.r.fillRect(r, gfx::Color{255, 255, 255, 255});
            ctx.r.strokeRect(r, accent, 1.2f);
        }
        const auto ring = shapes::ellipse(pts[8], 5.f, 5.f, 20);
        shapes::fillPolygon(ctx.r, ring, gfx::Color{255, 255, 255, 255});
        shapes::strokePolyline(ctx.r, ring, true, accent, 1.5f);
    }
}

void HmiCanvas::onPaint(const ui::PaintContext& ctx) {
    const auto r = bounds();
    ctx.r.fillRect(r, ctx.theme.color.panelBg);
    const auto* v = view();
    if (!v) {
        ctx.r.drawText({r.x + 16, r.y + 16}, "Cette vue n'existe plus.", ctx.theme.font.ui, ctx.theme.color.textMuted);
        return;
    }
    ctx.r.pushClip(r);
    const auto vp = viewport();
    const gfx::Point a = vp.toScreen(0, 0);
    const float w = static_cast<float>(v->width) * vp.zoom, h = static_cast<float>(v->height) * vp.zoom;
    ctx.r.fillRect({a.x + 3, a.y + 5, w, h}, gfx::Color{0, 0, 0, 70});   // ombre portee
    HmiPropertySource statics;
    HmiPaintOptions opt;
    opt.editor = true;
    opt.showEditorHidden = showHidden_;
    opt.insideGroup = insideGroup_;
    opt.assets = &doc_->project.assets;
    opt.project = &doc_->project;
    ctx.r.pushClip({a.x, a.y, w, h});
    // Lot 6 : ce que la vue emprunte - son ecran modele dessous, son en-tete et
    // son pied par-dessus - se dessine ESTOMPE : on le voit a sa place, on ne le
    // choisit pas ici (il se modifie dans sa propre vue).
    const auto chain = hmi::templateChain(doc_->project, *v);
    const auto* head = hmi::headerOf(doc_->project, *v);
    const auto* foot = hmi::footerOf(doc_->project, *v);
    if (chain.empty() && !head && !foot) {
        paintHmiView(ctx.r, *v, vp, statics, ctx.theme, opt);
    } else {
        const hmi::View composed = hmi::compose(doc_->project, *v);
        ctx.r.fillRect({a.x, a.y, w, h}, parseColor(v->background, gfx::Color::rgb(0x20242B)));
        HmiPaintOptions borrowed = opt;
        borrowed.alpha = 0.45f;
        for (const auto* o : composed.paintOrder())
            paintHmiObject(ctx.r, composed, *o, vp, statics, ctx.theme, v->object(o->id) ? opt : borrowed);
        // Les bandes : un cadre pointille et leur nom.
        const auto bandFrame = [&](const hmi::View& b, double y0, double hgt, const std::string& label) {
            const gfx::Point p0 = vp.toScreen(0, y0), p1 = vp.toScreen(v->width, y0 + hgt);
            const auto col = gfx::Color::rgb(0xB98CFF);
            for (float x = p0.x; x < p1.x; x += 8) {
                ctx.r.fillRect({x, p0.y, 4, 1}, col);
                ctx.r.fillRect({x, p1.y - 1, 4, 1}, col);
            }
            const std::string tag = label + " : " + b.name;
            const float tw = ctx.r.measure(tag, ctx.theme.font.smallUi).width + 10.f;
            const gfx::Rect t{p1.x - tw - 4.f, p0.y + 3.f, tw, 17.f};
            ctx.r.fillRect(t, gfx::Color{40, 30, 60, 210});
            ctx.r.drawText({t.x + 5.f, t.y + 1.f}, tag, ctx.theme.font.smallUi, col);
        };
        if (head) bandFrame(*head, 0, head->height, "En-t\xC3\xAAte");
        if (foot) bandFrame(*foot, v->height - foot->height, foot->height, "Pied");
        if (!chain.empty()) {
            std::string tag = "Mod\xC3\xA8le : ";
            for (std::size_t k = 0; k < chain.size(); ++k) tag += (k ? " \xE2\x86\x92 " : "") + chain[k]->name;
            const float tw = ctx.r.measure(tag, ctx.theme.font.smallUi).width + 10.f;
            const float ty = head ? vp.toScreen(0, head->height).y + 4.f : a.y + 3.f;
            const gfx::Rect t{a.x + w - tw - 4.f, ty, tw, 17.f};
            ctx.r.fillRect(t, gfx::Color{40, 30, 60, 210});
            ctx.r.drawText({t.x + 5.f, t.y + 1.f}, tag, ctx.theme.font.smallUi, gfx::Color::rgb(0xB98CFF));
        }
    }
    paintGrid(ctx, vp, *v);
    ctx.r.popClip();
    ctx.r.strokeRect({a.x - 1, a.y - 1, w + 2, h + 2}, ctx.theme.color.borderStrong, 1.f);
    // Lot 8 : une popup montre, au-dessus de son contenu, la barre de titre
    // qu'elle aura en marche - son titre tel qu'il est ecrit (les trous comme
    // {Nom} se remplissent en marche), la croix si elle en a une.
    if (v->role == "popup" && v->popup.titleBar) {
        const float tb = static_cast<float>(hmi::kPopupTitleHeight) * vp.zoom;
        const gfx::Rect bar{a.x, a.y - tb, w, tb};
        ctx.r.fillRect(bar, gfx::Color::rgb(0x273142));
        ctx.r.strokeRect({bar.x - 1, bar.y - 1, bar.w + 2, bar.h + 1}, ctx.theme.color.borderStrong, 1.f);
        const auto& f = ctx.theme.font;
        const float gs = std::max(8.f, std::min(tb - 8.f, 18.f));
        drawHmiGlyph(ctx.r, HmiGlyph::Popup, {bar.x + 8.f, bar.y + (tb - gs) / 2.f, gs, gs}, ctx.theme.color.accent);
        const std::string title = v->popup.title.empty() ? v->name : v->popup.title;
        const std::string tag = "barre de titre (en marche)";
        const float tagW = ctx.r.measure(tag, f.smallUi).width + 12.f;
        const float closeW = v->popup.closeButton ? tb : 0.f;
        const float room = w - gs - 24.f - closeW - tagW;
        const auto chars = ctx.r.fitCharacters(title, f.ui, std::max(0.f, room));
        if (tb >= ctx.r.lineHeight(f.ui) * 0.8f)
            ctx.r.drawText({bar.x + gs + 14.f, bar.y + (tb - ctx.r.lineHeight(f.ui)) / 2.f}, std::string_view(title).substr(0, chars),
                           f.ui, gfx::Color::rgb(0xE6EAF0));
        if (room > 40.f && tb >= ctx.r.lineHeight(f.smallUi))
            ctx.r.drawText({bar.right() - closeW - tagW, bar.y + (tb - ctx.r.lineHeight(f.smallUi)) / 2.f}, tag, f.smallUi,
                           ctx.theme.color.textMuted);
        if (v->popup.closeButton) {
            const gfx::Rect close{bar.right() - tb, bar.y, tb, tb};
            const float m = tb * 0.32f;
            const auto xc = gfx::Color::rgb(0xC8D0DC);
            ctx.r.line({close.x + m, close.y + m}, {close.right() - m, close.bottom() - m}, xc, 1.6f);
            ctx.r.line({close.right() - m, close.y + m}, {close.x + m, close.bottom() - m}, xc, 1.6f);
        }
    }

    // Les guides.
    const auto guideColor = gfx::Color::rgb(0x21B8C8);
    for (std::size_t i = 0; i < v->guides.size(); ++i) {
        const auto& g = v->guides[i];
        const bool dragged = drag_ == Drag::Guide && dragGuide_ == static_cast<int>(i);
        const auto col = dragged && (g.position < 0 || g.position > (g.vertical ? v->width : v->height))
                             ? ctx.theme.color.error : guideColor;
        const gfx::Point p = vp.toScreen(g.vertical ? g.position : 0, g.vertical ? 0 : g.position);
        if (g.vertical) for (float y = r.y; y < r.y + r.h; y += 6) ctx.r.fillRect({p.x, y, 1, 3}, col);
        else for (float x = r.x; x < r.x + r.w; x += 6) ctx.r.fillRect({x, p.y, 3, 1}, col);
    }
    // Les reperes du magnetisme.
    const auto snapColor = gfx::Color::rgb(0xE0409A);
    for (double x : snapX_) { const float sx = vp.toScreen(x, 0).x; ctx.r.fillRect({sx, a.y, 1, h}, snapColor); }
    for (double y : snapY_) { const float sy = vp.toScreen(0, y).y; ctx.r.fillRect({a.x, sy, w, 1}, snapColor); }
    // Lot 12 : les ecarts egaux - une cote par ecart (le nouveau et son modele), sa valeur.
    for (const auto& s : spacing_) {
        const gfx::Point p0 = s.horizontal ? vp.toScreen(s.from, s.at) : vp.toScreen(s.at, s.from);
        const gfx::Point p1 = s.horizontal ? vp.toScreen(s.to, s.at) : vp.toScreen(s.at, s.to);
        ctx.r.line(p0, p1, snapColor, 1.5f);
        const float t = 5.f;
        if (s.horizontal) {
            ctx.r.line({p0.x, p0.y - t}, {p0.x, p0.y + t}, snapColor, 1.5f);
            ctx.r.line({p1.x, p1.y - t}, {p1.x, p1.y + t}, snapColor, 1.5f);
        } else {
            ctx.r.line({p0.x - t, p0.y}, {p0.x + t, p0.y}, snapColor, 1.5f);
            ctx.r.line({p1.x - t, p1.y}, {p1.x + t, p1.y}, snapColor, 1.5f);
        }
        const std::string label = std::to_string(static_cast<long>(std::lround(s.gap)));
        const auto f = ctx.theme.font.smallUi;
        const float lw = ctx.r.measure(label, f).width + 8.f, lh = ctx.r.lineHeight(f) + 2.f;
        const gfx::Point mid{(p0.x + p1.x) / 2.f, (p0.y + p1.y) / 2.f};
        const gfx::Rect badge = s.horizontal ? gfx::Rect{mid.x - lw / 2.f, mid.y - lh - 4.f, lw, lh}
                                             : gfx::Rect{mid.x + 6.f, mid.y - lh / 2.f, lw, lh};
        ctx.r.fillRoundedRect(badge, snapColor, 3.f);
        ctx.r.drawText({badge.x + 4.f, badge.y + 1.f}, label, f, gfx::Color::rgb(0xFFFFFF));
    }

    paintSelection(ctx, vp);

    if (marqueeShown_) {
        const gfx::Point m0 = vp.toScreen(marquee_.x, marquee_.y), m1 = vp.toScreen(marquee_.right(), marquee_.bottom());
        const gfx::Rect mr{std::min(m0.x, m1.x), std::min(m0.y, m1.y), std::fabs(m1.x - m0.x), std::fabs(m1.y - m0.y)};
        ctx.r.fillRect(mr, ctx.theme.color.accent.withAlpha(40));
        ctx.r.strokeRect(mr, ctx.theme.color.accent, 1.f);
    }
    // L'objet a poser suit la souris.
    if (placeKind_ && pointerIn_) {
        const Pt p = toView(pointer_);
        const double x = v->grid.snapGrid ? hmi::edit::snapToGrid(p.x, v->grid.step) : p.x;
        const double y = v->grid.snapGrid ? hmi::edit::snapToGrid(p.y, v->grid.step) : p.y;
        auto ghost = hmi::makeObject(*placeKind_, 0, "", x, y, v->activeLayer);
        std::string ghostLabel(hmi::kindLabel(*placeKind_));
        if (!placeSymbol_.empty()) {
            // Lot 10 : une instance a la taille de son symbole, et son dessin.
            if (const auto* sv = doc_->project.viewByName(placeSymbol_)) {
                ghost.setNumber("w", sv->width);
                ghost.setNumber("h", sv->height);
                const gfx::Point s0 = vp.toScreen(x, y);
                paintHmiSymbolPreview(ctx.r, doc_->project, *sv,
                                      {s0.x, s0.y, static_cast<float>(sv->width) * vp.zoom, static_cast<float>(sv->height) * vp.zoom},
                                      ctx.theme);
            }
            ghostLabel = "Symbole : " + placeSymbol_;
        }
        // Lot 12 : une variable - l'objet qui lui va, et son nom.
        if (placeVariable_) ghostLabel = placeVariable_->name + " \xE2\x86\x92 " + std::string(hmi::kindLabel(*placeKind_));
        const Box gb = ghost.box();
        const gfx::Point g0 = vp.toScreen(gb.x, gb.y);
        const gfx::Rect gr{g0.x, g0.y, std::max(8.f, static_cast<float>(gb.w) * vp.zoom),
                           std::max(8.f, static_cast<float>(gb.h) * vp.zoom)};
        ctx.r.fillRect(gr, ctx.theme.color.accent.withAlpha(45));
        ctx.r.strokeRect(gr, ctx.theme.color.accent, 1.f);
        ctx.r.drawText({gr.x + 4, gr.y + gr.h + 4}, ghostLabel, ctx.theme.font.smallUi, ctx.theme.color.accent);
    }
    ctx.r.popClip();
}

void HmiCanvas::placeAt(gfx::Point screen, bool keep) {
    const auto* v = view();
    if (!v || !placeKind_) return;
    const Pt at = toView(screen);
    const auto kind = *placeKind_;
    const double x = v->grid.snapGrid ? hmi::edit::snapToGrid(at.x, v->grid.step) : at.x;
    const double y = v->grid.snapGrid ? hmi::edit::snapToGrid(at.y, v->grid.step) : at.y;
    Id made = kNoId;
    // Lot 12 : pose sur un conteneur a onglets, un cadre ou un panneau, l'objet y
    // entre (dans la page montree).
    const Id parent = insideGroup_ != kNoId ? insideGroup_ : hmi::holderAt(*v, x, y);
    const auto adoptIn = [&](hmi::View& vv) {
        if (made == kNoId || parent == kNoId || !vv.object(parent)) return;
        if (hmi::kindHoldsChildren(vv.object(parent)->kind)) {
            (void)hmi::adopt(vv, made, parent);
        } else {
            vv.object(made)->parent = parent;
            vv.object(made)->layer = vv.object(parent)->layer;
        }
        hmi::edit::refreshGroupBounds(vv);
    };
    // Lot 12 : une variable - l'objet qui lui va ; une structure genere sa popup
    // d'equipement (une commande du projet : la vue neuve et l'objet, ensemble).
    if (placeVariable_) {
        const auto info = *placeVariable_;
        const Id vid = viewId_;
        const bool structure = hmi::design::shapeOf(info) == hmi::design::VarShape::Structure;
        const bool hadPopup = structure && hmi::design::existingEquipmentPopup(doc_->project, info.type) != nullptr;
        auto cmd = hmi::changeProject(doc_, "Poser " + info.name, [&](hmi::Project& p) {
            made = hmi::design::placeVariable(p, vid, info, x, y);
            if (hmi::View* vv = p.view(vid)) adoptIn(*vv);      // reprise : une popup a pu s'ajouter
        });
        if (cmd) {
            apply_(std::move(cmd));
            documentChanged();
        }
        if (!keep) setPlaceVariable(std::nullopt);
        if (made != kNoId) setSelection({made});
        placed->emit();
        // Ce qui a ete pose, et ce qui a ete genere ; rien de pose : dire pourquoi.
        const auto* v2 = view();
        const auto* o = made != kNoId && v2 ? v2->object(made) : nullptr;
        if (o) {
            std::string text = info.name + " \xE2\x86\x92 " + o->name + " (" + std::string(hmi::kindLabel(o->kind)) + ")";
            if (structure && !o->actions.empty())
                text += " : ouvre " + o->actions.front().target
                      + (hadPopup ? std::string{} : " (g\xC3\xA9n\xC3\xA9r\xC3\xA9" "e depuis " + info.type + ")") + ", "
                      + o->actions.front().value;
            else if (!structure)
                text += ", reli\xC3\xA9 \xC3\xA0 sa variable";
            text += structure && !hadPopup ? ". Ctrl+Z retire l'objet et la popup." : ". Ctrl+Z le retire.";
            status->emit(text);
        } else if (info.type.size() >= 5 && (info.type.rfind("ARRAY", 0) == 0 || info.type.rfind("array", 0) == 0))
            status->emit(info.name + " : un tableau ne se pose pas tel quel (choisis un de ses \xC3\xA9l\xC3\xA9ments, "
                         + info.name + "[0]...)");
        else
            status->emit(info.name + " (" + info.type + ") : pas d'objet pour ce type (ni BOOL, ni nombre, ni texte, "
                         "ni structure aux membres connus)");
        return;
    }
    // 1.10.2 (chantier D) : un MODELE D'OBJETS (une vue du dossier "Modeles
    // d'objets", glissee depuis la bibliotheque, ou choisie puis un clic) : ses
    // objets sont copies la ou on la lache (en une commande, Ctrl+Z les retire),
    // ils deviennent la selection, puis "Dupliquer..." s'ouvre pour remplir leurs
    // reperes ($Vanne$...) et en poser plusieurs.
    if (const auto* tv = placeSymbol_.empty() ? nullptr : doc_->project.viewByName(placeSymbol_);
        tv && !hmi::isSymbolView(*tv) && hmi::dup::isTemplatesFolder(tv->folder)) {
        const std::string model = tv->name;
        if (tv->id == v->id) {
            status->emit("Refus\xC3\xA9 : " + model + " est la vue ouverte (un mod\xC3\xA8le se pose dans une autre vue)");
            return;
        }
        const std::vector<hmi::Object> objs = tv->objects;
        std::vector<Id> roots;
        for (const auto& o : objs) if (o.parent == kNoId) roots.push_back(o.id);
        if (roots.empty()) {
            status->emit(model + " : ce mod\xC3\xA8le d'objets est vide (range des objets dans sa vue)");
            setPlaceSymbol({});
            placed->emit();
            return;
        }
        const auto box = hmi::edit::selectionBounds(*tv, roots);
        const double dx = x - box.x, dy = y - box.y;
        std::vector<Id> top;
        const bool done = edit("Poser le mod\xC3\xA8le " + model, [&](hmi::Project& p, hmi::View& vv) {
            std::map<Id, Id> ids;
            for (const auto& o : objs) ids[o.id] = p.allocate();
            for (auto o : objs) {
                const bool isTop = !ids.count(o.parent);
                o.id = ids[o.id];
                o.parent = isTop ? insideGroup_ : ids[o.parent];
                o.layer = vv.activeLayer;          // les calques du modele ne sont pas ceux de la vue
                o.locked = false;
                o.name = hmi::uniqueObjectName(vv, o.name.empty() ? std::string("Objet") : o.name);
                o.setNumber("x", o.number("x") + dx);
                o.setNumber("y", o.number("y") + dy);
                if (isTop) top.push_back(o.id);
                vv.objects.push_back(std::move(o));
            }
            hmi::edit::refreshGroupBounds(vv);
        });
        if (!keep) setPlaceSymbol({});
        placed->emit();
        if (!done || top.empty()) return;
        setSelection(top);
        status->emit("Mod\xC3\xA8le " + model + " pos\xC3\xA9 (" + std::to_string(top.size())
                     + (top.size() > 1 ? " objets" : " objet") + ") : Dupliquer... remplit ses rep\xC3\xA8res. Ctrl+Z le retire.");
        duplicateRequested->emit();       // la fenetre "Dupliquer..." sur ces objets
        return;
    }
    const std::string symbol = placeSymbol_;
    if (!symbol.empty()) {
        // Lot 10 : pas de symbole qui se contienne (directement ou non).
        const auto* sv = doc_->project.viewByName(symbol);
        if (!sv || !hmi::isSymbolView(*sv)) {
            status->emit("'" + symbol + "' n'est plus un symbole du projet");
            setPlaceSymbol({});
            return;
        }
        if (hmi::isSymbolView(*v) && (sv->id == v->id || hmi::symbolContains(doc_->project, *sv, v->name))) {
            status->emit("Refus\xC3\xA9 : " + symbol + " contient d\xC3\xA9j\xC3\xA0 " + v->name
                         + " (un symbole ne se contient pas lui-m\xC3\xAAme)");
            return;
        }
    }
    edit(symbol.empty() ? "Poser " + std::string(hmi::kindLabel(kind)) : "Poser le symbole " + symbol,
         [&](hmi::Project& p, hmi::View& vv) {
        made = symbol.empty() ? hmi::edit::add(p, vv, kind, x, y) : hmi::placeSymbol(p, vv, symbol, x, y);
        adoptIn(vv);
    });
    if (!keep) { placeKind_.reset(); placeSymbol_.clear(); }
    if (made != kNoId) setSelection({made});
    placed->emit();
    emitStatus(screen);   // la barre d'etat oublie l'outil relache
}

// ------------------------------------------------------------------ souris -
gfx::Rect HmiCanvas::eventBounds() const {
    if (drag_ == Drag::None) return bounds();
    constexpr float kFar = 1.0e6f;
    return { -kFar, -kFar, 2.f * kFar, 2.f * kFar };
}

ui::EventResult HmiCanvas::onEvent(const ui::InputEvent& ev) {
    const auto* v = view();
    if (!v) return ui::EventResult::Ignored;

    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        grabFocus();
        downHere_ = true;
        pressScreen_ = m->pos;
        pressView_ = toView(m->pos);
        const bool shift = m->mods.shift, ctrl = m->mods.ctrl;

        if (m->button == ui::MouseButton::Middle || (m->button == ui::MouseButton::Left && spaceDown_)) {
            drag_ = Drag::Pan;
            return ui::EventResult::Consumed;
        }
        // 1.10.2 (chantier D) : le clic droit - le menu de l'editeur (Dupliquer...).
        if (m->button == ui::MouseButton::Right) {
            if (const hmi::Id hit = hmi::edit::hitTest(*v, pressView_.x, pressView_.y, insideGroup_, 4.0 / zoom_);
                hit != hmi::kNoId && std::find(selection_.begin(), selection_.end(), hit) == selection_.end())
                setSelection({hit});
            contextRequested->emit(m->pos);
            return ui::EventResult::Consumed;
        }
        if (m->button != ui::MouseButton::Left) return ui::EventResult::Consumed;

        if (placeKind_) {
            placeAt(m->pos, shift);
            return ui::EventResult::Consumed;
        }

        // Une poignee de la selection.
        if (const int h = handleAt(m->pos); h >= 0) {
            snapshot_ = *v;
            ++dragSerial_;
            dragHandle_ = h;
            const auto units = hmi::edit::units(*v, selection_);
            startBox_ = singleTransformable() ? v->object(units.front())->box() : hmi::edit::selectionBounds(*v, units);
            if (h == 8) {
                drag_ = Drag::Rotate;
                const Pt c = singleTransformable()
                                 ? hmi::edit::toView(*v->object(units.front()),
                                                     {startBox_.w * v->object(units.front())->number("pivotX", 0.5),
                                                      startBox_.h * v->object(units.front())->number("pivotY", 0.5)})
                                 : Pt{startBox_.cx(), startBox_.cy()};
                startAngle_ = angleOf(c, pressView_);
            } else {
                drag_ = Drag::Resize;
            }
            return ui::EventResult::Consumed;
        }

        // Un guide.
        if (const int g = guideAt(m->pos); g >= 0 && hmi::edit::hitTest(*v, pressView_.x, pressView_.y, insideGroup_,
                                                                          4.0 / zoom_) == kNoId) {
            snapshot_ = *v;
            ++dragSerial_;
            drag_ = Drag::Guide;
            dragGuide_ = g;
            return ui::EventResult::Consumed;
        }

        const Id hit = hmi::edit::hitTest(*v, pressView_.x, pressView_.y, insideGroup_, 4.0 / zoom_);
        // Lot 12 : un clic sur un onglet d'un conteneur a onglets montre sa page (celle
        // ou l'on pose et ou l'on edite).
        if (hit != kNoId && v->object(hit)->kind == hmi::Kind::TabContainer && !shift && !ctrl) {
            const auto* tc = v->object(hit);
            const Box tb = tc->box();
            const std::string part = hmi::tabHit(*tc, tb.w, tb.h, pressView_.x - tb.x, pressView_.y - tb.y, hmi::tabLabels(*tc).size());
            if (part.rfind("onglet:", 0) == 0) {
                const int page = std::atoi(part.c_str() + 7);
                if (page != hmi::shownTabPage(*tc))
                    edit("Onglet " + std::to_string(page) + " de " + tc->name, [&](hmi::Project&, hmi::View& vv) {
                        if (auto* o = vv.object(hit)) o->setNumber("page", page);
                    });
                setSelection({hit});
                return ui::EventResult::Consumed;
            }
        }
        if (m->clickCount >= 2) {
            if (hit != kNoId && v->object(hit)->kind == hmi::Kind::Group) { enterGroup(hit); return ui::EventResult::Consumed; }
            // Lot 12 : un conteneur, un conteneur a onglets, un cadre, un panneau : on y entre.
            if (hit != kNoId && hmi::kindHoldsChildren(v->object(hit)->kind)) { enterGroup(hit); return ui::EventResult::Consumed; }
            // Lot 10 : une instance de symbole s'edite dans son symbole.
            if (hit != kNoId && v->object(hit)->kind == hmi::Kind::SymbolInstance) {
                const std::string name = v->object(hit)->text("symbol");
                symbolOpened->emit(name);
                return ui::EventResult::Consumed;
            }
            if (hit == kNoId && insideGroup_ != kNoId) { leaveGroup(); return ui::EventResult::Consumed; }
        }
        if (hit != kNoId) {
            auto sel = selection_;
            const bool already = std::find(sel.begin(), sel.end(), hit) != sel.end();
            if (shift || ctrl) {
                if (already) sel.erase(std::find(sel.begin(), sel.end(), hit));
                else sel.push_back(hit);
                setSelection(sel);
                if (already) return ui::EventResult::Consumed;
            } else if (!already) {
                setSelection({hit});
            }
            if (m->mods.alt) {
                // Alt au depart du glisser : on deplace une copie, posee
                // d'abord SUR l'original - c'est la souris qui l'en ecarte.
                duplicateSelection(0.0);
                v = view();
            }
            snapshot_ = *v;
            ++dragSerial_;
            startBox_ = hmi::edit::selectionBounds(*v, selection_);
            drag_ = Drag::Move;
            return ui::EventResult::Consumed;
        }
        // Dans le vide.
        if (insideGroup_ != kNoId) {
            const auto* g = v->object(insideGroup_);
            if (g && !hmi::edit::rotatedBounds(*g).contains(pressView_.x, pressView_.y)) {
                leaveGroup();
                setSelection({});
            }
        }
        if (!shift && !ctrl) setSelection({});
        drag_ = Drag::Marquee;
        marquee_ = {pressView_.x, pressView_.y, 0, 0};
        marqueeShown_ = true;
        invalidate();
        return ui::EventResult::Consumed;
    }

    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        pointer_ = m->pos;
        pointerIn_ = true;
        const Pt p = toView(m->pos);
        const double dx = p.x - pressView_.x, dy = p.y - pressView_.y;
        switch (drag_) {
            case Drag::None: {
                const Id h = hmi::edit::hitTest(*v, p.x, p.y, insideGroup_, 4.0 / zoom_);
                if (h != hover_) { hover_ = h; invalidate(); }
                if (placeKind_) invalidate();
                break;
            }
            case Drag::Pan:
                panX_ += m->delta.x;
                panY_ += m->delta.y;
                invalidate();
                break;
            case Drag::Move: {
                hmi::edit::SnapResult snap;
                snap.dx = dx;
                snap.dy = dy;
                if (!m->mods.alt) snap = hmi::edit::snapMove(snapshot_, startBox_, dx, dy, selection_, 6.0 / zoom_);
                snapX_ = snap.xLines;
                snapY_ = snap.yLines;
                spacing_ = snap.spacing;
                const auto sel = selection_;
                const hmi::View start = snapshot_;
                edit("D\xC3\xA9placer", [&](hmi::Project&, hmi::View& vv) {
                    vv = start;
                    hmi::edit::move(vv, sel, snap.dx, snap.dy);
                }, "move:" + std::to_string(dragSerial_));
                break;
            }
            case Drag::Resize: {
                const auto sel = selection_;
                const hmi::View start = snapshot_;
                const int handle = dragHandle_;
                const bool keep = m->mods.shift;
                const bool single = hmi::edit::units(start, sel).size() == 1
                                    && !start.effectivelyLocked(*start.object(hmi::edit::units(start, sel).front()));
                edit("Redimensionner", [&](hmi::Project&, hmi::View& vv) {
                    vv = start;
                    if (single) {
                        const Id id = hmi::edit::units(start, sel).front();
                        const hmi::Object o0 = *start.object(id);
                        const Box b0 = o0.box();
                        const Pt l = hmi::edit::toLocal(o0, p);
                        double left = 0, top = 0, right = b0.w, bottom = b0.h;
                        const bool L = handle == 0 || handle == 6 || handle == 7;
                        const bool R = handle == 2 || handle == 3 || handle == 4;
                        const bool T = handle == 0 || handle == 1 || handle == 2;
                        const bool B = handle == 4 || handle == 5 || handle == 6;
                        if (L) left = std::min(l.x, right - 1);
                        if (R) right = std::max(l.x, left + 1);
                        if (T) top = std::min(l.y, bottom - 1);
                        if (B) bottom = std::max(l.y, top + 1);
                        if (keep && (L || R) && (T || B) && b0.w > 0 && b0.h > 0) {
                            const double s = std::max((right - left) / b0.w, (bottom - top) / b0.h);
                            const double nw = b0.w * s, nh = b0.h * s;
                            if (L) left = right - nw; else right = left + nw;
                            if (T) top = bottom - nh; else bottom = top + nh;
                        }
                        const double nw = right - left, nh = bottom - top;
                        const Pt c = hmi::edit::toView(o0, {left + nw * o0.number("pivotX", 0.5),
                                                            top + nh * o0.number("pivotY", 0.5)});
                        const Box nb{c.x - nw * o0.number("pivotX", 0.5), c.y - nh * o0.number("pivotY", 0.5), nw, nh};
                        hmi::edit::setBox(vv, id, nb);
                    } else {
                        Box nb = startBox_;
                        const bool L = handle == 0 || handle == 6 || handle == 7;
                        const bool R = handle == 2 || handle == 3 || handle == 4;
                        const bool T = handle == 0 || handle == 1 || handle == 2;
                        const bool B = handle == 4 || handle == 5 || handle == 6;
                        double l = nb.x, t = nb.y, r = nb.right(), b = nb.bottom();
                        if (L) l = std::min(p.x, r - 1);
                        if (R) r = std::max(p.x, l + 1);
                        if (T) t = std::min(p.y, b - 1);
                        if (B) b = std::max(p.y, t + 1);
                        hmi::edit::scaleSelection(vv, sel, startBox_, {l, t, r - l, b - t});
                    }
                }, "resize:" + std::to_string(dragSerial_));
                break;
            }
            case Drag::Rotate: {
                const auto sel = selection_;
                const hmi::View start = snapshot_;
                const auto units = hmi::edit::units(start, sel);
                const bool single = units.size() == 1;
                Pt c{startBox_.cx(), startBox_.cy()};
                double base = 0;
                if (single) {
                    const auto& o0 = *start.object(units.front());
                    c = hmi::edit::toView(o0, {startBox_.w * o0.number("pivotX", 0.5), startBox_.h * o0.number("pivotY", 0.5)});
                    base = o0.rotation();
                }
                double delta = angleOf(c, p) - startAngle_;
                if (m->mods.shift) delta = std::round((base + delta) / 15.0) * 15.0 - base;
                edit("Rotation", [&](hmi::Project&, hmi::View& vv) {
                    vv = start;
                    hmi::edit::rotate(vv, sel, delta, !single);
                }, "rotate:" + std::to_string(dragSerial_));
                break;
            }
            case Drag::Marquee:
                marquee_ = {std::min(pressView_.x, p.x), std::min(pressView_.y, p.y), std::fabs(dx), std::fabs(dy)};
                invalidate();
                break;
            case Drag::Guide: {
                const hmi::View start = snapshot_;
                const int g = dragGuide_;
                edit("Guide", [&](hmi::Project&, hmi::View& vv) {
                    vv = start;
                    auto& guide = vv.guides[static_cast<std::size_t>(g)];
                    double pos = guide.vertical ? p.x : p.y;
                    if (vv.grid.snapGrid) pos = hmi::edit::snapToGrid(pos, vv.grid.step);
                    guide.position = pos;
                }, "guide:" + std::to_string(dragSerial_));
                break;
            }
        }
        emitStatus(m->pos);
        return drag_ == Drag::None ? ui::EventResult::Ignored : ui::EventResult::Consumed;
    }

    if (const auto* m = std::get_if<ui::MouseUp>(&ev)) {
        // Lot 12 : un lacher venu d'ailleurs (une tuile ou une variable glissee
        // depuis la bibliotheque) : l'objet se pose la.
        const bool dropped = !downHere_ && placeKind_ && m->button == ui::MouseButton::Left && bounds().contains(m->pos);
        downHere_ = false;
        if (dropped) {
            placeAt(m->pos, false);
            return ui::EventResult::Consumed;
        }
        const Drag was = drag_;
        drag_ = Drag::None;
        snapX_.clear();
        snapY_.clear();
        spacing_.clear();
        // Lot 12 : lache dans un conteneur a onglets, un cadre ou un panneau, l'objet y
        // entre ; tire hors de lui, il en sort (dans le meme pas d'annulation).
        if (was == Drag::Move && insideGroup_ == kNoId) {
            const auto units = hmi::edit::units(*v, selection_);
            std::vector<std::pair<Id, Id>> moves;
            for (Id u : units) {
                const auto* o = v->object(u);
                if (!o || v->effectivelyLocked(*o)) continue;
                const auto* par = o->parent != kNoId ? v->object(o->parent) : nullptr;
                if (par && (par->kind == hmi::Kind::Group || par->kind == hmi::Kind::Container || !hmi::kindHoldsChildren(par->kind))) continue;
                const Box b = hmi::edit::rotatedBounds(*o);
                const Id h = hmi::holderAt(*v, b.cx(), b.cy(), u);
                if (h != o->parent) moves.emplace_back(u, h);
            }
            if (!moves.empty())
                edit("D\xC3\xA9placer", [&](hmi::Project&, hmi::View& vv) {
                    for (const auto& [u, h] : moves) (void)hmi::adopt(vv, u, h);
                }, "move:" + std::to_string(dragSerial_));
        }
        if (was == Drag::Marquee) {
            marqueeShown_ = false;
            if (marquee_.w > 2 / zoom_ || marquee_.h > 2 / zoom_) {
                auto picked = hmi::edit::inRect(*v, marquee_, insideGroup_);
                auto sel = selection_;
                for (Id id : picked) if (std::find(sel.begin(), sel.end(), id) == sel.end()) sel.push_back(id);
                setSelection(sel);
            }
        }
        if (was == Drag::Guide && dragGuide_ >= 0) {
            // Un guide tire hors de la vue est retire.
            const auto* cur = view();
            if (cur && static_cast<std::size_t>(dragGuide_) < cur->guides.size()) {
                const auto& g = cur->guides[static_cast<std::size_t>(dragGuide_)];
                if (g.position < 0 || g.position > (g.vertical ? cur->width : cur->height)) {
                    const int idx = dragGuide_;
                    edit("Retirer le guide", [&](hmi::Project&, hmi::View& vv) {
                        vv.guides.erase(vv.guides.begin() + idx);
                    });
                }
            }
        }
        dragGuide_ = -1;
        invalidate();
        return was == Drag::None ? ui::EventResult::Ignored : ui::EventResult::Consumed;
    }

    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (w->mods.ctrl) {
            zoomBy(std::pow(1.1f, w->dy), w->pos);
        } else if (w->mods.shift) {
            panX_ += w->dy * 40.f;
            invalidate();
        } else {
            panX_ += w->dx * 40.f;
            panY_ += w->dy * 40.f;
            invalidate();
        }
        return ui::EventResult::Consumed;
    }

    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // SEULEMENT AVEC LE FOCUS. Les touches traversent tout l'arbre de
        // widgets : sans ce test, Suppr tape dans une case du panneau des
        // proprietes supprimait l'objet choisi, Ctrl+A et Entree n'arrivaient
        // jamais au champ de renommage d'un calque, et Espace passait en
        // mode "main" au milieu d'un texte.
        if (!focused()) return ui::EventResult::Ignored;
        const double step = k->mods.shift ? std::max(1, v->grid.step) : 1.0;
        switch (k->key) {
            case ui::Key::Space:  spaceDown_ = true; return ui::EventResult::Consumed;
            case ui::Key::Delete:
            case ui::Key::Backspace: deleteSelection(); return ui::EventResult::Consumed;
            case ui::Key::Left:  nudge(-step, 0); return ui::EventResult::Consumed;
            case ui::Key::Right: nudge(step, 0);  return ui::EventResult::Consumed;
            case ui::Key::Up:    nudge(0, -step); return ui::EventResult::Consumed;
            case ui::Key::Down:  nudge(0, step);  return ui::EventResult::Consumed;
            case ui::Key::Escape:
                if (placeKind_) { placeKind_.reset(); placeSymbol_.clear(); placeVariable_.reset(); invalidate(); }
                else if (insideGroup_ != kNoId) leaveGroup();
                else setSelection({});
                return ui::EventResult::Consumed;
            case ui::Key::Return:
                if (selection_.size() == 1) enterGroup(selection_.front());
                return ui::EventResult::Consumed;
            case ui::Key::A: if (k->mods.ctrl) { selectAll(); return ui::EventResult::Consumed; } break;
            case ui::Key::Z: if (k->mods.ctrl) { historyRequested->emit(k->mods.shift); return ui::EventResult::Consumed; } break;
            case ui::Key::Y: if (k->mods.ctrl) { historyRequested->emit(true); return ui::EventResult::Consumed; } break;
            case ui::Key::C: if (k->mods.ctrl) { k->mods.shift ? copyStyle() : copySelection(); return ui::EventResult::Consumed; } break;
            case ui::Key::X: if (k->mods.ctrl) { cutSelection(); return ui::EventResult::Consumed; } break;
            // 1.10.2 (chantier D) : Ctrl+D "Dupliquer..." (la fenetre), Ctrl+Maj+D tel quel.
            case ui::Key::D:
                if (k->mods.ctrl && !selection_.empty()) {
                    if (k->mods.shift) duplicateSelection();
                    else duplicateRequested->emit();
                    return ui::EventResult::Consumed;
                }
                break;
            case ui::Key::V:
                if (k->mods.ctrl) { k->mods.shift ? pasteStyle() : paste(); return ui::EventResult::Consumed; }
                break;
            default: break;
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* k = std::get_if<ui::KeyUp>(&ev)) {
        if (k->key == ui::Key::Space && spaceDown_) { spaceDown_ = false; return ui::EventResult::Consumed; }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
