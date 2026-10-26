// =============================================================================
//  app/hmi/HmiMethodPanes.cpp - la section Methodes d'un type IHM (1.13.0, J1b)
// =============================================================================
#include "HmiMethodPanes.hpp"

#include "HmiAssetPanes.hpp"     // hmiSelectModelRow
#include "HmiIcons.hpp"
#include "HmiScriptPanes.hpp"    // guardedDiagnostics, squigglesOf
#include "HmiTypePicker.hpp"     // "Choisir..." : le selecteur de types
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiDecl.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;

namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool sameText(std::string_view a, std::string_view b) { return upperOf(a) == upperOf(b); }
std::string trimmedOf(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// La methode `id` d'un projet modifiable, et son type.
hmi::HmiFunction* findMethod(hmi::Project& p, Id id, hmi::HmiType** owner = nullptr) {
    for (auto& t : p.programs.types)
        for (auto& m : t.methods)
            if (m.id == id) {
                if (owner) *owner = &t;
                return &m;
            }
    return nullptr;
}

// Les fautes de forme de toutes les methodes (methodProblem) : un geste qui en fait naitre une est refuse.
std::set<std::string> methodProblems(const hmi::Project& p) {
    std::set<std::string> out;
    for (const auto& t : p.programs.types)
        for (const auto& m : t.methods)
            if (std::string w = hmi::methodProblem(p, t, m); !w.empty()) out.insert(std::move(w));
    return out;
}
std::string newProblem(const hmi::Project& before, const hmi::Project& after) {
    const auto old = methodProblems(before);
    for (const auto& w : methodProblems(after))
        if (!old.count(w)) return w;
    return {};
}

// Pourquoi `name` ne va pas comme nom d'une methode de `t` (vide : il va) ; `self` : la methode renommee.
std::string nameProblem(const hmi::Project& p, const hmi::HmiType& t, const std::string& name, Id self) {
    if (name.empty()) return "donne un nom";
    if (!hmi::isIdentifier(name)) return "nom invalide : lettres, chiffres et _, pas de chiffre en t\xC3\xAAte";
    for (const auto& m : t.methods)
        if (m.id != self && sameText(m.name, name)) return t.name + " a d\xC3\xA9j\xC3\xA0 une m\xC3\xA9thode " + m.name;
    for (const auto& mem : hmi::types::membersOf(p, hmi::fullName(t)))
        if (sameText(mem.name, name)) return name + " est d\xC3\xA9j\xC3\xA0 un membre de " + t.name + " (un membre et une m\xC3\xA9thode ne partagent pas un nom)";
    return {};
}

// La colonne du corps : la legende au-dessus, l'editeur dessous.
class CodeColumn final : public ui::Widget {
public:
    explicit CodeColumn(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        if (children().size() < 2) return;
        constexpr float kLegend = 24.f;
        children()[0]->setBounds({b.x, b.y, b.w, kLegend});
        children()[1]->setBounds({b.x, b.y + kLegend + 2.f, b.w, std::max(0.f, b.h - kLegend - 2.f)});
    }
};

// Le modele de la liste : du texte, un style, la saisie des cases d'une methode propre.
class MethodRows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    using Editable = std::function<bool(std::size_t, std::size_t)>;
    using Choices = std::function<std::vector<std::string>(std::size_t, std::size_t)>;
    using Commit = std::function<bool(std::size_t, std::size_t, const std::string&)>;
    MethodRows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style s, Editable e, Choices c, Commit m,
               std::vector<std::string> tips = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(s)), editable_(std::move(e)), choices_(std::move(c)),
          commit_(std::move(m)), tips_(std::move(tips)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return editable_ && editable_(r, c); }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex r, std::size_t c) const override {
        return choices_ ? choices_(r, c) : std::vector<std::string>{};
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override { return commit_ && commit_(r, c, std::string(text)); }
    // 1.13.0 (correctifs F2) : la ligne entiere au survol (sa signature, sa visibilite, son genre, son origine).
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < tips_.size() ? tips_[r] : std::string{}; }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    Style    style_;
    Editable editable_;
    Choices  choices_;
    Commit   commit_;
    std::vector<std::string> tips_;
};

// 1.13.0 (correctifs F2) : LES COLONNES DE LA LISTE. Avant : Methode, Parametres, Retour, Visibilite, Genre, Origine
// (764 px) - la section n'en montre que 300 a 400 : la visibilite, le genre et l'origine ("redefinit T_Pompe.EnDefaut")
// restaient hors de vue, la methode privee ne paraissait pas privee. Maintenant : le nom, le cadenas (une colonne
// courte, rien pour une publique), le genre et l'origine, puis la signature (aussi au titre du corps et dans les
// proprietes) ; la ligne entiere en infobulle.
enum MethodColumn : std::size_t { ColName = 0, ColVisibility, ColKind, ColOrigin, ColParams, ColReturn, ColCount };

using PG = ui::PropertyGrid;
PG::Property prop(std::string name, std::string value, PG::ValueType t = PG::ValueType::ReadOnly,
                  std::function<bool(std::string_view)> commit = {}, std::vector<std::string> choices = {}, std::string help = {}) {
    PG::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = commit ? t : PG::ValueType::ReadOnly;
    p.commit = std::move(commit);
    p.enumValues = std::move(choices);
    p.description = std::move(help);
    return p;
}

constexpr const char* kPublic = "Publique";
constexpr const char* kPrivate = "Priv\xC3\xA9" "e";

} // namespace

// =============================================================================
//  La fenetre "Ajouter une methode..."
// =============================================================================
HmiMethodDialog::HmiMethodDialog(std::string id) : ui::Widget(std::move(id)) {
    const std::string base = this->id();
    name_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".nom")));
    return_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".retour")));
    return_->setPlaceholder("vide : une proc\xC3\xA9" "dure (sans retour)");
    choose_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Choisir\xE2\x80\xA6", base + ".choisir")));
    params_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".parametres")));
    params_->setPlaceholder("Consigne : REAL; VAR_IN_OUT Cible : T_Pompe");
    params_->setMonospace(true);
    private_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Priv\xC3\xA9" "e (vue des seules m\xC3\xA9thodes du type)", base + ".privee")));
    virtual_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Red\xC3\xA9" "finissable (virtuelle)", base + ".virtuelle")));
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    create_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Cr\xC3\xA9" "er la m\xC3\xA9thode", base + ".creer")));
    create_->setStyle(ui::Button::Style::Primary);
    for (auto* f : {name_, return_, params_}) links_ += f->textChanged->connect([this](const std::string&) { sync(); });
    links_ += private_->stateChanged->connect([this](ui::Checkbox::State) { sync(); });
    links_ += virtual_->stateChanged->connect([this](ui::Checkbox::State) { sync(); });
    links_ += cancel_->clicked->connect([this] { close(); });
    links_ += create_->clicked->connect([this] {
        if (problem().empty()) created->emit();
    });
    links_ += choose_->clicked->connect([this] {
        if (chooseReturn) chooseReturn();
    });
    setVisibility(ui::Visibility::Collapsed);
}

void HmiMethodDialog::open(const hmi::Project& p, Id type) {
    project_ = &p;
    type_ = type;
    const auto* t = p.hmiType(type);
    interface_ = t && t->kind == hmi::HmiTypeKind::Interface;
    std::string name = interface_ ? "Demarrer" : "Methode";
    if (t)
        for (int k = 2; k < 1000 && !nameProblem(p, *t, name, kNoId).empty(); ++k) name = (interface_ ? "Demarrer" : "Methode") + std::to_string(k);
    name_->setText(name);
    return_->setText({});
    params_->setText({});
    private_->setState(ui::Checkbox::State::Unchecked);
    virtual_->setState(ui::Checkbox::State::Unchecked);
    private_->setVisibility(interface_ ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    virtual_->setVisibility(interface_ ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    create_->setText(interface_ ? "Cr\xC3\xA9" "er la signature" : "Cr\xC3\xA9" "er la m\xC3\xA9thode");
    open_ = true;
    setVisibility(ui::Visibility::Visible);
    sync();
    layout();
    invalidate();
}

void HmiMethodDialog::close() {
    open_ = false;
    setVisibility(ui::Visibility::Collapsed);
    if (parent()) parent()->layout();
}

void HmiMethodDialog::setName(const std::string& s) { name_->setText(s); sync(); }
void HmiMethodDialog::setReturnType(const std::string& s) { return_->setText(s); sync(); }
void HmiMethodDialog::setParams(const std::string& s) { params_->setText(s); sync(); }
void HmiMethodDialog::setPrivate(bool on) { private_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked); sync(); }
void HmiMethodDialog::setVirtual(bool on) { virtual_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked); sync(); }
std::string HmiMethodDialog::name() const { return trimmedOf(name_->text()); }
std::string HmiMethodDialog::returnType() const { return trimmedOf(return_->text()); }
std::string HmiMethodDialog::params() const { return trimmedOf(params_->text()); }
bool HmiMethodDialog::isPrivate() const { return !interface_ && private_->isChecked(); }
bool HmiMethodDialog::isVirtual() const { return !interface_ && virtual_->isChecked(); }

std::string HmiMethodDialog::title() const {
    const auto* t = project_ ? project_->hmiType(type_) : nullptr;
    return std::string(interface_ ? "Nouvelle signature de " : "Nouvelle m\xC3\xA9thode de ") + (t ? t->name : std::string("?"));
}

std::string HmiMethodDialog::signature() const {
    const auto* t = project_ ? project_->hmiType(type_) : nullptr;
    if (!t) return {};
    hmi::HmiFunction m;
    m.name = name().empty() ? std::string("\xE2\x80\xA6") : name();
    m.returnType = returnType();
    std::string why;
    (void)hmi::setMethodParams(m, params(), [] { return Id{1}; }, &why);
    return hmi::methodSignature(t->name, m);
}

std::string HmiMethodDialog::problem() const {
    const auto* t = project_ ? project_->hmiType(type_) : nullptr;
    if (!t) return "type introuvable";
    if (const std::string w = nameProblem(*project_, *t, name(), kNoId); !w.empty()) return w;
    hmi::HmiFunction m;
    m.name = name();
    m.returnType = returnType();
    m.visibility = isPrivate() ? hmi::Visibility::Private : hmi::Visibility::Public;
    m.isVirtual = isVirtual();
    std::string why;
    if (!hmi::setMethodParams(m, params(), [] { return Id{1}; }, &why)) return why;
    if (std::string w = hmi::methodProblem(*project_, *t, m); !w.empty()) return w;
    return {};
}

void HmiMethodDialog::sync() {
    create_->setEnabled(open_ && problem().empty());
    invalidate();
}

gfx::Rect HmiMethodDialog::boxRect() const {
    const auto b = bounds();
    const float w = std::min(b.w - 24.f, 720.f);
    const float h = std::min(b.h - 24.f, 400.f);
    return {b.x + (b.w - w) * 0.5f, b.y + std::max(12.f, (b.h - h) * 0.3f), w, h};
}

void HmiMethodDialog::onLayout() {
    const auto r = boxRect();
    const float x = r.x + 16.f, w = r.w - 32.f;
    name_->setBounds({x, r.y + 70.f, w * 0.5f, 28.f});
    return_->setBounds({x, r.y + 126.f, w * 0.5f, 28.f});
    choose_->setBounds({x + w * 0.5f + 8.f, r.y + 126.f, 100.f, 28.f});
    params_->setBounds({x, r.y + 182.f, w, 28.f});
    private_->setBounds({x, r.y + 222.f, w * 0.5f, 24.f});
    virtual_->setBounds({x + w * 0.5f, r.y + 222.f, w * 0.5f, 24.f});
    create_->setBounds({r.right() - 16.f - 190.f, r.bottom() - 42.f, 190.f, 30.f});
    cancel_->setBounds({r.right() - 16.f - 190.f - 8.f - 100.f, r.bottom() - 42.f, 100.f, 30.f});
}

void HmiMethodDialog::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    ctx.r.fillRect(b, gfx::Color{0, 0, 0, 110});
    const auto r = boxRect();
    ctx.r.fillRoundedRect(r, c.panelBg, 6.f);
    ctx.r.strokeRect(r, c.accent, 1.f);
    ctx.r.fillRect({r.x, r.y, r.w, 38.f}, c.headerBg);
    ctx.r.drawText({r.x + 16.f, r.y + 11.f}, title(), f.uiBold, c.text);
    const float x = r.x + 16.f;
    ctx.r.drawText({x, r.y + 50.f}, "NOM", f.smallUi, c.textMuted);
    ctx.r.drawText({x, r.y + 106.f}, "RETOUR (FACULTATIF)", f.smallUi, c.textMuted);
    ctx.r.drawText({x, r.y + 162.f}, "PARAM\xC3\x88TRES (nom : type, s\xC3\xA9par\xC3\xA9s par ;)", f.smallUi, c.textMuted);
    ctx.r.drawText({x, r.y + 258.f}, "SIGNATURE", f.smallUi, c.textMuted);
    ctx.r.drawText({x, r.y + 278.f}, signature(), f.mono, c.text);
    const std::string w = problem();
    ctx.r.drawText({x, r.y + 304.f}, w.empty() ? std::string("\xE2\x9C\x93 pr\xC3\xAAte \xC3\xA0 cr\xC3\xA9" "er") : "\xE2\x9C\x97 " + w, f.smallUi, w.empty() ? c.ok : c.error);
    ctx.r.line({r.x, r.bottom() - 54.f}, {r.right(), r.bottom() - 54.f}, c.border, 1.f);
    ctx.r.drawText({x, r.bottom() - 34.f}, interface_ ? "Une signature n'a pas de corps. Ctrl+Z annule la cr\xC3\xA9" "ation."
                                                      : "Son corps s'ouvre pr\xC3\xA9rempli dans l'\xC3\xA9" "diteur. Ctrl+Z annule la cr\xC3\xA9" "ation.",
                   f.smallUi, c.textMuted);
}

ui::EventResult HmiMethodDialog::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        close();
        return ui::EventResult::Consumed;
    }
    if (std::get_if<ui::MouseDown>(&ev) || std::get_if<ui::MouseUp>(&ev) || std::get_if<ui::MouseWheel>(&ev)) return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}

// =============================================================================
//  La section Methodes
// =============================================================================
HmiMethodsPane::HmiMethodsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(AAdd, HmiGlyph::Plus, "Ajouter une m\xC3\xA9thode : son nom, son retour, ses param\xC3\xA8tres, Priv\xC3\xA9" "e, Red\xC3\xA9" "finissable (virtuelle)",
               "Ajouter une m\xC3\xA9thode\xE2\x80\xA6");
    tools->add(AOverride, HmiGlyph::Code, "Red\xC3\xA9" "finir la m\xC3\xA9thode h\xC3\xA9rit\xC3\xA9" "e choisie (une virtuelle du parent) : son corps pr\xC3\xA9rempli avec SUPER",
               "Red\xC3\xA9" "finir");
    tools->separator();
    tools->add(ADuplicate, HmiGlyph::Duplicate, "Dupliquer la m\xC3\xA9thode (Ctrl+D) : la copie juste apr\xC3\xA8s", "Dupliquer");
    tools->add(ADelete, HmiGlyph::Delete, "Supprimer la m\xC3\xA9thode (Ctrl+Z la rend)", "Supprimer");
    tools->add(AUp, HmiGlyph::Up, "Monter la m\xC3\xA9thode", "Monter");
    tools->add(ADown, HmiGlyph::Down, "Descendre la m\xC3\xA9thode", "Descendre");
    tools->separator();
    tools->add(ACompile, HmiGlyph::Check, "Compiler la m\xC3\xA9thode montr\xC3\xA9" "e (F7) : ses fautes soulign\xC3\xA9" "es dans son corps, \xC3\xA0 leur ligne", "Compiler (F7)");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(AAdd, [this] { return owner() != nullptr; });
    tools_->setEnabledWhen(AOverride, [this] {
        const auto* m = selectedInherited() ? current() : nullptr;
        return m && m->isVirtual;
    });
    tools_->setEnabledWhen(ADuplicate, [this] { return selectedMethod() != kNoId; });
    tools_->setEnabledWhen(ADelete, [this] { return selectedMethod() != kNoId; });
    tools_->setEnabledWhen(AUp, [this] { return selectedMethod() != kNoId && selectedRow_ > 0; });
    tools_->setEnabledWhen(ADown, [this] {
        return selectedMethod() != kNoId && selectedRow_ >= 0 && static_cast<std::size_t>(selectedRow_) + 1 < ownCount();
    });
    tools_->setEnabledWhen(ACompile, [this] { return selectedMethod() != kNoId; });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto left = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".left");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".listPanel", "M\xC3\x89THODES");
        auto table = std::make_unique<ui::TableView>(base + ".methods");
        table->setColumns({{"M\xC3\xA9thode", 120.f, 60.f}, {"", 30.f, 24.f}, {"Genre", 72.f, 40.f}, {"Origine", 170.f, 60.f},
                           {"Param\xC3\xA8tres", 150.f, 50.f}, {"Retour", 70.f, 40.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        table_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        tools_->bindListKeys(*table_, HmiToolStrip::ListKeys{ADelete, ADuplicate, AAdd, AUp, ADown, 0});
        listPanel_ = panel.get();
        left->addPane(std::move(panel), 0.58f, 80.f);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".propsPanel", "PROPRI\xC3\x89T\xC3\x89S DE LA M\xC3\x89THODE");
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
        grid->setShowDescriptionPane(false);
        grid->setNameColumnRatio(0.40f);
        props_ = &static_cast<ui::PropertyGrid&>(panel->setBody(std::move(grid)));
        propsPanel_ = panel.get();
        left->addPane(std::move(panel), 0.42f, 80.f);
    }
    split->addPane(std::move(left), 0.44f, 260.f);
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".editorPanel", "CORPS");
        auto column = std::make_unique<CodeColumn>(base + ".code");
        auto legend = std::make_unique<ui::StatusBar>(base + ".legende");
        legend->setTooltip("Ce que le corps d'une m\xC3\xA9thode conna\xC3\xAEt : SELF (l'instance), ses membres par leur nom, ses param\xC3\xA8tres, son retour.");
        legend_ = &static_cast<ui::StatusBar&>(column->addChild(std::move(legend)));
        auto ed = std::make_unique<ui::MultiLineText>(base + ".editor");
        ed->setLanguage(ui::Language::StructuredText);
        ed->setShowLineNumbers(true);
        ed->setReadOnly(true);
        ed->setTabInsertsSpaces(4);
        ed->setCommandKeys(true);
        editor_ = &static_cast<ui::MultiLineText&>(column->addChild(std::move(ed)));
        panel->setBody(std::move(column));
        editorPanel_ = panel.get();
        split->addPane(std::move(panel), 0.56f, 260.f);
    }
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedMethod();
        std::string why;
        switch (a) {
            case AAdd: if (!openAddDialog()) say("Choisis d'abord une structure ou une interface.", true); break;
            case AOverride:
                if (const Id made = overrideMethod(selectedRowMethod(), &why); made == kNoId) say("Refus\xC3\xA9 : " + why, true);
                break;
            case ADuplicate:
                if (sel && duplicateMethod(sel, &why) == kNoId) say("Refus\xC3\xA9 : " + why, true);
                break;
            case ADelete:
                if (sel && !deleteMethod(sel, &why)) say("Refus\xC3\xA9 : " + why, true);
                break;
            case AUp: if (sel) (void)moveMethod(sel, -1); break;
            case ADown: if (sel) (void)moveMethod(sel, 1); break;
            case ACompile: (void)compileCurrent(); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_) return;
        selectedRow_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        showSelected();
        status_->setMessage(statusText());   // 1.13.0 (correctifs F2) : l'appel de la methode choisie
    });
    links_ += editor_->textChanged->connect([this](const std::string& text) {
        if (syncing_) return;
        margin_.edited();                       // les points d'arret suivent leurs lignes
        if (const Id sel = selectedMethod()) (void)setBody(sel, text);
        updateDiagnostics();
    });
    // LA MARGE DES POINTS D'ARRET : un point dans le corps d'une methode l'arrete a chaque appel (la cible : la
    // methode ; le moteur lit sa FUNCTION interne, en-tete sur la ligne 1 - les lignes sont celles de l'editeur).
    HmiDebugSession::instance().bindProject(doc_);
    margin_.attach(*this, *editor_, [this] {
        const auto* t = owner();
        const auto* m = current();
        return m && t && t->kind != hmi::HmiTypeKind::Interface ? HmiDebugTarget{kNoId, m->id} : HmiDebugTarget{};
    }, [this](const std::string& t, bool warning) { say(t, warning); });
    // L'aide a la saisie : SELF (et THIS) du type, ses membres par leur nom, les parametres de la methode.
    assist_ = assist::sourcesFor(doc_);
    assist_.declarations = [this] {
        const auto* t = owner();
        const auto* m = current();
        if (!t || !m || t->kind == hmi::HmiTypeKind::Interface) return std::string{};
        const std::string full = hmi::fullName(*t);
        std::string s = "VAR_IN_OUT SELF : " + full + "; THIS : " + full + "; END_VAR ";
        const auto members = hmi::types::membersOf(doc_->project, full);
        if (!members.empty()) {
            s += "VAR ";
            for (const auto& mem : members) s += mem.name + " : " + mem.type + "; ";
            s += "END_VAR ";
        }
        return s + assist::declarationsPrefix(m->decls, hmi::decl::Role::Function);
    };
    assist::attach(*editor_, assist_);
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

const hmi::HmiType* HmiMethodsPane::owner() const {
    const auto* t = doc_->project.hmiType(type_);
    return t && (t->kind == hmi::HmiTypeKind::Structure || t->kind == hmi::HmiTypeKind::Interface) ? t : nullptr;
}

const hmi::HmiFunction* HmiMethodsPane::current() const { return hmi::methodById(doc_->project, selectedRowMethod()); }

std::size_t HmiMethodsPane::ownCount() const {
    return static_cast<std::size_t>(std::count_if(rows_.begin(), rows_.end(), [](const Row& r) { return r.own; }));
}

Id HmiMethodsPane::selectedRowMethod() const {
    if (selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= rows_.size()) return kNoId;
    return rows_[static_cast<std::size_t>(selectedRow_)].id;
}

Id HmiMethodsPane::selectedMethod() const {
    if (selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= rows_.size()) return kNoId;
    const auto& r = rows_[static_cast<std::size_t>(selectedRow_)];
    return r.own ? r.id : kNoId;
}

bool HmiMethodsPane::selectedInherited() const {
    return selectedRow_ >= 0 && static_cast<std::size_t>(selectedRow_) < rows_.size() && !rows_[static_cast<std::size_t>(selectedRow_)].own;
}

void HmiMethodsPane::setType(Id type) {
    if (type == type_) return;
    type_ = type;
    selectedRow_ = -1;
    refresh();
}

void HmiMethodsPane::selectMethod(Id id) {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id && id != kNoId) {
            hmiSelectModelRow(*table_, i);
            selectedRow_ = static_cast<int>(i);
            showSelected();
            return;
        }
}

void HmiMethodsPane::goTo(Id method, int line) {
    const hmi::HmiType* t = nullptr;
    if (hmi::methodById(doc_->project, method, &t) && t && t->id != type_) setType(t->id);
    selectMethod(method);
    if (line > 0) editor_->goToLine(static_cast<std::size_t>(line - 1));
}

void HmiMethodsPane::refresh() {
    const Id keep = selectedRowMethod();
    rows_.clear();
    std::vector<std::vector<std::string>> cells;
    std::vector<std::string> tips;
    const auto& p = doc_->project;
    const auto* t = owner();
    const bool iface = t && t->kind == hmi::HmiTypeKind::Interface;
    // 1.13.0 (correctifs F2) : une ligne dans l'ordre des colonnes (MethodColumn), et son infobulle.
    const auto row = [&](const hmi::HmiType& where, const hmi::HmiFunction& m, const std::string& kind, const std::string& origin) {
        std::vector<std::string> r(ColCount);
        const bool priv = !iface && m.visibility == hmi::Visibility::Private;
        r[ColName] = m.name;
        r[ColVisibility] = priv ? kPrivate : std::string{};     // le cadenas ; une publique : rien
        r[ColKind] = kind;
        r[ColOrigin] = origin;
        r[ColParams] = hmi::methodParamsText(m);
        r[ColReturn] = m.returnType.empty() ? std::string("\xE2\x80\x94") : m.returnType;
        cells.push_back(std::move(r));
        std::string tip = hmi::methodSignature(where.name, m) + "\n" + (iface ? std::string("signature d'interface")
                                                                             : std::string(priv ? "priv\xC3\xA9" "e (vue des seules m\xC3\xA9thodes du type)"
                                                                                                : "publique"));
        if (kind != "\xE2\x80\x94" && !iface) tip += " \xC2\xB7 " + kind;
        if (!iface) tip += " \xC2\xB7 " + origin;
        tips.push_back(std::move(tip));
    };
    if (t) {
        const std::string full = hmi::fullName(*t);
        const auto bases = hmi::baseChain(p, full);
        const auto origin = [&](const hmi::HmiFunction& m) -> std::string {
            for (const auto* b : bases)
                if (hmi::ownMethod(*b, m.name)) return "red\xC3\xA9" "finit " + b->name + "." + m.name;
            return "propre";
        };
        for (const auto& m : t->methods) {
            rows_.push_back({m.id, t->id, true});
            row(*t, m, iface ? std::string("signature") : m.isVirtual ? std::string("virtuelle") : std::string("\xE2\x80\x94"),
                iface ? std::string("propre") : origin(m));
        }
        if (!iface)
            for (const auto& om : hmi::allMethods(p, full)) {
                if (!om.owner || !om.method || om.owner->id == t->id) continue;
                const auto& m = *om.method;
                rows_.push_back({m.id, om.owner->id, false});
                row(*om.owner, m, m.isVirtual ? std::string("virtuelle") : std::string("\xE2\x80\x94"), "(h\xC3\xA9rit\xC3\xA9 de " + om.owner->name + ")");
            }
    }
    std::vector<bool> own, priv;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        own.push_back(rows_[i].own);
        priv.push_back(cells[i][ColVisibility] == kPrivate);
    }
    // 1.13.0 (relecture F2) : une interface n'a ni visibilite, ni genre, ni origine ("signature", "propre" sur
    // chaque ligne) : ces trois colonnes se cachent, Parametres suit la signature dans la liste etroite (avant : a
    // 392 px, hors de vue). Une structure les retrouve telles que l'utilisateur les avait laissees.
    if (iface != ifaceColumns_) {
        const std::size_t hidden[3] = {ColVisibility, ColKind, ColOrigin};
        for (std::size_t i = 0; i < 3; ++i) {
            if (iface) keptColumns_[i] = hidden[i] < table_->columns().size() && table_->columns()[hidden[i]].visible;
            table_->setColumnVisible(hidden[i], !iface && keptColumns_[i]);
        }
        ifaceColumns_ = iface;
    }
    model_ = std::make_shared<MethodRows>(
        std::vector<std::string>{"M\xC3\xA9thode", "", "Genre", "Origine", "Param\xC3\xA8tres", "Retour"}, std::move(cells),
        [own, priv](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            const bool mine = r < own.size() && own[r];
            if (c == ColName) {
                s.icon = ui::Icon::Function;
                s.bold = mine;
            }
            if (c == ColParams) s.monospace = true;
            if (c == ColVisibility && r < priv.size() && priv[r]) s.icon = ui::Icon::Lock;
            if (!mine || c == ColOrigin) s.fgTone = ui::Tone::Muted;
            return s;
        },
        [own, iface](std::size_t r, std::size_t c) {
            return r < own.size() && own[r] && (c == ColName || c == ColParams || c == ColReturn || (c == ColVisibility && !iface));
        },
        [](std::size_t, std::size_t c) {
            if (c == ColVisibility) return std::vector<std::string>{kPublic, kPrivate};
            return std::vector<std::string>{};
        },
        [this](std::size_t r, std::size_t c, const std::string& text) {
            if (r >= rows_.size() || !rows_[r].own) return false;
            const Id id = rows_[r].id;
            std::string why;
            bool ok = false;
            if (c == ColName) ok = renameMethod(id, text, &why);
            else if (c == ColParams) ok = setParams(id, text, &why);
            else if (c == ColReturn) ok = setReturnType(id, text == "\xE2\x80\x94" ? std::string{} : text, &why);
            else if (c == ColVisibility) ok = setPrivate(id, text == kPrivate, &why);
            if (!ok && !why.empty()) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        },
        std::move(tips));
    syncing_ = true;
    table_->setModel(model_);
    selectedRow_ = -1;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == keep && keep != kNoId) {
            hmiSelectModelRow(*table_, i);
            selectedRow_ = static_cast<int>(i);
        }
    if (selectedRow_ < 0 && !rows_.empty()) {
        hmiSelectModelRow(*table_, 0);
        selectedRow_ = 0;
    }
    syncing_ = false;
    if (t)
        listPanel_->setTitle(std::string(iface ? "SIGNATURES DE " : "M\xC3\x89THODES DE ") + upperOf(t->name) + " (" + std::to_string(rows_.size()) + ")");
    else
        listPanel_->setTitle("M\xC3\x89THODES");
    // 1.13.0 (correctifs F2) : une interface n'a que des signatures (pas de "proprietes de la methode").
    if (propsPanel_) propsPanel_->setTitle(iface ? "PROPRI\xC3\x89T\xC3\x89S DE LA SIGNATURE" : "PROPRI\xC3\x89T\xC3\x89S DE LA M\xC3\x89THODE");
    tools_->setText(AAdd, iface ? "Ajouter une signature : son nom, son retour, ses param\xC3\xA8tres (sans corps)"
                                : "Ajouter une m\xC3\xA9thode : son nom, son retour, ses param\xC3\xA8tres, Priv\xC3\xA9" "e, Red\xC3\xA9" "finissable (virtuelle)",
                    iface ? "Ajouter une signature\xE2\x80\xA6" : "Ajouter une m\xC3\xA9thode\xE2\x80\xA6");
    showSelected();
    status_->setMessage(statusText());
    invalidate();
}

// 1.13.0 (correctifs F2) : avant, "appel : Pompe1.Demarrer() dans un script..." etait ecrit en dur - pour tout type,
// meme T_PompeDoublee (dont l'instance est Pompe2). L'instance : une variable IHM de ce type ; sinon <instance>.
std::string HmiMethodsPane::callHint() const {
    const auto* t = owner();
    if (!t || t->kind == hmi::HmiTypeKind::Interface || rows_.empty()) return {};
    const hmi::HmiFunction* m = current();
    if (!m) m = hmi::methodById(doc_->project, rows_.front().id);
    if (!m) return {};
    const auto& p = doc_->project;
    std::string instance = "<instance>";
    for (const auto& v : p.programs.variables)
        if (p.hmiTypeByName(trimmedOf(v.type)) == t) {
            instance = v.name;
            break;
        }
    const bool params = !hmi::methodParamsText(*m).empty();
    const std::string call = m->name + (params ? "(\xE2\x80\xA6)" : "()");
    if (m->visibility == hmi::Visibility::Private)
        return "appel : " + call + " ou SELF." + call + " dans une m\xC3\xA9thode de " + t->name + " (priv\xC3\xA9" "e : pas d'un script)";
    return "appel : " + instance + "." + call + " dans un script, " + call + " ou SELF." + call + " dans une m\xC3\xA9thode";
}

std::string HmiMethodsPane::statusText() const {
    const auto* t = owner();
    if (!t) return "Choisis une structure ou une interface : ses m\xC3\xA9thodes.";
    const std::size_t mine = ownCount();
    if (t->kind == hmi::HmiTypeKind::Interface)
        return std::to_string(mine) + " signature(s) \xC2\xB7 une interface n'a ni membre ni corps ; un type qui l'impl\xC3\xA9mente \xC3\xA9" "crit chaque m\xC3\xA9thode";
    if (rows_.empty())
        return "Aucune m\xC3\xA9thode : Ajouter une m\xC3\xA9thode\xE2\x80\xA6 (elle voit SELF, les membres par leur nom, ses param\xC3\xA8tres).";
    return std::to_string(mine) + " m\xC3\xA9thode(s) propre(s), " + std::to_string(rows_.size() - mine) + " h\xC3\xA9rit\xC3\xA9" "e(s) \xC2\xB7 " + callHint();
}

std::string HmiMethodsPane::legendText() const {
    const auto* t = owner();
    const auto* m = current();
    if (!t) return {};
    if (t->kind == hmi::HmiTypeKind::Interface)
        return "Une interface n'a pas de corps : ses signatures seules - chaque type qui l'impl\xC3\xA9mente \xC3\xA9" "crit la m\xC3\xA9thode.";
    if (!m) return "SELF : l'instance de " + t->name + " (THIS vaut SELF) \xC2\xB7 ses membres se lisent par leur nom ou SELF.X";
    const hmi::HmiType* where = nullptr;
    (void)hmi::methodById(doc_->project, m->id, &where);
    std::string s = "SELF : l'instance de " + t->name + " (THIS vaut SELF) \xC2\xB7 ses membres se lisent par leur nom ou SELF.X";
    if (!m->returnType.empty()) s += " \xC2\xB7 rends par " + m->name + " := \xE2\x80\xA6";
    if (where && where->id != t->id) s += " \xC2\xB7 h\xC3\xA9rit\xC3\xA9" "e de " + where->name + " : lecture seule (Red\xC3\xA9" "finir)";
    return s;
}

std::string HmiMethodsPane::editorTitle() const {
    const auto* m = current();
    const hmi::HmiType* where = nullptr;
    if (m) (void)hmi::methodById(doc_->project, m->id, &where);
    if (!m || !where) return "CORPS";
    if (where->kind == hmi::HmiTypeKind::Interface) return "SIGNATURE  \xC2\xB7  " + hmi::methodSignature(where->name, *m);
    return "CORPS  \xC2\xB7  " + hmi::methodSignature(where->name, *m);
}

void HmiMethodsPane::showSelected() {
    const auto* m = current();
    const auto* t = owner();
    const bool iface = t && t->kind == hmi::HmiTypeKind::Interface;
    const bool mine = selectedMethod() != kNoId;
    syncing_ = true;
    const std::string body = m && !iface ? m->body : std::string{};
    const bool other = !m || m->id != shownId_;     // 1.13.0 (correctifs F2) : une autre methode montree
    if (editor_->text() != body) {
        if (m && m->id == shownId_) editor_->reloadText(body);
        else editor_->setText(body);
    }
    // 1.13.0 (correctifs F2) : "modifie" suit la methode montree. Avant, le mot de l'editeur (partage par toutes les
    // methodes) restait apres une saisie dans une autre : Demarrer apres la seule pose d'un point d'arret, EnDefaut
    // d'un autre type. Un point d'arret ne touche pas le corps.
    if (other) editor_->clearModified();
    shownId_ = m ? m->id : kNoId;
    margin_.reset();
    editor_->setReadOnly(!mine || iface);
    syncing_ = false;
    editorPanel_->setTitle(editorTitle());
    legend_->setMessage(legendText());
    rebuildProperties();
    updateDiagnostics();
}

void HmiMethodsPane::rebuildProperties() {
    const auto& p = doc_->project;
    std::vector<PG::Category> cats;
    const auto* t = owner();
    const auto* m = current();
    if (!t || !m) {
        PG::Category c;
        c.name = "M\xC3\xA9thodes";
        c.properties.push_back(prop("M\xC3\xA9thodes", t ? std::to_string(t->methods.size()) : std::string("\xE2\x80\x94"), PG::ValueType::ReadOnly, {}, {},
                                    "Ajouter une m\xC3\xA9thode\xE2\x80\xA6 : elle voit SELF (l'instance), les membres par leur nom et ses param\xC3\xA8tres."));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    const hmi::HmiType* where = nullptr;
    (void)hmi::methodById(p, m->id, &where);
    const bool iface = t->kind == hmi::HmiTypeKind::Interface;
    const Id id = m->id;
    PG::Category c;
    if (selectedInherited()) {
        c.name = "M\xC3\xA9thode h\xC3\xA9rit\xC3\xA9" "e";
        c.properties.push_back(prop("Nom", m->name));
        c.properties.push_back(prop("H\xC3\xA9rit\xC3\xA9 de", where ? where->name : std::string("?")));
        c.properties.push_back(prop("Signature", hmi::methodSignature(where ? where->name : t->name, *m)));
        c.properties.push_back(prop("Red\xC3\xA9" "finissable (virtuelle)", m->isVirtual ? "oui : Red\xC3\xA9" "finir" : "non (pas virtuelle)"));
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    c.name = iface ? "Signature" : "M\xC3\xA9thode";
    const auto commit = [this, id](std::function<bool(const std::string&, std::string*)> fn) {
        return [this, fn](std::string_view text) {
            std::string why;
            const bool ok = fn(std::string(text), &why);
            if (!ok && !why.empty()) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        };
    };
    c.properties.push_back(prop("Nom", m->name, PG::ValueType::Text,
                                commit([this, id](const std::string& s, std::string* w) { return renameMethod(id, s, w); }), {},
                                "Renommer : son corps, ses red\xC3\xA9" "finitions et les appels SELF.X(), X(), SUPER.X() des m\xC3\xA9thodes du type suivent."));
    auto ret = prop("Retour", m->returnType, PG::ValueType::Text,
                    commit([this, id](const std::string& s, std::string* w) { return setReturnType(id, s, w); }), {},
                    "Vide : une proc\xC3\xA9" "dure (Pompe1.Demarrer()). Un type : une fonction, elle rend sa valeur par " + m->name + " := \xE2\x80\xA6 ou RETURN.");
    if (typepicker::available())
        ret.dialog = [this, id] {
            const auto* mm = hmi::methodById(doc_->project, id);
            HmiTypePicker::Spec spec;
            spec.field = "Retour de " + (mm ? mm->name : std::string("la m\xC3\xA9thode"));
            spec.current = mm ? mm->returnType : std::string{};
            spec.use = hmi::typereg::UseVariable;
            spec.doc = doc_;
            typepicker::ask(std::move(spec), [this, id](const HmiTypePicker::Answer& a) {
                std::string why;
                if (!a.type.empty() && !setReturnType(id, a.type, &why)) say("Refus\xC3\xA9 : " + why, true);
            });
        };
    c.properties.push_back(std::move(ret));
    c.properties.push_back(prop("Param\xC3\xA8tres", hmi::methodParamsText(*m), PG::ValueType::Text,
                                commit([this, id](const std::string& s, std::string* w) { return setParams(id, s, w); }), {},
                                "nom : type, s\xC3\xA9par\xC3\xA9s par ; (Consigne : REAL; VAR_IN_OUT Cible : T_Pompe ; VAR_OUTPUT pour une sortie)."));
    if (!iface) {
        c.properties.push_back(prop("Visibilit\xC3\xA9", m->visibility == hmi::Visibility::Private ? kPrivate : kPublic, PG::ValueType::Enum,
                                    commit([this, id](const std::string& s, std::string* w) { return setPrivate(id, s == kPrivate, w); }),
                                    {kPublic, kPrivate},
                                    "Priv\xC3\xA9" "e : appel\xC3\xA9" "e seulement depuis une m\xC3\xA9thode du m\xC3\xAAme type ; ailleurs, G\xC3\xA9n\xC3\xA9rer la refuse."));
        c.properties.push_back(prop("Red\xC3\xA9" "finissable (virtuelle)", m->isVirtual ? "TRUE" : "FALSE", PG::ValueType::Boolean,
                                    commit([this, id](const std::string& s, std::string* w) {
                                        return setVirtual(id, s == "TRUE" || s == "true" || s == "1", w);
                                    }), {},
                                    "Coch\xC3\xA9" "e : un sous-type peut la red\xC3\xA9" "finir (m\xC3\xAAme nom, m\xC3\xAAme signature) ; SUPER." + m->name + "() y rappelle ce corps."));
    }
    c.properties.push_back(prop("Description", m->description, PG::ValueType::Text, [this, id](std::string_view s) {
        return setDescription(id, std::string(s));
    }));
    c.properties.push_back(prop("Signature", hmi::methodSignature(t->name, *m)));
    if (!iface) {
        c.properties.push_back(prop("Fonction interne", hmi::methodFunctionName(hmi::fullName(*t), *m), PG::ValueType::ReadOnly, {}, {},
                                    "Le nom que le moteur et le d\xC3\xA9" "bogueur donnent \xC3\xA0 la m\xC3\xA9thode (SELF y est un VAR_IN_OUT)."));
        std::string redef = "\xE2\x80\x94";
        for (const auto* b : hmi::baseChain(p, hmi::fullName(*t)))
            if (hmi::ownMethod(*b, m->name)) {
                redef = b->name + "." + m->name;
                break;
            }
        c.properties.push_back(prop("Red\xC3\xA9" "finit", redef));
        std::vector<std::string> by;
        for (const auto& sub : p.programs.types) {
            if (sub.id == t->id || !hmi::ownMethod(sub, m->name)) continue;
            for (const auto* b : hmi::baseChain(p, hmi::fullName(sub)))
                if (b->id == t->id) {
                    by.push_back(sub.name);
                    break;
                }
        }
        std::string list;
        for (const auto& s : by) list += (list.empty() ? "" : ", ") + s;
        c.properties.push_back(prop("Red\xC3\xA9" "finie par", list.empty() ? std::string("\xE2\x80\x94") : list));
    }
    cats.push_back(std::move(c));
    props_->setCategories(std::move(cats));
}

void HmiMethodsPane::updateDiagnostics() {
    diagnostics_.clear();
    const auto* m = current();
    const auto* t = owner();
    if (m && t && selectedMethod() != kNoId && t->kind != hmi::HmiTypeKind::Interface) {
        const int line = static_cast<int>(editor_->caretLine()) + 1;
        const Id id = m->id;
        const std::string prefix = t->name + "." + m->name;
        diagnostics_ = guardedDiagnostics(
            [&] {
                std::vector<hmi::ScriptDiagnostic> out;
                for (const auto& i : hmi::checkMethod(doc_->project, {}, id)) {
                    std::string msg = i.message;
                    // "T_Pompe.Demarrer, ligne 3 : ..." -> "..." (la ligne est marquee dans le corps).
                    if (msg.rfind(prefix, 0) == 0)
                        if (const auto colon = msg.find(" : ", prefix.size()); colon != std::string::npos) msg = msg.substr(colon + 3);
                    out.push_back({i.severity == hmi::Issue::Severity::Error     ? hmi::ScriptDiagnostic::Severity::Error
                                   : i.severity == hmi::Issue::Severity::Warning ? hmi::ScriptDiagnostic::Severity::Warning
                                                                                 : hmi::ScriptDiagnostic::Severity::Info,
                                   i.line, msg});
                }
                return out;
            },
            line, prefix);
        std::stable_sort(diagnostics_.begin(), diagnostics_.end(),
                         [](const hmi::ScriptDiagnostic& a, const hmi::ScriptDiagnostic& b) { return a.line < b.line; });
    }
    std::vector<std::pair<std::size_t, gfx::Color>> marks;
    for (const auto& d : diagnostics_)
        if (d.line > 0 && d.severity != hmi::ScriptDiagnostic::Severity::Info)
            marks.emplace_back(static_cast<std::size_t>(d.line - 1), d.severity == hmi::ScriptDiagnostic::Severity::Error
                                                                         ? gfx::Color{231, 76, 60, 255}
                                                                         : gfx::Color{241, 196, 15, 255});
    editor_->setMarkedLines(std::move(marks));
    editor_->setSquiggles(squigglesOf(diagnostics_));
    if (!diagnostics_.empty() && m) {
        const auto& d = diagnostics_.front();
        status_->setMessage(m->name + " : " + std::to_string(diagnostics_.size()) + " faute(s) - "
                                + (d.line > 0 ? "ligne " + std::to_string(d.line) + " : " : std::string{}) + d.message,
                            ui::StatusBar::Severity::Warning);
    }
}

std::size_t HmiMethodsPane::compileCurrent() {
    updateDiagnostics();
    const auto* m = current();
    if (!m || selectedMethod() == kNoId) {
        say("Compiler : choisis une m\xC3\xA9thode du type.", true);
        return 0;
    }
    if (diagnostics_.empty()) say(m->name + " : rien \xC3\xA0 redire (sa forme et son corps se lisent).");
    return diagnostics_.size();
}

void HmiMethodsPane::say(std::string text, bool warning) {
    if (warning) ui::PropertyGrid::refuse(text);
    message_ = std::move(text);
    status_->setMessage(message_, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
}

// ---------------------------------------------------------------- les gestes ---
bool HmiMethodsPane::change(Id method, std::string label,
                            const std::function<bool(hmi::Project&, hmi::HmiType&, hmi::HmiFunction&, std::string*)>& edit, std::string* why,
                            std::string mergeKey) {
    hmi::Project trial = doc_->project;
    hmi::HmiType* tt = nullptr;
    hmi::HmiFunction* tm = findMethod(trial, method, &tt);
    if (!tm || !tt) return fail(why, "m\xC3\xA9thode introuvable");
    if (!edit(trial, *tt, *tm, why)) return false;
    if (const std::string w = newProblem(doc_->project, trial); !w.empty()) return fail(why, w);
    bool ok = true;
    auto cmd = hmi::changeProject(doc_, std::move(label), [&](hmi::Project& p) {
        hmi::HmiType* t = nullptr;
        if (hmi::HmiFunction* m = findMethod(p, method, &t); m && t) ok = edit(p, *t, *m, nullptr);
    }, std::move(mergeKey));
    if (cmd) apply_(std::move(cmd));
    return ok;
}

Id HmiMethodsPane::addMethod(const std::string& rawName, const std::string& returnType, const std::string& params, bool isPrivate, bool isVirtual,
                             std::string* why) {
    const auto* t = owner();
    if (!t) return fail(why, "choisis une structure ou une interface"), kNoId;
    const bool iface = t->kind == hmi::HmiTypeKind::Interface;
    const std::string name = trimmedOf(rawName);
    if (const std::string w = nameProblem(doc_->project, *t, name, kNoId); !w.empty()) return fail(why, w), kNoId;
    hmi::HmiFunction draft;
    draft.name = name;
    draft.returnType = trimmedOf(returnType);
    draft.visibility = isPrivate && !iface ? hmi::Visibility::Private : hmi::Visibility::Public;
    draft.isVirtual = isVirtual && !iface;
    std::string w;
    if (!hmi::setMethodParams(draft, params, [] { return Id{0}; }, &w)) return fail(why, w), kNoId;
    if (std::string mp = hmi::methodProblem(doc_->project, *t, draft); !mp.empty()) return fail(why, mp), kNoId;
    const Id typeId = t->id;
    const std::string typeName = t->name;
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, std::string(iface ? "Nouvelle signature " : "Nouvelle m\xC3\xA9thode ") + typeName + "." + name, [&](hmi::Project& p) {
        auto* x = p.hmiType(typeId);
        if (!x) return;
        hmi::HmiFunction m = draft;
        m.id = p.allocate();
        for (auto& d : m.decls) d.id = p.allocate();
        if (!iface) m.body = hmi::methodTemplate(p, typeName, m);
        made = m.id;
        x->methods.push_back(std::move(m));
    });
    if (cmd) apply_(std::move(cmd));
    if (made != kNoId) {
        refresh();
        selectMethod(made);
        say(std::string(iface ? "Signature ajout\xC3\xA9" "e : " : "M\xC3\xA9thode ajout\xC3\xA9" "e : ") + hmi::methodSignature(typeName, draft)
            + (iface ? std::string{} : " - son corps est dans l'\xC3\xA9" "diteur") + " (Ctrl+Z la retire)");
    }
    return made;
}

bool HmiMethodsPane::renameMethod(Id id, const std::string& raw, std::string* why) {
    const hmi::HmiType* t = nullptr;
    const auto* m = hmi::methodById(doc_->project, id, &t);
    if (!m || !t) return fail(why, "m\xC3\xA9thode introuvable");
    const std::string name = trimmedOf(raw);
    if (name == m->name) return true;
    if (const std::string w = nameProblem(doc_->project, *t, name, id); !w.empty()) return fail(why, w);
    const std::string before = m->name;
    hmi::Project trial = doc_->project;
    (void)hmi::renameMethodEverywhere(trial, id, name);
    if (const std::string w = newProblem(doc_->project, trial); !w.empty()) return fail(why, w);
    auto cmd = hmi::changeProject(doc_, "Renommer la m\xC3\xA9thode " + t->name + "." + before + " en " + name,
                                  [&](hmi::Project& p) { (void)hmi::renameMethodEverywhere(p, id, name); });
    if (cmd) apply_(std::move(cmd));
    say("M\xC3\xA9thode renomm\xC3\xA9" "e : " + before + " \xE2\x86\x92 " + name + " (son corps, ses red\xC3\xA9" "finitions et les appels des m\xC3\xA9thodes du type suivent ; "
        "les appels \xC3\xA9" "crits ailleurs gardent l'ancien nom et Compiler les signale ; Ctrl+Z l'annule)");
    return true;
}

Id HmiMethodsPane::duplicateMethod(Id id, std::string* why) {
    const hmi::HmiType* t = nullptr;
    const auto* m = hmi::methodById(doc_->project, id, &t);
    if (!m || !t) return fail(why, "m\xC3\xA9thode introuvable"), kNoId;
    std::string name = m->name + "_copie";
    for (int k = 2; k < 1000 && !nameProblem(doc_->project, *t, name, kNoId).empty(); ++k) name = m->name + "_copie" + std::to_string(k);
    const Id typeId = t->id;
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Dupliquer la m\xC3\xA9thode " + t->name + "." + m->name, [&](hmi::Project& p) {
        hmi::HmiType* x = nullptr;
        const hmi::HmiFunction* src = findMethod(p, id, &x);
        if (!src || !x || x->id != typeId) return;
        hmi::HmiFunction copy = *src;
        copy.id = p.allocate();
        copy.name = name;
        for (auto& d : copy.decls) d.id = p.allocate();
        made = copy.id;
        const auto at = std::find_if(x->methods.begin(), x->methods.end(), [&](const hmi::HmiFunction& f) { return f.id == id; });
        x->methods.insert(at == x->methods.end() ? x->methods.end() : at + 1, std::move(copy));
    });
    if (cmd) apply_(std::move(cmd));
    if (made != kNoId) {
        refresh();
        selectMethod(made);
        say("M\xC3\xA9thode dupliqu\xC3\xA9" "e : " + name + " (Ctrl+Z la retire)");
    }
    return made;
}

bool HmiMethodsPane::deleteMethod(Id id, std::string* why) {
    const hmi::HmiType* t = nullptr;
    const auto* m = hmi::methodById(doc_->project, id, &t);
    if (!m || !t) return fail(why, "m\xC3\xA9thode introuvable");
    const std::string label = t->name + "." + m->name;
    auto cmd = hmi::changeProject(doc_, "Supprimer la m\xC3\xA9thode " + label, [&](hmi::Project& p) {
        for (auto& x : p.programs.types)
            x.methods.erase(std::remove_if(x.methods.begin(), x.methods.end(), [&](const hmi::HmiFunction& f) { return f.id == id; }),
                            x.methods.end());
    });
    if (cmd) apply_(std::move(cmd));
    say("M\xC3\xA9thode supprim\xC3\xA9" "e : " + label + " (Ctrl+Z la rend)");
    return true;
}

bool HmiMethodsPane::moveMethod(Id id, int delta) {
    const hmi::HmiType* t = nullptr;
    if (!hmi::methodById(doc_->project, id, &t) || !t) return false;
    const auto it = std::find_if(t->methods.begin(), t->methods.end(), [&](const hmi::HmiFunction& f) { return f.id == id; });
    const auto index = static_cast<std::ptrdiff_t>(it - t->methods.begin());
    const auto target = index + delta;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(t->methods.size())) return false;
    const Id typeId = t->id;
    auto cmd = hmi::changeProject(doc_, delta < 0 ? "Monter la m\xC3\xA9thode" : "Descendre la m\xC3\xA9thode", [&](hmi::Project& p) {
        if (auto* x = p.hmiType(typeId)) std::swap(x->methods[static_cast<std::size_t>(index)], x->methods[static_cast<std::size_t>(target)]);
    });
    if (cmd) apply_(std::move(cmd));
    refresh();
    selectMethod(id);
    return true;
}

bool HmiMethodsPane::setPrivate(Id id, bool isPrivate, std::string* why) {
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m) return fail(why, "m\xC3\xA9thode introuvable");
    const bool already = m->visibility == hmi::Visibility::Private;
    if (already == isPrivate) return true;
    const std::string name = m->name;
    const bool ok = change(id, std::string(isPrivate ? "Rendre priv\xC3\xA9" "e la m\xC3\xA9thode " : "Rendre publique la m\xC3\xA9thode ") + name,
                           [isPrivate](hmi::Project&, hmi::HmiType& t, hmi::HmiFunction& f, std::string* w) {
                               if (t.kind == hmi::HmiTypeKind::Interface) return fail(w, "une signature d'interface est publique");
                               f.visibility = isPrivate ? hmi::Visibility::Private : hmi::Visibility::Public;
                               return true;
                           }, why);
    if (ok) say(std::string("M\xC3\xA9thode ") + name + (isPrivate ? " priv\xC3\xA9" "e : seules les m\xC3\xA9thodes du type l'appellent" : " publique") + " (Ctrl+Z l'annule)");
    return ok;
}

bool HmiMethodsPane::setVirtual(Id id, bool on, std::string* why) {
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m) return fail(why, "m\xC3\xA9thode introuvable");
    if (m->isVirtual == on) return true;
    const std::string name = m->name;
    const bool ok = change(id, std::string(on ? "Rendre red\xC3\xA9" "finissable " : "Rendre non red\xC3\xA9" "finissable ") + name,
                           [on](hmi::Project&, hmi::HmiType& t, hmi::HmiFunction& f, std::string* w) {
                               if (t.kind == hmi::HmiTypeKind::Interface) return fail(w, "une signature d'interface n'est ni virtuelle ni priv\xC3\xA9" "e");
                               f.isVirtual = on;
                               return true;
                           }, why);
    if (ok) say(std::string("M\xC3\xA9thode ") + name + (on ? " red\xC3\xA9" "finissable (virtuelle)" : " non red\xC3\xA9" "finissable") + " (Ctrl+Z l'annule)");
    return ok;
}

bool HmiMethodsPane::setReturnType(Id id, const std::string& type, std::string* why) {
    const std::string next = trimmedOf(type);
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m) return fail(why, "m\xC3\xA9thode introuvable");
    if (m->returnType == next) return true;
    return change(id, "Retour de " + m->name + (next.empty() ? std::string(" : aucun") : " : " + next),
                  [next](hmi::Project&, hmi::HmiType&, hmi::HmiFunction& f, std::string*) {
                      f.returnType = next;
                      return true;
                  }, why);
}

bool HmiMethodsPane::setParams(Id id, const std::string& text, std::string* why) {
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m) return fail(why, "m\xC3\xA9thode introuvable");
    if (hmi::methodParamsText(*m) == trimmedOf(text)) return true;
    return change(id, "Param\xC3\xA8tres de " + m->name, [text](hmi::Project& p, hmi::HmiType&, hmi::HmiFunction& f, std::string* w) {
        return hmi::setMethodParams(f, text, [&p] { return p.allocate(); }, w);
    }, why);
}

bool HmiMethodsPane::setDescription(Id id, const std::string& text) {
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m || m->description == text) return false;
    return change(id, "Description de " + m->name, [text](hmi::Project&, hmi::HmiType&, hmi::HmiFunction& f, std::string*) {
        f.description = text;
        return true;
    }, nullptr);
}

bool HmiMethodsPane::setBody(Id id, const std::string& body) {
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m || m->body == body) return false;
    auto cmd = hmi::changeProject(doc_, "Saisie dans la m\xC3\xA9thode " + m->name, [&](hmi::Project& p) {
        if (auto* x = findMethod(p, id)) x->body = body;
    }, "method:" + std::to_string(id));
    if (cmd) apply_(std::move(cmd));
    return true;
}

Id HmiMethodsPane::overrideMethod(Id inherited, std::string* why) {
    const auto* t = owner();
    const hmi::HmiType* from = nullptr;
    const auto* pm = hmi::methodById(doc_->project, inherited, &from);
    if (!t || !pm || !from) return fail(why, "choisis une m\xC3\xA9thode h\xC3\xA9rit\xC3\xA9" "e"), kNoId;
    if (from->id == t->id) return fail(why, pm->name + " est d\xC3\xA9j\xC3\xA0 une m\xC3\xA9thode de " + t->name), kNoId;
    if (!pm->isVirtual) return fail(why, from->name + "." + pm->name + " n'est pas virtuelle (VIRTUAL) : rien \xC3\xA0 red\xC3\xA9" "finir"), kNoId;
    if (hmi::ownMethod(*t, pm->name)) return fail(why, t->name + " red\xC3\xA9" "finit d\xC3\xA9j\xC3\xA0 " + pm->name), kNoId;
    const Id typeId = t->id;
    const std::string typeName = t->name;
    const hmi::HmiFunction parent = *pm;
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, "Red\xC3\xA9" "finir " + from->name + "." + pm->name + " dans " + typeName, [&](hmi::Project& p) {
        auto* x = p.hmiType(typeId);
        if (!x) return;
        hmi::HmiFunction m;
        m.id = p.allocate();
        m.name = parent.name;
        m.returnType = parent.returnType;
        m.visibility = parent.visibility;
        m.isVirtual = true;                  // encore redefinissable par un sous-type
        for (const auto& d : parent.decls)
            if (d.kind == hmi::DeclKind::Parameter) {
                hmi::Declaration copy = d;
                copy.id = p.allocate();
                m.decls.push_back(std::move(copy));
            }
        m.body = hmi::overrideTemplate(p, typeName, parent);
        made = m.id;
        x->methods.push_back(std::move(m));
    });
    if (cmd) apply_(std::move(cmd));
    if (made != kNoId) {
        refresh();
        selectMethod(made);
        say("Red\xC3\xA9" "finition de " + parent.name + " dans " + typeName + " : son corps rappelle SUPER." + parent.name + "() (Ctrl+Z la retire)");
    }
    return made;
}

void HmiMethodsPane::setDialogWidget(HmiMethodDialog* d) {
    dialog_ = d;
    if (!dialog_) return;
    links_ += dialog_->created->connect([this] {
        std::string why;
        if (createFromDialog(&why) == kNoId && !why.empty()) say("Refus\xC3\xA9 : " + why, true);
    });
    dialog_->chooseReturn = [this] {
        if (!dialog_ || !typepicker::available()) return;
        HmiTypePicker::Spec spec;
        spec.field = "Retour de la m\xC3\xA9thode";
        spec.current = dialog_->returnType();
        spec.use = hmi::typereg::UseVariable;
        spec.doc = doc_;
        typepicker::ask(std::move(spec), [this](const HmiTypePicker::Answer& a) {
            if (dialog_ && !a.type.empty()) dialog_->setReturnType(a.type);
        });
    };
}

bool HmiMethodsPane::openAddDialog() {
    if (!owner()) return false;
    if (!dialog_) return false;
    dialog_->open(doc_->project, type_);
    return true;
}

Id HmiMethodsPane::createFromDialog(std::string* why) {
    if (!dialog_ || !dialog_->isOpen()) return fail(why, "la fen\xC3\xAAtre n'est pas ouverte"), kNoId;
    if (const std::string w = dialog_->problem(); !w.empty()) return fail(why, w), kNoId;
    const Id made = addMethod(dialog_->name(), dialog_->returnType(), dialog_->params(), dialog_->isPrivate(), dialog_->isVirtual(), why);
    if (made != kNoId) dialog_->close();
    return made;
}

// ------------------------------------------------------------- la mise en page ---
ui::EventResult HmiMethodsPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F7 && k->mods.none()) {
        (void)compileCurrent();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void HmiMethodsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 34.f});
    status_->setBounds({b.x, b.y + b.h - 24.f, b.w, 24.f});
    split_->setBounds({b.x, b.y + 34.f, b.w, std::max(0.f, b.h - 58.f)});
}

void HmiMethodsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
