// =============================================================================
//  license_admin/gui/GlobeBoard.hpp - 1.47.0 : LA CARTE DU GLOBE (planche 11)
// -----------------------------------------------------------------------------
//  Ce que montrent la vue << Le globe >> de Statistiques (PageStatsGlobe.cpp, les
//  chiffres du service) et la page d'essai cachee (PageGlobeTest.cpp, les villes
//  de la maquette) : la carte #gcarte de la planche, les memes mesures que
//  globe.css -
//    - l'en-tete : l'icone, le titre et sa ligne, Compte / En direct, plein ecran ;
//    - a gauche les calques (ui::GlobeLayerList), au milieu le globe
//      (ui::GlobeView) avec << Il tourne seul >>, la pause, Recentrer, l'aide,
//      + / - ; a droite les villes (<< par >> et sa liste, ui::GlobeCityList) ;
//    - la carte d'une ville choisie (le globe ou la liste) : ses nombres par
//      calque, le trait vers son point, << Voir les N clients >> / << Les N PC >>
//      (la page decide : onClients, onPcs ; setCardActions) ;
//    - en bas, la note : d'ou viennent les points (request.cf, jamais l'adresse IP) ;
//    - un message au milieu du globe (setEmptyMessage : aucune ville encore, le
//      service a deployer) et un etat (setStatus : << Releve en cours... >>).
//  Les identifiants (les scripts, --cliquer) : <prefixe>.globe, .calques, .villes,
//  .par, .pause, .recentrer, .zoom-plus, .zoom-moins, .carte, .fermer,
//  .voir-clients, .voir-pc, .compte, .direct, .plein.
//  Le plein ecran : la page donne a la carte toute sa place (fullScreen()).
//
//  GlobeMiniCard : la petite carte du tableau de bord (planche 11, en bas) - le
//  globe compact (les PC actifs), << 42 PC actifs maintenant >>, les 3 premieres
//  villes ; un clic : onOpen.
// =============================================================================
#pragma once

#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/Globe.hpp"
#include "../../ui/widgets/GlobeLists.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace globekit {

// Les couleurs de la maquette (ds.css).
inline constexpr gfx::Color kInk{15, 23, 42, 255};
inline constexpr gfx::Color kInk2{51, 65, 85, 255};
inline constexpr gfx::Color kMuted{100, 116, 139, 255};
inline constexpr gfx::Color kFaint{148, 163, 184, 255};
inline constexpr gfx::Color kLine{227, 232, 239, 255};
inline constexpr gfx::Color kSurface2{248, 250, 252, 255};
inline constexpr gfx::Color kWhite{255, 255, 255, 255};
inline constexpr gfx::Color kBlue{42, 120, 214, 255};
inline constexpr gfx::Color kBlue50{238, 244, 255, 255};
inline constexpr gfx::Color kGreen{27, 175, 122, 255};
inline constexpr gfx::Color kGreenInk{15, 122, 85, 255};

[[nodiscard]] std::string fitText(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w);
[[nodiscard]] float midY(const gfx::IRenderer& r, gfx::FontId f, float top, float h);

// Les boutons de la planche : rond (la pause), en pilule (Recentrer), carre (+ / -), un segment (Compte / En direct),
// une icone (plein ecran), un bouton de la carte d'une ville (Voir les 9 clients).
class FlatButton final : public ui::Button {
public:
    enum class Kind : std::uint8_t { Round, Pill, Square, Segment, Icon, Card, CardGhost };
    enum class Glyph : std::uint8_t { None, Pause, Play, Full, Close, Pin, Dot };
    FlatButton(std::string text, std::string id, Kind k, Glyph g = Glyph::None);
    void setOn(bool on);
    void setGlyph(Glyph g);
    void setLabel(std::string text);
    [[nodiscard]] const std::string& label() const noexcept { return text_; }
    [[nodiscard]] float wanted() const;
    [[nodiscard]] ui::SizeHint sizeHint() const override;

protected:
    void onPaint(const ui::PaintContext& ctx) override;

private:
    std::string text_;
    Kind        kind_;
    Glyph       glyph_;
    bool        on_{false};
};

// Un widget qui se dessine par une fonction (le cadre du zoom, la carte d'une ville) ; `blocks` : il prend les clics.
class PaintBox final : public ui::Widget {
public:
    PaintBox(std::string id, std::function<void(const ui::PaintContext&, const gfx::Rect&)> paint, bool blocks);

protected:
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;
    [[nodiscard]] bool hitTest(gfx::Point local) const override;

private:
    std::function<void(const ui::PaintContext&, const gfx::Rect&)> paint_;
    bool                                                          blocks_;
};

} // namespace globekit

class GlobeBoard final : public ui::Widget {
public:
    explicit GlobeBoard(std::string prefix);
    ~GlobeBoard() override;

    // ---- les donnees ----
    // `adminLabels` (facultatif, une par ville) : << Camille Essai >> sur la ligne Admins de la carte.
    void setPlaces(std::vector<ui::globe::Place> places, std::vector<std::string> adminLabels = {});
    [[nodiscard]] const std::vector<ui::globe::Place>& places() const noexcept { return places_; }
    void setTotals(const std::array<int, ui::globe::kLayerCount>& totals);
    void setHint(ui::globe::Layer l, std::string hint);
    void setOrdersPeriod(std::string label);                // << 30 j >> : la legende (Commandes - 30 j) et la carte
    void setHeader(std::string title, std::string sub);      // << D'ou se connectent vos clients >>, la date du releve
    void setEmptyMessage(std::string title, std::string text);   // au milieu du globe ; vides : rien
    void setStatus(std::string text);                        // une pastille en haut du globe (<< Releve en cours... >>)
    void setCardActions(bool on);                            // << Voir les N clients >> / << Les N PC >>

    // ---- Compte / En direct ----
    void setLive(bool on);
    [[nodiscard]] bool live() const noexcept { return live_; }
    void setLiveLabel(std::string text);                     // << En direct - releve toutes les 6 s >> (l'en-tete)
    void setTicker(std::string city, std::string what, std::string when);   // la derniere connexion (au-dessus des villes)
    void setActiveTotal(int n);                              // En direct : les PC connectes maintenant (-1 : inconnu)
    [[nodiscard]] const std::string& tickerCity() const noexcept { return tickCity_; }   // les essais
    [[nodiscard]] const std::string& emptyTitle() const noexcept { return emptyTitle_; }  // les essais

    // ---- choisir, plein ecran, figer ----
    void choose(int place);                                  // -1 : aucune ; le globe y va, sa carte s'ouvre
    [[nodiscard]] int selected() const noexcept { return selected_; }
    void setFullScreen(bool on);
    [[nodiscard]] bool fullScreen() const noexcept { return full_; }
    // La vue de la planche (l'Europe, Lyon choisie, zoom 1,45) et plus rien ne bouge (les captures) ; faux : reprend.
    void freezeOn(int place);
    void unfreeze();

    [[nodiscard]] ui::GlobeView& globe() noexcept { return *globe_; }
    [[nodiscard]] ui::GlobeLayerList& layers() noexcept { return *layers_; }
    [[nodiscard]] ui::GlobeCityList& cities() noexcept { return *cities_; }
    [[nodiscard]] gfx::Rect cardRect() const noexcept { return cardBox_; }

    // Ce que la page fait d'un clic (vides : rien).
    std::function<void(bool live)> onMode;             // Compte / En direct
    std::function<void(bool on)>   onFullScreen;       // le bouton plein ecran : la page se remet en page
    std::function<void(int place)> onClients, onPcs;   // les boutons de la carte d'une ville

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void paintOverGlobe(const ui::PaintContext& ctx);
    void paintCityCard(const ui::PaintContext& ctx);
    void syncReserved();
    void syncBy();
    void syncCardButtons();
    [[nodiscard]] float cardHeight() const;

    std::string                                  prefix_;
    std::vector<ui::globe::Place>                places_;
    std::vector<std::string>                     adminLabels_;
    ui::GlobeLayerList*                          layers_{nullptr};
    ui::GlobeView*                               globe_{nullptr};
    ui::GlobeCityList*                           cities_{nullptr};
    ui::DropDown*                                by_{nullptr};
    globekit::PaintBox *                         overGlobe_{nullptr}, *zoomFrame_{nullptr}, *cityCard_{nullptr};
    globekit::FlatButton *                       pause_{nullptr}, *recenter_{nullptr}, *zoomIn_{nullptr}, *zoomOut_{nullptr}, *count_{nullptr},
        *live_btn_{nullptr}, *full_btn_{nullptr}, *close_{nullptr}, *seeClients_{nullptr}, *seePcs_{nullptr};
    std::vector<ui::globe::Layer>                byLayers_;
    gfx::Rect                                    globeArea_{}, right_{}, seg_{}, cardBox_{}, pill_{};
    std::string                                  title_, sub_, emptyTitle_, emptyText_, status_, liveLabel_, ordersPeriod_{"30 j"};
    std::string                                  tickCity_, tickWhat_, tickWhen_;
    int                                          selected_{-1}, activeTotal_{-1};
    bool                                         live_{false}, full_{false}, cardActions_{false};
    core::ConnectionScope                        links_;
};

class GlobeMiniCard final : public ui::Widget {
public:
    explicit GlobeMiniCard(std::string id);
    void setPlaces(std::vector<ui::globe::Place> places);
    void setActive(int n);                         // << 42 PC actifs maintenant >> (-1 : rien)
    void setMessage(std::string text);             // a la place des villes (pas de service, aucune ville...)
    [[nodiscard]] ui::GlobeView& globe() noexcept { return *globe_; }
    std::function<void()> onOpen;                  // un clic sur la carte : le globe
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    [[nodiscard]] std::optional<ui::cursor::Shape> cursorAt(gfx::Point local) const override;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    std::vector<ui::globe::Place> places_;
    ui::GlobeView*                globe_{nullptr};
    int                           active_{-1};
    std::string                   message_;
};

} // namespace license_admin::gui
