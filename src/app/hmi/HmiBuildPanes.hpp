// =============================================================================
//  app/hmi/HmiBuildPanes.hpp - 1.11.13 : la progression du build et ses sorties
// -----------------------------------------------------------------------------
//  LA FENETRE DE PROGRESSION (HmiBuildProgressDialog) : modale, mais l'appli
//  continue a se dessiner (le build tourne a cote, HmiBuildManager). La barre et
//  son pourcentage, les taches faites sur le total, l'etape principale (« C.
//  Generation de l'IHM › 8. Vues »), l'element en cours, le temps ecoule, les
//  avertissements et les erreurs ; les phases A a G (analyse, API, IHM,
//  compilation, validation, demarrage, restauration) avec leur etat, et sous B
//  et C leurs 4 et 16 sous-etapes (a jour, faites / a faire, en echec) ; a la
//  fin, un etat clair (reussi, echoue, annule). Rien n'est simule : ce qu'elle
//  montre est ce que le moteur publie.
//    En cours : « Continuer en arriere-plan » (la barre d'etat suit), « Annuler »
//    (le build s'arrete entre deux taches ; rien n'est coupe en deux).
//    Fini : « Voir les sorties », « Fermer » ; reussi, elle se ferme seule apres
//    une seconde quand la simulation demarre derriere.
//  La reponse (payload) : "background", "cancelled", "close", "outputs".
//
//  LES SORTIES (HmiBuildOutputPane, l'onglet « IHM · Sorties ») : le journal de
//  chaque build (ce que disent les phases, en francais, a l'heure) et les
//  diagnostics du dernier (gravite, code, message, element, fichier, ligne,
//  colonne, etape, suggestion). Filtres par niveau, recherche, Effacer, Copier.
//  Un double-clic sur un diagnostic ouvre sa source (l'editeur, la ligne).
// =============================================================================
#pragma once

#include "HmiBuild.hpp"
#include "HmiPanels.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiBuildProgressDialog final : public menu::WidgetMenu {
public:
    // `startsSimulation` : le build d'un Demarrer (reussi : la fenetre se ferme seule).
    HmiBuildProgressDialog(std::shared_ptr<HmiBuildManager> build, std::string title, bool startsSimulation);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return title_; }
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    void background();
    void cancelBuild();

protected:
    core::Status buildUi() override;

private:
    friend class HmiBuildProgressBody;
    void finish(const std::string& payload);
    void refreshButtons();

    std::shared_ptr<HmiBuildManager> build_;
    std::string                      title_;
    bool                             startsSimulation_{false};
    hmi::pipeline::Progress          progress_;
    std::shared_ptr<const hmi::pipeline::Report> report_;   // a la fin
    std::shared_ptr<const hmi::pipeline::Report> before_;   // le rapport d'avant (pour voir le nouveau)
    double                           elapsed_{0}, now_{0}, doneAt_{-1};
    bool                             done_{false}, closed_{false};
    ui::Button*                      bg_{nullptr};
    ui::Button*                      cancel_{nullptr};
    ui::Button*                      outputs_{nullptr};
    core::ConnectionScope            links_;
};

class HmiBuildOutputPane final : public ui::Widget {
public:
    explicit HmiBuildOutputPane(std::string id);
    // Un build fini : son journal s'ajoute (a l'heure), ses diagnostics remplacent les precedents.
    void addReport(const hmi::pipeline::Report& report, const hmi::pipeline::Request& request, double seconds);
    // Une ligne dite par l'ecran (un demarrage refuse, un verrou).
    void say(hmi::pipeline::Severity severity, std::string category, std::string message);
    void clear();
    [[nodiscard]] std::string copyText() const;
    // 0 : Sorties ; 1 : Diagnostics.
    void showTab(std::size_t tab);
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }
    [[nodiscard]] const std::vector<hmi::pipeline::Diagnostic>& diagnostics() const noexcept { return diags_; }
    [[nodiscard]] ui::TableView& outputTable() noexcept { return *out_; }
    [[nodiscard]] ui::TableView& diagnosticTable() noexcept { return *diagTable_; }
    // Double-clic : un diagnostic (aller a sa source) ; une ligne du journal qui nomme un element.
    const core::SignalPtr<hmi::pipeline::Diagnostic> diagnosticActivated = core::Signal<hmi::pipeline::Diagnostic>::create();
    const core::SignalPtr<std::string> elementActivated = core::Signal<std::string>::create();

protected:
    void onLayout() override;

private:
    struct Line {
        std::string              time;
        hmi::pipeline::Severity  severity{hmi::pipeline::Severity::Information};
        std::string              category, message, element;
        bool                     rule{false};   // la ligne de titre d'un build
    };
    void rebuild();
    [[nodiscard]] bool shown(const Line&) const;

    std::vector<Line>                       lines_;
    std::vector<hmi::pipeline::Diagnostic>  diags_;
    std::vector<std::size_t>                outRows_;    // la ligne de lines_ de chaque rangee
    std::vector<std::size_t>                diagRows_;
    bool                                    levels_[5]{true, true, true, true, true};
    HmiToolStrip*                           tools_{nullptr};
    ui::InputText*                          search_{nullptr};
    ui::TabControl*                         tabs_{nullptr};
    ui::TableView*                          out_{nullptr};
    ui::TableView*                          diagTable_{nullptr};
    ui::StatusBar*                          status_{nullptr};
    std::shared_ptr<ui::ITableModel>        outModel_, diagModel_;
    std::string                             summary_;
    core::ConnectionScope                   links_;
};

} // namespace app
