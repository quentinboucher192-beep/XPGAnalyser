// =============================================================================
//  app/HistoryPanel.hpp - l'historique du projet (lot 19)
// -----------------------------------------------------------------------------
//  UN TIROIR A DROITE DE L'ECRAN (Ctrl+H), PAR-DESSUS LES VOLETS. Chaque action
//  du projet - l'IHM comme l'automate, une seule pile - avec son endroit et son
//  heure, la plus recente en haut. Les actions annulees restent au-dessus,
//  grisees : un double clic les retablit. Des reperes : l'etat actuel, le
//  dernier enregistrement, l'ouverture du projet.
//
//  UN DOUBLE CLIC DEMANDE A REVENIR A CET ETAT : le tiroir ne fait rien
//  lui-meme, il le demande (goToRequested) ; l'ecran pose la question et
//  App::goToHistory le fait. Un clic droit va a l'endroit (placeRequested).
//
//  LE TIROIR NE PREND QUE SON RECTANGLE : ailleurs, les clics passent aux
//  volets (eventBounds). Il se tient a jour tout seul : la pile a un numero de
//  revision, relu a chaque image.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../core/Signal.hpp"
#include "../ui/Widget.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace app {

// "18:45" (l'heure du PC) ; avec les secondes : "18:45:07". Vide pour 0.
[[nodiscard]] std::string historyClock(std::int64_t wallMs, bool seconds = false);

class HistoryPanel final : public ui::Widget {
public:
    explicit HistoryPanel(std::string id = {});

    // La pile a montrer (relue a chaque image) ; l'heure d'ouverture et du
    // dernier enregistrement.
    void setStack(const core::CommandStack* stack) { stack_ = stack; built_ = ~0ull; invalidate(); }
    void setTimes(std::int64_t openedMs, std::int64_t savedMs);
    // "Cet onglet" : la cle de l'endroit courant (ses actions seulement).
    void setCurrentPlace(std::string placeKey, std::string placeName);

    void open();
    void close();
    void toggle() { if (isOpen()) close(); else open(); }
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    // Double clic : revenir a l'etat juste apres cette entree (0 : l'ouverture).
    const core::SignalPtr<std::uint64_t> goToRequested = core::Signal<std::uint64_t>::create();
    // Clic droit (ou "Aller a l'endroit") : la cle de l'endroit.
    const core::SignalPtr<std::string>   placeRequested = core::Signal<std::string>::create();
    // Le bouton "Etat enregistre".
    const core::SignalPtr<>              savedStateRequested = core::Signal<>::create();

    // ---- pour les tests et les sessions -------------------------------------
    enum class Filter : std::uint8_t { All, Api, Hmi, Here };
    void setFilter(Filter f);
    void setSearch(std::string text);
    [[nodiscard]] Filter filter() const noexcept { return filter_; }
    struct Row {
        enum class Kind : std::uint8_t { Header, Now, Saved, Opened, Entry, Dropped } kind{Kind::Entry};
        std::uint64_t serial{0};
        bool          undone{false};
        std::string   label, place, time, merged;
        int           area{0};
        std::string   placeKey;
    };
    // Les lignes montrees (apres filtre), de haut en bas.
    [[nodiscard]] const std::vector<Row>& rows() const { rebuildIfNeeded(); return rows_; }
    // La premiere ligne dont le libelle commence par `prefix` (-1 : aucune).
    [[nodiscard]] int rowByLabel(const std::string& prefix) const;
    // Ou est la ligne i a l'ecran (apres un dessin ; vide si elle n'est pas visible).
    [[nodiscard]] gfx::Rect rowRect(int i) const;
    [[nodiscard]] gfx::Rect drawerRect() const noexcept { return drawer_; }
    enum class Part : std::uint8_t { Close, ChipAll, ChipApi, ChipHmi, ChipHere, Search, SavedButton, OpenedButton };
    [[nodiscard]] gfx::Rect partRect(Part p) const noexcept;
    // Faire defiler pour montrer la ligne i.
    void reveal(int i);

    [[nodiscard]] gfx::Rect eventBounds() const override { return open_ ? drawer_ : gfx::Rect{}; }

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void rebuildIfNeeded() const;
    [[nodiscard]] bool keep(const core::CommandInfo& info) const;
    [[nodiscard]] int rowAt(gfx::Point p) const;
    [[nodiscard]] float rowHeight(const Row& r) const noexcept;
    void clampScroll();

    const core::CommandStack* stack_{nullptr};
    std::int64_t              openedMs_{0}, savedMs_{0};
    std::string               hereKey_, hereName_;
    bool                      open_{false};
    Filter                    filter_{Filter::All};
    std::string               search_;
    // Les lignes, refaites quand la pile ou le filtre change.
    mutable std::vector<Row>  rows_;
    mutable std::uint64_t     built_{~0ull};
    mutable bool              dirtyRows_{true};
    // La geometrie du dernier dessin.
    gfx::Rect                 drawer_{}, list_{}, close_{}, chips_[4]{}, searchBox_{}, savedBtn_{}, openedBtn_{};
    std::vector<gfx::Rect>    rowRects_;
    float                     scroll_{0.f};
    float                     contentH_{0.f};
    int                       hover_{-1}, selected_{-1};
    int                       hoverPart_{-1};
};

} // namespace app
