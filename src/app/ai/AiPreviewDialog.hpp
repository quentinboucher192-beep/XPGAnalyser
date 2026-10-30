// =============================================================================
//  app/ai/AiPreviewDialog.hpp - l'Assistant IA : la fenetre de previsualisation
// -----------------------------------------------------------------------------
//  CE QUI CHANGERAIT, AVANT QUE RIEN NE CHANGE. Une fenetre du logiciel (elle se
//  deplace et s'agrandit), sur une proposition :
//
//    en haut     la vue visee, la demande ; les comptes - creer, modifier,
//                supprimer ;
//    a gauche    les objets concernes, leurs proprietes (avant -> apres), les
//                variables associees, les avertissements et les erreurs ; une
//                variable introuvable : la liste des variables existantes (les
//                plus proches d'abord) et "Creer ..." - c'est l'utilisateur qui
//                decide, jamais l'Assistant ;
//    a droite    L'APERCU : une COPIE de la vue, modifiee, dessinee par les
//                painters du logiciel ; les objets nouveaux encadres en vert, les
//                modifies en bleu, ceux a supprimer barres de rouge ;
//    en bas      Modifier la demande (le texte revient dans la saisie), Annuler
//                (la proposition est refusee), Appliquer (un seul Ctrl+Z ; une
//                suppression multiple se confirme). Echap ferme sans decider.
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "../../menu/IMenu.hpp"

#include <string>

namespace ui { class Button; class DropDown; }

namespace app::ai {

class AiPanel;

class AiPreviewDialog final : public menu::WidgetMenu {
public:
    AiPreviewDialog(AiPanel& panel, int message);

    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override { return "Pr\xC3\xA9visualisation des modifications"; }
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;
    void                           Update(const menu::FrameContext& f) override;

    // Les gestes (essais, scripts).
    bool apply();
    void cancel();          // Annuler : la proposition est refusee
    void editRequest();     // Modifier la demande
    void close();           // Echap : sans decider
    bool chooseVariable(int index);   // dans la liste deroulante (le dernier : creer)

    [[nodiscard]] int                message() const noexcept { return message_; }
    [[nodiscard]] const Check&       check() const noexcept { return check_; }
    [[nodiscard]] const Preview&     preview() const noexcept { return preview_; }
    [[nodiscard]] ui::Button*        applyButton() const noexcept { return apply_; }
    [[nodiscard]] ui::DropDown*      variableChoice() const noexcept { return variable_; }
    [[nodiscard]] std::vector<std::string> variableItems() const;

protected:
    core::Status buildUi() override;

private:
    friend class AiPreviewBody;
    void refresh();
    void finish(bool ok);

    AiPanel&      panel_;
    int           message_;
    Check         check_;
    Preview       preview_;
    std::string   key_;
    ui::Button*   edit_{nullptr};
    ui::Button*   cancel_{nullptr};
    ui::Button*   apply_{nullptr};
    ui::DropDown* variable_{nullptr};
    std::vector<std::string> variableNames_;   // les noms de la liste ("" : creer)
    std::string   missingName_;                // la variable introuvable (gardee apres un choix)
    bool          done_{false};
    bool          syncing_{false};
    core::ConnectionScope links_;
};

} // namespace app::ai
