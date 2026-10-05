// =============================================================================
//  app/screens/HelpCenterScreen.hpp - 1.11 (chantier T2, tranche 4) : le
//  centre d'aide unique
// -----------------------------------------------------------------------------
//  Un seul ecran pour les quatre entrees d'avant (l'aide generale, l'IHM, les
//  macros, les blocs DFB / DDT) : les fabriques "help", "help.hmi",
//  "help.macros" et "help.blocs" l'ouvrent sur leur chapitre ; F1 sur le sujet
//  de l'endroit (help::center::Index::forF1).
//
//  Ce qu'il dessine est decide par help/CenterView et help/CenterPages (purs,
//  essayes) : l'ecran ne fait que les mettre en widgets. Le modele est
//  HelpScreen : un DockLayout, la barre en haut, l'arbre a gauche (280 px), la
//  page au centre - son en-tete (fil d'Ariane, titre, carte du tutoriel) puis
//  le texte de sa source.
//
//  La page d'un sujet de l'IHM porte l'id "<menu>.pane.article" : pour
//  "help.hmi", c'est "help.hmi.pane.article", le chemin que la fenetre
//  Nouveautes eclaire (le selecteur ST | C | C++, "#notation:").
// =============================================================================
#pragma once

#include <array>

#include "Screens.hpp"
#include "../../help/CenterIndex.hpp"
#include "../../help/CenterView.hpp"
#include "../../help/HelpSession.hpp"
#include "../../help/ProblemReport.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ui { class HelpArticleView; class HelpView; class HelpDocument; }

namespace app {

class HelpCenterScreen final : public menu::WidgetMenu {
public:
    HelpCenterScreen(App& app, std::string menuId, help::center::Chapter chapter);

    // L'index du centre, construit une fois : le guide, l'aide generale, la
    // bibliotheque de libs/ (et les sujets fixes de l'index).
    [[nodiscard]] static const help::center::Index& index();

    // Ouvre un sujet (sa cle) ; une cle inconnue : le premier sujet du chapitre.
    void open(const std::string& key);
    [[nodiscard]] const std::string& currentKey() const noexcept { return current_; }
    // Tranche 8 : suivre un lien de la page comme un clic ("domaine:Alarmes",
    // "contexte:1", "montrer:1.10.1|0|0", "tuto:<cle>", "signaler:capture"...).
    void follow(const std::string& target) { onLink(target); }

    // Alt+Gauche / Alt+Droite : l'historique du centre, toujours (la touche
    // globale Retour reste en dehors du centre) ; Ctrl+F : la recherche.
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    // Tranche 8 : l'en-tete d'une page de l'aide generale prend la hauteur de son contenu.
    void Update(const menu::FrameContext& fc) override;

protected:
    core::Status buildUi() override;
    void         onEnter() override;
    void         onExit() override;

private:
    void showTopic(const std::string& key, bool record);
    void rebuildList();
    void onRow(std::size_t r);
    void onLink(const std::string& target);
    void refreshBar();
    void saveNavigation(bool all);
    [[nodiscard]] help::report::Facts reportFacts() const;
    [[nodiscard]] std::string keysTerm() const;

    App&                     app_;
    help::center::Chapter    chapter_;
    core::ConnectionScope    links_;
    std::shared_ptr<const ui::HelpDocument> doc_;
    ui::ListView*            tree_{nullptr};
    ui::InputText*           search_{nullptr};
    ui::Button*              back_{nullptr};
    ui::Button*              forward_{nullptr};
    ui::Button*              star_{nullptr};
    ui::Button*              count_{nullptr};
    ui::HelpArticleView*     head_{nullptr};
    ui::HelpArticleView*     page_{nullptr};
    ui::HelpView*            api_{nullptr};
    ui::StatusBar*           status_{nullptr};
    help::center::TreeState  state_;
    help::Navigation         nav_;
    std::string              current_;
    bool                     opened_{false};
    // Signaler (tranche 7) : le formulaire (les trois champs, les quatre cases), son
    // panneau (visible sur page-signaler seulement) et le dernier zip prepare.
    help::report::Form       reportForm_;
    ui::Widget*              reportPanel_{nullptr};
    std::filesystem::path    lastZip_;
    std::filesystem::path    captureFile_;      // tranche 8 : la capture jointe (F12 de Signaler)

    // Tranche 8 : la page, son en-tete a part (aide generale) et sa hauteur.
    ui::DockLayout*          pageDock_{nullptr};
    float                    headExtent_{132.f};
    // Tranche 8 : la page des raccourcis (la recherche, la pastille d'un contexte,
    // l'apercu A4) et celle des notes (le domaine retenu, pour sa version).
    ui::Widget*              keysPanel_{nullptr};
    ui::InputText*           keysSearch_{nullptr};
    int                      keysOnly_{-1};      // -1 : tous les contextes
    bool                     keysSheet_{false};
    std::string              notesVersion_, notesDomain_;
    ui::Button*              recentsBtn_{nullptr};
    // Tranche 9 : ST | C | C++, la notation des exemples (aide.notation).
    std::array<ui::Button*, 3> notationBtn_{};
    void setNotation(const std::string& n);
    std::string apiSettle_;        // tranche 9 : l'ancre d'une page de l'aide generale, a reposer (Update)
    bool keepPlace_{false};       // showTopic : le meme article redessine (le defilement reste)
    bool                     recentsShown_{false};

    // Ce que montre la liste de gauche : l'arbre, ou les resultats groupes de
    // la recherche. Une ligne : un sujet (key), un chapitre a deplier (fold), ou
    // un titre de groupe (ni l'un ni l'autre).
    struct Row { std::string key; std::string fold; };
    std::vector<Row>         rows_;
    // Tranche 8 : le modele de l'arbre est garde (setModel remet le defilement a
    // zero) ; on ne le remplace qu'en passant de l'arbre a la recherche ou aux recents.
    class TreeModel;
    std::shared_ptr<TreeModel> treeModel_;
    int                      listMode_{-1};      // 0 l'arbre, 1 la recherche, 2 les recents
};

} // namespace app
