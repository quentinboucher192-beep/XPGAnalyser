// =============================================================================
//  app/hmi/HmiFunctionTestsPage.hpp - 1.13.0 (lot J4b) : L'INTERFACE DES TESTS
//  UNITAIRES DES FONCTIONS IHM
// -----------------------------------------------------------------------------
//  Le moteur (hmi/HmiFunctionTests.hpp : un cas rejoue sur un banc, son verdict)
//  est fait ; voici ce que l'utilisateur en voit.
//
//  L'ONGLET " TESTS " D'UNE FONCTION (le 5e, apres Code | Parametres | Locales |
//  Constantes ; une fonction du projet, d'une vue, d'une popup ou d'un symbole) :
//
//    +-- [+ Nouveau cas...] [Dupliquer] [Supprimer] | [Lancer ce cas] [Lancer tout] | [Rapport...] --+
//    | Cas               | Arguments          | Attendu | Tol.  | Verdict  | Detail                  |
//    | moyenne simple    | 12.0 ; 20.0 ; -    | 16.0    |       | reussi   | obtenu 16.0, attendu... |
//    | poids 25 %        | 12.0 ; 20.0 ; 0.25 | 18.0    | 0.001 | echec    | obtenu 17.5, attendu... |
//    +--------------------------------------------+-----------------------------------------------+
//    | Le cas : nom, description, actif, attendu, | POSER AVANT    [+] [-]  Variable | Valeur      |
//    | tolerance, erreur attendue                 | VERIFIER APRES [+] [-]  Cible | Attendu | Tol. |
//    | Arguments : une case par parametre         |                                               |
//    | Derniere execution : l'appel, le verdict   |                                               |
//    +--------------------------------------------+-----------------------------------------------+
//
//  Chaque geste est une commande (Ctrl+Z) et se controle avant de toucher le
//  projet : un nom de cas vide ou en double, un argument qui ne se lit pas dans le
//  type de son parametre, une tolerance sur un retour qui n'est pas REAL, un
//  attendu sur une procedure, une pose sur une variable inconnue sont refuses, la
//  raison sous la case. Les fautes que Generer signale (checkFunctionTests) mettent
//  le nom du cas en rouge.
//
//  LANCER : le cas choisi, tous ceux de la fonction (ici), toutes les fonctions du
//  volet (la barre du volet Fonctions), tout le projet (Compiler, Demarrer). Les
//  verdicts vont au panneau du bas (Sorties, categorie Tests ; un double-clic mene
//  au cas) et restent en memoire (testruns : de la marche, pas du projet) - l'arbre
//  et cet onglet les montrent tant que ni le cas ni la fonction n'ont change.
//
//  " NOUVEAU CAS... " (HmiNewTestCaseDialog) : le nom, la description, une case
//  par parametre (son type, son defaut), l'attendu et la tolerance ; l'apercu de
//  l'appel " Moyenne_Pression(12.0, 20.0, 0.25) ".
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "HmiTestRuns.hpp"          // les derniers verdicts (de la marche)
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCheck.hpp"        // hmi::Issue (les fautes de Generer sur un cas)
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiExport.hpp"
#include "../../hmi/HmiFunctionTests.hpp"
#include "../../hmi/HmiPipeline.hpp"
#include "../../hmi/HmiRuntime.hpp"       // hmi::ExportRequest (le rapport, dans exports/)
#include "../../menu/IMenu.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sim { class Environment; }

namespace app {

// Les derniers verdicts (testruns) : HmiTestRuns.hpp.

// ------------------------------------------------- jouer, et ce qu'on en dit ---
struct TestLine {
    hmi::pipeline::Severity severity{hmi::pipeline::Severity::Information};
    std::string             text;      // "Moyenne_Pression / poids 25 % : echec, obtenu 17.5, attendu 18.0 (ecart 0.5)"
    std::string             element;   // "cas-test:900" (un double-clic y mene) ; "fonction:152"
};
// "cas-test:900", et le cas, sa fonction et son porteur (une vue, kNoId : le projet) d'apres son identifiant.
[[nodiscard]] std::string testElement(hmi::Id test);
struct TestOwner {
    hmi::Id view{hmi::kNoId};       // la vue, la popup ou le symbole ; kNoId : une fonction IHM du projet
    hmi::Id function{hmi::kNoId};
};
[[nodiscard]] std::optional<TestOwner> ownerOfTest(const hmi::Project&, hmi::Id test);
[[nodiscard]] hmi::Id testOfElement(std::string_view element);   // "cas-test:900" -> 900 ; kNoId : autre chose

// Les lignes d'un rapport : l'en-tete (le bilan), puis une par cas. `owner` : le nom de la vue, du symbole ("" : le projet).
[[nodiscard]] std::vector<TestLine> testLines(const hmi::HmiFunction&, const hmi::FunctionTestReport&, std::string_view owner = {});
struct TestCampaign {
    struct Run {
        hmi::Id                 view{hmi::kNoId};
        std::string             owner;
        hmi::HmiFunction        function;   // une copie : le projet peut changer ensuite
        hmi::FunctionTestReport report;
    };
    std::vector<Run>      runs;
    testruns::Tally       tally;
    std::vector<TestLine> lines;
    [[nodiscard]] bool failed() const noexcept { return tally.red(); }
    [[nodiscard]] std::string summary() const;      // "Tests des fonctions : 24 cas \xC2\xB7 22 reussis, 1 echec, 1 erreur"
};
// Toutes les fonctions qui ont des cas (le projet, puis les vues, popups et symboles), chacune sur son banc ;
// les verdicts sont gardes (testruns). `plc` : l'automate de la simulation (lu, jamais ecrit) ; nul : aucun.
[[nodiscard]] TestCampaign runAllTests(const hmi::Project&, sim::Environment* plc = nullptr);
// Celles d'une liste (le volet Fonctions : celles du projet, ou d'une vue - `view`, son nom `owner`).
[[nodiscard]] TestCampaign runTestsOf(const hmi::Project&, const std::vector<hmi::HmiFunction>& list, hmi::Id view, std::string owner,
                                      sim::Environment* plc = nullptr);
// Le rapport (CSV, Excel, PDF) : la campagne entiere (functionTestTable d'une seule fonction si `only`).
[[nodiscard]] hmi::ExportTable testReportTable(const TestCampaign&, const hmi::HmiFunction* only = nullptr);
// " Rapport des tests... " de toute la campagne (Configuration) : les cas rejoues d'abord (verdicts gardes), le
// tableau ecrit par `write` (exports/). `where` : le chemin ecrit ; `campaign` : ce qui a ete joue.
bool exportTestReport(const hmi::Project&, sim::Environment* plc, hmi::ExportFormat format,
                      const std::function<bool(const hmi::ExportRequest&, std::string*)>& write, std::string* where = nullptr,
                      TestCampaign* campaign = nullptr);

// ---------------------------------------------------- l'onglet Tests ---
class HmiFunctionTestsPage final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiFunctionTestsPage(std::string id, hmi::DocumentPtr doc, Apply apply);
    ~HmiFunctionTestsPage() override;

    // La fonction montree : du projet (view kNoId), ou d'une vue / popup / symbole.
    void setFunction(hmi::Id view, hmi::Id function);
    [[nodiscard]] hmi::Id function() const noexcept { return function_; }
    [[nodiscard]] hmi::Id view() const noexcept { return view_; }
    void refresh();

    // ---- les gestes : chacun une commande (Ctrl+Z) ; kNoId / faux et `why` : refuse, rien ne change ----
    struct NewCase {
        std::string              name, description;
        std::vector<std::string> arguments;     // un par parametre ("" : sa valeur par defaut)
        std::string              expected, tolerance, errorCode;
    };
    hmi::Id addCase(const NewCase& c, std::string* why = nullptr);
    hmi::Id addCase(std::string* why = nullptr);       // un cas neuf : " cas 1 ", sans argument
    hmi::Id duplicateCase(hmi::Id test, std::string* why = nullptr);
    bool    removeCase(hmi::Id test, std::string* why = nullptr);
    enum class Field : std::uint8_t { Name, Description, Expected, Tolerance, ErrorCode, Enabled };
    bool setField(hmi::Id test, Field field, const std::string& text, std::string* why = nullptr);
    bool setArgument(hmi::Id test, std::size_t index, const std::string& text, std::string* why = nullptr);
    bool addGiven(hmi::Id test, const std::string& target, const std::string& value, std::string* why = nullptr);
    bool setGiven(hmi::Id test, std::size_t index, const std::string& target, const std::string& value, std::string* why = nullptr);
    bool removeGiven(hmi::Id test, std::size_t index, std::string* why = nullptr);
    bool addCheck(hmi::Id test, const std::string& target, const std::string& expected, const std::string& tolerance = {},
                  std::string* why = nullptr);
    bool setCheck(hmi::Id test, std::size_t index, const std::string& target, const std::string& expected, const std::string& tolerance,
                  std::string* why = nullptr);
    bool removeCheck(hmi::Id test, std::size_t index, std::string* why = nullptr);

    // ---- jouer (sans rien toucher du projet ni de la simulation) ----
    bool        runCase(hmi::Id test);   // vrai : reussi
    std::size_t runAll();                // tous les cas de la fonction ; rend le nombre de reussis
    [[nodiscard]] const hmi::FunctionTestResult* lastResult(hmi::Id test) const;
    // Le rapport de la fonction (ses cas joues d'abord), dans exports/ (par l'hote).
    bool exportReport(hmi::ExportFormat format, std::string* where = nullptr);

    // ---- la selection ----
    [[nodiscard]] hmi::Id selectedCase() const;
    void selectCase(hmi::Id test);
    // Les fautes de Generer sur ce cas (checkFunctionTests), jointes par " ; " ; vide : aucune.
    [[nodiscard]] std::string faultsOf(hmi::Id test) const;
    // "Tests" et le nombre de cas (le titre de l'onglet), et le ton de sa pastille (rouge : un echec).
    [[nodiscard]] std::size_t caseCount() const;
    [[nodiscard]] ui::Tone    badgeTone() const;
    [[nodiscard]] std::string summary() const;    // "3 cas \xC2\xB7 2 reussis, 1 echec"

    struct Hosts {
        std::function<void(hmi::Id view, hmi::Id function)>            newCase;     // la fenetre " Nouveau cas " ; sans : un cas neuf
        std::function<sim::Environment*()>                             plc;         // l'automate de la simulation (lu seulement)
        std::function<void(const std::vector<TestLine>&)>              output;      // les verdicts, au panneau du bas
        std::function<bool(const hmi::ExportRequest&, std::string*)>   exportFile;  // dans exports/
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] const Hosts& hosts() const noexcept { return hosts_; }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     cases() noexcept { return *cases_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TableView&     givens() noexcept { return *givens_; }
    [[nodiscard]] ui::TableView&     checks() noexcept { return *checks_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    // Un message pour la barre du volet (texte, avertissement), et le compte des cas a change (le titre de l'onglet).
    const core::SignalPtr<const std::string&, bool> message = core::Signal<const std::string&, bool>::create();
    const core::SignalPtr<>                         countsChanged = core::Signal<>::create();

    enum Action : int { TNew = 1, TDuplicate, TDelete, TRun, TRunAll, TReportXlsx, TReportPdf,
                        TGivenAdd = 20, TGivenDelete, TCheckAdd = 30, TCheckDelete };

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class CaseRows;
    [[nodiscard]] const hmi::HmiFunction* fn() const;
    [[nodiscard]] const hmi::FunctionTest* test(hmi::Id) const;
    bool change(const std::string& label, const std::function<bool(hmi::FunctionTest&, std::string*)>& edit, hmi::Id test, std::string* why);
    void say(std::string text, bool warning = false);
    void rebuildProperties();
    void refreshSides();
    void emitLines(const std::vector<TestLine>& lines);
    [[nodiscard]] std::string ownerName() const;

    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    hmi::Id           view_{hmi::kNoId};
    hmi::Id           function_{hmi::kNoId};
    HmiToolStrip*     tools_{nullptr};
    ui::TableView*    cases_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    HmiTitledPanel*   givensPanel_{nullptr};
    HmiTitledPanel*   checksPanel_{nullptr};
    HmiToolStrip*     givenTools_{nullptr};
    HmiToolStrip*     checkTools_{nullptr};
    ui::TableView*    givens_{nullptr};
    ui::TableView*    checks_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<CaseRows> rows_;
    std::vector<hmi::Id> order_;              // les cas, dans l'ordre des lignes
    hmi::Id           selected_{hmi::kNoId};
    std::vector<hmi::Issue> faults_;          // checkFunctionTests de la fonction montree
    std::string       message_;
    bool              syncing_{false};
    std::shared_ptr<int>  alive_{std::make_shared<int>(0)};
    core::ConnectionScope links_;
};

// ------------------------------------------------- " Nouveau cas... " ---
class HmiNewTestCaseDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string      id{"dialog.hmiNewTestCase"};
        hmi::DocumentPtr doc;
        hmi::Id          view{hmi::kNoId};      // la vue, la popup ou le symbole ; kNoId : une fonction IHM du projet
        hmi::Id          function{hmi::kNoId};
    };
    using Answer = HmiFunctionTestsPage::NewCase;

    explicit HmiNewTestCaseDialog(Spec spec);
    ~HmiNewTestCaseDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que ferait la souris (scripts, essais) ----
    void setName(const std::string& name);
    void setDescription(const std::string& text);
    void setArgument(std::size_t index, const std::string& text);
    void setExpected(const std::string& text);
    void setTolerance(const std::string& text);
    [[nodiscard]] const Answer& answer() const noexcept { return answer_; }
    // Une case par parametre : "A : REAL", "Poids_A : REAL  (defaut 0.5)" ; ce que le cas ecrit a sa place.
    [[nodiscard]] std::vector<std::string> parameterLabels() const;
    // 1.13.0 (lot F3) : la largeur de la colonne des libelles - le plus long, entier (170 px au moins).
    [[nodiscard]] float labelWidth() const;
    [[nodiscard]] std::string preview() const;                    // "Moyenne_Pression(12.0, 20.0, 0.25)"
    [[nodiscard]] std::string nameProblem() const;                // vide : le nom va
    [[nodiscard]] std::string argumentProblem(std::size_t index) const;
    [[nodiscard]] std::string expectedProblem() const;
    [[nodiscard]] std::string toleranceProblem() const;
    [[nodiscard]] bool toleranceUsable() const;                   // un retour REAL / LREAL
    [[nodiscard]] bool expectedUsable() const;                    // la fonction rend une valeur
    [[nodiscard]] bool canConfirm() const;
    bool confirm();
    void cancel();

    [[nodiscard]] static std::string encode(const Answer&);
    static bool parse(const std::string& payload, Answer& out);

protected:
    core::Status buildUi() override;

private:
    class Body;
    void refresh();
    void finish(bool ok);
    [[nodiscard]] const hmi::HmiFunction* fn() const;

    Spec        spec_;
    Answer      answer_;
    bool        done_{false};
    bool        syncing_{false};
    std::vector<std::string> paramNames_, paramTypes_, paramUnits_, paramDefaults_;
    std::vector<std::string> paramReads_;     // 1.13.0 : le type ou se lit chaque case (un T : sa contrainte)
    std::vector<bool>        paramOutput_;   // une sortie : rien a donner
    Body*           body_{nullptr};
    ui::InputText*  nameField_{nullptr};
    ui::InputText*  descField_{nullptr};
    std::vector<ui::InputText*> argFields_;
    ui::InputText*  expectedField_{nullptr};
    ui::InputText*  toleranceField_{nullptr};
    ui::Button*     ok_{nullptr};
    core::ConnectionScope links_;
};

} // namespace app
