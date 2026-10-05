// =============================================================================
//  app/SimConditionDialog.hpp - lot API 8 (2e partie) : la condition d'un
//  point d'arret (le clic droit dans la marge du code)
// -----------------------------------------------------------------------------
//  La maquette (sim-debug.js, condDialog) : le titre « Condition du point
//  d'arret 2 : SFC_PurgeA, ligne 42 » ; le code de la ligne ; le champ
//  « S'arreter seulement si » (vide = a chaque passage) ; « Idees : » des
//  pastilles (un clic les met dans le champ) ; en direct « Maintenant, c'est
//  vrai (x = 3). » ; les boutons « Retirer le point » (a gauche, rouge),
//  « Annuler », « Valider ». Echap : Annuler ; Entree : Valider.
//  La reponse (menu::DialogResult) : Ok + la condition (payload) ; No : retirer
//  le point ; Cancel : rien.
// =============================================================================
#pragma once

#include "../menu/IMenu.hpp"
#include "../ui/widgets/Controls.hpp"

#include <functional>
#include <string>
#include <vector>

namespace app {

class SimConditionDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string              id{"dialog.simCondition"};
        std::string              title;          // « Condition du point d'arret 2 : SFC_PurgeA, ligne 42 »
        std::string              code;           // le code de la ligne
        std::string              condition;      // la condition actuelle (le champ au depart)
        std::vector<std::string> ideas;          // les pastilles
        // Ce que le dialogue dit en direct du texte du champ.
        std::function<std::string(const std::string&)> preview;
    };

    explicit SimConditionDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Les scripts (point-arret-condition-marge "S" 42 "x > 3") : le prochain
    // dialogue ouvert a ce texte dans son champ (une fois).
    static void setNextText(std::string text);
    // Pour les scripts et les tests : le champ, la phrase en direct.
    [[nodiscard]] std::string fieldText() const;
    [[nodiscard]] std::string liveText() const;
    void chooseIdea(std::size_t index);

protected:
    core::Status buildUi() override;

private:
    void finish(menu::DialogResult::Button button);

    Spec                     spec_;
    ui::InputText*           field_{nullptr};
    ui::Widget*              body_{nullptr};
    std::string              live_;
    double                   nextLive_{0.0};
    bool                     done_{false};
    core::ConnectionScope    links_;
};

} // namespace app
