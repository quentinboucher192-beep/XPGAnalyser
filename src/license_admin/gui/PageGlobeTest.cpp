// =============================================================================
//  license_admin/gui/PageGlobeTest.cpp - 1.47.0 : LA PAGE D'ESSAI DU GLOBE
//  (cachee : --page globe-essai ; jamais dans le menu)
// -----------------------------------------------------------------------------
//  Le globe (ui/widgets/Globe) et ses deux colonnes (GlobeLists) montres comme la
//  planche 11 (docs/maquettes/1.47.0/hd/11-admin-globe.png), avec les 21 villes
//  d'essai de la maquette (globe::samplePlaces) - RIEN du service : c'est dit en
//  tete. La vraie page (Statistiques > Le globe) lira GET /admin/globe ; celle-ci
//  sert a le voir tourner, a le capturer et a le comparer a la planche.
//    - les calques a gauche, les villes a droite (un clic : le globe y va, la carte
//      de la ville s'ouvre, un trait la relie a son point), Compte / En direct,
//      plein ecran (la carte prend la page), Pause, Recentrer, + / - ;
//    - en bas, la petite carte du tableau de bord (le globe compact) ;
//    - POUR LES CAPTURES (en haut a droite) : << Figer >> (globe-essai.fige : la vue
//      de la planche, centree sur Lyon, zoom 1,45, Lyon choisie ; plus rien ne
//      bouge avec le temps - le #fige de la maquette), << Survoler Paris >>
//      (globe-essai.survol : la souris posee sur Paris, son infobulle), << Une
//      connexion >> (globe-essai.connexion : une bouffee En direct sur la
//      prochaine ville d'essai).
//  XPG-License-Admin --page globe-essai --taille 1373x917 --cliquer globe-essai.fige --capture a.png
// =============================================================================
#include "Page.hpp"

#include "../../ui/Icons.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/Globe.hpp"
#include "../../ui/widgets/GlobeLists.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
namespace gl = ui::globe;

constexpr Color kInk{15, 23, 42, 255};
constexpr Color kInk2{51, 65, 85, 255};
constexpr Color kMuted{100, 116, 139, 255};
constexpr Color kFaint{148, 163, 184, 255};
constexpr Color kLine{227, 232, 239, 255};
constexpr Color kLine2{213, 220, 230, 255};
constexpr Color kSurface2{248, 250, 252, 255};
constexpr Color kWhite{255, 255, 255, 255};
constexpr Color kBlue{42, 120, 214, 255};
constexpr Color kBlue50{238, 244, 255, 255};
constexpr Color kGreen{27, 175, 122, 255};
constexpr Color kGreenInk{15, 122, 85, 255};
constexpr Color kSeg{238, 241, 246, 255};
constexpr Color kYellow{250, 204, 21, 255};
constexpr Color kGlobeBg{246, 248, 252, 255};

template <class T>
T* add(ui::Widget& parent, std::unique_ptr<T> w) {
    T* p = w.get();
    parent.addChild(std::move(w));
    return p;
}

std::string fitText(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 6.f) return {};
    if (r.measure(s, f).width <= w) return s;
    const float dots = r.measure("\xE2\x80\xA6", f).width;
    auto k = r.fitCharacters(s, f, std::max(0.f, w - dots));
    while (k > 0 && k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
    return s.substr(0, k) + "\xE2\x80\xA6";
}
float midY(const gfx::IRenderer& r, FontId f, float top, float h) { return std::round(top + (h - r.lineHeight(f)) / 2.f); }

// Les boutons de la planche : rond (la pause), en pilule (Recentrer), carre (+ / -), un segment (Compte / En direct),
// une icone (plein ecran, fermer la carte).
class FlatButton final : public ui::Button {
public:
    enum class Kind : std::uint8_t { Round, Pill, Square, Segment, Icon };
    enum class Glyph : std::uint8_t { None, Pause, Play, Full, Close, Pin, Dot };
    FlatButton(std::string text, std::string id, Kind k, Glyph g = Glyph::None) : ui::Button(text, std::move(id)), text_(std::move(text)), kind_(k), glyph_(g) {}
    void setOn(bool on) {
        on_ = on;
        invalidate();
    }
    void setGlyph(Glyph g) {
        glyph_ = g;
        invalidate();
    }
    [[nodiscard]] float wanted() const {
        const FontId f = gfx::semibold(FontId{12});
        switch (kind_) {
            case Kind::Round: return 32.f;
            case Kind::Square: return 32.f;
            case Kind::Icon: return 32.f;
            case Kind::Segment: return ui::measureWidth(text_, f) + 22.f + (glyph_ == Glyph::Dot ? 13.f : 0.f);
            case Kind::Pill: return ui::measureWidth(text_, f) + 24.f + (glyph_ != Glyph::None ? 20.f : 0.f);
        }
        return 32.f;
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {wanted(), kind_ == Kind::Segment ? 28.f : 32.f};
        h.minimum = h.preferred;
        return h;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        gfx::IRenderer& r = ctx.r;
        const Rect b = bounds();
        const FontId f = gfx::semibold(FontId{12});
        const Color ink = kInk2;
        switch (kind_) {
            case Kind::Round:
                r.fillRoundedRect({b.x, b.y + 1.f, b.w, b.h}, Color{15, 23, 42, 14}, b.h / 2.f);
                ui::shapes::fillFramedRect(r, b, hovered() ? kSurface2 : kWhite, kLine, b.h / 2.f);
                break;
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
            case Kind::Icon:
                ui::shapes::fillFramedRect(r, b, hovered() ? kSurface2 : kWhite, kLine, 9.f);
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
                const Color c = kInk2;
                for (const float sx : {-1.f, 1.f})
                    for (const float sy : {-1.f, 1.f}) {
                        r.line({cx + sx * s, cy + sy * s}, {cx + sx * (s - l), cy + sy * s}, c, 1.5f);
                        r.line({cx + sx * s, cy + sy * s}, {cx + sx * s, cy + sy * (s - l)}, c, 1.5f);
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
        else if (kind_ == Kind::Square)
            r.drawText({cx - r.measure(text_, FontId{17}).width / 2.f, midY(r, FontId{17}, b.y, b.h)}, text_, FontId{17}, ink);
    }

private:
    std::string text_;
    Kind        kind_;
    Glyph       glyph_;
    bool        on_{false};
};

// Un widget qui se dessine par une fonction de la page (le cadre du zoom, la carte d'une ville) ; `blocks` : il prend
// les clics (la carte : un clic dessus ne fait pas tourner le globe dessous).
class PaintBox final : public ui::Widget {
public:
    PaintBox(std::string id, std::function<void(const ui::PaintContext&, const Rect&)> paint, bool blocks)
        : ui::Widget(std::move(id)), paint_(std::move(paint)), blocks_(blocks) {}

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        if (paint_) paint_(ctx, bounds());
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (!blocks_) return ui::EventResult::Ignored;
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && bounds().contains(m->pos)) return ui::EventResult::Consumed;
        if (const auto* m = std::get_if<ui::MouseUp>(&ev); m && bounds().contains(m->pos)) return ui::EventResult::Consumed;
        if (const auto* m = std::get_if<ui::MouseWheel>(&ev); m && bounds().contains(m->pos)) return ui::EventResult::Consumed;
        return ui::EventResult::Ignored;
    }
    [[nodiscard]] bool hitTest(gfx::Point local) const override { return blocks_ && ui::Widget::hitTest(local); }

private:
    std::function<void(const ui::PaintContext&, const Rect&)> paint_;
    bool                                                      blocks_;
};

// Les admins des villes d'essai (la carte d'une ville : << 1 Camille Essai >>), dans l'ordre des couleurs des admins.
std::string adminOf(const std::string& city) {
    if (city == "Lyon") return "Camille Essai";
    if (city == "Paris") return "L\xC3\xA9" "a Essai";
    if (city == "Grenoble") return "Hugo Essai";
    return {};
}

class GlobeTestPage final : public Page {
public:
    explicit GlobeTestPage(ShellHost host) : Page("page.globe-essai", std::move(host)), places_(gl::samplePlaces()) {
        // L'ordre des enfants est celui du dessin : le globe, ce qui est pose dessus, puis les boutons.
        layers_ = add(*this, std::make_unique<ui::GlobeLayerList>("globe-essai.calques"));
        globe_ = add(*this, std::make_unique<ui::GlobeView>("globe-essai.globe"));
        overGlobe_ = add(*this, std::make_unique<PaintBox>("globe-essai.dessus", [this](const ui::PaintContext& c, const Rect&) { paintOverGlobe(c); }, false));
        zoomFrame_ = add(*this, std::make_unique<PaintBox>("globe-essai.cadre-zoom", [](const ui::PaintContext& c, const Rect& z) {
            c.r.fillRoundedRect({z.x, z.y + 1.f, z.w, z.h}, Color{15, 23, 42, 14}, 10.f);
            ui::shapes::fillFramedRect(c.r, z, kWhite, kLine, 10.f);
            c.r.fillRect({z.x + 1.f, z.y + 30.f, z.w - 2.f, 1.f}, kLine);
        }, false));
        cities_ = add(*this, std::make_unique<ui::GlobeCityList>("globe-essai.villes"));
        by_ = add(*this, std::make_unique<ui::DropDown>("globe-essai.par"));
        pause_ = add(*this, std::make_unique<FlatButton>("", "globe-essai.pause", FlatButton::Kind::Round, FlatButton::Glyph::Pause));
        recenter_ = add(*this, std::make_unique<FlatButton>("Recentrer", "globe-essai.recentrer", FlatButton::Kind::Pill, FlatButton::Glyph::Pin));
        zoomIn_ = add(*this, std::make_unique<FlatButton>("+", "globe-essai.zoom-plus", FlatButton::Kind::Square));
        zoomOut_ = add(*this, std::make_unique<FlatButton>("\xE2\x88\x92", "globe-essai.zoom-moins", FlatButton::Kind::Square));
        cityCard_ = add(*this, std::make_unique<PaintBox>("globe-essai.carte", [this](const ui::PaintContext& c, const Rect&) { paintCityCard(c); }, true));
        close_ = add(*this, std::make_unique<FlatButton>("", "globe-essai.fermer", FlatButton::Kind::Square, FlatButton::Glyph::Close));
        count_ = add(*this, std::make_unique<FlatButton>("Compte", "globe-essai.compte", FlatButton::Kind::Segment));
        live_ = add(*this, std::make_unique<FlatButton>("En direct", "globe-essai.direct", FlatButton::Kind::Segment, FlatButton::Glyph::Dot));
        full_ = add(*this, std::make_unique<FlatButton>("", "globe-essai.plein", FlatButton::Kind::Icon, FlatButton::Glyph::Full));
        freeze_ = add(*this, std::make_unique<ui::Checkbox>("Figer (captures)", "globe-essai.fige"));
        hover_ = add(*this, std::make_unique<ui::Button>("Survoler Paris", "globe-essai.survol"));
        ping_ = add(*this, std::make_unique<ui::Button>("Une connexion", "globe-essai.connexion"));
        mini_ = add(*this, std::make_unique<ui::GlobeView>("globe-essai.petite"));

        pause_->setTooltip("Arr\xC3\xAAter / reprendre la rotation");
        full_->setTooltip("Plein \xC3\xA9" "cran");
        close_->setTooltip("Fermer");
        freeze_->setTooltip("Pour les captures : la vue de la planche (Lyon), plus rien ne bouge avec le temps.");
        hover_->setTooltip("Pour les captures : la souris pos\xC3\xA9" "e sur Paris (son infobulle).");
        ping_->setTooltip("En direct : un PC d'une ville d'essai se connecte (une bouff\xC3\xA9" "e sur le globe).");
        count_->setOn(true);

        globe_->setPlaces(places_);
        globe_->setHome(38.0, 4.85);
        globe_->setView(38.0, 4.85, 1.f);
        mini_->setCompact(true);
        mini_->setPlaces(places_);
        mini_->setLayers(gl::LayerSet::of({gl::Layer::ActivePcs}));
        mini_->setView(36.0, 0.0, 1.f);
        cities_->setPlaces(places_);
        std::array<int, gl::kLayerCount> totals{};
        for (const auto& p : places_)
            for (std::size_t i = 0; i < gl::kLayerCount; ++i) totals[i] += p.counts[i];
        layers_->setTotals(totals);
        layers_->setHint(gl::Layer::Outdated, "un PC pas en 1.47.0");
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
        links_ += count_->clicked->connect([this] { setLive(false); });
        links_ += live_->clicked->connect([this] { setLive(true); });
        links_ += full_->clicked->connect([this] {
            fullScreen_ = !fullScreen_;
            invalidateLayout();
        });
        links_ += close_->clicked->connect([this] { choose(-1); });
        links_ += freeze_->stateChanged->connect([this](ui::Checkbox::State s) { setFrozen(s == ui::Checkbox::State::Checked); });
        links_ += hover_->clicked->connect([this] {
            const int i = globe_->findPlace("FR|Paris");
            if (const auto at = globe_->screenOf(i)) globe_->hoverAt(*at);
        });
        links_ += ping_->clicked->connect([this] { ping(); });
        syncBy();
        close_->setVisibility(ui::Visibility::Collapsed);
        cityCard_->setVisibility(ui::Visibility::Collapsed);
    }
    ~GlobeTestPage() override { links_.clear(); }

    [[nodiscard]] std::string title() const override { return "Le globe (essai)"; }
    void refresh() override { invalidate(); }

protected:
    void onLayout() override {
        const Rect b = bounds();
        constexpr float kPad = 24.f;
        // Le haut : le titre, et les boutons des captures a droite.
        float rx = b.right() - kPad;
        for (ui::Widget* w : {static_cast<ui::Widget*>(ping_), static_cast<ui::Widget*>(hover_)}) {
            const float bw = std::max(110.f, w->sizeHint().preferred.w + 8.f);
            w->setBounds({rx - bw, b.y + 26.f, bw, 32.f});
            rx -= bw + 8.f;
        }
        const float fw = freeze_->sizeHint().preferred.w + 6.f;
        freeze_->setBounds({rx - fw, b.y + 26.f, fw, 32.f});
        for (ui::Widget* w : {static_cast<ui::Widget*>(ping_), static_cast<ui::Widget*>(hover_), static_cast<ui::Widget*>(freeze_)})
            w->setVisibility(fullScreen_ ? ui::Visibility::Collapsed : ui::Visibility::Visible);   // plein ecran : la carte seule
        titleRight_ = rx - fw - 12.f;
        // La carte du globe (en plein ecran : toute la page).
        const float miniH = 150.f;
        card_ = fullScreen_ ? b : Rect{b.x + kPad, b.y + 82.f, b.w - 2.f * kPad, std::max(320.f, b.bottom() - 18.f - miniH - 14.f - (b.y + 82.f))};
        const float leftW = fullScreen_ ? 260.f : 256.f, rightW = fullScreen_ ? 380.f : 326.f;
        const float top = card_.y + 52.f, bottom = card_.bottom() - 34.f;
        layers_->setBounds({card_.x + 1.f, top, leftW - 1.f, bottom - top});
        globeArea_ = {card_.x + leftW, top, std::max(120.f, card_.w - leftW - rightW), bottom - top};
        globe_->setBounds(globeArea_);
        right_ = {globeArea_.right(), top, rightW - 1.f, bottom - top};
        by_->setBounds({right_.right() - 16.f - 150.f, right_.y + 12.f, 150.f, 28.f});
        cities_->setBounds({right_.x + 8.f, right_.y + 76.f, right_.w - 16.f, std::max(30.f, right_.bottom() - 8.f - (right_.y + 76.f))});
        // L'en-tete de la carte : Compte / En direct, plein ecran.
        full_->setBounds({card_.right() - 14.f - 32.f, card_.y + 10.f, 32.f, 32.f});
        const float lw = live_->wanted(), cw = count_->wanted();
        live_->setBounds({full_->bounds().x - 10.f - 3.f - lw, card_.y + 12.f, lw, 28.f});
        count_->setBounds({live_->bounds().x - cw, card_.y + 12.f, cw, 28.f});
        seg_ = {count_->bounds().x - 3.f, card_.y + 9.f, cw + lw + 6.f, 34.f};
        // Dans le globe : la pause, Recentrer, le zoom.
        const Rect& g = globeArea_;
        pause_->setBounds({g.x + 14.f, g.bottom() - 12.f - 32.f, 32.f, 32.f});
        const float rw = recenter_->wanted();
        recenter_->setBounds({pause_->bounds().right() + 6.f, g.bottom() - 12.f - 32.f, rw, 32.f});
        zoomIn_->setBounds({g.right() - 14.f - 32.f, g.bottom() - 12.f - 61.f, 32.f, 30.f});
        zoomOut_->setBounds({g.right() - 14.f - 32.f, g.bottom() - 12.f - 30.f, 32.f, 30.f});
        zoomFrame_->setBounds({g.right() - 14.f - 32.f, g.bottom() - 12.f - 61.f, 32.f, 61.f});
        overGlobe_->setBounds(g);
        // La carte d'une ville (en haut a droite du globe).
        cardBox_ = {g.right() - 14.f - 244.f, g.y + 14.f, 244.f, cardHeight()};
        cityCard_->setBounds(cardBox_);
        close_->setBounds({cardBox_.right() - 12.f - 22.f, cardBox_.y + 9.f, 22.f, 22.f});
        // La petite carte du tableau de bord.
        mini_->setVisibility(fullScreen_ ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        miniCard_ = {b.x + kPad, b.bottom() - 18.f - miniH + 22.f, 476.f, miniH - 22.f};
        mini_->setBounds({miniCard_.x + 10.f, miniCard_.y + 34.f, 92.f, 92.f});
        syncReserved();
    }

    void onPaint(const ui::PaintContext& ctx) override {
        gfx::IRenderer& r = ctx.r;
        const auto& c = ctx.theme.color;
        const Rect b = bounds();
        r.fillRect(b, c.windowBg);
        if (!fullScreen_) {
            // Le titre
            r.drawText({b.x + 24.f, b.y + 18.f}, "Le globe", gfx::semibold(FontId{28}), kInk);
            const float tw = r.measure("Le globe", gfx::semibold(FontId{28})).width;
            const std::string tag = "page d'essai";
            const FontId ft = gfx::semibold(FontId{11});
            const float pw = r.measure(tag, ft).width + 16.f;
            r.fillRoundedRect({b.x + 24.f + tw + 12.f, b.y + 27.f, pw, 20.f}, Color{254, 243, 199, 255}, 10.f);
            r.drawText({b.x + 24.f + tw + 20.f, midY(r, ft, b.y + 27.f, 20.f)}, tag, ft, Color{146, 64, 14, 255});
            int clients = 0, pcs = 0;
            std::set<std::string> countries;
            for (const auto& p : places_) {
                clients += p.count(gl::Layer::Clients);
                pcs += p.pcs;
                countries.insert(p.country);
            }
            const std::string sub = "Les 21 villes d'essai de la maquette, rien du service (--page globe-essai) \xC2\xB7 " + std::to_string(clients) + " clients, " +
                                    std::to_string(pcs) + " PC, " + std::to_string(places_.size()) + " villes, " + std::to_string(countries.size()) + " pays";
            r.drawText({b.x + 24.f, b.y + 56.f}, fitText(r, sub, FontId{13}, titleRight_ - b.x - 24.f), FontId{13}, kMuted);
        }
        // La carte du globe
        r.fillRoundedRect({card_.x, card_.y + 1.f, card_.w, card_.h + 1.f}, Color{15, 23, 42, 10}, fullScreen_ ? 0.f : 14.f);
        ui::shapes::fillFramedRect(r, card_, kWhite, kLine, fullScreen_ ? 0.f : 14.f);
        // l'en-tete
        const Rect gi{card_.x + 18.f, card_.y + 11.f, 30.f, 30.f};
        r.fillRoundedRect(gi, kBlue50, 9.f);
        ui::drawIcon(r, ui::Icon::Globe, gi.inset(7.f, 7.f), kBlue);
        const float hx = gi.right() + 12.f;
        r.drawText({hx, card_.y + 9.f}, "D'o\xC3\xB9 se connectent vos clients", gfx::semibold(FontId{14}), kInk);
        r.drawText({hx, card_.y + 29.f},
                   fitText(r, "Les villes d'essai de la maquette \xC2\xB7 les points sont \xC3\xA0 la ville, jamais \xC3\xA0 l'adresse", FontId{11}, seg_.x - 16.f - hx),
                   FontId{11}, kMuted);
        r.fillRoundedRect(seg_, kSeg, 10.f);
        if (live_ && liveMode_) {
            const std::string t = "En direct \xC2\xB7 une connexion par clic";
            const FontId f = gfx::semibold(FontId{12});
            const float w = r.measure(t, f).width;
            r.fillRoundedRect({seg_.x - 18.f - w - 15.f, card_.y + 22.f, 8.f, 8.f}, kGreen, 4.f);
            r.drawText({seg_.x - 18.f - w, midY(r, f, card_.y + 9.f, 34.f)}, t, f, kGreenInk);
        }
        r.fillRect({card_.x, card_.y + 51.f, card_.w, 1.f}, kLine);
        // le fond du globe (globe.css, #gz : un degrade radial a peine)
        r.fillRect(globeArea_, kGlobeBg);
        r.fillRoundedRect(globeArea_.inset(globeArea_.w * 0.08f, globeArea_.h * 0.08f), Color{250, 251, 253, 255}, globeArea_.h * 0.4f);
        // la colonne des villes
        r.fillRect({right_.x, right_.y, 1.f, right_.h}, kLine);
        r.drawText({right_.x + 16.f, right_.y + 14.f}, "Les villes", gfx::semibold(FontId{14}), kInk);
        // << par >> et sa liste ; En direct : la liste est cachee, le texte va a droite.
        const std::string par = liveMode_ ? "PC connect\xC3\xA9s maintenant" : "par";
        const float parX = liveMode_ ? right_.right() - 16.f - r.measure(par, FontId{11}).width : by_->bounds().x - 8.f - r.measure(par, FontId{11}).width;
        r.drawText({parX, midY(r, FontId{11}, right_.y + 12.f, 28.f)}, par, FontId{11}, kMuted);
        const int shown = static_cast<int>(cities_->order().size());
        std::string sb = std::to_string(shown) + (shown > 1 ? " villes" : " ville") + " \xC2\xB7 " + std::to_string(cities_->countries()) + " pays";
        if (liveMode_) {
            int active = 0;
            for (const auto& p : places_) active += p.count(gl::Layer::ActivePcs);
            sb += " \xC2\xB7 " + std::to_string(active) + " PC";
        }
        r.drawText({right_.x + 16.f, right_.y + 50.f}, sb, FontId{11}, kFaint);
        const std::string hint = "un clic : le globe y va";
        r.drawText({right_.right() - 16.f - r.measure(hint, FontId{11}).width, right_.y + 50.f}, hint, FontId{11}, kFaint);
        // la note du bas
        const Rect note{card_.x + 1.f, card_.bottom() - 34.f, card_.w - 2.f, 33.f};
        r.fillRect(note, kSurface2);
        r.fillRect({card_.x, note.y, card_.w, 1.f}, kLine);
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
        if (!fullScreen_) paintBottom(ctx);
    }

private:
    // Sur le globe : << Il tourne seul >>, l'aide a cote de Recentrer.
    void paintOverGlobe(const ui::PaintContext& ctx) {
        gfx::IRenderer& r = ctx.r;
        const Rect& g = globeArea_;
        const FontId f = gfx::semibold(FontId{11});
        const std::string pill = globe_->autoRotate() ? "Il tourne seul \xC2\xB7 il s'arr\xC3\xAAte sous la souris" : "En pause \xC2\xB7 glisser pour le tourner";
        const float pw = r.measure(pill, f).width + 38.f;
        pill_ = {g.x + 14.f, g.y + 12.f, pw, 25.f};
        ui::shapes::fillFramedRect(r, pill_, Color{255, 255, 255, 230}, kLine, 12.5f);
        ui::drawIcon(r, ui::Icon::Refresh, {pill_.x + 9.f, pill_.y + 6.f, 13.f, 13.f}, kMuted);
        r.drawText({pill_.x + 28.f, midY(r, f, pill_.y, pill_.h)}, pill, f, kMuted);
        // l'aide, a cote de Recentrer
        const std::string help = "Glisser pour le tourner \xC2\xB7 molette pour zoomer";
        const Rect rc = recenter_->bounds();
        const float hw = r.measure(help, FontId{11}).width + 18.f;
        if (rc.right() + 10.f + hw < zoomIn_->bounds().x - 8.f) {
            r.fillRoundedRect({rc.right() + 10.f, rc.y + 5.f, hw, 22.f}, Color{255, 255, 255, 225}, 11.f);
            r.drawText({rc.right() + 19.f, midY(r, FontId{11}, rc.y + 5.f, 22.f)}, help, FontId{11}, kMuted);
        }
    }

    [[nodiscard]] float cardHeight() const { return 46.f + 7.f * 25.f + 10.f; }

    void paintCityCard(const ui::PaintContext& ctx) {
        if (selected_ < 0) return;
        gfx::IRenderer& r = ctx.r;
        const auto& p = places_[static_cast<std::size_t>(selected_)];
        const Rect& k = cardBox_;
        r.fillRoundedRect({k.x, k.y + 4.f, k.w, k.h + 4.f}, Color{15, 23, 42, 14}, 12.f);
        ui::shapes::fillFramedRect(r, k, kWhite, kLine, 12.f);
        r.drawText({k.x + 12.f, k.y + 10.f}, p.label(), gfx::semibold(FontId{14}), kInk);
        r.drawText({k.x + 12.f, k.y + 29.f}, fitText(r, p.region, FontId{11}, k.w - 50.f), FontId{11}, kMuted);
        float y = k.y + 46.f;
        for (const auto& st : gl::styles()) {
            const int n = p.count(st.layer);
            std::string extra;
            if (st.layer == gl::Layer::ActivePcs && p.pcs > 0) extra = "sur " + std::to_string(p.pcs) + " PC";
            if (st.layer == gl::Layer::Admins && n > 0) extra = adminOf(p.city);
            if (st.layer == gl::Layer::Orders) extra = "30 j";
            const bool zero = n == 0;
            r.fillRect({k.x + 12.f, y, k.w - 24.f, 1.f}, Color{240, 243, 247, 255});
            ui::GlobeView::drawGlyph(r, st.layer, {k.x + 12.f, y + 5.5f, 14.f, 14.f}, !zero);
            const FontId fl{12}, fn = gfx::semibold(FontId{12}), fx{11};
            r.drawText({k.x + 34.f, midY(r, fl, y, 25.f)}, st.shortName, fl, zero ? kFaint : kInk2);
            float rx = k.right() - 12.f;
            if (!extra.empty()) {
                const float ew = r.measure(extra, fx).width;
                r.drawText({rx - ew, midY(r, fx, y, 25.f) + 1.f}, extra, fx, zero ? kFaint : kMuted);
                rx -= ew + 4.f;
            }
            const std::string num = std::to_string(n);
            r.drawText({rx - r.measure(num, fn).width, midY(r, fn, y, 25.f)}, num, zero ? FontId{12} : fn, zero ? kFaint : kInk);
            y += 25.f;
        }
    }

    // La petite carte du tableau de bord, et ce que montre la page.
    void paintBottom(const ui::PaintContext& ctx) {
        gfx::IRenderer& r = ctx.r;
        const Rect& m = miniCard_;
        const FontId fo = gfx::semibold(FontId{11});
        r.drawText({m.x, m.y - 20.f}, "SUR LE TABLEAU DE BORD : LA PETITE CARTE", fo, kFaint);
        r.fillRoundedRect({m.x, m.y + 1.f, m.w, m.h + 1.f}, Color{15, 23, 42, 10}, 12.f);
        ui::shapes::fillFramedRect(r, m, kWhite, kLine, 12.f);
        ui::drawIcon(r, ui::Icon::Globe, {m.x + 14.f, m.y + 11.f, 15.f, 15.f}, kBlue);
        r.drawText({m.x + 36.f, m.y + 9.f}, "Le globe", gfx::semibold(FontId{13}), kInk);
        int active = 0;
        for (const auto& p : places_) active += p.count(gl::Layer::ActivePcs);
        const float lx = m.x + 36.f + r.measure("Le globe", gfx::semibold(FontId{13})).width + 14.f;
        r.fillRoundedRect({lx, m.y + 15.f, 8.f, 8.f}, kGreen, 4.f);
        r.drawText({lx + 14.f, m.y + 10.f}, std::to_string(active) + " PC actifs maintenant", gfx::semibold(FontId{12}), kGreenInk);
        // les 3 premieres villes (clients)
        std::vector<int> order(places_.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return places_[static_cast<std::size_t>(a)].count(gl::Layer::Clients) > places_[static_cast<std::size_t>(b)].count(gl::Layer::Clients);
        });
        const float x0 = m.x + 124.f;
        r.drawText({x0, m.y + 38.f}, "Les 3 premi\xC3\xA8res villes \xC2\xB7 clients", FontId{11}, kMuted);
        const int top = std::max(1, places_[static_cast<std::size_t>(order[0])].count(gl::Layer::Clients));
        for (int k = 0; k < 3 && k < static_cast<int>(order.size()); ++k) {
            const auto& p = places_[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
            const float y = m.y + 58.f + static_cast<float>(k) * 22.f;
            r.drawText({x0, y}, p.label(), FontId{12}, kInk);
            const float bx = x0 + 92.f, bw = m.right() - 16.f - 26.f - bx;
            r.fillRoundedRect({bx, y + 5.f, bw * static_cast<float>(p.count(gl::Layer::Clients)) / static_cast<float>(top), 7.f}, kBlue, 3.f);
            const std::string n = std::to_string(p.count(gl::Layer::Clients));
            r.drawText({m.right() - 16.f - r.measure(n, gfx::semibold(FontId{12})).width, y}, n, gfx::semibold(FontId{12}), kInk);
        }
        // ce que montre la page (les numeros de la planche)
        const char* notes[] = {
            "Il tourne seul, doucement. On le fait tourner \xC3\xA0 la souris (et on peut le lancer) ; il s'arr\xC3\xAAte d\xC3\xA8s que la souris est dessus. "
            "La molette zoome.",
            "Les calques se cochent ; chacun a sa couleur et sa forme. Les villes trop proches se groupent (\xC2\xAB Paris +6 \xC2\xBB) : on zoome, elles se "
            "s\xC3\xA9parent.",
            "Un clic sur une ville (le globe ou la liste) : le globe tourne vers elle et sa carte s'ouvre.",
            "En direct : un point pulse \xC3\xA0 chaque connexion d'un PC (ici : \xC2\xAB Une connexion \xC2\xBB). Compte : les totaux.",
        };
        const float ex = m.right() + 24.f, ew = (bounds().right() - 24.f - ex - 18.f) / 2.f;
        for (int i = 0; i < 4; ++i) {
            const float cx = ex + static_cast<float>(i % 2) * (ew + 18.f), cy = m.y - 4.f + static_cast<float>(i / 2) * 66.f;
            r.fillRoundedRect({cx, cy + 1.f, 19.f, 19.f}, kYellow, 9.5f);
            const std::string n = std::to_string(i + 1);
            r.drawText({cx + (19.f - r.measure(n, gfx::semibold(FontId{11})).width) / 2.f, midY(r, gfx::semibold(FontId{11}), cy + 1.f, 19.f)}, n,
                       gfx::semibold(FontId{11}), kInk);
            // le texte coupe en lignes (aux espaces)
            std::string line, text = notes[i];
            float ty = cy;
            std::size_t pos = 0;
            int lines = 0;
            while (pos <= text.size() && lines < 3) {
                std::size_t sp = text.find(' ', pos);
                if (sp == std::string::npos) sp = text.size();
                const std::string word = text.substr(pos, sp - pos);
                const std::string tried = line.empty() ? word : line + " " + word;
                if (!line.empty() && r.measure(tried, FontId{12}).width > ew - 28.f) {
                    r.drawText({cx + 28.f, ty}, line, FontId{12}, kInk2);
                    ty += 17.f;
                    ++lines;
                    line = word;
                } else {
                    line = tried;
                }
                pos = sp + 1;
            }
            if (lines < 3 && !line.empty()) r.drawText({cx + 28.f, ty}, line, FontId{12}, kInk2);
        }
    }

    // Choisir une ville (le globe, la liste) : le globe tourne vers elle, sa carte s'ouvre ; -1 : rien.
    void choose(int i) {
        selected_ = i >= 0 && i < static_cast<int>(places_.size()) ? i : -1;
        globe_->setSelected(selected_);
        cities_->setSelected(selected_);
        if (selected_ >= 0) {
            const auto& p = places_[static_cast<std::size_t>(selected_)];
            globe_->turnTo(p.lat, p.lon, std::max(globe_->view().zoom, 1.45f), 1.1);
            globe_->setLeaderTarget(gfx::Point{cardBox_.x, cardBox_.y + 22.f});
        } else {
            globe_->setLeaderTarget(std::nullopt);
        }
        close_->setVisibility(selected_ >= 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        cityCard_->setVisibility(selected_ >= 0 ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        syncReserved();
        invalidate();
    }

    // Les noms des villes ne vont ni sous les boutons, ni sous la carte d'une ville.
    void syncReserved() {
        std::vector<Rect> areas;
        const Rect& g = globeArea_;
        areas.push_back({g.x, g.y, std::max(pill_.w, 330.f) + 14.f, 44.f});
        areas.push_back({g.x, g.bottom() - 52.f, g.w, 52.f});
        if (selected_ >= 0) areas.push_back(cardBox_.inset(-6.f, -6.f));
        globe_->setReservedAreas(std::move(areas));
        if (selected_ >= 0) globe_->setLeaderTarget(gfx::Point{cardBox_.x, cardBox_.y + 22.f});
        // Une fenetre etroite : la carte couvrirait la ville choisie (au centre du disque) - le disque glisse a gauche, la
        // ville reste a 60 px de la carte. Assez large (la planche) : rien ne bouge.
        const float room = (cardBox_.x - 60.f) - (g.x + g.w / 2.f);
        globe_->setCentreShift(selected_ >= 0 ? std::min(0.f, room) : 0.f);
    }

    void setFrozen(bool on) {
        globe_->setFrozen(on);
        mini_->setFrozen(on);
        if (on) {
            // La vue de la planche : l'Europe, Lyon choisie (zoom 1,45), Compte.
            setLive(false);
            globe_->setView(45.76, 4.85, 1.45f);
            mini_->setView(36.0, 0.0, 1.f);
            choose(globe_->findPlace("FR|Lyon"));
        } else {
            choose(-1);
            globe_->setView(38.0, 4.85, 1.f);
        }
    }

    void setLive(bool on) {
        liveMode_ = on;
        count_->setOn(!on);
        live_->setOn(on);
        if (on) {
            auto s = globe_->layers();
            s.set(gl::Layer::ActivePcs, true);
            globe_->setLayers(s);
            layers_->setLayers(s);
        }
        syncBy();
        invalidate();
    }

    // Une connexion : la prochaine ville d'essai qui a des PC actifs (un tour dans l'ordre), une bouffee sur le globe.
    void ping() {
        if (!liveMode_) setLive(true);
        for (std::size_t n = 0; n < places_.size(); ++n) {
            nextPing_ = (nextPing_ + 1) % static_cast<int>(places_.size());
            if (places_[static_cast<std::size_t>(nextPing_)].count(gl::Layer::ActivePcs) > 0) break;
        }
        globe_->pulse(nextPing_);
        mini_->pulse(nextPing_);
    }

    // << par >> : les calques coches ; En direct : les PC actifs.
    void syncBy() {
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
        if (liveMode_) {
            cities_->setLayer(gl::Layer::ActivePcs);
            by_->setVisibility(ui::Visibility::Hidden);
        } else {
            by_->setVisibility(ui::Visibility::Visible);
            if (!byLayers_.empty() && !s.has(cities_->layer())) keep = 0;
            by_->setSelectedIndex(keep);
            if (!byLayers_.empty()) cities_->setLayer(byLayers_[static_cast<std::size_t>(keep)]);
        }
    }

    std::vector<gl::Place>  places_;
    ui::GlobeLayerList*     layers_{nullptr};
    ui::GlobeView*          globe_{nullptr};
    ui::GlobeView*          mini_{nullptr};
    ui::GlobeCityList*      cities_{nullptr};
    ui::DropDown*           by_{nullptr};
    PaintBox *              overGlobe_{nullptr}, *zoomFrame_{nullptr}, *cityCard_{nullptr};
    FlatButton *            pause_{nullptr}, *recenter_{nullptr}, *zoomIn_{nullptr}, *zoomOut_{nullptr}, *count_{nullptr}, *live_{nullptr},
        *full_{nullptr}, *close_{nullptr};
    ui::Checkbox*           freeze_{nullptr};
    ui::Button *            hover_{nullptr}, *ping_{nullptr};
    std::vector<gl::Layer>  byLayers_;
    Rect                    card_{}, globeArea_{}, right_{}, seg_{}, cardBox_{}, miniCard_{}, pill_{};
    float                   titleRight_{0.f};
    int                     selected_{-1}, nextPing_{-1};
    bool                    liveMode_{false}, fullScreen_{false};
    core::ConnectionScope   links_;
};

} // namespace

PagePtr makeGlobeTestPage(ShellHost host) { return std::make_unique<GlobeTestPage>(std::move(host)); }

} // namespace license_admin::gui
