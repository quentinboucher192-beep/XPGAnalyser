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
//    en bas      Modifier la demande (le texte revient dans la saisie ; la
//                proposition reste a decider), Refuser... (une question d'abord),
//                Appliquer (un seul Ctrl+Z ; une suppression multiple se
//                confirme). Echap ferme sans decider.
//
//  1.21.0 : LES SUPPRESSIONS (AiDelete) - une categorie en tete de la liste :
//  chaque suppression, sa strategie, le resume par genre (<< 4 variables
//  directement concernees >>), ce qui part, ce qui en depend (directe,
//  indirecte, potentielle, partagee), les consequences (references retirees ou
//  remplacees, orphelins gardes). Un clic sur une suppression la RETIRE du plan
//  (ou l'y remet) : le geste de la bande du plan (Assistant::setExcluded) - ce
//  qui en depend est ignore, la verification se refait.
//
//  1.16.1 : L'APERCU SE ZOOME (la molette autour de la souris, les boutons -,
//  Ajuster, +, Ctrl+plus / moins / 0 ; zoome, il se deplace au glisser ; un
//  double-clic l'ajuste) ; une INFOBULLE sur chaque objet (son nom, son genre,
//  nouveau / modifie / supprime et ce qui change, sa place) ; la LISTE DE GAUCHE
//  se replie par groupe (le clic sur un titre) et se masque toute (le bouton
//  entre elle et l'apercu) pour laisser la largeur a l'apercu.
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
    [[nodiscard]] bool waiting() const noexcept { return waiting_; }   // 1.20.1 : la verification de fond n'est pas finie
    bool chooseVariable(int index);   // dans la liste deroulante (le dernier : creer)
    // 1.21.0 : retirer (ou remettre) une suppression (l'operation `op`, une operation supprimer) - comme la bande du plan.
    bool excludeRemoval(std::size_t op, bool excluded);
    // Les lignes de la categorie Suppressions (leur texte), dans l'ordre (essais).
    [[nodiscard]] std::vector<std::string> removalRows() const;

    [[nodiscard]] int                message() const noexcept { return message_; }
    [[nodiscard]] const Check&       check() const noexcept { return check_; }
    [[nodiscard]] const Preview&     preview() const noexcept { return preview_; }
    [[nodiscard]] ui::Button*        applyButton() const noexcept { return apply_; }
    [[nodiscard]] ui::DropDown*      variableChoice() const noexcept { return variable_; }
    [[nodiscard]] std::vector<std::string> variableItems() const;

    // 1.16.1 : le zoom (1 : la vue entiere ; jusqu'a 10), les groupes de la liste ("OBJETS CONCERN\xC3\x89S", "ERREURS"...),
    // la liste masquee, l'infobulle sous un point de l'ecran ; les places (essais).
    void  setPreviewZoom(float z);
    [[nodiscard]] float previewZoom() const;
    bool  foldGroup(const std::string& heading, bool folded);
    [[nodiscard]] bool groupFolded(const std::string& heading) const;
    [[nodiscard]] std::size_t shownRows() const;              // les lignes de la liste montrees (groupes replies : leur titre)
    void  setListHidden(bool hidden);
    [[nodiscard]] bool listHidden() const;
    [[nodiscard]] std::string previewTooltip(gfx::Point at) const;
    [[nodiscard]] gfx::Rect  previewArea() const;
    [[nodiscard]] gfx::Rect  listArea() const;
    [[nodiscard]] gfx::Rect  groupRect(const std::string& heading) const;
    [[nodiscard]] gfx::Point objectPoint(const std::string& name) const;   // le centre d'un objet de l'apercu a l'ecran

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
    bool          waiting_{false};    // 1.20.1 : la verification de fond de la carte n'est pas finie
    bool          planReady_{false};
    double        lastWait_{-1.0};
    bool          syncing_{false};
    bool          planFolded_{false};   // 1.19.0 : les categories du plan repliees a la premiere ouverture
    core::ConnectionScope links_;
};

} // namespace app::ai
