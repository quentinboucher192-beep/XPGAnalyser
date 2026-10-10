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
#include "HmiConsole.hpp"
#include "HmiPanels.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
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

// 1.11.15 : LA QUESTION AVANT UN REDEMARRAGE DESTRUCTIF - « Le redemarrage va reinitialiser
// les donnees de simulation et supprimer l'etat remanent courant. Continuer ? », la case
// « Ne plus demander pour cette session ». Ok : redemarrer ; la charge "nomore" : la case
// cochee.
class HmiRestartDialog final : public menu::WidgetMenu {
public:
    HmiRestartDialog();
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return "Red\xC3\xA9marrer la simulation"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    void answer(bool yes);

protected:
    core::Status buildUi() override;

private:
    friend class HmiRestartBody;
    ui::Checkbox*         noMore_{nullptr};
    ui::Button*           yes_{nullptr};
    ui::Button*           no_{nullptr};
    bool                  closed_{false};
    core::ConnectionScope links_;
};

class HmiBuildOutputPane final : public ui::Widget {
public:
    // 1.11.14 : LE PANNEAU DU BAS - ses trois onglets.
    static constexpr std::size_t kSorties = 0, kConsole = 1, kDiagnostics = 2;

    explicit HmiBuildOutputPane(std::string id);
    // Un build fini : son journal s'ajoute (a l'heure), ses diagnostics remplacent les precedents.
    void addReport(const hmi::pipeline::Report& report, const hmi::pipeline::Request& request, double seconds);
    // Une ligne dite par l'ecran (un demarrage refuse, un verrou).
    void say(hmi::pipeline::Severity severity, std::string category, std::string message);
    // 1.11.14 : la simulation demarre ou s'arrete - une ligne des Sorties.
    void simulationEvent(bool started, int session, const std::string& detail = {});
    // Ce que montre l'onglet : Sorties, ses lignes ; Console, les siennes (Diagnostics : rien).
    void clear();
    [[nodiscard]] std::string copyText() const;
    void showTab(std::size_t tab);
    [[nodiscard]] std::size_t currentTab() const noexcept;
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }
    [[nodiscard]] const std::vector<hmi::pipeline::Diagnostic>& diagnostics() const noexcept { return diags_; }
    // ---- 1.11.21 : LES DIAGNOSTICS EN DIRECT (HmiLive.hpp) ----
    //  Ceux du document montre (un script, une fonction, des operateurs), recalcules a la
    //  frappe par son volet : en tete de l'onglet Diagnostics (l'etape "Saisie"). Les
    //  diagnostics du dernier build a l'etape Compilation pour le meme element sont caches
    //  (la saisie les recalcule) ; ses autres etapes restent. `element` vide : plus rien en
    //  direct. Un double-clic sur l'une de ces lignes : liveActivated (le volet y revient).
    void setLive(std::string element, std::vector<hmi::pipeline::Diagnostic> diags);
    [[nodiscard]] const std::string& liveElement() const noexcept { return liveElement_; }
    [[nodiscard]] const std::vector<hmi::pipeline::Diagnostic>& liveDiagnostics() const noexcept { return live_; }
    // Ce que l'onglet Diagnostics montre, dans son ordre (le direct, puis le dernier build), filtres compris.
    [[nodiscard]] std::vector<hmi::pipeline::Diagnostic> shownDiagnostics() const;
    // Une ligne des Sorties contient `text` (les sessions rejouees le verifient).
    [[nodiscard]] bool hasLine(std::string_view text) const;
    const core::SignalPtr<hmi::pipeline::Diagnostic> liveActivated = core::Signal<hmi::pipeline::Diagnostic>::create();
    [[nodiscard]] ui::TableView& outputTable() noexcept { return *out_; }
    [[nodiscard]] ui::TableView& diagnosticTable() noexcept { return *diagTable_; }
    // Double-clic : un diagnostic (aller a sa source) ; une ligne du journal qui nomme un element.
    const core::SignalPtr<hmi::pipeline::Diagnostic> diagnosticActivated = core::Signal<hmi::pipeline::Diagnostic>::create();
    const core::SignalPtr<std::string> elementActivated = core::Signal<std::string>::create();

    // ---- 1.11.14 : LA CONSOLE (HmiConsole.hpp) ----
    [[nodiscard]] HmiConsole&       console() noexcept { return console_; }
    [[nodiscard]] const HmiConsole& console() const noexcept { return console_; }
    [[nodiscard]] ui::TableView&    consoleTable() noexcept { return *consoleTable_; }
    [[nodiscard]] const HmiConsole::Filter& consoleFilter() const noexcept { return consoleFilter_; }
    void setConsoleLevel(hmi::LogLevel level, bool shown);
    void setSearch(const std::string& text);
    // Les lignes montrees de la Console (filtrees), et l'une d'elles (nulle : hors bornes).
    [[nodiscard]] std::size_t consoleRowCount() const noexcept;
    [[nodiscard]] const ConsoleEntry* consoleRow(std::size_t row) const;
    // A chaque image (l'ecran) : les lignes arrivees depuis - la table, ses pastilles,
    // le defilement automatique.
    void tick();
    // Le defilement automatique (faux : en pause) ; aller en bas le reprend.
    [[nodiscard]] bool following() const noexcept { return follow_; }
    void setFollowing(bool on);
    void scrollToEnd();
    // Le dossier ou ecrire les exports (exports/ du projet) ; vide : pas de projet.
    void setExportFolder(std::function<std::string()> folder) { exportFolder_ = std::move(folder); }
    // Exporter ce que montre l'onglet : le chemin ecrit (vide : rien, `why` dit pourquoi).
    std::string exportCurrent(bool csv, std::string* why = nullptr);
    // La croix du panneau : l'ecran le replie.
    void setOnClose(std::function<void()> f) { onClose_ = std::move(f); }
    [[nodiscard]] ui::TabControl* tabs() noexcept { return tabs_; }      // 1.12.3 : Projet > Disposition
    // Double-clic (ou Aller a la source) sur une ligne de la Console.
    const core::SignalPtr<ConsoleEntry> consoleActivated = core::Signal<ConsoleEntry>::create();

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
    void rebuildConsole();
    void refreshStatus();
    void consoleMenu(int action);
    [[nodiscard]] bool shown(const Line&) const;
    [[nodiscard]] const ConsoleEntry* selectedConsoleEntry() const;

    std::vector<Line>                       lines_;
    std::vector<hmi::pipeline::Diagnostic>  diags_;
    std::string                             liveElement_;    // 1.11.21 : le document montre
    std::vector<hmi::pipeline::Diagnostic>  live_;           // ... ses diagnostics en direct
    struct DiagRow { bool live{false}; std::size_t index{0}; };
    [[nodiscard]] const hmi::pipeline::Diagnostic& diagOf(const DiagRow& r) const { return r.live ? live_[r.index] : diags_[r.index]; }
    [[nodiscard]] bool hiddenByLive(const hmi::pipeline::Diagnostic& d) const;
    std::vector<std::size_t>                outRows_;    // la ligne de lines_ de chaque rangee
    std::vector<DiagRow>                    diagRows_;
    bool                                    levels_[5]{true, true, true, true, true};
    HmiToolStrip*                           tools_{nullptr};
    ui::InputText*                          search_{nullptr};
    ui::TabControl*                         tabs_{nullptr};
    ui::TableView*                          out_{nullptr};
    ui::TableView*                          diagTable_{nullptr};
    ui::TableView*                          consoleTable_{nullptr};
    ui::StatusBar*                          status_{nullptr};
    std::shared_ptr<ui::ITableModel>        outModel_, diagModel_, consoleModel_;
    std::string                             summary_;
    HmiConsole                              console_;
    HmiConsole::Filter                      consoleFilter_;
    std::uint64_t                           consoleSeen_{~std::uint64_t{0}};
    bool                                    follow_{true};
    std::function<std::string()>            exportFolder_;
    std::function<void()>                   onClose_;
    core::ConnectionScope                   links_;
};

} // namespace app
