// =============================================================================
//  app/hmi/HmiMethodPanes.cpp - la section Methodes d'un type IHM (1.13.0, J1b)
// =============================================================================
#include "HmiMethodPanes.hpp"

#include "HmiAssetPanes.hpp"     // hmiSelectModelRow
#include "HmiIcons.hpp"
#include "HmiScriptPanes.hpp"    // guardedDiagnostics, squigglesOf
#include "HmiTypePicker.hpp"     // "Choisir..." : le selecteur de types
#include "HmiUnitPanes.hpp"      // 1.50.0 : l'unite du retour (comme une fonction)
#include "HmiInitModel.hpp"      // 1.50.1 : l'invite d'une case vide d'Essayer (iv::zeroOf)
#include "HmiMethodFollow.hpp"   // 1.50.1 : la signature d'une virtuelle suivie par ses redefinitions
#include "../../hmi/HmiFunctionTests.hpp"   // 1.50.1 : Essayer sur le banc des Tests (l'instance d'essai)
#include "../../hmi/HmiRoles.hpp"           // 1.50.1 : l'invite d'une case vide (defaultInvite)
#include "../../hmi/HmiRuntime.hpp"         // 1.50.1 : Essayer
#include "../../hmi/HmiExpr.hpp"            // 1.50.1 : Essayer - un argument, une pose lus comme une case
#include "../../hmi/HmiSymbols.hpp"         // 1.50.1 : cellValue, shownCell, shownLiteral
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiDeclEdit.hpp"
#include "../../hmi/HmiDecl.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;

namespace {

// L'infobulle de la legende au-dessus du corps (1.13.1 : la legende du moment la precede).
constexpr const char* kLegendTip = "Ce que le corps d'une m\xC3\xA9thode conna\xC3\xAEt : SELF (l'instance), ses membres par leur nom, ses param\xC3\xA8tres, son retour.";

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
// 1.50.1 : `overload` (creer une methode) - un nom deja pris fait une SURCHARGE dans une structure, sauf d'une virtuelle
// (`isVirtual` : la nouvelle l'est) ; renommer, dupliquer : le nom doit etre libre (des appels changeraient de cible).
std::string nameProblem(const hmi::Project& p, const hmi::HmiType& t, const std::string& name, Id self, bool overload = false,
                        bool isVirtual = false) {
    if (name.empty()) return "donne un nom";
    if (!hmi::isIdentifier(name)) return "nom invalide : lettres, chiffres et _, pas de chiffre en t\xC3\xAAte";
    for (const auto& m : t.methods) {
        if (m.id == self || !sameText(m.name, name)) continue;
        if (!overload || t.kind != hmi::HmiTypeKind::Structure) return t.name + " a d\xC3\xA9j\xC3\xA0 une m\xC3\xA9thode " + m.name;
        if (m.isVirtual || isVirtual)
            return name + (m.isVirtual ? " est virtuelle" : " existe d\xC3\xA9j\xC3\xA0") + " : une m\xC3\xA9thode virtuelle ne se surcharge pas (ses "
                   "red\xC3\xA9" "finitions la d\xC3\xA9signent par son nom)";
    }
    for (const auto& mem : hmi::types::membersOf(p, hmi::fullName(t)))
        if (sameText(mem.name, name)) return name + " est d\xC3\xA9j\xC3\xA0 un membre de " + t.name + " (un membre et une m\xC3\xA9thode ne partagent pas un nom)";
    return {};
}

// La colonne du corps (l'onglet Code) : la legende au-dessus, le bandeau de l'ancien format (1.50.0 : des VAR dans le
// corps - " Migrer ce code "), l'editeur dessous.
class CodeColumn final : public ui::Widget {
public:
    explicit CodeColumn(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        if (children().size() < 2) return;
        constexpr float kLegend = 24.f;
        children()[0]->setBounds({b.x, b.y, b.w, kLegend});
        float y = b.y + kLegend + 2.f;
        if (children().size() >= 3) {
            const auto* banner = dynamic_cast<const HmiDeclBanner*>(children()[2].get());
            const float bh = banner && banner->shown() ? HmiDeclBanner::kHeight : 0.f;
            children()[2]->setBounds({b.x, y, b.w, bh});
            y += bh;
        }
        children()[1]->setBounds({b.x, y, b.w, std::max(0.f, b.y + b.h - y)});
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
// 1.50.0 (planche 01) : Tests, en dernier - le nombre de cas et leur dernier verdict ("3 \xE2\x9C\x93", "2 \xC2\xB7 1 \xE2\x9C\x95").
enum MethodColumn : std::size_t { ColName = 0, ColVisibility, ColKind, ColOrigin, ColParams, ColReturn, ColTests, ColCount };

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
//  La section Methodes
// =============================================================================
HmiMethodsPane::HmiMethodsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(AAdd, HmiGlyph::Plus, "Ajouter une m\xC3\xA9thode : la fen\xC3\xAAtre des fonctions (nom, g\xC3\xA9n\xC3\xA9riques, retour, unit\xC3\xA9, port\xC3\xA9" "e, virtuelle, description)",
               "Ajouter une m\xC3\xA9thode\xE2\x80\xA6");
    tools->add(AOverride, HmiGlyph::Code, "Red\xC3\xA9" "finir la m\xC3\xA9thode h\xC3\xA9rit\xC3\xA9" "e choisie (une virtuelle du parent) : son corps pr\xC3\xA9rempli avec SUPER",
               "Red\xC3\xA9" "finir");
    // 1.50.1 : comme le volet Fonctions - une methode du meme nom, d'autres parametres (chaque appel prend la sienne).
    tools->add(AOverload, HmiGlyph::Duplicate,
               "Nouvelle surcharge : une m\xC3\xA9thode du m\xC3\xAAme nom que la m\xC3\xA9thode choisie, avec d'autres param\xC3\xA8tres "
               "(chaque appel prend la sienne)",
               "Nouvelle surcharge");
    tools->separator();
    tools->add(ADuplicate, HmiGlyph::Duplicate, "Dupliquer la m\xC3\xA9thode (Ctrl+D) : la copie juste apr\xC3\xA8s", "Dupliquer");
    tools->add(ADelete, HmiGlyph::Delete, "Supprimer la m\xC3\xA9thode (Ctrl+Z la rend)", "Supprimer");
    tools->add(AUp, HmiGlyph::Up, "Monter la m\xC3\xA9thode", "Monter");
    tools->add(ADown, HmiGlyph::Down, "Descendre la m\xC3\xA9thode", "Descendre");
    tools->separator();
    tools->add(ACompile, HmiGlyph::Check, "Compiler la m\xC3\xA9thode montr\xC3\xA9" "e (F7) : ses fautes soulign\xC3\xA9" "es dans son corps, \xC3\xA0 leur ligne", "Compiler (F7)");
    // 1.50.1 : comme une fonction - des arguments, le resultat et SELF apres, sur une instance d'essai du type.
    tools->add(ATry, HmiGlyph::Play,
               "Essayer la m\xC3\xA9thode : des arguments, le r\xC3\xA9sultat et SELF apr\xC3\xA8s (une instance d'essai du type, sans toucher le projet)",
               "Essayer");
    // 1.50.0 : comme le volet Fonctions - tous les cas des methodes du type, chacun sur une instance d'essai.
    tools->add(ARunTests, HmiGlyph::Play, "Lancer les tests de toutes les m\xC3\xA9thodes du type (chaque cas part d'une instance d'essai) : les verdicts au panneau du bas",
               "Lancer les tests");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(AAdd, [this] { return owner() != nullptr; });
    tools_->setEnabledWhen(AOverride, [this] {
        const auto* m = selectedInherited() ? current() : nullptr;
        return m && m->isVirtual;
    });
    // 1.24.0 : plusieurs methodes choisies - Supprimer les retire toutes (un Ctrl+Z) ; le reste : une a la fois.
    const auto one = [this] { return !table_ || table_->selectedModelRows().size() <= 1; };
    tools_->setEnabledWhen(ADuplicate, [this, one] { return selectedMethod() != kNoId && one(); });
    tools_->setEnabledWhen(ADelete, [this] { return selectedMethod() != kNoId; });
    tools_->setLabelWhen(ADelete, [this] { return several::label("Supprimer", table_ ? table_->selectedModelRows().size() : 0); });
    tools_->setEnabledWhen(AUp, [this, one] { return selectedMethod() != kNoId && selectedRow_ > 0 && one(); });
    tools_->setEnabledWhen(ADown, [this, one] {
        return selectedMethod() != kNoId && selectedRow_ >= 0 && static_cast<std::size_t>(selectedRow_) + 1 < ownCount() && one();
    });
    tools_->setEnabledWhen(ACompile, [this, one] { return selectedMethod() != kNoId && one(); });
    // 1.50.1 : une methode propre d'une structure (une signature d'interface n'a ni corps ni surcharge).
    const auto structure = [this] {
        const auto* t = owner();
        return t && t->kind == hmi::HmiTypeKind::Structure;
    };
    tools_->setEnabledWhen(AOverload, [this, one, structure] {
        const auto* m = selectedMethod() != kNoId ? current() : nullptr;
        return m && !m->isVirtual && structure() && one();
    });
    tools_->setEnabledWhen(ATry, [this, one, structure] { return selectedMethod() != kNoId && structure() && one(); });
    tools_->setEnabledWhen(ARunTests, [this] {
        const auto* t = owner();
        return t && std::any_of(t->methods.begin(), t->methods.end(), [](const hmi::HmiFunction& m) { return !m.tests.empty(); });
    });

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto left = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".left");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".listPanel", "M\xC3\x89THODES");
        auto table = std::make_unique<ui::TableView>(base + ".methods");
        // 1.13.1 (lot K4, captures) : Retour se lit (le gras, les marges, le tri) - avant : "Re..." sur une interface.
        const float retour = std::max(70.f, std::ceil(ui::measureWidth("Retour", gfx::FontId{16}) * 1.08f) + 58.f);
        table->setColumns({{"M\xC3\xA9thode", 120.f, 60.f}, {"", 30.f, 24.f}, {"Genre", 72.f, 40.f}, {"Origine", 170.f, 60.f},
                           {"Param\xC3\xA8tres", 150.f, 50.f}, {"Retour", retour, 40.f}, {"Tests", 64.f, 40.f}});
        table->setSelectionMode(ui::SelectionMode::Extended);   // 1.24.0 : plusieurs methodes a supprimer d'un coup
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
        auto panel = std::make_unique<HmiTitledPanel>(base + ".editorPanel", "\xC3\x89" "DITEUR");
        auto column = std::make_unique<CodeColumn>(base + ".code");
        auto legend = std::make_unique<ui::StatusBar>(base + ".legende");
        legend->setTooltip(kLegendTip);
        legend_ = &static_cast<ui::StatusBar&>(column->addChild(std::move(legend)));
        auto ed = std::make_unique<ui::MultiLineText>(base + ".editor");
        ed->setLanguage(ui::Language::StructuredText);
        ed->setShowLineNumbers(true);
        ed->setReadOnly(true);
        ed->setTabInsertsSpaces(4);
        ed->setCommandKeys(true);
        editor_ = &static_cast<ui::MultiLineText&>(column->addChild(std::move(ed)));
        banner_ = &static_cast<HmiDeclBanner&>(column->addChild(std::make_unique<HmiDeclBanner>(base + ".declBanner")));
        // 1.50.0 (planche 01) : LES CINQ ONGLETS D'UNE FONCTION (le meme composant que le volet Fonctions).
        const auto made = makeFunctionTabs(*panel, base, doc_, apply_, std::move(column), banner_);
        codeTabs_ = made.tabs;
        testsPage_ = made.tests;
        editorPanel_ = panel.get();
        split->addPane(std::move(panel), 0.56f, 260.f);
    }
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedMethod();
        std::string why;
        switch (a) {
            case AAdd:
                if (!owner()) say("Choisis d'abord une structure ou une interface.", true);
                else if (!openAddDialog()) {               // sans hote (un essai) : une methode d'office, comme le volet Fonctions
                    HmiNewFunctionDialog::Answer a;
                    a.name = "Methode";
                    for (int k = 2; k < 1000 && !nameProblem(doc_->project, *owner(), a.name, kNoId).empty(); ++k) a.name = "Methode" + std::to_string(k);
                    if (createMethod(a, &why) == kNoId) say("Refus\xC3\xA9" "e : " + why, true);
                }
                break;
            case AOverride:
                if (const Id made = overrideMethod(selectedRowMethod(), &why); made == kNoId) say("Refus\xC3\xA9 : " + why, true);
                break;
            case ADuplicate:
                if (sel && duplicateMethod(sel, &why) == kNoId) say("Refus\xC3\xA9 : " + why, true);
                break;
            case ADelete: {
                // 1.24.0 : plusieurs methodes choisies - d'un coup, un seul Ctrl+Z ; une heritee reste (celle du parent).
                auto picked = table_->selectedModelRows();
                std::sort(picked.begin(), picked.end());
                if (picked.size() > 1) {
                    several::Batch b;
                    b.noun = "m\xC3\xA9thode";
                    b.nouns = "m\xC3\xA9thodes";
                    b.feminine = true;
                    std::vector<Id> ids;
                    for (const auto r : picked)
                        if (r < rows_.size()) {
                            const hmi::HmiType* owner = nullptr;
                            const auto* m = hmi::methodById(doc_->project, rows_[r].id, &owner);
                            b.names.push_back(m ? m->name : std::string("?"));
                            ids.push_back(rows_[r].own ? rows_[r].id : kNoId);
                        }
                    b.remove = [this, ids](std::size_t i, std::string* reason) {
                        if (ids[i] == kNoId) {
                            if (reason) *reason = "h\xC3\xA9rit\xC3\xA9" "e : elle se supprime dans son type";
                            return false;
                        }
                        return deleteMethod(ids[i], reason);
                    };
                    b.report = [this](const std::string& text, bool warning) { say(text, warning); };
                    b.alive = tools_->triggered;
                    several::run(std::move(b));
                    break;
                }
                if (sel && !deleteMethod(sel, &why)) say("Refus\xC3\xA9 : " + why, true);
                break;
            }
            case AUp: if (sel) (void)moveMethod(sel, -1); break;
            case ADown: if (sel) (void)moveMethod(sel, 1); break;
            case ACompile: (void)compileCurrent(); break;
            case ARunTests: (void)runTests(); break;
            case AOverload:                                    // 1.50.1
                if (sel && addOverload(sel, &why) == kNoId && !why.empty()) say("Surcharge refus\xC3\xA9" "e : " + why, true);
                break;
            case ATry:                                         // 1.50.1 : la fenetre des arguments (l'hote), sinon tout de suite
                if (!sel) break;
                if (tryHosts_.ask) tryHosts_.ask(sel);
                else (void)tryMethod(sel, {});
                break;
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
    // 1.50.0 : les grilles parlent dans la barre du volet ; leurs utilisations menent au code ; le bandeau migre.
    links_ += codeTabs_->message->connect([this](const std::string& t, bool warning) { say(t, warning); });
    links_ += codeTabs_->usesRequested->connect([this](const std::string& name) {
        showCodeTab(CodeTabCode);
        std::string said;
        const bool ok = selectNextUse(*editor_, name, &said);
        say(said, !ok);
    });
    links_ += codeTabs_->useRequested->connect([this](const std::string& name, int k) {
        showCodeTab(CodeTabCode);
        std::string said;
        const bool ok = selectNextUse(*editor_, name, &said, k);
        say(said, !ok);
    });
    links_ += codeTabs_->migrateRequested->connect([this] { (void)migrateCurrent(); });
    links_ += testsPage_->message->connect([this](const std::string& t, bool) { message_ = t; });
    links_ += testsPage_->countsChanged->connect([this] { refreshTabs(); });
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
    if (line > 0) {
        showCodeTab(CodeTabCode);          // 1.50.0 : la ligne est dans l'onglet Code
        editor_->goToLine(static_cast<std::size_t>(line - 1));
    }
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
        if (m.tests.empty()) {
            r[ColTests] = "\xE2\x80\x94";
        } else {
            const auto tl = testruns::tally(p, m);
            const std::size_t bad = tl.failed + tl.errors;
            r[ColTests] = bad ? std::to_string(tl.passed) + " \xC2\xB7 " + std::to_string(bad) + " \xE2\x9C\x95"
                        : tl.passed == m.tests.size() ? std::to_string(m.tests.size()) + " \xE2\x9C\x93"
                                                      : std::to_string(m.tests.size());
        }
        cells.push_back(std::move(r));
        std::string tip = hmi::methodSignature(where.name, m) + "\n" + (iface ? std::string("signature d'interface")
                                                                             : std::string(priv ? "priv\xC3\xA9" "e (vue des seules m\xC3\xA9thodes du type)"
                                                                                                : "publique"));
        if (kind != "\xE2\x80\x94" && !iface) tip += " \xC2\xB7 " + kind;
        if (const auto n = iface ? 0 : hmi::ownMethods(where, m.name).size(); n > 1) {   // 1.50.1 : "surcharge 2 sur 3"
            std::size_t k = 0;
            for (const auto* o : hmi::ownMethods(where, m.name)) {
                ++k;
                if (o->id == m.id) break;
            }
            tip += " " + std::to_string(k) + " sur " + std::to_string(n) + " (chaque appel prend la sienne par ses arguments)";
        }
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
            // 1.50.1 : une surcharge (plusieurs methodes de ce nom dans le type) le dit dans la colonne Genre.
            const bool overloaded = !iface && hmi::ownMethods(*t, m.name).size() > 1;
            row(*t, m, iface ? std::string("signature") : m.isVirtual ? std::string("virtuelle") : overloaded ? std::string("surcharge")
                                                                                                            : std::string("\xE2\x80\x94"),
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
    std::vector<int> tests;                       // 1.50.0 : 0 rien / pas joue, 1 tout reussi, 2 un echec
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        own.push_back(rows_[i].own);
        priv.push_back(cells[i][ColVisibility] == kPrivate);
        const std::string& tc = cells[i][ColTests];
        tests.push_back(tc.find("\xE2\x9C\x95") != std::string::npos ? 2 : tc.find("\xE2\x9C\x93") != std::string::npos ? 1 : 0);
    }
    // 1.13.0 (relecture F2) : une interface n'a ni visibilite, ni genre, ni origine ("signature", "propre" sur
    // chaque ligne) : ces trois colonnes se cachent, Parametres suit la signature dans la liste etroite (avant : a
    // 392 px, hors de vue). Une structure les retrouve telles que l'utilisateur les avait laissees.
    // 1.50.0 : Tests aussi (une signature n'a pas de cas).
    if (iface != ifaceColumns_) {
        const std::size_t hidden[4] = {ColVisibility, ColKind, ColOrigin, ColTests};
        for (std::size_t i = 0; i < 4; ++i) {
            if (iface) keptColumns_[i] = hidden[i] < table_->columns().size() && table_->columns()[hidden[i]].visible;
            table_->setColumnVisible(hidden[i], !iface && keptColumns_[i]);
        }
        ifaceColumns_ = iface;
    }
    model_ = std::make_shared<MethodRows>(
        std::vector<std::string>{"M\xC3\xA9thode", "", "Genre", "Origine", "Param\xC3\xA8tres", "Retour", "Tests"}, std::move(cells),
        [own, priv, tests](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            const bool mine = r < own.size() && own[r];
            if (c == ColName) {
                s.icon = ui::Icon::Function;
                s.bold = mine;
            }
            if (c == ColParams) s.monospace = true;
            if (c == ColTests && r < tests.size()) s.fgTone = tests[r] == 2 ? ui::Tone::Error : tests[r] == 1 ? ui::Tone::Ok : ui::Tone::Muted;
            if (c == ColVisibility && r < priv.size() && priv[r]) s.icon = ui::Icon::Lock;
            if (!mine || c == ColOrigin) s.fgTone = ui::Tone::Muted;
            return s;
        },
        [own, iface](std::size_t r, std::size_t c) {
            // 1.50.0 : les parametres se declarent dans l'onglet Parametres (plus dans une case de texte).
            return r < own.size() && own[r] && (c == ColName || c == ColReturn || (c == ColVisibility && !iface));
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
        return "Pas de corps : chaque type qui l'impl\xC3\xA9mente \xC3\xA9" "crit la m\xC3\xA9thode.";   // 1.13.1 : tient sur la ligne
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
    // 1.13.1 (lot K4, captures) : une legende coupee ("...") se lit entiere dans son infobulle.
    legend_->setTooltip(legendText().empty() ? std::string(kLegendTip) : legendText() + "\n" + kLegendTip);
    rebuildProperties();
    updateDiagnostics();
    refreshTabs();                         // 1.50.0 : Parametres (SELF en tete), Locales, Constantes, Tests
}

// ---- 1.50.0 (planche 01) : les cinq onglets ----
std::optional<hmi::decledit::Place> HmiMethodsPane::currentPlace() const {
    const auto* t = owner();
    const Id id = selectedMethod();
    const auto* m = id != kNoId ? hmi::methodById(doc_->project, id) : nullptr;
    if (!t || !m) return std::nullopt;
    hmi::decledit::Place at;
    at.kind = hmi::decledit::Place::Kind::TypeMethod;
    at.type = t->id;
    at.id = id;
    at.label = "m\xC3\xA9thode " + t->name + "." + m->name;
    return at;
}

void HmiMethodsPane::refreshTabs() {
    if (!codeTabs_ || !testsPage_) return;
    const auto* t = owner();
    const bool iface = t && t->kind == hmi::HmiTypeKind::Interface;
    const auto at = currentPlace();
    const std::string why = !t                   ? "Choisis une structure ou une interface."
                          : selectedInherited() ? "H\xC3\xA9rit\xC3\xA9" "e : ses d\xC3\xA9" "clarations se changent dans son type (Red\xC3\xA9" "finir pour la sienne)."
                                                : "Choisis une m\xC3\xA9thode du type.";
    codeTabs_->setPlace(at, why);
    // Une signature d'interface n'a que ses parametres (ni corps, ni locales, ni constantes, ni cas) : les autres onglets
    // se cachent (la planche : " Parametres seul ").
    if (iface && at) {
        localsGrid().setPlace(std::nullopt, "Une signature n'a pas de locales : seulement ses param\xC3\xA8tres.");
        constantsGrid().setPlace(std::nullopt, "Une signature n'a pas de constantes : seulement ses param\xC3\xA8tres.");
    }
    for (const std::size_t tab : {std::size_t{CodeTabCode}, std::size_t{CodeTabLocals}, std::size_t{CodeTabConstants}, std::size_t{CodeTabTests}})
        if (tab < codeTabs_->tabCount() && codeTabs_->tabHidden(tab) != iface) codeTabs_->setTabHidden(tab, iface);
    if (iface && codeTabs_->currentIndex() != CodeTabParameters) codeTabs_->setCurrentIndex(CodeTabParameters);
    testsPage_->setFunction(kNoId, at && !iface ? at->id : kNoId);
    if (codeTabs_->tabCount() > CodeTabTests) {
        const std::size_t n = testsPage_->caseCount();
        codeTabs_->setTabBadge(CodeTabTests, n ? std::to_string(n) : std::string{}, testsPage_->badgeTone());
    }
}

void HmiMethodsPane::showCodeTab(std::size_t tab) {
    if (codeTabs_ && tab < codeTabs_->tabCount()) codeTabs_->setCurrentIndex(tab);
}

std::size_t HmiMethodsPane::currentCodeTab() const noexcept { return codeTabs_ ? codeTabs_->currentIndex() : 0; }

bool HmiMethodsPane::migrateCurrent() {
    const auto at = currentPlace();
    if (!at) {
        say("Migrer : aucune m\xC3\xA9thode choisie.", true);
        return false;
    }
    std::string report;
    const bool ok = migrateOne(doc_, apply_, *at, &report);
    say(report, !ok);
    return ok;
}

void HmiMethodsPane::setTestHosts(HmiFunctionTestsPage::Hosts h) {
    if (testsPage_) testsPage_->setHosts(std::move(h));
}

bool HmiMethodsPane::showTestCase(Id test) {
    const auto* t = owner();
    const hmi::HmiType* where = nullptr;
    for (const auto& ty : doc_->project.programs.types)
        for (const auto& m : ty.methods)
            for (const auto& c : m.tests)
                if (c.id == test) where = &ty;
    if (!where) return false;
    if (!t || t->id != where->id) setType(where->id);
    for (const auto& m : where->methods)
        for (const auto& c : m.tests)
            if (c.id == test) {
                selectMethod(m.id);
                showCodeTab(CodeTabTests);
                testsPage_->selectCase(test);
                return true;
            }
    return false;
}

std::size_t HmiMethodsPane::runTests() {
    const auto* t = owner();
    if (!t || std::none_of(t->methods.begin(), t->methods.end(), [](const hmi::HmiFunction& m) { return !m.tests.empty(); })) {
        say("Aucun cas \xC3\xA0 lancer : l'onglet Tests d'une m\xC3\xA9thode, \xC2\xAB Nouveau cas\xE2\x80\xA6 \xC2\xBB.", true);
        return 0;
    }
    const auto& hosts = testsPage_->hosts();
    campaign_ = runTestsOf(doc_->project, t->methods, kNoId, t->name, hosts.plc ? hosts.plc() : nullptr);
    if (hosts.output) hosts.output(campaign_.lines);
    say(campaign_.summary() + (hosts.output ? std::string("  \xC2\xB7  le d\xC3\xA9tail au panneau du bas (Sorties)") : std::string{}),
        campaign_.failed());
    refresh();
    return campaign_.tally.passed;
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
    // 1.50.0 (planche 01) : les parametres, les locales et les constantes se declarent dans leurs onglets (comme une fonction).
    std::size_t nParams = 0, nLocals = 0, nConsts = 0;
    for (const auto& d : m->decls) {
        if (d.kind == hmi::DeclKind::Parameter) ++nParams;
        else if (d.kind == hmi::DeclKind::Constant) ++nConsts;
        else ++nLocals;
    }
    c.properties.push_back(prop("Param\xC3\xA8tres", std::to_string(nParams) + " (onglet Param\xC3\xA8tres)", PG::ValueType::ReadOnly, {}, {},
                                hmi::methodParamsText(*m).empty() ? std::string("Aucun : SELF seulement.") : hmi::methodParamsText(*m)));
    if (!iface) {
        c.properties.push_back(prop("Locales", std::to_string(nLocals) + " (onglet Locales)"));
        c.properties.push_back(prop("Constantes", std::to_string(nConsts) + " (onglet Constantes)"));
    }
    if (!m->generics.empty()) c.properties.push_back(prop("G\xC3\xA9n\xC3\xA9riques", hmi::genericsText(m->generics)));
    if (!m->returnUnit.empty()) c.properties.push_back(prop("Unit\xC3\xA9 du retour", m->returnUnit));
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
        c.properties.push_back(prop("Fonction interne", hmi::methodFunctionName(*t, *m), PG::ValueType::ReadOnly, {}, {},
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
    // 1.50.1 : un nom deja pris fait une surcharge (ses parametres, donnes ici, la distinguent) - pas d'une virtuelle.
    if (const std::string w = nameProblem(doc_->project, *t, name, kNoId, true, isVirtual && !iface); !w.empty()) return fail(why, w), kNoId;
    hmi::HmiFunction draft;
    draft.name = name;
    draft.returnType = trimmedOf(returnType);
    draft.visibility = isPrivate && !iface ? hmi::Visibility::Private : hmi::Visibility::Public;
    draft.isVirtual = isVirtual && !iface;
    std::string w;
    if (!hmi::setMethodParams(draft, params, [] { return Id{0}; }, &w)) return fail(why, w), kNoId;
    if (std::string mp = hmi::methodProblem(doc_->project, *t, draft); !mp.empty()) return fail(why, mp), kNoId;
    {
        hmi::HmiType with = *t;                        // 1.50.1 : la meme forme qu'une autre surcharge - refusee ici
        hmi::HmiFunction probe = draft;
        probe.id = doc_->project.nextId;
        with.methods.push_back(probe);
        if (std::string clash = hmi::methodOverloadProblem(with, probe); !clash.empty()) return fail(why, clash), kNoId;
    }
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

// 1.50.1 : une virtuelle redefinie dans des sous-types - le geste fait sur une copie, puis ses redefinitions suivent
// (followSignature : la question qui les nomme, une commande). Follow::None : rien a suivre, le geste d'avant.
Follow HmiMethodsPane::followed(Id id, const std::string& label,
                                const std::function<bool(hmi::Project&, hmi::HmiFunction&, std::string*)>& edit, std::string* why) {
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m || !m->isVirtual || hmi::overridesOf(doc_->project, id).empty()) return Follow::None;
    hmi::Project trial = doc_->project;
    hmi::HmiFunction* tm = findMethod(trial, id);
    if (!tm) return Follow::None;
    std::string w;
    if (!edit(trial, *tm, &w)) {
        if (why) *why = w;
        return Follow::Refused;
    }
    const std::weak_ptr<int> alive = alive_;
    const auto f = followSignature(doc_, apply_, id, trial, label, why, [this, alive] {
        if (!alive.expired()) refresh();
    });
    return f;
}

bool HmiMethodsPane::setReturnType(Id id, const std::string& type, std::string* why) {
    const std::string next = trimmedOf(type);
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m) return fail(why, "m\xC3\xA9thode introuvable");
    if (m->returnType == next) return true;
    const std::string label = "Retour de " + m->name + (next.empty() ? std::string(" : aucun") : " : " + next);
    // 1.50.1 : une virtuelle redefinie - le retour de ses redefinitions suit (avant : refuse, elles changeaient de signature).
    if (const auto f = followed(id, label, [next](hmi::Project&, hmi::HmiFunction& g, std::string*) {
            g.returnType = next;
            if (next.empty()) g.returnUnit.clear();
            return true;
        }, why); f != Follow::None)
        return f == Follow::Done;
    return change(id, label,
                  [next](hmi::Project&, hmi::HmiType&, hmi::HmiFunction& f, std::string*) {
                      f.returnType = next;
                      return true;
                  }, why);
}

bool HmiMethodsPane::setParams(Id id, const std::string& text, std::string* why) {
    const auto* m = hmi::methodById(doc_->project, id);
    if (!m) return fail(why, "m\xC3\xA9thode introuvable");
    if (hmi::methodParamsText(*m) == trimmedOf(text)) return true;
    const std::string label = "Param\xC3\xA8tres de " + m->name;
    // 1.50.1 : une virtuelle redefinie - les parametres de ses redefinitions suivent.
    if (const auto f = followed(id, label, [text](hmi::Project& p, hmi::HmiFunction& g, std::string* w) {
            return hmi::setMethodParams(g, text, [&p] { return p.allocate(); }, w);
        }, why); f != Follow::None)
        return f == Follow::Done;
    return change(id, label, [text](hmi::Project& p, hmi::HmiType&, hmi::HmiFunction& f, std::string* w) {
        return hmi::setMethodParams(f, text, [&p] { return p.allocate(); }, w);
    }, why);
}

// 1.50.1 : NOUVELLE SURCHARGE - le meme nom, son retour, sa portee ; l'onglet Parametres s'ouvre.
Id HmiMethodsPane::addOverload(Id of, std::string* why) {
    const hmi::HmiType* t = nullptr;
    const auto* m = hmi::methodById(doc_->project, of, &t);
    if (!m || !t) return fail(why, "m\xC3\xA9thode introuvable"), kNoId;
    if (t->kind != hmi::HmiTypeKind::Structure) return fail(why, "une interface n'a qu'une signature par nom"), kNoId;
    if (m->isVirtual)
        return fail(why, m->name + " est virtuelle : une m\xC3\xA9thode virtuelle ne se surcharge pas (ses red\xC3\xA9" "finitions la d\xC3\xA9signent par son nom)"),
               kNoId;
    if (t->id != type_) setType(t->id);
    HmiNewFunctionDialog::Answer a;
    a.name = m->name;
    a.returnType = m->returnType;
    a.returnUnit = m->returnUnit;
    a.description = m->description;
    a.pub = m->visibility != hmi::Visibility::Private;
    a.isVirtual = false;
    return createMethod(a, why);
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

// ---- 1.50.1 : ESSAYER (comme une fonction), sur une instance d'essai du type ----
std::vector<HmiMethodsPane::TryField> HmiMethodsPane::tryFields(Id id) const {
    std::vector<TryField> out;
    const hmi::HmiType* t = nullptr;
    const auto* m = hmi::methodById(doc_->project, id, &t);
    if (!m || !t || t->kind != hmi::HmiTypeKind::Structure) return out;
    const auto& p = doc_->project;
    const auto generic = [&](const std::string& type) {
        for (const auto* gens : {&m->generics, &t->generics})
            for (const auto& g : *gens)
                if (sameText(g.name, trimmedOf(type))) return true;
        return false;
    };
    const auto parts = functionDeclarations(*m);
    for (const auto* in : parts.parameters()) {
        using S = hmi::LocalVar::Section;
        TryField f;
        f.name = in->name;
        f.label = in->name + " : " + in->type
                + (in->section == S::InOut ? " (E/S : sa valeur de d\xC3\xA9part)" : in->section == S::Output ? " (sortie : rendue apr\xC3\xA8s l'essai)" : "");
        const std::string fallback = !in->initial.empty() ? hmi::shownCell(in->initial)
                                     : generic(in->type) ? std::string{}
                                                         : hmi::shownLiteral(initvalue::zeroOf(p, in->type));
        f.placeholder = in->section == S::Output ? std::string("rendue par la m\xC3\xA9thode")
                                                 : hmi::roles::defaultInvite(fallback.empty() ? std::string("vide") : fallback);
        out.push_back(std::move(f));
    }
    // SELF avant : un membre par case (les simples ; une structure, un tableau, une liste partent de leur valeur initiale).
    for (const auto& mem : hmi::types::membersOf(p, hmi::fullName(*t))) {
        if (!hmi::types::isElementary(hmi::types::normalized(mem.type)) && !p.hmiTypeByName(trimmedOf(mem.type))) continue;
        if (const auto* e = p.hmiTypeByName(trimmedOf(mem.type)); e && e->kind != hmi::HmiTypeKind::Enumeration) continue;
        TryField f;
        f.name = mem.name;
        f.member = true;
        f.label = "SELF." + mem.name + " : " + mem.type + " (avant)";
        const std::string init = !mem.initial.empty() ? hmi::shownCell(mem.initial) : hmi::shownLiteral(initvalue::zeroOf(p, mem.type));
        f.placeholder = hmi::roles::defaultInvite(init.empty() ? std::string("vide") : init, "sa valeur initiale");
        out.push_back(std::move(f));
    }
    return out;
}

bool HmiMethodsPane::tryWithAnswers(Id id, const std::vector<std::string>& answers) {
    const auto fields = tryFields(id);
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> before;
    for (std::size_t i = 0; i < fields.size() && i < answers.size(); ++i) {
        if (!fields[i].member) args.push_back(answers[i]);
        else if (!trimmedOf(answers[i]).empty()) before.emplace_back(fields[i].name, answers[i]);
    }
    return tryMethod(id, args, before);
}

bool HmiMethodsPane::tryMethod(Id id, const std::vector<std::string>& arguments, const std::vector<std::pair<std::string, std::string>>& selfBefore) {
    trial_ = {};
    trial_.method = id;
    const hmi::HmiType* t = nullptr;
    const auto* m = hmi::methodById(doc_->project, id, &t);
    const auto report = [this] {
        if (tryHosts_.output) tryHosts_.output(trialLines());
        if (trial_.ok) {
            std::string said = "Essai : " + trial_.call + (trial_.result.empty() ? std::string(" (sans retour)") : " = " + trial_.result);
            said += trial_.changed.empty() ? std::string("  \xC2\xB7  SELF inchang\xC3\xA9") : "  \xC2\xB7  " + trial_.changed.front()
                                                                                           + (trial_.changed.size() > 1 ? " (+" + std::to_string(trial_.changed.size() - 1) + ")" : std::string{});
            say(said + "  \xC2\xB7  le projet n'est pas touch\xC3\xA9");
        } else {
            say("Essai : " + (trial_.call.empty() ? std::string("la m\xC3\xA9thode") : trial_.call) + " \xC3\xA9" "choue : " + trial_.error, true);
        }
        return trial_.ok;
    };
    if (!m || !t) {
        trial_.error = "m\xC3\xA9thode introuvable";
        return report();
    }
    trial_.call = t->name + "." + m->name + "()";
    hmi::MethodBench bench;
    std::string why;
    if (!hmi::methodBench(doc_->project, *m, bench, &why)) {
        trial_.error = why;
        return report();
    }
    const hmi::Project& q = bench.project;
    const auto plc = trialPlc(tryHosts_.plc ? tryHosts_.plc() : nullptr);
    hmi::Runtime rt;
    rt.bind(&q, plc.get());
    rt.prime(0.0);
    // Une valeur tapee, lue comme une case dans son type (2,5 -> 2.5 ; VRAI -> TRUE ; =formule), puis evaluee sur le banc.
    const auto valueOf = [&](const std::string& type, const std::string& text, sim::Value& v, std::string& shown, std::string& err) {
        const auto cell = hmi::cellValue(&q, type, text);
        if (!cell.ok()) {
            err = cell.why;
            return false;
        }
        const auto e = hmi::Expression::compile(hmi::benchText(bench, cell.text, false));
        if (!e.valid()) {
            std::string rest;
            (void)hmi::splitLine(e.error(), &rest);
            err = text + " illisible (" + hmi::frenchSimMessage(rest.empty() ? e.error() : rest) + ")";
            return false;
        }
        auto r = e.evaluate(rt.environment());
        if (!r) {
            err = text + " ne s'\xC3\xA9value pas (" + hmi::frenchSimMessage(r.error().message()) + ")";
            return false;
        }
        v = *r;
        shown = cell.text;
        return true;
    };
    // SELF avant : chaque pose ecrit son membre sur l'instance d'essai (apres la remise a l'initiale, avant l'appel).
    const auto members = hmi::types::membersOf(q, bench.typeName);
    for (const auto& [rawName, value] : selfBefore) {
        std::string name = trimmedOf(rawName);
        if (const auto dot = name.find('.'); dot != std::string::npos && (sameText(name.substr(0, dot), "SELF") || sameText(name.substr(0, dot), "THIS")))
            name = name.substr(dot + 1);
        const hmi::TypeMember* mem = nullptr;
        for (const auto& x : members)
            if (sameText(x.name, name)) mem = &x;
        if (!mem) {
            trial_.error = "SELF." + name + " : " + t->name + " n'a pas ce membre";
            return report();
        }
        sim::Value v;
        std::string shown, err;
        if (!valueOf(mem->type, value, v, shown, err)) {
            trial_.error = "SELF." + mem->name + " : " + err;
            return report();
        }
        if (!rt.environment().write(bench.self + "." + mem->name, v)) {
            trial_.error = "SELF." + mem->name + " : ne s'\xC3\xA9" "crit pas";
            return report();
        }
        trial_.before.push_back(mem->name + " := " + shown);
    }
    // SELF et les variables IHM avant l'appel (pour dire ce qui a change).
    std::vector<std::pair<std::string, std::string>> selfWas, varsWas;
    for (const auto& x : members)
        if (sim::Value v; rt.environment().read(bench.self + "." + x.name, v)) selfWas.emplace_back(x.name, trialValueText(v));
    for (const auto& v : q.programs.variables)
        if (v.name != bench.self)
            if (const auto* val = rt.variable(v.name)) varsWas.emplace_back(v.name, hmi::formatValue(*val));
    // Les arguments : un par parametre (dans l'ordre), une sortie n'en demande pas.
    const auto parts = functionDeclarations(bench.call);
    const auto inputs = parts.parameters();
    std::vector<std::pair<std::string, sim::Value>> args;
    std::string shownArgs;
    for (std::size_t k = 0; k < arguments.size(); ++k) {
        const std::string text = trimmedOf(arguments[k]);
        if (text.empty()) continue;
        if (k >= inputs.size()) {
            trial_.error = "trop d'arguments (" + std::to_string(arguments.size()) + " pour " + std::to_string(inputs.size()) + ")";
            return report();
        }
        if (inputs[k]->section == hmi::LocalVar::Section::Output) continue;
        std::string asType = inputs[k]->type;              // un parametre generique (T) : sans type, le moteur le deduit
        for (const auto* gens : {&m->generics, &t->generics})
            for (const auto& g : *gens)
                if (sameText(g.name, trimmedOf(asType))) asType.clear();
        sim::Value v;
        std::string shown, err;
        if (!valueOf(asType, text, v, shown, err)) {
            trial_.error = inputs[k]->name + " : " + err;
            return report();
        }
        args.emplace_back(inputs[k]->name, v);
        shownArgs += (shownArgs.empty() ? "" : ", ") + inputs[k]->name + " := " + shown;
    }
    trial_.call = t->name + "." + m->name + "(" + shownArgs + ")";
    sim::Value result;
    std::vector<std::pair<std::string, sim::Value>> outputs;
    trial_.ok = rt.runFunction(bench.call.id, args, result, &why, &outputs);
    if (!trial_.ok) {
        // le message du banc, dans les mots de la methode
        for (std::size_t at = why.find(bench.self); at != std::string::npos; at = why.find(bench.self, at + 4)) why.replace(at, bench.self.size(), "SELF");
        trial_.error = why;
        return report();
    }
    if (!m->returnType.empty()) {
        trial_.result = trialValueText(result);
        trial_.type = m->returnType;
    }
    for (const auto& [name, value] : outputs) {
        const hmi::LocalVar* prm = parts.local(name);
        const bool inOut = prm && prm->section == hmi::LocalVar::Section::InOut;
        trial_.outputs.push_back(name + (inOut ? " (E/S)" : " (sortie)") + " = " + trialValueText(value));
    }
    for (const auto& x : members) {
        sim::Value v;
        if (!rt.environment().read(bench.self + "." + x.name, v)) continue;
        const std::string now = trialValueText(v);
        trial_.self.push_back(x.name + " = " + now);
        for (const auto& [n, was] : selfWas)
            if (n == x.name && was != now) trial_.changed.push_back("SELF." + x.name + " : " + was + "  \xE2\x86\x92  " + now);
    }
    for (const auto& [name, was] : varsWas)
        if (const auto* now = rt.variable(name); now && hmi::formatValue(*now) != was)
            trial_.changed.push_back(name + " : " + was + "  \xE2\x86\x92  " + hmi::formatValue(*now));
    for (const auto& e : rt.journal())
        if (e.kind != "Syst\xC3\xA8me" && e.kind != "Erreur") trial_.journal.push_back(e.kind == "Journal" ? e.message : e.kind + " : " + e.message);
    return report();
}

std::vector<std::pair<hmi::pipeline::Severity, std::string>> HmiMethodsPane::trialLines() const {
    using Sev = hmi::pipeline::Severity;
    std::vector<std::pair<Sev, std::string>> out;
    const hmi::HmiType* t = nullptr;
    (void)hmi::methodById(doc_->project, trial_.method, &t);
    const std::string where = t ? " (une instance d'essai de " + t->name + ")" : std::string{};
    if (!trial_.ok) {
        out.emplace_back(Sev::Error, "Essai : " + trial_.call + " \xC3\xA9" "choue : " + trial_.error);
        return out;
    }
    out.emplace_back(Sev::Information, "Essai : " + trial_.call + (trial_.result.empty() ? std::string(" (sans retour)") : " = " + trial_.result) + where);
    if (!trial_.before.empty()) {
        std::string b;
        for (const auto& x : trial_.before) b += (b.empty() ? "" : " ; ") + x;
        out.emplace_back(Sev::Information, "SELF avant : " + b);
    }
    std::string self;
    for (const auto& x : trial_.self) self += (self.empty() ? "" : "  \xC2\xB7  ") + x;
    out.emplace_back(Sev::Information, "SELF apr\xC3\xA8s : " + (self.empty() ? std::string("\xE2\x80\x94") : self));
    if (trial_.changed.empty()) out.emplace_back(Sev::Information, "Rien n'a chang\xC3\xA9 (ni SELF, ni les variables IHM).");
    for (const auto& c : trial_.changed) out.emplace_back(Sev::Information, "Chang\xC3\xA9 : " + c);
    for (const auto& o : trial_.outputs) out.emplace_back(Sev::Information, "Rendu : " + o);
    for (const auto& j : trial_.journal) out.emplace_back(Sev::Information, "Journal : " + j);
    out.emplace_back(Sev::Information, "Le projet n'est pas touch\xC3\xA9 : l'instance d'essai et son appel n'existent que le temps de l'essai.");
    return out;
}

// ---- 1.50.0 (planche 01) : la fenetre des fonctions, sur le type ----
HmiNewFunctionDialog::Spec HmiMethodsPane::addDialogSpec() const {
    HmiNewFunctionDialog::Spec spec;
    spec.doc = doc_;
    spec.type = type_;
    return spec;
}

bool HmiMethodsPane::openAddDialog() {
    if (!owner() || !newfunction::available()) return false;
    const std::weak_ptr<int> alive = alive_;
    newfunction::ask(addDialogSpec(), [this, alive](const HmiNewFunctionDialog::Answer& a) {
        if (alive.expired()) return;
        std::string why;
        if (createMethod(a, &why) == kNoId && !why.empty()) say("Refus\xC3\xA9" "e : " + why, true);
    });
    return true;
}

Id HmiMethodsPane::createMethod(const HmiNewFunctionDialog::Answer& a, std::string* why) {
    const auto* t = owner();
    if (!t) return fail(why, "choisis une structure ou une interface"), kNoId;
    const bool iface = t->kind == hmi::HmiTypeKind::Interface;
    const std::string name = trimmedOf(a.name);
    // 1.50.1 : un nom deja pris fait une surcharge (comme une fonction) - pas d'une virtuelle.
    if (const std::string w = nameProblem(doc_->project, *t, name, kNoId, true, a.isVirtual && !iface); !w.empty()) return fail(why, w), kNoId;
    const std::size_t siblings = hmi::ownMethods(*t, name).size();
    hmi::HmiFunction draft;
    draft.name = name;
    const std::string ret = trimmedOf(a.returnType);
    draft.returnType = ret == "Aucun" || ret == "(aucun)" ? std::string{} : ret;
    draft.returnUnit = draft.returnType.empty() ? std::string{} : trimmedOf(a.returnUnit);
    draft.description = a.description;
    draft.visibility = !a.pub && !iface ? hmi::Visibility::Private : hmi::Visibility::Public;
    draft.isVirtual = a.isVirtual && !iface;
    if (!trimmedOf(a.generics).empty()) {
        std::string w;
        if (!hmi::parseGenerics(a.generics, draft.generics, &w)) return fail(why, w), kNoId;
    }
    if (!draft.returnUnit.empty())
        if (std::string w = unitui::problem(doc_->project, draft.returnType, draft.returnUnit); !w.empty()) return fail(why, w), kNoId;
    if (std::string mp = hmi::methodProblem(doc_->project, *t, draft); !mp.empty()) return fail(why, mp), kNoId;
    const Id typeId = t->id;
    const std::string typeName = t->name;
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, std::string(iface ? "Nouvelle signature " : "Nouvelle m\xC3\xA9thode ") + typeName + "." + name, [&](hmi::Project& p) {
        auto* x = p.hmiType(typeId);
        if (!x) return;
        hmi::HmiFunction m = draft;
        m.id = p.allocate();
        if (!iface) m.body = hmi::methodTemplate(p, typeName, m);
        made = m.id;
        x->methods.push_back(std::move(m));
    });
    if (cmd) apply_(std::move(cmd));
    if (made != kNoId) {
        refresh();
        selectMethod(made);
        showCodeTab(CodeTabParameters);    // ses parametres d'abord (la planche : " Parametres s'ouvre ")
        // 1.50.1 : une surcharge - si sa forme est celle d'une autre (sans parametre), Compiler le dira : a changer.
        std::string note;
        if (siblings > 0) {
            const hmi::HmiType* now = doc_->project.hmiType(typeId);
            const auto* mine = hmi::methodById(doc_->project, made);
            const std::string clash = now && mine ? hmi::methodOverloadProblem(*now, *mine) : std::string{};
            note = clash.empty() ? " Une surcharge : " + std::to_string(siblings + 1) + " m\xC3\xA9thodes " + name + " (chaque appel prend la sienne)."
                                 : " Une surcharge, de la m\xC3\xAAme forme qu'une autre pour l'instant : ajoute ses param\xC3\xA8tres (onglet Param\xC3\xA8tres) "
                                   "pour la distinguer.";
        }
        say(std::string(iface ? "Signature ajout\xC3\xA9" "e : " : "M\xC3\xA9thode ajout\xC3\xA9" "e : ") + hmi::methodSignature(typeName, draft)
                + " - ses param\xC3\xA8tres, ses locales et ses constantes dans leurs onglets (Ctrl+Z la retire)." + note,
            note.find("m\xC3\xAAme forme") != std::string::npos);
    }
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
