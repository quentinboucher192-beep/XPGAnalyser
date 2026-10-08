#include "HmiFunctionPanes.hpp"

#include "HmiAssetPanes.hpp"
#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "HmiScriptPanes.hpp"    // 1.10 : placedScriptDiagnostics, squigglesOf
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiScriptCheck110.hpp"   // 1.10 (S1) : un type de retour riche (structure, enumeration, ARRAY, MAP...)
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiSymbols.hpp"   // 1.11.10 : les fonctions des symboles
#include "../../sim/Runtime.hpp"
#include "../../ui/Theme.hpp"

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
        children()[0]->setBounds({b.x, b.y, b.w, std::max(0.f, b.h - kBar - 2.f)});
        children()[1]->setBounds({b.x, b.bottom() - kBar, b.w, kBar});
    }
};

enum ToolAction : int { TNew = 1, TDelete, TTry, TCompile, TExport, TImport,   // 1.11.2 : TExport, TImport (decision 174)
                        TBuildGen, TBuildRegen, TBuildGenComp, TBuildState };   // 1.11.13 : la generation incrementale

constexpr const char* kNoReturn = "(aucun)";

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
    const auto parts = hmi::splitDeclarations(f.body, true);   // les pointeurs de inputs() vivent avec lui
    for (const auto* in : parts.inputs())
        out += (out.empty() ? "" : ", ") + in->name + " : " + in->type;
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
    tools->add(TCompile, HmiGlyph::Code, "Compiler (F7) : toutes les fonctions, scripts, expressions et actions", "Compiler (F7)");
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
        left->addPane(std::move(panel), 0.36f, 100.f);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".propsPanel", "PROPRI\xC3\x89T\xC3\x89S DE LA FONCTION");
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
        grid->setShowDescriptionPane(false);
        grid->setNameColumnRatio(0.36f);
        props_ = &static_cast<ui::PropertyGrid&>(panel->setBody(std::move(grid)));
        left->addPane(std::move(panel), 0.34f, 90.f);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".trialPanel", "ESSAI");
        auto table = std::make_unique<ui::TableView>(base + ".trial");
        table->setColumns({{"\xC3\x89l\xC3\xA9ment", 150.f}, {"Valeur", 420.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        trialTable_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        left->addPane(std::move(panel), 0.30f, 80.f);
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
        panel->setBody(std::move(area));
        editorPanel_ = panel.get();
        right->addPane(std::move(panel), 0.74f, 120.f);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".diagPanel", "DIAGNOSTICS");
        auto table = std::make_unique<ui::TableView>(base + ".diagnostics");
        // 1.10 : la ligne et la colonne de chaque faute (comme le rapport de Compiler).
        table->setColumns({{"Ligne", 62.f, 40.f, true, true, true, ui::Align::End}, {"Col.", 56.f, 36.f, true, true, true, ui::Align::End},
                           {"Gravit\xC3\xA9", 120.f}, {"Message", 600.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        diagTable_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        right->addPane(std::move(panel), 0.26f, 70.f);
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
            case TCompile:
                updateDiagnostics();
                if (hosts_.build && selectedFunction() != kNoId) hosts_.build(hmi::pipeline::Mode::Compile, buildKey());   // 1.11.13 : son etat
                if (hosts_.compile) hosts_.compile();
                break;
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
    links_ += diagTable_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (rows.empty() || rows.front() >= diagnostics_.size()) return;
        const auto& d = diagnostics_[rows.front()];
        if (d.line > 0 && d.column > 0)        // 1.10 : la faute selectionnee
            editor_->selectRange(static_cast<std::size_t>(d.line - 1), static_cast<std::uint32_t>(d.column - 1),
                                 static_cast<std::uint32_t>(std::max(0, d.length)));
        else if (d.line > 0)
            editor_->goToLine(static_cast<std::size_t>(d.line - 1));
    });
    links_ += editor_->caretSymbolChanged->connect([this](const std::string&) { updateSymbolLine(); });
    links_ += editor_->textChanged->connect([this](const std::string& text) {
        if (syncing_) return;
        if (const Id sel = selectedFunction()) (void)setBody(sel, text);
        updateDiagnostics();
    });
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
        editor_->goToLine(static_cast<std::size_t>(line - 1));
        for (std::size_t i = 0; i < diagnostics_.size(); ++i)
            if (diagnostics_[i].line == line) { hmiSelectModelRow(*diagTable_, i); break; }
    }
}

// 1.10 : un constat de Compiler a sa colonne : les caracteres de la faute selectionnes.
void HmiFunctionsPane::goTo(Id function, int line, int column, int length) {
    if (column <= 0) { goTo(function, line); return; }
    selectFunction(function);
    if (line <= 0) return;
    for (std::size_t i = 0; i < diagnostics_.size(); ++i)
        if (diagnostics_[i].line == line && diagnostics_[i].column == column) { hmiSelectModelRow(*diagTable_, i); break; }
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
        status_->setMessage(std::to_string(n) + " fonction(s) de " + sv->name + "  \xC2\xB7  appel : Nom() dans le symbole, "
                            "Instance.Nom() dans sa vue, Vue.Instance.Nom() partout (scripts g\xC3\xA9n\xC3\xA9raux, fonctions)");
    else
    status_->setMessage(std::to_string(n) + " fonction(s) IHM  \xC2\xB7  " + std::to_string(withReturn) + " avec retour, "
                        + std::to_string(n - withReturn) + " sans  \xC2\xB7  appel : Nom(a, b) dans un script, "
                          "une action ou une expression de vue");
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
    rebuildTrial();
    updateDiagnostics();
}

void HmiFunctionsPane::rebuildProperties() {
    using PG = ui::PropertyGrid;
    const auto* f = current();
    if (!f) { props_->setCategories({}); return; }
    const Id id = f->id;
    std::vector<std::string> types{kNoReturn};
    for (const auto t : hmi::kLocalTypes) types.emplace_back(t);
    // 1.10 (S1) : les types IHM du projet (structures, enumerations) ; un retour riche
    // deja ecrit (ARRAY[0..9] OF REAL, MAP[STRING] OF REAL...) reste dans la liste.
    for (const auto& t : doc_->project.programs.types) types.push_back(t.name);
    if (!f->returnType.empty() && std::find(types.begin(), types.end(), f->returnType) == types.end())
        types.push_back(f->returnType);
    PG::Category c;
    c.name = "Fonction";
    c.properties.push_back(hmikit::prop("Nom", f->name, PG::ValueType::Text,
        [this, id](std::string_view v) { return renameFunction(id, std::string(v)); },
        "Renommer la fonction renomme aussi ses appels, partout dans le projet."));
    c.properties.push_back(hmikit::prop("Type de retour", f->returnType.empty() ? std::string(kNoReturn) : f->returnType,
        PG::ValueType::Enum, [this, id](std::string_view v) { return setReturnType(id, std::string(v)); },
        "(aucun) : une proc\xC3\xA9" "dure, appel\xC3\xA9" "e seule sur sa ligne. Sinon le corps affecte le r\xC3\xA9sultat "
        "\xC3\xA0 son nom : Moyenne := ... ;", types));
    c.properties.push_back(hmikit::prop("Description", f->description, PG::ValueType::Text,
        [this, id](std::string_view v) { return setDescription(id, std::string(v)); }));
    c.properties.push_back(hmikit::prop("Signature", hmi::functionSignature(*f), PG::ValueType::ReadOnly));
    const auto parts = hmi::splitDeclarations(f->body, true);
    std::size_t kept = 0, temps = 0;
    for (const auto& l : parts.locals) {
        kept += l.section == hmi::LocalVar::Section::Var;
        temps += l.section == hmi::LocalVar::Section::Temp;
    }
    c.properties.push_back(hmikit::prop("Param\xC3\xA8tres (VAR_INPUT)", std::to_string(parts.inputs().size()), PG::ValueType::ReadOnly));
    c.properties.push_back(hmikit::prop("Locales (VAR / VAR_TEMP)", std::to_string(kept) + " / " + std::to_string(temps),
                                        PG::ValueType::ReadOnly));
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

void HmiFunctionsPane::rebuildTrial() {
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::Tone> tones;
    const auto add = [&](std::string a, std::string b, ui::Tone t = ui::Tone::None) {
        rows.push_back({std::move(a), std::move(b)});
        tones.push_back(t);
    };
    if (trial_.function != kNoId && trial_.function == selectedFunction()) {
        add("Appel", trial_.call);
        if (!trial_.ok) add("Erreur", trial_.error, ui::Tone::Error);
        else if (trial_.result.empty()) add("R\xC3\xA9sultat", "(sans retour)", ui::Tone::Muted);
        else add("R\xC3\xA9sultat", trial_.result + "   (" + trial_.type + ")", ui::Tone::Ok);
        for (const auto& j : trial_.journal) add("Journal", j);
        for (const auto& v : trial_.changed) add("Variable IHM", v, ui::Tone::Warning);
        add("Automate", trial_.livePlc ? "lu dans la simulation en marche (sans y \xC3\xA9" "crire)"
                                       : "simulation arr\xC3\xAAt\xC3\xA9" "e : ses variables ne sont pas lues",
            ui::Tone::Muted);
    } else if (current()) {
        add("Essayer", "le bouton Essayer (ou double-clic sur la fonction) : ses arguments, puis le r\xC3\xA9sultat", ui::Tone::Muted);
    }
    trialModel_ = std::make_shared<Rows>(std::vector<std::string>{"\xC3\x89l\xC3\xA9ment", "Valeur"}, std::move(rows),
                                         [tones](ui::RowIndex r, std::size_t c) {
                                             ui::CellStyle s;
                                             if (c == 1 && r < tones.size()) s.fgTone = tones[r];
                                             return s;
                                         });
    trialTable_->setModel(trialModel_);
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
                for (auto& d : placedScriptDiagnostics(doc_->project, f->body, symbolView(), f, plc.get())) all.push_back(std::move(d));
                return all;
            },
            line, f->name);
        std::stable_sort(diagnostics_.begin(), diagnostics_.end(), [](const hmi::ScriptDiagnostic& a, const hmi::ScriptDiagnostic& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
    }
    std::vector<std::vector<std::string>> rows;
    std::vector<std::pair<std::size_t, gfx::Color>> marks;
    std::vector<hmi::ScriptDiagnostic::Severity> sev;
    for (const auto& d : diagnostics_) {
        const char* label = d.severity == hmi::ScriptDiagnostic::Severity::Error ? "Erreur"
                          : d.severity == hmi::ScriptDiagnostic::Severity::Warning ? "Avertissement" : "Information";
        rows.push_back({d.line ? std::to_string(d.line) : std::string("-"), d.column > 0 ? std::to_string(d.column) : std::string{}, label,
                        d.message});
        sev.push_back(d.severity);
        if (d.line > 0 && d.column <= 0 && d.severity != hmi::ScriptDiagnostic::Severity::Info)
            marks.emplace_back(static_cast<std::size_t>(d.line - 1),
                               d.severity == hmi::ScriptDiagnostic::Severity::Error ? gfx::Color{231, 76, 60, 255}
                                                                                    : gfx::Color{241, 196, 15, 255});
    }
    diagModel_ = std::make_shared<Rows>(std::vector<std::string>{"Ligne", "Col.", "Gravit\xC3\xA9", "Message"}, std::move(rows),
                                        [sev](ui::RowIndex r, std::size_t c) {
                                            ui::CellStyle s;
                                            if (c == 2 && r < sev.size())
                                                s.fgTone = sev[r] == hmi::ScriptDiagnostic::Severity::Error ? ui::Tone::Error
                                                         : sev[r] == hmi::ScriptDiagnostic::Severity::Warning ? ui::Tone::Warning
                                                                                                              : ui::Tone::Muted;
                                            return s;
                                        });
    diagTable_->setModel(diagModel_);
    editor_->setMarkedLines(std::move(marks));
    editor_->setSquiggles(squigglesOf(diagnostics_));      // 1.10 : soulignees pendant la frappe
    if (auto* panel = dynamic_cast<HmiTitledPanel*>(findById(id() + ".diagPanel")))   // 1.10 : le titre les compte
        panel->setTitle(f ? diagnosticsTitle(diagnostics_, "cette fonction") : std::string("DIAGNOSTICS"));
}

void HmiFunctionsPane::setAssist(std::function<std::shared_ptr<const domain::Project>()> plc,
                                 std::function<bool(std::string_view, std::string&)> live) {
    assist_.hmi = [doc = doc_]() -> const hmi::Project* { return doc ? &doc->project : nullptr; };
    assist_.plc = std::move(plc);
    assist_.live = std::move(live);
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
    const auto d = assist::describe(*hp, plc.get(), symbol, text);
    if (d.keyword || (!d.found && editor_->completionOpen())) { symbolBar_->setMessage({}); return; }
    gfx::Color accent{0, 0, 0, 0};
    if (!d.found) accent = gfx::Color{232, 196, 111, 255};
    symbolBar_->setMessage(d.line, accent);
}

void HmiFunctionsPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiFunctionsPane::nameAllowed(const std::string& name, Id self, std::string* why) const {
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
    if (const auto* sv = symbolView()) {                          // 1.11.10 : unique dans le symbole
        if (const auto* other = hmi::symbolFunction(*sv, name); other && other->id != self)
            return refuse(sv->name + " a d\xC3\xA9j\xC3\xA0 une fonction " + name);
        if (sv->param(name)) return refuse("un param\xC3\xA8tre de " + sv->name + " porte d\xC3\xA9j\xC3\xA0 ce nom");
        if (u == hmi::kSuperName) return refuse("SUPER est r\xC3\xA9serv\xC3\xA9 (SUPER.Nom() rappelle le corps du symbole)");
        return true;
    }
    if (const auto* other = doc_->project.functionByName(name); other && other->id != self)
        return refuse("une fonction porte d\xC3\xA9j\xC3\xA0 ce nom");
    if (doc_->project.variable(name)) return refuse("une variable IHM porte d\xC3\xA9j\xC3\xA0 ce nom");
    return true;
}

Id HmiFunctionsPane::addFunction(std::string name, std::string returnType, std::string description, std::string* why) {
    if (returnType == kNoReturn) returnType.clear();
    std::string reason;
    if (!nameAllowed(name, kNoId, &reason)) {
        if (why) *why = reason;
        say("Fonction refus\xC3\xA9" "e : " + reason, true);
        return kNoId;
    }
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
        made = f.id;
        if (auto* l = listOf(p)) l->push_back(std::move(f));
        else made = kNoId;
    });
    if (cmd) apply_(std::move(cmd));
    if (made) {
        selectFunction(made);
        say("Fonction " + name + " cr\xC3\xA9\xC3\xA9" "e : " + (returnType.empty() ? std::string("sans retour") : "retour " + returnType)
            + ". Ctrl+Z la retire.");
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
    std::size_t changed = 0;
    auto cmd = hmi::changeProject(doc_, "Renommer la fonction " + old + " en " + name, [&](hmi::Project& p) {
        // 1.11.10 : une fonction de symbole - ses appels (dans le symbole, Instance.Nom, Vue.Instance.Nom)
        // et les redefinitions des instances suivent.
        if (const auto* sv = symbolView()) changed = hmi::renameSymbolFunction(p, sv->name, old, name);
        else changed = hmi::renameFunctionEverywhere(p, old, name);
        if (auto* g = fnOf(p, id)) g->name = name;
    });
    if (cmd) apply_(std::move(cmd));
    say(old + " devient " + name + (changed ? " : " + std::to_string(changed) + " texte(s) suivent (appels, corps)" : std::string{})
        + ". Ctrl+Z reprend tout.");
    return true;
}

bool HmiFunctionsPane::setReturnType(Id id, const std::string& type, std::string* why) {
    const auto* f = fnOf(doc_->project, id);
    if (!f) return false;
    const std::string t = type == kNoReturn ? std::string{} : type;
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

    const auto parts = hmi::splitDeclarations(f->body, true);
    const auto inputs = parts.inputs();
    std::vector<std::pair<std::string, sim::Value>> args;
    std::string shown;
    bool bad = false;
    for (std::size_t k = 0; k < arguments.size(); ++k) {
        const std::string text = hmikit::trimmed(arguments[k]);
        if (text.empty()) continue;
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
        trial_.ok = rt.runFunction(f->name, args, result, &why);
        if (!trial_.ok) trial_.error = why;
        else if (!f->returnType.empty()) {
            trial_.result = trialText(result);
            trial_.type = f->returnType;
        }
        for (const auto& e : rt.journal())
            if (e.kind != "Syst\xC3\xA8me" && e.kind != "Erreur")
                trial_.journal.push_back(e.kind == "Journal" ? e.message : e.kind + " : " + e.message);
        for (const auto& [name, was] : before)
            if (const auto* now = rt.variable(name); now && hmi::formatValue(*now) != was)
                trial_.changed.push_back(name + " : " + was + "  \xE2\x86\x92  " + hmi::formatValue(*now));
    }
    rebuildTrial();
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
