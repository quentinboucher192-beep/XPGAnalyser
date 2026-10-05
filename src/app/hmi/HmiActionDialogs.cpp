// =============================================================================
//  app/hmi/HmiActionDialogs.cpp - voir HmiActionDialogs.hpp
// =============================================================================
#include "HmiActionDialogs.hpp"

#include "HmiAssist.hpp"
#include "HmiScriptPanes.hpp"            // placedScriptDiagnostics : les fautes, comme Compiler
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiRuntime.hpp"     // parseArguments
#include "../../hmi/HmiEnums.hpp"       // findEnumeration : un type de locale permis
#include "../../hmi/HmiScript.hpp"      // checkScript : la syntaxe
#include "../../hmi/HmiTypes.hpp"       // pathProblems : les chemins IHM
#include "../../menu/MenuManager.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>

namespace app {

namespace {

constexpr gfx::FontId kBody{15};
constexpr gfx::FontId kSmall{13};
constexpr gfx::FontId kTitle{17};
constexpr char        kSep = '\x1F';
constexpr char        kSep2 = '\x1E';

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

// Ce que le script peut lire : les references (les parametres de la vue), l'IHM, l'automate, SYS.
struct Name {
    std::string zone, name, type, detail;
};
std::vector<Name> namesFor(const hmi::Project* hp, const hmi::View* view, const domain::Project* plc) {
    std::vector<Name> out;
    if (view)
        for (const auto& prm : view->params)
            out.push_back({"R\xC3\xA9" "f\xC3\xA9rence", prm.name, prm.type.empty() ? std::string("ANY") : prm.type,
                           prm.description.empty() ? "param\xC3\xA8tre de " + view->name : prm.description});
    if (hp)
        for (const auto& v : hp->programs.variables) out.push_back({"IHM", v.name, v.type, v.description});
    if (plc) {
        std::vector<Name> api;
        for (const auto& var : plc->variables) {
            if (var.scope != domain::VariableScope::Global) continue;
            std::string name(plc->strings.text(var.name));
            if (name.empty()) continue;
            api.push_back({"API", std::move(name), std::string(plc->strings.text(var.type.name)), std::string(plc->strings.text(var.comment))});
        }
        std::sort(api.begin(), api.end(), [](const Name& a, const Name& b) { return a.name < b.name; });
        for (auto& n : api) out.push_back(std::move(n));
    }
    for (const auto& sv : hmi::pub::kSysVars) out.push_back({"SYS", "SYS." + std::string(sv.name), std::string(sv.type), std::string(sv.text)});
    return out;
}

class NameRows final : public ui::ITableModel {
public:
    explicit NameRows(const std::vector<const Name*>* rows) : rows_(rows) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_->size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"Nom", "Type", "O\xC3\xB9"};
        return c < 3 ? h[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows_->size()) return {};
        const auto* n = (*rows_)[r];
        return c == 0 ? n->name : c == 1 ? n->type : n->zone;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (c == 0 && r < rows_->size() && (*rows_)[r]->zone.rfind("R\xC3\xA9", 0) == 0) {
            s.bold = true;
            s.fgTone = ui::Tone::Accent;
        }
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
private:
    const std::vector<const Name*>* rows_;
};

} // namespace

// ======================================================= le script d'une action ===
class HmiActionScriptDialog::Body final : public ui::Widget {
public:
    Body(HmiActionScriptDialog& owner, const Spec& spec) : ui::Widget("dialog.actionScript"), owner_(owner), spec_(spec) {
        const hmi::Project* hp = spec_.doc ? &spec_.doc->project : nullptr;
        view_ = hp ? hp->view(spec_.view) : nullptr;
        names_ = namesFor(hp, view_, spec_.plc.get());
        editor_ = &static_cast<ui::MultiLineText&>(addChild(std::make_unique<ui::MultiLineText>("dialog.actionScript.code")));
        editor_->setLanguage(ui::Language::StructuredText);
        editor_->setShowLineNumbers(true);
        editor_->setReadOnly(false);
        editor_->setTabInsertsSpaces(4);
        editor_->setText(spec_.code);
        if (spec_.doc) {
            sources_ = assist::sourcesFor(spec_.doc);
            if (spec_.plc) {
                auto plc = spec_.plc;
                sources_.plc = [plc] { return plc; };
            }
            assist::attach(*editor_, sources_);
        }
        links_ += editor_->textChanged->connect([this](const std::string&) { diagnose(); });
        links_ += editor_->caretSymbolChanged->connect([this](const std::string&) { describe(); });
        search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.actionScript.chercher")));
        search_->setPlaceholder("Chercher une variable, une r\xC3\xA9" "f\xC3\xA9rence\xE2\x80\xA6");
        links_ += search_->textChanged->connect([this](const std::string&) { filter(); });
        auto table = std::make_unique<ui::TableView>("dialog.actionScript.noms");
        table->setColumns({{"Nom", 170.f}, {"Type", 110.f}, {"O\xC3\xB9", 80.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        list_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
        links_ += list_->activated->connect([this](ui::RowIndex r) {
            if (r < shown_.size()) (void)insert(shown_[r]->name);
        });
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.actionScript.annuler")));
        ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Valider (Ctrl+Entr\xC3\xA9" "e)", "dialog.actionScript.valider")));
        ok_->setStyle(ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += ok_->clicked->connect([this] { owner_.finish(true); });
        filter();
        diagnose();
    }
    [[nodiscard]] std::string code() const { return editor_->text(); }
    void setCode(const std::string& c) { editor_->setText(c); diagnose(); }
    [[nodiscard]] std::size_t errors() const {
        std::size_t n = 0;
        for (const auto& d : diags_) n += d.severity == hmi::ScriptDiagnostic::Severity::Error ? 1 : 0;
        return n;
    }
    [[nodiscard]] std::vector<std::string> shownNames() const {
        std::vector<std::string> out;
        for (const auto* n : shown_) out.push_back(n->name);
        return out;
    }
    void setSearch(const std::string& t) { search_->setText(t); filter(); }
    bool insert(const std::string& name) {
        if (name.empty()) return false;
        owner_.focus().focus(editor_);
        editor_->insertText(name);
        return true;
    }
    void focusEditor() { owner_.focus().focus(editor_); }
    [[nodiscard]] bool editorFocused() const { return editor_->focused(); }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1100.f, r.w - 40.f), h = std::min(700.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float side = std::min(360.f, w * 0.34f);
        const float top = panel_.y + 52.f, bottom = panel_.bottom() - 52.f;
        diagH_ = 66.f;
        editor_->setBounds({panel_.x + 16.f, top, w - side - 44.f, bottom - top - diagH_ - 30.f});
        symbolLine_ = {panel_.x + 16.f, editor_->bounds().bottom() + 4.f, editor_->bounds().w, 22.f};
        diagBox_ = {panel_.x + 16.f, symbolLine_.bottom() + 4.f, editor_->bounds().w, diagH_};
        search_->setBounds({panel_.right() - 16.f - side, top + 22.f, side, 28.f});
        list_->setBounds({panel_.right() - 16.f - side, top + 56.f, side, bottom - top - 56.f});
        ok_->setBounds({panel_.right() - 16.f - 200.f, bottom + 10.f, 200.f, 32.f});
        cancel_->setBounds({ok_->bounds().x - 10.f - 100.f, bottom + 10.f, 100.f, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.drawText({panel_.x + 16.f, panel_.y + 13.f}, fit(r, "Script de l'action \xC2\xB7 " + spec_.where, kTitle, panel_.w * 0.7f), kTitle, c.text);
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        r.drawText({search_->bounds().x, search_->bounds().y - 20.f},
                   "Variables et r\xC3\xA9" "f\xC3\xA9rences (double-clic : ins\xC3\xA9rer)", kSmall, c.textMuted);
        // La ligne du nom sous le curseur.
        r.drawText({symbolLine_.x + 2.f, symbolLine_.y + 3.f}, fit(r, symbol_.empty() ? std::string("Ctrl+Espace : les noms ; le curseur sur un nom : ce qu'il est") : symbol_, kSmall, symbolLine_.w),
                   kSmall, symbol_.empty() ? c.textDisabled : c.textMuted);
        // Les fautes.
        r.fillRect(diagBox_, c.inputBg);
        r.strokeRect(diagBox_, c.border, 1.f);
        if (diags_.empty()) {
            r.drawText({diagBox_.x + 10.f, diagBox_.y + 8.f}, "Aucune faute.", kSmall, c.ok);
        } else {
            float y = diagBox_.y + 5.f;
            for (std::size_t i = 0; i < diags_.size() && i < 3; ++i) {
                const auto& d = diags_[i];
                const bool err = d.severity == hmi::ScriptDiagnostic::Severity::Error;
                const std::string t = "ligne " + std::to_string(d.line) + (d.column > 0 ? ", col. " + std::to_string(d.column) : std::string{}) + " : " + d.message;
                r.drawText({diagBox_.x + 10.f, y}, fit(r, t, kSmall, diagBox_.w - 20.f), kSmall, err ? c.error : c.warning);
                y += 19.f;
            }
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && close_.contains(d->pos)) {
            owner_.finish(false);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    void filter() {
        shown_.clear();
        const ui::SearchQuery q(search_->text());
        for (const auto& n : names_)
            if (q.matches({n.name, n.type, n.zone, n.detail})) shown_.push_back(&n);
        model_ = std::make_shared<NameRows>(&shown_);
        list_->setModel(model_);
    }
    void diagnose() {
        diags_.clear();
        if (!spec_.doc) return;
        const std::string code = editor_->text();
        // 1.11.9 : comme l'editeur des scripts (allDiagnostics) - la syntaxe, les chemins IHM, puis
        // les fautes a leur place (avant : seulement les dernieres, "Compteur := ;" passait).
        diags_ = guardedDiagnostics(
            [&] {
                const auto& p = spec_.doc->project;
                const hmi::TypeKnown knownType = [&p](std::string_view t) {
                    return !hmi::types::membersOf(p, t).empty() || hmi::findEnumeration(p, t) != nullptr;
                };
                // 1.11.10 : une redefinition se controle comme une fonction (ses entrees, son resultat).
                std::optional<hmi::HmiFunction> fn = spec_.function;
                if (fn) fn->body = code;
                auto d = fn ? hmi::checkFunction(*fn, knownType) : hmi::checkScript(hmi::ScriptLang::ST, code, "action", knownType);
                const auto placed = placedScriptDiagnostics(p, code, view_, fn ? &*fn : nullptr, spec_.plc.get());
                for (const auto& pp : hmi::types::pathProblems(p, code)) {
                    const bool said = std::any_of(placed.begin(), placed.end(), [&](const hmi::ScriptDiagnostic& x) {
                        return x.line == pp.line && x.message == pp.message;
                    });
                    if (!said) d.push_back({hmi::ScriptDiagnostic::Severity::Error, pp.line, pp.message});
                }
                d.insert(d.end(), placed.begin(), placed.end());
                return d;
            },
            1, "le script d'une action");
        editor_->setSquiggles(squigglesOf(diags_));
        invalidate();
    }
    void describe() {
        symbol_.clear();
        const auto* hp = spec_.doc ? &spec_.doc->project : nullptr;
        const std::string s = editor_->symbolAtCaret();
        if (hp && !s.empty()) {
            const auto d = assist::describe(*hp, spec_.plc.get(), s, editor_->text());
            if (d.found && !d.keyword) symbol_ = d.line;
        }
        invalidate();
    }

    HmiActionScriptDialog&            owner_;
    Spec                              spec_;
    const hmi::View*                  view_{nullptr};
    std::vector<Name>                 names_;
    std::vector<const Name*>          shown_;
    std::shared_ptr<ui::ITableModel>  model_;
    assist::Sources                   sources_;
    std::vector<hmi::ScriptDiagnostic> diags_;
    std::string                       symbol_;
    ui::MultiLineText*                editor_{nullptr};
    ui::InputText*                    search_{nullptr};
    ui::TableView*                    list_{nullptr};
    ui::Button*                       ok_{nullptr};
    ui::Button*                       cancel_{nullptr};
    gfx::Rect                         panel_{}, close_{}, symbolLine_{}, diagBox_{};
    float                             diagH_{66.f};
    core::ConnectionScope             links_;
};

HmiActionScriptDialog::HmiActionScriptDialog(Spec spec) : menu::WidgetMenu("dialog.actionScript"), spec_(std::move(spec)) {}
HmiActionScriptDialog::~HmiActionScriptDialog() = default;
menu::MenuTraits HmiActionScriptDialog::traits() const { return dialogTraits(); }
std::string HmiActionScriptDialog::title() const { return spec_.title.empty() ? std::string("Script de l'action") : spec_.title; }

core::Status HmiActionScriptDialog::buildUi() {
    auto body = std::make_unique<Body>(*this, spec_);
    body_ = body.get();
    setRoot(std::move(body));
    return core::ok();
}
void HmiActionScriptDialog::onEnter() {
    if (body_) body_->focusEditor();
}
ui::EventResult HmiActionScriptDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && body_) {
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            finish(true);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Escape && k->mods.none()) {
            finish(false);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}
std::string HmiActionScriptDialog::code() const { return body_ ? body_->code() : spec_.code; }
void HmiActionScriptDialog::setCode(const std::string& c) { if (body_) body_->setCode(c); }
std::size_t HmiActionScriptDialog::errorCount() const { return body_ ? body_->errors() : 0; }
std::vector<std::string> HmiActionScriptDialog::names() const { return body_ ? body_->shownNames() : std::vector<std::string>{}; }
void HmiActionScriptDialog::setSearch(const std::string& t) { if (body_) body_->setSearch(t); }
bool HmiActionScriptDialog::insertName(const std::string& n) { return body_ && body_->insert(n); }
void HmiActionScriptDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, code()});
}

// ============================================================ l'operation en arbre ===
namespace {
// Les noeuds : 0 la racine, 1..N les familles, 1000 + l'operation.
constexpr ui::NodeId kOpBase = 1000;
ui::NodeId nodeOf(hmi::Operation o) { return kOpBase + static_cast<ui::NodeId>(o); }

class OperationTree final : public ui::ITreeModel {
public:
    struct Group {
        std::string                 name;
        std::vector<hmi::Operation> ops;
    };
    std::vector<Group> groups;
    hmi::Operation     current{hmi::Operation::Set};
    [[nodiscard]] ui::NodeId  root() const override { return 0; }
    [[nodiscard]] std::size_t childCount(ui::NodeId n) const override {
        if (n == 0) return groups.size();
        if (n <= groups.size()) return groups[n - 1].ops.size();
        return 0;
    }
    [[nodiscard]] ui::NodeId childAt(ui::NodeId parent, std::size_t i) const override {
        if (parent == 0) return i + 1;
        if (parent <= groups.size() && i < groups[parent - 1].ops.size()) return nodeOf(groups[parent - 1].ops[i]);
        return 0;
    }
    [[nodiscard]] bool hasChildren(ui::NodeId n) const override { return n == 0 || (n <= groups.size() && !groups[n - 1].ops.empty()); }
    [[nodiscard]] std::string text(ui::NodeId n) const override {
        if (n > 0 && n <= groups.size()) return groups[n - 1].name + "  (" + std::to_string(groups[n - 1].ops.size()) + ")";
        if (n >= kOpBase) return std::string(hmi::operationLabel(static_cast<hmi::Operation>(n - kOpBase)));
        return {};
    }
    [[nodiscard]] ui::CellStyle style(ui::NodeId n) const override {
        ui::CellStyle st;
        if (n > 0 && n <= groups.size()) st.bold = true;
        if (n == nodeOf(current)) {
            st.bold = true;
            st.fgTone = ui::Tone::Accent;
        }
        return st;
    }
};
} // namespace

class HmiOperationDialog::Body final : public ui::Widget {
public:
    Body(HmiOperationDialog& owner, const Spec& spec) : ui::Widget("dialog.operation"), owner_(owner), spec_(spec), chosen_(spec.current) {
        search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.operation.chercher")));
        search_->setPlaceholder("Chercher une op\xC3\xA9ration (nom, famille, ce qu'elle fait)\xE2\x80\xA6");
        links_ += search_->textChanged->connect([this](const std::string&) { filter(); });
        tree_ = &static_cast<ui::TreeView&>(addChild(std::make_unique<ui::TreeView>("dialog.operation.arbre")));
        tree_->setShowRootNode(false);                                 // les familles a la racine
        links_ += tree_->selectionChanged->connect([this](ui::NodeId n) {
            if (n >= kOpBase) {
                chosen_ = static_cast<hmi::Operation>(n - kOpBase);
                invalidate();
            }
        });
        links_ += tree_->activated->connect([this](ui::NodeId n) {
            if (n >= kOpBase) {
                chosen_ = static_cast<hmi::Operation>(n - kOpBase);
                owner_.finish(true);
            } else if (n > 0) {
                tree_->toggle(n);
            }
        });
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.operation.annuler")));
        ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Choisir (Entr\xC3\xA9" "e)", "dialog.operation.choisir")));
        ok_->setStyle(ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += ok_->clicked->connect([this] { owner_.finish(true); });
        filter();
    }
    void setSearch(const std::string& t) { search_->setText(t); filter(); }
    [[nodiscard]] std::vector<std::string> groupsShown() const {
        std::vector<std::string> out;
        if (model_)
            for (const auto& g : model_->groups) out.push_back(g.name);
        return out;
    }
    [[nodiscard]] std::vector<std::string> operationsShown() const {
        std::vector<std::string> out;
        if (model_)
            for (const auto& g : model_->groups)
                for (const auto o : g.ops) out.emplace_back(hmi::operationLabel(o));
        return out;
    }
    bool choose(hmi::Operation o) {
        if (!model_) return false;
        for (const auto& g : model_->groups)
            if (std::find(g.ops.begin(), g.ops.end(), o) != g.ops.end()) {
                chosen_ = o;
                tree_->setCurrentNode(nodeOf(o));
                invalidate();
                return true;
            }
        return false;
    }
    [[nodiscard]] hmi::Operation chosen() const { return chosen_; }
    [[nodiscard]] std::string helpLine() const {
        return std::string(hmi::operationLabel(chosen_)) + " (" + std::string(hmi::actionkinds::groupOf(chosen_)) + ") \xE2\x80\x94 "
             + std::string(hmi::actionkinds::help(chosen_));
    }
    void focusSearch() { owner_.focus().focus(search_); }
    // Entree : la ligne choisie ; les fleches : la suivante, la precedente (dans l'ordre montre).
    bool step(int delta) {
        const auto all = operations();
        if (all.empty()) return false;
        auto it = std::find(all.begin(), all.end(), chosen_);
        std::ptrdiff_t i = it == all.end() ? 0 : (it - all.begin()) + delta;
        i = std::clamp<std::ptrdiff_t>(i, 0, static_cast<std::ptrdiff_t>(all.size()) - 1);
        return choose(all[static_cast<std::size_t>(i)]);
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(640.f, r.w - 40.f), h = std::min(640.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        search_->setBounds({panel_.x + 16.f, panel_.y + 54.f, w - 32.f, 30.f});
        const float bottom = panel_.bottom() - 52.f;
        helpBox_ = {panel_.x + 16.f, bottom - 58.f, w - 32.f, 54.f};
        tree_->setBounds({panel_.x + 16.f, panel_.y + 92.f, w - 32.f, helpBox_.y - 8.f - (panel_.y + 92.f)});
        ok_->setBounds({panel_.right() - 16.f - 160.f, bottom + 10.f, 160.f, 32.f});
        cancel_->setBounds({ok_->bounds().x - 10.f - 100.f, bottom + 10.f, 100.f, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.drawText({panel_.x + 16.f, panel_.y + 13.f}, fit(r, "Op\xC3\xA9ration \xC2\xB7 " + spec_.where, kTitle, panel_.w - 70.f), kTitle, c.text);
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        r.fillRect(helpBox_, c.inputBg);
        r.strokeRect(helpBox_, c.border, 1.f);
        if (model_ && model_->groups.empty()) {
            r.drawText({helpBox_.x + 10.f, helpBox_.y + 8.f}, "Aucune op\xC3\xA9ration ne contient \xC2\xAB " + search_->text() + " \xC2\xBB.", kSmall, c.warning);
            return;
        }
        r.drawText({helpBox_.x + 10.f, helpBox_.y + 6.f}, fit(r, std::string(hmi::operationLabel(chosen_)) + "  \xC2\xB7  " + std::string(hmi::actionkinds::groupOf(chosen_)), kBody, helpBox_.w - 20.f),
                   kBody, c.text);
        r.drawText({helpBox_.x + 10.f, helpBox_.y + 29.f}, fit(r, std::string(hmi::actionkinds::help(chosen_)), kSmall, helpBox_.w - 20.f), kSmall, c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && close_.contains(d->pos)) {
            owner_.finish(false);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    [[nodiscard]] std::vector<hmi::Operation> operations() const {
        std::vector<hmi::Operation> out;
        if (model_)
            for (const auto& g : model_->groups) out.insert(out.end(), g.ops.begin(), g.ops.end());
        return out;
    }
    void filter() {
        auto m = std::make_shared<OperationTree>();
        m->current = spec_.current;
        const ui::SearchQuery q(search_->text());
        for (const auto& g : hmi::actionkinds::groups()) {
            OperationTree::Group kept{std::string(g.name), {}};
            for (const auto o : g.operations)
                if (search_->text().empty() || q.matches({std::string(hmi::operationLabel(o)), std::string(g.name), std::string(hmi::actionkinds::help(o))}))
                    kept.ops.push_back(o);
            if (!kept.ops.empty()) m->groups.push_back(std::move(kept));
        }
        model_ = m;
        tree_->setModel(model_);
        // Recherche vide : la famille de l'operation actuelle ouverte ; sinon tout ce qui est trouve.
        for (std::size_t i = 0; i < model_->groups.size(); ++i) {
            const auto& g = model_->groups[i];
            const bool mine = std::find(g.ops.begin(), g.ops.end(), chosen_) != g.ops.end();
            if (!search_->text().empty() || mine) tree_->expand(i + 1);
        }
        if (!choose(chosen_)) {
            const auto all = operations();
            if (!all.empty()) (void)choose(all.front());
        }
        invalidate();
    }

    HmiOperationDialog&                owner_;
    Spec                               spec_;
    hmi::Operation                     chosen_;
    std::shared_ptr<OperationTree>     model_;
    ui::InputText*                     search_{nullptr};
    ui::TreeView*                      tree_{nullptr};
    ui::Button*                        ok_{nullptr};
    ui::Button*                        cancel_{nullptr};
    gfx::Rect                          panel_{}, close_{}, helpBox_{};
    core::ConnectionScope              links_;
};

HmiOperationDialog::HmiOperationDialog(Spec spec) : menu::WidgetMenu("dialog.operation"), spec_(std::move(spec)) {}
HmiOperationDialog::~HmiOperationDialog() = default;
menu::MenuTraits HmiOperationDialog::traits() const { return dialogTraits(); }
std::string HmiOperationDialog::title() const { return "Op\xC3\xA9ration de l'action"; }
core::Status HmiOperationDialog::buildUi() {
    auto body = std::make_unique<Body>(*this, spec_);
    body_ = body.get();
    setRoot(std::move(body));
    return core::ok();
}
void HmiOperationDialog::onEnter() {
    if (body_) body_->focusSearch();
}
ui::EventResult HmiOperationDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && body_) {
        if (k->key == ui::Key::Return) {
            finish(true);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Escape && k->mods.none()) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Down || k->key == ui::Key::Up) {
            (void)body_->step(k->key == ui::Key::Down ? 1 : -1);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}
std::optional<hmi::Operation> HmiOperationDialog::parse(const std::string& payload) { return hmi::operationFromLabel(payload); }
void HmiOperationDialog::setSearch(const std::string& t) { if (body_) body_->setSearch(t); }
std::vector<std::string> HmiOperationDialog::groupsShown() const { return body_ ? body_->groupsShown() : std::vector<std::string>{}; }
std::vector<std::string> HmiOperationDialog::operationsShown() const { return body_ ? body_->operationsShown() : std::vector<std::string>{}; }
bool HmiOperationDialog::choose(hmi::Operation o) { return body_ && body_->choose(o); }
hmi::Operation HmiOperationDialog::chosen() const { return body_ ? body_->chosen() : spec_.current; }
std::string HmiOperationDialog::helpLine() const { return body_ ? body_->helpLine() : std::string{}; }
void HmiOperationDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             std::string(hmi::operationLabel(chosen()))});
}

// ===================================================================== Maths ===
class HmiMathsDialog::Body final : public ui::Widget {
public:
    struct RefRow {
        ui::InputText* name{nullptr};
        ui::InputText* path{nullptr};
        ui::InputText* test{nullptr};
        ui::Button*    remove{nullptr};
    };
    Body(HmiMathsDialog& owner, const Spec& spec) : ui::Widget("dialog.maths"), owner_(owner), spec_(spec) {
        if (spec_.doc) {
            sources_ = assist::sourcesFor(spec_.doc);
            fieldAssist_ = assist::fieldAssist(sources_);
        }
        target_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.maths.resultat")));
        target_->setText(spec_.target);
        target_->setPlaceholder("la variable qui re\xC3\xA7oit le r\xC3\xA9sultat");
        if (fieldAssist_) target_->setAssist(fieldAssist_);
        formula_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.maths.formule")));
        formula_->setText(spec_.formula);
        formula_->setPlaceholder("(Mesure - Consigne) * 2");
        if (fieldAssist_) formula_->setAssist(fieldAssist_);
        links_ += formula_->textChanged->connect([this](const std::string&) { changed(); });
        links_ += target_->textChanged->connect([this](const std::string&) { changed(); });
        add_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("+ Ajouter une r\xC3\xA9" "f\xC3\xA9rence", "dialog.maths.ajouter")));
        links_ += add_->clicked->connect([this] { (void)addRef({}, {}, {}); });
        tester_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Tester", "dialog.maths.tester")));
        links_ += tester_->clicked->connect([this] { (void)runTest(); });
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.maths.annuler")));
        ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Valider", "dialog.maths.valider")));
        ok_->setStyle(ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += ok_->clicked->connect([this] { owner_.validate(); });
        for (const auto& [n, p] : spec_.refs) (void)addRef(n, p, {});
        if (rows_.empty()) (void)addRef({}, {}, {});
        changed();
    }

    std::size_t addRef(const std::string& name, const std::string& path, const std::string& test) {
        const std::string base = "dialog.maths.ref" + std::to_string(serial_++);
        RefRow row;
        row.name = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".nom")));
        row.name->setText(name.empty() && rows_.size() < 26 ? std::string(1, static_cast<char>('A' + rows_.size())) : name);
        row.path = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".reference")));
        row.path->setText(path);
        row.path->setPlaceholder("une variable : Armoires[0].ana.PT1.mes");
        if (fieldAssist_) row.path->setAssist(fieldAssist_);
        row.test = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".test")));
        row.test->setText(test);
        row.test->setPlaceholder("0");
        row.remove = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("\xC3\x97", base + ".retirer")));
        row.remove->setTooltip("Retirer cette r\xC3\xA9" "f\xC3\xA9rence");
        auto* removeButton = row.remove;
        links_ += row.remove->clicked->connect([this, removeButton] {
            for (std::size_t i = 0; i < rows_.size(); ++i)
                if (rows_[i].remove == removeButton) {
                    (void)removeRef(i);
                    return;
                }
        });
        for (auto* f : {row.name, row.path}) links_ += f->textChanged->connect([this](const std::string&) { changed(); });
        rows_.push_back(row);
        invalidateLayout();
        changed();
        return rows_.size() - 1;
    }
    bool removeRef(std::size_t i) {
        if (i >= rows_.size()) return false;
        auto& r = rows_[i];
        for (ui::Widget* w : {static_cast<ui::Widget*>(r.name), static_cast<ui::Widget*>(r.path), static_cast<ui::Widget*>(r.test),
                              static_cast<ui::Widget*>(r.remove)})
            w->setVisibility(ui::Visibility::Collapsed);   // detruits avec la fenetre (un signal peut etre en cours)
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(i));
        invalidateLayout();
        changed();
        return true;
    }
    void setTarget(const std::string& t) { target_->setText(t); }
    void setFormula(const std::string& f) { formula_->setText(f); }
    void setTest(std::size_t i, const std::string& v) { if (i < rows_.size()) rows_[i].test->setText(v); }
    [[nodiscard]] std::size_t refCount() const { return rows_.size(); }
    // Les lignes gardees : pas vides ; une ligne laissee vide (la premiere, « A ») que la formule
    // ne cite pas est ignoree.
    [[nodiscard]] bool kept(const RefRow& r) const {
        if (r.name->text().empty() && r.path->text().empty()) return false;
        return !r.path->text().empty()
            || hmi::actionkinds::mathsExpression(formula_->text(), {{r.name->text(), "\x01"}}).find('\x01') != std::string::npos;
    }
    [[nodiscard]] hmi::actionkinds::Params refs() const {
        hmi::actionkinds::Params out;
        for (const auto& r : rows_)
            if (kept(r)) out.emplace_back(r.name->text(), r.path->text());
        return out;
    }
    [[nodiscard]] Answer answer() const { return {target_->text(), formula_->text(), refs()}; }
    [[nodiscard]] const std::vector<hmi::actionkinds::MathsIssue>& issues() const { return issues_; }
    [[nodiscard]] bool blocking() const {
        return std::any_of(issues_.begin(), issues_.end(), [](const auto& i) { return !i.warning; }) || target_->text().empty();
    }
    hmi::actionkinds::MathsTest runTest() {
        std::vector<std::string> values;
        for (const auto& r : rows_)
            if (kept(r)) values.push_back(r.test->text());     // une valeur par reference gardee, dans l'ordre
        last_ = hmi::actionkinds::testMaths(refs(), values, formula_->text());
        tested_ = true;
        invalidate();
        return last_;
    }
    bool insertFunction(const std::string& f) {
        formula_->setText(formula_->text() + f + "(");
        owner_.focus().focus(formula_);
        return true;
    }
    void setRefused(bool on) { refused_ = on; invalidate(); }
    void focusFormula() { owner_.focus().focus(formula_); }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(860.f, r.w - 40.f);
        const float rowsH = static_cast<float>(rows_.size()) * 34.f;
        const float h = std::min(r.h - 40.f, 380.f + rowsH);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float lx = panel_.x + 16.f, fx = panel_.x + 150.f, fw = panel_.right() - 16.f - fx;
        float y = panel_.y + 56.f;
        target_->setBounds({fx, y, fw, 28.f});
        y += 44.f;
        refsTitle_ = y;
        add_->setBounds({panel_.right() - 16.f - 210.f, y - 4.f, 210.f, 28.f});
        y += 52.f;   // le titre, puis l'en-tete des colonnes
        for (auto& row : rows_) {
            row.name->setBounds({lx + 16.f, y, 120.f, 28.f});
            row.path->setBounds({lx + 146.f, y, fw - 210.f, 28.f});
            row.test->setBounds({row.path->bounds().right() + 10.f, y, 110.f, 28.f});
            row.remove->setBounds({row.test->bounds().right() + 6.f, y, 28.f, 28.f});
            y += 34.f;
        }
        (void)lx;
        y += 8.f;
        formula_->setBounds({fx, y, fw, 28.f});
        formulaY_ = y;
        y += 36.f;
        functionsY_ = y;
        y += 30.f;
        issuesY_ = y;
        const float bottom = panel_.bottom() - 52.f;
        tester_->setBounds({panel_.x + 16.f, bottom - 40.f, 100.f, 30.f});
        testY_ = bottom - 40.f;
        ok_->setBounds({panel_.right() - 16.f - 120.f, bottom + 10.f, 120.f, 32.f});
        cancel_->setBounds({ok_->bounds().x - 10.f - 100.f, bottom + 10.f, 100.f, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.drawText({panel_.x + 16.f, panel_.y + 13.f}, fit(r, "Maths \xC2\xB7 " + spec_.where, kTitle, panel_.w * 0.7f), kTitle, c.text);
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        const float lx = panel_.x + 16.f;
        r.drawText({lx, target_->bounds().y + 5.f}, "R\xC3\xA9sultat dans", kBody, c.text);
        r.drawText({lx, refsTitle_}, "R\xC3\xA9" "f\xC3\xA9rences", kBody, c.text);
        r.drawText({lx + 110.f, refsTitle_ + 2.f}, "des variables (pas de calcul) : la formule les nomme", kSmall, c.textMuted);
        r.drawText({lx + 16.f, refsTitle_ + 30.f}, "Nom", kSmall, c.textMuted);
        if (!rows_.empty()) {
            r.drawText({rows_.front().path->bounds().x, refsTitle_ + 30.f}, "R\xC3\xA9" "f\xC3\xA9rence (variable)", kSmall, c.textMuted);
            r.drawText({rows_.front().test->bounds().x, refsTitle_ + 30.f}, "Valeur de test", kSmall, c.textMuted);
        }
        r.drawText({lx, formulaY_ + 5.f}, "Formule", kBody, c.text);
        // Les fonctions : un clic insere.
        chips_.clear();
        float x = panel_.x + 150.f;
        r.drawText({lx, functionsY_ + 4.f}, "Fonctions", kSmall, c.textMuted);
        for (const char* f : kFunctions) {
            const float tw = r.measure(f, kSmall).width + 16.f;
            const gfx::Rect chip{x, functionsY_, tw, 22.f};
            r.fillRoundedRect(chip, c.inputBg, 4.f);
            r.strokeRect(chip, c.border, 1.f);
            r.drawText({chip.x + 8.f, chip.y + 3.f}, f, kSmall, c.accent);
            chips_.push_back({chip, f});
            x += tw + 6.f;
            if (x > panel_.right() - 60.f) break;
        }
        // Les fautes.
        float y = issuesY_;
        for (std::size_t i = 0; i < issues_.size() && i < 4; ++i) {
            const auto& is = issues_[i];
            r.drawText({lx, y}, fit(r, (is.warning ? "\xE2\x9A\xA0 " : "\xE2\x9C\x96 ") + is.why, kSmall, panel_.w - 32.f), kSmall,
                       is.warning ? c.warning : c.error);
            y += 18.f;
        }
        if (refused_) r.drawText({lx, y}, "Valider : corrige d'abord les fautes en rouge.", kSmall, c.error);
        // Le test.
        if (tested_) {
            const float tx = tester_->bounds().right() + 14.f;
            const std::string t = last_.ok ? "= " + last_.result + "   (" + last_.expression + ")" : last_.why;
            r.drawText({tx, testY_ + 6.f}, fit(r, t, kBody, panel_.right() - 16.f - tx), kBody, last_.ok ? c.ok : c.error);
        } else {
            r.drawText({tester_->bounds().right() + 14.f, testY_ + 7.f}, "les valeurs de test \xC3\xA0 la place des r\xC3\xA9" "f\xC3\xA9rences", kSmall,
                       c.textMuted);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (close_.contains(d->pos)) {
                owner_.finish(false);
                return ui::EventResult::Consumed;
            }
            for (const auto& [rect, name] : chips_)
                if (rect.contains(d->pos)) {
                    (void)insertFunction(name);
                    return ui::EventResult::Consumed;
                }
        }
        return ui::EventResult::Ignored;
    }

private:
    static constexpr const char* kFunctions[] = {"ABS", "SQRT", "MIN", "MAX", "LIMIT", "SIN", "COS", "TAN", "EXP", "LN", "LOG", "TRUNC", "SEL", "MUX"};
    void changed() {
        issues_ = hmi::actionkinds::checkMaths(refs(), formula_->text());
        refused_ = false;
        tested_ = false;
        invalidate();
    }

    HmiMathsDialog&                           owner_;
    Spec                                      spec_;
    assist::Sources                           sources_;
    ui::InputText::Assist                     fieldAssist_;
    ui::InputText*                            target_{nullptr};
    ui::InputText*                            formula_{nullptr};
    ui::Button*                               add_{nullptr};
    ui::Button*                               tester_{nullptr};
    ui::Button*                               ok_{nullptr};
    ui::Button*                               cancel_{nullptr};
    std::vector<RefRow>                       rows_;
    std::size_t                               serial_{0};
    std::vector<hmi::actionkinds::MathsIssue> issues_;
    hmi::actionkinds::MathsTest               last_;
    bool                                      tested_{false}, refused_{false};
    std::vector<std::pair<gfx::Rect, std::string>> chips_;
    gfx::Rect                                 panel_{}, close_{};
    float                                     refsTitle_{0}, formulaY_{0}, functionsY_{0}, issuesY_{0}, testY_{0};
    core::ConnectionScope                     links_;
};

HmiMathsDialog::HmiMathsDialog(Spec spec) : menu::WidgetMenu("dialog.maths"), spec_(std::move(spec)) {}
HmiMathsDialog::~HmiMathsDialog() = default;
menu::MenuTraits HmiMathsDialog::traits() const { return dialogTraits(); }
std::string HmiMathsDialog::title() const { return "Maths"; }

core::Status HmiMathsDialog::buildUi() {
    auto body = std::make_unique<Body>(*this, spec_);
    body_ = body.get();
    setRoot(std::move(body));
    return core::ok();
}
void HmiMathsDialog::onEnter() {
    if (body_) body_->focusFormula();
}
ui::EventResult HmiMathsDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && body_) {
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            validate();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Escape && k->mods.none()) {
            finish(false);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}
void HmiMathsDialog::setTarget(const std::string& t) { if (body_) body_->setTarget(t); }
void HmiMathsDialog::setFormula(const std::string& f) { if (body_) body_->setFormula(f); }
std::size_t HmiMathsDialog::addReference(const std::string& n, const std::string& p, const std::string& t) {
    return body_ ? body_->addRef(n, p, t) : 0;
}
bool HmiMathsDialog::removeReference(std::size_t i) { return body_ && body_->removeRef(i); }
void HmiMathsDialog::setTestValue(std::size_t i, const std::string& v) { if (body_) body_->setTest(i, v); }
std::size_t HmiMathsDialog::referenceCount() const { return body_ ? body_->refCount() : 0; }
HmiMathsDialog::Answer HmiMathsDialog::answer() const { return body_ ? body_->answer() : Answer{spec_.target, spec_.formula, spec_.refs}; }
std::vector<hmi::actionkinds::MathsIssue> HmiMathsDialog::issues() const {
    return body_ ? body_->issues() : std::vector<hmi::actionkinds::MathsIssue>{};
}
hmi::actionkinds::MathsTest HmiMathsDialog::test() { return body_ ? body_->runTest() : hmi::actionkinds::MathsTest{}; }
bool HmiMathsDialog::insertFunction(const std::string& name) { return body_ && body_->insertFunction(name); }
void HmiMathsDialog::validate() {
    if (!body_) return;
    if (body_->blocking()) {
        body_->setRefused(true);
        return;
    }
    finish(true);
}
void HmiMathsDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    const auto a = answer();
    const std::string payload = a.target + kSep + a.formula + kSep + hmi::actionkinds::formatParams(a.refs);
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload});
}
HmiMathsDialog::Answer HmiMathsDialog::parse(const std::string& payload) {
    std::vector<std::string> parts{""};
    for (const char ch : payload) {
        if (ch == kSep) parts.emplace_back();
        else parts.back() += ch;
    }
    Answer a;
    a.target = parts[0];
    a.formula = parts.size() > 1 ? parts[1] : std::string{};
    if (parts.size() > 2) a.refs = hmi::parseArguments(parts[2]);
    (void)kSep2;
    return a;
}

} // namespace app
