#include "HmiFunctionPanes.hpp"
#include "../../hmi/HmiOverload.hpp"   // 1.11.20 : les surcharges (leur forme)

#include "HmiAssetPanes.hpp"
#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "HmiScriptPanes.hpp"    // 1.10 : placedScriptDiagnostics, squigglesOf
#include "../../hmi/HmiDecl.hpp"        // 1.11.18 (refonte, lot 3) : les declarations du modele, reconstruites
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiScriptCheck110.hpp"   // 1.10 (S1) : un type de retour riche (structure, enumeration, ARRAY, MAP...)
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiSymbols.hpp"   // 1.11.10 : les fonctions des symboles
#include "../../sim/Runtime.hpp"
#include "../../ui/Theme.hpp"
#include "../../hmi/HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : les types, un seul catalogue
#include "HmiTypePicker.hpp"   // 1.11.19 (refonte, lot 6) : le retour, au selecteur de types

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>


namespace {
// 1.10 (S1) : le type de retour d'une fonction IHM : elementaire, ou riche (ARRAY a N
// dimensions, MAP, REF_TO, POINTER TO, une structure ou une enumeration du projet).
bool returnTypeAllowed(const hmi::Project& p, std::string_view t) {
    return hmi::localTypeSupported(t)
        || hmi::richLocalType(t, [&p](std::string_view n) { return hmi::lang110::knownHmiType(&p, n); });
}
hmi::TypeKnown knownTypeOf(const hmi::Project& p) {
    return [&p](std::string_view n) { return hmi::lang110::knownHmiType(&p, n); };
}
} // namespace

namespace app {

using hmi::Id;
using hmi::kNoId;
using hmikit::Rows;

namespace {

// L'editeur et, dessous, la barre du nom ou est le curseur (comme les scripts).
class CodeArea final : public ui::Widget {
public:
    explicit CodeArea(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        constexpr float kBar = 22.f;
        if (children().size() < 2) return;
        // 1.11.18 (refonte, lot 5) : le bandeau de l'ancien format, au-dessus du code.
        float top = 0.f;
        if (children().size() > 2 && children()[2]->visible()) {
            top = HmiDeclBanner::kHeight;
            children()[2]->setBounds({b.x, b.y, b.w, top});
        }
        children()[0]->setBounds({b.x, b.y + top, b.w, std::max(0.f, b.h - kBar - 2.f - top)});
        children()[1]->setBounds({b.x, b.bottom() - kBar, b.w, kBar});
    }
};

enum ToolAction : int { TNew = 1, TDelete, TTry, TCompile, TExport, TImport,   // 1.11.2 : TExport, TImport (decision 174)
                        TBuildGen, TBuildRegen, TBuildGenComp, TBuildState };   // 1.11.13 : la generation incrementale

// 1.11.18 (refonte, lot 5) : "Aucun" (une procedure) ; "(aucun)", VOID et le vide se relisent aussi.
constexpr const char* kNoReturn = "Aucun";
bool isNoReturn(std::string_view t) {
    std::string u;
    for (const char c : t)
        if (c != '(' && c != ')' && c != ' ') u += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return u.empty() || u == "aucun" || u == "void" || u == "none";
}

std::size_t lineCount(const std::string& s) {
    if (s.empty()) return 0;
    return 1 + static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n')) - (s.back() == '\n' ? 1 : 0);
}

std::string stateOf(const std::vector<hmi::ScriptDiagnostic>& d) {
    std::size_t errors = 0, warnings = 0;
    for (const auto& x : d) {
        errors += x.severity == hmi::ScriptDiagnostic::Severity::Error;
        warnings += x.severity == hmi::ScriptDiagnostic::Severity::Warning;
    }
    if (errors) return std::to_string(errors) + " erreur" + (errors > 1 ? "s" : "");
    if (warnings) return std::to_string(warnings) + " avertissement" + (warnings > 1 ? "s" : "");
    return "OK";
}

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Le resultat d'un essai : un REAL garde sa virgule (18.0, pas 18), une
// chaine ses guillemets ; le reste comme l'IHM l'affiche.
std::string trialText(const sim::Value& v) {
    if (v.type() == sim::Type::Real) {
        const double x = v.asReal();
        char b[48];
        if (std::isfinite(x) && std::fabs(x) < 1e15 && std::floor(x) == x) std::snprintf(b, sizeof b, "%.1f", x);
        else std::snprintf(b, sizeof b, "%.6g", x);
        return b;
    }
    if (v.type() == sim::Type::String) return "'" + v.asString() + "'";
    return hmi::formatValue(v);
}

std::string parameterList(const hmi::HmiFunction& f) {
    std::string out;
    const auto parts = hmi::splitDeclarations(hmi::decl::codeOf(f), true);   // les pointeurs de parameters() vivent avec lui ; 1.11.18 : le modele aussi
    for (const auto* in : parts.parameters())                                  // 1.11.20 : E/S (par reference) et sorties comprises
        out += (out.empty() ? "" : ", ")
             + std::string(in->section == hmi::LocalVar::Section::InOut ? "VAR_IN_OUT " : in->section == hmi::LocalVar::Section::Output ? "VAR_OUTPUT " : "")
             + in->name + " : " + in->type;
    return out.empty() ? std::string("-") : out;
}

// L'AUTOMATE DU BANC D'ESSAI : celui de la simulation s'il tourne, en lecture
// seule (une ecriture est refusee, un bloc n'est pas appele - il avancerait
// d'un cycle) ; sinon un simulateur vide, pour les fonctions standard.
class TrialPlc final : public sim::Environment {
public:
    explicit TrialPlc(sim::Environment* live) : live_(live) {
        if (!live_) standard_ = std::make_unique<sim::Runtime>(std::make_shared<const domain::Project>());
    }
    bool read(std::string_view n, sim::Value& out) override { return live_ && live_->read(n, out); }
    bool write(std::string_view n, const sim::Value&) override {
        refused.emplace_back(n);
        return false;
    }
    bool exists(std::string_view n) override { return live_ && live_->exists(n); }
    bool call(std::string_view name, std::string_view instance,
              const std::vector<std::pair<std::string, sim::Value>>& args, sim::Value& result) override {
        if (!hmi::isStandardFunction(name)) return false;
        return live_ ? live_->call(name, instance, args, result) : standard_->call(name, instance, args, result);
    }
    void report(sim::Diagnostic) override {}
    std::vector<std::string> refused;
private:
    sim::Environment*             live_;
    std::unique_ptr<sim::Runtime> standard_;
};

} // namespace

// 1.11.13 : la cle de build de la fonction choisie, et l'etat que montre la barre.
std::string HmiFunctionsPane::buildKey() const {
    const Id sel = selectedFunction();
    if (sel == kNoId) return {};
    return std::string(symbol_ != kNoId ? "fonction-symbole:" : "fonction:") + std::to_string(sel);
}

std::size_t HmiFunctionsPane::compileCurrent() {
    const auto* f = current();
    if (!f) {
        say("Compiler : aucune fonction choisie (le projet entier : IHM > Compiler).", true);
        return 0;
    }
    updateDiagnostics();
    std::size_t n = 0;
    for (const auto& d : diagnostics_) n += d.severity != hmi::ScriptDiagnostic::Severity::Info ? 1 : 0;
    const auto* sv = symbolView();
    say("Compiler la fonction " + (sv ? sv->name + "." : std::string{}) + f->name + " : "
            + (n ? std::to_string(n) + (n > 1 ? " fautes (un clic sur un r\xC3\xA9sultat y m\xC3\xA8ne)" : " faute (un clic sur le r\xC3\xA9sultat y m\xC3\xA8ne)")
                 : std::string("aucune faute"))
            + " \xC2\xB7 le projet entier : IHM > Compiler",
        n > 0);
    if (hosts_.build) hosts_.build(hmi::pipeline::Mode::Compile, buildKey());   // 1.11.13 : son etat ; 1.11.17 : ses Diagnostics
    return n;
}

ui::EventResult HmiFunctionsPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F7 && k->mods.none() && !k->repeat) {
        (void)compileCurrent();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void HmiFunctionsPane::refreshBuildState() {
    if (!hosts_.buildState || !tools_) return;
    const auto key = buildKey();
    const auto st = key.empty() ? std::pair<std::string, std::string>{"\xE2\x80\x94", "Aucune fonction choisie."} : hosts_.buildState(key);
    if (st.first == buildStateText_) return;
    buildStateText_ = st.first;
    tools_->setText(TBuildState, st.second.empty() ? std::string("L'\xC3\xA9tat de build de la fonction choisie") : st.second, st.first);
}

HmiFunctionsPane::HmiFunctionsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TNew, HmiGlyph::Plus, "Nouvelle fonction (nom, type de retour, description)", "Nouvelle fonction");
    tools->add(TDelete, HmiGlyph::Delete, "Supprimer la fonction (Ctrl+Z la rend)", "Supprimer");
    tools->separator();
    tools->add(TTry, HmiGlyph::Play, "Essayer la fonction : des arguments, le r\xC3\xA9sultat (sans toucher le projet)", "Essayer");
    // 1.11.17 (refonte, lot 1) : la fonction actuelle, elle seule ; le projet : IHM > Compiler.
    tools->add(TCompile, HmiGlyph::Code,
               "Compiler la fonction actuelle (F7) : elle seule, avec la signature de ce qu'elle appelle ; ses fautes ici et dans les "
               "Diagnostics du panneau du bas ; le projet entier : IHM > Compiler",
               "Compiler (F7)");
    // 1.11.13 : la generation incrementale de la fonction choisie, et son etat.
    tools->add(TBuildGen, HmiGlyph::Refresh, "G\xC3\xA9n\xC3\xA9rer la fonction choisie : rien n'est refait si elle est \xC3\xA0 jour", "G\xC3\xA9n\xC3\xA9rer");
    tools->add(TBuildRegen, HmiGlyph::Refresh, "R\xC3\xA9g\xC3\xA9n\xC3\xA9rer la fonction choisie, m\xC3\xAAme \xC3\xA0 jour", "R\xC3\xA9g\xC3\xA9n\xC3\xA9rer");
    tools->add(TBuildGenComp, HmiGlyph::Play, "G\xC3\xA9n\xC3\xA9rer et compiler la fonction choisie (seuls ses appelants touch\xC3\xA9s par sa signature suivent)",
               "G\xC3\xA9n\xC3\xA9rer et compiler");
    tools->add(TBuildState, HmiGlyph::None, "L'\xC3\xA9tat de build de la fonction choisie (un clic : les sorties du build)", "\xE2\x80\x94");
    tools->separator();   // 1.11.2 (decision 174) : les fonctions voyagent (.xpgfonctions)
    tools->add(TExport, HmiGlyph::Export,
               "Exporter des fonctions (.xpgfonctions) avec ce dont elles ont besoin : les fonctions qu'elles appellent, types IHM, variables IHM",
               "Exporter les fonctions\xE2\x80\xA6");
    tools->add(TImport, HmiGlyph::Import,
               "Importer des fonctions (ou tout fichier fait par Exporter) d'un autre projet : renommer ou remplacer chaque nom en conflit, "
               "un seul Ctrl+Z",
               "Importer\xE2\x80\xA6");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setVisibleWhen(TExport, [this] { return static_cast<bool>(hosts_.exportItems); });
    tools_->setVisibleWhen(TImport, [this] { return static_cast<bool>(hosts_.importAny); });
    tools_->setEnabledWhen(TDelete, [this] { return selectedFunction() != kNoId; });
    tools_->setEnabledWhen(TTry, [this] { return selectedFunction() != kNoId; });
    tools_->setEnabledWhen(TCompile, [this] { return selectedFunction() != kNoId; });     // 1.11.17 : rien a compiler
    for (const int a : {static_cast<int>(TBuildGen), static_cast<int>(TBuildRegen), static_cast<int>(TBuildGenComp), static_cast<int>(TBuildState)}) {   // 1.11.13
        tools_->setVisibleWhen(a, [this] { return static_cast<bool>(hosts_.build); });
        if (a != TBuildState) tools_->setEnabledWhen(a, [this] { return selectedFunction() != kNoId; });
    }
    tools_->setVisibleWhen(TTry, [this] { return symbol_ == kNoId; });   // 1.11.10 : une fonction de symbole s'essaie en marche

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto left = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".left");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".functionsPanel", "FONCTIONS IHM");
        auto table = std::make_unique<ui::TableView>(base + ".functions");
        table->setColumns({{"Nom", 150.f}, {"Retour", 80.f}, {"Param\xC3\xA8tres", 230.f},
                           {"Lignes", 70.f, 40.f, true, true, true, ui::Align::End}, {"\xC3\x89tat", 110.f},
                           {"Description", 260.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        functions_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        left->addPane(std::move(panel), 0.52f, 100.f);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".propsPanel", "PROPRI\xC3\x89T\xC3\x89S DE LA FONCTION");
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
        grid->setShowDescriptionPane(false);
        grid->setNameColumnRatio(0.36f);
        props_ = &static_cast<ui::PropertyGrid&>(panel->setBody(std::move(grid)));
        // 1.11.21 : plus de bandeau ESSAI - le resultat d'Essayer va aux Sorties du panneau du bas.
        left->addPane(std::move(panel), 0.48f, 90.f);
    }
    split->addPane(std::move(left), 0.42f, 260.f);

    auto right = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".right");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".editorPanel", "\xC3\x89" "DITEUR");
        auto area = std::make_unique<CodeArea>(base + ".code");
        auto ed = std::make_unique<ui::MultiLineText>(base + ".editor");
        ed->setLanguage(ui::Language::StructuredText);
        ed->setShowLineNumbers(true);
        ed->setReadOnly(true);
        ed->setTabInsertsSpaces(4);
        editor_ = &static_cast<ui::MultiLineText&>(area->addChild(std::move(ed)));
        auto bar = std::make_unique<ui::StatusBar>(base + ".symbol");
        bar->setTooltip("Le nom o\xC3\xB9 est le curseur : param\xC3\xA8tre, locale, variable IHM ou de l'automate, fonction.");
        symbolBar_ = &static_cast<ui::StatusBar&>(area->addChild(std::move(bar)));
        // 1.11.18 (refonte, lot 5) : Code, Parametres, Locales, Constantes ; le bandeau de l'ancien format.
        banner_ = &static_cast<HmiDeclBanner&>(area->addChild(std::make_unique<HmiDeclBanner>(base + ".declBanner")));
        codeTabs_ = &static_cast<HmiCodeTabs&>(panel->setBody(std::make_unique<HmiCodeTabs>(
            base + ".codeTabs", doc_, apply_, std::move(area),
            std::vector{hmi::decledit::Tab::Parameters, hmi::decledit::Tab::Variables, hmi::decledit::Tab::Constants}, banner_)));
        editorPanel_ = panel.get();
        // 1.11.21 : l'editeur prend toute la hauteur - ses fautes vont au panneau du bas
        // (Diagnostics, l'etape Saisie ; HmiLive.hpp), soulignees dans le code.
        right->addPane(std::move(panel), 1.f, 120.f);
    }
    split->addPane(std::move(right), 0.58f, 300.f);
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedFunction();
        switch (a) {
            case TExport: if (hosts_.exportItems) hosts_.exportItems(sel); break;   // 1.11.2 (decision 174)
            case TImport: if (hosts_.importAny) hosts_.importAny(); break;
            case TNew:
                if (hosts_.newFunction) hosts_.newFunction();
                else if (const auto* sv = symbolView()) {               // 1.11.10 : un nom libre dans le symbole
                    std::string n = "Fonction";
                    for (int k = 2; hmi::symbolFunction(*sv, n); ++k) n = "Fonction" + std::to_string(k);
                    (void)addFunction(n, {}, {});
                } else (void)addFunction(hmi::uniqueFunctionName(doc_->project, "Fonction"), "REAL", {});
                break;
            case TDelete:
                if (!sel) break;
                if (hosts_.remove) hosts_.remove(sel);
                else (void)deleteFunction(sel);
                break;
            case TTry:
                if (!sel) break;
                if (hosts_.tryIt) hosts_.tryIt(sel);
                else (void)tryFunction(sel, {});
                break;
            case TCompile: (void)compileCurrent(); break;   // 1.11.17 : la fonction actuelle (ici, et son build)
            case TBuildGen: if (hosts_.build && selectedFunction() != kNoId) hosts_.build(hmi::pipeline::Mode::Generate, buildKey()); break;
            case TBuildRegen: if (hosts_.build && selectedFunction() != kNoId) hosts_.build(hmi::pipeline::Mode::Regenerate, buildKey()); break;
            case TBuildGenComp: if (hosts_.build && selectedFunction() != kNoId) hosts_.build(hmi::pipeline::Mode::GenerateCompile, buildKey()); break;
            case TBuildState: if (hosts_.buildOutputs) hosts_.buildOutputs(); break;
            default: break;
        }
    });
    links_ += functions_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        selectedRow_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        showSelected();
    });
    links_ += functions_->activated->connect([this](ui::RowIndex) {
        if (selectedFunction() && hosts_.tryIt) hosts_.tryIt(selectedFunction());
    });
    // 1.11.21 : la barre du volet (ses fautes comptees) : un clic ouvre le panneau du bas, ou elles sont listees.
    status_->setMessageClick([this] { if (hosts_.showDiagnostics) hosts_.showDiagnostics(); });
    links_ += editor_->caretSymbolChanged->connect([this](const std::string&) { updateSymbolLine(); });
    links_ += editor_->textChanged->connect([this](const std::string& text) {
        if (syncing_) return;
        if (const Id sel = selectedFunction()) (void)setBody(sel, text);
        updateDiagnostics();
    });
    // 1.11.18 (lot 5) : les grilles parlent dans la barre du volet ; leurs utilisations menent au code.
    links_ += codeTabs_->message->connect([this](const std::string& t, bool warning) { say(t, warning); });
    links_ += codeTabs_->usesRequested->connect([this](const std::string& name) { (void)goToNextUse(name); });
    links_ += codeTabs_->migrateRequested->connect([this] { (void)migrateCurrent(); });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
    if (!order_.empty()) {
        hmiSelectModelRow(*functions_, 0);
        selectedRow_ = 0;
        showSelected();
    }
}

Id HmiFunctionsPane::selectedFunction() const {
    if (selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= order_.size()) return kNoId;
    return order_[static_cast<std::size_t>(selectedRow_)];
}

const hmi::HmiFunction* HmiFunctionsPane::current() const { return fnOf(doc_->project, selectedFunction()); }

// ---- 1.11.10 : les fonctions d'un symbole -------------------------------------------
std::vector<hmi::HmiFunction>* HmiFunctionsPane::listOf(hmi::Project& p) const {
    if (symbol_ == kNoId) return &p.programs.functions;
    auto* v = p.view(symbol_);
    return v ? &v->functions : nullptr;
}
const std::vector<hmi::HmiFunction>* HmiFunctionsPane::listOf(const hmi::Project& p) const {
    if (symbol_ == kNoId) return &p.programs.functions;
    const auto* v = p.view(symbol_);
    return v ? &v->functions : nullptr;
}
hmi::HmiFunction* HmiFunctionsPane::fnOf(hmi::Project& p, Id id) const {
    if (auto* l = listOf(p))
        for (auto& f : *l)
            if (f.id == id && id) return &f;
    return nullptr;
}
const hmi::HmiFunction* HmiFunctionsPane::fnOf(const hmi::Project& p, Id id) const {
    if (const auto* l = listOf(p))
        for (const auto& f : *l)
            if (f.id == id && id) return &f;
    return nullptr;
}
const hmi::View* HmiFunctionsPane::symbolView() const { return symbol_ != kNoId ? doc_->project.view(symbol_) : nullptr; }

void HmiFunctionsPane::setSymbol(Id symbol) {
    symbol_ = symbol;
    if (auto* panel = dynamic_cast<HmiTitledPanel*>(findById(id() + ".functionsPanel")))
        panel->setTitle(symbolView() ? "FONCTIONS DE " + symbolView()->name : std::string("FONCTIONS IHM"));
    selectedRow_ = -1;
    refresh();
}

bool HmiFunctionsPane::setVirtual(Id id, bool on) {
    const auto* f = fnOf(doc_->project, id);
    if (!f || symbol_ == kNoId) return false;
    if (f->isVirtual == on) return true;
    if (on && named(f->name).size() > 1) {          // 1.11.20 : ses redefinitions la designent par son nom
        say("Virtuelle refus\xC3\xA9" "e : " + f->name + " a des surcharges - une fonction virtuelle ne se surcharge pas "
            "(ses red\xC3\xA9" "finitions la d\xC3\xA9signent par son nom)", true);
        return false;
    }
    const std::string fname = f->name;   // copie : la commande remplace les vues
    auto cmd = hmi::changeProject(doc_, std::string(on ? "Rendre virtuelle " : "Rendre non virtuelle ") + fname, [&](hmi::Project& p) {
        if (auto* g = fnOf(p, id)) g->isVirtual = on;
    });
    if (cmd) apply_(std::move(cmd));
    say(std::string(on ? "Virtuelle : une instance peut red\xC3\xA9" "finir " : "Non virtuelle : toutes les instances gardent ") + fname
        + (on ? std::string(" (inspecteur de l'instance, Fonctions du symbole).") : std::string(" du symbole.")));
    return true;
}

void HmiFunctionsPane::selectFunction(Id id) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == id && id) {
            hmiSelectModelRow(*functions_, i);
            selectedRow_ = static_cast<int>(i);
            showSelected();
            return;
        }
}

void HmiFunctionsPane::goTo(Id function, int line) {
    selectFunction(function);
    if (line > 0) {
        showCodeTab(CodeTabCode);             // 1.11.18 (lot 5) : le code, pas une grille
        editor_->goToLine(static_cast<std::size_t>(line - 1));
    }
}

// 1.10 : un constat de Compiler a sa colonne : les caracteres de la faute selectionnes.
void HmiFunctionsPane::goTo(Id function, int line, int column, int length) {
    if (column <= 0) { goTo(function, line); return; }
    selectFunction(function);
    if (line <= 0) return;
    showCodeTab(CodeTabCode);                 // 1.11.18 (lot 5)
    editor_->selectRange(static_cast<std::size_t>(line - 1), static_cast<std::uint32_t>(column - 1),
                         static_cast<std::uint32_t>(std::max(0, length)));
}

void HmiFunctionsPane::refresh() {
    const Id keep = selectedFunction();
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> states;
    order_.clear();
    std::size_t withReturn = 0;
    static const std::vector<hmi::HmiFunction> kNone;
    const auto* list = listOf(doc_->project);
    for (const auto& f : list ? *list : kNone) {
        order_.push_back(f.id);
        withReturn += !f.returnType.empty();
        states.push_back(stateOf(hmi::checkFunction(f, knownTypeOf(doc_->project))));   // 1.10 : types IHM
        rows.push_back({f.name, f.returnType.empty() ? std::string("-") : f.returnType, parameterList(f),
                        std::to_string(lineCount(f.body)), states.back(), f.description});
    }
    functionModel_ = std::make_shared<Rows>(
        std::vector<std::string>{"Nom", "Retour", "Param\xC3\xA8tres", "Lignes", "\xC3\x89tat", "Description"}, std::move(rows),
        [states, purple = symbol_ != kNoId](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (c == 0) s.icon = ui::Icon::FunctionBlock;
            if (c == 0 && purple) s.iconTone = ui::Tone::InOut;    // 1.11.10 : les fonctions d'un symbole, en violet
            if (c == 4 && r < states.size())
                s.fgTone = states[r] == "OK" ? ui::Tone::Ok
                         : states[r].find("erreur") != std::string::npos ? ui::Tone::Error : ui::Tone::Warning;
            return s;
        });
    functions_->setModel(functionModel_);
    selectedRow_ = -1;
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == keep && keep) { hmiSelectModelRow(*functions_, i); selectedRow_ = static_cast<int>(i); }
    // La fonction choisie a disparu (supprimee, Ctrl+Z) : la premiere, plutot
    // qu'un editeur vide.
    if (selectedRow_ < 0 && !order_.empty()) {
        hmiSelectModelRow(*functions_, 0);
        selectedRow_ = 0;
    }
    showSelected();
    const auto n = list ? list->size() : 0u;
    if (const auto* sv = symbolView())                                    // 1.11.10
        countsText_ = std::to_string(n) + " fonction(s) de " + sv->name + "  \xC2\xB7  appel : Nom() dans le symbole, "
                      "Instance.Nom() dans sa vue, Vue.Instance.Nom() partout (scripts g\xC3\xA9n\xC3\xA9raux, fonctions)";
    else
        countsText_ = std::to_string(n) + " fonction(s) IHM  \xC2\xB7  " + std::to_string(withReturn) + " avec retour, "
                    + std::to_string(n - withReturn) + " sans  \xC2\xB7  appel : Nom(a, b) dans un script, une action ou une expression de vue";
    showStatus();
    invalidate();
}

void HmiFunctionsPane::showSelected() {
    const auto* f = current();
    syncing_ = true;
    const std::string body = f ? f->body : std::string{};
    if (editor_->text() != body) editor_->setText(body);
    editor_->setReadOnly(f == nullptr);
    syncing_ = false;
    std::string title = "\xC3\x89" "DITEUR";
    if (f) title += "  \xC2\xB7  " + hmi::functionSignature(*f);
    editorPanel_->setTitle(title);
    rebuildProperties();
    updateDiagnostics();
    codeTabs_->setPlace(currentPlace(), "Choisis une fonction.");   // 1.11.18 (lot 5) : ses onglets
}

// ---- 1.11.18 (refonte des scripts, lot 5) : les onglets de la fonction ----
std::optional<hmi::decledit::Place> HmiFunctionsPane::currentPlace() const {
    const auto* f = current();
    if (!f) return std::nullopt;
    hmi::decledit::Place at;
    at.kind = symbol_ != kNoId ? hmi::decledit::Place::Kind::SymbolFunction : hmi::decledit::Place::Kind::Function;
    at.view = symbol_;
    at.id = f->id;
    const auto* sv = symbolView();
    at.label = "fonction " + (sv ? sv->name + "." : std::string{}) + f->name;
    return at;
}

void HmiFunctionsPane::showCodeTab(std::size_t tab) {
    if (codeTabs_ && tab < codeTabs_->tabCount()) codeTabs_->setCurrentIndex(tab);
}

std::size_t HmiFunctionsPane::currentCodeTab() const noexcept { return codeTabs_ ? codeTabs_->currentIndex() : 0; }

bool HmiFunctionsPane::migrateCurrent() {
    const auto at = currentPlace();
    if (!at) {
        say("Migrer : aucune fonction choisie.", true);
        return false;
    }
    std::string report;
    const bool ok = migrateOne(doc_, apply_, *at, &report);
    say(report, !ok);
    return ok;
}

bool HmiFunctionsPane::goToNextUse(const std::string& name) {
    showCodeTab(CodeTabCode);
    std::string said;
    const bool ok = selectNextUse(*editor_, name, &said);
    say(said, !ok);
    if (ok) updateSymbolLine();
    return ok;
}

bool HmiFunctionsPane::showDeclaration(const std::string& name) { return codeTabs_->showDeclaration(name); }

void HmiFunctionsPane::rebuildProperties() {
    using PG = ui::PropertyGrid;
    const auto* f = current();
    if (!f) { props_->setCategories({}); return; }
    const Id id = f->id;
    std::vector<std::string> types{kNoReturn};
    for (const auto& t : hmi::typereg::baseRegistry().names(hmi::typereg::UseDeclaration)) types.push_back(t);
    // 1.10 (S1) : les types IHM du projet (structures, enumerations) ; un retour riche
    // deja ecrit (ARRAY[0..9] OF REAL, MAP[STRING] OF REAL...) reste dans la liste.
    for (const auto& t : doc_->project.programs.types) types.push_back(t.name);
    if (!f->returnType.empty() && std::find(types.begin(), types.end(), f->returnType) == types.end())
        types.push_back(f->returnType);
    if (typepicker::available()) types.push_back(typepicker::kChoose);   // 1.11.19 (lot 6) : le selecteur de types
    PG::Category c;
    c.name = "Fonction";
    c.properties.push_back(hmikit::prop("Nom", f->name, PG::ValueType::Text,
        [this, id](std::string_view v) { return renameFunction(id, std::string(v)); },
        "Renommer la fonction renomme aussi ses appels, partout dans le projet."));
    c.properties.push_back(hmikit::prop("Type de retour", f->returnType.empty() ? std::string(kNoReturn) : f->returnType,
        PG::ValueType::Enum, [this, id](std::string_view v) { return setReturnType(id, std::string(v)); },
        "Aucun : une proc\xC3\xA9" "dure, appel\xC3\xA9" "e seule sur sa ligne. Sinon le corps affecte le r\xC3\xA9sultat "
        "\xC3\xA0 son nom : Moyenne := ... ;", types));
    c.properties.push_back(hmikit::prop("Description", f->description, PG::ValueType::Text,
        [this, id](std::string_view v) { return setDescription(id, std::string(v)); }));
    c.properties.push_back(hmikit::prop("Signature", hmi::functionSignature(*f), PG::ValueType::ReadOnly));
    const auto parts = hmi::splitDeclarations(hmi::decl::codeOf(*f), true);   // 1.11.18 (lot 3) : ses declarations du modele aussi
    std::size_t kept = 0, temps = 0;
    for (const auto& l : parts.locals) {
        kept += l.section == hmi::LocalVar::Section::Var;
        temps += l.section == hmi::LocalVar::Section::Temp;
    }
    // 1.11.18 (lot 5) : ses onglets les montrent (ceux d'un ancien bloc VAR du code aussi comptes).
    c.properties.push_back(hmikit::prop("Param\xC3\xA8tres", std::to_string(parts.parameters().size()), PG::ValueType::ReadOnly,   // 1.11.20 : E/S, sorties
                                        nullptr, "Les param\xC3\xA8tres de la fonction : l'onglet Param\xC3\xA8tres (l'ordre est la signature)."));
    c.properties.push_back(hmikit::prop("Locales", std::to_string(kept + temps), PG::ValueType::ReadOnly, nullptr,
                                        "Ses variables locales (onglet Locales) : elles repartent \xC3\xA0 chaque appel."));
    if (const auto* sv = symbolView()) {
        // 1.11.10 : virtuelle, et qui la redefinit.
        c.properties.push_back(hmikit::prop("Virtuelle", f->isVirtual ? "TRUE" : "FALSE", PG::ValueType::Boolean,
            [this, id](std::string_view v) { return setVirtual(id, v == "TRUE" || v == "true" || v == "1"); },
            "Coch\xC3\xA9" "e : une instance peut red\xC3\xA9" "finir le corps (inspecteur de l'instance, Fonctions du symbole) ; "
            "la signature reste celle du symbole. SUPER." + f->name + "(...) y rappelle le corps du symbole."));
        std::vector<std::string> over;
        for (const auto& [owner, inst] : hmi::instancesOf(doc_->project, sv->name))
            if (hmi::functionOverride(*inst, f->name)) over.push_back(owner->name + "." + inst->name);
        c.properties.push_back(hmikit::prop("Red\xC3\xA9" "finie par",
            over.empty() ? std::string("(aucune instance)") : std::to_string(over.size()) + " : " + hmikit::fewOf(over, 3),
            PG::ValueType::ReadOnly));
        c.properties.push_back(hmikit::prop("Appel",
            f->name + "() dans " + sv->name + " ; Instance." + f->name + "() dans sa vue ; Vue.Instance." + f->name + "() partout",
            PG::ValueType::ReadOnly));
    } else {
    const auto callers = hmi::functionCallers(doc_->project, f->name);
    c.properties.push_back(hmikit::prop("Appel\xC3\xA9" "e par", callers.empty() ? std::string("(personne)") : hmikit::fewOf(callers, 3),
                                        PG::ValueType::ReadOnly));
    }
    c.properties.push_back(hmikit::prop("Lignes", std::to_string(lineCount(f->body)), PG::ValueType::ReadOnly));
    props_->setCategories({std::move(c)});
}

// 1.11.21 : l'essai aux Sorties du panneau du bas (plus de bandeau ESSAI) - l'appel et son
// resultat, les parametres rendus (E/S, sorties), le journal, les variables IHM changees, l'automate.
std::vector<std::pair<hmi::pipeline::Severity, std::string>> HmiFunctionsPane::trialLines() const {
    using S = hmi::pipeline::Severity;
    std::vector<std::pair<S, std::string>> out;
    if (trial_.function == kNoId) return out;
    if (!trial_.ok) out.emplace_back(S::Error, "Essai de " + trial_.call + " : \xC3\xA9" "chou\xC3\xA9 - " + trial_.error);
    else if (trial_.result.empty()) out.emplace_back(S::Success, "Essai de " + trial_.call + " : r\xC3\xA9ussi (sans retour)");
    else out.emplace_back(S::Success, "Essai de " + trial_.call + " = " + trial_.result + "   (" + trial_.type + ")");
    for (const auto& o : trial_.outputs) out.emplace_back(S::Information, "Param\xC3\xA8tre rendu : " + o);
    for (const auto& j : trial_.journal) out.emplace_back(S::Information, "Journal : " + j);
    for (const auto& v : trial_.changed) out.emplace_back(S::Warning, "Variable IHM : " + v);
    out.emplace_back(S::Information, trial_.livePlc ? std::string("Automate : lu dans la simulation en marche (sans y \xC3\xA9" "crire)")
                                                    : std::string("Automate : simulation arr\xC3\xAAt\xC3\xA9" "e, ses variables ne sont pas lues"));
    return out;
}

void HmiFunctionsPane::updateDiagnostics() {
    diagnostics_.clear();
    const auto* f = current();
    if (f) {
        // 1.10.4 : rien ne sort d'un diagnostic (le plantage de la 1.10.3) - l'exception
        // va au journal interne et la ligne du curseur dit "diagnostic indisponible".
        const int line = editor_ ? static_cast<int>(editor_->caretLine()) + 1 : 1;
        diagnostics_ = guardedDiagnostics(
            [&] {
                auto all = hmi::checkFunction(*f, knownTypeOf(doc_->project));               // 1.10 : types IHM
                // 1.10 : les fautes a leur place, sans attendre Compiler (les memes) :
                // un nom qui ne se resout pas (ni entree, ni locale, ni variable IHM, ni
                // variable de l'automate - avec le nom proche), un membre, un appel, une
                // ecriture interdite, un type. Sans programme de l'automate branche, un
                // nom inconnu de l'IHM peut etre a lui : rien n'est dit.
                const auto plc = assist_.plc ? assist_.plc() : nullptr;
                // 1.11.10 : une fonction de symbole lit les parametres et appelle les fonctions du symbole.
                for (auto& d : placedScriptDiagnostics(doc_->project, f->body, symbolView(), f, plc.get(), nullptr, &f->decls))
                    all.push_back(std::move(d));                                   // 1.11.18 : ses declarations du modele
                // 1.11.20 : une autre fonction du meme nom et de la meme forme (une surcharge mal
                // distinguee), une virtuelle surchargee : dit ici, comme Compiler le dit.
                const auto* sv = symbolView();
                for (const auto& c : hmi::overload::clashes(sv ? sv->functions : doc_->project.programs.functions))
                    if (c.function == f->id) all.push_back({hmi::ScriptDiagnostic::Severity::Error, 0, c.message});
                return all;
            },
            line, f->name);
        std::stable_sort(diagnostics_.begin(), diagnostics_.end(), [](const hmi::ScriptDiagnostic& a, const hmi::ScriptDiagnostic& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
    }
    ++liveRev_;                                   // 1.11.21 : le panneau du bas les relira
    std::vector<std::pair<std::size_t, gfx::Color>> marks;
    for (const auto& d : diagnostics_) {
        if (d.line > 0 && d.column <= 0 && d.severity != hmi::ScriptDiagnostic::Severity::Info)
            marks.emplace_back(static_cast<std::size_t>(d.line - 1),
                               d.severity == hmi::ScriptDiagnostic::Severity::Error ? gfx::Color{231, 76, 60, 255}
                                                                                    : gfx::Color{241, 196, 15, 255});
    }
    editor_->setMarkedLines(std::move(marks));
    editor_->setSquiggles(squigglesOf(diagnostics_));      // 1.10 : soulignees pendant la frappe
    showStatus();
}

// 1.11.21 : la barre - le compte du volet, puis les fautes de la fonction montree (le panneau du
// bas les liste : un clic sur la barre l'ouvre).
void HmiFunctionsPane::showStatus() {
    if (!status_) return;
    std::size_t errors = 0, warnings = 0;
    for (const auto& d : diagnostics_) {
        errors += d.severity == hmi::ScriptDiagnostic::Severity::Error;
        warnings += d.severity == hmi::ScriptDiagnostic::Severity::Warning;
    }
    const auto* f = current();
    if (!f || (!errors && !warnings)) {
        status_->setMessage(countsText_, ui::StatusBar::Severity::Info);
        return;
    }
    status_->setMessage(f->name + " : " + (errors ? std::to_string(errors) + " erreur(s)" : std::string{}) + (errors && warnings ? ", " : "")
                            + (warnings ? std::to_string(warnings) + " avertissement(s)" : std::string{})
                            + ", soulign\xC3\xA9" "(e)s pendant que tu tapes \xE2\x80\x94 la liste : panneau du bas, Diagnostics (clic ici)",
                        ui::StatusBar::Severity::Warning);
}

// ---- 1.11.21 : les diagnostics de la fonction montree, au panneau du bas (HmiLive.hpp) ----
std::vector<hmi::pipeline::Diagnostic> HmiFunctionsPane::liveDiagnostics() const {
    std::vector<hmi::pipeline::Diagnostic> out;
    const auto* f = current();
    if (!f) return out;
    const auto* sv = symbolView();
    const std::string path = sv ? sv->name + " \xC2\xB7 " + f->name : f->name;
    for (const auto& d : diagnostics_) {
        auto x = liveDiagnostic(d, buildKey(), path, "Fonction");
        x.item = f->id;
        x.view = sv ? sv->id : hmi::kNoId;
        x.property = f->name;
        out.push_back(std::move(x));
    }
    return out;
}

void HmiFunctionsPane::goToLive(const hmi::pipeline::Diagnostic& d) {
    if (d.item != hmi::kNoId && d.item != selectedFunction()) selectFunction(d.item);
    if (d.line > 0) showCodeTab(CodeTabCode);
    if (d.line > 0 && d.column > 0)
        editor_->selectRange(static_cast<std::size_t>(d.line - 1), static_cast<std::uint32_t>(d.column - 1),
                             static_cast<std::uint32_t>(std::max(0, d.length)));
    else if (d.line > 0)
        editor_->goToLine(static_cast<std::size_t>(d.line - 1));
    else if (const auto name = declarationNamed(d.message); !name.empty())
        (void)showDeclaration(name);       // la faute d'une declaration, dans son onglet
    updateSymbolLine();
}

void HmiFunctionsPane::setAssist(std::function<std::shared_ptr<const domain::Project>()> plc,
                                 std::function<bool(std::string_view, std::string&)> live) {
    assist_.hmi = [doc = doc_]() -> const hmi::Project* { return doc ? &doc->project : nullptr; };
    assist_.plc = std::move(plc);
    assist_.live = std::move(live);
    assist_.declarations = [this] {                                        // 1.11.18 (lot 3) : ses parametres et locales du modele
        const auto* f = current();
        return f ? assist::declarationsPrefix(f->decls, hmi::decl::Role::Function) : std::string{};
    };
    assist::attach(*editor_, assist_);
    updateDiagnostics();
    updateSymbolLine();
}

void HmiFunctionsPane::updateSymbolLine() {
    if (!symbolBar_) return;
    const auto symbol = editor_->symbolAtCaret();
    if (!assist_.hmi || symbol.empty()) { symbolBar_->setMessage({}); return; }
    const std::string text = editor_->text();
    const auto at = std::min(editor_->caretOffset(), text.size());
    std::size_t start = at;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(text[start - 1])) || text[start - 1] == '_'
                         || text[start - 1] == '.' || text[start - 1] == '[' || text[start - 1] == ']'))
        --start;
    const auto where = assist::locate(std::string_view(text).substr(0, start));
    const auto* hp = assist_.hmi();
    if (!hp) return;
    const auto* f = current();
    // Le nom de la fonction dans son propre corps : le resultat qu'on affecte.
    if (f && upperOf(symbol) == upperOf(f->name) && !f->returnType.empty()) {
        symbolBar_->setMessage(f->name + " : " + f->returnType + "   le r\xC3\xA9sultat de la fonction (" + f->name + " := ...;)",
                               gfx::Color{0, 0, 0, 0});
        return;
    }
    if (where.context != assist::Context::Code && where.context != assist::Context::Placeholder
        && where.context != assist::Context::Member) {
        // Dans un bloc de declaration : la locale qu'on y declare.
        const auto parts = hmi::splitDeclarations(text, true);
        if (const auto* l = parts.local(symbol)) {
            const auto d = assist::describe(*hp, nullptr, symbol, text);
            symbolBar_->setMessage(d.line, gfx::Color{0, 0, 0, 0});
            (void)l;
            return;
        }
        symbolBar_->setMessage({});
        return;
    }
    const auto plc = assist_.plc ? assist_.plc() : nullptr;
    const auto d = assist::describe(*hp, plc.get(), symbol, (assist_.declarations ? assist_.declarations() : std::string{}) + text);
    if (d.keyword || (!d.found && editor_->completionOpen())) { symbolBar_->setMessage({}); return; }
    gfx::Color accent{0, 0, 0, 0};
    if (!d.found) accent = gfx::Color{232, 196, 111, 255};
    symbolBar_->setMessage(d.line, accent);
}

void HmiFunctionsPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

std::vector<const hmi::HmiFunction*> HmiFunctionsPane::named(std::string_view name) const {
    if (const auto* sv = symbolView()) return hmi::symbolFunctions(*sv, name);
    return doc_->project.functionsNamed(name);
}

bool HmiFunctionsPane::nameAllowed(const std::string& name, Id self, std::string* why, bool overload) const {
    auto refuse = [&](std::string w) {
        if (why) *why = w;
        return false;
    };
    if (!hmi::isIdentifier(name)) return refuse("nom invalide : lettres, chiffres et _ (pas de chiffre en t\xC3\xAAte)");
    const auto u = upperOf(name);
    if (u.rfind("IHM_", 0) == 0) return refuse("le pr\xC3\xA9" "fixe IHM_ est r\xC3\xA9serv\xC3\xA9 aux fonctions de l'IHM");
    static const char* kReserved[] = {"IF", "THEN", "ELSE", "ELSIF", "END_IF", "FOR", "TO", "BY", "DO", "END_FOR", "WHILE",
                                      "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT", "CASE", "OF", "END_CASE", "EXIT",
                                      "RETURN", "AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "VAR", "VAR_TEMP",
                                      "VAR_INPUT", "END_VAR", "FUNCTION", "END_FUNCTION"};
    for (const char* k : kReserved) if (u == k) return refuse("'" + name + "' est un mot du langage ST");
    if (hmi::localTypeSupported(u) || hmi::isStandardFunction(u)) return refuse("'" + name + "' est un type ou une fonction standard");
    // 1.11.20 : plusieurs fonctions du meme nom (des surcharges) - a la creation (sa forme differera,
    // sinon Compiler le dit) ; pas en renommant (ses appels pourraient changer de cible) ; jamais une
    // fonction virtuelle (ses redefinitions la designent par son nom).
    std::string selfName;
    if (const auto* me = fnOf(doc_->project, self)) selfName = me->name;
    for (const auto* other : named(name)) {
        if (other->id == self || (!selfName.empty() && hmikit::same(other->name, selfName))) continue;   // elle-meme, ses surcharges
        if (other->isVirtual)
            return refuse(name + " est virtuelle : une fonction virtuelle ne se surcharge pas (ses red\xC3\xA9" "finitions la d\xC3\xA9signent par son nom)");
        if (!overload)
            return refuse("une fonction porte d\xC3\xA9j\xC3\xA0 ce nom : la renommer ainsi en ferait une surcharge de " + name
                          + ", et ses appels pourraient changer de cible (cr\xC3\xA9" "ez plut\xC3\xB4t la surcharge : Nouvelle fonction " + name + ")");
    }
    if (const auto* sv = symbolView()) {
        if (sv->param(name)) return refuse("un param\xC3\xA8tre de " + sv->name + " porte d\xC3\xA9j\xC3\xA0 ce nom");
        if (u == hmi::kSuperName) return refuse("SUPER est r\xC3\xA9serv\xC3\xA9 (SUPER.Nom() rappelle le corps du symbole)");
        return true;
    }
    if (doc_->project.variable(name)) return refuse("une variable IHM porte d\xC3\xA9j\xC3\xA0 ce nom");
    return true;
}

Id HmiFunctionsPane::addFunction(std::string name, std::string returnType, std::string description, std::string* why) {
    if (isNoReturn(returnType)) returnType.clear();
    std::string reason;
    if (!nameAllowed(name, kNoId, &reason, /*overload*/ true)) {
        if (why) *why = reason;
        say("Fonction refus\xC3\xA9" "e : " + reason, true);
        return kNoId;
    }
    const auto siblings = named(name);          // 1.11.20 : des surcharges de ce nom
    if (!returnType.empty() && !returnTypeAllowed(doc_->project, returnType)) {
        if (why) *why = "type de retour non pris en charge : " + returnType;
        say("Fonction refus\xC3\xA9" "e : type de retour " + returnType, true);
        return kNoId;
    }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Nouvelle fonction " + name, [&](hmi::Project& p) {
        hmi::HmiFunction f;
        f.id = p.allocate();
        f.name = name;
        f.returnType = returnType;
        f.description = description;
        f.body = hmi::functionTemplate(name, returnType, description);
        f.decls = hmi::functionTemplateDecls(returnType);         // 1.11.18 (lot 5) : dans ses onglets, plus dans le code
        for (auto& d : f.decls) d.id = p.allocate();
        made = f.id;
        if (auto* l = listOf(p)) l->push_back(std::move(f));
        else made = kNoId;
    });
    if (cmd) apply_(std::move(cmd));
    if (made) {
        selectFunction(made);
        std::string overloadNote;
        if (!siblings.empty()) {
            // 1.11.20 : une surcharge - si sa forme est celle d'une autre, Compiler le dira : a changer.
            const auto* me = fnOf(doc_->project, made);
            const auto mine = me ? hmi::overload::signatureOf(*me) : hmi::overload::Signature{};
            std::string clash;
            for (const auto* o : named(name))
                if (o->id != made && hmi::overload::sameShape(hmi::overload::signatureOf(*o), mine)) clash = hmi::overload::signatureOf(*o).shape();
            overloadNote = clash.empty()
                               ? " Une surcharge : " + std::to_string(siblings.size() + 1) + " fonctions " + name + " (chaque appel prend la sienne)."
                               : " M\xC3\xAAme forme que " + clash + " : changez ses param\xC3\xA8tres (onglet Param\xC3\xA8tres) pour en faire une surcharge.";
        }
        say("Fonction " + name + " cr\xC3\xA9\xC3\xA9" "e : " + (returnType.empty() ? std::string("sans retour") : "retour " + returnType)
            + "." + overloadNote + " Ctrl+Z la retire.", !overloadNote.empty() && overloadNote.find("M\xC3\xAAme forme") != std::string::npos);
    }
    return made;
}

bool HmiFunctionsPane::renameFunction(Id id, const std::string& name, std::string* why) {
    const auto* f = fnOf(doc_->project, id);
    if (!f) return false;
    if (f->name == name) return true;
    std::string reason;
    if (!nameAllowed(name, id, &reason)) {
        if (why) *why = reason;
        say("Renommage refus\xC3\xA9 : " + reason, true);
        return false;
    }
    const std::string old = f->name;
    // 1.11.17 : un appel court qui changerait de cible (une fonction du symbole du meme nom) : refuse.
    if (const auto captured = hmi::renameCaptures(doc_->project, symbolView(), old, name); !captured.empty()) {
        reason = symbolView() ? "dans " + hmikit::fewOf(captured, 3) + ", " + name + "() vise d\xC3\xA9j\xC3\xA0 une fonction IHM : "
                                    "il viserait la fonction renomm\xC3\xA9" "e"
                              : "dans " + hmikit::fewOf(captured, 3) + ", " + old + "() viserait la fonction " + name
                                    + " du symbole, pas celle-ci";
        if (why) *why = reason;
        say("Renommage refus\xC3\xA9 : " + reason, true);
        return false;
    }
    // 1.11.20 : ses surcharges (les fonctions du meme nom) sont renommees avec elle - leurs appels
    // portent le meme nom et suivent tous : chacun garde sa cible.
    std::vector<Id> group;
    for (const auto* g : named(old)) group.push_back(g->id);
    std::size_t changed = 0;
    auto cmd = hmi::changeProject(doc_, "Renommer la fonction " + old + " en " + name, [&](hmi::Project& p) {
        // 1.11.10 : une fonction de symbole - ses appels (dans le symbole, Instance.Nom, Vue.Instance.Nom)
        // et les redefinitions des instances suivent.
        if (const auto* sv = symbolView()) changed = hmi::renameSymbolFunction(p, sv->name, old, name);
        else changed = hmi::renameFunctionEverywhere(p, old, name);
        for (const Id g : group)
            if (auto* fn = fnOf(p, g)) fn->name = name;
        if (auto* fn = fnOf(p, id)) fn->name = name;
    });
    if (cmd) apply_(std::move(cmd));
    say(old + " devient " + name
        + (group.size() == 2 ? std::string(" avec son autre surcharge")
           : group.size() > 2 ? " avec ses " + std::to_string(group.size() - 1) + " autres surcharges" : std::string{})
        + (changed ? " : " + std::to_string(changed) + " texte(s) suivent (appels, corps)" : std::string{}) + ". Ctrl+Z reprend tout.");
    return true;
}

bool HmiFunctionsPane::setReturnType(Id id, const std::string& type, std::string* why) {
    const auto* f = fnOf(doc_->project, id);
    if (!f) return false;
    // 1.11.19 (lot 6) : "Choisir un type..." - le selecteur (les types d'un retour, Aucun compris).
    if (type == typepicker::kChoose) {
        HmiTypePicker::Spec spec;
        spec.field = "Retour de " + f->name;
        spec.current = f->returnType.empty() ? std::string("Aucun") : f->returnType;
        spec.use = hmi::typereg::UseReturn;
        spec.doc = doc_;
        const std::weak_ptr<int> alive = alive_;
        typepicker::ask(std::move(spec), [this, id, alive](const HmiTypePicker::Answer& a) {
            if (alive.expired() || a.type.empty()) return;
            std::string w;
            if (!setReturnType(id, a.type, &w)) say("Retour refus\xC3\xA9 : " + w, true);
            rebuildProperties();
        });
        return false;
    }
    const std::string t = isNoReturn(type) ? std::string{} : type;
    if (!t.empty() && !returnTypeAllowed(doc_->project, t)) {
        if (why) *why = "type de retour non pris en charge : " + t;
        return false;
    }
    if (t == f->returnType) return true;
    auto cmd = hmi::changeProject(doc_, "Retour de " + f->name, [&](hmi::Project& p) {
        if (auto* g = fnOf(p, id)) g->returnType = t;
    });
    if (cmd) apply_(std::move(cmd));
    return true;
}

bool HmiFunctionsPane::setDescription(Id id, const std::string& description) {
    const auto* f = fnOf(doc_->project, id);
    if (!f || f->description == description) return f != nullptr;
    auto cmd = hmi::changeProject(doc_, "Description de " + f->name, [&](hmi::Project& p) {
        if (auto* g = fnOf(p, id)) g->description = description;
    });
    if (cmd) apply_(std::move(cmd));
    return true;
}

bool HmiFunctionsPane::setBody(Id id, const std::string& body) {
    const auto* f = fnOf(doc_->project, id);
    if (!f || f->body == body) return false;
    auto cmd = hmi::changeProject(doc_, "Saisie dans " + f->name, [&](hmi::Project& p) {
        if (auto* g = fnOf(p, id)) g->body = body;
    }, "function:" + std::to_string(id));
    if (cmd) apply_(std::move(cmd));
    return true;
}

bool HmiFunctionsPane::deleteFunction(Id id) {
    const auto* f = fnOf(doc_->project, id);
    if (!f) return false;
    const std::string name = f->name;
    auto cmd = hmi::changeProject(doc_, "Supprimer la fonction " + name, [&](hmi::Project& p) {
        if (!listOf(p)) return;
        auto& list = *listOf(p);
        list.erase(std::remove_if(list.begin(), list.end(), [&](const hmi::HmiFunction& g) { return g.id == id; }), list.end());
    });
    if (cmd) apply_(std::move(cmd));
    if (trial_.function == id) trial_ = {};
    say("Fonction " + name + " supprim\xC3\xA9" "e. Ctrl+Z la rend.");
    return true;
}

bool HmiFunctionsPane::tryFunction(Id id, const std::vector<std::string>& arguments) {
    const auto* f = fnOf(doc_->project, id);
    if (!f) return false;
    trial_ = {};
    trial_.function = id;
    auto* live = hosts_.plc ? hosts_.plc() : nullptr;
    trial_.livePlc = live != nullptr;
    TrialPlc plc(live);
    hmi::Runtime rt;
    rt.bind(&doc_->project, &plc);
    rt.prime(0.0);
    std::vector<std::pair<std::string, std::string>> before;
    for (const auto& v : doc_->project.programs.variables)
        if (const auto* val = rt.variable(v.name)) before.emplace_back(v.name, hmi::formatValue(*val));

    const auto parts = hmi::splitDeclarations(hmi::decl::codeOf(*f), true);   // 1.11.18 (lot 3) : ses parametres du modele aussi
    // 1.11.20 : tous ses parametres, dans l'ordre : une E/S recoit sa valeur de depart, une sortie
    // n'en demande pas (sa case est ignoree) ; leur valeur finale est montree apres l'essai.
    const auto inputs = parts.parameters();
    std::vector<std::pair<std::string, sim::Value>> args;
    std::string shown;
    bool bad = false;
    for (std::size_t k = 0; k < arguments.size(); ++k) {
        const std::string text = hmikit::trimmed(arguments[k]);
        if (text.empty()) continue;
        if (k < inputs.size() && inputs[k]->section == hmi::LocalVar::Section::Output) continue;   // une sortie : rien a donner
        if (k >= inputs.size()) {
            trial_.error = "trop d'arguments (" + std::to_string(arguments.size()) + " pour " + std::to_string(inputs.size()) + ")";
            bad = true;
            break;
        }
        const auto e = hmi::Expression::compile(text);
        if (!e.valid()) {
            std::string rest;
            (void)hmi::splitLine(e.error(), &rest);
            trial_.error = inputs[k]->name + " : " + text + " illisible (" + hmi::frenchSimMessage(rest.empty() ? e.error() : rest) + ")";
            bad = true;
            break;
        }
        auto v = e.evaluate(rt.environment());
        if (!v) {
            trial_.error = inputs[k]->name + " : " + text + " ne s'\xC3\xA9value pas (" + hmi::frenchSimMessage(v.error().message()) + ")";
            bad = true;
            break;
        }
        args.emplace_back(inputs[k]->name, *v);
        shown += (shown.empty() ? "" : ", ") + inputs[k]->name + " := " + text;
    }
    trial_.call = f->name + "(" + shown + ")";
    if (!bad) {
        sim::Value result;
        std::string why;
        std::vector<std::pair<std::string, sim::Value>> outputs;
        trial_.ok = rt.runFunction(f->id, args, result, &why, &outputs);     // 1.11.20 : par son identifiant (surcharges)
        if (!trial_.ok) trial_.error = why;
        else if (!f->returnType.empty()) {
            trial_.result = trialText(result);
            trial_.type = f->returnType;
        }
        if (trial_.ok)
            for (const auto& [name, value] : outputs) {
                const hmi::LocalVar* p = parts.local(name);
                const bool inOut = p && p->section == hmi::LocalVar::Section::InOut;
                trial_.outputs.push_back(name + (inOut ? " (E/S)" : " (sortie)") + " = " + trialText(value));
            }
        for (const auto& e : rt.journal())
            if (e.kind != "Syst\xC3\xA8me" && e.kind != "Erreur")
                trial_.journal.push_back(e.kind == "Journal" ? e.message : e.kind + " : " + e.message);
        for (const auto& [name, was] : before)
            if (const auto* now = rt.variable(name); now && hmi::formatValue(*now) != was)
                trial_.changed.push_back(name + " : " + was + "  \xE2\x86\x92  " + hmi::formatValue(*now));
    }
    if (hosts_.trialOutput) hosts_.trialOutput(trialLines());      // 1.11.21 : aux Sorties du panneau du bas
    if (trial_.ok)
        say("Essai : " + trial_.call + (trial_.result.empty() ? std::string(" (sans retour)") : " = " + trial_.result));
    else
        say("Essai : " + trial_.call + " \xC3\xA9" "choue : " + trial_.error, true);
    return trial_.ok;
}

void HmiFunctionsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
}

void HmiFunctionsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
