// =============================================================================
//  app/PixelIconEditor.hpp - l'icone du projet et son editeur en pixel art
//  (lot API 6)
// -----------------------------------------------------------------------------
//  Projet > Icone du projet..., ou un clic sur l'icone de la barre du haut.
//
//  DEUX ONGLETS. Galerie : quinze dessins prets (l'armoire, la bouteille de
//  gaz, la vanne... les initiales du projet) et "Aucune" (le logo revient).
//  Dessiner : la toile de 32 x 32 (14 pixels d'ecran par pixel), les outils a
//  gauche - crayon (P), gomme (E), pot de peinture (F), pipette (I), ligne (L),
//  rectangle (R), symetrie gauche-droite (M), grille (G), Ctrl+Z / Ctrl+Y dans
//  l'editeur -, a droite les seize couleurs (chacune se change en #RRGGBB),
//  l'apercu en 64, 32 et 16 sur fond sombre et clair, et ou elle apparait.
//
//  Clic droit : la gomme. Maj avec le crayon : une ligne droite depuis le
//  dernier point.
//
//  Appliquer rend l'icone (DialogResult::payload : config/icone.txt tel quel,
//  vide : pas d'icone) ; l'appelant en fait une commande - Ctrl+Z la retire.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../menu/IMenu.hpp"

#include <string>
#include <vector>

namespace ui { class Button; class InputText; }

namespace app {

class PixelIconEditor final : public menu::WidgetMenu {
public:
    enum class Tool : int { Pencil = 0, Eraser, Bucket, Picker, Line, Rect };
    enum class Tab : int { Gallery = 0, Draw };

    PixelIconEditor(domain::ProjectIcon current, std::string projectName);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- l'etat, pour la toile, les scripts et les tests ----------------------
    struct State {
        domain::ProjectIcon                icon;
        std::string                        projectName;
        std::string                        preset;          // le dessin choisi ("" : a la main, "aucune")
        Tab                                tab{Tab::Draw};
        Tool                               tool{Tool::Pencil};
        std::uint8_t                       color{7};
        bool                               mirror{false};
        bool                               grid{true};
        std::vector<std::vector<std::uint8_t>> undo, redo;
        int                                hoverX{-1}, hoverY{-1};
    };
    [[nodiscard]] State& state() noexcept { return state_; }
    void choosePreset(const std::string& key);
    void setTool(Tool t);
    void setColor(int index);
    // Un geste sur la toile, en cases (0..31) : ce que fait l'outil choisi.
    void strokeAt(int x, int y, bool begin, bool rightButton = false, bool shift = false);
    void strokeEnd();
    void undoStep();
    void redoStep();
    void clearAll();
    [[nodiscard]] static const char* toolName(Tool t);

protected:
    core::Status buildUi() override;

private:
    void finish(bool ok);
    void snapshot();
    void syncHex();

    State                 state_;
    bool                  noIcon_{false};       // "Aucune" : le logo revient
    ui::Button*           ok_{nullptr};
    ui::Button*           cancel_{nullptr};
    ui::InputText*        hex_{nullptr};
    std::vector<std::uint8_t> strokeBase_;
    int                   strokeX_{-1}, strokeY_{-1};
    bool                  stroking_{false};
    bool                  strokeErase_{false};
    bool                  done_{false};
    core::ConnectionScope links_;
};

} // namespace app
