// =============================================================================
//  web/WebCanvas.cpp - 1.12.31 : voir WebCanvas.hpp
// -----------------------------------------------------------------------------
//  Le meme chemin que la simulation et le poste d'exploitation de l'appli
//  (app/hmi/HmiSimulation.cpp : HmiLayerBuilder::build, HmiLiveCanvas::onPaint,
//  HmiLiveCanvas::pressObject), sans les widgets de l'ecran : ce sont ces trois
//  morceaux, repris pour la page. Les fonctions de geometrie et de clic sont
//  celles du moteur (src/hmi) : un clic touche ici ce qu'il toucherait la-bas.
// =============================================================================
#ifdef XPG_WEB

#include "WebCanvas.hpp"

#include "../hmi/HmiAlarmViews.hpp"
#include "../hmi/HmiChoiceLists.hpp"
#include "../hmi/HmiComm.hpp"
#include "../hmi/HmiControls.hpp"
#include "../hmi/HmiDisplay.hpp"
#include "../hmi/HmiExpr.hpp"
#include "../hmi/HmiForms.hpp"
#include "../hmi/HmiLanguages.hpp"
#include "../hmi/HmiMarkers.hpp"
#include "../hmi/HmiNavigation.hpp"
#include "../hmi/HmiProduction.hpp"
#include "../hmi/HmiTable.hpp"
#include "../hmi/HmiWidgets.hpp"

#include <algorithm>
#include <cmath>

namespace app::web {

using hmi::Id;
using hmi::kNoId;

namespace {
gfx::Color faded(gfx::Color c, float a) {
    c.a = static_cast<std::uint8_t>(std::clamp(static_cast<float>(c.a) * a, 0.f, 255.f));
    return c;
}

std::vector<hmi::Crumb> trailOf(const hmi::Object& o, const hmi::Runtime* rt, const hmi::Project* p) {
    if (!rt || !p) return {};
    const auto* cur = p->view(rt->currentView());
    const auto* home = p->view(rt->homeView());
    return hmi::breadcrumbTrail(*p, o, cur ? cur->name : std::string{}, rt->historyNames(), home ? home->name : std::string{});
}

std::string lot12PartAt(const hmi::View& view, const hmi::Object& o, double w, double h, double lx, double ly,
                        const hmi::Runtime* rt, const hmi::Project* p) {
    switch (o.kind) {
        case hmi::Kind::NavBar: return hmi::navBarHit(o, w, h, lx, ly, hmi::navItems(p, o).size());
        case hmi::Kind::Breadcrumb: return hmi::breadcrumbHit(o, w, h, lx, ly, trailOf(o, rt, p));
        case hmi::Kind::TabContainer: return hmi::tabHit(o, w, h, lx, ly, hmi::tabLabels(o).size());
        case hmi::Kind::ScrollPanel: return hmi::scrollHit(hmi::scrollLayout(view, o), lx, ly);
        case hmi::Kind::CollapsiblePanel: return hmi::collapsibleHit(o, w, h, lx, ly);
        case hmi::Kind::ZoneMap: return hmi::zoneMapHit(o, w, h, lx, ly);
        default: return {};
    }
}
} // namespace

// ============================================================ les vues evaluees ==
//  HmiLayerBuilder::build : la vue courante (ou les deux d'une transition), puis
//  les popups ouvertes - chacune evaluee contre le moteur, ses structures rangees.
void WebCanvas::buildLayers(double now) {
    layers_.clear();
    if (!rt_ || !project_ || !rt_->running()) return;
    auto& env = rt_->environment();
    const auto evaluate = [&](Id viewId) -> std::optional<hmi::View> {
        const auto* v = rt_->composedView(viewId);
        if (!v) return std::nullopt;
        auto it = live_.find(viewId);
        const auto options = rt_->displayOptions();
        if (!hmi::plainDisplay(*project_, options)) {
            auto& o = originals_[viewId];
            if (it == live_.end() || !(o.second == options) || !(o.first == *v)) {
                o = {*v, options};
                if (it == live_.end()) it = live_.emplace(viewId, hmi::LiveView{}).first;
                it->second.bind(hmi::displayView(*v, *project_, options));
            }
        } else {
            originals_.erase(viewId);
            if (it == live_.end()) {
                it = live_.emplace(viewId, hmi::LiveView{}).first;
                it->second.bind(*v);
            } else if (!(it->second.source() == *v)) {
                it->second.bind(*v);
            }
        }
        std::vector<hmi::LiveValue> values;
        return it->second.evaluate(env, &values, rt_->viewScope(viewId), now - rt_->startedAt());
    };

    const Id top = rt_->topView();
    const auto layerOf = [&](Id viewId, hmi::Frame frame, bool popup, float veil) {
        auto shown = evaluate(viewId);
        if (!shown) return;
        Layer l;
        l.view = std::move(*shown);
        hmi::layoutStructures(l.view);
        l.frame = frame;
        l.popup = popup;
        l.veil = veil;
        if (popup) {
            if (const auto* pv = project_->view(viewId)) {
                auto ps = hmi::popupSettingsOf(*pv);
                ps.title = hmi::translateText(project_->languages, rt_->language(), ps.title);
                l.titleBar = ps.titleBar;
                l.title = ps.title;
                if (ps.title.find('{') != std::string::npos) {
                    auto t = titles_.find(ps.title);
                    if (t == titles_.end()) t = titles_.emplace(ps.title, hmi::TextTemplate::compile(ps.title)).first;
                    l.title = t->second.render(env, rt_->viewScope(viewId));
                }
                l.closeButton = ps.titleBar && ps.closeButton;
                l.modal = ps.modal;
                l.movable = ps.titleBar && ps.movable;
                l.closeOutside = ps.closeOutside;
            }
            const auto& slots = rt_->popupSlots();
            for (std::size_t k = 0; k < slots.size(); ++k)
                if (slots[k].view == viewId) {
                    l.slot = static_cast<int>(k);
                    l.px = slots[k].x;
                    l.py = slots[k].y;
                    lastPlaces_[viewId] = {slots[k].x, slots[k].y};
                }
            if (l.slot < 0)
                if (const auto it = lastPlaces_.find(viewId); it != lastPlaces_.end()) {
                    l.slot = -2;
                    l.px = it->second.first;
                    l.py = it->second.second;
                }
        }
        // La securite : ce que l'utilisateur connecte ne peut pas actionner (un voile, pas de clic).
        if (project_->security.enabled && viewId == top)
            if (const auto* v = rt_->composedView(viewId))
                for (const auto& o : v->objects) {
                    if (const hmi::Object* s = l.view.object(o.id); s && !s->flag("visible", true)) continue;
                    const bool writes = hmi::kindWritesVariable(o.kind);
                    const bool acts = !o.actions.empty() || writes || o.kind == hmi::Kind::SystemButton || o.kind == hmi::Kind::LoginMenuButton;
                    if (acts && !rt_->objectAllowed(o)) l.locked.push_back(o.id);
                    else if (writes && !rt_->permitted("Piloter")) l.locked.push_back(o.id);
                }
        layers_.push_back(std::move(l));
    };

    const auto& anim = rt_->animation();
    const double p = rt_->animationProgress(now);
    if (anim && !anim->popup) {
        layerOf(anim->from, hmi::outgoingFrame(anim->spec, p), false, 0.f);
        layerOf(anim->to, hmi::incomingFrame(anim->spec, p), false, 0.f);
    } else {
        layerOf(rt_->currentView(), hmi::Frame{}, false, 0.f);
    }
    for (const Id popupId : rt_->popups()) {
        hmi::Frame f;
        if (anim && anim->popup && !anim->closing && anim->to == popupId) f = hmi::incomingFrame(anim->spec, p);
        layerOf(popupId, f, true, static_cast<float>(0.45 * f.opacity));
    }
    if (anim && anim->popup && anim->closing
        && std::find(rt_->popups().begin(), rt_->popups().end(), anim->from) == rt_->popups().end()) {
        const auto f = hmi::outgoingFrame(anim->spec, p);
        layerOf(anim->from, f, true, static_cast<float>(0.45 * f.opacity));
    }
}

// ================================================================== le dessin ==
//  HmiLiveCanvas::onPaint, le poste d'exploitation : la vue sur toute la surface,
//  selon le reglage du poste ; une popup posee dans la vue du dessous, a la meme
//  echelle, sur un voile, avec sa barre de titre.
void WebCanvas::paint(WebRenderer& r, float w, float h, double time) {
    r.begin(w, h);
    buildLayers(time);
    vps_.clear();
    popupRects_.clear();
    const gfx::Rect b{0.f, 0.f, w, h};
    r.fillRect(b, gfx::Color{0, 0, 0, 255});
    if (layers_.empty() || layers_.front().view.width <= 0 || layers_.front().view.height <= 0) {
        r.drawText({24.f, 24.f}, rt_ && rt_->running() ? "Vue introuvable" : "L'IHM d\xC3\xA9marre\xE2\x80\xA6", theme_.font.ui,
                   gfx::Color::rgb(0xC8D0DC));
        return;
    }
    const hmi::ScreenFit fitMode = project_ ? project_->station.screenFit : hmi::ScreenFit::Fill;
    const auto& v0 = layers_.front().view;
    float sx = 1.f, sy = 1.f;
    if (fitMode == hmi::ScreenFit::Fill) {
        const float zx = b.w / static_cast<float>(std::max(1, v0.width)), zy = b.h / static_cast<float>(std::max(1, v0.height));
        const float z0 = std::min(zx, zy);
        if (z0 > 0.f) {
            sx = std::max(1.f, zx / z0);
            sy = std::max(1.f, zy / z0);
        }
    }
    const gfx::Rect fitArea{b.x, b.y, b.w / sx, b.h / sy};
    const auto stretchX = [&](float x) { return b.x + (x - b.x) * sx; };
    const auto stretchY = [&](float y) { return b.y + (y - b.y) * sy; };
    const auto fit = [&](const hmi::View& v) {
        if (fitMode == hmi::ScreenFit::Actual) return 1.f;
        return std::max(0.05f, std::min(fitArea.w / static_cast<float>(std::max(1, v.width)), fitArea.h / static_cast<float>(std::max(1, v.height))));
    };
    float baseZoom = 1.f;
    gfx::Point baseOrigin{};
    gfx::Rect baseRect{};
    const auto display = rt_->displayOptions();
    for (std::size_t li = 0; li < layers_.size(); ++li) {
        const auto& layer = layers_[li];
        const auto& v = layer.view;
        const float titleH = layer.popup && layer.titleBar ? static_cast<float>(hmi::kPopupTitleHeight) : 0.f;
        float z = 1.f, ox = 0.f, oy = 0.f;
        if (!layer.popup) {
            z = fit(v);
            ox = fitArea.x + (fitArea.w - static_cast<float>(v.width) * z) / 2.f;
            oy = fitArea.y + (fitArea.h - static_cast<float>(v.height) * z) / 2.f;
            baseZoom = z;
            baseOrigin = {ox, oy};
            baseRect = {stretchX(ox), stretchY(oy), static_cast<float>(v.width) * z * sx, static_cast<float>(v.height) * z * sy};
        } else {
            z = baseZoom;
            if (layer.slot != -1) {
                ox = baseOrigin.x + static_cast<float>(layer.px) * z;
                oy = baseOrigin.y + (static_cast<float>(layer.py) + titleH) * z;
            } else {
                ox = fitArea.x + (fitArea.w - static_cast<float>(v.width) * z) / 2.f;
                oy = fitArea.y + (fitArea.h - static_cast<float>(v.height) * z) / 2.f;
            }
            if (layer.modal && layer.veil > 0.f)
                r.fillRect(baseRect, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(std::clamp(layer.veil, 0.f, 1.f) * 150.f)});
        }
        const float vw0 = static_cast<float>(v.width) * z, vh0 = static_cast<float>(v.height) * z;
        const float cx = ox + vw0 / 2.f + static_cast<float>(layer.frame.offsetX) * vw0;
        const float cy = oy + vh0 / 2.f + static_cast<float>(layer.frame.offsetY) * vh0;
        HmiViewport vp;
        vp.zoom = z * static_cast<float>(layer.frame.scale);
        vp.stretchX = sx;
        vp.stretchY = sy;
        vp.originX = stretchX(cx) - static_cast<float>(v.width) * vp.zoomX() / 2.f;
        vp.originY = stretchY(cy) - static_cast<float>(v.height) * vp.zoomY() / 2.f;
        vp.angle = static_cast<float>(layer.frame.angle);
        vp.pivotX = stretchX(cx);
        vp.pivotY = stretchY(cy);
        vps_.push_back(vp);
        if (layer.frame.opacity <= 0.001) continue;
        const float op = static_cast<float>(layer.frame.opacity);
        HmiPaintOptions opt;
        opt.editor = false;
        opt.time = time;
        opt.assets = project_ ? &project_->assets : nullptr;
        opt.alpha = op;
        opt.runtime = rt_;
        opt.history = history_;
        opt.project = project_;
        opt.display = &display;
        const gfx::Point a = vp.toScreen(0, 0);
        const float vw = static_cast<float>(v.width) * vp.zoomX(), vh = static_cast<float>(v.height) * vp.zoomY();
        const float tb = titleH * vp.zoomY();
        if (layer.popup && vp.angle == 0.f) {
            r.fillRect({a.x + 6, a.y - tb + 8, vw, vh + tb}, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(90 * op)});
            if (tb > 0.f) {
                const gfx::Rect bar{a.x, a.y - tb, vw, tb};
                r.fillRect(bar, faded(hmiSeenColor(gfx::Color::rgb(0x273142), &display), op));
                const auto& f = theme_.font;
                const std::string title = layer.title.empty() ? v.name : layer.title;
                const float room = vw - 20.f - (layer.closeButton ? tb : 0.f);
                const auto chars = r.fitCharacters(title, f.ui, std::max(0.f, room));
                r.drawText({bar.x + 10.f, bar.y + (tb - r.lineHeight(f.ui)) / 2.f}, std::string_view(title).substr(0, chars), f.ui,
                           faded(hmiSeenColor(gfx::Color::rgb(0xE6EAF0), &display), op));
                gfx::Rect close{};
                if (layer.closeButton) {
                    close = {bar.right() - tb, bar.y, tb, tb};
                    const float m = tb * 0.32f;
                    const gfx::Color xc = faded(hmiSeenColor(gfx::Color::rgb(0xC8D0DC), &display), op);
                    r.line({close.x + m, close.y + m}, {close.right() - m, close.bottom() - m}, xc, 1.6f);
                    r.line({close.right() - m, close.y + m}, {close.x + m, close.bottom() - m}, xc, 1.6f);
                }
                if (layer.slot >= 0) {
                    PopupRect pr;
                    pr.window = {a.x, a.y - tb, vw, vh + tb};
                    pr.title = {bar.x, bar.y, bar.w - (layer.closeButton ? tb : 0.f), tb};
                    pr.close = close;
                    pr.content = {a.x, a.y, vw, vh};
                    pr.slot = layer.slot;
                    pr.modal = layer.modal;
                    pr.movable = layer.movable;
                    pr.closeOutside = layer.closeOutside;
                    pr.layer = li;
                    popupRects_.push_back(pr);
                }
            } else if (layer.slot >= 0) {
                PopupRect pr;
                pr.window = pr.content = {a.x, a.y, vw, vh};
                pr.slot = layer.slot;
                pr.modal = layer.modal;
                pr.closeOutside = layer.closeOutside;
                pr.layer = li;
                popupRects_.push_back(pr);
            }
        }
        paintHmiView(r, v, vp, HmiPropertySource{}, theme_, opt);
        if (layer.popup && vp.angle == 0.f) r.strokeRect({a.x, a.y - tb, vw, vh + tb}, faded(theme_.color.accent, op), 1.5f);
        // Ce que l'utilisateur connecte ne peut pas actionner : un voile sur l'objet.
        for (Id id : layer.locked)
            if (const auto* o = v.object(id)) {
                const auto box = o->box();
                const auto p0 = vp.toScreen(box.x, box.y), p1 = vp.toScreen(box.x + box.w, box.y + box.h);
                r.fillRect({p0.x, p0.y, p1.x - p0.x, p1.y - p0.y}, gfx::Color{20, 24, 30, 110});
            }
    }
}

// =================================================================== les clics ==
std::size_t WebCanvas::liveTopLayer() const {
    for (std::size_t li = layers_.size(); li-- > 0;)
        if (!(layers_[li].popup && layers_[li].slot == -2)) return li;
    return layers_.empty() ? 0 : layers_.size() - 1;
}

Id WebCanvas::objectAtLayer(std::size_t layer, gfx::Point p) const {
    if (layer >= layers_.size() || vps_.size() != layers_.size()) return kNoId;
    const auto& top = layers_[layer];
    const auto& vp = vps_[layer];
    const double x = vp.toViewX(p.x), y = vp.toViewY(p.y);
    const auto order = top.view.paintOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const auto* o = *it;
        if (o->kind == hmi::Kind::Group || !o->flag("visible", true)) continue;
        const auto box = o->box();
        if (x >= box.x && x <= box.x + box.w && y >= box.y && y <= box.y + box.h && !hmi::clippedAt(top.view, *o, x, y)) return o->id;
    }
    return kNoId;
}

// La couche qui recoit un clic en ce point : la popup du dessus qui le contient ; une
// popup modale arrete la recherche ; sinon la vue.
int WebCanvas::layerUnder(gfx::Point p) const {
    for (auto it = popupRects_.rbegin(); it != popupRects_.rend(); ++it) {
        if (it->window.contains(p)) return static_cast<int>(it->layer);
        if (it->modal) return -1;            // dehors d'une modale : rien (ou la fermer)
    }
    return layers_.empty() ? -1 : 0;
}

double WebCanvas::dragFraction(std::size_t layer, const hmi::Object& o, gfx::Point p) const {
    if (layer >= vps_.size()) return 0;
    const auto& vp = vps_[layer];
    const auto box = o.box();
    const double lx = vp.toViewX(p.x) - box.x, ly = vp.toViewY(p.y) - box.y;
    return o.kind == hmi::Kind::Knob ? hmi::knobFractionAt(box.w, box.h, lx, ly) : hmi::sliderFractionAt(o, box.w, box.h, lx, ly);
}

//  HmiLiveCanvas::pressObject : l'objet touche ; un objet a parties rend la partie
//  touchee au moteur (objectPart) ; une commande se tire ; un champ prend le focus ;
//  sinon c'est un clic (press, et double-clic).
void WebCanvas::pressObject(std::size_t layer, gfx::Point p, int clicks, double now) {
    const Id id = objectAtLayer(layer, p);
    const auto* o = id && layer < layers_.size() ? layers_[layer].view.object(id) : nullptr;
    const auto away = [&](Id towards) { rt_->unfocus(now, towards); };
    const auto part = [&](Id obj, const std::string& pt) { rt_->objectPart(obj, pt, now); };
    if (o && layer < vps_.size()) {
        const auto& vp = vps_[layer];
        const auto box = o->box();
        const double lx = vp.toViewX(p.x) - box.x, ly = vp.toViewY(p.y) - box.y;
        if (o->kind == hmi::Kind::DocumentViewer) {
            if (rt_->focusedObject() != kNoId && rt_->focusedObject() != id) away(id);
            const std::string pt = rt_->docPartAt(id, lx, ly);
            if (!pt.empty()) part(id, "doc:at:" + hmi::formatNumber(lx) + ":" + hmi::formatNumber(ly));
            return;
        }
        if (o->kind == hmi::Kind::RecipeManager || o->kind == hmi::Kind::UserManager) {
            std::size_t rows = 0;
            if (o->kind == hmi::Kind::RecipeManager) {
                const auto* rc = project_ ? project_->recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text)) : nullptr;
                rows = rc ? rc->records.size() : 0;
            } else {
                rows = project_ ? project_->security.users.size() : 0;
            }
            const auto pt = hmi::recipeManagerHit(*o, box.w, box.h, lx, ly, rows);
            if (rt_->focusedObject() != kNoId) away(id);
            if (!pt.empty()) part(id, pt);
            return;
        }
        if (o->kind == hmi::Kind::AlarmBanner || o->kind == hmi::Kind::AlarmSummary || o->kind == hmi::Kind::ProductionCounter
            || o->kind == hmi::Kind::VariableTable || o->kind == hmi::Kind::RecipeEditor || o->kind == hmi::Kind::History) {
            std::string pt;
            switch (o->kind) {
                case hmi::Kind::AlarmBanner: pt = hmi::bannerHit(*o, box.w, box.h, lx, ly); break;
                case hmi::Kind::AlarmSummary:
                    pt = hmi::summaryHit(*o, box.w, box.h, lx, ly, project_ ? hmi::zonesOf(*project_, o->text("groups")).size() : 0);
                    break;
                case hmi::Kind::ProductionCounter: pt = hmi::productionHit(*o, box.w, box.h, lx, ly); break;
                case hmi::Kind::VariableTable: pt = hmi::variableTableHit(*o, box.w, box.h, lx, ly, hmi::variableRows(*o).size()); break;
                case hmi::Kind::RecipeEditor: {
                    const auto* rc = project_ ? project_->recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text)) : nullptr;
                    pt = hmi::recipeEditorHit(*o, box.w, box.h, lx, ly, rc ? rc->fields.size() : 0);
                    break;
                }
                default: {
                    const std::string src = o->text("source", "alarmes");
                    std::size_t rows = 0;
                    if (src == "alarmes" || src.rfind("acquitt", 0) == 0)
                        rows = hmi::historyAlarmRows(rt_->alarms(), src, hmi::markers::strip(o->text("group"), hmi::markers::Mode::Text)).size();
                    else if (src.rfind("mises", 0) == 0)
                        rows = hmi::historyShelvedRows(rt_->shelvedAlarms(), hmi::markers::strip(o->text("group"), hmi::markers::Mode::Text)).size();
                    const double k = std::clamp(o->number("fontSize", 12), 7.0, 40.0) / 12.0;
                    pt = hmi::historyHit(box.w / k, box.h / k, lx / k, ly / k, rows);
                    break;
                }
            }
            const bool form = o->kind == hmi::Kind::VariableTable || o->kind == hmi::Kind::RecipeEditor;
            if (rt_->focusedObject() != kNoId && (!form || rt_->focusedObject() != id || pt.empty())) away(id);
            if (!pt.empty()) part(id, pt);
            const bool alsoClick = o->kind == hmi::Kind::AlarmSummary || o->kind == hmi::Kind::History
                                || ((o->kind == hmi::Kind::AlarmBanner || o->kind == hmi::Kind::ProductionCounter) && pt.empty());
            if (!alsoClick) return;
        }
        if (o->kind == hmi::Kind::PlcDiagnostic) {
            const std::string pt = hmi::comm::diagnosticHit(*o, box.w, box.h, lx, ly, 0);
            if (rt_->focusedObject() != kNoId) away(id);
            if (!pt.empty()) part(id, pt);
            return;
        }
        if (o->kind == hmi::Kind::ThemeSelector) {
            const std::string pt = hmi::themeSelectorHit(*o, box.w, box.h, lx, ly);
            if (rt_->focusedObject() != kNoId) away(id);
            if (!pt.empty()) part(id, pt);
            return;
        }
        if (o->kind == hmi::Kind::LanguageSelector) {
            const std::size_t n = project_ ? hmi::languageChoices(*o, project_->languages).size() : 0;
            const std::string pt = hmi::languageSelectorHit(*o, box.w, box.h, lx, ly, n);
            if (rt_->focusedObject() != kNoId) away(id);
            if (!pt.empty()) part(id, pt);
            return;
        }
        if (hmi::kindIsNavigation(o->kind) && o->kind != hmi::Kind::Frame) {
            const std::string pt = lot12PartAt(layers_[layer].view, *o, box.w, box.h, lx, ly, rt_, project_);
            if (rt_->focusedObject() != kNoId) away(id);
            if (!pt.empty()) part(id, pt);
            const bool alsoClick = o->kind == hmi::Kind::ZoneMap || (pt.empty() && o->kind != hmi::Kind::NavBar);
            if (!alsoClick) return;
        }
        if (o->kind == hmi::Kind::Trend) {
            if (const std::string pt = hmi::trendHit(*o, box.w, box.h, lx, ly); !pt.empty()) {
                part(id, pt);
                return;
            }
        }
        if (o->kind == hmi::Kind::Table) {
            hmi::table::Model m;
            const bool rows = hmi::table::modeOf(*o) == hmi::table::Mode::Rows;
            if (rt_->tableShown(layers_[layer].view.id, *o, m)) {
                const std::size_t first = rows ? rt_->listFirst(o->id) : 0;
                if (rows)
                    if (const std::string pt = hmi::tableHit(*o, box.w, box.h, lx, ly, m.rows.size(), first); !pt.empty()) {
                        part(id, pt);
                        return;
                    }
                std::vector<double> widths = m.widths;
                if (!m.columns) widths = hmi::columnFractions(*o, std::max<std::size_t>(1, m.headers.size()));
                const std::size_t count = rows ? m.rows.size() : std::min(m.rows.size(), hmi::tableLayout(*o, box.w, box.h, 0, 0).fit);
                if (const std::string pt = hmi::tableCellHit(*o, box.w, box.h, lx, ly, count, first, widths); !pt.empty()) part(id, pt);
            }
        }
        if (o->kind == hmi::Kind::Slider || o->kind == hmi::Kind::Knob) {
            if (rt_->focusedObject() != kNoId) away(id);
            valueDrag_ = id;
            valueDragLayer_ = layer;
            rt_->dragValue(id, dragFraction(layer, *o, p), false, now);
            return;
        }
        if (o->kind == hmi::Kind::Selector || o->kind == hmi::Kind::RadioGroup || o->kind == hmi::Kind::ComboBox
            || o->kind == hmi::Kind::DateTimePicker || o->kind == hmi::Kind::WeeklySchedule || o->kind == hmi::Kind::List) {
            std::string pt;
            switch (o->kind) {
                case hmi::Kind::Selector: pt = hmi::selectorHit(*o, box.w, box.h, lx, ly, hmi::choicesOf(*o).size()); break;
                case hmi::Kind::RadioGroup: pt = hmi::radioHit(*o, box.w, box.h, lx, ly, hmi::choicesOf(*o).size()); break;
                case hmi::Kind::ComboBox: pt = "ouvrir"; break;
                case hmi::Kind::List: pt = hmi::listHit(*o, box.w, box.h, lx, ly, hmi::choicesOf(*o).size(), rt_->listFirst(o->id)); break;
                case hmi::Kind::DateTimePicker: pt = hmi::pickerHit(*o, box.w, box.h, lx, ly); break;
                default: {
                    const auto* ws = rt_->schedule(o->id);
                    const int res = ws ? ws->slotMinutes : hmi::resolutionMinutes(o->text("resolution", "30 min"));
                    pt = hmi::scheduleHit(*o, box.w, box.h, lx, ly, res);
                    break;
                }
            }
            if (rt_->focusedObject() != kNoId) away(id);
            if (!pt.empty()) part(id, pt);
            return;
        }
        if (o->kind == hmi::Kind::InputField || o->kind == hmi::Kind::LoginPanel || o->kind == hmi::Kind::PasswordChange
            || o->kind == hmi::Kind::LogoutButton) {
            const auto pt = hmi::formHit(*o, box.w, box.h, lx, ly);
            if (pt.empty()) {
                if (rt_->focusedObject() != kNoId && rt_->focusedObject() != id) away(id);
            } else {
                part(id, pt);
            }
            if (o->kind != hmi::Kind::LogoutButton || o->actions.empty()) return;
        }
    }
    if (rt_->focusedObject() != kNoId && rt_->focusedObject() != id) away(id);
    down_ = id;
    downLayer_ = layer;
    if (id) {
        rt_->press(id, now);
        if (clicks >= 2) rt_->doubleClick(id, now);
    }
}

void WebCanvas::mouseDown(float x, float y, int button, int clicks, double now) {
    if (!rt_ || !rt_->running()) return;
    const gfx::Point p{x, y};
    rt_->pointerButton(button, true, now);
    if (button != 0) return;
    // Une liste deroulante ouverte (dans la couche du dessus) prend le clic d'abord.
    if (!layers_.empty() && vps_.size() == layers_.size()) {
        const std::size_t li = liveTopLayer();
        const auto& vp = vps_[li];
        for (const auto& o : layers_[li].view.objects) {
            std::size_t first = 0;
            if (o.kind != hmi::Kind::ComboBox || !rt_->comboOpen(o.id, &first)) continue;
            const auto box = o.box();
            const double lx = vp.toViewX(p.x) - box.x, ly = vp.toViewY(p.y) - box.y;
            const std::string hit = hmi::comboHit(o, box.w, box.h, lx, ly, hmi::choicesOf(o).size(), true, first);
            rt_->objectPart(o.id, hit.empty() ? std::string("fermer") : hit, now);
            return;
        }
    }
    // Les popups : la croix, la barre de titre (tirer), un clic dehors (fermer).
    for (auto it = popupRects_.rbegin(); it != popupRects_.rend(); ++it) {
        if (it->close.w > 0.f && it->close.contains(p)) {
            hmi::Transition t;
            t.kind = hmi::TransitionKind::Fade;
            t.durationMs = 200;
            (void)rt_->closePopupAt(static_cast<std::size_t>(std::max(0, it->slot)), t, now);
            return;
        }
        if (it->movable && it->title.contains(p)) {
            popupDrag_ = it->slot;
            popupGrab_ = p;
            const auto& slots = rt_->popupSlots();
            if (it->slot >= 0 && static_cast<std::size_t>(it->slot) < slots.size()) {
                popupStartX_ = slots[static_cast<std::size_t>(it->slot)].x;
                popupStartY_ = slots[static_cast<std::size_t>(it->slot)].y;
            }
            return;
        }
        if (it->window.contains(p)) break;
        if (it->modal) {
            if (it->closeOutside) {
                hmi::Transition t;
                t.kind = hmi::TransitionKind::Fade;
                t.durationMs = 200;
                (void)rt_->closePopupAt(static_cast<std::size_t>(std::max(0, it->slot)), t, now);
            }
            return;
        }
    }
    const int layer = layerUnder(p);
    if (layer < 0) return;
    pressObject(static_cast<std::size_t>(layer), p, clicks, now);
}

void WebCanvas::mouseMove(float x, float y, double now) {
    if (!rt_ || !rt_->running()) return;
    const gfx::Point p{x, y};
    if (popupDrag_ >= 0 && !vps_.empty()) {
        const float z = vps_.front().zoom;
        if (z > 0.f) rt_->movePopup(static_cast<std::size_t>(popupDrag_), popupStartX_ + (p.x - popupGrab_.x) / z, popupStartY_ + (p.y - popupGrab_.y) / z);
        return;
    }
    if (valueDrag_ != kNoId && valueDragLayer_ < layers_.size())
        if (const auto* o = layers_[valueDragLayer_].view.object(valueDrag_)) rt_->dragValue(valueDrag_, dragFraction(valueDragLayer_, *o, p), false, now);
    // SYS.Mouse* : la vue et l'objet sous la souris, en pixels de la vue.
    const int layer = layerUnder(p);
    if (layer >= 0 && static_cast<std::size_t>(layer) < vps_.size()) {
        const auto& vp = vps_[static_cast<std::size_t>(layer)];
        const Id obj = objectAtLayer(static_cast<std::size_t>(layer), p);
        rt_->pointerMoved(layers_[static_cast<std::size_t>(layer)].view.id, vp.toViewX(p.x), vp.toViewY(p.y), obj, true);
    }
}

void WebCanvas::mouseUp(float x, float y, int button, double now) {
    if (!rt_ || !rt_->running()) return;
    const gfx::Point p{x, y};
    rt_->pointerButton(button, false, now);
    if (button != 0) return;
    if (popupDrag_ >= 0) {
        popupDrag_ = -1;
        return;
    }
    if (valueDrag_ != kNoId) {
        if (valueDragLayer_ < layers_.size())
            if (const auto* o = layers_[valueDragLayer_].view.object(valueDrag_)) rt_->dragValue(valueDrag_, dragFraction(valueDragLayer_, *o, p), true, now);
        valueDrag_ = kNoId;
        return;
    }
    if (down_ != kNoId) {
        const bool inside = objectAtLayer(downLayer_, p) == down_;
        rt_->release(down_, now, inside);
        down_ = kNoId;
    }
}

void WebCanvas::wheel(double notches) {
    if (rt_ && rt_->running()) rt_->pointerWheel(notches);
}

void WebCanvas::text(const std::string& utf8, double now) {
    if (rt_ && rt_->running() && rt_->focusedObject() != kNoId) rt_->typeText(utf8, now);
}

void WebCanvas::editKey(int key, double now) {
    if (rt_ && rt_->running()) rt_->typeKey(static_cast<hmi::EditKey>(key), now);
}

bool WebCanvas::key(const std::string& token, bool ctrl, bool shift, bool alt, bool down, bool repeat, double now) {
    if (!rt_ || !rt_->running()) return false;
    if (token.empty()) {
        rt_->keyModifiers(ctrl, shift, alt);
        return false;
    }
    if (down) {
        hmi::keys::Chord chord;
        chord.key = token;
        chord.ctrl = ctrl;
        chord.shift = shift;
        chord.alt = alt;
        return rt_->keyDown(chord, now, repeat, rt_->focusedObject() != kNoId);
    }
    const bool taken = rt_->keyUp(token, now);
    rt_->keyModifiers(ctrl, shift, alt);
    return taken;
}

std::string WebCanvas::objectNameAt(float x, float y) const {
    const int layer = layerUnder({x, y});
    if (layer < 0) return {};
    const Id id = objectAtLayer(static_cast<std::size_t>(layer), {x, y});
    const auto* o = id ? layers_[static_cast<std::size_t>(layer)].view.object(id) : nullptr;
    return o ? o->name : std::string{};
}

bool WebCanvas::objectBox(const std::string& name, float& x, float& y, float& w, float& h) const {
    if (vps_.size() != layers_.size()) return false;
    for (std::size_t li = layers_.size(); li-- > 0;)
        for (const auto& o : layers_[li].view.objects)
            if (o.name == name) {
                const auto box = o.box();
                const auto a = vps_[li].toScreen(box.x, box.y);
                const auto b = vps_[li].toScreen(box.x + box.w, box.y + box.h);
                x = a.x;
                y = a.y;
                w = b.x - a.x;
                h = b.y - a.y;
                return true;
            }
    return false;
}

bool WebCanvas::objectCenter(const std::string& name, float& x, float& y) const {
    float w = 0, h = 0;
    if (!objectBox(name, x, y, w, h)) return false;
    x += w / 2;
    y += h / 2;
    return true;
}

} // namespace app::web

#endif // XPG_WEB
