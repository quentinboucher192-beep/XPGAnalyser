// =============================================================================
//  app/hmi/HmiHelpPane.hpp - IHM > Aide : le guide de l'IHM dans l'application
// -----------------------------------------------------------------------------
//  A GAUCHE, les chapitres et leurs sujets, et une recherche (sans casse ni
//  accents) qui les filtre ; A DROITE, le sujet mis en page par le meme
//  HelpArticleView que l'aide de la bibliotheque : en-tete, resume, intertitres,
//  exemples de ST colores et copiables, tableaux, encadres, "Voir aussi"
//  cliquables. F1 ouvre ce volet sur le sujet de l'endroit ou l'on est.
//
//  Le contenu vient de hmi::guide (une seule source avec le guide Word / PDF).
// =============================================================================
#pragma once

#include "HmiExampleView.hpp"
#include "HmiObjectTutorial.hpp"
#include "HmiPanels.hpp"
#include "HmiTrails.hpp"
#include "../../hmi/HmiGuide.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/HelpArticleView.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiHelpPane final : public ui::Widget {
public:
    explicit HmiHelpPane(std::string id);

    // Montrer un sujet ("" ou inconnu : le premier). Le precedent va dans
    // l'historique (Precedent).
    void show(std::string_view key);
    [[nodiscard]] const std::string& current() const noexcept { return current_; }
    bool back();
    // Filtrer la liste (le champ de recherche le fait a la frappe).
    void setSearch(std::string text);
    [[nodiscard]] std::size_t listedTopics() const noexcept;

    struct Hosts {
        std::function<void()> replayTutorial;    // la visite de l'IHM (sans parcours : l'ancien bouton)
        // Lot 21 : les parcours du didacticiel (leurs cartes, leur progression) et
        // les lancer (cle, reprendre la ou on s'etait arrete).
        std::function<std::vector<TrailCard>()>          trails{};
        std::function<void(const std::string&, bool)>    startTrail{};
    };
    void setHosts(Hosts h);
    // Lot 21 : les cartes des parcours (la page "didacticiel" les montre) ;
    // l'ecran les rafraichit quand un parcours avance ou se ferme.
    [[nodiscard]] HmiTrailCards& trails() noexcept { return *trails_; }
    void refreshTrails();
    [[nodiscard]] bool trailsShown() const noexcept;
    // Lot 21 : la legende des icones de la liste (sous elle ; un clic sur son titre la replie).
    [[nodiscard]] ui::Widget& legend() noexcept { return *legend_; }

    [[nodiscard]] HmiToolStrip&        tools() noexcept { return *tools_; }
    [[nodiscard]] ui::InputText&       searchField() noexcept { return *search_; }
    [[nodiscard]] ui::TableView&       topicList() noexcept { return *list_; }
    [[nodiscard]] ui::HelpArticleView& article() noexcept { return *view_; }
    // Lot 8 : l'exemple anime de l'objet dont on lit la page (replie sinon).
    [[nodiscard]] HmiExampleView&      example() noexcept { return *example_; }
    // Lot 16 : l'onglet Tutoriel de la page d'un objet (l'exemple en chapitres,
    // joue par le vrai moteur ; "A toi"). Faux : le sujet n'est pas un objet.
    bool showTutorial(bool on);
    [[nodiscard]] bool tutorialShown() const noexcept;
    [[nodiscard]] HmiObjectTutorial&   tutorial() noexcept { return *tutorial_; }

    // La mise en page d'un sujet (sans ecran : les tests la lisent).
    // 1.10 : un sujet ou un paragraphe plus recent que la derniere version vue
    // (help::news::session()) porte l'etiquette orange "NOUVEAU . 1.10".
    [[nodiscard]] static ui::HelpArticle compose(const hmi::guide::Topic&);

    // ---- 1.10 (chantier P) : l'aide F1 en orange ----
    //  Un sujet nouveau (ou change) pour le lecteur : la pastille NOUVEAU dans la
    //  liste, le cadre orange dans l'article ; "Nouveautes seulement" ne garde
    //  qu'eux ; "Tout marquer comme lu" retire l'orange (les reglages le gardent :
    //  `readChanged` previent l'application).
    [[nodiscard]] static bool isNewTopic(const hmi::guide::Topic&);
    [[nodiscard]] static std::size_t newTopicCount();
    void setOnlyNew(bool on);
    [[nodiscard]] bool onlyNew() const noexcept { return onlyNew_; }
    void markAllRead();
    // L'etat des nouveautes a change ici (Tout marquer comme lu, le filtre).
    const core::SignalPtr<> readChanged = core::Signal<>::create();
    // Relire l'etat (une autre page l'a change) : la liste et l'article.
    void refreshNovelty();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildList();
    void selectRowOf(std::string_view key);
    void showExampleOf(const hmi::guide::Topic&);

    Hosts                 hosts_;
    HmiToolStrip*         tools_{nullptr};
    ui::Splitter*         split_{nullptr};
    ui::InputText*        search_{nullptr};
    ui::TableView*        list_{nullptr};
    ui::HelpArticleView*  view_{nullptr};
    HmiExampleView*       example_{nullptr};
    HmiObjectTutorial*    tutorial_{nullptr};     // lot 16
    HmiTrailCards*        trails_{nullptr};       // lot 21
    ui::Widget*           legend_{nullptr};       // lot 21 : la legende des icones, sous la liste
    ui::Widget*           right_{nullptr};      // l'exemple au-dessus de l'article
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<std::string> rowKeys_;          // le sujet de chaque ligne ("" : un chapitre)
    std::vector<std::string> history_;
    std::string           current_;
    std::string           filter_;
    bool                  syncing_{false};
    bool                  onlyNew_{false};       // 1.10 : "Nouveautes seulement"
    core::ConnectionScope links_;
};

} // namespace app
