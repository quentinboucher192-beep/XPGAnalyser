#include "HmiOperatorPanes.hpp"

#include "HmiAssetPanes.hpp"
#include "HmiIcons.hpp"
#include "HmiPaneKit.hpp"
#include "HmiScriptPanes.hpp"   // 1.10.4 : guardedDiagnostics
#include "HmiAskDialog.hpp"     // 1.11.3 : la fenetre d'import des operateurs
#include "HmiImages.hpp"        // hmiProjectFolder
#include "../../menu/MenuManager.hpp"
#include "../../ui/widgets/PathBrowse.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiEnums.hpp"     // 1.10 (decision 15)
#include "../../hmi/HmiSymbols.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app {

using hmi::Id;
using hmi::kNoId;
using hmikit::Rows;

namespace {

// Au-dessus, les equivalents C++ et C, puis (1.10.1) la legende de a, b et
// Resultat ; l'editeur ; dessous, la barre du nom ou est le curseur (comme les scripts).
float legendHeight(const ui::Widget&);
class CodeArea final : public ui::Widget {
public:
    explicit CodeArea(std::string id) : ui::Widget(std::move(id)) {}
protected:
    void onLayout() override {
        const auto b = bounds();
        constexpr float kBar = 22.f;
        if (children().size() < 4) return;
        children()[0]->setBounds({b.x, b.y, b.w, kBar});
        // La legende : sa hauteur voulue, sans manger plus de la moitie du script.
        const float legend = children()[1]->visible() ? std::min(legendHeight(*children()[1]), std::max(48.f, b.h * 0.5f)) : 0.f;
        children()[1]->setBounds({b.x, b.y + kBar + 2.f, b.w, legend});
        float top = b.y + kBar + 2.f + (legend > 0.f ? legend + 2.f : 0.f);
        // 1.11.18 (refonte, lot 5) : le bandeau de l'ancien format, juste au-dessus du code.
        if (children().size() > 4 && children()[4]->visible()) {
            children()[4]->setBounds({b.x, top, b.w, HmiDeclBanner::kHeight});
            top += HmiDeclBanner::kHeight;
        }
        children()[2]->setBounds({b.x, top, b.w, std::max(0.f, b.bottom() - kBar - 2.f - top)});
        children()[3]->setBounds({b.x, b.bottom() - kBar, b.w, kBar});
    }
};

// 1.10.1 (U2) : la legende du script, ecrite noir sur blanc dans un petit encadre :
// "a : l'objet de type T_VECTEUR (a gauche de +) . b : l'autre operande, REAL .
// Resultat : T_VECTEUR", puis "Exemple : Resultat.x := a.x + b.x;". La meme au-dessus
// du script et dans la fenetre d'ajout.
// Rend la hauteur qu'il faudrait : une colonne etroite fait passer a la ligne
// (a, b et Resultat chacun sur la sienne s'il le faut), l'exemple en dessous.
constexpr float kLegendLine = 21.f;
// 1.10.1 (I1101) : un texte qui tient dans `room`, coupe avec "..." au lieu d'etre
// tranche au bord (un caractere UTF-8 entier a la fois).
std::string elided(const ui::PaintContext& ctx, std::string_view text, gfx::FontId font, float room) {
    std::string s(text);
    if (ctx.r.measure(s, font).width <= room) return s;
    const std::string dots = "\xE2\x80\xA6";
    while (!s.empty() && ctx.r.measure(s + dots, font).width > room) {
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();   // octets de suite
        if (!s.empty()) s.pop_back();                                                              // octet de tete
    }
    return s.empty() ? std::string{} : s + dots;
}
float paintLegend(const ui::PaintContext& ctx, gfx::Rect b, std::string_view legend, std::string_view example) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.inputBg);
    ctx.r.strokeRect(b, c.border, 1.f);
    ctx.r.fillRect({b.x, b.y, 3.f, b.h}, c.accent);
    ctx.r.pushClip({b.x + 4.f, b.y + 1.f, std::max(0.f, b.w - 8.f), std::max(0.f, b.h - 2.f)});
    const auto ui = ctx.theme.font.ui, mono = ctx.theme.font.mono, bold = ctx.theme.font.uiBold;
    // Chaque nom (a, b, Resultat) en chasse fixe, en couleur, puis ce qu'il est.
    const float left = b.x + 12.f, right = b.right() - 8.f;
    float x = left, y = b.y + 5.f;
    std::size_t at = 0;
    const std::string_view sep = "  \xC2\xB7  ";
    const float sepW = ctx.r.measure(sep, ui).width;
    while (at <= legend.size()) {
        auto end = legend.find(sep, at);
        if (end == std::string_view::npos) end = legend.size();
        const auto part = legend.substr(at, end - at);
        const auto colon = part.find(" : ");
        const auto name = colon == std::string_view::npos ? std::string_view{} : part.substr(0, colon);
        const bool ident = !name.empty() && name.find(' ') == std::string_view::npos;
        const auto nameFont = ident ? mono : bold;
        const auto rest = colon == std::string_view::npos ? part : part.substr(colon);
        const float w = (name.empty() ? 0.f : ctx.r.measure(name, nameFont).width) + ctx.r.measure(rest, ui).width;
        if (x > left && x + w > right) {                 // passer a la ligne
            x = left;
            y += kLegendLine;
        }
        if (!name.empty()) {
            ctx.r.drawText({x, y}, name, nameFont, ident ? c.accent : c.text);
            x += ctx.r.measure(name, nameFont).width;
        }
        ctx.r.drawText({x, y}, rest, ui, c.text);
        x += ctx.r.measure(rest, ui).width;
        if (end == legend.size()) break;
        if (x + sepW < right) ctx.r.drawText({x, y}, sep, ui, c.textMuted);
        x += sepW;
        at = end + sep.size();
    }
    // Un exemple d'une ligne, en dessous. 1.10.1 (I1101) : dans une colonne etroite,
    // coupe avec "..." au lieu d'etre tranche au bord (l'infobulle le donne en entier).
    y += kLegendLine;
    const std::string_view lead = "Exemple : ";
    ctx.r.drawText({left, y}, lead, ui, c.textMuted);
    const float from = left + ctx.r.measure(lead, ui).width;
    ctx.r.drawText({from, y}, elided(ctx, example, mono, right - from), mono, c.text);
    ctx.r.popClip();
    return y + kLegendLine + 6.f - b.y;
}

class LegendBox final : public ui::Widget {
public:
    explicit LegendBox(std::string id) : ui::Widget(std::move(id)) {}
    void setTexts(std::string legend, std::string example) {
        legend_ = std::move(legend);
        example_ = std::move(example);
        setTooltip(legend_.empty() ? std::string{} : legend_ + "\nExemple : " + example_);
        invalidate();
    }
    // La hauteur voulue (mesuree au dessin) : deux lignes, plus si la colonne est etroite.
    [[nodiscard]] float wanted() const noexcept { return wanted_; }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const float need = paintLegend(ctx, bounds(), legend_, example_);
        if (need > 0.f && std::abs(need - wanted_) > 0.5f) {
            wanted_ = need;
            if (auto* area = parent()) area->invalidateLayout();
        }
    }
private:
    std::string legend_, example_;
    float       wanted_{2.f * kLegendLine + 6.f + 5.f};
};

float legendHeight(const ui::Widget& w) {
    const auto* box = dynamic_cast<const LegendBox*>(&w);
    return box ? box->wanted() : 48.f;
}

// La largeur d'une pastille, sans le moteur de dessin (la meme a l'ecran et dans les essais).
float chipWidth(std::string_view label) {
    std::size_t chars = 0;
    for (const char ch : label) chars += (static_cast<unsigned char>(ch) & 0xC0) != 0x80;
    return std::max(34.f, 22.f + 8.2f * static_cast<float>(chars));
}

// Le brouillon d'un operateur du porteur : une conversion vers `value`, ou `kind` avec `value` a droite.
hmi::HmiOperator draftFor(const hmi::Project& p, const hmi::OperatorOwner& owner, const std::string& kind, const std::string& value) {
    return kind == hmi::kConversionOp ? hmi::draftOperator(p, kind, owner.name, {}, value)
                                      : hmi::draftOperator(p, kind, owner.name, value, owner.name);
}

// Les operandes de droite et les cibles proposes par la fenetre.
void choiceLists(const hmi::Project& p, const hmi::OperatorOwner& owner, const std::function<std::vector<std::string>()>& plcTypes,
                 std::vector<std::string>& operands, std::vector<std::string>& targets) {
    operands = {owner.name, "REAL", "LREAL", "INT", "DINT"};
    targets = {"STRING", "REAL", "LREAL", "INT", "DINT", "BOOL"};
    for (const auto& t : p.programs.types) {
        if (hmi::sameTypeName(t.name, owner.name)) continue;
        if (!hmi::isEnumeration(t) && operands.size() < 8) operands.push_back(t.name);
        if (targets.size() < 10) targets.push_back(t.name);
    }
    (void)plcTypes;                                  // les DDT : la liste Cible des proprietes (des dizaines ici)
}

// La premiere valeur de `list` (sauf `except`) dont l'operateur est permis sur le porteur.
std::string firstFree(const hmi::Project& p, const hmi::OperatorOwner& owner, const std::string& kind, const std::vector<std::string>& list,
                      const std::string& except, const std::function<bool(std::string_view)>& plcType) {
    for (const auto& v : list) {
        if (!except.empty() && hmi::sameTypeName(v, except)) continue;
        if (hmi::operatorProblem(p, owner, draftFor(p, owner, kind, v), hmi::kNoId, plcType).empty()) return v;
    }
    return list.empty() ? std::string{} : list.front();
}

enum ToolAction : int { TAdd = 1, TDuplicate, TDelete, TCompile, TRewrite,
                        TExport, TImport };   // 1.11.3 : les operateurs voyagent (.xpgst)

constexpr const char* kConversionLabel = "Conversion (TO_xxx)";
constexpr const char* kNone = "(aucun)";

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
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

// Les constats d'un operateur (Compiler) en diagnostics de script.
std::vector<hmi::ScriptDiagnostic> diagnosticsOf(const std::vector<hmi::OperatorIssue>& issues, Id id) {
    std::vector<hmi::ScriptDiagnostic> out;
    for (const auto& i : issues) {
        if (i.id != id) continue;
        out.push_back({i.error ? hmi::ScriptDiagnostic::Severity::Error : hmi::ScriptDiagnostic::Severity::Warning, i.line,
                       i.message});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const hmi::ScriptDiagnostic& a, const hmi::ScriptDiagnostic& b) { return a.line < b.line; });
    return out;
}

// Les cibles proposees pour une conversion, dans l'ordre.
constexpr std::string_view kConversionTargets[] = {"STRING", "REAL", "LREAL", "DINT", "INT", "BOOL", "UDINT", "UINT",
                                                    "WORD", "DWORD", "TIME"};

} // namespace

HmiOperatorsPane::HmiOperatorsPane(std::string id, hmi::DocumentPtr doc, Apply apply, hmi::OperatorOwner owner)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)), owner_(std::move(owner)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    // 1.10.1 (U2) : un seul outil, la fenetre de la maquette (genre, operande ou cible,
    // signature, legende, script prerempli).
    tools->add(TAdd, HmiGlyph::Plus,
               "Ajouter un op\xC3\xA9rateur : le genre (TO_\xE2\x80\xA6, + - * / += = < ...), l'op\xC3\xA9rande ou la cible, son script pr\xC3\xA9rempli",
               "Ajouter un op\xC3\xA9rateur");
    tools->separator();
    tools->add(TDuplicate, HmiGlyph::Duplicate, "Dupliquer : la fen\xC3\xAAtre d'ajout, le m\xC3\xAAme genre, l'op\xC3\xA9rande (ou la cible) libre suivant",
               "Dupliquer");
    tools->add(TDelete, HmiGlyph::Delete, "Supprimer l'op\xC3\xA9rateur (Ctrl+Z le rend)", "Supprimer");
    tools->separator();
    // 1.11.17 (refonte, lot 1) : les operateurs du porteur affiche ; le projet : IHM > Compiler.
    tools->add(TCompile, HmiGlyph::Code,
               "Compiler les op\xC3\xA9rateurs affich\xC3\xA9s (F7) : ceux de ce symbole ou de ce type, leurs fautes ici et dans les "
               "Diagnostics du panneau du bas ; le projet entier : IHM > Compiler",
               "Compiler (F7)");
    tools->separator();
    // 1.11.3 : exporter et importer les operateurs (un fichier .xpgst, lisible).
    tools->add(TExport, HmiGlyph::Export, "Exporter les op\xC3\xA9rateurs de ce symbole ou de ce type dans un fichier .xpgst, lisible et modifiable",
               "Exporter\xE2\x80\xA6");
    tools->add(TImport, HmiGlyph::Import,
               "Importer des op\xC3\xA9rateurs (.xpgst) : chacun coch\xC3\xA9, un neuf ajout\xC3\xA9, un diff\xC3\xA9rent remplac\xC3\xA9 (m\xC3\xAAme signature), un seul Ctrl+Z",
               "Importer\xE2\x80\xA6");
    tools->add(TRewrite, HmiGlyph::Refresh, "R\xC3\xA9\xC3\xA9" "crire toString et fromString d'apr\xC3\xA8s les valeurs de l'\xC3\xA9num\xC3\xA9ration (Ctrl+Z reprend)",
               "R\xC3\xA9\xC3\xA9" "crire depuis les valeurs");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setVisibleWhen(TRewrite, [this] {
        const auto* t = owner_.kind == hmi::OperatorOwner::Kind::Type ? doc_->project.hmiType(owner_.id) : nullptr;
        return t && hmi::isEnumeration(*t);
    });
    tools_->setEnabledWhen(TAdd, [this] { return owner_.valid(); });
    tools_->setEnabledWhen(TExport, [this] { return owner_.valid() && !order_.empty(); });
    tools_->setEnabledWhen(TImport, [this] { return owner_.valid(); });
    tools_->setEnabledWhen(TDuplicate, [this] { return selectedOperator() != kNoId; });
    tools_->setEnabledWhen(TDelete, [this] { return selectedOperator() != kNoId; });
    tools_->setEnabledWhen(TCompile, [this] { return owner_.valid() && !order_.empty(); });     // 1.11.17 : rien a compiler

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    auto left = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".left");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".listPanel", "OP\xC3\x89RATEURS");
        auto table = std::make_unique<ui::TableView>(base + ".operators");
        table->setColumns({{"Genre", 104.f}, {"Signature", 250.f}, {"Retour", 110.f}, {"\xC3\x89tat", 110.f},
                           {"Fonction", 110.f}, {"Description", 220.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        table_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        listPanel_ = panel.get();
        left->addPane(std::move(panel), 0.55f, 100.f);
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".propsPanel", "PROPRI\xC3\x89T\xC3\x89S DE L'OP\xC3\x89RATEUR");
        auto grid = std::make_unique<ui::PropertyGrid>(base + ".props");
        grid->setShowDescriptionPane(false);
        grid->setNameColumnRatio(0.36f);
        props_ = &static_cast<ui::PropertyGrid&>(panel->setBody(std::move(grid)));
        left->addPane(std::move(panel), 0.45f, 90.f);
    }
    split->addPane(std::move(left), 0.42f, 260.f);
    auto right = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".right");
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".editorPanel", "SCRIPT");
        auto area = std::make_unique<CodeArea>(base + ".code");
        auto eq = std::make_unique<ui::StatusBar>(base + ".equivalents");
        eq->setTooltip("Le m\xC3\xAAme op\xC3\xA9rateur en C++ (operator+, explicit operator REAL()) et en C (une fonction nomm\xC3\xA9" "e).");
        equivBar_ = &static_cast<ui::StatusBar&>(area->addChild(std::move(eq)));
        legend_ = &area->addChild(std::make_unique<LegendBox>(base + ".legend"));   // 1.10.1 (U2)
        auto ed = std::make_unique<ui::MultiLineText>(base + ".editor");
        ed->setLanguage(ui::Language::StructuredText);
        ed->setShowLineNumbers(true);
        ed->setReadOnly(true);
        ed->setTabInsertsSpaces(4);
        ed->setCommandKeys(true);            // 1.12.2 : les raccourcis de Visual Studio (accords Ctrl+K...)
        editor_ = &static_cast<ui::MultiLineText&>(area->addChild(std::move(ed)));
        auto bar = std::make_unique<ui::StatusBar>(base + ".symbol");
        bar->setTooltip("Le nom o\xC3\xB9 est le curseur : a, b, Resultat, une locale, une variable IHM ou de l'automate.");
        symbolBar_ = &static_cast<ui::StatusBar&>(area->addChild(std::move(bar)));
        // 1.11.18 (refonte, lot 5) : Code, Locales, Constantes ; le bandeau de l'ancien format.
        banner_ = &static_cast<HmiDeclBanner&>(area->addChild(std::make_unique<HmiDeclBanner>(base + ".declBanner")));
        codeTabs_ = &static_cast<HmiCodeTabs&>(panel->setBody(std::make_unique<HmiCodeTabs>(
            base + ".codeTabs", doc_, apply_, std::move(area), std::vector{hmi::decledit::Tab::Variables, hmi::decledit::Tab::Constants},
            banner_)));
        editorPanel_ = panel.get();
        // 1.11.21 : l'editeur prend toute la hauteur - ses fautes vont au panneau du bas
        // (Diagnostics, l'etape Saisie ; HmiLive.hpp), leurs lignes marquees dans le code.
        right->addPane(std::move(panel), 1.f, 120.f);
    }
    split->addPane(std::move(right), 0.58f, 300.f);
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    // 1.10.1 (U2) : la fenetre "Ajouter un operateur", par-dessus tout (ajoutee en dernier).
    dialog_ = &static_cast<HmiOperatorDialog&>(addChild(std::make_unique<HmiOperatorDialog>(base + ".ajouter")));
    dialog_->onCreate = [this] { (void)createFromDialog(); };
    dialog_->onOpen = [this](Id existing) {
        dialog_->close();
        goTo(existing, 0);
        say("Cet op\xC3\xA9rateur existe d\xC3\xA9j\xC3\xA0 : son script est ouvert.");
    };

    links_ += tools_->triggered->connect([this](int a) {
        const Id sel = selectedOperator();
        switch (a) {
            case TAdd: (void)openAddDialog(); break;
            case TDuplicate: if (sel) (void)openDuplicateDialog(sel); break;
            case TDelete: if (sel) (void)deleteOperator(sel); break;
            case TRewrite: (void)rewriteFromValues(); break;
            case TExport: {   // 1.11.3
                const std::weak_ptr<char> alive = alive_;
                const std::string name = owner_.name + "_operateurs" + std::string(hmi::scriptfile::kExtension);
                if (!ui::browsePath(ui::saveFile("Op\xC3\xA9rateurs XPGAnalyser|*.xpgst", ui::pathIn(hmiProjectFolder(), "exports"),
                                                 "Exporter les op\xC3\xA9rateurs de " + owner_.name),
                                    name, [this, alive](std::string path) {
                                        if (alive.expired()) return;
                                        std::string why;
                                        if (exportOperators(path, &why)) say("Op\xC3\xA9rateurs export\xC3\xA9s : " + path);
                                        else say(why, true);
                                    }))
                    say("Pas d'explorateur de fichiers ici.", true);
                break;
            }
            case TImport: {
                const std::weak_ptr<char> alive = alive_;
                if (!ui::browsePath(ui::openFile("Op\xC3\xA9rateurs XPGAnalyser|*.xpgst", ui::pathIn(hmiProjectFolder(), "exports"),
                                                 "Importer des op\xC3\xA9rateurs"),
                                    {}, [this, alive](std::string path) {
                                        if (alive.expired()) return;
                                        std::string why;
                                        if (!importOperators(path, &why)) say(why, true);
                                    }))
                    say("Pas d'explorateur de fichiers ici.", true);
                break;
            }
            case TCompile: (void)compileCurrent(); break;   // 1.11.17 : les operateurs affiches (ici, et leur build)
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        selectedRow_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        showSelected();
    });
    links_ += editor_->textChanged->connect([this](const std::string& text) {
        if (syncing_) return;
        if (const Id sel = selectedOperator()) (void)setBody(sel, text);
        updateDiagnostics();
    });
    // 1.10.1 (U2) : la barre sous le script dit ce qu'est le nom ou est le curseur
    // (a : T_VECTEUR, l'objet... ; a.x : REAL, membre de a).
    links_ += editor_->caretSymbolChanged->connect([this](const std::string& symbol) {
        const auto plc = assist_.plc ? assist_.plc() : nullptr;
        const auto d = assist::describe(doc_->project, plc.get(), symbol,
                                        (assist_.declarations ? assist_.declarations() : std::string{}) + editor_->text(), selectedOperator());
        symbolLine_ = symbol.empty() || d.keyword ? std::string{} : d.found ? d.line : symbol + "  \xE2\x80\x94  inconnu ici";
        symbolBar_->setMessage(symbolLine_);
    });
    // 1.11.18 (lot 5) : les grilles parlent dans la barre du volet ; leurs utilisations menent au code.
    links_ += codeTabs_->message->connect([this](const std::string& t, bool warning) { say(t, warning); });
    links_ += codeTabs_->usesRequested->connect([this](const std::string& name) { (void)goToNextUse(name); });
    links_ += codeTabs_->migrateRequested->connect([this] { (void)migrateCurrent(); });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    // 1.10.1 (U2) : l'aide a la saisie, des la creation (le volet des types IHM ne la
    // branchait pas) : le programme de l'automate pose par l'ecran ; a, b, Resultat.
    assist_ = assist::sourcesFor(doc_);
    assist_.op = [this] { return selectedOperator(); };
    assist_.declarations = [this] {                                        // 1.11.18 (lot 3) : ses constantes et variables du modele
        const auto* o = current();
        return o ? assist::declarationsPrefix(o->decls, hmi::decl::Role::Operator) : std::string{};
    };
    assist::attach(*editor_, assist_);
    // 1.10.1 (U2) : les DDT de l'automate (une cible de conversion permise, la liste
    // Cible des proprietes), lus dans le programme de l'aide a la saisie : aucun hote
    // ne les donnait, une conversion vers un DDT etait refusee.
    plcTypes = [this] {
        std::vector<std::string> out;
        if (const auto plc = assist_.plc ? assist_.plc() : nullptr)
            for (const auto& d : plc->derivedTypes) out.emplace_back(plc->strings.text(d.name));
        return out;
    };
    isPlcType = [this](std::string_view t) {
        if (const auto plc = assist_.plc ? assist_.plc() : nullptr)
            for (const auto& d : plc->derivedTypes)
                if (hmi::sameTypeName(plc->strings.text(d.name), t)) return true;
        return false;
    };
    refresh();
}

void HmiOperatorsPane::setOwner(hmi::OperatorOwner owner) {
    if (owner.kind == owner_.kind && owner.id == owner_.id && owner.name == owner_.name) return;
    owner_ = std::move(owner);
    selectedRow_ = -1;
    refresh();
}

Id HmiOperatorsPane::selectedOperator() const {
    if (selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= order_.size()) return kNoId;
    return order_[static_cast<std::size_t>(selectedRow_)];
}

const hmi::HmiOperator* HmiOperatorsPane::current() const {
    const Id id = selectedOperator();
    const auto* list = hmi::operatorsOf(doc_->project, owner_);
    if (!list || id == kNoId) return nullptr;
    for (const auto& o : *list) if (o.id == id) return &o;
    return nullptr;
}

void HmiOperatorsPane::selectOperator(Id id) {
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == id && id) {
            hmiSelectModelRow(*table_, i);
            selectedRow_ = static_cast<int>(i);
            showSelected();
            return;
        }
}

void HmiOperatorsPane::goTo(Id op, int line) {
    hmi::OperatorOwner who;
    if (hmi::operatorById(doc_->project, op, &who) && (who.kind != owner_.kind || who.id != owner_.id)) setOwner(who);
    selectOperator(op);
    if (line > 0) {
        showCodeTab(CodeTabCode);             // 1.11.18 (lot 5) : le code, pas une grille
        editor_->goToLine(static_cast<std::size_t>(line - 1));
    }
}

void HmiOperatorsPane::refresh() {
    // Le porteur a pu etre renomme (ou supprime) : on le relit par son identifiant.
    if (owner_.kind == hmi::OperatorOwner::Kind::Type) {
        if (const auto* t = doc_->project.hmiType(owner_.id)) owner_.name = t->name;
        else owner_ = {};
    } else if (owner_.kind == hmi::OperatorOwner::Kind::Symbol) {
        if (const auto* v = doc_->project.view(owner_.id)) owner_.name = v->name;
        else owner_ = {};
    }
    const Id keep = selectedOperator();
    order_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> states;
    std::size_t conversions = 0;
    const auto issues = hmi::operatorIssues(doc_->project, isPlcType);
    if (const auto* list = hmi::operatorsOf(doc_->project, owner_)) {
        for (const auto& o : *list) {
            order_.push_back(o.id);
            conversions += o.op == hmi::kConversionOp;
            states.push_back(stateOf(diagnosticsOf(issues, o.id)));
            rows.push_back({kindText(o.id), hmi::operatorSignature(o),
                            o.result.empty() ? std::string("a modifi\xC3\xA9") : o.result, states.back(), hmi::operatorFunctionName(o),
                            o.description});
        }
    }
    model_ = std::make_shared<Rows>(
        std::vector<std::string>{"Genre", "Signature", "Retour", "\xC3\x89tat", "Fonction", "Description"}, std::move(rows),
        [states](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (c == 1) s.icon = ui::Icon::FunctionBlock;
            if (c == 3 && r < states.size())
                s.fgTone = states[r] == "OK" ? ui::Tone::Ok
                         : states[r].find("erreur") != std::string::npos ? ui::Tone::Error : ui::Tone::Warning;
            return s;
        });
    table_->setModel(model_);
    listPanel_->setTitle(owner_.valid() ? "OP\xC3\x89RATEURS DU " + upperOf(owner_.label()) + " (" + std::to_string(order_.size()) + ")"
                                        : std::string("OP\xC3\x89RATEURS"));
    selectedRow_ = -1;
    for (std::size_t i = 0; i < order_.size(); ++i)
        if (order_[i] == keep && keep) { hmiSelectModelRow(*table_, i); selectedRow_ = static_cast<int>(i); }
    if (selectedRow_ < 0 && !order_.empty()) {
        hmiSelectModelRow(*table_, 0);
        selectedRow_ = 0;
    }
    showSelected();
    if (!owner_.valid()) status_->setMessage("Choisis un symbole ou un type IHM : ses op\xC3\xA9rateurs.");
    else if (order_.empty())
        status_->setMessage("Aucun op\xC3\xA9rateur : Ajouter un op\xC3\xA9rateur (un TO_xxx, un + - * / += = < ...) ; chacun a son script, a et b y sont ses op\xC3\xA9randes.");
    else
        status_->setMessage(std::to_string(order_.size()) + " op\xC3\xA9rateur(s)  \xC2\xB7  " + std::to_string(conversions)
                            + " conversion(s)  \xC2\xB7  emploi : TO_xxx(x), a + b, v += 1.5 dans les scripts et les expressions");
    invalidate();
}

std::string HmiOperatorsPane::kindText(Id id) const {
    hmi::OperatorOwner who;
    const auto* o = hmi::operatorById(doc_->project, id, &who);
    if (!o) return {};
    const auto role = hmi::operatorRole(doc_->project, who, *o);
    return std::string(hmi::operatorKindLabel(o->op)) + (role.empty() ? std::string{} : " \xC2\xB7 " + std::string(role));
}

bool HmiOperatorsPane::rewriteFromValues(std::string* why) {
    const auto* t = owner_.kind == hmi::OperatorOwner::Kind::Type ? doc_->project.hmiType(owner_.id) : nullptr;
    if (!t || !hmi::isEnumeration(*t)) {
        if (why) *why = "le porteur n'est pas une \xC3\xA9num\xC3\xA9ration";
        return false;
    }
    const Id typeId = t->id;
    const std::string name = t->name;
    std::size_t written = 0;
    auto cmd = hmi::changeProject(doc_, "R\xC3\xA9\xC3\xA9" "crire toString et fromString de " + name, [&](hmi::Project& p) {
        if (auto* x = p.hmiType(typeId)) written = hmi::regenerateEnumConversions(p, *x);
    });
    if (cmd) apply_(std::move(cmd));
    say("toString et fromString de " + name + " r\xC3\xA9\xC3\xA9" "crits d'apr\xC3\xA8s ses valeurs (" + std::to_string(written)
        + " op\xC3\xA9rateur(s)). Ctrl+Z reprend.");
    return true;
}

std::string HmiOperatorsPane::equivalents() const {
    const auto* o = current();
    if (!o) return {};
    // 1.10 (decision 15) : une enumeration n'a pas d'operateur membre en C++ (comme la maquette, scene 14).
    const auto role = hmi::operatorRole(doc_->project, owner_, *o);
    std::string lower = owner_.name;
    for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (role == "toString")
        return "C++  std::string to_string(" + owner_.name + " a)      C  const char *" + lower + "_to_string(" + owner_.name + " a)";
    if (role == "fromString")
        return "C++  " + owner_.name + " from_string(const std::string& a)      C  " + owner_.name + " " + lower + "_from_string(const char *a)";
    return "C++  " + hmi::operatorCppSignature(*o, owner_.name) + "      C  " + hmi::operatorCSignature(*o, owner_.name);
}

std::string HmiOperatorsPane::editorTitle() const {
    const auto* o = current();
    return o ? "SCRIPT  \xC2\xB7  " + hmi::operatorSignature(*o) : std::string("SCRIPT");
}

void HmiOperatorsPane::showSelected() {
    const auto* o = current();
    syncing_ = true;
    const std::string body = o ? o->body : std::string{};
    if (editor_->text() != body) {      // 1.12.2 : le meme operateur (annuler) garde la vue
        if (o && o->id == shownId_) editor_->reloadText(body);
        else editor_->setText(body);
    }
    shownId_ = o ? o->id : hmi::kNoId;
    editor_->setReadOnly(o == nullptr);
    syncing_ = false;
    editorPanel_->setTitle(editorTitle());
    equivBar_->setMessage(equivalents());
    // 1.10.1 (U2) : la legende - a, b, Resultat et leurs types, un exemple.
    if (auto* box = dynamic_cast<LegendBox*>(legend_)) box->setTexts(legendText(), exampleText());
    if (legend_->visible() != (o != nullptr)) {
        legend_->setVisibility(o ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (auto* area = legend_->parent()) area->invalidateLayout();
    }
    rebuildProperties();
    updateDiagnostics();
    codeTabs_->setPlace(currentPlace(), "Choisis un op\xC3\xA9rateur.");   // 1.11.18 (lot 5) : ses onglets
}

// ---- 1.11.18 (refonte des scripts, lot 5) : les onglets de l'operateur ----
std::optional<hmi::decledit::Place> HmiOperatorsPane::currentPlace() const {
    const auto* o = current();
    if (!o || !owner_.valid()) return std::nullopt;
    hmi::decledit::Place at;
    if (owner_.kind == hmi::OperatorOwner::Kind::Type) {
        at.kind = hmi::decledit::Place::Kind::TypeOperator;
        at.type = owner_.id;
    } else {
        at.kind = hmi::decledit::Place::Kind::SymbolOperator;
        at.view = owner_.id;
    }
    at.id = o->id;
    at.label = "op\xC3\xA9rateur " + hmi::operatorSignature(*o) + " (" + owner_.name + ")";
    return at;
}

void HmiOperatorsPane::showCodeTab(std::size_t tab) {
    if (codeTabs_ && tab < codeTabs_->tabCount()) codeTabs_->setCurrentIndex(tab);
}

std::size_t HmiOperatorsPane::currentCodeTab() const noexcept { return codeTabs_ ? codeTabs_->currentIndex() : 0; }

bool HmiOperatorsPane::migrateCurrent() {
    const auto at = currentPlace();
    if (!at) {
        say("Migrer : aucun op\xC3\xA9rateur choisi.", true);
        return false;
    }
    std::string report;
    const bool ok = migrateOne(doc_, apply_, *at, &report);
    say(report, !ok);
    return ok;
}

bool HmiOperatorsPane::goToNextUse(const std::string& name) {
    showCodeTab(CodeTabCode);
    std::string said;
    const bool ok = selectNextUse(*editor_, name, &said);
    say(said, !ok);
    return ok;
}

bool HmiOperatorsPane::showDeclaration(const std::string& name) { return codeTabs_->showDeclaration(name); }

std::string HmiOperatorsPane::legendText() const {
    const auto* o = current();
    return o ? hmi::operatorLegend(*o, owner_.name) : std::string{};
}

std::string HmiOperatorsPane::exampleText() const {
    const auto* o = current();
    return o ? hmi::operatorExample(doc_->project, *o) : std::string{};
}

std::vector<std::string> HmiOperatorsPane::typeChoices(bool operands) const {
    std::vector<std::string> out;
    if (owner_.valid()) out.push_back(owner_.name);
    for (const auto t : {"REAL", "LREAL", "DINT", "INT", "UDINT", "UINT", "BOOL", "STRING", "TIME", "WORD", "DWORD"})
        out.emplace_back(t);
    for (const auto& t : doc_->project.programs.types)
        if (std::find(out.begin(), out.end(), t.name) == out.end()) out.push_back(t.name);
    if (operands)
        for (const auto* v : hmi::symbolsOf(doc_->project))
            if (std::find(out.begin(), out.end(), v->name) == out.end()) out.push_back(v->name);
    if (plcTypes)
        for (auto& t : plcTypes())
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(std::move(t));
    return out;
}

void HmiOperatorsPane::rebuildProperties() {
    using PG = ui::PropertyGrid;
    const auto* o = current();
    if (!o) { props_->setCategories({}); return; }
    const Id id = o->id;
    const hmi::HmiOperator cur = *o;
    const auto fam = hmi::operatorFamily(cur.op);
    std::vector<std::string> kinds{kConversionLabel};
    for (const auto s : hmi::kOperatorSymbols) kinds.emplace_back(std::string(s) + "  (" + std::string(hmi::operatorIecName(s)) + ")");
    const auto kindText = [](const hmi::HmiOperator& x) {
        return x.op == hmi::kConversionOp ? std::string(kConversionLabel)
                                          : x.op + "  (" + std::string(hmi::operatorIecName(x.op)) + ")";
    };
    PG::Category c;
    c.name = fam == hmi::OperatorFamily::Conversion ? "Conversion" : "Op\xC3\xA9rateur";
    c.properties.push_back(hmikit::prop("Genre", kindText(cur), PG::ValueType::Enum,
        [this, id, cur](std::string_view v) {
            std::string op = std::string(v) == kConversionLabel ? std::string(hmi::kConversionOp) : std::string(v.substr(0, v.find(' ')));
            std::string right = cur.right, result = cur.result;
            const auto f = hmi::operatorFamily(op);
            if (f == hmi::OperatorFamily::Conversion) {
                right.clear();
                if (result.empty() || result == "BOOL" || hmi::sameTypeName(result, cur.left)) result = "STRING";
            }
            else {
                if (right.empty()) right = owner_.name;
                if (f == hmi::OperatorFamily::Comparison) result = "BOOL";
                else if (f == hmi::OperatorFamily::Assignment) result.clear();
                else if (result.empty() || hmi::operatorFamily(cur.op) != hmi::OperatorFamily::Arithmetic) result = owner_.name;
            }
            return changeSignature(id, op, cur.left, right, result);
        },
        "Une conversion TO_xxx(x), ou un op\xC3\xA9rateur : + - * / (un r\xC3\xA9sultat), += -= *= /= (modifient la gauche), "
        "= <> < > <= >= (un BOOL). Le script rend son r\xC3\xA9sultat \xC3\xA0 son nom de fonction (ADD, EQ, TO_REAL...).",
        kinds));
    const auto types = typeChoices(true);
    c.properties.push_back(hmikit::prop(fam == hmi::OperatorFamily::Conversion ? "Source (a)" : "Gauche (a)", cur.left, PG::ValueType::Enum,
        [this, id, cur](std::string_view v) { return changeSignature(id, cur.op, std::string(v), cur.right, cur.result); },
        "Le type de l'op\xC3\xA9rande a (dans le script : a). L'un des op\xC3\xA9randes (ou la source, ou la cible) est " + owner_.name + ".", types));
    if (fam != hmi::OperatorFamily::Conversion)
        c.properties.push_back(hmikit::prop("Droite (b)", cur.right, PG::ValueType::Enum,
            [this, id, cur](std::string_view v) { return changeSignature(id, cur.op, cur.left, std::string(v), cur.result); },
            "Le type de l'op\xC3\xA9rande b (dans le script : b) : " + owner_.name + " ou un type externe (REAL, un autre type IHM...). L'ordre compte.", types));
    if (fam == hmi::OperatorFamily::Conversion || fam == hmi::OperatorFamily::Arithmetic)
        c.properties.push_back(hmikit::prop(fam == hmi::OperatorFamily::Conversion ? "Cible" : "R\xC3\xA9sultat", cur.result, PG::ValueType::Enum,
            [this, id, cur](std::string_view v) { return changeSignature(id, cur.op, cur.left, cur.right, std::string(v)); },
            fam == hmi::OperatorFamily::Conversion ? "Le type vis\xC3\xA9 : un type de base, un type IHM ou un DDT de l'automate. Le nom suit : TO_<cible>."
                                                   : "Le type que rend l'op\xC3\xA9rateur.",
            typeChoices(false)));
    else
        c.properties.push_back(hmikit::prop("R\xC3\xA9sultat", cur.result.empty() ? std::string(kNone) : cur.result, PG::ValueType::ReadOnly));
    c.properties.push_back(hmikit::prop("Description", cur.description, PG::ValueType::Text,
        [this, id](std::string_view v) { return setDescription(id, std::string(v)); }));
    if (const auto role = hmi::operatorRole(doc_->project, owner_, cur); !role.empty())   // 1.10 (decision 15)
        c.properties.push_back(hmikit::prop("R\xC3\xB4le", std::string(role) + " de l'\xC3\xA9num\xC3\xA9ration " + owner_.name, PG::ValueType::ReadOnly));
    c.properties.push_back(hmikit::prop("Fonction", hmi::operatorFunctionName(cur), PG::ValueType::ReadOnly));
    c.properties.push_back(hmikit::prop("Signature", hmi::operatorSignature(cur), PG::ValueType::ReadOnly));
    c.properties.push_back(hmikit::prop("Emploi", fam == hmi::OperatorFamily::Conversion ? hmi::operatorFunctionName(cur) + "(x)"
                                                  : fam == hmi::OperatorFamily::Assignment ? "a " + cur.op + " b;"
                                                                                           : "a " + cur.op + " b",
                                        PG::ValueType::ReadOnly));
    props_->setCategories({std::move(c)});
}

std::string HmiOperatorsPane::buildKey() const {
    if (!owner_.valid()) return {};
    return std::string(owner_.kind == hmi::OperatorOwner::Kind::Symbol ? "symbole:" : "type:") + std::to_string(owner_.id);
}

std::size_t HmiOperatorsPane::compileCurrent() {
    if (!owner_.valid() || order_.empty()) {
        say("Compiler : aucun op\xC3\xA9rateur ici (le projet entier : IHM > Compiler).", true);
        return 0;
    }
    updateDiagnostics();
    const auto issues = hmi::operatorIssues(doc_->project, isPlcType);
    std::size_t n = 0;
    for (const Id id : order_)
        for (const auto& d : diagnosticsOf(issues, id)) n += d.severity != hmi::ScriptDiagnostic::Severity::Info ? 1 : 0;
    say("Compiler les op\xC3\xA9rateurs du " + owner_.label() + " : "
            + (n ? std::to_string(n) + (n > 1 ? " fautes" : " faute") : std::string("aucune faute"))
            + " \xC2\xB7 le projet entier : IHM > Compiler",
        n > 0);
    if (build) build(hmi::pipeline::Mode::Compile, buildKey());
    else if (compile) compile();
    return n;
}

ui::EventResult HmiOperatorsPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F7 && (k->mods.none() || (k->mods.ctrl && !k->mods.shift && !k->mods.alt)) && !k->repeat) {
        (void)compileCurrent();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

void HmiOperatorsPane::updateDiagnostics() {
    diagnostics_.clear();
    const auto* o = current();
    // 1.10.4 : rien ne sort d'un diagnostic (le plantage de la 1.10.3) - l'exception
    // va au journal interne et la ligne du curseur dit "diagnostic indisponible".
    if (o)
        diagnostics_ = guardedDiagnostics([&] { return diagnosticsOf(hmi::operatorIssues(doc_->project, isPlcType), o->id); },
                                          editor_ ? static_cast<int>(editor_->caretLine()) + 1 : 1, o->op);
    ++liveRev_;                                   // 1.11.21 : le panneau du bas les relira
    std::vector<std::pair<std::size_t, gfx::Color>> marks;
    for (const auto& d : diagnostics_) {
        if (d.line > 0 && d.severity != hmi::ScriptDiagnostic::Severity::Info)
            marks.emplace_back(static_cast<std::size_t>(d.line - 1),
                               d.severity == hmi::ScriptDiagnostic::Severity::Error ? gfx::Color{231, 76, 60, 255}
                                                                                    : gfx::Color{241, 196, 15, 255});
    }
    editor_->setMarkedLines(std::move(marks));
}

// ---- 1.11.21 : les diagnostics de l'operateur montre, au panneau du bas (HmiLive.hpp) ----
std::vector<hmi::pipeline::Diagnostic> HmiOperatorsPane::liveDiagnostics() const {
    std::vector<hmi::pipeline::Diagnostic> out;
    const auto* o = current();
    if (!o || !owner_.valid()) return out;
    for (const auto& d : diagnostics_) {
        auto x = liveDiagnostic(d, buildKey(), owner_.label() + " \xC2\xB7 " + o->op, "Op\xC3\xA9rateur");
        x.item = o->id;
        x.view = owner_.kind == hmi::OperatorOwner::Kind::Symbol ? owner_.id : hmi::kNoId;
        x.property = o->op;
        out.push_back(std::move(x));
    }
    return out;
}

void HmiOperatorsPane::goToLive(const hmi::pipeline::Diagnostic& d) {
    if (d.item != hmi::kNoId && d.item != selectedOperator()) selectOperator(d.item);
    if (d.line > 0) {
        showCodeTab(CodeTabCode);
        editor_->goToLine(static_cast<std::size_t>(d.line - 1));
    } else if (const auto name = declarationNamed(d.message); !name.empty()) {
        (void)showDeclaration(name);      // la faute d'une declaration, dans son onglet
    }
}

void HmiOperatorsPane::setAssist(std::function<std::shared_ptr<const domain::Project>()> plc,
                                 std::function<bool(std::string_view, std::string&)> live) {
    assist_.hmi = [doc = doc_]() -> const hmi::Project* { return doc ? &doc->project : nullptr; };
    assist_.plc = std::move(plc);
    assist_.live = std::move(live);
    assist::attach(*editor_, assist_);
    updateDiagnostics();
}

// ---- 1.11.3 : exporter et importer les operateurs ----
bool HmiOperatorsPane::exportOperators(const std::string& path, std::string* why) {
    const auto* ops = owner_.valid() ? hmi::operatorsOf(doc_->project, owner_) : nullptr;
    if (!ops || ops->empty()) { if (why) *why = "Rien \xC3\xA0 exporter : aucun op\xC3\xA9rateur."; return false; }
    const auto file = hmi::scriptfile::fromOperators(*ops, owner_.name,
                                                     owner_.kind == hmi::OperatorOwner::Kind::Type ? std::string("type") : std::string("symbole"));
    return hmi::scriptfile::save(file, path, why);
}

bool HmiOperatorsPane::importOperators(const std::string& path, std::string* why) {
    const auto* ops = owner_.valid() ? hmi::operatorsOf(doc_->project, owner_) : nullptr;
    if (!ops) { if (why) *why = "Choisis d'abord un symbole ou un type IHM."; return false; }
    hmi::scriptfile::File f;
    if (!hmi::scriptfile::load(path, f, why)) return false;
    if (f.genre != hmi::scriptfile::Genre::Operators) {
        if (why) *why = "Ce fichier contient des scripts de vue (de " + f.source + ") : importe-le dans les Scripts d'une vue.";
        return false;
    }
    const auto states = hmi::scriptfile::compareOperators(f, *ops);
    app::HmiAskDialog::Spec spec;
    spec.id = "dialog.importOperators";
    spec.title = "Importer des op\xC3\xA9rateurs dans " + owner_.label();
    spec.text = "Le fichier vient de " + (f.source.empty() ? std::string("?") : f.source) + " : coche les op\xC3\xA9rateurs \xC3\xA0 prendre. "
                "Un op\xC3\xA9rateur de m\xC3\xAAme signature est remplac\xC3\xA9.";
    spec.listTitle = "OP\xC3\x89RATEURS DU FICHIER";
    for (std::size_t i = 0; i < f.entries.size(); ++i) {
        const auto st = i < states.size() ? states[i] : hmi::scriptfile::State::New;
        std::string detail = hmi::scriptfile::describe(f, i, st);
        if (st == hmi::scriptfile::State::New)
            if (const auto problem = hmi::operatorProblem(doc_->project, owner_, f.entries[i].op, hmi::kNoId, isPlcType); !problem.empty())
                detail += " \xC2\xB7 refus\xC3\xA9 : " + problem;
        spec.items.push_back({hmi::operatorSignature(f.entries[i].op), detail, st != hmi::scriptfile::State::Same});
    }
    spec.confirm = "Importer";
    spec.confirmLabel = [](const std::vector<bool>& items, const std::vector<bool>&, int) {
        const auto n = static_cast<std::size_t>(std::count(items.begin(), items.end(), true));
        return n == 0 ? std::string("Rien \xC3\xA0 importer") : "Importer " + std::to_string(n) + " op\xC3\xA9rateur" + (n > 1 ? "s" : "");
    };
    spec.note = "Un seul Ctrl+Z annule l'import.";
    auto* manager = menu::MenuManager::instance();
    if (!manager) {
        std::vector<bool> chosen;
        for (const auto& it : spec.items) chosen.push_back(it.checked);
        (void)applyOperatorsImport(f, chosen);
        return true;
    }
    const std::weak_ptr<char> alive = alive_;
    manager->ShowDialog(std::make_unique<app::HmiAskDialog>(std::move(spec)), [this, alive, f](const menu::DialogResult& r) {
        if (alive.expired() || !r.accepted()) return;
        (void)applyOperatorsImport(f, app::HmiAskDialog::parse(r.payload).items);
    });
    return true;
}

std::size_t HmiOperatorsPane::applyOperatorsImport(const hmi::scriptfile::File& f, const std::vector<bool>& chosen) {
    if (!owner_.valid()) return 0;
    // Un neuf que la verification refuse (en double, un type inconnu) est laisse, et dit.
    std::vector<bool> keep = chosen.empty() ? std::vector<bool>(f.entries.size(), true) : chosen;
    std::string refused;
    if (const auto* ops = hmi::operatorsOf(doc_->project, owner_)) {
        const auto states = hmi::scriptfile::compareOperators(f, *ops);
        for (std::size_t i = 0; i < f.entries.size() && i < keep.size(); ++i)
            if (keep[i] && i < states.size() && states[i] == hmi::scriptfile::State::New)
                if (const auto problem = hmi::operatorProblem(doc_->project, owner_, f.entries[i].op, hmi::kNoId, isPlcType); !problem.empty()) {
                    keep[i] = false;
                    refused += (refused.empty() ? "" : " ; ") + hmi::operatorSignature(f.entries[i].op) + " (" + problem + ")";
                }
    }
    std::size_t changed = 0;
    auto cmd = hmi::changeProject(doc_, "Importer des op\xC3\xA9rateurs dans " + owner_.name, [&](hmi::Project& p) {
        if (auto* list = hmi::operatorsOf(p, owner_)) changed = hmi::scriptfile::applyToOperators(p, *list, f, keep);
    });
    if (cmd && changed > 0) apply_(std::move(cmd));
    refresh();
    std::string msg = changed == 0 ? std::string("Rien n'a chang\xC3\xA9.")
                                   : std::to_string(changed) + " op\xC3\xA9rateur" + (changed > 1 ? "s import\xC3\xA9s" : " import\xC3\xA9") + " (Ctrl+Z pour annuler).";
    if (!refused.empty()) msg += " Laiss\xC3\xA9s : " + refused + ".";
    say(msg, !refused.empty());
    return changed;
}

void HmiOperatorsPane::say(std::string text, bool warning) {
    message_ = text;
    status_->setTransientMessage(std::move(text), 8.0, warning ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

// ------------------------------------------------------------------ actions ---
Id HmiOperatorsPane::add(const hmi::OperatorOwner& owner, hmi::HmiOperator op, const std::string& label, std::string* why) {
    if (!owner.valid()) {
        if (why) *why = "aucun symbole ni type choisi";
        return kNoId;
    }
    if (const auto problem = hmi::operatorProblem(doc_->project, owner, op, kNoId, isPlcType); !problem.empty()) {
        if (why) *why = problem;
        say("Refus\xC3\xA9 : " + problem, true);
        return kNoId;
    }
    Id made = kNoId;
    auto cmd = hmi::changeProject(doc_, label + " " + hmi::operatorSignature(op), [&](hmi::Project& p) {
        auto* list = hmi::operatorsOf(p, owner);
        if (!list) return;
        op.id = p.allocate();
        made = op.id;
        list->push_back(op);
    });
    if (cmd) apply_(std::move(cmd));
    if (made) {
        if (owner.kind != owner_.kind || owner.id != owner_.id) setOwner(owner);
        selectOperator(made);
        say(hmi::operatorSignature(op) + " : ajout\xC3\xA9, son script est pr\xC3\xAAt \xC3\xA0 \xC3\xAA" "tre modifi\xC3\xA9. Ctrl+Z le retire.");
    }
    return made;
}

Id HmiOperatorsPane::addOperator(const std::string& op, const std::string& left, const std::string& right, const std::string& result,
                                 std::string* why) {
    return add(owner_, hmi::draftOperator(doc_->project, op, left, right, result),
               op == hmi::kConversionOp ? "Nouvelle conversion" : "Nouvel op\xC3\xA9rateur", why);
}

Id HmiOperatorsPane::addConversion(std::string* why) {
    if (!owner_.valid()) {
        if (why) *why = "aucun symbole ni type choisi";
        return kNoId;
    }
    for (const auto t : kConversionTargets) {
        const auto o = hmi::draftOperator(doc_->project, hmi::kConversionOp, owner_.name, {}, t);
        if (hmi::operatorProblem(doc_->project, owner_, o, kNoId, isPlcType).empty()) return add(owner_, o, "Nouvelle conversion", why);
    }
    if (why) *why = "toutes les conversions propos\xC3\xA9" "es existent d\xC3\xA9j\xC3\xA0";
    say("Toutes les conversions propos\xC3\xA9" "es existent d\xC3\xA9j\xC3\xA0 : change la cible d'une copie (Dupliquer).", true);
    return kNoId;
}

Id HmiOperatorsPane::addArithmetic(std::string* why) {
    if (!owner_.valid()) {
        if (why) *why = "aucun symbole ni type choisi";
        return kNoId;
    }
    for (const auto s : hmi::kOperatorSymbols) {
        const auto o = hmi::draftOperator(doc_->project, s, owner_.name, owner_.name, owner_.name);
        if (hmi::operatorProblem(doc_->project, owner_, o, kNoId, isPlcType).empty()) return add(owner_, o, "Nouvel op\xC3\xA9rateur", why);
    }
    if (why) *why = "tous les op\xC3\xA9rateurs entre " + owner_.name + " et " + owner_.name + " existent";
    say("Tous les op\xC3\xA9rateurs entre " + owner_.name + " et lui-m\xC3\xAAme existent : change l'op\xC3\xA9rande b d'une copie (Dupliquer).", true);
    return kNoId;
}

bool HmiOperatorsPane::changeSignature(Id id, const std::string& op, const std::string& left, const std::string& right,
                                       const std::string& result, std::string* why) {
    hmi::OperatorOwner who;
    const auto* o = hmi::operatorById(doc_->project, id, &who);
    if (!o) return false;
    hmi::HmiOperator next = *o;
    next.op = op;
    next.left = left;
    next.right = hmi::operatorFamily(op) == hmi::OperatorFamily::Conversion ? std::string{} : right;
    next.result = result;
    if (next.op == o->op && next.left == o->left && next.right == o->right && next.result == o->result) return true;
    if (const auto problem = hmi::operatorProblem(doc_->project, who, next, id, isPlcType); !problem.empty()) {
        if (why) *why = problem;
        say("Refus\xC3\xA9 : " + problem, true);
        return false;
    }
    next = hmi::retitled(*o, next);
    const std::string before = hmi::operatorSignature(*o), after = hmi::operatorSignature(next);
    auto cmd = hmi::changeProject(doc_, "Op\xC3\xA9rateur " + before + " \xE2\x86\x92 " + after, [&](hmi::Project& p) {
        if (auto* x = hmi::operatorById(p, id)) *x = next;
    });
    if (cmd) apply_(std::move(cmd));
    say(before + " devient " + after + " (l'en-t\xC3\xAAte du script suit). Ctrl+Z reprend.");
    return true;
}

bool HmiOperatorsPane::renameOperator(Id id, const std::string& name, std::string* why) {
    const auto* o = hmi::operatorById(doc_->project, id);
    if (!o) return false;
    std::string n = name;
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back()))) n.pop_back();
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.front()))) n.erase(n.begin());
    if (o->op == hmi::kConversionOp) {
        if (upperOf(n).rfind("TO_", 0) == 0) n = n.substr(3);
        return changeSignature(id, o->op, o->left, o->right, n, why);
    }
    std::string op = n;
    for (const auto s : hmi::kOperatorSymbols)
        if (upperOf(n) == hmi::operatorIecName(s)) op = std::string(s);
    if (hmi::operatorFamily(op) == hmi::OperatorFamily::Unknown || op == hmi::kConversionOp) {
        if (why) *why = "op\xC3\xA9rateur inconnu : " + n + " (+ - * / += -= *= /= = <> < > <= >=)";
        say("Refus\xC3\xA9 : op\xC3\xA9rateur inconnu : " + n, true);
        return false;
    }
    const auto f = hmi::operatorFamily(op);
    std::string result = o->result;
    if (f == hmi::OperatorFamily::Comparison) result = "BOOL";
    else if (f == hmi::OperatorFamily::Assignment) result.clear();
    else if (hmi::operatorFamily(o->op) != hmi::OperatorFamily::Arithmetic) result = owner_.valid() ? owner_.name : o->left;
    return changeSignature(id, op, o->left, o->right, result, why);
}

bool HmiOperatorsPane::setDescription(Id id, const std::string& description) {
    const auto* o = hmi::operatorById(doc_->project, id);
    if (!o || o->description == description) return false;
    auto cmd = hmi::changeProject(doc_, "Description de " + hmi::operatorSignature(*o), [&](hmi::Project& p) {
        if (auto* x = hmi::operatorById(p, id)) x->description = description;
    });
    if (cmd) apply_(std::move(cmd));
    return true;
}

bool HmiOperatorsPane::setBody(Id id, const std::string& body) {
    const auto* o = hmi::operatorById(doc_->project, id);
    if (!o || o->body == body) return false;
    auto cmd = hmi::changeProject(doc_, "Saisie dans " + hmi::operatorSignature(*o), [&](hmi::Project& p) {
        if (auto* x = hmi::operatorById(p, id)) x->body = body;
    }, "operator:" + std::to_string(id));
    if (cmd) apply_(std::move(cmd));
    return true;
}

bool HmiOperatorsPane::deleteOperator(Id id, std::string* why) {
    hmi::OperatorOwner who;
    const auto* o = hmi::operatorById(doc_->project, id, &who);
    if (!o) {
        if (why) *why = "op\xC3\xA9rateur introuvable";
        return false;
    }
    const std::string sig = hmi::operatorSignature(*o);
    auto cmd = hmi::changeProject(doc_, "Supprimer l'op\xC3\xA9rateur " + sig, [&](hmi::Project& p) {
        if (auto* list = hmi::operatorsOf(p, who))
            list->erase(std::remove_if(list->begin(), list->end(), [&](const hmi::HmiOperator& x) { return x.id == id; }), list->end());
    });
    if (cmd) apply_(std::move(cmd));
    say("Op\xC3\xA9rateur " + sig + " supprim\xC3\xA9. Ctrl+Z le rend.");
    return true;
}

Id HmiOperatorsPane::duplicateOperator(Id id, std::string* why) {
    hmi::OperatorOwner who;
    const auto* o = hmi::operatorById(doc_->project, id, &who);
    if (!o) {
        if (why) *why = "op\xC3\xA9rateur introuvable";
        return kNoId;
    }
    const hmi::HmiOperator src = *o;
    std::vector<hmi::HmiOperator> tries;
    if (src.op == hmi::kConversionOp) {
        for (const auto t : kConversionTargets) {
            hmi::HmiOperator x = src;
            if (src.left == who.name) x.result = std::string(t);   // une autre cible
            else x.left = std::string(t);                           // une autre source vers le porteur
            tries.push_back(hmi::retitled(src, x));
        }
    } else {
        const auto fam = hmi::operatorFamily(src.op);
        for (const auto s : hmi::kOperatorSymbols) {
            if (hmi::operatorFamily(s) != fam) continue;
            hmi::HmiOperator x = src;
            x.op = std::string(s);
            tries.push_back(hmi::retitled(src, x));
        }
        // Le meme operateur avec un autre type a droite (REAL, DINT, INT).
        for (const auto t : {"REAL", "DINT", "INT", "LREAL"}) {
            hmi::HmiOperator x = src;
            x.right = t;
            tries.push_back(hmi::retitled(src, x));
        }
    }
    for (auto& x : tries) {
        x.id = kNoId;
        if (hmi::operatorProblem(doc_->project, who, x, kNoId, isPlcType).empty()) return add(who, std::move(x), "Dupliquer", why);
    }
    if (why) *why = "aucune signature libre pour la copie";
    say("Pas de copie : aucune signature libre pour " + hmi::operatorSignature(src) + ".", true);
    return kNoId;
}

// ---------------------------------------- 1.10.1 (U2) : "Ajouter un operateur" ---
bool HmiOperatorsPane::openAddDialog(const std::string& kind, const std::string& operand) {
    if (!owner_.valid()) {
        say("Choisis d'abord un symbole ou un type IHM.", true);
        return false;
    }
    std::vector<std::string> operands, targets;
    choiceLists(doc_->project, owner_, plcTypes, operands, targets);
    // Preregle comme la maquette : sur un type, / et REAL ; sur un symbole, une conversion.
    const std::string k = !kind.empty() ? kind : owner_.kind == hmi::OperatorOwner::Kind::Type ? std::string("/") : std::string(hmi::kConversionOp);
    std::string v = operand;
    if (v.empty()) v = k == hmi::kConversionOp ? firstFree(doc_->project, owner_, k, targets, {}, isPlcType) : std::string("REAL");
    dialog_->open(doc_, owner_, k, v, std::move(operands), std::move(targets), isPlcType);
    say("Ajouter un op\xC3\xA9rateur \xC3\xA0 " + owner_.name + " : choisis le genre et l'op\xC3\xA9rande ; Entr\xC3\xA9" "e cr\xC3\xA9" "e, \xC3\x89" "chap ferme.");
    return true;
}

bool HmiOperatorsPane::openDuplicateDialog(Id id) {
    hmi::OperatorOwner who;
    const auto* o = hmi::operatorById(doc_->project, id, &who);
    if (!o) return false;
    if (who.kind != owner_.kind || who.id != owner_.id) setOwner(who);
    std::vector<std::string> operands, targets;
    choiceLists(doc_->project, owner_, plcTypes, operands, targets);
    const bool conversion = o->op == hmi::kConversionOp;
    const std::string v = firstFree(doc_->project, owner_, o->op, conversion ? targets : operands, conversion ? o->result : o->right, isPlcType);
    const bool opened = openAddDialog(o->op, v);
    if (opened) say("Dupliquer : choisis l'autre op\xC3\xA9rande ou l'autre type cible.");
    return opened;
}

Id HmiOperatorsPane::createFromDialog(std::string* why) {
    if (!dialog_->isOpen()) {
        if (why) *why = "la fen\xC3\xAAtre n'est pas ouverte";
        return kNoId;
    }
    if (!dialog_->problem().empty()) {
        if (why) *why = dialog_->problem();
        say("Refus\xC3\xA9 : " + dialog_->problem(), true);
        return kNoId;
    }
    const hmi::HmiOperator op = dialog_->draft();
    const Id made = add(dialog_->owner(), op, op.op == hmi::kConversionOp ? "Nouvelle conversion" : "Nouvel op\xC3\xA9rateur", why);
    if (made == kNoId) return kNoId;
    dialog_->close();
    editor_->takeFocus();
    say(hmi::operatorSignature(op) + " : cr\xC3\xA9\xC3\xA9, son script est ouvert (la l\xC3\xA9gende dit ce que sont a, b et Resultat). Ctrl+Z le retire.");
    return made;
}

HmiOperatorDialog::HmiOperatorDialog(std::string id) : ui::Widget(std::move(id)) {}

void HmiOperatorDialog::open(hmi::DocumentPtr doc, hmi::OperatorOwner owner, std::string kind, std::string operand,
                             std::vector<std::string> operands, std::vector<std::string> targets,
                             std::function<bool(std::string_view)> plcType) {
    doc_ = std::move(doc);
    owner_ = std::move(owner);
    operands_ = std::move(operands);
    targets_ = std::move(targets);
    plcType_ = std::move(plcType);
    kind_ = kind.empty() ? std::string("+") : std::move(kind);
    if (kind_ == hmi::kConversionOp) {
        target_ = std::move(operand);
        if (operand_.empty() || !std::count(operands_.begin(), operands_.end(), operand_)) operand_ = operands_.size() > 1 ? operands_[1] : owner_.name;
    } else {
        operand_ = std::move(operand);
        if (target_.empty() || !std::count(targets_.begin(), targets_.end(), target_)) target_ = targets_.empty() ? std::string("STRING") : targets_.front();
    }
    // Une valeur hors des pastilles (un type du projet) : ajoutee au bout.
    auto& list = kind_ == hmi::kConversionOp ? targets_ : operands_;
    const std::string& v = kind_ == hmi::kConversionOp ? target_ : operand_;
    if (!v.empty() && std::none_of(list.begin(), list.end(), [&](const std::string& x) { return hmi::sameTypeName(x, v); })) list.push_back(v);
    open_ = true;
    rebuild();
    setFocusPolicy(true);                            // ouverte, elle prend le clavier (Entree, Echap)
    grabFocus();
    invalidate();
}

void HmiOperatorDialog::close() {
    if (!open_) return;
    open_ = false;
    releaseFocus();
    setFocusPolicy(false);
    invalidate();
}

void HmiOperatorDialog::setKind(const std::string& op) {
    if (op == kind_) return;
    kind_ = op;
    rebuild();
    invalidate();
}

void HmiOperatorDialog::setOperand(const std::string& type) {
    (kind_ == hmi::kConversionOp ? target_ : operand_) = type;
    rebuild();
    invalidate();
}

std::vector<std::string> HmiOperatorDialog::kinds() const {
    std::vector<std::string> out{std::string(hmi::kConversionOp)};
    for (const auto s : hmi::kOperatorSymbols) out.emplace_back(s);
    return out;
}

std::string HmiOperatorDialog::signature() const { return hmi::operatorSignature(draft_); }
std::string HmiOperatorDialog::legend() const { return hmi::operatorLegend(draft_, owner_.name); }
std::string HmiOperatorDialog::example() const { return doc_ ? hmi::operatorExample(doc_->project, draft_) : std::string{}; }

gfx::Rect HmiOperatorDialog::surface() const {
    const ui::Widget* w = this;
    while (w->parent()) w = w->parent();
    return w->bounds();
}

gfx::Rect HmiOperatorDialog::eventBounds() const { return open_ ? surface() : bounds(); }

bool HmiOperatorDialog::kindRect(std::string_view op, gfx::Rect& out) const {
    for (const auto& c : kindChips_)
        if (c.value == op) { out = c.rect; return open_; }
    return false;
}
bool HmiOperatorDialog::choiceRect(std::string_view type, gfx::Rect& out) const {
    for (const auto& c : choiceChips_)
        if (hmi::sameTypeName(c.value, type)) { out = c.rect; return open_; }
    return false;
}
gfx::Rect HmiOperatorDialog::createRect() const { return open_ ? create_ : gfx::Rect{}; }
gfx::Rect HmiOperatorDialog::cancelRect() const { return open_ ? cancel_ : gfx::Rect{}; }
gfx::Rect HmiOperatorDialog::openLinkRect() const { return open_ && existing_ != kNoId ? link_ : gfx::Rect{}; }

void HmiOperatorDialog::rebuild() {
    // Le brouillon, et ce qui l'empeche (en double : l'operateur existant).
    existing_ = kNoId;
    problem_.clear();
    if (doc_ && owner_.valid()) {
        const auto& p = doc_->project;
        draft_ = draftFor(p, owner_, kind_, kind_ == hmi::kConversionOp ? target_ : operand_);
        problem_ = hmi::operatorProblem(p, owner_, draft_, kNoId, plcType_);
        const std::string fn = hmi::operatorFunctionName(draft_);
        for (const auto& x : hmi::allOperators(p))
            if (hmi::sameTypeName(hmi::operatorFunctionName(*x.op), fn) && hmi::sameTypeName(x.op->left, draft_.left)
                && hmi::sameTypeName(x.op->right, draft_.right))
                existing_ = x.op->id;
    } else {
        draft_ = {};
        problem_ = "aucun symbole ni type choisi";
    }
    // La place de chaque chose : la fenetre au milieu de la surface.
    const gfx::Rect s = surface();
    const float w = std::min(std::max(360.f, s.w - 40.f), 720.f);
    const float inner = w - 32.f;
    const auto flow = [&](std::vector<Chip>& chips, float top) {
        float x = 0.f, y = top;
        for (auto& c : chips) {
            const float cw = chipWidth(c.label);
            if (x > 0.f && x + cw > inner) {
                x = 0.f;
                y += 32.f;
            }
            c.rect = {x, y, cw, 26.f};
            x += cw + 6.f;
        }
        return chips.empty() ? top : y + 26.f;
    };
    kindChips_.clear();
    for (const auto& k : kinds()) kindChips_.push_back({k, k == hmi::kConversionOp ? std::string("Conversion TO_\xE2\x80\xA6") : k, {}});
    choiceChips_.clear();
    for (const auto& v : choices()) choiceChips_.push_back({v, v, {}});
    float y = 52.f;                                  // sous l'en-tete : GENRE
    kindsY_ = y;
    y = flow(kindChips_, y + 18.f) + 14.f;
    choicesY_ = y;                                   // OPERANDE DE DROITE / TYPE CIBLE
    y = flow(choiceChips_, y + 18.f) + 8.f;
    hintY_ = y;
    y += 30.f;                                       // SIGNATURE
    sig_ = {0.f, y + 18.f, inner, 32.f};
    y = sig_.bottom() + (existing_ == kNoId && !problem_.empty() ? 26.f : 12.f);
    {
        // DANS LE SCRIPT : a, b, Resultat (une ligne de plus si la legende est longue).
        std::size_t chars = 0;
        for (const char ch : legend()) chars += (static_cast<unsigned char>(ch) & 0xC0) != 0x80;
        const float lines = 7.4f * static_cast<float>(chars) > inner - 24.f ? 2.f : 1.f;
        legend_ = {0.f, y + 18.f, inner, (lines + 1.f) * kLegendLine + 11.f};
    }
    y = legend_.bottom() + 12.f;
    std::size_t lines = 1;
    for (const char ch : draft_.body) lines += ch == '\n';
    if (!draft_.body.empty() && draft_.body.back() == '\n') --lines;
    code_ = {0.f, y + 18.f, inner, 12.f + 20.f * static_cast<float>(std::min<std::size_t>(lines, 9))};
    const float h = code_.bottom() + 14.f + 56.f;
    box_ = {s.x + (s.w - w) * 0.5f, s.y + std::max(20.f, (s.h - h) * 0.3f), w, h};
    // En coordonnees de la surface.
    const float ox = box_.x + 16.f, oy = box_.y;
    for (auto& c : kindChips_) c.rect = {c.rect.x + ox, c.rect.y + oy, c.rect.w, c.rect.h};
    for (auto& c : choiceChips_) c.rect = {c.rect.x + ox, c.rect.y + oy, c.rect.w, c.rect.h};
    kindsY_ += oy;
    choicesY_ += oy;
    hintY_ += oy;
    sig_ = {sig_.x + ox, sig_.y + oy, sig_.w, sig_.h};
    legend_ = {legend_.x + ox, legend_.y + oy, legend_.w, legend_.h};
    code_ = {code_.x + ox, code_.y + oy, code_.w, code_.h};
    close_ = {box_.right() - 36.f, box_.y + 6.f, 28.f, 28.f};
    create_ = {box_.right() - 16.f - 220.f, box_.bottom() - 44.f, 220.f, 32.f};
    cancel_ = {create_.x - 10.f - 104.f, create_.y, 104.f, 32.f};
    link_ = {sig_.right() - 70.f, sig_.y + 4.f, 64.f, sig_.h - 8.f};
}

void HmiOperatorDialog::onPaintOverlay(const ui::PaintContext& ctx) {
    if (!open_) return;
    rebuild();                                       // la surface a pu changer de taille
    const gfx::Rect s = surface();
    const auto& c = ctx.theme.color;
    const auto& f = ctx.theme.font;
    ctx.r.fillRect(s, gfx::Color{0, 0, 0, 120});
    ctx.r.fillRoundedRect(box_, c.panelBg, 8.f);
    ctx.r.strokeRect(box_, c.accent, 1.f);
    ctx.r.fillRect({box_.x + 1.f, box_.y + 1.f, box_.w - 2.f, 39.f}, c.headerBg);
    const std::string title = "Ajouter un op\xC3\xA9rateur \xC3\xA0 " + owner_.name;
    ctx.r.drawText({box_.x + 16.f, box_.y + 11.f}, title, f.uiBold, c.text);
    ctx.r.drawText({close_.x + 8.f, close_.y + 5.f}, "\xE2\x9C\x95", f.ui, c.textMuted);
    const auto label = [&](float y, std::string_view text, std::string_view more = {}) {
        ctx.r.drawText({box_.x + 16.f, y}, text, f.smallUi, c.textMuted);
        if (!more.empty())
            ctx.r.drawText({box_.x + 22.f + ctx.r.measure(text, f.smallUi).width, y}, more, f.smallUi, c.textDisabled);
    };
    const auto chips = [&](const std::vector<Chip>& list, const std::string& selected) {
        for (const auto& ch : list) {
            const bool on = hmi::sameTypeName(ch.value, selected) || ch.value == selected;
            if (on) ctx.r.fillRoundedRect(ch.rect, c.accent, 12.f);
            else {
                ctx.r.fillRoundedRect(ch.rect, c.border, 12.f);
                ctx.r.fillRoundedRect({ch.rect.x + 1.f, ch.rect.y + 1.f, ch.rect.w - 2.f, ch.rect.h - 2.f}, c.inputBg, 11.f);
            }
            const auto font = on ? f.uiBold : f.ui;
            const float tw = ctx.r.measure(ch.label, font).width;
            ctx.r.drawText({ch.rect.x + (ch.rect.w - tw) * 0.5f, ch.rect.y + (ch.rect.h - ctx.r.lineHeight(font)) * 0.5f}, ch.label, font,
                           on ? c.selectionText : c.text);
        }
    };
    const bool conversion = kind_ == hmi::kConversionOp;
    label(kindsY_, "GENRE");
    chips(kindChips_, kind_);
    if (conversion) label(choicesY_, "TYPE CIBLE");
    else label(choicesY_, "OP\xC3\x89RANDE DE DROITE", "(\xC3\xA0 gauche : " + owner_.name + ")");
    chips(choiceChips_, conversion ? target_ : operand_);
    const std::string hint = conversion
        ? "Un type de base ou un type IHM (un DDT de l'automate : la Cible des propri\xC3\xA9t\xC3\xA9s) ; ailleurs on \xC3\xA9" "crit "
              + hmi::operatorFunctionName(draft_) + "(x)."
        : "Son propre type ou un type externe ; l'ordre compte : " + owner_.name + " * REAL et REAL * " + owner_.name
              + " sont deux op\xC3\xA9rateurs.";
    ctx.r.drawText({box_.x + 16.f, hintY_}, hint, f.smallUi, c.textMuted);
    // La signature : nouveau, ou existe deja (l'ouvrir), ou impossible (la raison dessous).
    label(sig_.y - 18.f, "SIGNATURE");
    const bool bad = existing_ != kNoId || !problem_.empty();
    ctx.r.fillRect(sig_, c.inputBg);
    ctx.r.strokeRect(sig_, bad ? c.error : c.ok, 1.f);
    ctx.r.drawText({sig_.x + 10.f, sig_.y + (sig_.h - ctx.r.lineHeight(f.mono)) * 0.5f}, signature(), f.mono, c.text);
    const float midY = sig_.y + (sig_.h - ctx.r.lineHeight(f.ui)) * 0.5f;
    if (existing_ != kNoId) {
        const std::string_view lead = "existe d\xC3\xA9j\xC3\xA0  \xC2\xB7  ";
        const float lw = ctx.r.measure(lead, f.ui).width;
        ctx.r.drawText({link_.x - lw, midY}, lead, f.ui, c.error);
        ctx.r.drawText({link_.x, midY}, "l'ouvrir", f.uiBold, c.accent);
        ctx.r.line({link_.x, midY + ctx.r.lineHeight(f.ui)}, {link_.x + ctx.r.measure("l'ouvrir", f.uiBold).width, midY + ctx.r.lineHeight(f.ui)},
                   c.accent, 1.f);
    } else if (!problem_.empty()) {
        const std::string_view no = "impossible";
        ctx.r.drawText({sig_.right() - 10.f - ctx.r.measure(no, f.ui).width, midY}, no, f.ui, c.error);
        ctx.r.drawText({sig_.x, sig_.bottom() + 5.f}, "Refus\xC3\xA9 : " + problem_, f.smallUi, c.error);
    } else {
        const std::string_view yes = "nouveau";
        ctx.r.drawText({sig_.right() - 10.f - ctx.r.measure(yes, f.ui).width, midY}, yes, f.ui, c.ok);
    }
    // La legende : ce que sont a, b et Resultat dans le script, et un exemple.
    label(legend_.y - 18.f, "DANS LE SCRIPT");
    paintLegend(ctx, legend_, legend(), example());
    // Le script prerempli (et son equivalent C++).
    label(code_.y - 18.f, "SCRIPT PR\xC3\x89REMPLI", "(C++ : " + hmi::operatorCppSignature(draft_, owner_.name) + ")");
    ctx.r.fillRect(code_, c.inputBg);
    ctx.r.strokeRect(code_, c.border, 1.f);
    ctx.r.pushClip(code_);
    float ly = code_.y + 6.f;
    for (std::size_t at = 0; at < draft_.body.size() && ly < code_.bottom() - 4.f;) {
        auto nl = draft_.body.find('\n', at);
        if (nl == std::string::npos) nl = draft_.body.size();
        const std::string_view l = std::string_view(draft_.body).substr(at, nl - at);
        const auto first = l.find_first_not_of(" \t");
        const bool comment = first != std::string_view::npos && l.substr(first, 2) == "(*";
        // 1.10.1 (I1101) : une ligne trop longue (TO_STRING := CONCAT(...)) finit par "...".
        ctx.r.drawText({code_.x + 10.f, ly}, elided(ctx, l, f.mono, code_.w - 20.f), f.mono, comment ? c.syntaxComment : c.text);
        ly += 20.f;
        at = nl + 1;
    }
    ctx.r.popClip();
    // Le pied : ce qui se passe, Annuler, Creer et ouvrir le script.
    ctx.r.line({box_.x, create_.y - 12.f}, {box_.right(), create_.y - 12.f}, c.border, 1.f);
    ctx.r.drawText({box_.x + 16.f, create_.y + (create_.h - ctx.r.lineHeight(f.smallUi)) * 0.5f},
                   "Le script s'ouvre dans l'\xC3\xA9" "diteur ; Ctrl+Z annule.", f.smallUi, c.textMuted);
    const auto button = [&](gfx::Rect r, std::string_view text, bool primary, bool enabled) {
        const gfx::Color bg = primary ? (enabled ? c.accent : c.border) : c.inputBg;
        ctx.r.fillRoundedRect(r, primary ? bg : c.border, 5.f);
        if (!primary) ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, bg, 4.f);
        const auto font = primary ? f.uiBold : f.ui;
        const float tw = ctx.r.measure(text, font).width;
        ctx.r.drawText({r.x + (r.w - tw) * 0.5f, r.y + (r.h - ctx.r.lineHeight(font)) * 0.5f}, text, font,
                       primary ? (enabled ? c.selectionText : c.textDisabled) : c.text);
    };
    button(cancel_, "Annuler", false, true);
    button(create_, "Cr\xC3\xA9" "er et ouvrir le script", true, problem_.empty());
}

ui::EventResult HmiOperatorDialog::onEvent(const ui::InputEvent& ev) {
    if (!open_) return ui::EventResult::Ignored;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) close();
        else if (k->key == ui::Key::Return && problem_.empty() && onCreate) onCreate();
        return ui::EventResult::Consumed;                // la fenetre est modale : rien ne passe dessous
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (d->button != ui::MouseButton::Left) return ui::EventResult::Consumed;
        const gfx::Point p = d->pos;
        if (close_.contains(p) || cancel_.contains(p)) {
            close();
            return ui::EventResult::Consumed;
        }
        if (create_.contains(p)) {
            if (problem_.empty() && onCreate) onCreate();
            return ui::EventResult::Consumed;
        }
        if (existing_ != kNoId && link_.contains(p)) {
            if (onOpen) onOpen(existing_);
            return ui::EventResult::Consumed;
        }
        for (const auto& c : kindChips_)
            if (c.rect.contains(p)) {
                setKind(c.value);
                return ui::EventResult::Consumed;
            }
        for (const auto& c : choiceChips_)
            if (c.rect.contains(p)) {
                setOperand(c.value);
                return ui::EventResult::Consumed;
            }
        return ui::EventResult::Consumed;
    }
    if (std::get_if<ui::FocusChange>(&ev)) return ui::EventResult::Ignored;
    return ui::EventResult::Consumed;
}

void HmiOperatorsPane::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    split_->setBounds({b.x, b.y + 38, b.w, std::max(0.f, b.h - 62)});
    dialog_->setBounds(b);
}

void HmiOperatorsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
