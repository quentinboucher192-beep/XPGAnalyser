// app/MacroEditorView.cpp - le mode Modifier d'une macro (lot API 6).
#include "MacroEditorView.hpp"

#include "MacroFormView.hpp"
#include "hmi/HmiIcons.hpp"
#include "hmi/HmiPanels.hpp"

#include "../project/Macro.hpp"
#include "../project/MacroSession.hpp"
#include "../project/MacroSpecWriter.hpp"
#include "../sim/Interpreter.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../xls/MacroXls.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <set>

namespace app {

using ui::RowIndex;
using PG = ui::PropertyGrid;
namespace mac = project::macro;

namespace {

const gfx::FontId kSmall{13};
constexpr float kSideW = 470.f;
constexpr float kTrialH = 170.f;

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string restAfterFirstWord(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (a < s.size() && !std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

bool hasWord(const std::string& s, std::string_view word) {
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i;
        std::size_t j = i;
        while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j])) && s[j] != ',') ++j;
        if (lower(s.substr(i, j - i)) == lower(word)) return true;
        i = j;
    }
    return false;
}

// La ligne (1...) de l'appel Ask('cle', ...) ; 0 : aucune.
std::size_t askLineOf(const std::string& text, const std::string& key) {
    const std::string a = "ask('" + lower(key) + "'";
    std::size_t line = 1, start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        std::string l = lower(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        l.erase(std::remove(l.begin(), l.end(), ' '), l.end());
        if (l.find(a) != std::string::npos) return line;
        if (end == std::string::npos) break;
        start = end + 1;
        ++line;
    }
    return 0;
}

// Les genres, dans l'ordre de la liste de la fiche.
const std::vector<std::string>& kindKeys() {
    static const std::vector<std::string> k = [] {
        std::vector<std::string> out;
        for (int i = 0; i <= static_cast<int>(mac::FieldKind::Checks); ++i)
            out.emplace_back(mac::kindKey(static_cast<mac::FieldKind>(i)));
        return out;
    }();
    return k;
}

} // namespace

// ============================================================== les cartes ====
class MacroEditorView::CardsModel final : public ui::ITableModel {
public:
    explicit CardsModel(MacroEditorView& v) : v_(v) {}
    [[nodiscard]] std::size_t rowCount() const override { return v_.cardRows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? "Question" : c == 1 ? "Cl\xC3\xA9" : "Genre";
    }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= v_.cardRows_.size()) return {};
        const auto& card = v_.cardRows_[r];
        if (card.header) {
            if (c != 0) return {};
            return (card.group.empty() ? std::string("Sans groupe") : card.group) + "   " + std::to_string(card.count);
        }
        const auto* f = v_.spec_.field(card.key);
        if (!f) return c == 1 ? card.key : std::string{};
        switch (c) {
            case 0: return f->label.empty() ? f->askPrompt : f->label;
            case 1: return card.key;
            default: {
                std::string k(mac::kindKey(f->kind));
                if (f->optional) k += " \xC2\xB7 facult.";
                return k;
            }
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= v_.cardRows_.size()) return s;
        const auto& card = v_.cardRows_[r];
        if (card.header) {
            if (c == 0) {
                s.spanRow = true;
                s.bold = true;
                s.fgTone = ui::Tone::Muted;
                s.icon = ui::Icon::Folder;
                s.iconTone = ui::Tone::Muted;
            }
            return s;
        }
        const auto* f = v_.spec_.field(card.key);
        switch (c) {
            case 0:
                s.indent = 14.f;
                s.icon = ui::Icon::Document;
                s.iconTone = ui::Tone::Info;
                if (f && v_.spec_.isAdvanced(card.key)) {
                    s.badge = "avanc\xC3\xA9" "e";
                    s.badgeTone = ui::Tone::Muted;
                }
                if (f && !f->declared) s.fgTone = ui::Tone::Warning;
                break;
            case 1: s.monospace = true; s.fgTone = ui::Tone::Muted; break;
            default: s.fgTone = ui::Tone::Muted; break;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool canDropRows(const std::vector<RowIndex>& from, RowIndex to, ui::TreeView::DropWhere where) const override {
        if (from.size() != 1 || from.front() >= v_.cardRows_.size() || v_.cardRows_[from.front()].header) return false;
        if (to == ui::TableView::kNoRow) return true;
        if (to >= v_.cardRows_.size() || to == from.front()) return false;
        // Sur un titre : dans ce groupe ; sur une carte : avant ou apres elle.
        return v_.cardRows_[to].header ? true : where != ui::TreeView::DropWhere::Into;
    }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= v_.cardRows_.size()) return {};
        const auto& card = v_.cardRows_[r];
        if (card.header) return "Glisse une carte ici : elle passe dans ce groupe (sa ligne #! groupe est r\xC3\xA9\xC3\xA9" "crite).";
        const auto* f = v_.spec_.field(card.key);
        std::string t = card.key + " \xC2\xB7 " + (f ? mac::describe(*f) : std::string("?"));
        if (f && !f->declared) t += " \xC2\xB7 pas de ligne #! champ : le formulaire la d\xC3\xA9" "duit du Ask";
        return t;
    }
private:
    MacroEditorView& v_;
};

// ============================================================ les remarques ====
class MacroEditorView::RemarksModel final : public ui::ITableModel {
public:
    explicit RemarksModel(MacroEditorView& v) : v_(v) {}
    [[nodiscard]] std::size_t rowCount() const override { return v_.remarks_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "Ligne" : "Remarque"; }
    [[nodiscard]] std::string cellText(RowIndex r, std::size_t c) const override {
        if (r >= v_.remarks_.size()) return {};
        const auto& m = v_.remarks_[r];
        if (c == 0) return m.line ? std::to_string(m.line) : std::string{};
        return m.text;
    }
    [[nodiscard]] ui::CellStyle cellStyle(RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= v_.remarks_.size()) return s;
        const auto& m = v_.remarks_[r];
        const ui::Tone tone = m.kind == Remark::Kind::Ok        ? ui::Tone::Ok
                            : m.kind == Remark::Kind::Warning   ? ui::Tone::Warning
                            : m.kind == Remark::Kind::Error     ? ui::Tone::Error
                                                                : ui::Tone::Info;
        if (c == 0) {
            s.icon = m.kind == Remark::Kind::Ok ? ui::Icon::Ok : m.kind == Remark::Kind::Error ? ui::Icon::Error
                   : m.kind == Remark::Kind::Warning ? ui::Icon::Warning : ui::Icon::Info;
            s.iconTone = tone;
            s.monospace = true;
            s.fgTone = ui::Tone::Muted;
        } else if (m.kind == Remark::Kind::Error) {
            s.fgTone = ui::Tone::Error;
        }
        return s;
    }
    [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(RowIndex r) const override {
        if (r >= v_.remarks_.size() || !v_.remarks_[r].line) return {};
        return "Double-clic : la ligne " + std::to_string(v_.remarks_[r].line) + " dans le code.";
    }
private:
    MacroEditorView& v_;
};

// =============================================================== la vue ====
MacroEditorView::MacroEditorView(std::string id, std::string name, std::string text)
    : ui::Widget(std::move(id)), name_(std::move(name)), saved_(text) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".outils");
    tools->add(ASave, HmiGlyph::Export, "Enregistrer dans libs/Macros : la version suivante, et ce qui change (Ctrl+S)", "Enregistrer");
    tools->add(AVersion, HmiGlyph::Star, "La version du fichier, et celle qu'\xC3\xA9" "crira Enregistrer", "Version");
    tools->separator();
    tools->add(AUndo, HmiGlyph::Undo, "Annuler dans l'\xC3\xA9" "diteur : une frappe, une carte d\xC3\xA9plac\xC3\xA9" "e (Ctrl+Z)");
    tools->add(ARedo, HmiGlyph::Redo, "R\xC3\xA9tablir dans l'\xC3\xA9" "diteur (Ctrl+Y)");
    tools->separator();
    tools->add(ATry, HmiGlyph::Play, "Essayer : la macro rejou\xC3\xA9" "e en aper\xC3\xA7u sur le projet ouvert, tout est d\xC3\xA9" "fait (F5)", "Essayer (F5)");
    tools->add(ACheck, HmiGlyph::Check, "V\xC3\xA9rifier l'en-t\xC3\xAA" "te (#!) et les questions, sans rien lancer", "V\xC3\xA9rifier");
    tools->separator();
    tools->add(AUse, HmiGlyph::View, "Utiliser : le formulaire de la macro, avec le code tel qu'il est \xC3\xA0 l'\xC3\xA9" "cran (rien n'est \xC3\xA9" "crit avant Appliquer)",
               "Utiliser");
    tools->add(AShow, HmiGlyph::List, "Revenir \xC3\xA0 la fiche de la macro et \xC3\xA0 la liste (Utiliser) ; ce qui n'est pas enregistr\xC3\xA9 reste ici", "Fiche");
    tools->add(AHelp, HmiGlyph::Help, "Les fonctions des macros (F1)", "Fonctions");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(AUndo, [this] { return !undo_.empty() || source() != lastSnapshot_; });
    tools_->setEnabledWhen(ARedo, [this] { return !redo_.empty(); });
    tools_->setEnabledWhen(AUse, [this] { return static_cast<bool>(hosts_.use); });
    links_ += tools_->triggered->connect([this](int a) { runAction(a); });

    auto code = std::make_unique<ui::MultiLineText>(base + ".code");
    code->setShowLineNumbers(true);
    code->setLanguage(ui::Language::StructuredText);
    code->setReadOnly(false);
    code->setTabInsertsSpaces(4);
    code->setText(text);
    code_ = &static_cast<ui::MultiLineText&>(addChild(std::move(code)));
    code_->setCompletionProvider([this](std::string_view prefix, std::vector<ui::MultiLineText::Completion>& out) {
        std::vector<std::pair<std::string, std::string>> words;
        complete(prefix, words);
        int rank = 0;
        for (auto& [w, d] : words) {
            ui::MultiLineText::Completion c;
            c.text = w;
            c.detail = d;
            c.rank = 100 + rank++;
            out.push_back(std::move(c));
        }
    });
    links_ += code_->textChanged->connect([this](const std::string&) {
        if (settingText_) return;
        typing_ = true;
        reparseAt_ = now_ + 0.5;
        redo_.clear();
    });

    auto remarks = std::make_unique<ui::TableView>(base + ".essai");
    remarksModel_ = std::make_shared<RemarksModel>(*this);
    remarks->setModel(remarksModel_);
    {
        ui::TableView::Column lineCol{"Ligne", 90.f, 60.f, true, false};
        ui::TableView::Column textCol{"Remarque", 900.f, 200.f, true, false};
        remarks->setColumns({lineCol, textCol});
    }
    remarks->setSelectionMode(ui::SelectionMode::Single);
    remarkTable_ = &static_cast<ui::TableView&>(addChild(std::move(remarks)));
    links_ += remarkTable_->activated->connect([this](RowIndex r) {
        if (r < remarks_.size() && remarks_[r].line) goToLine(remarks_[r].line);
    });

    auto q = std::make_unique<ui::ToggleButton>("Questions", base + ".onglet.questions");
    tabQuestions_ = &static_cast<ui::ToggleButton&>(addChild(std::move(q)));
    auto pv = std::make_unique<ui::ToggleButton>("Aper\xC3\xA7u du formulaire", base + ".onglet.apercu");
    tabPreview_ = &static_cast<ui::ToggleButton&>(addChild(std::move(pv)));
    links_ += tabQuestions_->toggled->connect([this](bool) { if (!syncing_) setSideTab(0); });
    links_ += tabPreview_->toggled->connect([this](bool) { if (!syncing_) setSideTab(1); });

    auto cards = std::make_unique<ui::TableView>(base + ".cartes");
    cardsModel_ = std::make_shared<CardsModel>(*this);
    cards->setModel(cardsModel_);
    {
        ui::TableView::Column label{"Question", 240.f, 120.f, true, false};
        ui::TableView::Column key{"Cl\xC3\xA9", 110.f, 60.f, true, false};
        ui::TableView::Column kind{"Genre", 110.f, 60.f, true, false};
        cards->setColumns({label, key, kind});
    }
    cards->setSelectionMode(ui::SelectionMode::Single);
    cards->setRowDragEnabled(true);
    cards_ = &static_cast<ui::TableView&>(addChild(std::move(cards)));
    links_ += cards_->selectionChanged->connect([this](const std::vector<RowIndex>& rows) {
        if (syncing_ || rows.empty() || rows.front() >= cardRows_.size()) return;
        const auto& card = cardRows_[rows.front()];
        if (card.header) return;
        selected_ = card.key;
        rebuildProperties();
        markLines();
    });
    links_ += cards_->rowsDropped->connect([this](const std::vector<RowIndex>& from, RowIndex to, ui::TreeView::DropWhere where) {
        if (from.size() != 1 || from.front() >= cardRows_.size() || cardRows_[from.front()].header) return;
        const std::string key = cardRows_[from.front()].key;
        std::string group;
        std::size_t index = static_cast<std::size_t>(-1);
        if (to == ui::TableView::kNoRow || to >= cardRows_.size()) {
            group = cardRows_.empty() ? std::string{} : cardRows_.back().group;
        } else if (cardRows_[to].header) {
            group = cardRows_[to].group;
            index = 0;
        } else {
            group = cardRows_[to].group;
            // La place dans le groupe vise, sans compter la carte tiree.
            std::size_t pos = 0;
            for (std::size_t i = 0; i < cardRows_.size(); ++i) {
                if (cardRows_[i].header || cardRows_[i].group != group || cardRows_[i].key == key) continue;
                if (i == to) break;
                ++pos;
            }
            index = pos + (where == ui::TreeView::DropWhere::After ? 1u : 0u);
        }
        (void)moveQuestion(key, group, index);
    });

    auto add = std::make_unique<ui::Button>("+ Ajouter une question", base + ".ajouter");
    add->setStyle(ui::Button::Style::Flat);
    add->setTooltip("\xC3\x89" "crit ses lignes #! champ et #! libelle, et son appel Ask apr\xC3\xA8s le dernier");
    addButton_ = &static_cast<ui::Button&>(addChild(std::move(add)));
    links_ += addButton_->clicked->connect([this] {
        if (!hosts_.askQuestion) return;
        hosts_.askQuestion([this](const std::string& key, const std::string& kind, const std::string& label) {
            (void)addQuestion(key, kind, label);
        });
    });

    props_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".fiche")));
    props_->setShowDescriptionPane(true);

    auto form = std::make_unique<MacroFormView>(base + ".apercu");
    form_ = &static_cast<MacroFormView&>(addChild(std::move(form)));

    lastSnapshot_ = text;
    reparse();
    setSideTab(0);
    setRemarks({{Remark::Kind::Info, 0, "F5 essaie la macro en aper\xC3\xA7u sur le projet ouvert ; ses remarques s'affichent ici, un double-clic m\xC3\xA8ne \xC3\xA0 leur ligne."}},
               "ESSAI (F5)");
}

MacroEditorView::~MacroEditorView() = default;

void MacroEditorView::setHosts(Hosts hosts) {
    hosts_ = std::move(hosts);
    MacroFormHosts fh;
    fh.names = [this](mac::FieldKind kind) {
        std::vector<std::pair<std::string, std::string>> out;
        const auto p = hosts_.project ? hosts_.project() : nullptr;
        if (!p) return out;
        const auto text = [&](domain::SymbolId id) { return std::string(p->strings.text(id)); };
        switch (kind) {
            case mac::FieldKind::Task:
                for (const auto& t : p->tasks) out.emplace_back(text(t.name), "t\xC3\xA2" "che");
                break;
            case mac::FieldKind::Section:
                for (const auto& s : p->sections)
                    if (!s.isSubroutine) out.emplace_back(text(s.name), std::to_string(s.lineCount) + " lignes");
                break;
            case mac::FieldKind::Unit:
                for (const auto& pou : p->pous)
                    if (pou.kind == domain::PouKind::ProgramUnit) out.emplace_back(text(pou.name), "unit\xC3\xA9");
                break;
            default: break;
        }
        return out;
    };
    fh.sheets = [] { return xls::MacroXls::instance().sheetNames(); };
    fh.lastWorkbook = [] { return xls::MacroXls::instance().loadedWorkbook(); };
    form_->setHosts(std::move(fh));
    rebuildPreview();
}

std::string MacroEditorView::source() const { return code_->text(); }

void MacroEditorView::setSource(const std::string& text, bool undoable) {
    const std::string before = source();
    if (text == before) return;
    if (undoable) remember(before);
    const auto first = code_->firstVisibleLine();
    settingText_ = true;
    code_->setText(text);
    settingText_ = false;
    code_->scrollToLine(first);
    lastSnapshot_ = text;
    typing_ = false;
    reparseAt_ = -1.0;
    reparse();
}

void MacroEditorView::remember(const std::string& before) {
    // Une frappe pas encore relevee : elle devient une entree d'annulation.
    if (!lastSnapshot_.empty() && lastSnapshot_ != before) undo_.push_back(lastSnapshot_);
    undo_.push_back(before);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
}

bool MacroEditorView::undoStep() {
    const std::string now = source();
    std::string back;
    if (now != lastSnapshot_) back = lastSnapshot_;          // la frappe pas encore relevee
    else if (!undo_.empty()) {
        back = undo_.back();
        undo_.pop_back();
    } else {
        return false;
    }
    redo_.push_back(now);
    const auto first = code_->firstVisibleLine();
    settingText_ = true;
    code_->setText(back);
    settingText_ = false;
    code_->scrollToLine(first);
    lastSnapshot_ = back;
    typing_ = false;
    reparseAt_ = -1.0;
    reparse();
    return true;
}

bool MacroEditorView::redoStep() {
    if (redo_.empty()) return false;
    const std::string next = redo_.back();
    redo_.pop_back();
    undo_.push_back(source());
    const auto first = code_->firstVisibleLine();
    settingText_ = true;
    code_->setText(next);
    settingText_ = false;
    code_->scrollToLine(first);
    lastSnapshot_ = next;
    reparse();
    return true;
}

void MacroEditorView::tick(double now) {
    now_ = now;
    if (reparseAt_ >= 0.0 && now >= reparseAt_) {
        reparseAt_ = -1.0;
        const std::string text = source();
        if (text != lastSnapshot_) {
            undo_.push_back(lastSnapshot_);
            if (undo_.size() > 200) undo_.erase(undo_.begin());
            lastSnapshot_ = text;
        }
        typing_ = false;
        reparse();
    }
}

void MacroEditorView::goToLine(std::size_t line) {
    if (line == 0) return;
    code_->goToLine(line - 1);
    code_->takeFocus();
    markLines();
}

// ---- le releve -------------------------------------------------------------------
void MacroEditorView::reparse() {
    spec_ = mac::parseMacroSpec(source(), name_);
    if (!selected_.empty() && !spec_.field(selected_)) selected_.clear();
    rebuildCards();
    if (selected_.empty())
        if (const auto keys = questionKeys(); !keys.empty()) {
            selected_ = keys.front();
            rebuildCards();
        }
    rebuildProperties();
    rebuildPreview();
    markLines();
    const std::string v = spec_.version.empty() ? std::string("sans version") : spec_.version;
    tools_->setText(AVersion, "La version du fichier, et celle qu'\xC3\xA9" "crira Enregistrer",
                    dirty() ? "Version " + v + " \xE2\x86\x92 " + nextVersion() : "Version " + v);
    invalidate();
}

std::vector<std::string> MacroEditorView::questionKeys() const {
    std::vector<std::string> out;
    for (const auto& c : cardRows_)
        if (!c.header) out.push_back(c.key);
    if (!out.empty() || spec_.fields.empty()) return out;
    for (const auto& f : spec_.fields) out.push_back(f.key);
    return out;
}

std::vector<std::string> MacroEditorView::groupNames() const {
    std::vector<std::string> out;
    for (const auto& g : spec_.groups) out.push_back(g.name);
    return out;
}

void MacroEditorView::rebuildCards() {
    cardRows_.clear();
    std::set<std::string> placed;
    for (const auto& g : spec_.groups) {
        Card head;
        head.header = true;
        head.group = g.name;
        const auto at = cardRows_.size();
        cardRows_.push_back(head);
        for (const auto& k : g.keys) {
            if (!spec_.field(k) || placed.count(k)) continue;
            placed.insert(k);
            Card c;
            c.group = g.name;
            c.key = k;
            cardRows_.push_back(c);
            ++cardRows_[at].count;
        }
    }
    std::vector<Card> loose;
    for (const auto& f : spec_.fields) {
        if (placed.count(f.key)) continue;
        placed.insert(f.key);
        Card c;
        c.key = f.key;
        loose.push_back(c);
    }
    if (!loose.empty()) {
        Card head;
        head.header = true;
        head.count = loose.size();
        cardRows_.push_back(head);
        cardRows_.insert(cardRows_.end(), loose.begin(), loose.end());
    }
    cardsModel_->modelReset->emit();
    syncing_ = true;
    for (std::size_t i = 0; i < cardRows_.size(); ++i)
        if (!cardRows_[i].header && cardRows_[i].key == selected_) cards_->selectModelRows({static_cast<RowIndex>(i)}, false);
    syncing_ = false;
}

std::string MacroEditorView::champValue(const std::string& key) const {
    for (const auto& h : mac::headerLines(source()))
        if (h.word == "champ" && h.key == key) return h.value;
    return {};
}

void MacroEditorView::rebuildProperties() {
    std::vector<PG::Category> cats;
    const auto* f = selected_.empty() ? nullptr : spec_.field(selected_);
    if (!f) {
        PG::Category c;
        c.name = "Questions";
        c.properties.push_back({"", spec_.fields.empty() ? std::string("Cette macro ne pose aucune question.")
                                                          : std::string("Choisis une carte : sa fiche s'affiche ici."),
                                PG::ValueType::ReadOnly, {}, {}, nullptr});
        cats.push_back(std::move(c));
        props_->setCategories(std::move(cats));
        return;
    }
    const std::string key = selected_;
    PG::Category c;
    c.name = "Question \xC2\xAB " + key + " \xC2\xBB";
    c.properties.push_back({"Cl\xC3\xA9", key, PG::ValueType::ReadOnly, "Le nom de la r\xC3\xA9ponse dans le code (Ask('" + key + "', ...)).", {}, nullptr});
    c.properties.push_back({"Libell\xC3\xA9", f->label, PG::ValueType::Text, "Ce que lit celui qui lance la macro (#! libelle), accents permis.", {},
                            [this, key](std::string_view v) { return setQuestionLabel(key, std::string(v)); }});
    c.properties.push_back({"Genre", std::string(mac::kindKey(f->kind)), PG::ValueType::Enum,
                            "Le widget du formulaire (#! champ) : nom, fichier, t\xC3\xA2" "che, section, nombre, choix...", kindKeys(),
                            [this, key](std::string_view v) { return setQuestionKind(key, std::string(v)); }});
    c.properties.push_back({"Facultative", f->optional ? "TRUE" : "FALSE", PG::ValueType::Boolean, "Vide permis (le mot \xC2\xAB facultatif \xC2\xBB de #! champ).", {},
                            [this, key](std::string_view v) { return setQuestionOptional(key, v == "TRUE"); }});
    std::vector<std::string> groups = groupNames();
    groups.insert(groups.begin(), "(sans groupe)");
    const std::string group = spec_.groupOf(key);
    c.properties.push_back({"Groupe", group.empty() ? std::string("(sans groupe)") : group, PG::ValueType::Enum,
                            "Le titre sous lequel elle s'affiche (#! groupe) ; une carte gliss\xC3\xA9" "e sur un titre fait pareil.", groups,
                            [this, key](std::string_view v) { return setQuestionGroup(key, v == "(sans groupe)" ? std::string{} : std::string(v)); }});
    c.properties.push_back({"Avanc\xC3\xA9" "e", spec_.isAdvanced(key) ? "TRUE" : "FALSE", PG::ValueType::Boolean,
                            "Dans \xC2\xAB R\xC3\xA9glages avanc\xC3\xA9s \xC2\xBB, repli\xC3\xA9s au lancement (#! avance).", {},
                            [this, key](std::string_view v) { return setQuestionAdvanced(key, v == "TRUE"); }});
    c.properties.push_back({"Valeur par d\xC3\xA9" "faut", f->askPreset, PG::ValueType::ReadOnly, "Le troisi\xC3\xA8me argument de Ask, dans le code.", {}, nullptr});
    cats.push_back(std::move(c));
    PG::Category h;
    h.name = "Aide (#! param)";
    h.properties.push_back({"Aide", f->help, PG::ValueType::Text, "La phrase sous le champ, et dans l'aide de la macro.", {},
                            [this, key](std::string_view v) { return setQuestionHelp(key, std::string(v)); }});
    cats.push_back(std::move(h));
    PG::Category w;
    w.name = "Dans le code";
    std::string lines;
    for (const auto l : mac::linesOf(source(), key)) lines += (lines.empty() ? "" : ", ") + std::to_string(l + 1);
    w.properties.push_back({"Lignes", lines.empty() ? std::string("\xE2\x80\x94") : lines, PG::ValueType::ReadOnly,
                            "Ses lignes #!, et l'appel Ask - \xC3\xA9" "clair\xC3\xA9" "es dans le code.", {}, nullptr});
    if (!f->declared)
        w.properties.push_back({"", "pas de ligne #! champ : le formulaire la d\xC3\xA9" "duit du Ask", PG::ValueType::ReadOnly, {}, {}, nullptr});
    cats.push_back(std::move(w));
    props_->setCategories(std::move(cats));
}

void MacroEditorView::rebuildPreview() {
    std::vector<FormField> fields;
    for (const auto& k : questionKeys()) {
        const auto* f = spec_.field(k);
        if (!f) continue;
        FormField ff;
        ff.key = k;
        ff.spec = *f;
        ff.label = f->label.empty() ? f->askPrompt : f->label;
        ff.help = f->help;
        ff.group = spec_.groupOf(k);
        ff.advanced = spec_.isAdvanced(k);
        ff.value = f->askPreset;
        ff.proposed = f->askPreset;
        fields.push_back(std::move(ff));
    }
    form_->setGroupOrder(groupNames());
    form_->setGroupKeys(spec_.groups);
    form_->show(fields, now_);
    form_->setMessage(spec_.summary.empty() ? name_ : name_ + " \xE2\x80\x94 " + spec_.summary, ui::Tone::None);
}

void MacroEditorView::markLines() {
    std::vector<std::pair<std::size_t, gfx::Color>> marks;
    if (!selected_.empty())
        for (const auto l : mac::linesOf(source(), selected_)) marks.emplace_back(l, gfx::Color{76, 184, 232, 46});
    for (const auto& r : remarks_)
        if (r.line && (r.kind == Remark::Kind::Error || r.kind == Remark::Kind::Warning))
            marks.emplace_back(r.line - 1, r.kind == Remark::Kind::Error ? gfx::Color{228, 87, 79, 60} : gfx::Color{245, 197, 66, 50});
    code_->setMarkedLines(std::move(marks));
}

// ---- les gestes des cartes --------------------------------------------------------
bool MacroEditorView::rewrite(const std::string& next, const std::string& what) {
    if (next == source()) return true;
    setSource(next, true);
    if (hosts_.status) hosts_.status(what + " \xE2\x80\x94 les lignes #! sont r\xC3\xA9\xC3\xA9" "crites (Ctrl+Z dans l'\xC3\xA9" "diteur les reprend).", false);
    return true;
}

bool MacroEditorView::moveQuestion(const std::string& key, const std::string& group, std::size_t index) {
    if (!spec_.field(key)) return false;
    selected_ = key;
    return rewrite(mac::moveField(source(), key, group, index),
                   key + " d\xC3\xA9plac\xC3\xA9" "e" + (group.empty() ? std::string(" (sans groupe)") : " dans " + group));
}

bool MacroEditorView::setQuestionLabel(const std::string& key, const std::string& label) {
    if (!spec_.field(key)) return false;
    return rewrite(mac::setKeyLine(source(), "libelle", key, label), "Libell\xC3\xA9 de " + key);
}

bool MacroEditorView::setQuestionKind(const std::string& key, const std::string& kind) {
    if (!spec_.field(key) || !mac::kindFromKey(kind)) return false;
    const std::string now = champValue(key);
    const std::string value = now.empty() ? kind : kind + restAfterFirstWord(now);
    return rewrite(mac::setKeyLine(source(), "champ", key, value), "Genre de " + key + " : " + kind);
}

bool MacroEditorView::setQuestionOptional(const std::string& key, bool optional) {
    const auto* f = spec_.field(key);
    if (!f) return false;
    std::string now = champValue(key);
    if (now.empty()) now = std::string(mac::kindKey(f->kind));
    if (hasWord(now, "facultatif") == optional) return true;
    std::string value;
    if (optional) {
        value = now + " facultatif";
    } else {
        // Le mot retire, les autres gardes dans leur ordre.
        std::string out;
        std::size_t i = 0;
        while (i < now.size()) {
            std::size_t j = i;
            while (j < now.size() && !std::isspace(static_cast<unsigned char>(now[j]))) ++j;
            const std::string w = now.substr(i, j - i);
            if (lower(w) != "facultatif" && lower(w) != "facultative") out += (out.empty() ? "" : " ") + w;
            while (j < now.size() && std::isspace(static_cast<unsigned char>(now[j]))) ++j;
            i = j;
        }
        value = out;
    }
    return rewrite(mac::setKeyLine(source(), "champ", key, value), key + (optional ? " facultative" : " obligatoire"));
}

bool MacroEditorView::setQuestionGroup(const std::string& key, const std::string& group) {
    return moveQuestion(key, group, static_cast<std::size_t>(-1));
}

bool MacroEditorView::setQuestionAdvanced(const std::string& key, bool advanced) {
    if (!spec_.field(key)) return false;
    return rewrite(mac::setAdvanced(source(), key, advanced), key + (advanced ? " dans les r\xC3\xA9glages avanc\xC3\xA9s" : " hors des r\xC3\xA9glages avanc\xC3\xA9s"));
}

bool MacroEditorView::setQuestionHelp(const std::string& key, const std::string& help) {
    if (!spec_.field(key)) return false;
    return rewrite(mac::setHelp(source(), key, help), "Aide de " + key);
}

bool MacroEditorView::addQuestion(const std::string& key, const std::string& kind, const std::string& label, const std::string& preset) {
    if (!mac::isIdentifier(key)) {
        if (hosts_.status) hosts_.status("\xC2\xAB " + key + " \xC2\xBB n'est pas une cl\xC3\xA9 : une lettre, puis lettres, chiffres, _.", true);
        return false;
    }
    if (spec_.field(key) && spec_.field(key)->key == key) {
        if (hosts_.status) hosts_.status("La macro a d\xC3\xA9j\xC3\xA0 une question " + key + ".", true);
        return false;
    }
    const std::string k = mac::kindFromKey(kind) ? kind : std::string("texte");
    selected_ = key;
    return rewrite(mac::addField(source(), key, k, label.empty() ? key : label, preset), "Question " + key + " ajout\xC3\xA9" "e");
}

void MacroEditorView::selectQuestion(const std::string& key) {
    if (!spec_.field(key)) return;
    selected_ = key;
    syncing_ = true;
    for (std::size_t i = 0; i < cardRows_.size(); ++i)
        if (!cardRows_[i].header && cardRows_[i].key == key) cards_->selectModelRows({static_cast<RowIndex>(i)}, true);
    syncing_ = false;
    rebuildProperties();
    markLines();
    const auto lines = mac::linesOf(source(), key);
    if (!lines.empty()) code_->scrollToLine(lines.front() > 4 ? lines.front() - 4 : 0);
}

void MacroEditorView::setSideTab(int tab) {
    sideTab_ = tab == 1 ? 1 : 0;
    syncing_ = true;
    tabQuestions_->setChecked(sideTab_ == 0);
    tabPreview_->setChecked(sideTab_ == 1);
    syncing_ = false;
    const auto show = [](ui::Widget* w, bool on) { w->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed); };
    show(cards_, sideTab_ == 0);
    show(addButton_, sideTab_ == 0);
    show(props_, sideTab_ == 0);
    show(form_, sideTab_ == 1);
    if (sideTab_ == 1) rebuildPreview();
    invalidateLayout();
    invalidate();
}

// ---- essayer, verifier ---------------------------------------------------------
void MacroEditorView::setRemarks(std::vector<Remark> remarks, std::string title) {
    remarks_ = std::move(remarks);
    trialTitle_ = std::move(title);
    remarksModel_->modelReset->emit();
    markLines();
    invalidate();
}

void MacroEditorView::check() {
    reparse();
    std::vector<Remark> out;
    for (const auto& p : spec_.problems) out.push_back({Remark::Kind::Warning, 0, p});
    const std::string text = source();
    // Le code d'abord : il se lit, ou pas (et ou).
    if (auto program = sim::parse(text, name_); !program)
        out.push_back({Remark::Kind::Error, 0, "le code ne se lit pas : " + program.error().message()});
    for (const auto& f : spec_.fields) {
        const std::size_t askLine = askLineOf(text, f.key);
        if (!f.declared) out.push_back({Remark::Kind::Info, askLine, f.key + " : pas de ligne #! champ - le formulaire en fait un texte libre"});
        if (f.declared && askLine == 0 && !f.isPattern())
            out.push_back({Remark::Kind::Warning, 0, f.key + " : une ligne #! champ, mais aucun Ask('" + f.key + "', ...) dans le code"});
    }
    for (const auto& g : spec_.groups)
        for (const auto& k : g.keys)
            if (!spec_.field(k)) out.push_back({Remark::Kind::Warning, 0, "#! groupe " + g.name + " cite " + k + ", qui n'est pas une question"});
    if (spec_.version.empty()) out.push_back({Remark::Kind::Info, 0, "pas de ligne #! version : Enregistrer \xC3\xA9" "crira la 1.0"});
    if (out.empty()) out.push_back({Remark::Kind::Ok, 0, std::to_string(spec_.fields.size()) + " questions, " + std::to_string(spec_.groups.size())
                                                         + " groupes : l'en-t\xC3\xAA" "te est bon"});
    setRemarks(std::move(out), "V\xC3\x89RIFIER \xC2\xB7 l'en-t\xC3\xAA" "te et les questions, rien n'est lanc\xC3\xA9");
}

void MacroEditorView::tryRun() {
    reparse();
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    if (!p) {
        setRemarks({{Remark::Kind::Warning, 0, "Aucun projet ouvert : l'essai a besoin d'un projet sur lequel tourner (rien n'y est laiss\xC3\xA9)."}},
                   "ESSAI (F5)");
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    mac::MacroSession session(p, hosts_.libsRoot, name_, source());
    const auto& out = session.refresh(true);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::vector<Remark> r;
    const auto& rep = out.report;
    const auto& t = rep.tally;
    if (out.failure.empty() && rep.failure.empty() && rep.ok) {
        std::string done;
        const auto part = [&](std::size_t n, const char* one, const char* many) {
            if (!n) return;
            done += (done.empty() ? "" : " \xC2\xB7 ") + std::to_string(n) + " " + (n == 1 ? one : many);
        };
        part(t.sections, "section \xC3\xA9" "crite", "sections \xC3\xA9" "crites");
        part(t.lines, "ligne ajout\xC3\xA9" "e", "lignes ajout\xC3\xA9" "es");
        part(t.variables, "variable", "variables");
        part(t.types, "type", "types");
        part(t.imports, "\xC3\xA9l\xC3\xA9ment de la biblioth\xC3\xA8que", "\xC3\xA9l\xC3\xA9ments de la biblioth\xC3\xA8que");
        part(t.rowsRead, "ligne lue", "lignes lues");
        r.push_back({Remark::Kind::Ok, 0, done.empty() ? std::string("Elle va au bout, sans rien changer au projet.") : "Elle irait au bout : " + done});
    } else {
        r.push_back({Remark::Kind::Error, 0, "Elle s'arr\xC3\xAAte : " + (!out.failure.empty() ? out.failure : !rep.failure.empty() ? rep.failure : std::string("une erreur"))});
    }
    for (const auto& d : rep.diagnostics) {
        // Fail() arrete la macro en passant par l'interpreteur, qui le note comme
        // une fonction inconnue : ce n'est pas une remarque, c'est l'arret deja dit.
        if (d.message.find("'Fail'") != std::string::npos) continue;
        const auto kind = d.severity == sim::Diagnostic::Severity::Error ? Remark::Kind::Error
                        : d.severity == sim::Diagnostic::Severity::Warning ? Remark::Kind::Warning : Remark::Kind::Info;
        r.push_back({kind, d.line, d.message});
    }
    for (const auto& w : rep.warnings) r.push_back({Remark::Kind::Warning, 0, w});
    for (std::size_t i = 0; i < rep.log.size() && i < 40; ++i) r.push_back({Remark::Kind::Info, 0, rep.log[i]});
    if (source().find("Confirm(") != std::string::npos)
        r.push_back({Remark::Kind::Info, 0, "Confirm() pose une question pendant l'essai : l'essai a r\xC3\xA9pondu \xC2\xAB oui \xC2\xBB (c'est Appliquer qui confirme)"});
    char secs[32];
    std::snprintf(secs, sizeof secs, "%.2f s", seconds);
    setRemarks(std::move(r), "ESSAI (F5) \xC2\xB7 aper\xC3\xA7u, tout est d\xC3\xA9" "fait \xC2\xB7 " + std::to_string(out.rounds) + " tour(s), " + secs);
    if (hosts_.status) hosts_.status("Essai de " + name_ + " : " + remarks_.front().text, remarks_.front().kind == Remark::Kind::Error);
}

// ---- enregistrer ----------------------------------------------------------------
std::string MacroEditorView::nextVersion() const { return mac::nextVersion(spec_.version); }

void MacroEditorView::askSave() {
    if (!hosts_.askSave) {
        (void)save(nextVersion(), {});
        return;
    }
    hosts_.askSave(spec_.version, nextVersion(), [this](const std::string& version, const std::string& changes) { (void)save(version, changes); });
}

bool MacroEditorView::save(const std::string& version, const std::string& changes) {
    if (!hosts_.save) return false;
    const std::string before = source();
    const std::string text = version.empty() ? before : mac::bumpVersion(before, version, changes);
    if (!hosts_.save(name_, text)) return false;
    if (text != before) setSource(text, true);
    saved_ = text;
    reparse();
    if (hosts_.status) hosts_.status(name_ + " " + (version.empty() ? std::string{} : version + " ") + "enregistr\xC3\xA9" "e dans libs/Macros.", false);
    return true;
}

void MacroEditorView::runAction(int action) {
    switch (action) {
        case ASave:
        case AVersion: askSave(); return;
        case AUndo: (void)undoStep(); return;
        case ARedo: (void)redoStep(); return;
        case ATry: tryRun(); return;
        case ACheck: check(); return;
        case AUse: if (hosts_.use) hosts_.use(name_, source()); return;
        case AShow: if (hosts_.showInMacros) hosts_.showInMacros(name_); return;
        case AHelp: if (hosts_.help) hosts_.help(); return;
        default: return;
    }
}

// ---- la completion ---------------------------------------------------------------
void MacroEditorView::complete(std::string_view prefix, std::vector<std::pair<std::string, std::string>>& out) const {
    const auto keep = [&](const std::string& word) {
        if (prefix.empty()) return true;
        if (word.size() < prefix.size()) return false;
        return lower(word.substr(0, prefix.size())) == lower(prefix);
    };
    for (const auto& n : project::MacroRunner::natives()) {
        const auto open = n.signature.find('(');
        const auto word = n.signature.substr(0, open == std::string::npos ? n.signature.size() : open);
        if (keep(word)) out.emplace_back(word, n.category + " - " + n.help);
    }
    const auto& xl = xls::MacroXls::instance();
    if (!xl.loadedWorkbook().empty()) {
        std::string book = xl.loadedWorkbook();
        const auto slash = book.find_last_of("/\\");
        if (slash != std::string::npos) book = book.substr(slash + 1);
        for (const auto& s : xl.sheetNames())
            if (keep(s)) out.emplace_back(s, "onglet de " + book);
    }
    const auto p = hosts_.project ? hosts_.project() : nullptr;
    if (!p) return;
    std::size_t n = 0;
    const auto add = [&](domain::SymbolId id, const char* what) {
        const std::string w(p->strings.text(id));
        if (n < 400 && keep(w)) {
            out.emplace_back(w, what);
            ++n;
        }
    };
    for (const auto& t : p->tasks) add(t.name, "t\xC3\xA2" "che du projet");
    for (const auto& s : p->sections) add(s.name, s.isSubroutine ? "sous-routine du projet" : "section du projet");
    for (const auto& pou : p->pous)
        if (pou.kind == domain::PouKind::ProgramUnit || pou.kind == domain::PouKind::FunctionBlockType)
            add(pou.name, pou.kind == domain::PouKind::ProgramUnit ? "unit\xC3\xA9 du projet" : "bloc DFB du projet");
    for (const auto& dt : p->derivedTypes) add(dt.name, "type d\xC3\xA9riv\xC3\xA9 du projet");
    for (const auto& v : p->variables)
        if (v.scope == domain::VariableScope::Global) add(v.name, "variable du projet");
}

// ---- dessiner, placer ----------------------------------------------------------
void MacroEditorView::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 38.f});
    const float sideW = std::min(kSideW, std::max(320.f, b.w * 0.36f));
    const float mainW = std::max(200.f, b.w - sideW - 1.f);
    const float top = b.y + 38.f;
    codeTitle_ = {b.x, top, mainW, 24.f};
    const float trialH = std::min(kTrialH, std::max(90.f, (b.h - 62.f) * 0.3f));
    code_->setBounds({b.x, top + 24.f, mainW, std::max(40.f, b.h - 38.f - 24.f - trialH - 24.f)});
    trialTitleRect_ = {b.x, code_->bounds().bottom(), mainW, 24.f};
    remarkTable_->setBounds({b.x, trialTitleRect_.bottom(), mainW, std::max(0.f, b.bottom() - trialTitleRect_.bottom())});
    sideRect_ = {b.x + mainW + 1.f, top, sideW, b.h - 38.f};
    const float tabY = top + 6.f;
    const float half = (sideW - 24.f) * 0.5f;
    tabQuestions_->setBounds({sideRect_.x + 8.f, tabY, half, 28.f});
    tabPreview_->setBounds({sideRect_.x + 16.f + half, tabY, half, 28.f});
    const float bodyY = tabY + 36.f;
    const float bodyH = std::max(0.f, b.bottom() - bodyY);
    const float cardsH = std::max(120.f, bodyH * 0.48f);
    cards_->setBounds({sideRect_.x, bodyY, sideW, cardsH});
    addButton_->setBounds({sideRect_.x + 8.f, bodyY + cardsH + 4.f, sideW - 16.f, 28.f});
    props_->setBounds({sideRect_.x, bodyY + cardsH + 36.f, sideW, std::max(0.f, b.bottom() - (bodyY + cardsH + 36.f))});
    form_->setBounds({sideRect_.x, bodyY, sideW, bodyH});
}

void MacroEditorView::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(bounds(), c.panelBg);
    r.fillRect({sideRect_.x - 1.f, sideRect_.y, 1.f, sideRect_.h}, c.border);
    // Le titre du code : le fichier, a droite la ligne et la colonne.
    r.fillRect(codeTitle_, c.headerBg);
    const std::string file = name_ + ".mac \xC2\xB7 libs/Macros" + (dirty() ? "  \xC2\xB7 modifi\xC3\xA9" "e" : std::string{});
    r.drawText({codeTitle_.x + 10.f, codeTitle_.y + 4.f}, file, kSmall, dirty() ? c.warning : c.textMuted);
    {
        const std::string text = source();
        const std::size_t caret = std::min(code_->caretOffset(), text.size());
        std::size_t line = 1, col = 1;
        for (std::size_t i = 0; i < caret; ++i) {
            if (text[i] == '\n') { ++line; col = 1; }
            else if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u) ++col;
        }
        const std::string where = "ligne " + std::to_string(line) + ", colonne " + std::to_string(col)
                                + (spec_.version.empty() ? std::string{} : "  \xC2\xB7  version " + spec_.version);
        const float w = r.measure(where, kSmall).width;
        r.drawText({codeTitle_.right() - w - 10.f, codeTitle_.y + 4.f}, where, kSmall, c.textMuted);
    }
    // Le titre de l'essai.
    r.fillRect(trialTitleRect_, c.headerBg);
    r.fillRect({trialTitleRect_.x, trialTitleRect_.y, trialTitleRect_.w, 1.f}, c.border);
    r.drawText({trialTitleRect_.x + 10.f, trialTitleRect_.y + 4.f}, trialTitle_, kSmall, c.textMuted);
}

ui::EventResult MacroEditorView::onEvent(const ui::InputEvent& ev) {
    const auto* k = std::get_if<ui::KeyDown>(&ev);
    if (!k || !visible()) return ui::EventResult::Ignored;
    if (k->key == ui::Key::F5 && k->mods.none()) { tryRun(); return ui::EventResult::Consumed; }
    if (!k->mods.ctrl || k->mods.alt) return ui::EventResult::Ignored;
    // Ctrl+Z, Ctrl+Y, Ctrl+S : ceux de l'editeur quand le code a la main (la
    // pile du projet, elle, ne connait pas le texte d'une macro).
    if (!code_->focused()) return ui::EventResult::Ignored;
    if (k->key == ui::Key::Z && !k->mods.shift) { (void)undoStep(); return ui::EventResult::Consumed; }
    if (k->key == ui::Key::Y || (k->key == ui::Key::Z && k->mods.shift)) { (void)redoStep(); return ui::EventResult::Consumed; }
    if (k->key == ui::Key::S && !k->repeat) { askSave(); return ui::EventResult::Consumed; }
    return ui::EventResult::Ignored;
}

} // namespace app
