#include "HmiCanvas.hpp"
#include "HmiPartage.hpp"   // 1.12.27 : copier dans une fenetre, coller dans l'autre
#include "../TablePaste.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "HmiIcons.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiDuplicate.hpp"   // 1.10.2 (D) : les modeles d'objets
#include "../../hmi/HmiRanges.hpp"   // 1.12.10 (lot F) : un objet pose suit la plage de sa variable
#include "../../hmi/HmiStore.hpp"    // 1.13.1 (lot K3) : le code brut d'un objet

#include "../../ui/Shapes.hpp"
#include "../../ui/widgets/Controls.hpp"   // 1.12.18 : KeyboardMenuScope (Maj+F10 sur la vue)
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
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
constexpr float kVertex = 4.5f;         // 1.12.12 (lot H) : le rayon d'une poignee de sommet, en pixels ecran

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
    // 1.12.27 : le document de la copie. Un autre (un autre projet ouvert dans cette
    // fenetre depuis) : la copie publiee, avec ce dont les objets ont besoin.
    const void*              fromDoc{nullptr};
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
    setZoomSupport(ui::ZoomSupport::Native);   // 1.12.30 : le zoom de la vue, celui de la puce du bandeau bas
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
    const auto o = drawOrigin();
    return {o.x + panX_, o.y + panY_, zoom_};
}

// ---- 1.12.30 : les regles ------------------------------------------------------------
gfx::Point HmiCanvas::drawOrigin() const noexcept {
    const auto r = contentRect();
    const float off = rulers_ ? kRuler : 0.f;
    return {r.x + off, r.y + off};
}

gfx::Rect HmiCanvas::drawArea() const noexcept {
    const auto r = contentRect();
    const float off = rulers_ ? kRuler : 0.f;
    return {r.x + off, r.y + off, std::max(0.f, r.w - off), std::max(0.f, r.h - off)};
}

gfx::Rect HmiCanvas::topRuler() const noexcept {
    if (!rulers_) return {};
    const auto r = contentRect();
    return {r.x + kRuler, r.y, std::max(0.f, r.w - kRuler), kRuler};
}

gfx::Rect HmiCanvas::leftRuler() const noexcept {
    if (!rulers_) return {};
    const auto r = contentRect();
    return {r.x, r.y + kRuler, kRuler, std::max(0.f, r.h - kRuler)};
}

int HmiCanvas::rulerAt(gfx::Point s) const noexcept {
    if (!rulers_) return 0;
    if (topRuler().contains(s)) return 1;
    if (leftRuler().contains(s)) return 2;
    const auto r = contentRect();
    if (gfx::Rect{r.x, r.y, kRuler, kRuler}.contains(s)) return 3;   // le coin
    return 0;
}

void HmiCanvas::setRulersShown(bool on) {
    if (on == rulers_) return;
    // Le dessin ne saute pas : la vue garde sa place a l'ecran, la place du dessin change.
    const float d = on ? kRuler : -kRuler;
    panX_ -= d;
    panY_ -= d;
    rulers_ = on;
    invalidate();
}

void HmiCanvas::toggleGuidesHidden() {
    edit("Guides", [&](hmi::Project&, hmi::View& v) { v.grid.guidesHidden = !v.grid.guidesHidden; });
    if (const auto* v = view()) status->emit(v->grid.guidesHidden ? "Guides cach\xC3\xA9s (ils ne sont plus magn\xC3\xA9tiques)" : "Guides montr\xC3\xA9s");
}

void HmiCanvas::toggleGuidesLocked() {
    edit("Guides", [&](hmi::Project&, hmi::View& v) { v.grid.guidesLocked = !v.grid.guidesLocked; });
    if (const auto* v = view()) status->emit(v->grid.guidesLocked ? "Guides verrouill\xC3\xA9s : ils ne se d\xC3\xA9placent plus" : "Guides d\xC3\xA9verrouill\xC3\xA9s");
}

void HmiCanvas::clearGuides() {
    const auto* v = view();
    if (!v || v->guides.empty()) return;
    const auto n = v->guides.size();
    edit("Retirer les guides", [&](hmi::Project&, hmi::View& vv) { vv.guides.clear(); });
    status->emit(std::to_string(n) + (n > 1 ? " guides retir\xC3\xA9s" : " guide retir\xC3\xA9") + " (Ctrl+Z les remet)");
}

void HmiCanvas::toggleColumns() {
    edit("Grille de colonnes", [&](hmi::Project&, hmi::View& v) { v.columns.visible = !v.columns.visible; });
}

std::vector<hmi::edit::Dimension> HmiCanvas::dimensionsAt(gfx::Point screen, Id* target) const {
    if (target) *target = kNoId;
    const auto* v = view();
    if (!v || selection_.empty()) return {};
    const auto units = hmi::edit::units(*v, selection_);
    if (units.empty()) return {};
    const Box sel = hmi::edit::selectionBounds(*v, units);
    Id t = kNoId;
    if (drawArea().contains(screen)) {
        const Pt p = toView(screen);
        t = hmi::edit::hitTest(*v, p.x, p.y, insideGroup_, 4.0 / zoom_, true);
        // L'objet vise fait partie de la selection (ou de l'un de ses groupes) : rien de vise.
        for (Id u : units)
            if (t == u || (t != kNoId && hmi::edit::topLevelOf(*v, t) == u)) t = kNoId;
        // Un groupe en edition interne : on vise ses enfants, comme le clic.
    }
    if (target) *target = t;
    return hmi::edit::dimensions(*v, sel, units, t);
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
    // 1.12.12 (lot H) : le sommet choisi est celui d'un objet ; une autre selection l'oublie.
    if (selection_.size() != 1 || selection_.front() != vertexOwner_) {
        vertex_ = -1;
        vertexOwner_ = kNoId;
    }
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
    placeVariant_.clear();
    invalidate();
}

void HmiCanvas::setPlaceVariant(hmi::Kind kind, std::string variant) {
    setPlaceKind(kind);
    placeVariant_ = std::move(variant);
}

void HmiCanvas::setPlaceSymbol(std::string symbol) {
    placeVariant_.clear();
    placeSymbol_ = std::move(symbol);
    placeKind_ = placeSymbol_.empty() ? std::nullopt : std::optional<hmi::Kind>(hmi::Kind::SymbolInstance);
    placeVariable_.reset();
    invalidate();
}

void HmiCanvas::setPlaceVariable(std::optional<hmi::design::VarInfo> variable) {
    placeSymbol_.clear();
    placeVariant_.clear();
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
    // 1.12.12 (lot H) : un sommet qui n'existe plus (Ctrl+Z, Contenu) ne reste pas choisi ; un plan retire
    // n'attend plus sa zone.
    if (vertex_ >= 0) {
        const auto t = vertexTarget();
        if (!t.valid() || t.object != vertexOwner_ || static_cast<std::size_t>(vertex_) >= hmi::vertices::points(*v, t).size()) {
            vertex_ = -1;
            vertexOwner_ = kNoId;
        }
    }
    if (traceMap_ != kNoId && !v->object(traceMap_)) {
        traceMap_ = kNoId;
        trace_.clear();
    }
    invalidate();
}

// ------------------------------------------------- 1.12.12 (lot H) : sommets -
hmi::vertices::Target HmiCanvas::vertexTarget() const {
    using T = hmi::vertices::Target;
    const auto* v = view();
    if (!v || selection_.size() != 1) return {};
    const auto* o = v->object(selection_.front());
    if (!o) return {};
    T t;
    if (hmi::vertices::isShape(*o)) {
        t.what = T::What::Shape;
        t.object = o->id;
    } else if (o->kind == hmi::Kind::ZoneMap && zoneMap_ == o->id && zone_ >= 0) {
        t.what = T::What::Zone;
        t.object = o->id;
        t.zone = zone_;
    } else if (o->kind == hmi::Kind::XYChart) {   // la courbe de reference, dans la zone de trace (echelle fixe)
        t.what = T::What::Reference;
        t.object = o->id;
    }
    if (!t.valid() || !hmi::vertices::editable(*v, t)) return {};
    return t;
}

void HmiCanvas::selectVertex(int index) {
    const auto t = vertexTarget();
    const Id owner = t.valid() && index >= 0 ? t.object : kNoId;
    if (owner == kNoId) index = -1;
    if (index == vertex_ && owner == vertexOwner_) return;
    vertex_ = index;
    vertexOwner_ = owner;
    invalidate();
    vertexChosen->emit(vertex_);
}

bool HmiCanvas::removeSelectedVertex() {
    const auto* v = view();
    const auto t = vertexTarget();
    if (!v || !t.valid() || vertex_ < 0) return false;
    const int k = vertex_;
    const auto n = hmi::vertices::points(*v, t).size();
    if (n <= hmi::vertices::minimum(*v, t)) {
        using W = hmi::vertices::Target::What;
        status->emit(t.what == W::Reference ? "Sur le dessin, une courbe de r\xC3\xA9" "f\xC3\xA9rence garde au moins 2 points (Contenu peut la "
                                              "vider) : le point reste."
                     : t.what == W::Shape && v->object(t.object)->kind == hmi::Kind::Line
                         ? "Une ligne garde au moins 2 sommets : le sommet reste."
                         : "Un polygone, une zone gardent au moins 3 sommets : le sommet reste.");
        return false;
    }
    bool done = false;
    (void)edit("Retirer le sommet", [&](hmi::Project&, hmi::View& vv) { done = hmi::vertices::removeVertex(vv, t, static_cast<std::size_t>(k)); });
    if (done) selectVertex(std::min(k, static_cast<int>(n) - 2));
    return done;
}

void HmiCanvas::setEditedZone(Id map, int zone) {
    if (map == zoneMap_ && zone == zone_) return;
    zoneMap_ = map;
    zone_ = zone;
    vertex_ = -1;
    vertexOwner_ = kNoId;
    invalidate();
}

void HmiCanvas::startZoneTrace(Id map) {
    const auto* v = view();
    const auto* o = v ? v->object(map) : nullptr;
    if (!o || o->kind != hmi::Kind::ZoneMap) return;
    // Des zones en formule, un plan verrouille : rien a tracer (addZone le refuserait a la fin).
    if (const auto* z = o->find("mapZones"); (z && !z->expr.empty()) || v->effectivelyLocked(*o)) {
        status->emit("Les zones de ce plan ne se tracent pas : elles viennent d'une formule, ou le plan est verrouill\xC3\xA9.");
        return;
    }
    traceMap_ = map;
    trace_.clear();
    placeKind_.reset();
    placeSymbol_.clear();
    placeVariable_.reset();
    placeVariant_.clear();
    grabFocus();
    status->emit("Tracer une zone : un clic par point ; un double-clic, ou un clic sur le premier point, la ferme ; "
                 "\xC3\x89" "chap l'abandonne.");
    invalidate();
}

void HmiCanvas::traceClick(gfx::Point screen, int clicks, bool free) {
    const auto* v = view();
    const auto* map = v ? v->object(traceMap_) : nullptr;
    if (!map || map->kind != hmi::Kind::ZoneMap) {
        traceMap_ = kNoId;
        trace_.clear();
        invalidate();
        return;
    }
    const auto vp = viewport();
    bool close = clicks >= 2 && trace_.size() >= 3;
    if (!close && trace_.size() >= 3) {
        const gfx::Point first = vp.toScreen(trace_.front().x, trace_.front().y);
        close = std::hypot(first.x - screen.x, first.y - screen.y) <= kVertex + 3.f;
    }
    if (close) {
        const Id mapId = traceMap_;
        const auto pts = trace_;
        traceMap_ = kNoId;
        trace_.clear();
        int made = -1;
        (void)edit("Tracer une zone", [&](hmi::Project&, hmi::View& vv) { made = hmi::vertices::addZone(vv, mapId, pts); });
        invalidate();
        if (made >= 0) zoneTraced->emit(made);
        return;
    }
    if (clicks >= 2) return;   // le second clic d'un double-clic, avant le troisieme point : rien de plus
    hmi::edit::Pt p = toView(screen);
    if (!free) p = hmi::vertices::snapPoint(*v, p, trace_, traceMap_, 6.0 / zoom_).at;
    trace_.push_back(p);
    invalidate();
}

void HmiCanvas::paintVertices(const ui::PaintContext& ctx, const HmiViewport& vp) {
    const auto* v = view();
    if (!v) return;
    const auto accent = ctx.theme.color.accent;
    const gfx::Color white{255, 255, 255, 255};
    const auto ring = [&](hmi::edit::Pt p, bool chosen, float r) {
        const auto pts = shapes::ellipse(vp.toScreen(p.x, p.y), r, r, 16);
        shapes::fillPolygon(ctx.r, pts, chosen ? accent : white);
        shapes::strokePolyline(ctx.r, pts, true, chosen ? white : accent, 1.5f);
    };
    if (const auto t = vertexTarget(); t.valid()) {
        const auto pts = hmi::vertices::points(*v, t);
        // La zone choisie dans Contenu : son contour, par-dessus le plan.
        if (t.what == hmi::vertices::Target::What::Zone && pts.size() >= 2) {
            std::vector<gfx::Point> line;
            for (const auto& p : pts) line.push_back(vp.toScreen(p.x, p.y));
            shapes::strokePolyline(ctx.r, line, true, accent, 2.f);
        }
        for (std::size_t i = 0; i < pts.size(); ++i) ring(pts[i], static_cast<int>(i) == vertex_, kVertex);
    }
    // La zone qui se trace : ses points, et le trait jusqu'a la souris ; le premier grossit quand il ferme.
    if (traceMap_ != kNoId && !trace_.empty()) {
        std::vector<gfx::Point> line;
        for (const auto& p : trace_) line.push_back(vp.toScreen(p.x, p.y));
        if (pointerIn_) line.push_back(pointer_);
        shapes::strokePolyline(ctx.r, line, false, accent, 1.5f);
        for (std::size_t i = 0; i < trace_.size(); ++i) ring(trace_[i], i == 0 && trace_.size() >= 3, i == 0 ? kVertex + 1.5f : kVertex - 1.f);
    }
}

// -------------------------------------------------------------------- zoom --
void HmiCanvas::setZoom(float z) { zoomBy(z / zoom_); }

void HmiCanvas::zoomBy(float factor, std::optional<gfx::Point> anchor) {
    const float nz = std::clamp(zoom_ * factor, 0.05f, 16.f);
    const auto r = drawArea();    // 1.12.30 : sans les regles
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
    const auto r = drawArea();    // 1.12.30 : sans les regles
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
    const std::string list = partage::texteDesObjets(c.objects);
    c.systemText = list;
    c.fromDoc = doc_.get();
    ui::setClipboardText(list);
    // 1.12.27 : publiee pour les autres fenetres (et les autres projets de celle-ci).
    if (doc_) (void)partage::publier(doc_->project, viewId_, c.objects, list);
}

void HmiCanvas::cutSelection() {
    copySelection();
    const auto sel = selection_;
    edit("Couper", [&](hmi::Project&, hmi::View& v) { hmi::edit::remove(v, sel); });
    setSelection({});
}

void HmiCanvas::paste() {
    auto& c = clipboard();
    // 1.12.27 : la copie d'une autre fenetre (ou d'un autre projet de celle-ci) : ses
    // objets, et ce dont ils ont besoin (symboles, popups, images, styles, variables).
    if (const std::string sys = ui::clipboardText();
        !sys.empty() && (sys != c.systemText || c.fromDoc != doc_.get()) && pasteShared(sys))
        return;
    // 1.13.1 (lot K3) : un code brut d'objet (Copier le code brut) : ses objets, neufs.
    if (const std::string sys = ui::clipboardText(); sys != c.systemText && hmi::isObjectCode(sys) && pasteCode(sys)) return;
    // Lot 20 : le presse-papiers du systeme a change depuis la derniere copie
    // d'objets (une colonne copiee dans Excel) : ce sont des variables.
    if (const std::string sys = ui::clipboardText(); !sys.empty() && sys != c.systemText) {
        // 1.12.27 : la liste des objets d'une autre fenetre dont la copie n'est plus la :
        // ce ne sont pas des noms de variables.
        if (partage::listeObjets(sys)) {
            status->emit("Coller : ce sont les objets d'une autre fen\xC3\xAAtre, mais leur copie n'est plus l\xC3\xA0 "
                         "(recopie-les dans l'autre fen\xC3\xAAtre).");
            return;
        }
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
    // 1.12.12 (lot H) : un sommet choisi se deplace aux fleches (Maj : le pas de la grille) ; l'objet reste.
    if (const auto t = vertexTarget(); vertex_ >= 0 && t.valid()) {
        const auto pts = hmi::vertices::points(*view(), t);
        if (static_cast<std::size_t>(vertex_) < pts.size()) {
            const auto k = static_cast<std::size_t>(vertex_);
            const hmi::edit::Pt to{pts[k].x + dx, pts[k].y + dy};
            (void)edit("D\xC3\xA9placer le sommet", [&](hmi::Project&, hmi::View& v) { (void)hmi::vertices::moveVertex(v, t, k, to); },
                       "nudge-vertex");
            return;
        }
    }
    const auto sel = selection_;
    edit("D\xC3\xA9placer", [&](hmi::Project&, hmi::View& v) { hmi::edit::move(v, sel, dx, dy); }, "nudge");
    nudgedAt_ = paintTime_;    // 1.12.30 : les cotes aux voisins, un moment
}
void HmiCanvas::addGuide(bool vertical) {
    edit(vertical ? "Guide vertical" : "Guide horizontal", [&](hmi::Project&, hmi::View& v) {
        v.guides.push_back({vertical, vertical ? v.width / 2.0 : v.height / 2.0});
    });
}
// ---- 1.12.18 : ce que fait le menu du clic droit, au point du clic ----------------------
hmi::edit::Pt HmiCanvas::contextPoint() const { return toView(contextScreen_); }

void HmiCanvas::cancelPlacement() {
    const bool was = placeKind_.has_value() || traceMap_ != kNoId;
    if (traceMap_ != kNoId) { traceMap_ = kNoId; trace_.clear(); }
    placeKind_.reset();
    placeSymbol_.clear();
    placeVariable_.reset();
    placeVariant_.clear();
    invalidate();
    if (was) {
        placed->emit();   // la bibliotheque ne montre plus l'outil
        status->emit("Pose annul\xC3\xA9" "e");
    }
}

// ---- 1.13.1 (lot K3) : un code brut colle dans la vue -----------------------------------
bool HmiCanvas::pasteCode(const std::string& text, std::optional<hmi::edit::Pt> at) {
    if (!hmi::isObjectCode(text) || !view()) return false;
    auto parsed = hmi::parseObjectCode(text);
    if (!parsed) {
        status->emit("Coller le code brut : rien n'a chang\xC3\xA9 - " + parsed.error().context);
        return true;
    }
    auto objects = parsed->objects;
    if (at) {
        // Le coin haut-gauche des objets du haut vient au point.
        double minX = 0, minY = 0;
        bool first = true;
        for (const Id r : parsed->roots)
            for (const auto& o : objects)
                if (o.id == r) {
                    if (first || o.number("x") < minX) minX = o.number("x");
                    if (first || o.number("y") < minY) minY = o.number("y");
                    first = false;
                }
        for (auto& o : objects) {
            o.setNumber("x", o.number("x") - minX + at->x);
            o.setNumber("y", o.number("y") - minY + at->y);
        }
    }
    std::vector<Id> top;
    std::string why;
    const std::size_t n = objects.size();
    (void)edit("Coller le code brut", [&](hmi::Project& p, hmi::View& v) { top = hmi::edit::pasteCode(p, v, objects, insideGroup_, &why); });
    if (top.empty()) {
        status->emit("Coller le code brut : rien n'a chang\xC3\xA9 - " + (why.empty() ? std::string("aucun objet") : why));
        return true;
    }
    setSelection(top);
    status->emit("Code brut coll\xC3\xA9 : " + std::to_string(n) + (n > 1 ? " objets neufs" : " objet neuf") + " (Ctrl+Z annule).");
    return true;
}

bool HmiCanvas::pasteShared(const std::string& text, std::optional<hmi::edit::Pt> at) {
    if (!doc_ || !view()) return false;
    const auto copie = partage::lire(text);
    if (!copie) return false;
    double dx = 0, dy = 0;
    if (at) {
        // Le coin haut-gauche de ce qui est colle vient au point.
        if (const auto box = partage::etendue(*copie)) {
            dx = at->x - box->x;
            dy = at->y - box->y;
        }
    } else {
        // A la meme place la premiere fois (une autre vue), puis decale a chaque fois.
        static std::string lastKey;
        static int again = 0;
        const std::string key = copie->cle + "@" + std::to_string(viewId_);
        again = key == lastKey ? again + 1 : 0;
        lastKey = key;
        dx = dy = 20.0 * again;
    }
    partage::Resultat r;
    const Id into = viewId_, group = insideGroup_;
    auto cmd = hmi::changeProject(doc_, "Coller depuis l'autre fen\xC3\xAAtre",
                                  [&](hmi::Project& p) { r = partage::coller(p, into, *copie, dx, dy, group); });
    if (!cmd) return false;
    apply_(std::move(cmd));
    documentChanged();
    setSelection(r.poses);
    status->emit(partage::message(*copie, r));
    return true;
}

void HmiCanvas::dragOut() {
    dragOutOk_ = false;
    const auto* v = view();
    // Les objets tires, a leur place de depart, avec leur contenu.
    std::set<Id> wanted;
    for (Id id : hmi::edit::units(snapshot_, selection_)) {
        wanted.insert(id);
        for (Id d : snapshot_.descendantsOf(id)) wanted.insert(d);
    }
    std::vector<hmi::Object> objets;
    for (const auto& o : snapshot_.objects)
        if (wanted.count(o.id)) objets.push_back(o);
    // Ici, rien ne bouge : le deplacement s'annule (Ctrl+Z s'il tient en un pas ;
    // sinon la place de depart, remise en un pas de plus).
    drag_ = Drag::None;
    snapX_.clear();
    snapY_.clear();
    spacing_.clear();
    if (moveSegments_ == 1) {
        historyRequested->emit(false);
    } else if (moveSegments_ > 1 && v) {
        const hmi::View start = snapshot_;
        const auto sel = wanted;
        (void)edit("D\xC3\xA9placer", [&](hmi::Project&, hmi::View& vv) {
            for (auto& o : vv.objects)
                if (sel.count(o.id))
                    if (const auto* was = start.object(o.id)) o = *was;
        }, "move:" + std::to_string(dragSerial_));
    }
    moveSegments_ = 0;
    invalidate();
    if (objets.empty() || !doc_) return;
    const std::string texte = partage::texteDesObjets(objets);
    if (!partage::publier(doc_->project, viewId_, objets, texte, partage::Canal::Glisser)) {
        status->emit("Glisser vers une autre fen\xC3\xAAtre : la copie ne s'\xC3\xA9" "crit pas dans le dossier des fen\xC3\xAAtres.");
        return;
    }
    std::size_t haut = 0;
    for (const auto& o : objets)
        if (!wanted.count(o.parent)) ++haut;
    const int r = ui::dragText(texte);
    if (r > 0)
        status->emit(std::to_string(haut) + (haut == 1 ? " objet copi\xC3\xA9" : " objets copi\xC3\xA9s")
                     + " vers l'autre fen\xC3\xAAtre (ici, rien ne bouge).");
    else
        status->emit("Glisser vers une autre fen\xC3\xAAtre : annul\xC3\xA9 (\xC3\x89" "chap, ou l\xC3\xA2" "ch\xC3\xA9 sur rien).");
}

void HmiCanvas::pasteAt(gfx::Point screen) {
    auto& c = clipboard();
    // 1.12.27 : la copie d'une autre fenetre (ou d'un autre projet), au point du clic.
    if (const std::string sys = ui::clipboardText(); !sys.empty() && (sys != c.systemText || c.fromDoc != doc_.get())) {
        const auto* v = view();
        Pt at = toView(screen);
        if (v && v->grid.snapGrid) { at.x = hmi::edit::snapToGrid(at.x, v->grid.step); at.y = hmi::edit::snapToGrid(at.y, v->grid.step); }
        if (pasteShared(sys, at)) return;
        if (hmi::isObjectCode(sys) && pasteCode(sys, at)) return;   // 1.13.1 (lot K3) : un code brut, au point
    }
    if (c.objects.empty()) { paste(); return; }   // des noms de variables (Excel) : la colonne habituelle
    // Le coin haut-gauche de ce qui est colle (les objets du haut) vient au point du clic.
    double minX = 0, minY = 0;
    bool first = true;
    std::set<Id> ids;
    for (const auto& o : c.objects) ids.insert(o.id);
    for (const auto& o : c.objects) {
        if (ids.count(o.parent)) continue;
        const double x = o.number("x"), y = o.number("y");
        if (first || x < minX) minX = x;
        if (first || y < minY) minY = y;
        first = false;
    }
    const auto* v = view();
    Pt at = toView(screen);
    if (v && v->grid.snapGrid) { at.x = hmi::edit::snapToGrid(at.x, v->grid.step); at.y = hmi::edit::snapToGrid(at.y, v->grid.step); }
    std::vector<Id> top;
    const bool done = edit("Coller ici", [&](hmi::Project& p, hmi::View& vv) {
        std::map<Id, Id> made;
        for (const auto& o : c.objects) made[o.id] = p.allocate();
        for (auto o : c.objects) {
            const bool isTop = !made.count(o.parent);
            o.id = made[o.id];
            o.parent = isTop ? insideGroup_ : made[o.parent];
            if (!vv.layer(o.layer)) o.layer = vv.activeLayer;
            o.locked = false;
            std::string base = o.name;
            while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
            if (!base.empty() && base.back() == '_') base.pop_back();
            o.name = hmi::uniqueObjectName(vv, base.empty() ? "Objet" : base);
            o.setNumber("x", o.number("x") - minX + at.x);
            o.setNumber("y", o.number("y") - minY + at.y);
            if (isTop) top.push_back(o.id);
            vv.objects.push_back(std::move(o));
        }
        hmi::edit::refreshGroupBounds(vv);
    });
    if (done) setSelection(top);
}

void HmiCanvas::insertAt(hmi::Kind kind, gfx::Point screen) {
    placeKind_ = kind;
    placeSymbol_.clear();
    placeVariant_.clear();
    placeVariable_.reset();
    placeAt(screen, false);
}

void HmiCanvas::addGuideAt(bool vertical, gfx::Point screen) {
    const Pt at = toView(screen);
    edit(vertical ? "Guide vertical" : "Guide horizontal", [&](hmi::Project&, hmi::View& v) {
        const double pos = vertical ? std::clamp(at.x, 0.0, static_cast<double>(v.width)) : std::clamp(at.y, 0.0, static_cast<double>(v.height));
        v.guides.push_back({vertical, std::round(pos)});
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
    // 1.12.30 : des guides caches ou verrouilles ne se prennent pas.
    if (!v || v->grid.guidesHidden || v->grid.guidesLocked) return -1;
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
    // 1.12.12 (lot H) : le sommet choisi, ou il est.
    if (vertex_ >= 0)
        if (const auto t = vertexTarget(); t.valid()) {
            const auto pts = hmi::vertices::points(*v, t);
            // La reference d'une courbe XY : dans les unites de ses axes.
            const auto data = t.what == hmi::vertices::Target::What::Reference ? hmi::vertices::listOf(*v->object(t.object)).points
                                                                                : std::vector<hmi::vertices::Vertex>{};
            if (static_cast<std::size_t>(vertex_) < data.size())
                text += "   |   point " + std::to_string(vertex_ + 1) + " : x " + hmi::formatNumber(data[static_cast<std::size_t>(vertex_)].x)
                      + "  y " + hmi::formatNumber(data[static_cast<std::size_t>(vertex_)].y);
            else if (static_cast<std::size_t>(vertex_) < pts.size())
                text += "   |   sommet " + std::to_string(vertex_ + 1) + " : x " + fmt(pts[static_cast<std::size_t>(vertex_)].x) + "  y "
                      + fmt(pts[static_cast<std::size_t>(vertex_)].y);
        }
    if (traceMap_ != kNoId) text += "   |   zone : " + std::to_string(trace_.size()) + " point(s), double-clic pour la fermer";
    // 1.12.30 : Alt au survol - les cotes, en clair.
    if (altHeld_ && drag_ == Drag::None && !selection_.empty() && !placeKind_) {
        Id target = kNoId;
        const auto dims = dimensionsAt(s, &target);
        if (!dims.empty()) {
            const auto* t = target != kNoId ? v->object(target) : nullptr;
            text += t ? "   |   cotes vers " + t->name + " :" : std::string("   |   cotes (voisins, bords) :");
            for (const auto& d : dims) text += " " + fmt(d.value());
        }
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

// ---- 1.12.26 : le suivi des modifications -------------------------------------------
// Le point orange des objets changes depuis le dernier enregistrement, en haut a droite de l'objet.
void HmiCanvas::paintPendingDots(const ui::PaintContext& ctx, const HmiViewport& vp) {
    if (!pendingDots_ || pendingMarks_.empty()) return;
    const auto* v = view();
    if (!v) return;
    const auto orange = gfx::Color::rgb(0xF2994A), white = gfx::Color::rgb(0xFFFFFF);
    for (const Id id : pendingMarks_) {
        const auto* o = v->object(id);
        if (!o || (!showHidden_ && v->effectivelyHidden(*o))) continue;
        const Box b = hmi::edit::rotatedBounds(*o);
        const gfx::Point p = vp.toScreen(b.right(), b.y);
        ctx.r.fillRoundedRect({p.x - 5.5f, p.y - 5.5f, 11.f, 11.f}, white, 5.5f);
        ctx.r.fillRoundedRect({p.x - 4.f, p.y - 4.f, 8.f, 8.f}, orange, 4.f);
    }
}

std::string HmiCanvas::editTipAt(gfx::Point screen) const {
    const auto* v = view();
    if (!v || !editTip_) return {};
    const Pt p = toView(screen);
    const Id id = hmi::edit::hitTest(*v, p.x, p.y, insideGroup_, 4.0 / zoom_);
    return id == kNoId ? std::string{} : editTip_(id);
}

// La souris arretee une demi-seconde sur le meme objet (et pas pendant un glisser, une pose, un trace).
std::string HmiCanvas::liveTooltip(gfx::Point mouse) const {
    if (editTipOn_ && editTip_ && drag_ == Drag::None && !placeKind_ && traceMap_ == kNoId && hover_ != kNoId && hoverSince_ >= 0
        && paintTime_ - hoverSince_ >= 0.5)
        if (auto t = editTipAt(mouse); !t.empty()) return t;
    return Widget::liveTooltip(mouse);
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
    paintVertices(ctx, vp);   // 1.12.12 (lot H) : par-dessus les poignees du cadre
}

void HmiCanvas::onPaint(const ui::PaintContext& ctx) {
    paintTime_ = ctx.time;                       // 1.12.26 : le delai de l'infobulle de suivi
    if (hoverSince_ < 0) hoverSince_ = ctx.time;
    const auto r = bounds();
    ctx.r.fillRect(r, ctx.theme.color.panelBg);
    const auto* v = view();
    if (!v) {
        ctx.r.drawText({r.x + 16, r.y + 16}, "Cette vue n'existe plus.", ctx.theme.font.ui, ctx.theme.color.textMuted);
        return;
    }
    ctx.r.pushClip(drawArea());    // 1.12.30 : les regles se dessinent a part, par-dessus
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
    paintColumns(ctx, vp, *v);     // 1.12.30 : la grille de colonnes
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

    // Les guides (1.12.30 : caches, rien ; tire jusqu'a une regle, il est en rouge : lache, il part).
    const auto guideColor = gfx::Color::rgb(0x21B8C8);
    const bool overRuler = (drag_ == Drag::Guide || drag_ == Drag::NewGuide) && pointerIn_ && rulerAt(pointer_) != 0;
    for (std::size_t i = 0; i < v->guides.size() && !v->grid.guidesHidden; ++i) {
        const auto& g = v->guides[i];
        const bool dragged = drag_ == Drag::Guide && dragGuide_ == static_cast<int>(i);
        const auto col = dragged && (overRuler || g.position < 0 || g.position > (g.vertical ? v->width : v->height))
                             ? ctx.theme.color.error : v->grid.guidesLocked ? guideColor.withAlpha(150) : guideColor;
        const gfx::Point p = vp.toScreen(g.vertical ? g.position : 0, g.vertical ? 0 : g.position);
        if (g.vertical) for (float y = r.y; y < r.y + r.h; y += 6) ctx.r.fillRect({p.x, y, 1, 3}, col);
        else for (float x = r.x; x < r.x + r.w; x += 6) ctx.r.fillRect({x, p.y, 3, 1}, col);
    }
    // 1.12.30 : le guide qu'on tire d'une regle - plein, et sa position.
    if (drag_ == Drag::NewGuide && pointerIn_) {
        const auto col = overRuler ? ctx.theme.color.error : guideColor;
        const gfx::Point p = vp.toScreen(newGuideVertical_ ? newGuidePos_ : 0, newGuideVertical_ ? 0 : newGuidePos_);
        if (newGuideVertical_) ctx.r.fillRect({p.x, r.y, 1, r.h}, col);
        else ctx.r.fillRect({r.x, p.y, r.w, 1}, col);
        const std::string tag = overRuler ? std::string("l\xC3\xA2" "cher : pas de guide")
                                          : (newGuideVertical_ ? "x " : "y ") + fmt(newGuidePos_);
        const auto f = ctx.theme.font.smallUi;
        const float tw = ctx.r.measure(tag, f).width + 10.f, th = ctx.r.lineHeight(f) + 4.f;
        const gfx::Rect box = newGuideVertical_ ? gfx::Rect{p.x + 6.f, pointer_.y + 8.f, tw, th}
                                                : gfx::Rect{pointer_.x + 10.f, p.y + 6.f, tw, th};
        ctx.r.fillRoundedRect(box, col, 3.f);
        ctx.r.drawText({box.x + 5.f, box.y + 2.f}, tag, f, gfx::Color::rgb(0xFFFFFF));
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

    paintPendingDots(ctx, vp);   // 1.12.26
    paintSelection(ctx, vp);
    paintDimensions(ctx, vp);    // 1.12.30 : les cotes (Alt au survol, un deplacement)

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
        // 1.14.1 (N4) : une variante - sa taille, son nom.
        if (!placeVariant_.empty() && hmi::applyKindVariant(ghost, placeVariant_))
            for (const auto& kv : hmi::kindVariants(*placeKind_))
                if (kv.key == placeVariant_) ghostLabel = std::string(kv.label);
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
    paintRulers(ctx, vp, *v);    // 1.12.30 : par-dessus le dessin, hors de sa decoupe
}

// ---- 1.12.30 : la grille de colonnes --------------------------------------------------
void HmiCanvas::paintColumns(const ui::PaintContext& ctx, const HmiViewport& vp, const hmi::View& v) {
    if (!v.columns.visible) return;
    const auto spans = hmi::edit::columnSpans(v);
    const auto red = gfx::Color::rgb(0xE5484D);
    const gfx::Point top = vp.toScreen(0, 0), bottom = vp.toScreen(0, v.height);
    for (const auto& c : spans) {
        const float x0 = vp.toScreen(c.x, 0).x, x1 = vp.toScreen(c.x + c.w, 0).x;
        ctx.r.fillRect({x0, top.y, std::max(1.f, x1 - x0), bottom.y - top.y}, red.withAlpha(24));
        ctx.r.fillRect({x0, top.y, 1.f, bottom.y - top.y}, red.withAlpha(70));
        ctx.r.fillRect({x1 - 1.f, top.y, 1.f, bottom.y - top.y}, red.withAlpha(70));
    }
}

// ---- 1.12.30 : les cotes ----------------------------------------------------------------
void HmiCanvas::paintDimensions(const ui::PaintContext& ctx, const HmiViewport& vp) {
    dims_.clear();
    dimTarget_ = kNoId;
    const auto* v = view();
    if (!v || selection_.empty() || placeKind_ || traceMap_ != kNoId) return;
    const bool nudged = ctx.time - nudgedAt_ < 1.5;
    const bool altHover = altHeld_ && pointerIn_ && drag_ == Drag::None;
    const bool altMove = altHeld_ && drag_ == Drag::Move;
    if (!altHover && !altMove && !(nudged && drag_ == Drag::None)) return;
    if (altHover) dims_ = dimensionsAt(pointer_, &dimTarget_);
    else {
        const auto units = hmi::edit::units(*v, selection_);
        dims_ = hmi::edit::dimensions(*v, hmi::edit::selectionBounds(*v, units), units, kNoId);
    }
    if (dims_.empty()) return;
    const auto col = gfx::Color::rgb(0xE0409A);     // la couleur des cotes de l'espacement egal
    // L'objet vise : son cadre, de la couleur des cotes.
    if (dimTarget_ != kNoId)
        if (const auto* t = v->object(dimTarget_)) {
            std::vector<gfx::Point> pts;
            for (const auto& p : hmi::edit::corners(*t)) pts.push_back(vp.toScreen(p.x, p.y));
            shapes::strokePolyline(ctx.r, pts, true, col, 1.f);
        }
    const auto f = ctx.theme.font.smallUi;
    for (const auto& d : dims_) {
        const gfx::Point p0 = d.horizontal ? vp.toScreen(d.from, d.at) : vp.toScreen(d.at, d.from);
        const gfx::Point p1 = d.horizontal ? vp.toScreen(d.to, d.at) : vp.toScreen(d.at, d.to);
        // Les traits de rappel (pointilles), d'un bord jusqu'a la cote.
        const auto witness = [&](double along, double lo, double hi) {
            if (hi - lo < 0.5) return;
            const gfx::Point a = d.horizontal ? vp.toScreen(along, lo) : vp.toScreen(lo, along);
            const gfx::Point b = d.horizontal ? vp.toScreen(along, hi) : vp.toScreen(hi, along);
            if (d.horizontal) for (float y = a.y; y < b.y; y += 4) ctx.r.fillRect({a.x, y, 1, std::min(2.f, b.y - y)}, col.withAlpha(170));
            else for (float x = a.x; x < b.x; x += 4) ctx.r.fillRect({x, a.y, std::min(2.f, b.x - x), 1}, col.withAlpha(170));
        };
        witness(d.from, d.fromLo, d.fromHi);
        witness(d.to, d.toLo, d.toHi);
        // Le trait de cote : plein entre deux objets, en tirets jusqu'au bord de la vue.
        const float len = d.horizontal ? p1.x - p0.x : p1.y - p0.y;
        if (d.edge) {
            for (float t = 0; t < len; t += 7) {
                const float e = std::min(4.f, len - t);
                if (d.horizontal) ctx.r.fillRect({p0.x + t, p0.y, e, 1}, col);
                else ctx.r.fillRect({p0.x, p0.y + t, 1, e}, col);
            }
        } else {
            ctx.r.line(p0, p1, col, 1.f);
        }
        // Les fleches (assez de place : dedans), les petits traits des bouts.
        const float t = 4.f;
        if (d.horizontal) {
            ctx.r.line({p0.x, p0.y - t}, {p0.x, p0.y + t}, col, 1.f);
            ctx.r.line({p1.x, p1.y - t}, {p1.x, p1.y + t}, col, 1.f);
        } else {
            ctx.r.line({p0.x - t, p0.y}, {p0.x + t, p0.y}, col, 1.f);
            ctx.r.line({p1.x - t, p1.y}, {p1.x + t, p1.y}, col, 1.f);
        }
        if (len >= 18.f) {
            const float k = 5.f, wing = 3.f;
            std::vector<gfx::Point> a0, a1;
            if (d.horizontal) {
                a0 = {p0, {p0.x + k, p0.y - wing}, {p0.x + k, p0.y + wing}};
                a1 = {p1, {p1.x - k, p1.y - wing}, {p1.x - k, p1.y + wing}};
            } else {
                a0 = {p0, {p0.x - wing, p0.y + k}, {p0.x + wing, p0.y + k}};
                a1 = {p1, {p1.x - wing, p1.y - k}, {p1.x + wing, p1.y - k}};
            }
            shapes::fillPolygon(ctx.r, a0, col);
            shapes::fillPolygon(ctx.r, a1, col);
        }
        // Le nombre, lisible : sur une pastille, au milieu (a cote du trait s'il est court).
        const double value = d.value();
        const std::string label = std::fabs(value - std::round(value)) < 0.05 ? std::to_string(static_cast<long>(std::lround(value)))
                                                                              : fmt(value);
        const float lw = ctx.r.measure(label, f).width + 10.f, lh = ctx.r.lineHeight(f) + 4.f;
        // Le milieu de ce qui se voit du trait (une cote jusqu'au bord de la vue peut sortir du dessin).
        const auto area = drawArea();
        const float lo = d.horizontal ? std::max(p0.x, area.x) : std::max(p0.y, area.y);
        const float hi = d.horizontal ? std::min(p1.x, area.right()) : std::min(p1.y, area.bottom());
        if (hi <= lo) continue;
        const gfx::Point mid = d.horizontal ? gfx::Point{(lo + hi) / 2.f, p0.y} : gfx::Point{p0.x, (lo + hi) / 2.f};
        gfx::Rect pill = {mid.x - lw / 2.f, mid.y - lh / 2.f, lw, lh};
        if (d.horizontal && len < lw + 12.f) pill.y = mid.y - lh - 6.f;
        if (!d.horizontal && len < lh + 12.f) pill.x = mid.x + 8.f;
        ctx.r.fillRoundedRect(pill, col, lh / 2.f);
        ctx.r.drawText({pill.x + 5.f, pill.y + 2.f}, label, f, gfx::Color::rgb(0xFFFFFF));
    }
}

// ---- 1.12.30 : les regles ------------------------------------------------------------
void HmiCanvas::paintRulers(const ui::PaintContext& ctx, const HmiViewport& vp, const hmi::View& v) {
    if (!rulers_) return;
    const auto& c = ctx.theme.color;
    const auto top = topRuler(), left = leftRuler();
    const auto cr = contentRect();
    const gfx::Rect corner{cr.x, cr.y, kRuler, kRuler};
    ctx.r.pushClip(bounds());
    ctx.r.fillRect(top, c.headerBg);
    ctx.r.fillRect(left, c.headerBg);
    ctx.r.fillRect(corner, c.headerBg);
    // L'etendue de la vue sur chaque regle, un peu plus claire.
    const gfx::Point v0 = vp.toScreen(0, 0), v1 = vp.toScreen(v.width, v.height);
    const auto lit = c.panelBg;
    ctx.r.pushClip(top);
    ctx.r.fillRect({v0.x, top.y + 2.f, v1.x - v0.x, top.h - 2.f}, lit);
    ctx.r.popClip();
    ctx.r.pushClip(left);
    ctx.r.fillRect({left.x + 2.f, v0.y, left.w - 2.f, v1.y - v0.y}, lit);
    ctx.r.popClip();
    // L'etendue de la selection, en bleu leger.
    if (const auto* vv = view(); vv && !selection_.empty()) {
        const auto units = hmi::edit::units(*vv, selection_);
        if (!units.empty()) {
            const Box b = hmi::edit::selectionBounds(*vv, units);
            const gfx::Point s0 = vp.toScreen(b.x, b.y), s1 = vp.toScreen(b.right(), b.bottom());
            ctx.r.pushClip(top);
            ctx.r.fillRect({s0.x, top.y, std::max(1.f, s1.x - s0.x), top.h}, c.accent.withAlpha(60));
            ctx.r.popClip();
            ctx.r.pushClip(left);
            ctx.r.fillRect({left.x, s0.y, left.w, std::max(1.f, s1.y - s0.y)}, c.accent.withAlpha(60));
            ctx.r.popClip();
        }
    }
    // Les graduations : un pas majeur d'au moins 56 px a l'ecran (1, 2, 5, 10, 20, 25, 50...).
    double major = 1;
    for (const double m : {1.0, 2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) {
        major = m;
        if (m * vp.zoom >= 56.0) break;
    }
    const int parts = (major * vp.zoom >= 100.0) ? 10 : 5;
    const double minor = major / parts;
    const auto f = gfx::FontId{static_cast<std::uint16_t>(std::max<int>(9, ctx.theme.font.smallUi.v - 2))};
    const gfx::Color tick = c.textMuted;
    // La regle du haut.
    ctx.r.pushClip(top);
    {
        const double from = std::floor(vp.toViewX(top.x) / minor) * minor, to = vp.toViewX(top.right());
        int guard = 0;
        for (double x = from; x <= to && guard < 4000; x += minor, ++guard) {
            const float sx = vp.toScreen(x, 0).x;
            const bool isMajor = std::fabs(std::fmod(std::fabs(x) + 1e-6, major)) < minor * 0.5;
            // Le trait majeur s'arrete sous le nombre : "|500" ne se lit pas "1500".
            const float hgt = isMajor ? 9.f : (std::fabs(std::fmod(std::fabs(x) + 1e-6, major / 2)) < minor * 0.5 ? 6.f : 3.f);
            ctx.r.fillRect({std::round(sx), top.bottom() - hgt, 1.f, hgt}, tick);
            if (isMajor) ctx.r.drawText({std::round(sx) + 3.f, top.y + 1.f}, std::to_string(static_cast<long>(std::lround(x))), f, c.textMuted);
        }
    }
    ctx.r.popClip();
    // La regle de gauche : les nombres tournes, lus de bas en haut.
    ctx.r.pushClip(left);
    {
        const double from = std::floor(vp.toViewY(left.y) / minor) * minor, to = vp.toViewY(left.bottom());
        int guard = 0;
        for (double y = from; y <= to && guard < 4000; y += minor, ++guard) {
            const float sy = vp.toScreen(0, y).y;
            const bool isMajor = std::fabs(std::fmod(std::fabs(y) + 1e-6, major)) < minor * 0.5;
            const float wid = isMajor ? 9.f : (std::fabs(std::fmod(std::fabs(y) + 1e-6, major / 2)) < minor * 0.5 ? 6.f : 3.f);
            ctx.r.fillRect({left.right() - wid, std::round(sy), wid, 1.f}, tick);
            if (isMajor) {
                const std::string n = std::to_string(static_cast<long>(std::lround(y)));
                const float tw = ctx.r.measure(n, f).width;
                const gfx::Point o{left.x + 1.f, std::round(sy) + 3.f + tw};
                ctx.r.drawTextRotated(o, n, f, c.textMuted, -90.f, o);
            }
        }
    }
    ctx.r.popClip();
    // Les guides sur les regles : un petit triangle a leur place.
    if (!v.grid.guidesHidden) {
        const auto gc = gfx::Color::rgb(0x21B8C8).withAlpha(v.grid.guidesLocked ? 150 : 255);
        for (const auto& g : v.guides) {
            if (g.vertical) {
                const float x = vp.toScreen(g.position, 0).x;
                if (x >= top.x && x <= top.right())
                    shapes::fillPolygon(ctx.r, {{x - 4.f, top.bottom() - 6.f}, {x + 4.f, top.bottom() - 6.f}, {x, top.bottom()}}, gc);
            } else {
                const float y = vp.toScreen(0, g.position).y;
                if (y >= left.y && y <= left.bottom())
                    shapes::fillPolygon(ctx.r, {{left.right() - 6.f, y - 4.f}, {left.right() - 6.f, y + 4.f}, {left.right(), y}}, gc);
            }
        }
    }
    // La souris : un trait sur chaque regle.
    if (pointerIn_ && drawArea().contains(pointer_)) {
        ctx.r.fillRect({std::round(pointer_.x), top.y, 1.f, top.h}, c.accent);
        ctx.r.fillRect({left.x, std::round(pointer_.y), left.w, 1.f}, c.accent);
    }
    // Les bords, et le coin : l'unite.
    ctx.r.fillRect({top.x, top.bottom() - 1.f, top.w, 1.f}, c.border);
    ctx.r.fillRect({left.right() - 1.f, left.y, 1.f, left.h}, c.border);
    ctx.r.fillRect({corner.x, corner.bottom() - 1.f, corner.w, 1.f}, c.border);
    ctx.r.fillRect({corner.right() - 1.f, corner.y, 1.f, corner.h}, c.border);
    ctx.r.drawText({corner.x + 4.f, corner.y + 3.f}, "px", f, c.textMuted);
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
    // 1.14.1 (N4) : une variante de la bibliotheque (la Calculatrice scientifique) - son nom, ses reglages.
    std::string what(hmi::kindLabel(kind));
    const std::string variant = symbol.empty() ? placeVariant_ : std::string{};
    for (const auto& kv : hmi::kindVariants(kind))
        if (kv.key == variant) what = std::string(kv.label);
    edit(symbol.empty() ? "Poser " + what : "Poser le symbole " + symbol,
         [&](hmi::Project& p, hmi::View& vv) {
        made = symbol.empty() ? hmi::edit::add(p, vv, kind, x, y) : hmi::placeSymbol(p, vv, symbol, x, y);
        if (auto* fresh = !variant.empty() ? vv.object(made) : nullptr) (void)hmi::applyKindVariant(*fresh, variant);
        // 1.12.10 (lot F) : un objet a echelle pose ici suit la plage de sa variable (Min, Max vides).
        if (auto* fresh = symbol.empty() ? vv.object(made) : nullptr) hmi::ranges::followVariable(*fresh);
        adoptIn(vv);
    });
    if (!keep) { placeKind_.reset(); placeSymbol_.clear(); placeVariant_.clear(); }
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

    // 1.12.30 : Alt tenu (les cotes au survol) - suivi par la souris et par les touches,
    // sans rien prendre.
    {
        bool alt = altHeld_;
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) alt = m->mods.alt;
        else if (const auto* k = std::get_if<ui::KeyDown>(&ev)) alt = k->mods.alt;
        else if (const auto* u = std::get_if<ui::KeyUp>(&ev)) alt = u->mods.alt;
        else if (std::holds_alternative<ui::FocusChange>(ev)) alt = false;
        if (alt != altHeld_) {
            altHeld_ = alt;
            invalidate();
            if (pointerIn_ && drag_ == Drag::None) emitStatus(pointer_);
        }
    }

    // 1.12.27 : du texte lache sur la vue - les objets glisses depuis une autre fenetre
    // (leur copie publiee) se posent la ; une colonne de noms y devient des etiquettes
    // et des afficheurs (comme Ctrl+V).
    if (const auto* t = std::get_if<ui::TextDropped>(&ev)) {
        if (!bounds().contains(t->pos)) return ui::EventResult::Ignored;
        Pt at = toView(t->pos);
        if (v->grid.snapGrid) {
            at.x = hmi::edit::snapToGrid(at.x, v->grid.step);
            at.y = hmi::edit::snapToGrid(at.y, v->grid.step);
        }
        if (pasteShared(t->text, at)) return ui::EventResult::Consumed;
        if (partage::listeObjets(t->text)) {
            status->emit("L\xC3\xA2" "cher : les objets d'une autre fen\xC3\xAAtre, mais leur copie n'est plus l\xC3\xA0 : rien n'est pos\xC3\xA9.");
            return ui::EventResult::Consumed;
        }
        pressView_ = at;
        if (pasteNames(t->text) == 0)
            status->emit("L\xC3\xA2" "cher : ni des objets d'une autre fen\xC3\xAAtre, ni des noms de variables.");
        return ui::EventResult::Consumed;
    }

    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        grabFocus();
        downHere_ = true;
        pressScreen_ = m->pos;
        pressView_ = toView(m->pos);
        const bool shift = m->mods.shift, ctrl = m->mods.ctrl;

        // 1.12.30 : LES REGLES. Le clic droit : leur menu (les regles, les guides, les colonnes) ;
        // un appui a gauche sur celle du haut (de gauche) tire un guide horizontal (vertical).
        if (const int ruler = rulerAt(m->pos); ruler != 0) {
            if (m->button == ui::MouseButton::Right) {
                if (placeKind_ || traceMap_ != kNoId) cancelPlacement();
                contextScreen_ = m->pos;
                contextLocked_ = kNoId;
                contextTarget_ = ContextTarget::Ruler;
                contextRequested->emit(toSurface(m->pos));   // 1.12.31 : dans la surface (un ilot : la meme)
                return ui::EventResult::Consumed;
            }
            if (m->button != ui::MouseButton::Left || ruler == 3) return ui::EventResult::Consumed;
            if (v->grid.guidesLocked) {
                status->emit("Les guides sont verrouill\xC3\xA9s : clic droit sur la r\xC3\xA8gle, D\xC3\xA9verrouiller les guides");
                return ui::EventResult::Consumed;
            }
            newGuideVertical_ = ruler == 2;
            newGuidePos_ = newGuideVertical_ ? pressView_.x : pressView_.y;
            ++dragSerial_;
            drag_ = Drag::NewGuide;
            invalidate();
            return ui::EventResult::Consumed;
        }

        if (m->button == ui::MouseButton::Middle || (m->button == ui::MouseButton::Left && spaceDown_)) {
            drag_ = Drag::Pan;
            return ui::EventResult::Consumed;
        }
        // 1.10.2 (chantier D) : le clic droit - le menu de l'editeur. 1.12.18 : il vise ce qui
        // est sous le pointeur (HmiCanvas.hpp, « ce que vise le clic droit »).
        if (m->button == ui::MouseButton::Right) {
            // Une pose ou un trace en cours : le clic droit l'annule, comme Echap, sans menu.
            if (placeKind_ || traceMap_ != kNoId) {
                cancelPlacement();
                return ui::EventResult::Consumed;
            }
            contextScreen_ = m->pos;
            contextLocked_ = kNoId;
            if (const hmi::Id hit = hmi::edit::hitTest(*v, pressView_.x, pressView_.y, insideGroup_, 4.0 / zoom_); hit != kNoId) {
                if (std::find(selection_.begin(), selection_.end(), hit) == selection_.end()) setSelection({hit});
                contextTarget_ = ContextTarget::Selection;
            } else if (const hmi::Id locked = hmi::edit::hitTest(*v, pressView_.x, pressView_.y, insideGroup_, 4.0 / zoom_, true);
                       locked != kNoId) {
                // Bug 4 : l'ancienne selection n'est plus visee (Supprimer la supprimait).
                if (!selection_.empty()) setSelection({});
                contextTarget_ = ContextTarget::Locked;
                contextLocked_ = locked;
            } else {
                if (!selection_.empty()) setSelection({});
                contextTarget_ = ContextTarget::View;
            }
            // 1.12.12 (lot H) : sur un sommet, le menu peut le retirer.
            if (const auto t = vertexTarget(); t.valid())
                if (const int k = hmi::vertices::vertexAt(*v, t, pressView_, (kVertex + 3.0) / zoom_); k >= 0) selectVertex(k);
            contextRequested->emit(toSurface(m->pos));   // 1.12.31 : dans la surface
            return ui::EventResult::Consumed;
        }
        if (m->button != ui::MouseButton::Left) return ui::EventResult::Consumed;

        if (placeKind_) {
            placeAt(m->pos, shift);
            return ui::EventResult::Consumed;
        }

        // 1.12.12 (lot H) : une zone qui se trace - chaque clic pose un point ; un double-clic, ou un clic sur
        // le premier point, la ferme.
        if (traceMap_ != kNoId) {
            traceClick(m->pos, m->clickCount, m->mods.alt);
            return ui::EventResult::Consumed;
        }
        // 1.12.12 (lot H) : un sommet (avant les poignees du cadre, qui peuvent tomber dessus) ; un double-clic
        // sur un segment en insere un.
        if (const auto t = vertexTarget(); t.valid()) {
            if (const int k = hmi::vertices::vertexAt(*v, t, pressView_, (kVertex + 3.0) / zoom_); k >= 0) {
                selectVertex(k);
                const auto pts = hmi::vertices::points(*v, t);
                grab_ = {pts[static_cast<std::size_t>(k)].x - pressView_.x, pts[static_cast<std::size_t>(k)].y - pressView_.y};
                snapshot_ = *v;
                ++dragSerial_;
                dragTarget_ = t;
                drag_ = Drag::Vertex;
                return ui::EventResult::Consumed;
            }
            hmi::edit::Pt foot{};
            if (m->clickCount >= 2)
                if (const int seg = hmi::vertices::segmentAt(*v, t, pressView_, 5.0 / zoom_, &foot); seg >= 0) {
                    int made = -1;
                    (void)edit("Ajouter un sommet", [&](hmi::Project&, hmi::View& vv) {
                        made = hmi::vertices::insertVertex(vv, t, static_cast<std::size_t>(seg), foot);
                    });
                    if (made >= 0) selectVertex(made);
                    return ui::EventResult::Consumed;
                }
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
            if (vertex_ >= 0) selectVertex(-1);   // 1.12.12 (lot H) : l'objet, pas son sommet
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
            dragOutOk_ = !m->mods.alt;     // 1.12.27 : Alt pose une copie ici : elle ne sort pas
            moveSegments_ = 0;
            lastMoveAt_ = -1;
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
                if (h != hover_) { hover_ = h; hoverSince_ = -1; invalidate(); }   // 1.12.26 : l'infobulle de suivi repart
                if (placeKind_ || traceMap_ != kNoId) invalidate();   // 1.12.12 (lot H) : la zone qui se trace
                break;
            }
            // 1.12.12 (lot H) : un sommet glisse - recalcule depuis l'etat du depart, un seul pas d'annulation.
            case Drag::Vertex: {
                const auto t = dragTarget_;
                const hmi::View start = snapshot_;
                const int k = vertex_;
                hmi::edit::Pt target{p.x + grab_.x, p.y + grab_.y};
                snapX_.clear();
                snapY_.clear();
                // La reference d'une courbe XY : ses valeurs s'arrondissent (un millieme de l'axe), sans magnetisme.
                if (!m->mods.alt && t.what != hmi::vertices::Target::What::Reference) {
                    auto siblings = hmi::vertices::points(start, t);
                    if (k >= 0 && static_cast<std::size_t>(k) < siblings.size()) siblings.erase(siblings.begin() + k);
                    const auto snap = hmi::vertices::snapPoint(start, target, siblings, t.object, 6.0 / zoom_);
                    target = snap.at;
                    snapX_ = snap.xLines;
                    snapY_ = snap.yLines;
                }
                if (k >= 0)
                    (void)edit("D\xC3\xA9placer le sommet", [&](hmi::Project&, hmi::View& vv) {
                        vv = start;
                        (void)hmi::vertices::moveVertex(vv, t, static_cast<std::size_t>(k), target);
                    }, "vertex:" + std::to_string(dragSerial_));
                break;
            }
            case Drag::Pan:
                panX_ += m->delta.x;
                panY_ += m->delta.y;
                invalidate();
                break;
            case Drag::Move: {
                // 1.12.27 : tire hors de la fenetre - vers une autre fenetre d'IHM Studio.
                if (dragOutOk_ && ui::canDragText()) {
                    const auto s = ui::surfaceSize();
                    if (s.w > 0 && s.h > 0 && (m->pos.x < 0 || m->pos.y < 0 || m->pos.x >= s.w || m->pos.y >= s.h)) {
                        dragOut();
                        return ui::EventResult::Consumed;
                    }
                }
                hmi::edit::SnapResult snap;
                snap.dx = dx;
                snap.dy = dy;
                if (!m->mods.alt) snap = hmi::edit::snapMove(snapshot_, startBox_, dx, dy, selection_, 6.0 / zoom_);
                snapX_ = snap.xLines;
                snapY_ = snap.yLines;
                spacing_ = snap.spacing;
                const auto sel = selection_;
                const hmi::View start = snapshot_;
                if (edit("D\xC3\xA9placer", [&](hmi::Project&, hmi::View& vv) {
                    vv = start;
                    hmi::edit::move(vv, sel, snap.dx, snap.dy);
                }, "move:" + std::to_string(dragSerial_))) {
                    // 1.12.27 : un arret de plus de 2 s et la pile commence un autre pas (dragOut).
                    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    if (moveSegments_ == 0 || now - lastMoveAt_ > 2.0) ++moveSegments_;
                    lastMoveAt_ = now;
                }
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
            case Drag::NewGuide: {
                // 1.12.30 : le guide tire d'une regle suit la souris (au pas de la grille).
                double pos = newGuideVertical_ ? p.x : p.y;
                if (v->grid.snapGrid) pos = hmi::edit::snapToGrid(pos, v->grid.step);
                newGuidePos_ = std::round(pos);
                invalidate();
                break;
            }
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
        if (was == Drag::NewGuide) {
            // 1.12.30 : lache sur la place du dessin, le guide est pose (un pas d'annulation) ;
            // ramene sur une regle (ou hors du canevas), il n'y en a pas.
            if (rulerAt(m->pos) != 0 || !drawArea().contains(m->pos)) {
                status->emit("Pas de guide : l\xC3\xA2" "ch\xC3\xA9 sur la r\xC3\xA8gle");
            } else {
                const bool vertical = newGuideVertical_;
                const double pos = newGuidePos_;
                edit(vertical ? "Guide vertical" : "Guide horizontal", [&](hmi::Project&, hmi::View& vv) {
                    vv.guides.push_back({vertical, pos});
                    vv.grid.guidesHidden = false;
                });
                status->emit(std::string(vertical ? "Guide vertical \xC3\xA0 x = " : "Guide horizontal \xC3\xA0 y = ") + fmt(pos)
                             + " : le ramener sur la r\xC3\xA8gle le retire");
            }
        }
        if (was == Drag::Guide && dragGuide_ >= 0) {
            // Un guide tire hors de la vue est retire ; 1.12.30 : ramene sur une regle aussi.
            const auto* cur = view();
            if (cur && static_cast<std::size_t>(dragGuide_) < cur->guides.size()) {
                const auto& g = cur->guides[static_cast<std::size_t>(dragGuide_)];
                if (rulerAt(m->pos) != 0 || g.position < 0 || g.position > (g.vertical ? cur->width : cur->height)) {
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
        // 1.12.18 : Maj+F10 / Menu - le menu de la selection, sous elle (rien de choisi : la vue).
        if (ui::isContextMenuKey(*k)) {
            if (placeKind_ || traceMap_ != kNoId) cancelPlacement();
            contextLocked_ = kNoId;
            const auto vp = viewport();
            if (!selection_.empty()) {
                const Box b = hmi::edit::selectionBounds(*v, hmi::edit::units(*v, selection_));
                contextScreen_ = vp.toScreen(b.x + 8.0 / zoom_, b.bottom());
                contextTarget_ = ContextTarget::Selection;
            } else {
                contextScreen_ = {bounds().x + bounds().w / 2.f, bounds().y + bounds().h / 2.f};
                contextTarget_ = ContextTarget::View;
            }
            contextScreen_.x = std::clamp(contextScreen_.x, bounds().x + 4.f, std::max(bounds().x + 4.f, bounds().right() - 4.f));
            contextScreen_.y = std::clamp(contextScreen_.y, bounds().y + 4.f, std::max(bounds().y + 4.f, bounds().bottom() - 4.f));
            ui::KeyboardMenuScope keyboard;
            contextRequested->emit(contextScreen_);
            return ui::EventResult::Consumed;
        }
        const double step = k->mods.shift ? std::max(1, v->grid.step) : 1.0;
        switch (k->key) {
            case ui::Key::Space:  spaceDown_ = true; return ui::EventResult::Consumed;
            case ui::Key::Delete:
            case ui::Key::Backspace:
                // 1.12.12 (lot H) : un sommet choisi se retire (l'objet reste, meme s'il n'en a plus assez).
                if (vertex_ >= 0 && vertexTarget().valid()) {
                    (void)removeSelectedVertex();
                    return ui::EventResult::Consumed;
                }
                deleteSelection();
                return ui::EventResult::Consumed;
            case ui::Key::Left:  nudge(-step, 0); return ui::EventResult::Consumed;
            case ui::Key::Right: nudge(step, 0);  return ui::EventResult::Consumed;
            case ui::Key::Up:    nudge(0, -step); return ui::EventResult::Consumed;
            case ui::Key::Down:  nudge(0, step);  return ui::EventResult::Consumed;
            case ui::Key::Escape:
                // 1.12.12 (lot H) : la zone qui se trace s'abandonne ; un sommet choisi se laisse.
                if (traceMap_ != kNoId) cancelPlacement();
                else if (vertex_ >= 0) selectVertex(-1);
                else if (placeKind_) cancelPlacement();
                else if (insideGroup_ != kNoId) leaveGroup();
                else setSelection({});
                return ui::EventResult::Consumed;
            case ui::Key::Return:
                // 1.12.18 : Entree ouvre le symbole d'une instance (comme le double-clic), entre
                // dans un groupe ou un conteneur.
                if (selection_.size() == 1) {
                    if (const auto* o = v->object(selection_.front()); o && o->kind == hmi::Kind::SymbolInstance)
                        symbolOpened->emit(o->text("symbol"));
                    else
                        enterGroup(selection_.front());
                }
                return ui::EventResult::Consumed;
            case ui::Key::F2:
                if (k->mods.none() && !selection_.empty()) { renameRequested->emit(); return ui::EventResult::Consumed; }
                break;
            case ui::Key::A: if (k->mods.ctrl) { selectAll(); return ui::EventResult::Consumed; } break;
            // 1.12.30 : Ctrl+R les regles ; Ctrl+; cacher les guides, Ctrl+Alt+; les verrouiller.
            case ui::Key::R:
                if (k->mods.ctrl && !k->mods.alt && !k->mods.shift) {
                    setRulersShown(!rulers_);
                    rulersToggled->emit(rulers_);
                    return ui::EventResult::Consumed;
                }
                break;
            case ui::Key::Semicolon:
                if (k->mods.ctrl && !k->mods.shift) {
                    if (k->mods.alt) toggleGuidesLocked();
                    else toggleGuidesHidden();
                    return ui::EventResult::Consumed;
                }
                break;
            case ui::Key::Z: if (k->mods.ctrl) { historyRequested->emit(k->mods.shift); return ui::EventResult::Consumed; } break;
            case ui::Key::Y: if (k->mods.ctrl) { historyRequested->emit(true); return ui::EventResult::Consumed; } break;
            // 1.12.18 : un mot, un raccourci - le style passe sur Ctrl+Alt+C / Ctrl+Alt+V
            // (Ctrl+Maj+C reste « Copier sans les titres », dans les tableaux).
            case ui::Key::C:
                if (k->mods.ctrl && k->mods.alt) { copyStyle(); return ui::EventResult::Consumed; }
                if (k->mods.ctrl && !k->mods.shift) { copySelection(); return ui::EventResult::Consumed; }
                break;
            case ui::Key::X: if (k->mods.ctrl) { cutSelection(); return ui::EventResult::Consumed; } break;
            // 1.12.18 : Ctrl+D duplique tout de suite, partout ; la fenetre « Dupliquer... »
            // passe sur Ctrl+Maj+D (1.10.2 : c'etait l'inverse).
            case ui::Key::D:
                if (k->mods.ctrl && !k->mods.alt && !selection_.empty()) {
                    if (k->mods.shift) duplicateRequested->emit();
                    else duplicateSelection();
                    return ui::EventResult::Consumed;
                }
                break;
            // 1.12.18 : Ctrl+G groupe, Ctrl+Maj+G degroupe (le code garde Aller a la ligne).
            case ui::Key::G:
                if (k->mods.ctrl && !k->mods.alt) {
                    if (k->mods.shift) ungroupSelection();
                    else if (selection_.size() >= 2) groupSelection();
                    return ui::EventResult::Consumed;
                }
                break;
            case ui::Key::V:
                if (k->mods.ctrl && k->mods.alt) { pasteStyle(); return ui::EventResult::Consumed; }
                if (k->mods.ctrl && !k->mods.shift) { paste(); return ui::EventResult::Consumed; }
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
