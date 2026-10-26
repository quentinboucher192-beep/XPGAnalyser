// =============================================================================
//  app/hmi/HmiDebugPane.cpp - 1.13.0 (lot J2) : Simulation > Debogage (l'IHM)
// =============================================================================
#include "HmiDebugPane.hpp"

#include "../../ui/Commands.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace app {

using hmi::Id;
using hmi::kNoId;

namespace {

constexpr float kBand = 36.f;        // la barre des boutons
constexpr float kState = 26.f;       // l'etat en clair
constexpr float kState2 = 42.f;      // ... sur deux lignes, dans le volet etroit
constexpr float kNarrow = 760.f;     // plus etroit (le panneau de droite de la simulation) : le volet en lignes

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// 1 243 : les milliers separes d'une espace fine insecable (comme l'onglet Simulation de l'automate).
std::string grouped(std::uint64_t n) {
    std::string digits = std::to_string(n), out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += digits[i];
    }
    return out;
}

std::string plural(std::uint64_t n, std::string_view one, std::string_view many) {
    return grouped(n) + " " + std::string(n > 1 ? many : one);
}

// 1.13.0 : un tableau refait (a chaque revision du moteur, parfois a chaque image) garde ce que l'utilisateur y avait :
// la ligne choisie, le defilement.
void refill(ui::TableView& table, const std::shared_ptr<ui::ITableModel>& model, std::vector<ui::RowIndex> chosen) {
    const float y = table.scrollOffset();
    table.setModel(model);
    if (!chosen.empty()) table.selectModelRows(std::move(chosen), false);
    table.setScrollOffset(y);
}

} // namespace

// ------------------------------------------------------------------- modeles ---
class HmiDebugPane::BreakModel final : public ui::ITableModel {
public:
    explicit BreakModel(HmiDebugPane& p) : pane_(p) {}
    struct Row {
        std::uint32_t id{0};
        std::string   where, condition, hits, note;
        bool          enabled{true}, armed{true}, hit{false};
    };
    std::vector<Row> rows;
    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 4; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const h[] = {"O\xC3\xB9", "Condition", "Passages", "Ce qu'en dit le moteur"};
        return c < 4 ? h[c] : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        const auto& x = rows[r];
        return c == 0 ? x.where : c == 1 ? x.condition : c == 2 ? x.hits : x.note;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle cs;
        if (r >= rows.size()) return cs;
        const auto& x = rows[r];
        if (c == 0) {
            cs.check = x.enabled ? 1 : 0;
            cs.icon = ui::Icon::Breakpoint;
            cs.iconTone = x.enabled && x.armed ? ui::Tone::Error : ui::Tone::Muted;
            cs.bold = x.hit;
        }
        if (c == 1) cs.monospace = true;
        if (c == 3 && !x.armed) cs.fgTone = ui::Tone::Warning;
        if (c == 3 && x.armed) cs.fgTone = ui::Tone::Muted;
        return cs;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return r < rows.size() && c == 1; }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (r >= rows.size() || c != 1) return false;
        auto& session = HmiDebugSession::instance();
        const auto* b = session.find(rows[r].id);
        const HmiDebugTarget code = b ? HmiDebugTarget{b->script, b->function} : HmiDebugTarget{};
        const bool ok = session.setCondition(rows[r].id, std::string(text));
        if (ok) pane_.say(trimmed(text).empty() ? std::string("Sans condition : il s'arr\xC3\xAAtera \xC3\xA0 chaque passage.")
                                                 : "Condition : " + trimmed(text) + " - " + conditionPreview(code, std::string(text)));
        return ok;
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= rows.size()) return {};
        // 1.13.0 : en trois colonnes, "Ou" se coupe - l'infobulle dit le point en entier (son code, sa ligne, sa condition).
        const auto& x = rows[r];
        const std::string head = x.where + (x.condition.empty() ? std::string{} : " si " + x.condition) + " \xC2\xB7 " + x.hits;
        return head + "\n" + (x.note.empty() ? std::string("Double clic : le code, \xC3\xA0 sa ligne. La case : actif ou non.") : x.note);
    }
private:
    HmiDebugPane& pane_;
};

class HmiDebugPane::StackModel final : public ui::ITableModel {
public:
    struct Row { std::string label, line; bool code{true}; bool chosen{false}; };
    std::vector<Row> rows;
    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "Niveau (la ligne d'arr\xC3\xAAt en haut)" : "Ligne"; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        return c == 0 ? rows[r].label : rows[r].line;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle cs;
        if (r >= rows.size()) return cs;
        if (!rows[r].code) cs.fgTone = ui::Tone::Muted;
        if (c == 0 && r == 0) cs.icon = ui::Icon::Breakpoint, cs.iconTone = ui::Tone::Warning;
        cs.bold = rows[r].chosen;
        return cs;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
};

class HmiDebugPane::LocalsModel final : public ui::ITableModel {
public:
    const std::vector<LocalRow>* rows{nullptr};
    [[nodiscard]] std::size_t rowCount() const override { return rows ? rows->size() : 0; }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "Nom" : c == 1 ? "Type" : "Valeur"; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (!rows || r >= rows->size()) return {};
        const auto& x = (*rows)[r];
        return c == 0 ? x.name : c == 1 ? x.type : x.value;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle cs;
        if (!rows || r >= rows->size()) return cs;
        const auto& x = (*rows)[r];
        if (c == 0) {
            cs.indent = 14.f * static_cast<float>(x.depth);
            cs.expander = x.expandable ? (x.open ? 1 : 0) : -1;
            if (x.parameter) cs.fgTone = ui::Tone::Input;
        }
        if (c == 1) cs.fgTone = ui::Tone::Muted;
        if (c == 2) cs.monospace = true;
        return cs;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
};

class HmiDebugPane::WatchModel final : public ui::ITableModel {
public:
    explicit WatchModel(HmiDebugPane& p) : pane_(p) {}
    [[nodiscard]] std::size_t rowCount() const override { return pane_.watches_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "Espion" : "Valeur"; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= pane_.watches_.size()) return {};
        return c == 0 ? pane_.watches_[r] : r < pane_.watchValues_.size() ? pane_.watchValues_[r] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle cs;
        cs.monospace = true;
        if (c == 1 && r < pane_.watchValues_.size() && pane_.watchValues_[r].rfind("(", 0) == 0) cs.fgTone = ui::Tone::Muted;
        return cs;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return r < pane_.watches_.size() && c == 0; }
    // 1.13.0 : la colonne Valeur se coupe (trois colonnes) - l'infobulle dit l'espion et sa valeur (ou pourquoi) en entier.
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override {
        if (r >= pane_.watches_.size()) return {};
        const std::string v = r < pane_.watchValues_.size() ? pane_.watchValues_[r] : std::string{};
        return v.empty() ? std::string{} : pane_.watches_[r] + " = " + v;
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (r >= pane_.watches_.size() || c != 0) return false;
        const std::string t = trimmed(text);
        if (t.empty()) pane_.watches_.erase(pane_.watches_.begin() + static_cast<std::ptrdiff_t>(r));
        else pane_.watches_[r] = t;
        pane_.readWatches();
        return true;
    }
private:
    HmiDebugPane& pane_;
};

// --------------------------------------------------------------------- volet ---
HmiDebugPane::HmiDebugPane(std::string id, hmi::DocumentPtr doc, std::function<hmi::Runtime*()> runtime)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), runtimeOf_(std::move(runtime)) {
    const std::string base = this->id();
    struct B { const char* label; const char* tip; int action; };
    static const B kButtons[] = {
        {"Continuer", "F5 : le script reprend jusqu'au prochain point d'arr\xC3\xAAt", AContinue},
        {"Suivant", "F10 : la ligne suivante de ce code (un appel s'ex\xC3\xA9" "cute d'un trait)", AStepOver},
        {"Entrer", "F11 : dans la fonction appel\xC3\xA9" "e sur cette ligne (fonction IHM, interne, op\xC3\xA9rateur)", AStepInto},
        {"Sortir", "Maj+F11 : jusqu'au retour dans le code qui l'a appel\xC3\xA9" "e", AStepOut},
        {"Pause", "S'arr\xC3\xAAter \xC3\xA0 la prochaine ligne d'un script qui tourne (demand\xC3\xA9" "e : Annuler la pause)", APause},
    };
    for (const auto& b : kButtons) {
        auto btn = std::make_unique<ui::Button>(b.label, base + ".b" + std::to_string(b.action));
        btn->setTooltip(b.tip);
        btn->setStyle(b.action == AContinue ? ui::Button::Style::Primary : ui::Button::Style::Default);
        auto* raw = &static_cast<ui::Button&>(addChild(std::move(btn)));
        const int action = b.action;
        links_ += raw->clicked->connect([this, action] { runAction(action); });
        buttons_.push_back(raw);
    }
    {
        auto btn = std::make_unique<ui::Button>("Tout retirer", base + ".toutRetirer");
        btn->setStyle(ui::Button::Style::Flat);
        btn->setTooltip("Retirer tous les points d'arr\xC3\xAAt de l'IHM");
        clearBtn_ = &static_cast<ui::Button&>(addChild(std::move(btn)));
        links_ += clearBtn_->clicked->connect([this] { runAction(AClearBreakpoints); });
    }

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    // A gauche : les points, les espions.
    {
        auto left = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".gauche");
        auto bpPanel = std::make_unique<HmiTitledPanel>(base + ".pointsPanel", "POINTS D'ARR\xC3\x8ATS");
        auto field = std::make_unique<ui::InputText>(base + ".pointTape");
        field->setPlaceholder("+ Point d'arr\xC3\xAAt : Statistiques_Pression 14 si P < 5 (Entr\xC3\xA9" "e)");
        bpField_ = &static_cast<ui::InputText&>(bpPanel->addRow(std::move(field), 32.f));
        links_ += bpField_->editingDone->connect([this](const std::string& t) {
            if (trimmed(t).empty()) return;
            std::string why;
            if (addBreakpointText(t, &why)) bpField_->setText({});
            else say(why, true);
        });
        auto row = std::make_unique<ui::Button>("Retirer le point choisi", base + ".retirer");
        row->setStyle(ui::Button::Style::Flat);
        removeBtn_ = &static_cast<ui::Button&>(bpPanel->addRow(std::move(row), 28.f));
        links_ += removeBtn_->clicked->connect([this] { runAction(ARemoveBreakpoint); });
        auto table = std::make_unique<ui::TableView>(base + ".points");
        table->setColumns({{"O\xC3\xB9", 190.f}, {"Condition", 150.f}, {"Passages", 70.f}, {"Ce qu'en dit le moteur", 260.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        bpTable_ = &static_cast<ui::TableView&>(bpPanel->setBody(std::move(table)));
        left->addPane(std::move(bpPanel), 0.55f, 110.f);
        auto wPanel = std::make_unique<HmiTitledPanel>(base + ".espionsPanel", "ESPIONS");
        auto wField = std::make_unique<ui::InputText>(base + ".espionTape");
        wField->setPlaceholder("+ Espion : un nom, une expression (Entr\xC3\xA9" "e)");
        watchField_ = &static_cast<ui::InputText&>(wPanel->addRow(std::move(wField), 32.f));
        links_ += watchField_->editingDone->connect([this](const std::string& t) {
            if (addWatch(t)) watchField_->setText({});
        });
        auto wTable = std::make_unique<ui::TableView>(base + ".espions");
        wTable->setColumns({{"Espion", 170.f}, {"Valeur", 170.f}});
        wTable->setSelectionMode(ui::SelectionMode::Single);
        watchTable_ = &static_cast<ui::TableView&>(wPanel->setBody(std::move(wTable)));
        left->addPane(std::move(wPanel), 0.45f, 90.f);
        leftSplit_ = left.get();
        split->addPane(std::move(left), 0.34f, 120.f);
    }
    // Au centre : la pile, le code.
    {
        auto center = std::make_unique<ui::Splitter>(ui::Orientation::Vertical, base + ".centre");
        auto sPanel = std::make_unique<HmiTitledPanel>(base + ".pilePanel", "PILE");
        auto st = std::make_unique<ui::TableView>(base + ".pile");
        st->setColumns({{"Niveau (la ligne d'arr\xC3\xAAt en haut)", 280.f}, {"Ligne", 60.f}});
        st->setSelectionMode(ui::SelectionMode::Single);
        stackTable_ = &static_cast<ui::TableView&>(sPanel->setBody(std::move(st)));
        center->addPane(std::move(sPanel), 0.32f, 80.f);
        auto cPanel = std::make_unique<HmiTitledPanel>(base + ".codePanel", "CODE");
        auto ed = std::make_unique<ui::MultiLineText>(base + ".code");
        ed->setLanguage(ui::Language::StructuredText);
        ed->setShowLineNumbers(true);
        ed->setReadOnly(true);
        code_ = &static_cast<ui::MultiLineText&>(cPanel->setBody(std::move(ed)));
        codePanel_ = cPanel.get();
        center->addPane(std::move(cPanel), 0.68f, 120.f);
        centerSplit_ = center.get();
        split->addPane(std::move(center), 0.40f, 200.f);
    }
    // A droite : les locales.
    {
        auto lPanel = std::make_unique<HmiTitledPanel>(base + ".localesPanel", "LOCALES");
        auto lt = std::make_unique<ui::TableView>(base + ".locales");
        lt->setColumns({{"Nom", 130.f}, {"Type", 90.f}, {"Valeur", 150.f}});
        lt->setSelectionMode(ui::SelectionMode::Single);
        localsTable_ = &static_cast<ui::TableView&>(lPanel->setBody(std::move(lt)));
        split->addPane(std::move(lPanel), 0.26f, 100.f);
    }
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));

    bpModel_ = std::make_shared<BreakModel>(*this);
    stackModel_ = std::make_shared<StackModel>();
    localsModel_ = std::make_shared<LocalsModel>();
    localsModel_->rows = &locals_;
    watchModel_ = std::make_shared<WatchModel>(*this);
    bpTable_->setModel(bpModel_);
    stackTable_->setModel(stackModel_);
    localsTable_->setModel(localsModel_);
    watchTable_->setModel(watchModel_);

    links_ += bpTable_->checkClicked->connect([this](ui::RowIndex r) {
        if (r >= bpIds_.size()) return;
        auto& s = HmiDebugSession::instance();
        if (const auto* b = s.find(bpIds_[r])) (void)s.setEnabled(b->id, !b->enabled);
    });
    links_ += bpTable_->activated->connect([this](ui::RowIndex r) {
        if (r >= bpIds_.size()) return;
        if (const auto* b = HmiDebugSession::instance().find(bpIds_[r]); b && goTo)
            goTo(b->script, b->function, static_cast<int>(b->effectiveLine ? b->effectiveLine : b->line));
    });
    links_ += stackTable_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (rows.empty()) return;
        (void)chooseLevel(rows.front());
    });
    links_ += stackTable_->activated->connect([this](ui::RowIndex r) {
        const auto* f = (static_cast<void>(chooseLevel(r)), shownFrame());
        // 1.13.0 : un code du projet seulement (une redefinition d'instance se montre ici, dans CODE, sans editeur a ouvrir).
        if (f && goTo && (f->script != kNoId || f->function != kNoId) && doc_
            && !hmi::Runtime::debugCodeName(doc_->project, f->script, f->function).empty())
            goTo(f->script, f->function, static_cast<int>(f->line));
    });
    links_ += localsTable_->expanderClicked->connect([this](ui::RowIndex r) {
        if (r >= locals_.size()) return;
        const auto& key = locals_[r].key;
        if (expanded_.count(key)) expanded_.erase(key);
        else expanded_.insert(key);
        rebuildLocals();
    });
    // La marge du code montre : y poser un point (le code du niveau choisi).
    margin_.attach(*this, *code_, [this] { return codeShown_; }, [this](const std::string& t, bool w) { say(t, w); });
    links_ += HmiDebugSession::instance().changed->connect([this] { seen_.clear(); invalidate(); });
    refresh();
}

HmiDebugPane::~HmiDebugPane() = default;

hmi::Runtime* HmiDebugPane::runtime() const { return runtimeOf_ ? runtimeOf_() : nullptr; }

std::string HmiDebugPane::codeName(Id script, Id function) const {
    std::string name = doc_ ? hmi::Runtime::debugCodeName(doc_->project, script, function) : std::string{};
    // 1.13.0 : une redefinition d'instance (le moteur la connait par l'id de sa copie).
    if (name.empty() && script == kNoId)
        if (const auto* rt = runtime()) name = rt->boundCodeName(function);
    return name;
}

std::string HmiDebugPane::codeText(Id script, Id function) const {
    // 1.13.0 : tout code que le moteur peut arreter - un script, une fonction (du projet, d'une bibliotheque, d'une vue,
    // d'une popup, d'un symbole), une methode ou un operateur d'un type, une redefinition d'instance.
    std::string text = doc_ ? hmi::Runtime::debugCodeBody(doc_->project, script, function) : std::string{};
    if (text.empty() && script == kNoId)
        if (const auto* rt = runtime()) text = rt->boundCodeBody(function);
    return text;
}

void HmiDebugPane::say(const std::string& text, bool warning) {
    message_ = text;
    messageWarn_ = warning;
    const auto* rt = runtime();
    messageRev_ = rt ? rt->debugRevision() : 0;
    invalidate();
}

gfx::Rect HmiDebugPane::buttonRect(int action) const {
    if (action >= AContinue && action <= APause) return buttons_[static_cast<std::size_t>(action - AContinue)]->bounds();
    if (action == AClearBreakpoints) return clearBtn_->bounds();
    return {};
}

std::string HmiDebugPane::buttonText(int action) const {
    if (action >= AContinue && action <= APause) return buttons_[static_cast<std::size_t>(action - AContinue)]->text();
    if (action == AClearBreakpoints) return clearBtn_->text();
    return {};
}

// ------------------------------------------------------------------ gestes ---
void HmiDebugPane::runAction(int action) {
    auto* rt = runtime();
    auto& s = HmiDebugSession::instance();
    // Continuer, un pas : la pause suivante peut venir avant le retour (le script reprend tout de suite) - ce qui est
    // dit ici date d'avant elle, son etat passera devant.
    const std::uint64_t before = rt ? rt->debugRevision() : 0;
    // La reprise : la pause suivante se montre a sa ligne d'arret (le niveau 0), pas au niveau choisi avant.
    if (action == AContinue || action == AStepOver || action == AStepInto || action == AStepOut) s.setLevel(0);
    switch (action) {
        case AContinue:
            if (rt && rt->debugContinue()) say("Continu\xC3\xA9 : le script reprend jusqu'au prochain point d'arr\xC3\xAAt.");
            else if (rt && rt->pauseAsked()) say("Pause demand\xC3\xA9" "e : rien ne s'est encore arr\xC3\xAAt\xC3\xA9.", true);
            else say("Pas en pause : rien \xC3\xA0 continuer.", true);
            break;
        case AStepOver:
        case AStepInto:
        case AStepOut: {
            const auto step = action == AStepOver ? hmi::DebugStep::Over : action == AStepInto ? hmi::DebugStep::Into : hmi::DebugStep::Out;
            if (!rt || !rt->debugStep(step)) say("Pas en pause : pose un point d'arr\xC3\xAAt, ou Pause.", true);
            else if (!rt->paused()) say("Le pas continue : le script attend (WAIT) ou appelle - il s'arr\xC3\xAAtera \xC3\xA0 sa prochaine ligne.");
            break;
        }
        case APause:
            if (!rt || !rt->running()) say("L'IHM ne tourne pas : d\xC3\xA9marre-la d'abord.", true);
            else if (rt->paused()) say("D\xC3\xA9j\xC3\xA0 en pause.", true);
            else if (rt->pauseAsked()) {                 // 1.13.0 : le meme bouton, Annuler la pause
                (void)rt->debugCancelPause();
                say("Pause annul\xC3\xA9" "e : l'IHM continue sans s'arr\xC3\xAAter.");
            } else {
                rt->debugPause();
                say("Pause demand\xC3\xA9" "e : le prochain script qui tourne s'arr\xC3\xAAtera \xC3\xA0 sa ligne.");
            }
            break;
        case AClearBreakpoints:
            if (s.breakpoints().empty()) say("Aucun point d'arr\xC3\xAAt.", true);
            else {
                const auto n = s.breakpoints().size();
                s.clear();
                say(plural(n, "point d'arr\xC3\xAAt retir\xC3\xA9", "points d'arr\xC3\xAAt retir\xC3\xA9s") + ".");
            }
            break;
        case ADisableAll: s.disableAll(); break;
        case ARemoveBreakpoint: {
            const auto sel = bpTable_->selectedModelRows();
            if (sel.empty() || sel.front() >= bpIds_.size()) {
                say("Choisis d'abord un point dans la liste.", true);
                break;
            }
            (void)s.remove(bpIds_[sel.front()]);
            say("Point d'arr\xC3\xAAt retir\xC3\xA9.");
            break;
        }
        default: break;
    }
    if (action == AContinue || action == AStepOver || action == AStepInto || action == AStepOut) messageRev_ = before;
    seen_.clear();
    refresh(nextWatch_);
}

bool HmiDebugPane::debugKey(ui::Key key, bool shift) {
    auto* rt = runtime();
    if (!rt || !rt->paused()) return false;
    if (key == ui::Key::F5 && !shift) runAction(AContinue);
    else if (key == ui::Key::F10 && !shift) runAction(AStepOver);
    else if (key == ui::Key::F11) runAction(shift ? AStepOut : AStepInto);
    else return false;
    return true;
}

bool HmiDebugPane::addBreakpointText(const std::string& text, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    std::string t = trimmed(text);
    std::string condition;
    for (const char* sep : {" si ", " SI ", " if ", " IF "})
        if (const auto at = t.find(sep); at != std::string::npos) {
            condition = trimmed(std::string_view(t).substr(at + std::strlen(sep)));
            t = trimmed(std::string_view(t).substr(0, at));
            break;
        }
    // La ligne : le dernier nombre (« Calcul 14 », « Calcul:14 », « Calcul, ligne 14 », « Calcul l.14 »).
    std::size_t end = t.size();
    while (end > 0 && !std::isdigit(static_cast<unsigned char>(t[end - 1]))) --end;
    std::size_t start = end;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(t[start - 1]))) --start;
    if (start == end) return fail("Il manque la ligne : \xC2\xAB Statistiques_Pression 14 \xC2\xBB, \xC2\xAB Moyenne_Pression 5 si A > 3.0 \xC2\xBB.");
    const auto line = static_cast<std::uint32_t>(std::strtoul(t.c_str() + start, nullptr, 10));
    std::string name = trimmed(std::string_view(t).substr(0, start));
    for (const char* tail : {", ligne", " ligne", "ligne", " l.", ":", ","})
        if (name.size() >= std::strlen(tail) && lowered(name.substr(name.size() - std::strlen(tail))) == tail) {
            name = trimmed(std::string_view(name).substr(0, name.size() - std::strlen(tail)));
            break;
        }
    for (const char* head : {"script ", "fonction "})
        if (lowered(name).rfind(head, 0) == 0) name = trimmed(std::string_view(name).substr(std::strlen(head)));
    if (name.empty() || line == 0) return fail("Il manque le script ou la fonction : \xC2\xAB Statistiques_Pression 14 \xC2\xBB.");
    if (!doc_) return fail("Pas de projet.");
    const auto& p = doc_->project;
    HmiDebugTarget code;
    if (const auto* sc = p.generalScript(name)) code.script = sc->id;
    if (code.empty())
        if (const auto dot = name.find('.'); dot != std::string::npos)
            if (const auto* v = p.viewByName(name.substr(0, dot)))
                for (const auto& sc : v->scripts)
                    if (lowered(sc.event) == lowered(name.substr(dot + 1))) code.script = sc.id;
    if (code.empty())
        if (const auto* f = p.functionByName(name)) code.function = f->id;
    if (code.empty()) {
        for (const auto& v : p.views)
            for (const auto& f : v.functions)
                if (lowered(v.name + "." + f.name) == lowered(name)) code.function = f.id;
    }
    if (code.empty()) return fail("\xC2\xAB " + name + " \xC2\xBB n'est ni un script g\xC3\xA9n\xC3\xA9ral, ni un script de vue (Vue.OnCycle), ni une fonction IHM.");
    std::string w;
    const auto id = HmiDebugSession::instance().add(code, line, condition, &w);
    if (!id) return fail(w);
    say("Point d'arr\xC3\xAAt pos\xC3\xA9 : " + codeName(code.script, code.function) + ", ligne " + std::to_string(line)
        + (condition.empty() ? std::string{} : " si " + condition) + ".");
    return true;
}

bool HmiDebugPane::addWatch(const std::string& expression) {
    const std::string e = trimmed(expression);
    if (e.empty() || std::find(watches_.begin(), watches_.end(), e) != watches_.end()) return false;
    watches_.push_back(e);
    readWatches();
    return true;
}

bool HmiDebugPane::removeWatch(const std::string& expression) {
    const auto it = std::find(watches_.begin(), watches_.end(), trimmed(expression));
    if (it == watches_.end()) return false;
    watches_.erase(it);
    readWatches();
    return true;
}

std::vector<std::string> HmiDebugPane::watchLines() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < watches_.size(); ++i) out.push_back(watches_[i] + " = " + (i < watchValues_.size() ? watchValues_[i] : std::string{}));
    return out;
}

std::vector<std::string> HmiDebugPane::stackLabels() const {
    std::vector<std::string> out;
    for (const auto& r : stackModel_->rows) out.push_back(r.label + (r.line.empty() ? std::string{} : ", ligne " + r.line));
    return out;
}

std::vector<std::string> HmiDebugPane::localLines() const {
    std::vector<std::string> out;
    for (const auto& l : locals_) out.push_back(std::string(static_cast<std::size_t>(l.depth) * 2, ' ') + l.name + (l.type.empty() ? "" : " : " + l.type) + " = " + l.value);
    return out;
}

std::vector<std::string> HmiDebugPane::breakpointLines() const {
    std::vector<std::string> out;
    for (const auto& r : bpModel_->rows)
        out.push_back(r.where + (r.condition.empty() ? std::string{} : " si " + r.condition) + " - " + r.hits + (r.note.empty() ? std::string{} : " - " + r.note));
    return out;
}

bool HmiDebugPane::chooseLevel(std::size_t level) {
    auto* rt = runtime();
    if (!rt || !rt->paused() || !rt->debugHit()) return false;
    if (level >= rt->debugHit()->stack.size()) return false;
    HmiDebugSession::instance().setLevel(level);
    seen_.clear();
    refresh(nextWatch_);
    return true;
}

const hmi::DebugFrame* HmiDebugPane::shownFrame() const {
    auto* rt = runtime();
    if (!rt || !rt->paused() || !rt->debugHit()) return nullptr;
    const auto& stack = rt->debugHit()->stack;
    if (stack.empty()) return nullptr;
    const std::size_t k = std::min(HmiDebugSession::instance().level(), stack.size() - 1);
    return &stack[stack.size() - 1 - k];
}

// --------------------------------------------------------------- l'affichage ---
void HmiDebugPane::refresh(double now) {
    auto* rt = runtime();
    auto& s = HmiDebugSession::instance();
    const bool live = rt && rt->running();
    if (live && !rt->paused() && now >= nextWatch_ && !watches_.empty()) {
        nextWatch_ = now + 0.25;
        readWatches();
    }
    const std::string sig = std::to_string(s.stateRevision()) + ":" + std::to_string(rt ? rt->debugRevision() : 0) + ":" + (live ? "1" : "0")
                            + (rt && rt->paused() ? "p" : "") + (rt && rt->stepping() ? "s" : "") + (rt && rt->pauseAsked() ? "a" : "")
                            + ":" + std::to_string(s.level());
    if (sig == seen_) return;
    seen_ = sig;
    rebuild();
}

void HmiDebugPane::rebuild() {
    auto* rt = runtime();
    auto& s = HmiDebugSession::instance();
    const bool live = rt && rt->running();
    const bool paused = rt && rt->paused() && rt->debugHit();
    // Une nouvelle pause : son etat passe devant ce qui a ete dit avant (« Continue : ... », « Point d'arret pose »).
    if (paused && rt->debugRevision() != messageRev_) message_.clear();
    // L'etat en clair.
    std::size_t armed = 0;
    for (const auto& b : s.breakpoints()) armed += b.enabled && b.note.find("il ne s'arr\xC3\xAA") == std::string::npos;
    if (!live) {
        state_ = "L'IHM est arr\xC3\xAAt\xC3\xA9" "e : d\xC3\xA9marre-la, puis pose un point d'arr\xC3\xAAt dans la marge d'un script (un clic \xC3\xA0 gauche des num\xC3\xA9ros).";
    } else if (paused) {
        const auto& h = *rt->debugHit();
        std::string head;
        switch (h.why) {
            case hmi::DebugHit::Why::Breakpoint: head = "En pause au point " + std::to_string(s.rankOf(h.breakpoint)); break;
            case hmi::DebugHit::Why::Step: head = "En pause apr\xC3\xA8s le pas"; break;
            case hmi::DebugHit::Why::Code: head = "En pause sur IHM_POINT_ARRET"; break;
            case hmi::DebugHit::Why::Pause: head = "En pause"; break;
        }
        state_ = head + " : " + h.where + ", cycle IHM " + grouped(h.cycle);
        std::string values;
        for (const auto& [n, v] : h.values) {
            if (values.size() > 60) break;
            values += (values.empty() ? "" : ", ") + n + " = " + v;
        }
        if (!values.empty()) state_ += " - " + values;
        // Le contexte, s'il dit plus que le cycle (« clic sur Bouton_Purge », un script detache) ; puis l'attente.
        const bool plainCycle = h.context.empty() || h.context == "cycle IHM" || h.context == "IHM";
        state_ += " (" + (plainCycle ? std::string{} : h.context + " ; ") + "le cycle IHM attend la reprise)";
    } else if (rt->stepping()) {
        state_ = "Le pas continue : le script attend (un WAIT) - il s'arr\xC3\xAAtera \xC3\xA0 sa prochaine ligne, l'IHM respire en attendant.";
    } else if (rt->pauseAsked()) {
        state_ = "Pause demand\xC3\xA9" "e : le prochain script qui tourne s'arr\xC3\xAAtera \xC3\xA0 sa ligne (Annuler la pause : la retirer).";
    } else if (armed > 0) {
        state_ = "En marche - " + plural(armed, "point d'arr\xC3\xAAt arm\xC3\xA9", "points d'arr\xC3\xAAt arm\xC3\xA9s") + " : la simulation s'arr\xC3\xAAtera dessus.";
    } else {
        state_ = "En marche - pose un point d'arr\xC3\xAAt : un clic dans la marge d'un script ou d'une fonction (\xC3\xA0 gauche des num\xC3\xA9ros), ou + Point d'arr\xC3\xAAt.";
    }
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const int action = static_cast<int>(i) + AContinue;
        buttons_[i]->setEnabled(action == APause ? live && !(rt && rt->paused()) : paused);
    }
    // Une pause demandee qui n'a encore rien arrete : le bouton la retire.
    const bool asked = live && rt->pauseAsked() && !rt->paused();
    if (auto* pause = buttons_[static_cast<std::size_t>(APause - AContinue)]; pause->text() != (asked ? "Annuler la pause" : "Pause")) {
        pause->setText(asked ? "Annuler la pause" : "Pause");
        pause->setTooltip(asked ? "Retirer la pause demand\xC3\xA9" "e : l'IHM continue sans s'arr\xC3\xAAter"
                                : "S'arr\xC3\xAAter \xC3\xA0 la prochaine ligne d'un script qui tourne (demand\xC3\xA9" "e : Annuler la pause)");
        invalidateLayout();
    }
    clearBtn_->setEnabled(!s.breakpoints().empty());
    rebuildBreakpoints();
    rebuildStack();
    rebuildCode();
    rebuildLocals();
    if (paused) readWatches();
    invalidate();
}

void HmiDebugPane::rebuildBreakpoints() {
    auto& s = HmiDebugSession::instance();
    auto* rt = runtime();
    const std::uint32_t hitId = rt && rt->paused() && rt->debugHit() ? rt->debugHit()->breakpoint : 0;
    std::vector<std::uint32_t> chosenIds;                // le point choisi, par son id (la liste peut avoir change)
    for (const auto r : bpTable_->selectedModelRows())
        if (r < bpIds_.size()) chosenIds.push_back(bpIds_[r]);
    bpModel_->rows.clear();
    bpIds_.clear();
    for (const auto& b : s.breakpoints()) {
        BreakModel::Row r;
        r.id = b.id;
        const std::string name = codeName(b.script, b.function);
        // 1.13.0 : la ligne d'abord - en trois colonnes, "Ou" se coupe a droite ("script Statistiqu...") et deux points
        // du meme code ne se distinguaient plus.
        r.where = "l. " + std::to_string(b.effectiveLine ? b.effectiveLine : b.line) + " \xC2\xB7 " + (name.empty() ? std::string("?") : name);
        r.condition = b.condition;
        r.hits = plural(b.hits, "passage", "passages");
        r.note = b.note;
        r.enabled = b.enabled;
        r.armed = b.note.find("il ne s'arr\xC3\xAA") == std::string::npos;
        r.hit = b.id == hitId;
        bpModel_->rows.push_back(std::move(r));
        bpIds_.push_back(b.id);
    }
    std::vector<ui::RowIndex> again;
    for (std::size_t i = 0; i < bpIds_.size(); ++i)
        if (std::find(chosenIds.begin(), chosenIds.end(), bpIds_[i]) != chosenIds.end()) again.push_back(static_cast<ui::RowIndex>(i));
    refill(*bpTable_, bpModel_, std::move(again));
}

void HmiDebugPane::rebuildStack() {
    auto* rt = runtime();
    stackModel_->rows.clear();
    std::vector<ui::RowIndex> shown;
    if (rt && rt->paused() && rt->debugHit()) {
        const auto& stack = rt->debugHit()->stack;
        const std::size_t chosen = std::min(HmiDebugSession::instance().level(), stack.empty() ? 0 : stack.size() - 1);
        if (!stack.empty()) shown.push_back(static_cast<ui::RowIndex>(chosen));   // la ligne choisie : le niveau montre
        for (std::size_t k = 0; k < stack.size(); ++k) {
            const auto& f = stack[stack.size() - 1 - k];
            StackModel::Row r;
            r.label = f.label;
            r.code = f.script != kNoId || f.function != kNoId;
            r.line = r.code && f.line ? std::to_string(f.line) : std::string{};
            r.chosen = k == chosen;
            stackModel_->rows.push_back(std::move(r));
        }
    }
    refill(*stackTable_, stackModel_, std::move(shown));
}

void HmiDebugPane::rebuildCode() {
    const auto* f = shownFrame();
    HmiDebugTarget code = f ? HmiDebugTarget{f->script, f->function} : HmiDebugTarget{};
    if (!f) {
        // Hors pause : le code du premier point d'arret, s'il y en a un (on y voit ses ronds).
        const auto& list = HmiDebugSession::instance().breakpoints();
        if (!list.empty()) code = HmiDebugTarget{list.front().script, list.front().function};
    }
    const std::string text = code.empty() ? std::string{} : codeText(code.script, code.function);
    if (!(code == codeShown_) || code_->text() != text) {
        codeShown_ = code;
        code_->setText(text);
        margin_.reset();
    }
    const std::string name = code.empty() ? std::string{} : codeName(code.script, code.function);
    codePanel_->setTitle(name.empty() ? std::string("CODE") : "CODE  \xC2\xB7  " + name + "  \xC2\xB7  lecture seule");
    // Les valeurs de la ligne d'arret, a droite de son code (le niveau du haut seulement).
    std::vector<ui::MultiLineText::LineNote> notes;
    auto* rt = runtime();
    if (f && rt && rt->debugHit() && HmiDebugSession::instance().level() == 0 && f->line > 0) {
        std::string v;
        for (const auto& [n, val] : rt->debugHit()->values) {
            if (v.size() > 90) break;
            v += (v.empty() ? "" : "  \xC2\xB7  ") + n + " = " + val;
        }
        if (!v.empty()) notes.push_back(ui::MultiLineText::LineNote{f->line - 1, v, ui::Tone::Accent});
    }
    code_->setLineNotes(std::move(notes));
    margin_.refresh();
}

void HmiDebugPane::rebuildLocals() {
    std::vector<std::string> chosenKeys;                 // la locale choisie, par son chemin (la liste se refait)
    for (const auto r : localsTable_->selectedModelRows())
        if (r < locals_.size()) chosenKeys.push_back(locals_[r].key);
    locals_.clear();
    const auto* f = shownFrame();
    if (f) {
        std::function<void(const hmi::DebugLocal&, int, const std::string&)> add = [&](const hmi::DebugLocal& l, int depth, const std::string& parent) {
            LocalRow r;
            r.name = l.name;
            r.type = l.type;
            r.value = l.value;
            r.depth = depth;
            r.key = parent + "/" + l.name;
            r.expandable = !l.children.empty();
            r.open = r.expandable && expanded_.count(r.key) != 0;
            r.parameter = l.parameter;
            const bool open = r.open;
            const std::string key = r.key;
            locals_.push_back(std::move(r));
            if (open)
                for (const auto& c : l.children) add(c, depth + 1, key);
        };
        for (const auto& l : f->locals) add(l, 0, f->label);
    }
    std::vector<ui::RowIndex> again;
    for (std::size_t i = 0; i < locals_.size(); ++i)
        if (std::find(chosenKeys.begin(), chosenKeys.end(), locals_[i].key) != chosenKeys.end()) again.push_back(static_cast<ui::RowIndex>(i));
    refill(*localsTable_, localsModel_, std::move(again));
}

void HmiDebugPane::readWatches() {
    watchValues_.assign(watches_.size(), std::string{});
    auto* rt = runtime();
    for (std::size_t i = 0; i < watches_.size(); ++i) {
        if (!rt || !rt->running()) {
            watchValues_[i] = "(l'IHM ne tourne pas)";
            continue;
        }
        std::string v, why;
        if (rt->debugEvaluate(watches_[i], v, &why)) watchValues_[i] = v;
        else watchValues_[i] = rt->paused() ? "(" + why + ")" : "(se lira en pause, ou n'existe pas ici)";
    }
    refill(*watchTable_, watchModel_, watchTable_->selectedModelRows());
    invalidate();
}

// ------------------------------------------------------------------ dessin ---
void HmiDebugPane::onLayout() {
    const auto b = bounds();
    // Etroit (le panneau de droite de la simulation, ~570 px a 1920) : trois colonnes ne tiennent pas. En lignes :
    // la pile et le code en haut, les locales dessous (toute la largeur : nom, type, valeur), puis les points et les
    // espions cote a cote ; l'etat en clair sur deux lignes. Large : les trois colonnes.
    const bool narrow = b.w < kNarrow;
    if (narrow != narrow_) {
        narrow_ = narrow;
        split_->setOrientation(narrow ? ui::Orientation::Vertical : ui::Orientation::Horizontal);
        split_->setOrder(narrow ? std::vector<std::size_t>{1, 2, 0} : std::vector<std::size_t>{});
        split_->setRatios(narrow ? std::vector<float>{0.25f, 0.52f, 0.23f} : std::vector<float>{0.34f, 0.40f, 0.26f});
        centerSplit_->setRatios(narrow ? std::vector<float>{0.42f, 0.58f} : std::vector<float>{0.32f, 0.68f});
        leftSplit_->setOrientation(narrow ? ui::Orientation::Horizontal : ui::Orientation::Vertical);
        // Etroit : pas de place pour « Retirer le point choisi » - Suppr dans le tableau, la marge, Tout retirer.
        removeBtn_->setVisibility(narrow ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    }
    const float stateH = narrow_ ? kState2 : kState;
    band_ = {b.x, b.y, b.w, kBand};
    stateBand_ = {b.x, b.y + kBand, b.w, stateH};
    float x = b.x + 8.f;
    for (auto* btn : buttons_) {
        const float w = std::max(70.f, btn->sizeHint().preferred.w);
        btn->setBounds({x, b.y + 5.f, w, 26.f});
        x += w + 6.f;
    }
    const float cw = std::max(90.f, clearBtn_->sizeHint().preferred.w);
    clearBtn_->setBounds({std::max(x, b.x + b.w - 8.f - cw), b.y + 5.f, cw, 26.f});
    split_->setBounds({b.x, b.y + kBand + stateH, b.w, std::max(0.f, b.h - kBand - stateH)});
}

void HmiDebugPane::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(band_, c.panelBg);
    ctx.r.fillRect({band_.x, band_.y + band_.h - 1.f, band_.w, 1.f}, c.border);
    auto* rt = runtime();
    const bool paused = rt && rt->paused();
    ctx.r.fillRect(stateBand_, paused ? c.headerBg : c.panelBg);
    const gfx::FontId font = ctx.theme.font.smallUi;
    const float lh = ctx.r.lineHeight(font);
    std::string text = state_;
    gfx::Color color = paused ? c.warning : c.textMuted;
    if (!message_.empty()) {
        text = message_;
        color = messageWarn_ ? c.warning : c.text;
    }
    // Le texte tient dans la bande : coupe a la largeur (une ellipse) ; etroit, d'abord a un espace (deux lignes).
    const float room = stateBand_.w - 20.f;
    const auto fit = [&](std::string t) {
        if (room <= 40.f || ctx.r.measure(t, font).width <= room) return t;
        while (!t.empty() && ctx.r.measure(t + "\xE2\x80\xA6", font).width > room) {
            t.pop_back();
            while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        }
        return t + "\xE2\x80\xA6";
    };
    std::string first = text, second;
    if (narrow_ && room > 40.f && ctx.r.measure(text, font).width > room) {
        std::size_t cut = std::string::npos;
        for (std::size_t at = text.find(' '); at != std::string::npos; at = text.find(' ', at + 1)) {
            if (ctx.r.measure(text.substr(0, at), font).width > room) break;
            cut = at;
        }
        if (cut != std::string::npos) {
            first = text.substr(0, cut);
            second = text.substr(cut + 1);
        }
    }
    if (second.empty()) {
        ctx.r.drawText({stateBand_.x + 10.f, stateBand_.y + (stateBand_.h - lh) / 2.f}, fit(first), font, color);
    } else {
        const float top = stateBand_.y + (stateBand_.h - 2.f * lh) / 2.f;
        ctx.r.drawText({stateBand_.x + 10.f, top}, first, font, color);
        ctx.r.drawText({stateBand_.x + 10.f, top + lh}, fit(second), font, color);
    }
}

ui::EventResult HmiDebugPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !k->repeat && !k->mods.ctrl && !k->mods.alt) {
        if (debugKey(k->key, k->mods.shift)) return ui::EventResult::Consumed;
        // Suppr dans le tableau des points : retirer le point choisi.
        if (k->key == ui::Key::Delete && !k->mods.shift && bpTable_->focused() && !bpTable_->selectedModelRows().empty()) {
            runAction(ARemoveBreakpoint);
            return ui::EventResult::Consumed;
        }
    }
    if (std::get_if<ui::MouseDown>(&ev) && !message_.empty()) {
        message_.clear();                      // un geste : le message d'avant s'efface, l'etat revient
        invalidate();
    }
    return ui::EventResult::Ignored;
}

} // namespace app
