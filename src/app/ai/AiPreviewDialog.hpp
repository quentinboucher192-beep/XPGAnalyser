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
//  1.22.1 : LE CLIC DROIT SUR UNE ERREUR (AiFixes.hpp) - le menu du clic droit
//  propose des corrections adaptees (une variable introuvable : utiliser une
//  proche, la choisir, la creer, retirer ce qui la lit ; une operation en erreur :
//  la retirer ; copier ; demander a l'IA). La proposition change (jamais le
//  projet), se revalide, l'erreur disparait ; la categorie CORRECTIONS dit ce qui
//  a ete fait, et le clic droit sur une correction la reprend (Revenir). Deux
//  erreurs identiques : une ligne << ... (2 operations) >>. Le choix du haut vaut
//  pour UNE variable introuvable (la premiere, ou celle qu'on a demandee) - les
//  autres gardent leur erreur jusqu'a leur propre choix.
//
//  1.22.4 : OU SONT LES ERREURS. Chaque objet de l'apercu vise par une erreur est
//  encadre de rouge, par un avertissement d'ambre (un cadre plus epais, une
//  pastille !) ; la legende le dit. A gauche, les lignes liees a une erreur ou a
//  un avertissement (les messages, les etapes, les objets, les variables, les
//  types) en ont la couleur et la pastille ; une RECHERCHE (le champ de l'appli :
//  Ctrl+F y met le clavier, Echap la vide) filtre les lignes de toutes les
//  categories, avec << 12 lignes >> ; deux PUCES, << Erreurs (n) >> et
//  << Avertissements (n) >>, ne montrent que ces lignes. CHOISIR UNE LIGNE ENCADRE
//  DANS L'APERCU CE QU'ELLE DESIGNE : un objet, les objets qui lisent une variable
//  ou un type, ceux d'une etape, ceux d'un message - un cadre de la couleur
//  d'accent qui pulse une seconde, l'apercu s'y recentre (et zoome si besoin) ; un
//  clic sur un objet de l'apercu choisit sa ligne. Ce que designe chaque ligne est
//  calcule une fois par proposition (Check::spots : les objets de chaque operation
//  et de chaque message), jamais a chaque image.
//
//  1.16.1 : L'APERCU SE ZOOME (la molette autour de la souris, les boutons -,
//  Ajuster, +, Ctrl+plus / moins / 0 ; zoome, il se deplace au glisser ; un
//  double-clic l'ajuste) ; une INFOBULLE sur chaque objet (son nom, son genre,
//  nouveau / modifie / supprime et ce qui change, sa place) ; la LISTE DE GAUCHE
//  se replie par groupe (le clic sur un titre) et se masque toute (le bouton
//  entre elle et l'apercu) pour laisser la largeur a l'apercu.
// =============================================================================
#pragma once

#include "AiFixes.hpp"
#include "AiOperations.hpp"
#include "../../menu/IMenu.hpp"

#include <map>
#include <string>
#include <vector>

namespace ui { class Button; class DropDown; class PopupMenu; class SearchField; }

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

    // 1.22.1 : LE CLIC DROIT SUR UNE ERREUR (AiFixes.hpp) - les lignes telles qu'elles se lisent (les doublons regroupes), celles
    // des corrections faites ; le menu d'une ligne (au milieu de la liste), ses entrees (un sous-menu : "parent > enfant"),
    // en choisir une comme un clic ; la variable introuvable que vise le choix du haut (focusVariable : << Choisir une
    // variable... >>). Pour les essais et les scripts ; la souris passe par les memes gestes.
    [[nodiscard]] std::vector<std::string> errorRows() const;
    [[nodiscard]] std::vector<std::string> warningRows() const;
    [[nodiscard]] std::vector<std::string> correctionRows() const;
    bool openErrorMenu(std::size_t line);
    bool openWarningMenu(std::size_t line);
    bool openCorrectionMenu(std::size_t line);
    [[nodiscard]] bool menuOpen() const;
    [[nodiscard]] std::vector<std::string> menuItems() const;
    bool chooseMenuItem(const std::string& label);
    void focusVariable(const std::string& name, bool openList = false);   // openList : sa liste s'ouvre aussi
    [[nodiscard]] const std::string& variableShown() const noexcept { return missingName_; }

    // 1.22.4 : OU SONT LES ERREURS - le niveau d'un objet de l'apercu (0 : rien ; 1 : un avertissement ; 2 : une erreur) ;
    // l'objet de l'apercu de ce nom (kNoId : aucun) ; la recherche et les puces (Erreurs, Avertissements) ; les lignes
    // montrees (titres compris) et celles qui repondent au filtre (<< 12 lignes >> ; 0 sans filtre) ; choisir une ligne (celle
    // qui dit `part`, sinon la premiere qui le contient ; son groupe se deplie), ce qu'elle encadre dans l'apercu ; un clic sur
    // un objet de l'apercu (il choisit sa ligne) ; la place d'une puce. Pour les essais et les scripts ; la souris et le
    // clavier passent par les memes gestes.
    [[nodiscard]] int                      issueOf(hmi::Id id) const;
    [[nodiscard]] hmi::Id                  objectId(const std::string& name) const;
    void                                   setSearch(const std::string& text);
    [[nodiscard]] std::string              searchText() const;
    void                                   setIssueFilter(bool errors, bool warnings);
    [[nodiscard]] bool                     errorFilter() const;
    [[nodiscard]] bool                     warningFilter() const;
    [[nodiscard]] std::vector<std::string> visibleRows() const;
    [[nodiscard]] std::size_t              matchCount() const;
    bool                                   selectRow(const std::string& part);
    [[nodiscard]] std::string              selectedRow() const;
    [[nodiscard]] std::vector<hmi::Id>     highlighted() const;
    bool                                   clickObject(hmi::Id id);
    [[nodiscard]] gfx::Rect                chipRect(bool errors) const;
    [[nodiscard]] ui::SearchField*         searchField() const noexcept { return search_; }

protected:
    core::Status buildUi() override;

private:
    friend class AiPreviewBody;
    void refresh();
    void finish(bool ok);
    // 1.22.1 : le menu d'une ligne (`list` 1 : une erreur, 2 : un avertissement, 3 : une correction), au point `at`.
    bool openFixMenu(int list, std::size_t line, gfx::Point at);
    void runFix(int id);

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
    // 1.22.1 : le clic droit (le menu, ses actions par id), les lignes d'erreur, d'avertissement et de correction montrees ;
    // le choix du haut demande pour une variable (pinned_) et sa liste a ouvrir au prochain Update.
    ui::PopupMenu*                  fixMenu_{nullptr};
    std::vector<fixes::Action>      fixActions_;
    std::vector<fixes::Line>        errorLines_, warningLines_;
    std::vector<fixes::Correction>  corrections_;
    std::string                     createType_{"BOOL"};   // le type de << Creer ... >> dans la liste du haut
    std::size_t                     otherMissing_{0};      // les autres variables introuvables (sans choix)
    bool                            pinned_{false};
    bool                            openChoice_{false};
    // 1.22.4 : la recherche de la liste ; calcules une fois par proposition (refresh) - le rang de chaque objet de l'apercu,
    // le niveau de chaque objet en cause et ses messages (son infobulle).
    ui::SearchField*                         search_{nullptr};
    std::map<hmi::Id, std::size_t>           objectIndex_;
    std::map<hmi::Id, int>                   issues_;
    std::map<hmi::Id, std::vector<std::string>> issueTexts_;
    core::ConnectionScope links_;
};

} // namespace app::ai
