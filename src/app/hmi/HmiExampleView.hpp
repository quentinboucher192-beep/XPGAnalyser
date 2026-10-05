// =============================================================================
//  app/hmi/HmiExampleView.hpp - l'exemple anime d'un objet, dans l'aide
// -----------------------------------------------------------------------------
//  L'exemple (hmi::examples) tourne sur le VRAI moteur de l'IHM, sans automate,
//  et se dessine comme la simulation (HmiLayerBuilder + HmiLiveCanvas) : ce
//  qu'on voit dans l'aide est ce que l'objet fera en marche. Un pointeur fait
//  les gestes du scenario (il glisse jusqu'a l'objet, le clique, tape au
//  clavier virtuel) ; a la fin du tour, l'exemple recommence de zero.
//
//  Il ne repond pas a la souris : c'est une demonstration, pas une simulation.
//
//  LOT 16 : LE TUTORIEL le pilote (setManual) - son horloge a lui : lecture,
//  pause, vitesse, une position qu'on choisit (reculer rejoue le tour depuis le
//  debut, a l'identique) ; et "A toi" (setInteractive) : le scenario se tait, les
//  clics et le clavier vont au moteur, comme en simulation.
// =============================================================================
#pragma once

#include "HmiSimulation.hpp"
#include "../../hmi/HmiExamples.hpp"

#include <memory>
#include <optional>
#include <string>

namespace app {

class HmiExampleView final : public ui::Widget {
public:
    explicit HmiExampleView(std::string id);
    ~HmiExampleView() override;

    // L'exemple de ce genre d'objet ; nullopt : aucun (le volet se replie).
    void setKind(std::optional<hmi::Kind> kind);
    [[nodiscard]] std::optional<hmi::Kind> kind() const noexcept { return kind_; }
    [[nodiscard]] bool hasExample() const noexcept { return example_.has_value(); }
    // Ce que montre l'exemple, ecrit au-dessus (le @exemple du guide).
    void setCaption(std::string caption) { caption_ = std::move(caption); invalidate(); }
    // Reprendre au debut du tour.
    void restart();
    // Sans attendre les images (tests, captures) : l'exemple a `seconds` du tour.
    void advanceTo(double seconds);
    // Les captures : l'exemple rejoue depuis le debut jusqu'a `seconds`, puis s'arrete
    // la (negatif : il reprend sa marche).
    void freezeAt(double seconds);
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }
    [[nodiscard]] double cycleTime() const noexcept { return played_; }
    [[nodiscard]] HmiLiveCanvas& canvas() noexcept { return *canvas_; }
    [[nodiscard]] const HmiLiveCanvas& canvas() const noexcept { return *canvas_; }

    // ---- lot 16 : le tutoriel ----------------------------------------------------
    void setManual(bool on);                 // l'horloge du tutoriel (sans boucle)
    [[nodiscard]] bool manual() const noexcept { return manual_; }
    void setPlaying(bool on);
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    void setSpeed(double factor);            // 0,25 a 4
    [[nodiscard]] double speed() const noexcept { return speed_; }
    void seek(double seconds);               // a cet instant du tour (le tutoriel)
    [[nodiscard]] double position() const noexcept { return played_; }
    [[nodiscard]] double period() const noexcept { return example_ ? example_->period : 0.0; }
    void setInteractive(bool on);            // "A toi"
    [[nodiscard]] bool interactive() const noexcept { return interactive_; }
    [[nodiscard]] const hmi::examples::Example* example() const noexcept { return example_ ? &*example_ : nullptr; }
    // Le titre de l'en-tete ("Exemple anime", "Tutoriel").
    void setTitle(std::string title) { title_ = std::move(title); invalidate(); }
    [[nodiscard]] const hmi::Runtime* runtime() const noexcept { return runtime_.get(); }
    // Le cadre de la vue de l'exemple, a l'ecran (captures du guide Word).
    [[nodiscard]] gfx::Rect exampleRect() const noexcept { return frame_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    void onPaintOverlay(const ui::PaintContext&) override;   // le pointeur, par-dessus le canevas

private:
    void start(double now);
    void step(double now);
    void manualTo(double seconds);          // le tutoriel : avancer jusqu'a cet instant
    void showNow();                          // les couches de l'instant (et les clics, en "A toi")
    [[nodiscard]] bool targetOf(const hmi::examples::Step&, gfx::Point& out) const;

    std::optional<hmi::Kind>                 kind_;
    std::optional<hmi::examples::Example>    example_;
    std::unique_ptr<hmi::Runtime>            runtime_;
    HmiLayerBuilder                          layers_;
    HmiLiveCanvas*                           canvas_{nullptr};
    std::string                              caption_;
    double                                   origin_{-1};   // l'instant (ctx.time) du debut du tour
    double                                   played_{0};    // le temps du tour deja joue
    double                                   clock_{0};     // l'heure du moteur
    gfx::Rect                                frame_{};
    // Le pointeur : d'ou il part, ou il va, et le dernier clic (l'onde).
    gfx::Point                               pointerFrom_{}, pointerTo_{};
    double                                   moveStart_{0}, moveEnd_{0}, clickAt_{-10};
    std::size_t                              nextStep_{0};
    bool                                     pointerShown_{false};
    bool                                     frozen_{false};
    // Lot 16 : le tutoriel.
    bool                                     manual_{false}, playing_{true}, interactive_{false};
    double                                   speed_{1.0};
    double                                   lastPaint_{-1};
    std::string                              title_{"Exemple anim\xC3\xA9"};
    core::ConnectionScope                    links_;
};

} // namespace app
