// =============================================================================
//  ui/widgets/GlobeLists.hpp - 1.47.0 : LES DEUX COLONNES DU GLOBE (planche 11)
// -----------------------------------------------------------------------------
//  A cote du globe (Globe.hpp), la maquette a deux listes, dessinees comme elle
//  (globe.css : .gcal, .ly ; .gvil, .vr) :
//    GlobeLayerList  les calques a cocher (a gauche) : la case de la couleur du
//                    calque, son glyphe (sa forme), son nom, ce qu'il compte, son
//                    total ; en bas, << La taille d'une marque suit le nombre >>
//                    (1, 4, 9, 18+). Un calque reste toujours coche (le dernier ne
//                    se decoche pas). Signal layersChanged.
//    GlobeCityList   la liste des villes (a droite) : << Lyon, FR >>, une barre de
//                    la couleur du calque choisi, le nombre ; la plus grande
//                    d'abord, celles a 0 absentes ; la ville choisie surlignee.
//                    Un clic : placeChosen(index de la ville dans setPlaces) - la
//                    page fait tourner le globe vers elle (GlobeView::turnTo). La
//                    molette fait defiler.
//  L'en-tete des villes (<< Les villes >>, << par >> et sa liste, << 21 villes,
//  9 pays >>) est a la page : il a une liste deroulante.
// =============================================================================
#pragma once

#include "Globe.hpp"

#include <array>
#include <string>
#include <vector>

namespace ui {

class GlobeLayerList : public Widget {
public:
    explicit GlobeLayerList(std::string id = {});

    void setLayers(globe::LayerSet on);
    [[nodiscard]] globe::LayerSet layers() const noexcept { return on_; }
    void setTotals(const std::array<int, globe::kLayerCount>& totals);
    // Ce qui est ecrit sous le nom (defaut : LayerStyle::hint) - << un PC pas en 1.47.0 >>.
    void setHint(globe::Layer l, std::string hint);
    // Apres le nom, en gris (<< Commandes - 30 j >>).
    void setNameSuffix(globe::Layer l, std::string suffix);
    // Cocher / decocher (comme un clic) : faux si c'etait le dernier coche.
    bool toggle(globe::Layer l);
    // La rangee (le calque) sous `p` (repere de bounds()) ; -1 : aucune.
    [[nodiscard]] int rowAt(gfx::Point p) const;

    const core::SignalPtr<globe::LayerSet> layersChanged = core::Signal<globe::LayerSet>::create();

    [[nodiscard]] SizeHint sizeHint() const override;
    [[nodiscard]] std::optional<cursor::Shape> cursorAt(gfx::Point local) const override;

    static constexpr float kHeader = 30.f, kRow = 46.f, kFooter = 74.f;

protected:
    void        onPaint(const PaintContext& ctx) override;
    EventResult onEvent(const InputEvent& ev) override;

private:
    globe::LayerSet                               on_ = globe::LayerSet::of({globe::Layer::Clients, globe::Layer::ActivePcs, globe::Layer::Outdated});
    std::array<int, globe::kLayerCount>           totals_{};
    std::array<std::string, globe::kLayerCount>   hints_{}, suffixes_{};
    int                                           hot_{-1};
};

class GlobeCityList : public Widget {
public:
    explicit GlobeCityList(std::string id = {});

    void setPlaces(const std::vector<globe::Place>& places);
    void setLayer(globe::Layer by);                       // le calque qui classe les villes
    [[nodiscard]] globe::Layer layer() const noexcept { return by_; }
    void setSelected(int placeIndex);                     // -1 : aucune
    [[nodiscard]] int selected() const noexcept { return selected_; }
    // Les villes montrees, dans l'ordre (index dans setPlaces) ; et le nombre de pays.
    [[nodiscard]] const std::vector<int>& order() const noexcept { return order_; }
    [[nodiscard]] int countries() const noexcept { return countries_; }
    // La ville sous `p` (repere de bounds()) ; -1 : aucune.
    [[nodiscard]] int placeAt(gfx::Point p) const;

    const core::SignalPtr<int> placeChosen = core::Signal<int>::create();

    [[nodiscard]] SizeHint sizeHint() const override;
    [[nodiscard]] std::optional<cursor::Shape> cursorAt(gfx::Point local) const override;

    static constexpr float kRow = 29.f;

protected:
    void        onPaint(const PaintContext& ctx) override;
    EventResult onEvent(const InputEvent& ev) override;

private:
    void rebuild();
    struct Entry {
        std::string label;
        std::array<int, globe::kLayerCount> counts{};
        std::string country;
        int         pcs{0};   // tous ses PC (globe::Place::pcs) : a egalite, la plus grande ville d'abord
    };
    std::vector<Entry> entries_;
    std::vector<int>   order_;
    globe::Layer       by_{globe::Layer::Clients};
    int                selected_{-1}, hot_{-1}, countries_{0};
    float              scroll_{0.f};
};

} // namespace ui
