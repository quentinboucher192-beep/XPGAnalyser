// =============================================================================
//  app/hmi/HmiFunctionTestsPage.cpp - 1.13.0 (lot J4b) : l'interface des tests
//  unitaires des fonctions IHM (voir HmiFunctionTestsPage.hpp)
// =============================================================================
#include "HmiFunctionTestsPage.hpp"
#include "HmiPaneKit.hpp"
#include "../ExportTarget.hpp"            // askExportTarget : ou ecrire le rapport
#include "../../core/FieldCodec.hpp"
#include "../../hmi/HmiDecl.hpp"          // decl::codeOf : les parametres du modele
#include "../../hmi/HmiExpr.hpp"          // Expression : une cible lisible
#include "../../hmi/HmiHistory.hpp"       // wallStamp : la date du rapport
#include "../../hmi/HmiScript.hpp"        // splitDeclarations, LocalVar
#include "../../hmi/HmiSymbols.hpp"       // cellValue (la regle des cases)
#include "../../hmi/HmiTypes.hpp"         // 1.13.0 (D3b) : types::typeOfPath - le type d'une verif
#include "../../hmi/HmiUnits.hpp"         // withUnit : REAL [bar]
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>

namespace app {

namespace {

using hmi::Id;
using hmi::kNoId;
using hmi::Verdict;
using hmikit::prop;
using PG = ui::PropertyGrid;
namespace pl = hmi::pipeline;

std::string trim(std::string_view s);

// La regle des cases (hmi::cellValue), gardee sous une forme qui se relit pareil : 2,5 -> 2.5, VRAI -> TRUE,
// Azote -> 'Azote' (un STRING) ; une formule (ou un nom connu) garde son = (=Niveau * 2). Illisible : tel quel.
std::string keptCell(const hmi::Project& p, std::string_view type, const std::string& typed) {
    if (typed.empty()) return typed;
    const auto cell = hmi::cellValue(&p, type, typed);
    if (!cell.ok() || cell.text.empty()) return typed;
    return cell.formula ? "=" + cell.text : cell.text;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool sameText(std::string_view a, std::string_view b) { return hmikit::same(a, b); }

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// 1.13.0 (seconde passe) : la tolerance sert-elle a ce retour (REAL, LREAL, ou un T qui peut etre un reel) ?
bool toleranceApplies(const hmi::HmiFunction* f) { return f && hmi::testToleranceApplies(*f); }

bool parseTolerance(std::string_view text, double& out) {
    std::string t = trim(text);
    for (auto& c : t)
        if (c == ',') c = '.';
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

// La fonction d'un porteur (kNoId : le projet), en lecture et en ecriture.
const hmi::HmiFunction* functionIn(const hmi::Project& p, Id view, Id id) {
    if (id == kNoId) return nullptr;
    if (view == kNoId) {
        for (const auto& f : p.programs.functions)
            if (f.id == id) return &f;
        return nullptr;
    }
    if (const auto* v = p.view(view))
        for (const auto& f : v->functions)
            if (f.id == id) return &f;
    return nullptr;
}
hmi::HmiFunction* functionIn(hmi::Project& p, Id view, Id id) {
    return const_cast<hmi::HmiFunction*>(functionIn(static_cast<const hmi::Project&>(p), view, id));
}

// Ses parametres, dans l'ordre (le modele et un ancien bloc VAR_INPUT du code).
// 1.13.0 (seconde passe) : `read` - le type dans lequel la case se lit (T generique : sa contrainte, ANY_NUM) ; tous
// les types se lisent ici (un T, une structure IHM), comme la signature : sinon un parametre T disparaissait.
struct Param {
    std::string name, type, initial, unit, read;
    bool        output{false};
};
std::vector<Param> paramsOf(const hmi::HmiFunction& f) {
    std::vector<Param> out;
    const auto parts = hmi::splitDeclarations(hmi::decl::codeOf(f), true, [](std::string_view) { return true; });
    for (const auto* l : parts.parameters()) {
        Param p;
        p.name = l->name;
        p.type = l->type;
        p.read = hmi::testCellType(f, l->type);
        p.initial = l->initial;
        p.output = l->section == hmi::LocalVar::Section::Output;
        for (const auto& d : f.decls)
            if (sameText(d.name, l->name)) p.unit = d.unit;
        out.push_back(std::move(p));
    }
    return out;
}

std::string argsSummary(const hmi::FunctionTest& t, std::size_t params) {
    std::string s;
    const std::size_t n = std::max(params, t.arguments.size());
    for (std::size_t k = 0; k < n; ++k) {
        const std::string v = k < t.arguments.size() ? trim(t.arguments[k]) : std::string{};
        s += (k ? " ; " : "") + (v.empty() ? std::string("\xE2\x80\x94") : v);
    }
    return s;
}

std::string expectedSummary(const hmi::FunctionTest& t) {
    if (!t.errorCode.empty()) return "l\xC3\xA8ve " + t.errorCode;
    if (!trim(t.expected).empty()) return trim(t.expected);
    return t.checks.empty() ? std::string("\xE2\x80\x94") : plural(t.checks.size(), "v\xC3\xA9rif", "v\xC3\xA9rifs");
}

ui::Tone toneOf(Verdict v) {
    switch (v) {
        case Verdict::Passed: return ui::Tone::Ok;
        case Verdict::Failed: return ui::Tone::Error;
        case Verdict::Error: return ui::Tone::Warning;
        default: return ui::Tone::Muted;
    }
}

ui::Icon iconOf(Verdict v) {
    switch (v) {
        case Verdict::Passed: return ui::Icon::Ok;
        case Verdict::Failed: return ui::Icon::Error;
        case Verdict::Error: return ui::Icon::Warning;
        default: return ui::Icon::Info;
    }
}

std::string fileSafe(std::string s) {
    for (auto& c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    return s.empty() ? std::string("projet") : s;
}

} // namespace

// Les derniers verdicts (testruns) : HmiTestRuns.cpp.

// =================================================== jouer, et ce qu'on en dit ===
std::string testElement(Id test) { return "cas-test:" + std::to_string(test); }

Id testOfElement(std::string_view element) {
    constexpr std::string_view head = "cas-test:";
    if (element.substr(0, head.size()) != head) return kNoId;
    const std::string rest(element.substr(head.size()));
    if (rest.empty() || !std::all_of(rest.begin(), rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
        return kNoId;
    return static_cast<Id>(std::stoull(rest));
}

std::optional<TestOwner> ownerOfTest(const hmi::Project& p, Id test) {
    if (test == kNoId) return std::nullopt;
    for (const auto& f : p.programs.functions)
        for (const auto& t : f.tests)
            if (t.id == test) return TestOwner{kNoId, f.id};
    for (const auto& v : p.views)
        for (const auto& f : v.functions)
            for (const auto& t : f.tests)
                if (t.id == test) return TestOwner{v.id, f.id};
    return std::nullopt;
}

std::vector<TestLine> testLines(const hmi::HmiFunction& f, const hmi::FunctionTestReport& rep, std::string_view owner) {
    std::vector<TestLine> out;
    const std::string name = owner.empty() ? f.name : std::string(owner) + "." + f.name;
    testruns::Tally t;
    for (const auto& r : rep.results) {
        ++t.cases;
        if (r.verdict == Verdict::Passed) ++t.passed;
        else if (r.verdict == Verdict::Failed) ++t.failed;
        else if (r.verdict == Verdict::Error) ++t.errors;
        else if (r.verdict == Verdict::Skipped) ++t.skipped;
        else ++t.pending;
    }
    TestLine head;
    head.severity = t.red() ? pl::Severity::Warning : pl::Severity::Information;
    head.text = "Tests de " + name + " : " + t.text();
    head.element = std::string(owner.empty() ? "fonction:" : "fonction-symbole:") + std::to_string(f.id);
    out.push_back(std::move(head));
    for (const auto& r : rep.results) {
        TestLine l;
        l.element = testElement(r.test);
        const std::string who = name + " / " + (r.name.empty() ? std::string("(sans nom)") : r.name) + " : ";
        switch (r.verdict) {
            case Verdict::Passed:
                l.severity = pl::Severity::Success;
                l.text = who + "r\xC3\xA9ussi" + (r.got.empty() ? std::string{} : " (" + r.got + ")");
                break;
            case Verdict::Failed:
                l.severity = pl::Severity::Warning;
                l.text = who + "\xC3\xA9" "chec" + (r.detail.empty() ? std::string{} : ", " + r.detail);
                break;
            case Verdict::Error:
                l.severity = pl::Severity::Error;
                l.text = who + "erreur" + (r.detail.empty() ? std::string{} : ", " + r.detail);
                break;
            default:
                l.severity = pl::Severity::Information;
                l.text = who + "non jou\xC3\xA9 (cas inactif)";
                break;
        }
        out.push_back(std::move(l));
    }
    return out;
}

std::string TestCampaign::summary() const { return "Tests des fonctions : " + tally.text(); }

namespace {
void runOne(TestCampaign& c, const hmi::Project& p, const hmi::HmiFunction& f, Id view, const std::string& owner, sim::Environment* plc) {
    if (f.tests.empty()) return;
    TestCampaign::Run run;
    run.view = view;
    run.owner = owner;
    run.function = f;
    run.report = hmi::runFunctionTests(p, f, plc);
    testruns::record(p, f, run.report);
    for (auto& l : testLines(f, run.report, owner)) c.lines.push_back(std::move(l));
    c.tally += testruns::tally(p, f);
    c.runs.push_back(std::move(run));
}
} // namespace

TestCampaign runAllTests(const hmi::Project& p, sim::Environment* plc) {
    const testruns::Batch batch;          // 1.13.0 : les volets se refont une fois, a la fin de la campagne
    TestCampaign c;
    for (const auto& f : p.programs.functions) runOne(c, p, f, kNoId, {}, plc);
    for (const auto& v : p.views)
        for (const auto& f : v.functions) runOne(c, p, f, v.id, v.name, plc);
    return c;
}

TestCampaign runTestsOf(const hmi::Project& p, const std::vector<hmi::HmiFunction>& list, Id view, std::string owner, sim::Environment* plc) {
    const testruns::Batch batch;          // 1.13.0 : une seule annonce pour toute la liste
    TestCampaign c;
    for (const auto& f : list) runOne(c, p, f, view, owner, plc);
    return c;
}

hmi::ExportTable testReportTable(const TestCampaign& c, const hmi::HmiFunction* only) {
    if (only)
        for (const auto& r : c.runs)
            if (r.function.id == only->id) return hmi::functionTestTable(r.function, r.report);
    std::vector<std::pair<const hmi::HmiFunction*, const hmi::FunctionTestReport*>> runs;
    for (const auto& r : c.runs) runs.emplace_back(&r.function, &r.report);
    return hmi::campaignTestTable(runs);
}

bool exportTestReport(const hmi::Project& p, sim::Environment* plc, hmi::ExportFormat format,
                      const std::function<bool(const hmi::ExportRequest&, std::string*)>& write, std::string* where, TestCampaign* campaign) {
    TestCampaign c = runAllTests(p, plc);
    hmi::ExportTable table = testReportTable(c);
    table.subtitle = p.config.name + " - " + hmi::wallStamp().substr(0, 19) + " - " + table.subtitle;
    hmi::ExportRequest rq;
    rq.fileName = hmi::exportFileName("tests_" + fileSafe(p.config.name) + "_" + hmi::wallStamp().substr(0, 10), format);
    rq.format = std::string(hmi::exportFormatLabel(format));
    rq.source = "tests";
    rq.rows = table.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(hmi::exportBytes(table, format));
    rq.origin = "Configuration > Tests des fonctions";
    std::string path;
    const bool ok = write && write(rq, &path);
    if (where) *where = path;
    if (campaign) *campaign = std::move(c);
    return ok;
}

// ============================================================ l'onglet Tests ===
class HmiFunctionTestsPage::CaseRows final : public ui::ITableModel {
public:
    struct Row {
        std::vector<std::string> cells;
        Verdict                  verdict{Verdict::Pending};
        bool                     played{false};
        bool                     enabled{true};
        bool                     expectsError{false};   // 1.13.0 : une Erreur attendue - l'Attendu ne sert pas
        std::string              fault;
        std::string              tip;
    };
    std::vector<Row> rows;
    bool hasReturn{false}, realReturn{false};
    std::function<bool(std::size_t, std::size_t, const std::string&)> commit;

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 6; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"Cas", "Arguments", "Attendu", "Tol\xC3\xA9rance", "Verdict", "D\xC3\xA9tail"};
        return c < 6 ? h[c] : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows.size() && c < rows[r].cells.size() ? rows[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows.size()) return s;
        const auto& row = rows[r];
        if (c == 0) {
            s.bold = true;
            s.icon = ui::Icon::Function;
            if (!row.fault.empty()) {
                s.fgTone = ui::Tone::Error;
                s.iconTone = ui::Tone::Error;
            } else if (!row.enabled) {
                s.fgTone = ui::Tone::Muted;
            }
        }
        if (c == 1 || c == 2 || c == 3) s.monospace = true;
        if (c == 3 && !realReturn) s.fgTone = ui::Tone::Muted;
        if (c == 4) {
            const Verdict v = !row.enabled ? Verdict::Skipped : row.played ? row.verdict : Verdict::Pending;
            s.icon = iconOf(v);
            s.iconTone = toneOf(v);
            s.fgTone = toneOf(v);
            s.bold = v == Verdict::Failed || v == Verdict::Error;
        }
        if (c == 5) s.fgTone = ui::Tone::Muted;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    // 1.13.0 (seconde passe) : F2 edite le nom ; un double-clic lance le cas (la table ne l'edite pas, voir le
    // constructeur). L'Attendu d'un cas qui attend une erreur ne se tape pas ici : le cas juge sur l'erreur.
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return false;
        return c == 0 || (c == 2 && hasReturn && !rows[r].expectsError) || (c == 3 && realReturn && !rows[r].expectsError);
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override { return commit && commit(r, c, std::string(text)); }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < rows.size() ? rows[r].tip : std::string{}; }
};

HmiFunctionTestsPage::HmiFunctionTestsPage(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TNew, HmiGlyph::Plus, "Nouveau cas de test (Inser) : des arguments, un r\xC3\xA9sultat attendu, une tol\xC3\xA9rance", "Nouveau cas\xE2\x80\xA6");
    tools->add(TDuplicate, HmiGlyph::Duplicate, "Dupliquer le cas choisi (Ctrl+D) : la copie juste apr\xC3\xA8s lui", "Dupliquer");
    tools->add(TDelete, HmiGlyph::Delete, "Supprimer le cas choisi (Suppr) - Ctrl+Z le rend", "Supprimer");
    tools->separator();
    tools->add(TRun, HmiGlyph::Play, "Lancer ce cas : sur un banc, sans toucher le projet ni la simulation (double-clic sur la ligne)", "Lancer ce cas");
    tools->add(TRunAll, HmiGlyph::Check, "Lancer tous les cas de la fonction (les verdicts vont aussi au panneau du bas, Sorties)", "Lancer tout");
    tools->separator();
    tools->add(TReportXlsx, HmiGlyph::Export, "Rapport Excel des tests de la fonction (ses cas rejou\xC3\xA9s d'abord ; dossier exports/)", "Rapport Excel");
    tools->add(TReportPdf, HmiGlyph::Export, "Rapport PDF des tests de la fonction (ses cas rejou\xC3\xA9s d'abord ; dossier exports/)", "Rapport PDF");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto hasFn = [this] { return fn() != nullptr; };
    const auto hasCase = [this] { return test(selectedCase()) != nullptr; };
    tools_->setEnabledWhen(TNew, hasFn);
    tools_->setEnabledWhen(TDuplicate, hasCase);
    tools_->setEnabledWhen(TDelete, hasCase);
    tools_->setEnabledWhen(TRun, hasCase);
    tools_->setEnabledWhen(TRunAll, [this] { const auto* f = fn(); return f && !f->tests.empty(); });
    tools_->setVisibleWhen(TReportXlsx, [this] { return static_cast<bool>(hosts_.exportFile); });
    tools_->setVisibleWhen(TReportPdf, [this] { return static_cast<bool>(hosts_.exportFile); });
    for (const int a : {static_cast<int>(TReportXlsx), static_cast<int>(TReportPdf)})
        tools_->setEnabledWhen(a, [this] { const auto* f = fn(); return f && !f->tests.empty(); });

    auto table = std::make_unique<ui::TableView>(base + ".cases");
    table->setColumns({{"Cas", 170.f, 60.f, true, false}, {"Arguments", 190.f, 60.f, true, false}, {"Attendu", 110.f, 50.f, true, false},
                       {"Tol\xC3\xA9rance", 90.f, 40.f, true, false}, {"Verdict", 100.f, 60.f, true, false}, {"D\xC3\xA9tail", 320.f, 60.f, true, false}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table->setDoubleClickEdits(false);        // 1.13.0 (seconde passe) : un double-clic lance le cas, F2 edite
    cases_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    tools_->bindListKeys(*cases_, {TDelete, TDuplicate, TNew});
    rows_ = std::make_shared<CaseRows>();
    rows_->commit = [this](std::size_t r, std::size_t c, const std::string& text) {
        if (r >= order_.size()) return false;
        std::string why;
        const Field f = c == 0 ? Field::Name : c == 2 ? Field::Expected : Field::Tolerance;
        const bool ok = setField(order_[r], f, text, &why);
        if (!ok) say("Refus\xC3\xA9 : " + why, true);
        return ok;
    };
    cases_->setModel(rows_);

    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setShowDescriptionPane(false);
    grid->setNameColumnRatio(0.42f);
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));

    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".givensPanel", "POSER AVANT L'APPEL");
        auto strip = std::make_unique<HmiToolStrip>(base + ".givenTools");
        strip->add(TGivenAdd, HmiGlyph::Plus, "Poser une variable avant l'appel (elle part de cette valeur ; Inser)", "Poser");
        strip->add(TGivenDelete, HmiGlyph::Delete, "Retirer la pose choisie (Suppr)", "Retirer");
        givenTools_ = &static_cast<HmiToolStrip&>(panel->addRow(std::move(strip), 30.f));
        auto t = std::make_unique<ui::TableView>(base + ".givens");
        t->setColumns({{"Variable", 170.f, 60.f, true, false}, {"Valeur", 150.f, 50.f, true, false}});
        t->setSelectionMode(ui::SelectionMode::Single);
        givens_ = &static_cast<ui::TableView&>(panel->setBody(std::move(t)));
        givensPanel_ = &static_cast<HmiTitledPanel&>(addChild(std::move(panel)));
        givenTools_->setEnabledWhen(TGivenAdd, hasCase);
        givenTools_->setEnabledWhen(TGivenDelete, [this] { return hmikit::selectedRow(*givens_) >= 0; });
        givenTools_->bindListKeys(*givens_, {TGivenDelete, 0, TGivenAdd});
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".checksPanel", "V\xC3\x89RIFIER APR\xC3\x88S L'APPEL");
        auto strip = std::make_unique<HmiToolStrip>(base + ".checkTools");
        strip->add(TCheckAdd, HmiGlyph::Plus, "V\xC3\xA9rifier une variable apr\xC3\xA8s l'appel, ou ce que le JOURNAL contient (Inser)", "V\xC3\xA9rifier");
        strip->add(TCheckDelete, HmiGlyph::Delete, "Retirer la v\xC3\xA9rif choisie (Suppr)", "Retirer");
        checkTools_ = &static_cast<HmiToolStrip&>(panel->addRow(std::move(strip), 30.f));
        auto t = std::make_unique<ui::TableView>(base + ".checks");
        t->setColumns({{"Cible", 150.f, 60.f, true, false}, {"Attendu", 150.f, 50.f, true, false}, {"Tol\xC3\xA9rance", 80.f, 40.f, true, false}});
        t->setSelectionMode(ui::SelectionMode::Single);
        checks_ = &static_cast<ui::TableView&>(panel->setBody(std::move(t)));
        checksPanel_ = &static_cast<HmiTitledPanel&>(addChild(std::move(panel)));
        checkTools_->setEnabledWhen(TCheckAdd, hasCase);
        checkTools_->setEnabledWhen(TCheckDelete, [this] { return hmikit::selectedRow(*checks_) >= 0; });
        checkTools_->bindListKeys(*checks_, {TCheckDelete, 0, TCheckAdd});
    }
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    const auto onTool = [this](int a) {
        const Id sel = selectedCase();
        std::string why;
        switch (a) {
            case TNew:
                if (!fn()) break;
                if (hosts_.newCase) hosts_.newCase(view_, function_);
                else if (addCase(&why) == kNoId) say(why, true);
                break;
            case TDuplicate: if (sel != kNoId && duplicateCase(sel, &why) == kNoId) say(why, true); break;
            case TDelete: if (sel != kNoId && !removeCase(sel, &why)) say(why, true); break;
            case TRun: if (sel != kNoId) (void)runCase(sel); break;
            case TRunAll: (void)runAll(); break;
            case TReportXlsx:
                if (!askExportTarget("le rapport des tests (Excel)", "Classeurs Excel|*.xlsx", [this] { (void)exportReport(hmi::ExportFormat::Excel); }))
                    (void)exportReport(hmi::ExportFormat::Excel);
                break;
            case TReportPdf:
                if (!askExportTarget("le rapport des tests (PDF)", "Documents PDF|*.pdf", [this] { (void)exportReport(hmi::ExportFormat::Pdf); }))
                    (void)exportReport(hmi::ExportFormat::Pdf);
                break;
            case TGivenAdd: {
                if (sel == kNoId) break;
                const auto& vars = doc_->project.programs.variables;
                if (!addGiven(sel, vars.empty() ? std::string("Variable") : vars.front().name, "0", &why)) say("Refus\xC3\xA9 : " + why, true);
                break;
            }
            case TGivenDelete: {
                const int r = hmikit::selectedRow(*givens_);
                if (sel != kNoId && r >= 0 && !removeGiven(sel, static_cast<std::size_t>(r), &why)) say(why, true);
                break;
            }
            case TCheckAdd: {
                if (sel == kNoId) break;
                const auto& vars = doc_->project.programs.variables;
                if (!addCheck(sel, vars.empty() ? std::string("JOURNAL") : vars.front().name, vars.empty() ? std::string("texte") : std::string("0"), {}, &why))
                    say("Refus\xC3\xA9 : " + why, true);
                break;
            }
            case TCheckDelete: {
                const int r = hmikit::selectedRow(*checks_);
                if (sel != kNoId && r >= 0 && !removeCheck(sel, static_cast<std::size_t>(r), &why)) say(why, true);
                break;
            }
            default: break;
        }
    };
    links_ += tools_->triggered->connect(onTool);
    links_ += givenTools_->triggered->connect(onTool);
    links_ += checkTools_->triggered->connect(onTool);
    links_ += cases_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_) return;
        selected_ = !rows.empty() && rows.front() < order_.size() ? order_[rows.front()] : kNoId;
        rebuildProperties();
        refreshSides();
    });
    links_ += cases_->activated->connect([this](ui::RowIndex r) {
        if (r < order_.size()) (void)runCase(order_[r]);
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    links_ += testruns::changed()->connect([this] { refresh(); });
    refresh();
}

HmiFunctionTestsPage::~HmiFunctionTestsPage() = default;

const hmi::HmiFunction* HmiFunctionTestsPage::fn() const { return functionIn(doc_->project, view_, function_); }

const hmi::FunctionTest* HmiFunctionTestsPage::test(Id id) const {
    const auto* f = fn();
    if (!f || id == kNoId) return nullptr;
    for (const auto& t : f->tests)
        if (t.id == id) return &t;
    return nullptr;
}

std::string HmiFunctionTestsPage::ownerName() const {
    if (view_ == kNoId) return {};
    const auto* v = doc_->project.view(view_);
    return v ? v->name : std::string{};
}

void HmiFunctionTestsPage::setFunction(Id view, Id function) {
    if (view == view_ && function == function_) return;
    view_ = view;
    function_ = function;
    selected_ = kNoId;
    refresh();
}

void HmiFunctionTestsPage::say(std::string text, bool warning) {
    if (warning) ui::PropertyGrid::refuse(text);
    message_ = text;
    status_->setTransientMessage(text, 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
    message->emit(message_, warning);
}

std::size_t HmiFunctionTestsPage::caseCount() const {
    const auto* f = fn();
    return f ? f->tests.size() : 0;
}

ui::Tone HmiFunctionTestsPage::badgeTone() const {
    const auto* f = fn();
    if (!f) return ui::Tone::None;
    const auto t = testruns::tally(doc_->project, *f);
    return t.red() ? ui::Tone::Error : t.cases && t.passed == t.cases ? ui::Tone::Ok : ui::Tone::None;
}

std::string HmiFunctionTestsPage::summary() const {
    const auto* f = fn();
    return f ? testruns::tally(doc_->project, *f).text() : std::string("aucune fonction choisie");
}

std::string HmiFunctionTestsPage::faultsOf(Id t) const {
    std::string out;
    for (const auto& i : faults_)
        if (i.item == t && i.severity != hmi::Issue::Severity::Info) out += (out.empty() ? "" : " ; ") + i.message;
    return out;
}

void HmiFunctionTestsPage::refresh() {
    const auto* f = fn();
    order_.clear();
    faults_.clear();
    rows_->rows.clear();
    rows_->hasReturn = f && !f->returnType.empty();
    rows_->realReturn = toleranceApplies(f);
    if (f) {
        // Les fautes que Generer dirait sur les cas de CETTE fonction (sans jouer).
        hmi::Project probe;
        probe.config = doc_->project.config;
        probe.programs.variables = doc_->project.programs.variables;
        probe.programs.types = doc_->project.programs.types;
        probe.programs.functions.push_back(*f);
        hmi::checkFunctionTests(probe, faults_);
        const std::size_t params = paramsOf(*f).size();
        for (const auto& t : f->tests) {
            order_.push_back(t.id);
            CaseRows::Row row;
            row.enabled = t.enabled;
            row.expectsError = !t.errorCode.empty();
            row.fault = faultsOf(t.id);
            const auto* r = testruns::last(doc_->project, *f, t);
            row.played = r != nullptr;
            if (r) row.verdict = r->verdict;
            const std::string verdict = !t.enabled ? std::string("inactif")
                                      : r ? std::string(hmi::verdictLabel(r->verdict))
                                          : std::string("\xC3\xA0 jouer");
            row.cells = {t.name, argsSummary(t, params), expectedSummary(t), trim(t.tolerance), verdict, r ? r->detail : std::string{}};
            row.tip = t.name + (t.description.empty() ? std::string{} : "\n" + t.description)
                    + (t.errorCode.empty() ? std::string{} : "\nLe cas attend l'erreur " + t.errorCode + " : l'attendu ne sert pas")
                    + (r && !r->call.empty() ? "\nAppel : " + r->call : std::string{})
                    + (row.fault.empty() ? std::string{} : "\nFaute : " + row.fault);
            rows_->rows.push_back(std::move(row));
        }
    }
    syncing_ = true;
    rows_->modelReset->emit();
    if (std::find(order_.begin(), order_.end(), selected_) == order_.end()) selected_ = order_.empty() ? kNoId : order_.front();
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == selected_) cases_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    if (order_.empty()) cases_->selectModelRows({}, false);
    syncing_ = false;
    rebuildProperties();
    refreshSides();
    if (!f) status_->setMessage("Choisis une fonction.", ui::StatusBar::Severity::Info);
    else if (f->tests.empty())
        status_->setMessage("Aucun cas : \xC2\xAB Nouveau cas\xE2\x80\xA6 \xC2\xBB \xC3\xA9pingle des arguments et le r\xC3\xA9sultat attendu \xE2\x80\x94 un cas se rejoue "
                            "\xC3\xA0 chaque Compiler et D\xC3\xA9marrer.",
                            ui::StatusBar::Severity::Info);
    else
        status_->setMessage(f->name + " : " + testruns::tally(doc_->project, *f).text() + "  \xC2\xB7  double-clic : lancer le cas ; F2 : le renommer ; les verdicts aussi au panneau du bas",
                            testruns::tally(doc_->project, *f).red() ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
    countsChanged->emit();
    invalidate();
}

Id HmiFunctionTestsPage::selectedCase() const { return test(selected_) ? selected_ : kNoId; }

void HmiFunctionTestsPage::selectCase(Id t) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == t) {
            selected_ = t;
            syncing_ = true;
            cases_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
            syncing_ = false;
            rebuildProperties();
            refreshSides();
            return;
        }
}

void HmiFunctionTestsPage::refreshSides() {
    const auto* t = test(selected_);
    const Id tid = selected_;
    {
        std::vector<std::vector<std::string>> rows;
        if (t)
            for (const auto& g : t->givens) rows.push_back({g.target, g.value});
        auto m = std::make_shared<hmikit::Rows>(std::vector<std::string>{"Variable", "Valeur"}, std::move(rows), [](ui::RowIndex, std::size_t c) {
            ui::CellStyle s;
            s.monospace = true;
            if (c == 0) s.icon = ui::Icon::Variable;
            return s;
        });
        // Les cases s'ecrivent (la variable, la valeur) : un modele editable par dessus Rows.
        class Editable final : public ui::ITableModel {
        public:
            Editable(std::shared_ptr<hmikit::Rows> base, std::function<bool(std::size_t, std::size_t, const std::string&)> commit)
                : base_(std::move(base)), commit_(std::move(commit)) {}
            [[nodiscard]] std::size_t rowCount() const override { return base_->rowCount(); }
            [[nodiscard]] std::size_t columnCount() const override { return base_->columnCount(); }
            [[nodiscard]] std::string headerText(std::size_t c) const override { return base_->headerText(c); }
            [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override { return base_->cellText(r, c); }
            [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return base_->cellStyle(r, c); }
            [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return base_->less(a, b, c); }
            [[nodiscard]] bool editable(ui::RowIndex r, std::size_t) const override { return r < base_->rowCount(); }
            bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override { return commit_(r, c, std::string(text)); }
        private:
            std::shared_ptr<hmikit::Rows> base_;
            std::function<bool(std::size_t, std::size_t, const std::string&)> commit_;
        };
        givens_->setModel(std::make_shared<Editable>(m, [this, tid](std::size_t r, std::size_t c, const std::string& text) {
            const auto* t2 = test(tid);
            if (!t2 || r >= t2->givens.size()) return false;
            std::string why;
            const auto& g = t2->givens[r];
            const bool ok = setGiven(tid, r, c == 0 ? text : g.target, c == 1 ? text : g.value, &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }));
        std::vector<std::vector<std::string>> crows;
        if (t)
            for (const auto& c : t->checks) crows.push_back({c.target, c.expected, c.tolerance});
        auto cm = std::make_shared<hmikit::Rows>(std::vector<std::string>{"Cible", "Attendu", "Tol\xC3\xA9rance"}, std::move(crows), [](ui::RowIndex, std::size_t c) {
            ui::CellStyle s;
            s.monospace = true;
            if (c == 0) s.icon = ui::Icon::Search;
            return s;
        });
        checks_->setModel(std::make_shared<Editable>(cm, [this, tid](std::size_t r, std::size_t c, const std::string& text) {
            const auto* t2 = test(tid);
            if (!t2 || r >= t2->checks.size()) return false;
            std::string why;
            const auto& k = t2->checks[r];
            const bool ok = setCheck(tid, r, c == 0 ? text : k.target, c == 1 ? text : k.expected, c == 2 ? text : k.tolerance, &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        }));
    }
}

void HmiFunctionTestsPage::rebuildProperties() {
    const auto* f = fn();
    const auto* t = test(selected_);
    if (!f || !t) {
        grid_->setCategories({});
        return;
    }
    const Id tid = t->id;
    const auto edit = [this, tid](Field field) {
        return [this, tid, field](std::string_view v) {
            std::string why;
            const bool ok = setField(tid, field, std::string(v), &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        };
    };
    std::vector<PG::Category> cats;
    PG::Category c;
    c.name = "Le cas";
    c.properties.push_back(prop("Nom", t->name, PG::ValueType::Text, edit(Field::Name), "Unique dans la fonction : il nomme le cas dans les verdicts."));
    c.properties.push_back(prop("Description", t->description, PG::ValueType::Text, edit(Field::Description)));
    c.properties.push_back(prop("Actif", t->enabled ? "TRUE" : "FALSE", PG::ValueType::Boolean, edit(Field::Enabled),
                                "D\xC3\xA9" "coch\xC3\xA9 : le cas reste mais ne se joue plus (verdict \xC2\xAB non jou\xC3\xA9 \xC2\xBB)."));
    if (!f->returnType.empty() && !t->errorCode.empty() && !trim(t->expected).empty()) {
        // 1.13.0 (D3b) : un attendu reste d'avant l'Erreur attendue - ignore (Generer le dit), il se vide ici : la case ne
        // prend que le vide (un attendu tape est refuse, avec la raison).
        auto pi = prop("Attendu (ignor\xC3\xA9)", t->expected, PG::ValueType::Text, edit(Field::Expected),
                       "Le cas attend l'erreur " + t->errorCode + " : cet attendu ne sert pas. Vide la case pour l'\xC3\xB4ter ; pour comparer le "
                       "retour, vide d'abord l'Erreur attendue.");
        pi.valueTone = ui::Tone::Warning;
        c.properties.push_back(std::move(pi));
    } else if (!f->returnType.empty() && !t->errorCode.empty())     // 1.13.0 : le cas juge sur l'erreur, pas sur le retour
        c.properties.push_back(prop("Attendu", "(le cas attend l'erreur " + t->errorCode + " : vide l'Erreur attendue pour comparer le retour)",
                                    PG::ValueType::ReadOnly));
    else if (!f->returnType.empty())
        c.properties.push_back(prop("Attendu", t->expected, PG::ValueType::Text, edit(Field::Expected),
                                    "Ce que la fonction doit rendre (" + hmi::units::withUnit(f->returnType, f->returnUnit) + ") : il se lit comme toute case "
                                    "(2,5 -> 2.5, VRAI -> TRUE) ; =formule (=Seuil_Haut - 5) se calcule apr\xC3\xA8s l'appel. Vide : seules les v\xC3\xA9rifs jugent."));
    else
        c.properties.push_back(prop("Attendu", "(proc\xC3\xA9" "dure : rien \xC3\xA0 comparer - ses v\xC3\xA9rifs jugent)", PG::ValueType::ReadOnly));
    if (toleranceApplies(f) && t->errorCode.empty()) {
        auto pt = prop("Tol\xC3\xA9rance", t->tolerance, PG::ValueType::Text, edit(Field::Tolerance),
                       "|obtenu - attendu| \xE2\x89\xA4 tol\xC3\xA9rance ; vide : \xC3\xA9galit\xC3\xA9 stricte.");
        pt.placeholder = "exacte";
        c.properties.push_back(std::move(pt));
    } else {
        c.properties.push_back(prop("Tol\xC3\xA9rance", t->errorCode.empty() ? "(exacte : seulement pour un retour REAL)" : "(le cas attend une erreur)",
                                    PG::ValueType::ReadOnly));
    }
    {
        auto pe = prop("Erreur attendue", t->errorCode, PG::ValueType::Text, edit(Field::ErrorCode),
                       "Un code (E_Niveau, E_DIVISION) : le cas r\xC3\xA9ussit si la fonction l\xC3\xA8ve cette erreur. Vide : elle ne doit rien lever.");
        pe.placeholder = "aucune";
        c.properties.push_back(std::move(pe));
    }
    cats.push_back(std::move(c));
    // Une case par parametre (son type, son defaut) - vide : la valeur par defaut.
    PG::Category a;
    const auto params = paramsOf(*f);
    a.name = "Arguments \xE2\x80\x94 " + f->name + "(" + std::to_string(params.size()) + ")";
    for (std::size_t k = 0; k < params.size(); ++k) {
        const auto& p = params[k];
        const std::string label = p.name + " : " + hmi::units::withUnit(p.type, p.unit);
        if (p.output) {
            a.properties.push_back(prop(label, "(sortie : rien \xC3\xA0 donner)", PG::ValueType::ReadOnly));
            continue;
        }
        auto pa = prop(label, k < t->arguments.size() ? t->arguments[k] : std::string{}, PG::ValueType::Text,
                       [this, tid, k](std::string_view v) {
                           std::string why;
                           const bool ok = setArgument(tid, k, std::string(v), &why);
                           if (!ok) say("Refus\xC3\xA9 : " + why, true);
                           return ok;
                       },
                       "Se lit comme toute case, dans le type du param\xC3\xA8tre (2,5 -> 2.5 ; Azote -> 'Azote' ; =formule).");
        pa.placeholder = p.initial.empty() ? std::string("vide") : "d\xC3\xA9" "faut " + p.initial;
        a.properties.push_back(std::move(pa));
    }
    // 1.13.0 (seconde passe) : un argument au-dela de la signature (un parametre retire d'un ancien projet) se voit et
    // se retire : vider sa case l'ote du cas.
    for (std::size_t k = params.size(); k < t->arguments.size(); ++k) {
        if (trim(t->arguments[k]).empty()) continue;
        auto px = prop("Argument " + std::to_string(k + 1) + " (en trop)", t->arguments[k], PG::ValueType::Text,
                       [this, tid, k](std::string_view v) {
                           std::string why;
                           const bool ok = setArgument(tid, k, std::string(v), &why);
                           if (!ok) say("Refus\xC3\xA9 : " + why, true);
                           return ok;
                       },
                       f->name + " n'a plus ce param\xC3\xA8tre : vide la case pour retirer l'argument du cas.");
        px.valueTone = ui::Tone::Error;
        a.properties.push_back(std::move(px));
    }
    if (params.empty() && t->arguments.empty()) a.properties.push_back(prop("Param\xC3\xA8tres", "aucun", PG::ValueType::ReadOnly));
    cats.push_back(std::move(a));
    PG::Category r;
    r.name = "Derni\xC3\xA8re ex\xC3\xA9" "cution";
    if (const auto* res = testruns::last(doc_->project, *f, *t)) {
        auto pv = prop("Verdict", std::string(hmi::verdictLabel(res->verdict)), PG::ValueType::ReadOnly);
        pv.valueTone = toneOf(res->verdict);
        r.properties.push_back(std::move(pv));
        if (!res->call.empty()) r.properties.push_back(prop("Appel", res->call, PG::ValueType::ReadOnly));
        if (!res->got.empty()) r.properties.push_back(prop("Obtenu", res->got, PG::ValueType::ReadOnly));
        if (!res->detail.empty()) r.properties.push_back(prop("D\xC3\xA9tail", res->detail, PG::ValueType::ReadOnly));
    } else {
        r.properties.push_back(prop("Verdict", t->enabled ? "pas encore jou\xC3\xA9 (ou chang\xC3\xA9 depuis) : Lancer ce cas" : "inactif",
                                    PG::ValueType::ReadOnly));
    }
    if (const std::string fault = faultsOf(tid); !fault.empty()) {
        auto pf = prop("G\xC3\xA9n\xC3\xA9rer dit", fault, PG::ValueType::ReadOnly);
        pf.valueTone = ui::Tone::Error;
        r.properties.push_back(std::move(pf));
    }
    cats.push_back(std::move(r));
    grid_->setCategories(std::move(cats));
}

// ---- les gestes ----
bool HmiFunctionTestsPage::change(const std::string& label, const std::function<bool(hmi::FunctionTest&, std::string*)>& edit, Id tid,
                                  std::string* why) {
    const auto* f = fn();
    if (!f) {
        if (why) *why = "aucune fonction choisie";
        return false;
    }
    // A blanc d'abord : un refus ne laisse rien.
    {
        hmi::FunctionTest probe;
        bool found = false;
        for (const auto& t : f->tests)
            if (t.id == tid) {
                probe = t;
                found = true;
            }
        if (!found) {
            if (why) *why = "cas introuvable";
            return false;
        }
        if (!edit(probe, why)) return false;
    }
    const Id view = view_, fid = function_;
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) {
        if (auto* g = functionIn(p, view, fid))
            for (auto& t : g->tests)
                if (t.id == tid) (void)edit(t, nullptr);
    });
    if (cmd) apply_(std::move(cmd));
    return true;
}

Id HmiFunctionTestsPage::addCase(const NewCase& nc, std::string* why) {
    const auto* f = fn();
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        say("Cas refus\xC3\xA9 : " + w, true);
        return kNoId;
    };
    if (!f) return refuse("aucune fonction choisie");
    const std::string name = trim(nc.name);
    if (name.empty()) return refuse("un cas a un nom (il le nomme dans les verdicts)");
    for (const auto& t : f->tests)
        if (sameText(t.name, name)) return refuse("le cas \xC2\xAB " + name + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 dans " + f->name);
    const auto params = paramsOf(*f);
    if (nc.arguments.size() > params.size())
        return refuse("trop d'arguments (" + std::to_string(nc.arguments.size()) + " pour " + std::to_string(params.size()) + ")");
    for (std::size_t k = 0; k < nc.arguments.size(); ++k) {
        const std::string text = trim(nc.arguments[k]);
        if (text.empty() || params[k].output) continue;
        if (const auto cell = hmi::cellValue(&doc_->project, params[k].read, text); !cell.ok()) return refuse(params[k].name + " : " + cell.why);
    }
    const std::string returnRead = hmi::testCellType(*f, f->returnType);     // 1.13.0 : un T se lit dans sa contrainte
    if (!trim(nc.expected).empty()) {
        if (f->returnType.empty()) return refuse("la fonction n'a pas de retour : v\xC3\xA9rifie plut\xC3\xB4t ses variables (V\xC3\xA9rifier apr\xC3\xA8s)");
        if (!trim(nc.errorCode).empty()) return refuse("le cas attend l'erreur " + trim(nc.errorCode) + " : il juge sur elle, pas sur un attendu");
        if (const auto e = hmi::testExpected(doc_->project, returnRead, trim(nc.expected)); !e.ok()) return refuse("attendu : " + e.why);
    }
    if (!trim(nc.tolerance).empty()) {
        double d = 0;
        if (!toleranceApplies(f)) return refuse("la tol\xC3\xA9rance ne sert qu'\xC3\xA0 un retour REAL (ici " + (f->returnType.empty() ? std::string("aucun") : f->returnType) + ")");
        if (!parseTolerance(nc.tolerance, d) || d < 0) return refuse("tol\xC3\xA9rance \xC2\xAB " + trim(nc.tolerance) + " \xC2\xBB : un nombre positif (0.001)");
    }
    // Ce qui est tape, garde lu (2,5 -> 2.5) : la liste, l'apercu et le rapport montrent ce que le banc joue.
    std::vector<std::string> kept;
    for (std::size_t k = 0; k < nc.arguments.size(); ++k) {
        const std::string text = trim(nc.arguments[k]);
        kept.push_back(text.empty() || params[k].output ? text : keptCell(doc_->project, params[k].read, text));
    }
    // 1.13.0 (D3b) : l'attendu garde comme le banc le relit (un mot seul n'est un calcul que s'il nomme une variable ; NAN).
    const std::string keptExpected = trim(nc.expected).empty() ? std::string{} : hmi::testExpected(doc_->project, returnRead, trim(nc.expected)).kept;
    Id made = kNoId;
    const Id view = view_, fid = function_;
    auto cmd = hmi::changeProject(doc_, "Nouveau cas " + name + " (" + f->name + ")", [&](hmi::Project& p) {
        auto* g = functionIn(p, view, fid);
        if (!g) return;
        hmi::FunctionTest t;
        t.id = p.allocate();
        t.name = name;
        t.description = trim(nc.description);
        for (const auto& a : kept) t.arguments.push_back(a);
        while (!t.arguments.empty() && t.arguments.back().empty()) t.arguments.pop_back();
        t.expected = keptExpected;
        t.tolerance = trim(nc.tolerance);
        t.errorCode = trim(nc.errorCode);
        made = t.id;
        g->tests.push_back(std::move(t));
    });
    if (cmd) apply_(std::move(cmd));
    if (made != kNoId) {
        selectCase(made);
        say("Cas " + name + " ajout\xC3\xA9 \xC3\xA0 " + (fn() ? fn()->name : std::string{}) + " : Lancer ce cas pour son verdict (Ctrl+Z le retire).");
    }
    return made;
}

Id HmiFunctionTestsPage::addCase(std::string* why) {
    const auto* f = fn();
    if (!f) {
        if (why) *why = "aucune fonction choisie";
        return kNoId;
    }
    NewCase c;
    for (int k = 1; k < 1000; ++k) {
        c.name = "cas " + std::to_string(k);
        if (std::none_of(f->tests.begin(), f->tests.end(), [&](const hmi::FunctionTest& t) { return sameText(t.name, c.name); })) break;
    }
    return addCase(c, why);
}

Id HmiFunctionTestsPage::duplicateCase(Id tid, std::string* why) {
    const auto* f = fn();
    const auto* t = test(tid);
    if (!f || !t) {
        if (why) *why = "cas introuvable";
        return kNoId;
    }
    hmi::FunctionTest copy = *t;
    std::string name = t->name + " (copie)";
    for (int k = 2; std::any_of(f->tests.begin(), f->tests.end(), [&](const hmi::FunctionTest& x) { return sameText(x.name, name); }) && k < 1000; ++k)
        name = t->name + " (copie " + std::to_string(k) + ")";
    Id made = kNoId;
    const Id view = view_, fid = function_;
    auto cmd = hmi::changeProject(doc_, "Dupliquer le cas " + t->name, [&](hmi::Project& p) {
        auto* g = functionIn(p, view, fid);
        if (!g) return;
        hmi::FunctionTest x = copy;
        x.id = p.allocate();
        x.name = name;
        made = x.id;
        auto at = g->tests.end();
        for (auto it = g->tests.begin(); it != g->tests.end(); ++it)
            if (it->id == tid) at = std::next(it);
        g->tests.insert(at, std::move(x));
    });
    if (cmd) apply_(std::move(cmd));
    if (made != kNoId) {
        selectCase(made);
        say(copy.name + " dupliqu\xC3\xA9 : " + name + " (Ctrl+Z le retire).");
    }
    return made;
}

bool HmiFunctionTestsPage::removeCase(Id tid, std::string* why) {
    const auto* t = test(tid);
    if (!t) {
        if (why) *why = "cas introuvable";
        return false;
    }
    const std::string name = t->name;
    const Id view = view_, fid = function_;
    auto cmd = hmi::changeProject(doc_, "Supprimer le cas " + name, [&](hmi::Project& p) {
        if (auto* g = functionIn(p, view, fid))
            g->tests.erase(std::remove_if(g->tests.begin(), g->tests.end(), [&](const hmi::FunctionTest& x) { return x.id == tid; }), g->tests.end());
    });
    if (cmd) apply_(std::move(cmd));
    say("Cas " + name + " supprim\xC3\xA9 (Ctrl+Z le rend).");
    return true;
}

bool HmiFunctionTestsPage::setField(Id tid, Field field, const std::string& raw, std::string* why) {
    const auto* f = fn();
    const auto* t = test(tid);
    if (!f || !t) {
        if (why) *why = "cas introuvable";
        return false;
    }
    std::string text = trim(raw);
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        return false;
    };
    std::string label;
    switch (field) {
        case Field::Name:
            if (text.empty()) return refuse("un cas a un nom");
            if (text == t->name) return true;
            for (const auto& o : f->tests)
                if (o.id != tid && sameText(o.name, text)) return refuse("le cas \xC2\xAB " + text + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 dans " + f->name);
            label = "Renommer le cas " + t->name + " en " + text;
            break;
        case Field::Description:
            if (text == t->description) return true;
            label = "Description du cas " + t->name;
            break;
        case Field::Expected:
            if (!text.empty()) {
                if (f->returnType.empty()) return refuse("la fonction n'a pas de retour : v\xC3\xA9rifie plut\xC3\xB4t ses variables (V\xC3\xA9rifier apr\xC3\xA8s)");
                // 1.13.0 (seconde passe) : un cas qui attend une erreur juge sur elle - un attendu tape ne servirait a rien.
                if (!t->errorCode.empty())
                    return refuse("le cas attend l'erreur " + t->errorCode + " : vide d'abord l'Erreur attendue (sa fiche) pour comparer le retour");
                // 1.13.0 (D3b) : lu comme le banc le relira (testExpected : Arret reste un texte meme si un bouton
                // porte ce nom ; NAN, INF pour un REAL).
                const auto e = hmi::testExpected(doc_->project, hmi::testCellType(*f, f->returnType), text);
                if (!e.ok()) return refuse("attendu : " + e.why);
                text = e.kept;
            }
            if (text == t->expected) return true;
            label = "Attendu du cas " + t->name;
            break;
        case Field::Tolerance: {
            if (text == t->tolerance) return true;
            double d = 0;
            if (!text.empty()) {
                if (!toleranceApplies(f))
                    return refuse("la tol\xC3\xA9rance ne sert qu'\xC3\xA0 un retour REAL (ici " + (f->returnType.empty() ? std::string("aucun") : f->returnType) + ")");
                if (!parseTolerance(text, d) || d < 0) return refuse("tol\xC3\xA9rance \xC2\xAB " + text + " \xC2\xBB : un nombre positif (0.001)");
            }
            label = "Tol\xC3\xA9rance du cas " + t->name;
            break;
        }
        case Field::ErrorCode:
            if (text == t->errorCode) return true;
            if (!text.empty() && !hmi::isIdentifier(text)) return refuse("un code d'erreur s'\xC3\xA9" "crit comme un nom (E_Niveau, E_DIVISION)");
            label = "Erreur attendue du cas " + t->name;
            break;
        case Field::Enabled: {
            const bool on = hmi::parseBool(text, true);
            if (on == t->enabled) return true;
            label = std::string(on ? "Activer" : "D\xC3\xA9sactiver") + " le cas " + t->name;
            break;
        }
    }
    const bool ok = change(label, [field, text](hmi::FunctionTest& x, std::string*) {
        switch (field) {
            case Field::Name: x.name = text; break;
            case Field::Description: x.description = text; break;
            case Field::Expected: x.expected = text; break;
            case Field::Tolerance: x.tolerance = text; break;
            case Field::ErrorCode: x.errorCode = text; break;
            case Field::Enabled: x.enabled = hmi::parseBool(text, true); break;
        }
        return true;
    }, tid, why);
    if (ok) say(label + " (Ctrl+Z).");
    return ok;
}

bool HmiFunctionTestsPage::setArgument(Id tid, std::size_t index, const std::string& raw, std::string* why) {
    const auto* f = fn();
    const auto* t = test(tid);
    if (!f || !t) {
        if (why) *why = "cas introuvable";
        return false;
    }
    const auto params = paramsOf(*f);
    std::string text = trim(raw);
    if (index >= params.size()) {
        // 1.13.0 (seconde passe) : un argument en trop (au-dela de la signature) ne se retape pas, il se retire.
        if (index < t->arguments.size() && text.empty()) {
            const bool ok = change("Retirer l'argument " + std::to_string(index + 1) + " du cas " + t->name, [index](hmi::FunctionTest& x, std::string*) {
                if (index < x.arguments.size()) x.arguments[index].clear();
                while (!x.arguments.empty() && trim(x.arguments.back()).empty()) x.arguments.pop_back();
                return true;
            }, tid, why);
            if (ok) say("Argument " + std::to_string(index + 1) + " retir\xC3\xA9 du cas " + t->name + " (Ctrl+Z le rend).");
            return ok;
        }
        if (why) *why = "pas de param\xC3\xA8tre " + std::to_string(index + 1) + " (" + f->name + " en a " + std::to_string(params.size())
                        + ") : vide la case pour retirer cet argument";
        return false;
    }
    if (!text.empty() && params[index].output) {
        if (why) *why = params[index].name + " est une sortie : rien \xC3\xA0 lui donner";
        return false;
    }
    if (!text.empty())
        if (const auto cell = hmi::cellValue(&doc_->project, params[index].read, text); !cell.ok()) {
            if (why) *why = params[index].name + " : " + cell.why;
            return false;
        }
    text = keptCell(doc_->project, params[index].read, text);
    if ((index < t->arguments.size() ? t->arguments[index] : std::string{}) == text) return true;
    const std::string pname = params[index].name;
    const bool ok = change("Argument " + pname + " du cas " + t->name, [index, text](hmi::FunctionTest& x, std::string*) {
        if (x.arguments.size() <= index) x.arguments.resize(index + 1);
        x.arguments[index] = text;
        while (!x.arguments.empty() && x.arguments.back().empty()) x.arguments.pop_back();
        return true;
    }, tid, why);
    if (ok) say(pname + " := " + (text.empty() ? std::string("(sa valeur par d\xC3\xA9" "faut)") : text) + " (Ctrl+Z).");
    return ok;
}

namespace {
// Une pose ou une verif vise une variable IHM (un chemin : Four1.Vannes[1].Position) ou l'automate (API.x) ;
// vide : bonne, sinon la raison.
std::string targetProblem(const hmi::Project& p, const std::string& target, bool journal) {
    if (target.empty()) return "une cible (une variable IHM)";
    if (journal && sameText(target, "JOURNAL")) return {};
    if (!hmi::Expression::compile(target).valid()) return "cible \xC2\xAB " + target + " \xC2\xBB illisible";
    std::string root;
    for (const char c : target) {
        if (c == '.' || c == '[') break;
        root += c;
    }
    if (sameText(root, "API")) return {};
    if (!p.variable(trim(root))) return "variable IHM inconnue \xC2\xAB " + trim(root) + " \xC2\xBB";
    return {};
}
} // namespace

bool HmiFunctionTestsPage::addGiven(Id tid, const std::string& rawTarget, const std::string& rawValue, std::string* why) {
    const std::string target = trim(rawTarget), typedValue = trim(rawValue);
    if (const std::string bad = targetProblem(doc_->project, target, false); !bad.empty()) {
        if (why) *why = bad;
        return false;
    }
    std::string value = typedValue;
    if (const auto* v = doc_->project.variable(target)) {
        if (const auto cell = hmi::cellValue(&doc_->project, v->type, value); !cell.ok()) {
            if (why) *why = target + " : " + cell.why;
            return false;
        }
        value = keptCell(doc_->project, v->type, value);
    }
    const auto* t = test(tid);
    const bool ok = change("Poser " + target + " (cas " + (t ? t->name : std::string{}) + ")", [target, value](hmi::FunctionTest& x, std::string*) {
        x.givens.push_back({target, value});
        return true;
    }, tid, why);
    if (ok) say("Avant l'appel : " + target + " := " + value + " (Ctrl+Z).");
    return ok;
}

bool HmiFunctionTestsPage::setGiven(Id tid, std::size_t index, const std::string& rawTarget, const std::string& rawValue, std::string* why) {
    const auto* t = test(tid);
    if (!t || index >= t->givens.size()) {
        if (why) *why = "pose introuvable";
        return false;
    }
    const std::string target = trim(rawTarget);
    std::string value = trim(rawValue);
    if (const std::string bad = targetProblem(doc_->project, target, false); !bad.empty()) {
        if (why) *why = bad;
        return false;
    }
    if (const auto* v = doc_->project.variable(target)) {
        if (const auto cell = hmi::cellValue(&doc_->project, v->type, value); !cell.ok()) {
            if (why) *why = target + " : " + cell.why;
            return false;
        }
        value = keptCell(doc_->project, v->type, value);
    }
    if (t->givens[index].target == target && t->givens[index].value == value) return true;
    return change("Pose " + target + " (cas " + t->name + ")", [index, target, value](hmi::FunctionTest& x, std::string*) {
        if (index >= x.givens.size()) return false;
        x.givens[index] = {target, value};
        return true;
    }, tid, why);
}

bool HmiFunctionTestsPage::removeGiven(Id tid, std::size_t index, std::string* why) {
    const auto* t = test(tid);
    if (!t || index >= t->givens.size()) {
        if (why) *why = "pose introuvable";
        return false;
    }
    const std::string target = t->givens[index].target;
    const bool ok = change("Retirer la pose " + target + " (cas " + t->name + ")", [index](hmi::FunctionTest& x, std::string*) {
        if (index >= x.givens.size()) return false;
        x.givens.erase(x.givens.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }, tid, why);
    if (ok) say("Pose " + target + " retir\xC3\xA9" "e (Ctrl+Z la rend).");
    return ok;
}

namespace {
// 1.13.0 (D3b) : l'attendu d'une verif, lu et garde dans le type de sa cible comme celui du retour (testExpected) : le
// banc le relit pareil, quoi que le projet nomme ensuite. JOURNAL : le texte cherche, tel quel.
bool keptCheckExpected(const hmi::Project& p, const std::string& target, std::string& expected, std::string* why) {
    if (expected.empty() || sameText(target, "JOURNAL")) return true;
    const auto e = hmi::testExpected(p, hmi::types::typeOfPath(p, target), expected);
    if (!e.ok()) {
        if (why) *why = target + " : attendu : " + e.why;
        return false;
    }
    expected = e.kept;
    return true;
}
} // namespace

bool HmiFunctionTestsPage::addCheck(Id tid, const std::string& rawTarget, const std::string& rawExpected, const std::string& rawTol, std::string* why) {
    const std::string target = trim(rawTarget), tol = trim(rawTol);
    std::string expected = trim(rawExpected);
    if (const std::string bad = targetProblem(doc_->project, target, true); !bad.empty()) {
        if (why) *why = bad;
        return false;
    }
    if (!keptCheckExpected(doc_->project, target, expected, why)) return false;
    double d = 0;
    if (!tol.empty() && (!parseTolerance(tol, d) || d < 0)) {
        if (why) *why = "tol\xC3\xA9rance \xC2\xAB " + tol + " \xC2\xBB : un nombre positif";
        return false;
    }
    const auto* t = test(tid);
    const bool ok = change("V\xC3\xA9rifier " + target + " (cas " + (t ? t->name : std::string{}) + ")", [target, expected, tol](hmi::FunctionTest& x, std::string*) {
        x.checks.push_back({target, expected, tol});
        return true;
    }, tid, why);
    if (ok) say("Apr\xC3\xA8s l'appel : " + target + (sameText(target, "JOURNAL") ? " contient " : " = ") + expected + " (Ctrl+Z).");
    return ok;
}

bool HmiFunctionTestsPage::setCheck(Id tid, std::size_t index, const std::string& rawTarget, const std::string& rawExpected, const std::string& rawTol,
                                    std::string* why) {
    const auto* t = test(tid);
    if (!t || index >= t->checks.size()) {
        if (why) *why = "v\xC3\xA9rif introuvable";
        return false;
    }
    const std::string target = trim(rawTarget), tol = trim(rawTol);
    std::string expected = trim(rawExpected);
    const auto& cur = t->checks[index];
    if (cur.target == target && cur.expected == expected && cur.tolerance == tol) return true;
    if (const std::string bad = targetProblem(doc_->project, target, true); !bad.empty()) {
        if (why) *why = bad;
        return false;
    }
    if (!keptCheckExpected(doc_->project, target, expected, why)) return false;
    if (cur.target == target && cur.expected == expected && cur.tolerance == tol) return true;
    double d = 0;
    if (!tol.empty() && (!parseTolerance(tol, d) || d < 0)) {
        if (why) *why = "tol\xC3\xA9rance \xC2\xAB " + tol + " \xC2\xBB : un nombre positif";
        return false;
    }
    return change("V\xC3\xA9rif " + target + " (cas " + t->name + ")", [index, target, expected, tol](hmi::FunctionTest& x, std::string*) {
        if (index >= x.checks.size()) return false;
        x.checks[index] = {target, expected, tol};
        return true;
    }, tid, why);
}

bool HmiFunctionTestsPage::removeCheck(Id tid, std::size_t index, std::string* why) {
    const auto* t = test(tid);
    if (!t || index >= t->checks.size()) {
        if (why) *why = "v\xC3\xA9rif introuvable";
        return false;
    }
    const std::string target = t->checks[index].target;
    const bool ok = change("Retirer la v\xC3\xA9rif " + target + " (cas " + t->name + ")", [index](hmi::FunctionTest& x, std::string*) {
        if (index >= x.checks.size()) return false;
        x.checks.erase(x.checks.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }, tid, why);
    if (ok) say("V\xC3\xA9rif " + target + " retir\xC3\xA9" "e (Ctrl+Z la rend).");
    return ok;
}

// ---- jouer ----
void HmiFunctionTestsPage::emitLines(const std::vector<TestLine>& lines) {
    if (hosts_.output) hosts_.output(lines);
}

bool HmiFunctionTestsPage::runCase(Id tid) {
    const auto* f = fn();
    const auto* t = test(tid);
    if (!f || !t) return false;
    const hmi::HmiFunction copy = *f;            // le banc ne touche rien, mais la page peut se refaire pendant
    const hmi::FunctionTest one = *t;
    hmi::FunctionTestReport rep;
    rep.function = copy.id;
    rep.functionName = copy.name;
    rep.results.push_back(hmi::runFunctionTest(doc_->project, copy, one, hosts_.plc ? hosts_.plc() : nullptr));
    testruns::record(doc_->project, copy, rep.results.front());
    emitLines(testLines(copy, rep, ownerName()));
    const auto& r = rep.results.front();
    const bool passed = r.verdict == Verdict::Passed;
    say(one.name + " : " + std::string(hmi::verdictLabel(r.verdict)) + (r.detail.empty() ? std::string{} : " \xE2\x80\x94 " + r.detail),
        !passed && r.verdict != Verdict::Skipped);
    return passed;
}

std::size_t HmiFunctionTestsPage::runAll() {
    const auto* f = fn();
    if (!f || f->tests.empty()) {
        say("Aucun cas \xC3\xA0 lancer : \xC2\xAB Nouveau cas\xE2\x80\xA6 \xC2\xBB d'abord.", true);
        return 0;
    }
    const hmi::HmiFunction copy = *f;
    const auto rep = hmi::runFunctionTests(doc_->project, copy, hosts_.plc ? hosts_.plc() : nullptr);
    testruns::record(doc_->project, copy, rep);
    emitLines(testLines(copy, rep, ownerName()));
    const auto tally = testruns::tally(doc_->project, copy);
    say("Tests de " + copy.name + " : " + tally.text(), tally.red());
    return tally.passed;
}

const hmi::FunctionTestResult* HmiFunctionTestsPage::lastResult(Id tid) const {
    const auto* f = fn();
    const auto* t = test(tid);
    return f && t ? testruns::last(doc_->project, *f, *t) : nullptr;
}

bool HmiFunctionTestsPage::exportReport(hmi::ExportFormat format, std::string* where) {
    const auto* f = fn();
    if (!f || f->tests.empty()) {
        say("Rien \xC3\xA0 rapporter : la fonction n'a pas de cas.", true);
        return false;
    }
    const hmi::HmiFunction copy = *f;
    TestCampaign c = runTestsOf(doc_->project, {copy}, view_, ownerName(), hosts_.plc ? hosts_.plc() : nullptr);
    emitLines(c.lines);
    hmi::ExportTable table = testReportTable(c, &copy);
    table.subtitle = doc_->project.config.name + " - " + hmi::wallStamp().substr(0, 19) + " - " + table.subtitle;
    hmi::ExportRequest rq;
    rq.fileName = hmi::exportFileName("tests_" + fileSafe(copy.name) + "_" + hmi::wallStamp().substr(0, 10), format);
    rq.format = std::string(hmi::exportFormatLabel(format));
    rq.source = "tests";
    rq.rows = table.rows.size();
    rq.data = std::make_shared<const hmi::Bytes>(hmi::exportBytes(table, format));
    rq.origin = "Fonctions > " + copy.name + " > Tests";
    std::string path;
    const bool ok = hosts_.exportFile && hosts_.exportFile(rq, &path);
    if (where) *where = path;
    say(ok ? "Rapport des tests \xC3\xA9" "crit : " + path : "Rapport impossible \xC3\xA0 \xC3\xA9" "crire" + (path.empty() ? std::string{} : " : " + path), !ok);
    return ok;
}

void HmiFunctionTestsPage::onLayout() {
    const auto b = bounds();
    constexpr float kTools = 34.f, kStatus = 22.f;
    tools_->setBounds({b.x, b.y, b.w, kTools});
    status_->setBounds({b.x, b.y + b.h - kStatus, b.w, kStatus});
    const float top = b.y + kTools, h = std::max(0.f, b.h - kTools - kStatus);
    const float tableH = std::floor(h * 0.42f);
    cases_->setBounds({b.x, top, b.w, tableH});
    const float y = top + tableH + 4.f, rest = std::max(0.f, h - tableH - 4.f);
    const float gridW = std::floor(b.w * 0.5f);
    grid_->setBounds({b.x, y, gridW, rest});
    const float sx = b.x + gridW + 4.f, sw = std::max(0.f, b.w - gridW - 4.f);
    const float half = std::floor(rest * 0.5f);
    givensPanel_->setBounds({sx, y, sw, half - 2.f});
    checksPanel_->setBounds({sx, y + half + 2.f, sw, std::max(0.f, rest - half - 2.f)});
}

void HmiFunctionTestsPage::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

// ======================================================= " Nouveau cas... " ===
namespace {
const gfx::FontId kSmall{13};
constexpr float   kLabelW = 170.f;
constexpr float   kFieldH = 28.f;
constexpr float   kNoteH = 18.f;
constexpr std::size_t kMaxArgs = 8;   // au-dela : dans le detail du cas (l'onglet Tests)
} // namespace

class HmiNewTestCaseDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiNewTestCaseDialog& d) : ui::Widget("dialog.hmiNewTestCase.body"), d_(d) {}
    ui::Button* cancel{nullptr};
    gfx::Rect   panel{};
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(780.f, r.w - 40.f);
        const std::size_t args = std::min(d_.argFields_.size(), kMaxArgs);
        float h = 44.f + 10.f + 40.f;                                   // titre, texte
        h += static_cast<float>(2 + args + 2) * (kFieldH + kNoteH + 4.f);   // nom, description, arguments, attendu, tolerance
        h += 10.f + 54.f + 56.f;                                         // l'apercu, les boutons
        h = std::min(h, r.h - 40.f);
        panel = ui::dialogframe::place(*this, {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h});
        const float left = panel.x + 20.f, fieldX = left + kLabelW, fieldW = panel.w - 40.f - kLabelW;
        float y = panel.y + 44.f + 10.f;
        textY_ = y;
        y += 40.f;
        ys_.clear();
        const auto place = [&](ui::Widget* f) {
            ys_.push_back(y);
            if (f) f->setBounds({fieldX, y, fieldW, kFieldH});
            y += kFieldH + kNoteH + 4.f;
        };
        place(d_.nameField_);
        place(d_.descField_);
        for (std::size_t k = 0; k < d_.argFields_.size(); ++k) {
            if (k < kMaxArgs) place(d_.argFields_[k]);
            else d_.argFields_[k]->setBounds({0, 0, 0, 0});
        }
        place(d_.expectedField_);
        place(d_.toleranceField_);
        previewY_ = y + 4.f;
        float bx = panel.x + panel.w - 20.f;
        for (ui::Button* b : {d_.ok_, cancel}) {
            if (!b) continue;
            const float bw = std::max(104.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            b->setBounds({bx, panel.y + panel.h - 46.f, bw, 32.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const float left = panel.x + 20.f, fieldX = left + kLabelW;
        ctx.r.pushClip({panel.x + 8.f, panel.y + th, panel.w - 16.f, panel.h - th});
        ctx.r.drawText({left, textY_}, "Un cas \xC3\xA9pingle des arguments et le r\xC3\xA9sultat attendu : il se rejoue \xC3\xA0 chaque Compiler et D\xC3\xA9marrer.",
                       ctx.theme.font.ui, c.text);
        ctx.r.drawText({left, textY_ + 20.f}, "Chaque case se lit comme toute case de valeur (2,5 -> 2.5, VRAI -> TRUE) ; vide : la valeur par d\xC3\xA9" "faut.",
                       kSmall, c.textMuted);
        const auto label = [&](float at, const std::string& text) {
            ctx.r.pushClip({left, at, kLabelW - 8.f, kFieldH});
            ctx.r.drawText({left, at + (kFieldH - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, text, ctx.theme.font.ui, c.textMuted);
            ctx.r.popClip();
        };
        const auto note = [&](float at, const std::string& text, gfx::Color col) {
            if (!text.empty()) ctx.r.drawText({fieldX + 2.f, at + kFieldH + 2.f}, text, kSmall, col);
        };
        std::size_t i = 0;
        if (i < ys_.size()) {
            label(ys_[i], "Nom du cas");
            const std::string p = d_.nameProblem();
            note(ys_[i], p.empty() ? std::string("unique dans la fonction") : p, p.empty() ? c.textMuted : c.error);
            ++i;
        }
        if (i < ys_.size()) label(ys_[i++], "Description");
        const auto labels = d_.parameterLabels();
        for (std::size_t k = 0; k < d_.argFields_.size() && k < kMaxArgs && i < ys_.size(); ++k, ++i) {
            label(ys_[i], k < labels.size() ? labels[k] : std::string{});
            const std::string p = d_.argumentProblem(k);
            if (!p.empty()) note(ys_[i], p, c.error);
        }
        if (i < ys_.size()) {
            label(ys_[i], "Attendu");
            const std::string p = d_.expectedProblem();
            note(ys_[i], !p.empty() ? p : d_.expectedUsable() ? std::string("ce que la fonction doit rendre ; vide : seules les v\xC3\xA9rifs jugent")
                                                             : std::string("une proc\xC3\xA9" "dure : ses v\xC3\xA9rifs jugent (onglet Tests)"),
                 p.empty() ? c.textMuted : c.error);
            ++i;
        }
        if (i < ys_.size()) {
            label(ys_[i], "Tol\xC3\xA9rance");
            const std::string p = d_.toleranceProblem();
            note(ys_[i], !p.empty() ? p : d_.toleranceUsable() ? std::string("|obtenu - attendu| \xE2\x89\xA4 tol\xC3\xA9rance ; vide : exacte")
                                                              : std::string("seulement pour un retour REAL"),
                 p.empty() ? c.textMuted : c.error);
            ++i;
        }
        const gfx::Rect box{left, previewY_, panel.w - 40.f, 48.f};
        ctx.r.fillRect(box, ctx.theme.brand.codeBg);
        ctx.r.strokeRect(box, c.border, 1.f);
        ctx.r.drawText({box.x + 10.f, box.y + 6.f}, "L'appel jou\xC3\xA9", kSmall, c.textMuted);
        ctx.r.drawText({box.x + 10.f, box.y + 24.f}, d_.preview(), ctx.theme.font.mono, c.text);
        ctx.r.popClip();
    }
private:
    HmiNewTestCaseDialog& d_;
    std::vector<float>    ys_;
    float textY_{0}, previewY_{0};
};

HmiNewTestCaseDialog::HmiNewTestCaseDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    if (const auto* f = fn()) {
        for (const auto& p : paramsOf(*f)) {
            paramNames_.push_back(p.name);
            paramTypes_.push_back(p.type);
            paramReads_.push_back(p.read);
            paramUnits_.push_back(p.unit);
            paramDefaults_.push_back(p.initial);
            paramOutput_.push_back(p.output);
        }
        answer_.arguments.assign(paramNames_.size(), {});
        for (int k = 1; k < 1000; ++k) {
            answer_.name = "cas " + std::to_string(k);
            if (std::none_of(f->tests.begin(), f->tests.end(), [&](const hmi::FunctionTest& t) { return sameText(t.name, answer_.name); })) break;
        }
    }
}

HmiNewTestCaseDialog::~HmiNewTestCaseDialog() = default;

const hmi::HmiFunction* HmiNewTestCaseDialog::fn() const {
    return spec_.doc ? functionIn(spec_.doc->project, spec_.view, spec_.function) : nullptr;
}

menu::MenuTraits HmiNewTestCaseDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiNewTestCaseDialog::title() const {
    const auto* f = fn();
    return "Nouveau cas de test" + (f ? " \xE2\x80\x94 " + f->name : std::string{});
}

std::vector<std::string> HmiNewTestCaseDialog::parameterLabels() const {
    std::vector<std::string> out;
    for (std::size_t k = 0; k < paramNames_.size(); ++k)
        out.push_back(paramNames_[k] + " : " + hmi::units::withUnit(paramTypes_[k], paramUnits_[k])
                      + (paramOutput_[k] ? std::string("  (sortie)") : paramDefaults_[k].empty() ? std::string{} : "  (d\xC3\xA9" "faut " + paramDefaults_[k] + ")"));
    return out;
}

std::string HmiNewTestCaseDialog::preview() const {
    const auto* f = fn();
    std::string s = (f ? f->name : std::string("?")) + "(";
    bool first = true;
    for (std::size_t k = 0; k < paramNames_.size(); ++k) {
        if (paramOutput_[k]) continue;
        const std::string v = k < answer_.arguments.size() ? trim(answer_.arguments[k]) : std::string{};
        std::string shown = v;
        if (!v.empty() && spec_.doc)
            if (const auto cell = hmi::cellValue(&spec_.doc->project, paramReads_[k], v); cell.ok()) shown = cell.text;
        if (shown.empty()) shown = paramDefaults_[k].empty() ? std::string("\xE2\x80\xA6") : paramDefaults_[k];
        s += (first ? "" : ", ") + shown;
        first = false;
    }
    s += ")";
    if (f && !f->returnType.empty() && !trim(answer_.expected).empty())
        s += "  \xE2\x86\x92  " + trim(answer_.expected) + (trim(answer_.tolerance).empty() ? std::string{} : " \xC2\xB1 " + trim(answer_.tolerance));
    return s;
}

std::string HmiNewTestCaseDialog::nameProblem() const {
    const std::string n = trim(answer_.name);
    if (n.empty()) return "Son nom : il nomme le cas dans les verdicts.";
    if (const auto* f = fn())
        for (const auto& t : f->tests)
            if (sameText(t.name, n)) return "\xC2\xAB " + n + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 dans " + f->name + ".";
    return {};
}

std::string HmiNewTestCaseDialog::argumentProblem(std::size_t k) const {
    if (k >= answer_.arguments.size() || k >= paramTypes_.size() || !spec_.doc) return {};
    const std::string v = trim(answer_.arguments[k]);
    if (v.empty()) return {};
    if (paramOutput_[k]) return paramNames_[k] + " est une sortie : rien \xC3\xA0 lui donner.";
    const auto cell = hmi::cellValue(&spec_.doc->project, paramReads_[k], v);
    return cell.ok() ? std::string{} : cell.why;
}

bool HmiNewTestCaseDialog::expectedUsable() const {
    const auto* f = fn();
    return f && !f->returnType.empty();
}

bool HmiNewTestCaseDialog::toleranceUsable() const { return toleranceApplies(fn()); }

std::string HmiNewTestCaseDialog::expectedProblem() const {
    const std::string v = trim(answer_.expected);
    if (v.empty()) return {};
    const auto* f = fn();
    if (!f || f->returnType.empty()) return "La fonction n'a pas de retour : rien \xC3\xA0 attendre.";
    return hmi::testExpected(spec_.doc->project, hmi::testCellType(*f, f->returnType), v).why;   // 1.13.0 : T dans sa contrainte ; NAN
}

std::string HmiNewTestCaseDialog::toleranceProblem() const {
    const std::string v = trim(answer_.tolerance);
    if (v.empty()) return {};
    if (!toleranceUsable()) return "La tol\xC3\xA9rance ne sert qu'\xC3\xA0 un retour REAL.";
    double d = 0;
    if (!parseTolerance(v, d) || d < 0) return "Un nombre positif (0.001).";
    return {};
}

bool HmiNewTestCaseDialog::canConfirm() const {
    if (!fn() || !nameProblem().empty() || !expectedProblem().empty() || !toleranceProblem().empty()) return false;
    for (std::size_t k = 0; k < answer_.arguments.size(); ++k)
        if (!argumentProblem(k).empty()) return false;
    return true;
}

void HmiNewTestCaseDialog::refresh() {
    syncing_ = true;
    if (nameField_ && nameField_->text() != answer_.name) nameField_->setText(answer_.name);
    if (descField_ && descField_->text() != answer_.description) descField_->setText(answer_.description);
    for (std::size_t k = 0; k < argFields_.size() && k < answer_.arguments.size(); ++k)
        if (argFields_[k]->text() != answer_.arguments[k]) argFields_[k]->setText(answer_.arguments[k]);
    if (expectedField_ && expectedField_->text() != answer_.expected) expectedField_->setText(answer_.expected);
    if (toleranceField_ && toleranceField_->text() != answer_.tolerance) toleranceField_->setText(answer_.tolerance);
    syncing_ = false;
    if (expectedField_) expectedField_->setEnabled(expectedUsable());
    if (toleranceField_) toleranceField_->setEnabled(toleranceUsable());
    if (ok_) ok_->setEnabled(canConfirm());
    if (body_) {
        body_->invalidateLayout();
        body_->invalidate();
    }
}

void HmiNewTestCaseDialog::setName(const std::string& name) {
    answer_.name = name;
    refresh();
}
void HmiNewTestCaseDialog::setDescription(const std::string& text) {
    answer_.description = text;
    refresh();
}
void HmiNewTestCaseDialog::setArgument(std::size_t index, const std::string& text) {
    if (index >= answer_.arguments.size()) return;
    answer_.arguments[index] = text;
    refresh();
}
void HmiNewTestCaseDialog::setExpected(const std::string& text) {
    answer_.expected = text;
    refresh();
}
void HmiNewTestCaseDialog::setTolerance(const std::string& text) {
    answer_.tolerance = text;
    refresh();
}

bool HmiNewTestCaseDialog::confirm() {
    if (!canConfirm()) {
        if (nameField_) {
            focus().clear();
            focus().focus(nameField_);
        }
        return false;
    }
    finish(true);
    return true;
}

void HmiNewTestCaseDialog::cancel() { finish(false); }

std::string HmiNewTestCaseDialog::encode(const Answer& a) {
    std::vector<std::string> fields{trim(a.name), a.description, a.expected, a.tolerance, a.errorCode};
    for (const auto& arg : a.arguments) fields.push_back(arg);
    return core::joinFields(fields);
}

bool HmiNewTestCaseDialog::parse(const std::string& payload, Answer& out) {
    const auto v = core::splitFields(payload);
    if (v.size() < 5 || v[0].empty()) return false;
    out = Answer{};
    out.name = v[0];
    out.description = v[1];
    out.expected = v[2];
    out.tolerance = v[3];
    out.errorCode = v[4];
    out.arguments.assign(v.begin() + 5, v.end());
    return true;
}

void HmiNewTestCaseDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? encode(answer_) : std::string{}});
}

core::Status HmiNewTestCaseDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    const auto field = [&](const std::string& key, const std::string& placeholder, const std::string& text, std::function<void(const std::string&)> set) {
        auto f = std::make_unique<ui::InputText>(base + "." + key);
        f->setPlaceholder(placeholder);
        f->setText(text);
        auto* raw = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        links_ += raw->textChanged->connect([this, set](const std::string& t) {
            if (!syncing_) set(t);
        });
        links_ += raw->editingDone->connect([this, raw](const std::string&) {
            if (!syncing_ && raw->focused()) (void)confirm();
        });
        return raw;
    };
    nameField_ = field("name", "moyenne simple, poids 25 %\xE2\x80\xA6", answer_.name, [this](const std::string& t) { setName(t); });
    descField_ = field("description", "ce que le cas v\xC3\xA9rifie", answer_.description, [this](const std::string& t) { setDescription(t); });
    for (std::size_t k = 0; k < paramNames_.size(); ++k) {
        const std::string ph = paramOutput_[k] ? std::string("(sortie)") : paramDefaults_[k].empty() ? std::string("vide") : "d\xC3\xA9" "faut " + paramDefaults_[k];
        argFields_.push_back(field("arg" + std::to_string(k), ph, answer_.arguments[k], [this, k](const std::string& t) { setArgument(k, t); }));
        if (paramOutput_[k]) argFields_.back()->setEnabled(false);
    }
    expectedField_ = field("expected", "16.0, TRUE, 'Azote'\xE2\x80\xA6", answer_.expected, [this](const std::string& t) { setExpected(t); });
    toleranceField_ = field("tolerance", "exacte", answer_.tolerance, [this](const std::string& t) { setTolerance(t); });
    body->cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    links_ += body->cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Cr\xC3\xA9" "er le cas", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { (void)confirm(); });
    setRoot(std::move(body));
    refresh();
    focus().focus(nameField_);
    return core::ok();
}

ui::EventResult HmiNewTestCaseDialog::HandleEvent(const ui::InputEvent& ev) {
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            (void)confirm();
            return ui::EventResult::Consumed;
        }
    }
    return r;
}

} // namespace app
