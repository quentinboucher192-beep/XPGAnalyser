// =============================================================================
//  license_admin/gui/PageGlobeTest.cpp - 1.47.0 : LA PAGE D'ESSAI DU GLOBE
//  (cachee : --page globe-essai ; jamais dans le menu)
// -----------------------------------------------------------------------------
//  La carte du globe (GlobeBoard.hpp) avec les 21 villes d'essai de la maquette
//  (globe::samplePlaces) - RIEN du service : c'est dit en tete. La vraie vue
//  (Statistiques > Le globe, PageStatsGlobe.cpp) lit GET /admin/globe ; celle-ci
//  sert a voir le globe tourner, a le capturer et a le comparer a la planche 11 :
//    - la carte du globe (calques, globe, villes, carte d'une ville, Compte / En
//      direct, plein ecran : la carte prend la page) ;
//    - en bas, la petite carte du tableau de bord (GlobeMiniCard) et ce que la
//      planche explique ;
//    - POUR LES CAPTURES (en haut a droite) : << Figer >> (globe-essai.fige : la vue
//      de la planche, centree sur Lyon, zoom 1,45, Lyon choisie ; plus rien ne
//      bouge avec le temps - le #fige de la maquette), << Survoler Paris >>
//      (globe-essai.survol : la souris posee sur Paris, son infobulle), << Une
//      connexion >> (globe-essai.connexion : une bouffee En direct sur la
//      prochaine ville d'essai).
//  XPG-License-Admin --page globe-essai --taille 1373x917 --cliquer globe-essai.fige --capture a.png
// =============================================================================
#include "GlobeBoard.hpp"
#include "Page.hpp"

#include "../../ui/Shapes.hpp"

#include <algorithm>
#include <cmath>
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
using namespace globekit;

constexpr Color kYellow{250, 204, 21, 255};

template <class T>
T* add(ui::Widget& parent, std::unique_ptr<T> w) {
    T* p = w.get();
    parent.addChild(std::move(w));
    return p;
}

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
        board_ = add(*this, std::make_unique<GlobeBoard>("globe-essai"));
        freeze_ = add(*this, std::make_unique<ui::Checkbox>("Figer (captures)", "globe-essai.fige"));
        hover_ = add(*this, std::make_unique<ui::Button>("Survoler Paris", "globe-essai.survol"));
        ping_ = add(*this, std::make_unique<ui::Button>("Une connexion", "globe-essai.connexion"));
        mini_ = add(*this, std::make_unique<GlobeMiniCard>("globe-essai.petite"));
        freeze_->setTooltip("Pour les captures : la vue de la planche (Lyon), plus rien ne bouge avec le temps.");
        hover_->setTooltip("Pour les captures : la souris pos\xC3\xA9" "e sur Paris (son infobulle).");
        ping_->setTooltip("En direct : un PC d'une ville d'essai se connecte (une bouff\xC3\xA9" "e sur le globe).");

        std::vector<std::string> admins;
        std::array<int, gl::kLayerCount> totals{};
        for (const auto& p : places_) {
            admins.push_back(adminOf(p.city));
            for (std::size_t i = 0; i < gl::kLayerCount; ++i) totals[i] += p.counts[i];
        }
        board_->setPlaces(places_, admins);
        board_->setTotals(totals);
        board_->setHint(gl::Layer::Outdated, "un PC pas en 1.47.0");
        board_->setHeader("D'o\xC3\xB9 se connectent vos clients",
                          "Les villes d'essai de la maquette \xC2\xB7 les points sont \xC3\xA0 la ville, jamais \xC3\xA0 l'adresse");
        board_->setLiveLabel("En direct \xC2\xB7 une connexion par clic");
        board_->onFullScreen = [this](bool) { invalidateLayout(); };
        mini_->setPlaces(places_);
        int active = 0;
        for (const auto& p : places_) active += p.count(gl::Layer::ActivePcs);
        board_->setActiveTotal(active);
        mini_->setActive(active);

        links_ += freeze_->stateChanged->connect([this](ui::Checkbox::State s) {
            const bool on = s == ui::Checkbox::State::Checked;
            mini_->globe().setFrozen(on);
            if (on) {
                board_->freezeOn(board_->globe().findPlace("FR|Lyon"));
                mini_->globe().setView(36.0, 0.0, 1.f);
            } else {
                board_->unfreeze();
            }
        });
        links_ += hover_->clicked->connect([this] {
            auto& g = board_->globe();
            if (const auto at = g.screenOf(g.findPlace("FR|Paris"))) g.hoverAt(*at);
        });
        links_ += ping_->clicked->connect([this] { ping(); });
    }
    ~GlobeTestPage() override { links_.clear(); }

    [[nodiscard]] std::string title() const override { return "Le globe (essai)"; }
    void refresh() override { invalidate(); }

protected:
    void onLayout() override {
        const Rect b = bounds();
        constexpr float kPad = 24.f;
        const bool full = board_->fullScreen();
        float rx = b.right() - kPad;
        for (ui::Widget* w : {static_cast<ui::Widget*>(ping_), static_cast<ui::Widget*>(hover_)}) {
            const float bw = std::max(110.f, w->sizeHint().preferred.w + 8.f);
            w->setBounds({rx - bw, b.y + 26.f, bw, 32.f});
            rx -= bw + 8.f;
        }
        const float fw = freeze_->sizeHint().preferred.w + 6.f;
        freeze_->setBounds({rx - fw, b.y + 26.f, fw, 32.f});
        titleRight_ = rx - fw - 12.f;
        for (ui::Widget* w : {static_cast<ui::Widget*>(ping_), static_cast<ui::Widget*>(hover_), static_cast<ui::Widget*>(freeze_),
                              static_cast<ui::Widget*>(mini_)})
            w->setVisibility(full ? ui::Visibility::Collapsed : ui::Visibility::Visible);   // plein ecran : la carte seule
        const float miniH = 150.f;
        board_->setBounds(full ? b : Rect{b.x + kPad, b.y + 82.f, b.w - 2.f * kPad, std::max(320.f, b.bottom() - 18.f - miniH - 14.f - (b.y + 82.f))});
        mini_->setBounds({b.x + kPad, b.bottom() - 18.f - miniH + 22.f, 476.f, miniH - 22.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        gfx::IRenderer& r = ctx.r;
        const Rect b = bounds();
        r.fillRect(b, ctx.theme.color.windowBg);
        if (board_->fullScreen()) return;
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
        paintNotes(ctx);
    }

private:
    // La petite carte (son titre) et ce que la planche explique, a sa droite.
    void paintNotes(const ui::PaintContext& ctx) {
        gfx::IRenderer& r = ctx.r;
        const Rect m = mini_->bounds();
        r.drawText({m.x, m.y - 20.f}, "SUR LE TABLEAU DE BORD : LA PETITE CARTE", gfx::semibold(FontId{11}), kFaint);
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

    // Une connexion : la prochaine ville d'essai qui a des PC actifs (un tour dans l'ordre), une bouffee sur le globe.
    void ping() {
        if (!board_->live()) board_->setLive(true);
        for (std::size_t n = 0; n < places_.size(); ++n) {
            nextPing_ = (nextPing_ + 1) % static_cast<int>(places_.size());
            if (places_[static_cast<std::size_t>(nextPing_)].count(gl::Layer::ActivePcs) > 0) break;
        }
        board_->globe().pulse(nextPing_);
        mini_->globe().pulse(nextPing_);
        board_->setTicker(places_[static_cast<std::size_t>(nextPing_)].label(), "un Studio connect\xC3\xA9", "(essai)");
    }

    std::vector<gl::Place> places_;
    GlobeBoard*            board_{nullptr};
    GlobeMiniCard*         mini_{nullptr};
    ui::Checkbox*          freeze_{nullptr};
    ui::Button *           hover_{nullptr}, *ping_{nullptr};
    float                  titleRight_{0.f};
    int                    nextPing_{-1};
    core::ConnectionScope  links_;
};

} // namespace

PagePtr makeGlobeTestPage(ShellHost host) { return std::make_unique<GlobeTestPage>(std::move(host)); }

} // namespace license_admin::gui
