// =============================================================================
//  app/hmi/HmiStretchDialog.hpp - 1.50.0 : la fenetre « Etirer... » (planche 02)
// -----------------------------------------------------------------------------
//  Les cas fins du menu Etirer : les bords a la carte (les quatre fleches autour
//  de la carte de l'objet), la cible (« Jusqu'a » : la vue, la selection, le
//  voisin, un repere), la marge (celle de la vue d'office ; 0 : l'objet touche),
//  la taille obtenue. L'apercu suit chaque changement sur la vue (la fenetre ne
//  l'assombrit pas) ; « Etirer » rend la demande (DialogResult::payload, parse),
//  l'editeur la fait en une commande (un seul Ctrl+Z).
// =============================================================================
#pragma once

#include "../../hmi/HmiStretch.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <array>
#include <functional>
#include <string>
#include <utility>

namespace app {

class HmiStretchDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string                    id{"dialog.hmiStretch"};
        std::string                    title{"\xC3\x89tirer"};   // "Etirer Courbe_1"
        std::string                    object;                   // la carte : "Courbe_1", "3 objets"
        hmi::stretch::Request          request;                  // le depart (la marge : >= 0)
        double                         viewMargin{10};           // « celle de la vue »
        // L'apercu sur la vue pour cette demande : possible ou non, et la ligne Taille (ou pourquoi rien).
        std::function<std::pair<bool, std::string>(const hmi::stretch::Request&)> preview;
    };

    explicit HmiStretchDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que ferait la souris (scripts, essais) --------------------------
    void setEdge(unsigned edge, bool on);                      // hmi::stretch::kLeft...
    [[nodiscard]] unsigned edges() const noexcept { return request_.edges; }
    void setTarget(hmi::stretch::To to);                       // View, Selection, Neighbour, Guide
    [[nodiscard]] hmi::stretch::To target() const noexcept { return request_.to; }
    void setMargin(double px);
    [[nodiscard]] double margin() const noexcept { return request_.margin; }
    [[nodiscard]] const hmi::stretch::Request& request() const noexcept { return request_; }
    [[nodiscard]] const std::string& sizeLine() const noexcept { return size_; }   // la ligne Taille
    [[nodiscard]] bool canConfirm() const noexcept { return possible_; }
    bool confirm();                                            // « Etirer »
    void cancel();

    [[nodiscard]] static std::string encode(const hmi::stretch::Request&);
    [[nodiscard]] static hmi::stretch::Request parse(const std::string& payload);

protected:
    core::Status buildUi() override;

private:
    class Body;
    void refresh();       // l'apercu, la ligne Taille, l'allure des boutons
    void finish(bool ok);

    Spec                          spec_;
    hmi::stretch::Request         request_;
    std::string                   size_;
    bool                          possible_{false};
    bool                          done_{false};
    bool                          syncing_{false};
    Body*                         body_{nullptr};
    std::array<ui::Button*, 4>    edgeButtons_{};     // gauche, droite, haut, bas
    std::array<ui::Button*, 4>    targetButtons_{};   // la vue, selection, voisin, repere
    ui::InputText*                marginField_{nullptr};
    ui::Button*                   ok_{nullptr};
    core::ConnectionScope         links_;
};

} // namespace app
