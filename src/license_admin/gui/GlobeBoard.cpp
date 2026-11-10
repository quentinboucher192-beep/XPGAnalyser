// =============================================================================
//  license_admin/gui/GlobeBoard.cpp - 1.47.0 : la carte du globe et la petite
//  carte du tableau de bord (voir GlobeBoard.hpp ; les mesures de globe.css)
// =============================================================================
#include "GlobeBoard.hpp"

#include "../../ui/Icons.hpp"
#include "../../ui/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <utility>

namespace license_admin::gui {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
namespace gl = ui::globe;
using namespace globekit;

namespace globekit {

std::string fitText(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 6.f) return {};
    if (r.measure(s, f).width <= w) return s;
    const float dots = r.measure("\xE2\x80\xA6", f).width;
    auto k = r.fitCharacters(s, f, std::max(0.f, w - dots));
    while (k > 0 && k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
    return s.substr(0, k) + "\xE2\x80\xA6";
}

float midY(const gfx::IRenderer& r, FontId f, float top, float h) { return std::round(top + (h - r.lineHeight(f)) / 2.f); }

// ------------------------------------------------------------------ FlatButton
FlatButton::FlatButton(std::string text, std::string id, Kind k, Glyph g)
    : ui::Button(text, std::move(id)), text_(std::move(text)), kind_(k), glyph_(g) {}
void FlatButton::setOn(bool on) {
    on_ = on;
    invalidate();
}
void FlatButton::setGlyph(Glyph g) {
    glyph_ = g;
    invalidate();
}
void FlatButton::setLabel(std::string text) {
    text_ = std::move(text);
    setText(text_);
    invalidateLayout();
}
float FlatButton::wanted() const {
    const FontId f = gfx::semibold(FontId{12});
    switch (kind_) {
        case Kind::Round:
        case Kind::Square:
        case Kind::Icon: return 32.f;
        case Kind::Segment: return ui::measureWidth(text_, f) + 22.f + (glyph_ == Glyph::Dot ? 13.f : 0.f);
        case Kind::Pill: return ui::measureWidth(text_, f) + 24.f + (glyph_ != Glyph::None ? 20.f : 0.f);
        case Kind::Card:
        case Kind::CardGhost: return ui::measureWidth(text_, f) + 20.f;
    }
    return 32.f;
}
ui::SizeHint FlatButton::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {wanted(), kind_ == Kind::Segment || kind_ == Kind::Card || kind_ == Kind::CardGhost ? 28.f : 32.f};
    h.minimum = h.preferred;
    return h;
}
void FlatButton::onPaint(const ui::PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const Rect b = bounds();
    const FontId f = gfx::semibold(FontId{12});
    const Color ink = kInk2;
    switch (kind_) {
        case Kind::Round:
        case Kind::Pill:
            r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h}, Color{15, 23, 42, 14}, b.h / 2.f);
            ui::shapes::fillFramedRect(r, b, hovered() ? kSurface2 : kWhite, kLine, b.h / 2.f);
            break;
        case Kind::Square:
            if (hovered()) r.fillRect(b.inset(1.f, 1.f), kSurface2);
            break;
        case Kind::Segment:
            if (on_) {
                r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h}, Color{15, 23, 42, 18}, 8.f);
                r.fillRoundedRect(b, kWhite, 8.f);
            }
            break;
        case Kind::Icon: ui::shapes::fillFramedRect(r, b, hovered() ? kSurface2 : kWhite, kLine, 9.f); break;
        case Kind::Card: ui::shapes::fillFramedRect(r, b, hovered() ? kSurface2 : kWhite, Color{213, 220, 230, 255}, 8.f); break;
        case Kind::CardGhost:
            if (hovered()) r.fillRoundedRect(b, kSurface2, 8.f);
            break;
    }
    const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f;
    float tx = b.x + 12.f;
    switch (glyph_) {
        case Glyph::Pause:
            r.fillRoundedRect({cx - 4.5f, cy - 5.5f, 3.f, 11.f}, ink, 1.f);
            r.fillRoundedRect({cx + 1.5f, cy - 5.5f, 3.f, 11.f}, ink, 1.f);
            break;
        case Glyph::Play: {
            const gfx::Vertex v[3] = {{{cx - 3.5f, cy - 6.f}, ink}, {{cx + 6.f, cy}, ink}, {{cx - 3.5f, cy + 6.f}, ink}};
            r.fillTriangles(v, 3);
            break;
        }
        case Glyph::Full: {
            const float s = 7.f, l = 3.5f;
            for (const float sx : {-1.f, 1.f})
                for (const float sy : {-1.f, 1.f}) {
                    r.line({cx + sx * s, cy + sy * s}, {cx + sx * (s - l), cy + sy * s}, kInk2, 1.5f);
                    r.line({cx + sx * s, cy + sy * s}, {cx + sx * s, cy + sy * (s - l)}, kInk2, 1.5f);
                }
            break;
        }
        case Glyph::Close:
            r.drawText({cx - r.measure("\xC3\x97", FontId{17}).width / 2.f, midY(r, FontId{17}, b.y, b.h)}, "\xC3\x97", FontId{17}, kFaint);
            break;
        case Glyph::Pin:
            ui::drawIcon(r, ui::Icon::Globe, {tx - 1.f, cy - 7.f, 14.f, 14.f}, ink);
            tx += 20.f;
            break;
        case Glyph::Dot:
            r.fillRoundedRect({tx - 1.f, cy - 3.5f, 7.f, 7.f}, kGreen, 3.5f);
            tx += 13.f;
            break;
        case Glyph::None: break;
    }
    if (kind_ == Kind::Pill || kind_ == Kind::Segment)
        r.drawText({kind_ == Kind::Segment ? tx - 1.f : tx, midY(r, f, b.y, b.h)}, text_, f, kind_ == Kind::Segment && !on_ ? kMuted : kInk);
    else if (kind_ == Kind::Card || kind_ == Kind::CardGhost)
        r.drawText({std::round(cx - r.measure(text_, f).width / 2.f), midY(r, f, b.y, b.h)}, text_, f, kInk);
    else if (kind_ == Kind::Square && glyph_ == Glyph::None)
        r.drawText({cx - r.measure(text_, FontId{17}).width / 2.f, midY(r, FontId{17}, b.y, b.h)}, text_, FontId{17}, ink);
}

// ------------------------------------------------------------------ PaintBox
PaintBox::PaintBox(std::string id, std::function<void(const ui::PaintContext&, const Rect&)> paint, bool blocks)
    : ui::Widget(std::move(id)), paint_(std::move(paint)), blocks_(blocks) {}
void PaintBox::onPaint(const ui::PaintContext& ctx) {
    if (paint_) paint_(ctx, bounds());
}
ui::EventResult PaintBox::onEvent(const ui::InputEvent& ev) {
    if (!blocks_) return ui::EventResult::Ignored;
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && bounds().contains(m->pos)) return ui::EventResult::Consumed;
    if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && bounds().contains(m->pos)) return ui::EventResult::Consumed;
    if (const auto* m = std::get_if<ui::MouseWheel>(&ev); m && bounds().contains(m->pos)) return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}
bool PaintBox::hitTest(gfx::Point local) const { return blocks_ && ui::Widget::hitTest(local); }

} // namespace globekit

namespace {
template <class T>
T* add(ui::Widget& parent, std::unique_ptr<T> w) {
    T* p = w.get();
    parent.addChild(std::move(w));
    return p;
}
constexpr Color kSeg{238, 241, 246, 255};
constexpr Color kGlobeBg{246, 248, 252, 255};
} // namespace

// ============================================================================ GlobeBoard
GlobeBoard::GlobeBoard(std::string prefix) : ui::Widget(prefix + ".carte-globe"), prefix_(std::move(prefix)) {
    const std::string& p = prefix_;
    // L'ordre des enfants est celui du dessin : le globe, ce qui est pose dessus, puis les boutons.
    layers_ = add(*this, std::make_unique<ui::GlobeLayerList>(p + ".calques"));
    globe_ = add(*this, std::make_unique<ui::GlobeView>(p + ".globe"));
    overGlobe_ = add(*this, std::make_unique<PaintBox>(p + ".dessus", [this](const ui::PaintContext& c, const Rect&) { paintOverGlobe(c); }, false));
    zoomFrame_ = add(*this, std::make_unique<PaintBox>(p + ".cadre-zoom",
                                                       [](const ui::PaintContext& c, const Rect& z) {
                                                           c.r.fillRoundedRect({z.x, z.y + 1.f, z.w, z.h}, Color{15, 23, 42, 14}, 10.f);
                                                           ui::shapes::fillFramedRect(c.r, z, kWhite, kLine, 10.f);
                                                           c.r.fillRect({z.x + 1.f, z.y + 30.f, z.w - 2.f, 1.f}, kLine);
                                                       },
                                                       false));
    cities_ = add(*this, std::make_unique<ui::GlobeCityList>(p + ".villes"));
    by_ = add(*this, std::make_unique<ui::DropDown>(p + ".par"));
    pause_ = add(*this, std::make_unique<FlatButton>("", p + ".pause", FlatButton::Kind::Round, FlatButton::Glyph::Pause));
    recenter_ = add(*this, std::make_unique<FlatButton>("Recentrer", p + ".recentrer", FlatButton::Kind::Pill, FlatButton::Glyph::Pin));
    zoomIn_ = add(*this, std::make_unique<FlatButton>("+", p + ".zoom-plus", FlatButton::Kind::Square));
    zoomOut_ = add(*this, std::make_unique<FlatButton>("\xE2\x88\x92", p + ".zoom-moins", FlatButton::Kind::Square));
    cityCard_ = add(*this, std::make_unique<PaintBox>(p + ".carte", [this](const ui::PaintContext& c, const Rect&) { paintCityCard(c); }, true));
    close_ = add(*this, std::make_unique<FlatButton>("", p + ".fermer", FlatButton::Kind::Square, FlatButton::Glyph::Close));
    seeClients_ = add(*this, std::make_unique<FlatButton>("Voir les clients", p + ".voir-clients", FlatButton::Kind::Card));
    seePcs_ = add(*this, std::make_unique<FlatButton>("Les PC", p + ".voir-pc", FlatButton::Kind::CardGhost));
    count_ = add(*this, std::make_unique<FlatButton>("Compte", p + ".compte", FlatButton::Kind::Segment));
    live_btn_ = add(*this, std::make_unique<FlatButton>("En direct", p + ".direct", FlatButton::Kind::Segment, FlatButton::Glyph::Dot));
    full_btn_ = add(*this, std::make_unique<FlatButton>("", p + ".plein", FlatButton::Kind::Icon, FlatButton::Glyph::Full));

    pause_->setTooltip("Arr\xC3\xAAter / reprendre la rotation");
    full_btn_->setTooltip("Plein \xC3\xA9" "cran");
    close_->setTooltip("Fermer");
    count_->setTooltip("Les totaux de la p\xC3\xA9riode");
    live_btn_->setTooltip("Un point pulse \xC3\xA0 chaque connexion d'un PC ; les villes comptent les PC connect\xC3\xA9s maintenant");
    count_->setOn(true);
    layers_->setNameSuffix(gl::Layer::Orders, " \xC2\xB7 30 j");

    links_ += layers_->layersChanged->connect([this](gl::LayerSet s) {
        globe_->setLayers(s);
        syncBy();
    });
    links_ += globe_->placeClicked->connect([this](int i) { choose(i == selected_ ? -1 : i); });
    links_ += cities_->placeChosen->connect([this](int i) { choose(i == selected_ ? -1 : i); });
    links_ += by_->selectionChanged->connect([this](int i) {
        if (i >= 0 && i < static_cast<int>(byLayers_.size())) cities_->setLayer(byLayers_[static_cast<std::size_t>(i)]);
    });
    links_ += pause_->clicked->connect([this] {
        globe_->setAutoRotate(!globe_->autoRotate());
        pause_->setGlyph(globe_->autoRotate() ? FlatButton::Glyph::Pause : FlatButton::Glyph::Play);
    });
    links_ += recenter_->clicked->connect([this] {
        choose(-1);
        globe_->recenter();
    });
    links_ += zoomIn_->clicked->connect([this] { globe_->zoomBy(1.35f); });
    links_ += zoomOut_->clicked->connect([this] { globe_->zoomBy(1.f / 1.35f); });
    links_ += count_->clicked->connect([this] {
        setLive(false);
        if (onMode) onMode(false);
    });
    links_ += live_btn_->clicked->connect([this] {
        setLive(true);
        if (onMode) onMode(true);
    });
    links_ += full_btn_->clicked->connect([this] {
        setFullScreen(!full_);
        if (onFullScreen) onFullScreen(full_);
    });
    links_ += close_->clicked->connect([this] { choose(-1); });
    links_ += seeClients_->clicked->connect([this] {
        if (selected_ >= 0 && onClients) onClients(selected_);
    });
    links_ += seePcs_->clicked->connect([this] {
        if (selected_ >= 0 && onPcs) onPcs(selected_);
    });
    globe_->setHome(38.0, 4.85);
    globe_->setView(38.0, 4.85, 1.f);
    syncBy();
    for (ui::Widget* w : {static_cast<ui::Widget*>(close_), static_cast<ui::Widget*>(cityCard_), static_cast<ui::Widget*>(seeClients_),
                          static_cast<ui::Widget*>(seePcs_)})
        w->setVisibility(ui::Visibility::Collapsed);
}

GlobeBoard::~GlobeBoard() { links_.clear(); }

void GlobeBoard::setPlaces(std::vector<gl::Place> places, std::vector<std::string> adminLabels) {
    // La ville choisie reste choisie si elle est encore la (par son id).
    const std::string keep = selected_ >= 0 && selected_ < static_cast<int>(places_.size()) ? places_[static_cast<std::size_t>(selected_)].id : std::string();
    places_ = std::move(places);
    adminLabels_ = std::move(adminLabels);
    adminLabels_.resize(places_.size());
    globe_->setPlaces(places_);
    cities_->setPlaces(places_);
    int again = -1;
    for (std::size_t i = 0; i < places_.size() && !keep.empty(); ++i)
        if (places_[i].id == keep) again = static_cast<int>(i);
    selected_ = -1;
    if (again >= 0) {
        selected_ = again;
        globe_->setSelected(again);
        cities_->setSelected(again);
    } else {
        globe_->setSelected(-1);
        cities_->setSelected(-1);
        globe_->setLeaderTarget(std::nullopt);
    }
    for (ui::Widget* w : {static_cast<ui::Widget*>(close_), static_cast<ui::Widget*>(cityCard_)})
        w->setVisibility(selected_ >= 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    syncCardButtons();
    syncReserved();
    invalidate();
}

void GlobeBoard::setTotals(const std::array<int, gl::kLayerCount>& totals) { layers_->setTotals(totals); }
void GlobeBoard::setHint(gl::Layer l, std::string hint) { layers_->setHint(l, std::move(hint)); }
void GlobeBoard::setOrdersPeriod(std::string label) {
    ordersPeriod_ = std::move(label);
    layers_->setNameSuffix(gl::Layer::Orders, " \xC2\xB7 " + ordersPeriod_);
    invalidate();
}
void GlobeBoard::setHeader(std::string title, std::string sub) {
    title_ = std::move(title);
    sub_ = std::move(sub);
    invalidate();
}
void GlobeBoard::setEmptyMessage(std::string title, std::string text) {
    emptyTitle_ = std::move(title);
    emptyText_ = std::move(text);
    invalidate();
}
void GlobeBoard::setStatus(std::string text) {
    status_ = std::move(text);
    invalidate();
}
void GlobeBoard::setCardActions(bool on) {
    cardActions_ = on;
    syncCardButtons();
    invalidateLayout();
}
void GlobeBoard::setLiveLabel(std::string text) {
    liveLabel_ = std::move(text);
    invalidate();
}
void GlobeBoard::setTicker(std::string city, std::string what, std::string when) {
    tickCity_ = std::move(city);
    tickWhat_ = std::move(what);
    tickWhen_ = std::move(when);
    invalidate();
}
void GlobeBoard::setActiveTotal(int n) {
    activeTotal_ = n;
    invalidate();
}

void GlobeBoard::setLive(bool on) {
    live_ = on;
    count_->setOn(!on);
    live_btn_->setOn(on);
    if (on) {
        auto s = globe_->layers();
        s.set(gl::Layer::ActivePcs, true);
        globe_->setLayers(s);
        layers_->setLayers(s);
    }
    syncBy();
    invalidateLayout();
    invalidate();
}

void GlobeBoard::setFullScreen(bool on) {
    full_ = on;
    invalidateLayout();
}

// Choisir une ville (le globe, la liste) : le globe tourne vers elle, sa carte s'ouvre ; -1 : rien.
void GlobeBoard::choose(int i) {
    selected_ = i >= 0 && i < static_cast<int>(places_.size()) ? i : -1;
    globe_->setSelected(selected_);
    cities_->setSelected(selected_);
    if (selected_ >= 0) {
        const auto& p = places_[static_cast<std::size_t>(selected_)];
        globe_->turnTo(p.lat, p.lon, std::max(globe_->view().zoom, 1.45f), 1.1);
    } else {
        globe_->setLeaderTarget(std::nullopt);
    }
    for (ui::Widget* w : {static_cast<ui::Widget*>(close_), static_cast<ui::Widget*>(cityCard_)})
        w->setVisibility(selected_ >= 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    syncCardButtons();
    syncReserved();
    invalidate();
}

void GlobeBoard::freezeOn(int place) {
    globe_->setFrozen(true);
    setLive(false);
    if (place >= 0 && place < static_cast<int>(places_.size())) {
        const auto& p = places_[static_cast<std::size_t>(place)];
        globe_->setView(p.lat, p.lon, 1.45f);
        choose(place);
    } else {
        globe_->setView(45.76, 4.85, 1.45f);
        choose(-1);
    }
}

void GlobeBoard::unfreeze() {
    globe_->setFrozen(false);
    choose(-1);
    globe_->setView(38.0, 4.85, 1.f);
}

float GlobeBoard::cardHeight() const { return 46.f + 7.f * 25.f + 10.f + (cardActions_ ? 38.f : 0.f); }

void GlobeBoard::syncCardButtons() {
    const bool show = cardActions_ && selected_ >= 0;
    if (show) {
        const auto& p = places_[static_cast<std::size_t>(selected_)];
        const int n = p.count(gl::Layer::Clients);
        seeClients_->setLabel(n == 1 ? std::string("Voir le client") : "Voir les " + std::to_string(n) + " clients");
        seePcs_->setLabel(p.pcs == 1 ? std::string("Le PC") : "Les " + std::to_string(p.pcs) + " PC");
        seeClients_->setVisibility(n > 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        seePcs_->setVisibility(p.pcs > 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    } else {
        seeClients_->setVisibility(ui::Visibility::Collapsed);
        seePcs_->setVisibility(ui::Visibility::Collapsed);
    }
}

// Les noms des villes ne vont ni sous les boutons, ni sous la carte d'une ville ; une fenetre etroite : le disque glisse.
void GlobeBoard::syncReserved() {
    std::vector<Rect> areas;
    const Rect& g = globeArea_;
    areas.push_back({g.x, g.y, std::max(pill_.w, 330.f) + 14.f, 44.f});
    areas.push_back({g.x, g.bottom() - 52.f, g.w, 52.f});
    if (selected_ >= 0) areas.push_back(cardBox_.inset(-6.f, -6.f));
    globe_->setReservedAreas(std::move(areas));
    if (selected_ >= 0) globe_->setLeaderTarget(gfx::Point{cardBox_.x, cardBox_.y + 22.f});
    const float room = (cardBox_.x - 60.f) - (g.x + g.w / 2.f);
    globe_->setCentreShift(selected_ >= 0 && g.w > 0.f ? std::min(0.f, room) : 0.f);
}

// << par >> : les calques coches ; En direct : les PC actifs.
void GlobeBoard::syncBy() {
    byLayers_.clear();
    std::vector<ui::DropDown::Item> items;
    int keep = 0;
    const gl::LayerSet s = globe_->layers();
    for (const auto& st : gl::styles()) {
        if (!s.has(st.layer)) continue;
        if (st.layer == cities_->layer()) keep = static_cast<int>(byLayers_.size());
        byLayers_.push_back(st.layer);
        items.push_back({st.name, st.key});
    }
    by_->setItems(std::move(items));
    if (live_) {
        cities_->setLayer(gl::Layer::ActivePcs);
        by_->setVisibility(ui::Visibility::Hidden);
    } else {
        by_->setVisibility(ui::Visibility::Visible);
        if (!byLayers_.empty() && !s.has(cities_->layer())) keep = 0;
        by_->setSelectedIndex(keep);
        if (!byLayers_.empty()) cities_->setLayer(byLayers_[static_cast<std::size_t>(keep)]);
    }
}

void GlobeBoard::onLayout() {
    const Rect b = bounds();
    const float leftW = full_ ? 260.f : 256.f, rightW = full_ ? 380.f : 326.f;
    const float top = b.y + 52.f, bottom = b.bottom() - 34.f;
    layers_->setBounds({b.x + 1.f, top, leftW - 1.f, std::max(0.f, bottom - top)});
    globeArea_ = {b.x + leftW, top, std::max(120.f, b.w - leftW - rightW), std::max(0.f, bottom - top)};
    globe_->setBounds(globeArea_);
    overGlobe_->setBounds(globeArea_);
    right_ = {globeArea_.right(), top, rightW - 1.f, std::max(0.f, bottom - top)};
    by_->setBounds({right_.right() - 16.f - 150.f, right_.y + 12.f, 150.f, 28.f});
    const float listTop = right_.y + 76.f + (live_ ? 38.f : 0.f);
    cities_->setBounds({right_.x + 8.f, listTop, right_.w - 16.f, std::max(30.f, right_.bottom() - 8.f - listTop)});
    // l'en-tete : Compte / En direct, plein ecran
    full_btn_->setBounds({b.right() - 14.f - 32.f, b.y + 10.f, 32.f, 32.f});
    const float lw = live_btn_->wanted(), cw = count_->wanted();
    live_btn_->setBounds({full_btn_->bounds().x - 10.f - 3.f - lw, b.y + 12.f, lw, 28.f});
    count_->setBounds({live_btn_->bounds().x - cw, b.y + 12.f, cw, 28.f});
    seg_ = {count_->bounds().x - 3.f, b.y + 9.f, cw + lw + 6.f, 34.f};
    // dans le globe : la pause, Recentrer, le zoom
    const Rect& g = globeArea_;
    pause_->setBounds({g.x + 14.f, g.bottom() - 12.f - 32.f, 32.f, 32.f});
    recenter_->setBounds({pause_->bounds().right() + 6.f, g.bottom() - 12.f - 32.f, recenter_->wanted(), 32.f});
    zoomIn_->setBounds({g.right() - 14.f - 32.f, g.bottom() - 12.f - 61.f, 32.f, 30.f});
    zoomOut_->setBounds({g.right() - 14.f - 32.f, g.bottom() - 12.f - 30.f, 32.f, 30.f});
    zoomFrame_->setBounds({g.right() - 14.f - 32.f, g.bottom() - 12.f - 61.f, 32.f, 61.f});
    // la carte d'une ville (en haut a droite du globe)
    cardBox_ = {g.right() - 14.f - 244.f, g.y + 14.f, 244.f, cardHeight()};
    cityCard_->setBounds(cardBox_);
    close_->setBounds({cardBox_.right() - 12.f - 22.f, cardBox_.y + 9.f, 22.f, 22.f});
    const float by = cardBox_.bottom() - 10.f - 28.f;
    seeClients_->setBounds({cardBox_.x + 12.f, by, seeClients_->wanted(), 28.f});
    seePcs_->setBounds({seeClients_->visible() ? seeClients_->bounds().right() + 6.f : cardBox_.x + 12.f, by, seePcs_->wanted(), 28.f});
    syncReserved();
}

void GlobeBoard::onPaint(const ui::PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const Rect b = bounds();
    const float rad = full_ ? 0.f : 14.f;
    r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h + 1.f}, Color{15, 23, 42, 10}, rad);
    ui::shapes::fillFramedRect(r, b, kWhite, kLine, rad);
    // l'en-tete
    const Rect gi{b.x + 18.f, b.y + 11.f, 30.f, 30.f};
    r.fillRoundedRect(gi, kBlue50, 9.f);
    ui::drawIcon(r, ui::Icon::Globe, gi.inset(7.f, 7.f), kBlue);
    const float hx = gi.right() + 12.f;
    float rightEdge = seg_.x - 16.f;
    if (live_ && !liveLabel_.empty()) {
        const FontId f = gfx::semibold(FontId{12});
        const float w = r.measure(liveLabel_, f).width;
        const float lx = seg_.x - 18.f - w;
        r.fillRoundedRect({lx - 15.f, b.y + 22.f, 8.f, 8.f}, kGreen, 4.f);
        r.drawText({lx, midY(r, f, b.y + 9.f, 34.f)}, liveLabel_, f, kGreenInk);
        rightEdge = lx - 27.f;
    }
    r.drawText({hx, b.y + 9.f}, fitText(r, title_, gfx::semibold(FontId{14}), rightEdge - hx), gfx::semibold(FontId{14}), kInk);
    r.drawText({hx, b.y + 29.f}, fitText(r, sub_, FontId{11}, rightEdge - hx), FontId{11}, kMuted);
    r.fillRoundedRect(seg_, kSeg, 10.f);
    r.fillRect({b.x, b.y + 51.f, b.w, 1.f}, kLine);
    // le fond du globe (globe.css, #gz : un degrade radial a peine)
    r.fillRect(globeArea_, kGlobeBg);
    r.fillRoundedRect(globeArea_.inset(globeArea_.w * 0.08f, globeArea_.h * 0.08f), Color{250, 251, 253, 255}, globeArea_.h * 0.4f);
    // la colonne des villes
    r.fillRect({right_.x, right_.y, 1.f, right_.h}, kLine);
    r.drawText({right_.x + 16.f, right_.y + 14.f}, "Les villes", gfx::semibold(FontId{14}), kInk);
    // << par >> et sa liste ; En direct : la liste est cachee, le texte va a droite.
    const std::string par = live_ ? "PC connect\xC3\xA9s maintenant" : "par";
    const float parX = live_ ? right_.right() - 16.f - r.measure(par, FontId{11}).width : by_->bounds().x - 8.f - r.measure(par, FontId{11}).width;
    r.drawText({parX, midY(r, FontId{11}, right_.y + 12.f, 28.f)}, par, FontId{11}, kMuted);
    const int shown = static_cast<int>(cities_->order().size());
    std::string sb = std::to_string(shown) + (shown > 1 ? " villes" : " ville") + " \xC2\xB7 " + std::to_string(cities_->countries()) + " pays";
    if (live_ && activeTotal_ >= 0) sb += " \xC2\xB7 " + std::to_string(activeTotal_) + " PC";
    r.drawText({right_.x + 16.f, right_.y + 50.f}, sb, FontId{11}, kFaint);
    const std::string hint = "un clic : le globe y va";
    r.drawText({right_.right() - 16.f - r.measure(hint, FontId{11}).width, right_.y + 50.f}, hint, FontId{11}, kFaint);
    if (live_) {   // la derniere connexion (globe.css, #gtick)
        const Rect t{right_.x + 12.f, right_.y + 72.f, right_.w - 24.f, 30.f};
        r.fillRoundedRect(t, Color{236, 250, 244, 255}, 9.f);
        r.fillRoundedRect({t.x + 10.f, t.y + 11.5f, 7.f, 7.f}, kGreen, 3.5f);
        const FontId fb = gfx::semibold(FontId{11}), f{11};
        if (tickCity_.empty()) {
            r.drawText({t.x + 24.f, midY(r, f, t.y, t.h)}, "En attente de la prochaine connexion\xE2\x80\xA6", f, Color{15, 95, 67, 255});
        } else {
            const float ww = r.measure(tickWhen_, f).width;
            r.drawText({t.right() - 10.f - ww, midY(r, f, t.y, t.h)}, tickWhen_, f, Color{63, 143, 112, 255});
            const float room = t.right() - 20.f - ww - (t.x + 24.f);
            const std::string c = fitText(r, tickCity_, fb, room);
            r.drawText({t.x + 24.f, midY(r, fb, t.y, t.h)}, c, fb, Color{15, 95, 67, 255});
            const float cx = t.x + 24.f + r.measure(c, fb).width;
            r.drawText({cx, midY(r, f, t.y, t.h)}, fitText(r, " \xC2\xB7 " + tickWhat_, f, t.right() - 20.f - ww - cx), f, Color{15, 95, 67, 255});
        }
    }
    // la note du bas
    const Rect note{b.x + 1.f, b.bottom() - 34.f, b.w - 2.f, 33.f};
    r.fillRect(note, kSurface2);
    r.fillRect({b.x, note.y, b.w, 1.f}, kLine);
    ui::drawIcon(r, ui::Icon::Info, {note.x + 17.f, note.y + 10.f, 14.f, 14.f}, kFaint);
    const std::string lead = "D'o\xC3\xB9 viennent les points : ";
    const FontId fb = gfx::semibold(FontId{11});
    r.drawText({note.x + 39.f, midY(r, fb, note.y, note.h)}, lead, fb, kInk2);
    const float lx = note.x + 39.f + r.measure(lead, fb).width;
    r.drawText({lx, midY(r, FontId{11}, note.y, note.h)},
               fitText(r, "la ville et le pays que Cloudflare donne pour chaque appel au service (request.cf). Le service ne garde que la ville de chaque PC "
                          "\xC3\xA0 sa derni\xC3\xA8re connexion, jamais son adresse IP.",
                       FontId{11}, note.right() - 12.f - lx),
               FontId{11}, kMuted);
}

// Sur le globe : << Il tourne seul >>, l'etat, l'aide a cote de Recentrer, le message d'une carte vide.
void GlobeBoard::paintOverGlobe(const ui::PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const Rect& g = globeArea_;
    const FontId f = gfx::semibold(FontId{11});
    const std::string pill = globe_->autoRotate() ? "Il tourne seul \xC2\xB7 il s'arr\xC3\xAAte sous la souris" : "En pause \xC2\xB7 glisser pour le tourner";
    const float pw = r.measure(pill, f).width + 38.f;
    pill_ = {g.x + 14.f, g.y + 12.f, pw, 25.f};
    ui::shapes::fillFramedRect(r, pill_, Color{255, 255, 255, 230}, kLine, 12.5f);
    ui::drawIcon(r, ui::Icon::Refresh, {pill_.x + 9.f, pill_.y + 6.f, 13.f, 13.f}, kMuted);
    r.drawText({pill_.x + 28.f, midY(r, f, pill_.y, pill_.h)}, pill, f, kMuted);
    if (!status_.empty()) {   // << Releve en cours... >>, sous la pastille
        const float sw = r.measure(status_, f).width + 20.f;
        const Rect s{g.x + 14.f, pill_.bottom() + 6.f, sw, 23.f};
        ui::shapes::fillFramedRect(r, s, Color{255, 255, 255, 230}, kLine, 11.5f);
        r.drawText({s.x + 10.f, midY(r, f, s.y, s.h)}, status_, f, kInk2);
    }
    const std::string help = "Glisser pour le tourner \xC2\xB7 molette pour zoomer";
    const Rect rc = recenter_->bounds();
    const float hw = r.measure(help, FontId{11}).width + 18.f;
    if (rc.right() + 10.f + hw < zoomIn_->bounds().x - 8.f) {
        r.fillRoundedRect({rc.right() + 10.f, rc.y + 5.f, hw, 22.f}, Color{255, 255, 255, 225}, 11.f);
        r.drawText({rc.right() + 19.f, midY(r, FontId{11}, rc.y + 5.f, 22.f)}, help, FontId{11}, kMuted);
    }
    if (!emptyTitle_.empty()) {   // aucune ville encore, le service a deployer : au milieu du globe
        const float w = std::min(g.w - 40.f, 380.f);
        std::vector<std::string> lines;
        std::string line;
        std::size_t pos = 0;
        while (pos <= emptyText_.size()) {
            std::size_t sp = emptyText_.find(' ', pos);
            if (sp == std::string::npos) sp = emptyText_.size();
            const std::string word = emptyText_.substr(pos, sp - pos);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && r.measure(tried, FontId{12}).width > w - 32.f) {
                lines.push_back(line);
                line = word;
            } else {
                line = tried;
            }
            pos = sp + 1;
        }
        if (!line.empty()) lines.push_back(line);
        const float h = 22.f + 22.f + static_cast<float>(lines.size()) * 17.f + 14.f;
        const Rect box{std::round(g.x + (g.w - w) / 2.f), std::round(g.y + (g.h - h) / 2.f), w, h};
        r.fillRoundedRect({box.x, box.y + 3.f, box.w, box.h + 2.f}, Color{15, 23, 42, 16}, 12.f);
        ui::shapes::fillFramedRect(r, box, kWhite, kLine, 12.f);
        r.drawText({box.x + 16.f, box.y + 16.f}, fitText(r, emptyTitle_, gfx::semibold(FontId{14}), w - 32.f), gfx::semibold(FontId{14}), kInk);
        float y = box.y + 42.f;
        for (const auto& l : lines) {
            r.drawText({box.x + 16.f, y}, l, FontId{12}, kMuted);
            y += 17.f;
        }
    }
}

void GlobeBoard::paintCityCard(const ui::PaintContext& ctx) {
    if (selected_ < 0 || selected_ >= static_cast<int>(places_.size())) return;
    gfx::IRenderer& r = ctx.r;
    const auto& p = places_[static_cast<std::size_t>(selected_)];
    const Rect& k = cardBox_;
    r.fillRoundedRect({k.x, k.y + 4.f, k.w, k.h + 4.f}, Color{15, 23, 42, 14}, 12.f);
    ui::shapes::fillFramedRect(r, k, kWhite, kLine, 12.f);
    r.drawText({k.x + 12.f, k.y + 10.f}, fitText(r, p.label(), gfx::semibold(FontId{14}), k.w - 50.f), gfx::semibold(FontId{14}), kInk);
    r.drawText({k.x + 12.f, k.y + 29.f}, fitText(r, p.region, FontId{11}, k.w - 50.f), FontId{11}, kMuted);
    float y = k.y + 46.f;
    for (const auto& st : gl::styles()) {
        const int n = p.count(st.layer);
        std::string extra;
        if (st.layer == gl::Layer::ActivePcs && p.pcs > 0) extra = "sur " + std::to_string(p.pcs) + " PC";
        if (st.layer == gl::Layer::Admins && n > 0) extra = adminLabels_[static_cast<std::size_t>(selected_)];
        if (st.layer == gl::Layer::Orders) extra = ordersPeriod_;
        const bool zero = n == 0;
        r.fillRect({k.x + 12.f, y, k.w - 24.f, 1.f}, Color{240, 243, 247, 255});
        ui::GlobeView::drawGlyph(r, st.layer, {k.x + 12.f, y + 5.5f, 14.f, 14.f}, !zero);
        const FontId fl{12}, fn = gfx::semibold(FontId{12}), fx{11};
        r.drawText({k.x + 34.f, midY(r, fl, y, 25.f)}, st.shortName, fl, zero ? kFaint : kInk2);
        const std::string num = std::to_string(n);
        const float nw = r.measure(num, zero ? fl : fn).width;
        float rx = k.right() - 12.f;
        if (!extra.empty()) {
            const float room = rx - (k.x + 34.f + r.measure(st.shortName, fl).width + 12.f + nw + 4.f);
            const std::string e = fitText(r, extra, fx, room);
            const float ew = r.measure(e, fx).width;
            r.drawText({rx - ew, midY(r, fx, y, 25.f) + 1.f}, e, fx, zero ? kFaint : kMuted);
            rx -= ew + 4.f;
        }
        r.drawText({rx - nw, midY(r, fn, y, 25.f)}, num, zero ? fl : fn, zero ? kFaint : kInk);
        y += 25.f;
    }
}

// ============================================================================ GlobeMiniCard
GlobeMiniCard::GlobeMiniCard(std::string id) : ui::Widget(id) {
    globe_ = add(*this, std::make_unique<ui::GlobeView>(id + ".globe"));
    globe_->setCompact(true);
    globe_->setLayers(gl::LayerSet::of({gl::Layer::ActivePcs}));
    globe_->setView(36.0, 0.0, 1.f);
    setTooltip("Ouvrir le globe (Statistiques)");
}
void GlobeMiniCard::setPlaces(std::vector<gl::Place> places) {
    places_ = std::move(places);
    globe_->setPlaces(places_);
    invalidate();
}
void GlobeMiniCard::setActive(int n) {
    active_ = n;
    invalidate();
}
void GlobeMiniCard::setMessage(std::string text) {
    message_ = std::move(text);
    invalidate();
}
ui::SizeHint GlobeMiniCard::sizeHint() const { return ui::SizeHint{{476.f, 150.f}, {300.f, 130.f}, 1.f, 0.f}; }
std::optional<ui::cursor::Shape> GlobeMiniCard::cursorAt(gfx::Point local) const {
    return bounds().contains(local) ? std::optional<ui::cursor::Shape>(ui::cursor::Shape::Pointer) : std::nullopt;
}
void GlobeMiniCard::onLayout() {
    const Rect b = bounds();
    const float s = std::min(116.f, b.h - 44.f);
    globe_->setBounds({b.x + 10.f, b.y + 38.f + (b.h - 44.f - s) / 2.f, s, s});
}
ui::EventResult GlobeMiniCard::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left && bounds().contains(m->pos)) {
        if (onOpen) onOpen();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}
void GlobeMiniCard::onPaint(const ui::PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const Rect m = bounds();
    r.fillRoundedRect({m.x, m.y + 1.f, m.w, m.h + 1.f}, Color{15, 23, 42, 10}, 12.f);
    ui::shapes::fillFramedRect(r, m, hovered() ? Color{252, 253, 255, 255} : kWhite, kLine, 12.f);
    ui::drawIcon(r, ui::Icon::Globe, {m.x + 14.f, m.y + 11.f, 15.f, 15.f}, kBlue);
    const FontId ft = gfx::semibold(FontId{13});
    r.drawText({m.x + 36.f, m.y + 9.f}, "Le globe", ft, kInk);
    const std::string link = "Ouvrir le globe \xE2\x80\xBA";
    const float lw = r.measure(link, FontId{12}).width;
    r.drawText({m.right() - 16.f - lw, m.y + 11.f}, link, FontId{12}, Color{109, 74, 255, 255});
    if (active_ >= 0) {
        const float lx = m.x + 36.f + r.measure("Le globe", ft).width + 14.f;
        r.fillRoundedRect({lx, m.y + 15.f, 8.f, 8.f}, kGreen, 4.f);
        r.drawText({lx + 14.f, m.y + 10.f}, fitText(r, std::to_string(active_) + " PC actifs maintenant", gfx::semibold(FontId{12}), m.right() - 30.f - lw - lx - 14.f),
                   gfx::semibold(FontId{12}), kGreenInk);
    }
    const float x0 = globe_->bounds().right() + 14.f, x1 = m.right() - 16.f;
    if (!message_.empty()) {
        // le message (pas de service, aucune ville) coupe en lignes
        std::string line;
        float y = m.y + 44.f;
        std::size_t pos = 0;
        int n = 0;
        while (pos <= message_.size() && n < 4) {
            std::size_t sp = message_.find(' ', pos);
            if (sp == std::string::npos) sp = message_.size();
            const std::string word = message_.substr(pos, sp - pos);
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && r.measure(tried, FontId{12}).width > x1 - x0) {
                r.drawText({x0, y}, line, FontId{12}, kMuted);
                y += 17.f;
                ++n;
                line = word;
            } else {
                line = tried;
            }
            pos = sp + 1;
        }
        if (n < 4 && !line.empty()) r.drawText({x0, y}, line, FontId{12}, kMuted);
        return;
    }
    // les 3 premieres villes (clients)
    std::vector<int> order;
    for (std::size_t i = 0; i < places_.size(); ++i)
        if (places_[i].count(gl::Layer::Clients) > 0) order.push_back(static_cast<int>(i));
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return places_[static_cast<std::size_t>(a)].count(gl::Layer::Clients) > places_[static_cast<std::size_t>(b)].count(gl::Layer::Clients);
    });
    r.drawText({x0, m.y + 38.f}, "Les 3 premi\xC3\xA8res villes \xC2\xB7 clients", FontId{11}, kMuted);
    if (order.empty()) return;
    const int top = std::max(1, places_[static_cast<std::size_t>(order[0])].count(gl::Layer::Clients));
    for (std::size_t k = 0; k < 3 && k < order.size(); ++k) {
        const auto& p = places_[static_cast<std::size_t>(order[k])];
        const float y = m.y + 58.f + static_cast<float>(k) * 22.f;
        r.drawText({x0, y}, fitText(r, p.label(), FontId{12}, 88.f), FontId{12}, kInk);
        const float bx = x0 + 92.f, bw = std::max(10.f, x1 - 26.f - bx);
        r.fillRoundedRect({bx, y + 5.f, std::max(3.f, bw * static_cast<float>(p.count(gl::Layer::Clients)) / static_cast<float>(top)), 7.f}, kBlue, 3.f);
        const std::string n = std::to_string(p.count(gl::Layer::Clients));
        r.drawText({x1 - r.measure(n, gfx::semibold(FontId{12})).width, y}, n, gfx::semibold(FontId{12}), kInk);
    }
}

} // namespace license_admin::gui
