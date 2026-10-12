// =============================================================================
//  app/hmi/HmiCalcDialog.cpp - 1.12.7 : la fenetre d'une valeur calculee au
//  demarrage (voir l'en-tete)
// =============================================================================
#include "HmiCalcDialog.hpp"

#include "../../core/FieldCodec.hpp"
#include "../../hmi/HmiDecl.hpp"        // les locales d'un texte (decl::extract)
#include "../../hmi/HmiDeclEdit.hpp"    // « Deplacer dans Locales » (moveVarBlocks)
#include "../../menu/MenuManager.hpp"
#include "../../ui/Syntax.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace app {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kButton{16};

float buttonWidth(const std::string& text) { return std::max(96.f, ui::measureWidth(text, kButton) + 36.f); }

std::string trimmed(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool startsWith(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

// Un texte coupe en lignes (les \n d'abord, puis les mots a la largeur).
std::vector<std::string> wrapped(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto nl = s.find('\n', from);
        const std::string para = s.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        std::string line, word;
        const auto flush = [&] {
            if (word.empty()) return;
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(trial, f) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            word.clear();
        };
        for (const char c : para) {
            if (c == ' ') flush();
            else word += c;
        }
        flush();
        if (!line.empty()) out.push_back(line);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    return out;
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= s.size()) {
        const auto at = s.find(", ", from);
        const std::string one = trimmed(s.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (!one.empty()) out.push_back(one);
        if (at == std::string::npos) break;
        from = at + 2;
    }
    return out;
}

} // namespace

// ------------------------------------------------------------- la grille des locales ----
class HmiCalcDialog::LocalsModel final : public ui::ITableModel {
public:
    std::vector<Local> rows;

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 4; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        switch (c) {
            case 0: return "Nom";
            case 1: return "Type";
            case 2: return "Valeur initiale";
            default: return "Documentation";
        }
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        const auto& l = rows[r];
        return c == 0 ? l.name : c == 1 ? l.type : c == 2 ? l.value : l.doc;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows.size()) return s;
        s.monospace = c == 1 || c == 2;
        if (c == 3) s.fgTone = ui::Tone::Muted;
        if (c == 0 && rows[r].name.empty()) s.fgTone = ui::Tone::Error;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return r < rows.size() && c < 4; }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (r >= rows.size() || c >= 4) return false;
        std::string v(text);
        for (auto& ch : v)
            if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        v = trimmed(v);
        auto& l = rows[r];
        (c == 0 ? l.name : c == 1 ? l.type : c == 2 ? l.value : l.doc) = v;
        modelReset->emit();
        return true;
    }
};

std::vector<HmiCalcDialog::Local> HmiCalcDialog::parseLocals(const std::string& text) {
    std::vector<Local> out;
    if (trimmed(text).empty()) return out;
    const auto x = hmi::decl::extract("VAR\n" + text + "\nEND_VAR\n");
    for (const auto& d : x.decls) out.push_back({d.name, d.type, d.initial, d.comment});
    return out;
}

std::string HmiCalcDialog::composeLocals(const std::vector<Local>& rows) {
    std::string out;
    for (const auto& l : rows) {
        if (l.name.empty() && l.type.empty() && l.value.empty() && l.doc.empty()) continue;   // une ligne vide ne compte pas
        out += l.name + " : " + l.type;
        if (!l.value.empty()) out += " := " + l.value;
        out += ';';
        if (!l.doc.empty()) out += "   // " + l.doc;
        out += '\n';
    }
    return out;
}

HmiCalcDialog::Facts HmiCalcDialog::factsOf(const std::string& preview) {
    Facts f;
    std::size_t from = 0;
    while (from < preview.size()) {
        const auto nl = preview.find('\n', from);
        const std::string line = trimmed(preview.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        from = nl == std::string::npos ? preview.size() : nl + 1;
        if (line.empty()) continue;
        static const std::string kValue = "Valeur au d\xC3\xA9marrage : ", kError = "Erreur : ", kReads = "Lit : ", kBy = "Lu par : ";
        if (startsWith(line, kValue)) f.value = line.substr(kValue.size());
        else if (startsWith(line, kError)) f.error = line.substr(kError.size());
        else if (startsWith(line, kReads)) f.reads = splitList(line.substr(kReads.size()));
        else if (startsWith(line, kBy)) f.readBy = splitList(line.substr(kBy.size()));
        else if (startsWith(line, "Faute") || startsWith(line, "Attention")) {
            Facts::Problem p;
            p.error = startsWith(line, "Faute");
            // « Faute ligne 3 : ... » : la ligne ; le texte apres le premier « : ».
            const auto colon = line.find(" : ");
            const std::string head = line.substr(0, colon == std::string::npos ? line.size() : colon);
            if (const auto at = head.find("ligne "); at != std::string::npos) p.line = std::atoi(head.c_str() + at + 6);
            p.text = (p.line > 0 ? "ligne " + std::to_string(p.line) + " : " : std::string{})
                   + (colon == std::string::npos ? line : line.substr(colon + 3));
            f.problems.push_back(std::move(p));
        } else {
            f.note += (f.note.empty() ? "" : "\n") + line;
        }
    }
    return f;
}

// ------------------------------------------------------------- le corps ----
namespace {

class CalcBody final : public ui::Widget {
public:
    CalcBody(std::string title, std::string help) : title_(std::move(title)), help_(std::move(help)) {}
    ui::Widget*                    code{nullptr};
    ui::Widget*                    table{nullptr};
    ui::Button*                    tabCode{nullptr};
    ui::Button*                    tabLocals{nullptr};
    std::vector<ui::Button*>       codeBar, localsBar;
    ui::Button*                    cancel{nullptr};
    ui::Button*                    ok{nullptr};
    const HmiCalcDialog::Facts*    facts{nullptr};
    const bool*                    localsShown{nullptr};
    std::string                    status;
    std::function<void(std::size_t)> problemClicked;

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1180.f, std::max(640.f, r.w - 40.f));
        const float h = std::min(760.f, std::max(480.f, r.h - 40.f));
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + 16.f;
        side_ = std::clamp(w * 0.3f, 260.f, 360.f);
        leftW_ = w - 32.f - side_ - 14.f;
        helpAt_ = panel_.y + 34.f + 8.f;
        const float tabsY = helpAt_ + 42.f;
        float tx = x;
        for (auto* b : {tabCode, tabLocals}) {
            if (!b) continue;
            const float bw = buttonWidth(b->text());
            b->setBounds({tx, tabsY, bw, 28.f});
            tx += bw + 6.f;
        }
        const float by = panel_.bottom() - 44.f;
        const float contentY = tabsY + 36.f;
        const float barY = by - 12.f - 30.f;
        const float contentH = std::max(80.f, barY - 8.f - contentY);
        const bool loc = localsShown && *localsShown;
        if (code) {
            code->setVisibility(loc ? ui::Visibility::Collapsed : ui::Visibility::Visible);
            code->setBounds({x, contentY, leftW_, contentH});
        }
        if (table) {
            table->setVisibility(loc ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            table->setBounds({x, contentY, leftW_, contentH});
        }
        const auto bar = [&](std::vector<ui::Button*>& buttons, bool shown) {
            float bx = x;
            for (auto* b : buttons) {
                if (!b) continue;
                const bool visible = shown && b->visibility() != ui::Visibility::Collapsed;
                if (!shown) b->setVisibility(ui::Visibility::Hidden);
                else if (b->visibility() == ui::Visibility::Hidden) b->setVisibility(ui::Visibility::Visible);
                if (!visible) continue;
                const float bw = buttonWidth(b->text());
                b->setBounds({bx, barY, bw, 30.f});
                bx += bw + 8.f;
            }
        };
        bar(codeBar, !loc);
        bar(localsBar, loc);
        sideBox_ = {x + leftW_ + 14.f, tabsY, side_, by - 12.f - tabsY};
        float bx = panel_.right() - 16.f;
        for (auto* b : {ok, cancel}) {
            if (!b) continue;
            const float bw = buttonWidth(b->text());
            bx -= bw;
            b->setBounds({bx, by, bw, 30.f});
            bx -= 10.f;
        }
        statusAt_ = {x, by + 6.f};
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.fillRect({panel_.x, panel_.y, panel_.w, 34.f}, c.headerBg);
        const auto bold = ctx.theme.font.uiBold;
        r.drawText({panel_.x + 12.f, panel_.y + (34.f - r.lineHeight(bold)) * 0.5f}, title_, bold, c.text);
        float y = helpAt_;
        for (const auto& l : wrapped(help_, kSmall, panel_.w - 32.f)) {
            if (y > helpAt_ + 36.f) break;
            r.drawText({panel_.x + 16.f, y}, l, kSmall, c.textMuted);
            y += 17.f;
        }
        // La colonne de droite : Apercu, Lit, Lu par, Problemes.
        r.fillRect(sideBox_, c.headerBg);
        r.strokeRect(sideBox_, c.border, 1.f);
        problemRects_.clear();
        if (!facts) return;
        const float inner = sideBox_.w - 20.f;
        y = sideBox_.y + 8.f;
        const float bottom = sideBox_.bottom() - 6.f;
        const auto heading = [&](const std::string& t) {
            if (y + 18.f > bottom) return false;
            r.drawText({sideBox_.x + 10.f, y}, t, bold, c.text);
            y += 22.f;
            return true;
        };
        const auto lines = [&](const std::string& t, gfx::Color col, std::size_t max) {
            std::size_t n = 0;
            for (const auto& l : wrapped(t, kSmall, inner)) {
                if (y + 17.f > bottom || n++ >= max) break;
                r.drawText({sideBox_.x + 12.f, y}, l, kSmall, col);
                y += 17.f;
            }
            y += 6.f;
        };
        const auto& f = *facts;
        if (heading("Aper\xC3\xA7u")) {
            if (!f.error.empty()) lines("Erreur : " + f.error + " - le d\xC3\xA9marrage s'arr\xC3\xAAterait", c.error, 6);
            else if (!f.value.empty()) lines(f.value, c.accent, 8);
            if (!f.note.empty()) lines(f.note, c.warning, 4);
            if (f.error.empty() && f.value.empty() && f.note.empty()) lines("\xE2\x80\x94", c.textMuted, 1);
        }
        const auto join = [](const std::vector<std::string>& v, const std::string& none) {
            if (v.empty()) return none;
            std::string s;
            for (const auto& x : v) s += (s.empty() ? "" : "  \xC2\xB7  ") + x;
            return s;
        };
        if (heading("Lit")) lines(join(f.reads, "rien du projet : des litt\xC3\xA9raux"), c.text, 4);
        if (heading("Lu par")) lines(join(f.readBy, "personne pour l'instant"), f.readBy.empty() ? c.textMuted : c.text, 5);
        if (heading("Probl\xC3\xA8mes")) {
            if (f.problems.empty()) lines("aucun", c.ok, 1);
            for (std::size_t i = 0; i < f.problems.size(); ++i) {
                const auto& p = f.problems[i];
                const float top = y;
                lines(p.text, p.error ? c.error : c.warning, 4);
                if (p.line > 0) problemRects_.push_back({i, gfx::Rect{sideBox_.x, top, sideBox_.w, y - top}});
                if (y + 17.f > bottom) break;
            }
        }
        if (!status.empty()) r.drawText(statusAt_, status, kSmall, c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && problemClicked)
            for (const auto& [i, box] : problemRects_)
                if (box.contains(d->pos)) {
                    problemClicked(i);
                    return ui::EventResult::Consumed;
                }
        return ui::EventResult::Ignored;
    }

private:
    std::string title_, help_;
    gfx::Rect   panel_{}, sideBox_{};
    gfx::Point  statusAt_{};
    float       side_{300.f}, leftW_{500.f}, helpAt_{0.f};
    mutable std::vector<std::pair<std::size_t, gfx::Rect>> problemRects_;
};

} // namespace

// ------------------------------------------------------------- la fenetre ----
HmiCalcDialog::HmiCalcDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)), model_(std::make_shared<LocalsModel>()) {
    model_->rows = parseLocals(spec_.locals);
}

HmiCalcDialog::~HmiCalcDialog() = default;

menu::MenuTraits HmiCalcDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status HmiCalcDialog::buildUi() {
    const std::string help = "Le code \xC3\xA9" "crit " + spec_.name + " (" + spec_.type + ") sous son nom ; il tourne une fois au d\xC3\xA9marrage"
                           + (spec_.constant ? std::string(", puis la constante est fig\xC3\xA9" "e (comme un final de Java).")
                                             : std::string(", apr\xC3\xA8s les constantes ; ensuite les scripts \xC3\xA9" "crivent la variable comme toute autre."))
                           + " Ses locales sont dans l'onglet Locales, pas dans le code. Ctrl+Espace : l'aide \xC3\xA0 la saisie ; F5 : calculer ici.";
    auto body = std::make_unique<CalcBody>(spec_.title, help);
    const std::string base = spec_.id;
    const auto button = [&](const std::string& text, const std::string& id) {
        return &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(text, base + "." + id)));
    };
    // Les onglets.
    tabCode_ = button("</> Code", "onglet.code");
    tabLocals_ = button("\xE2\x89\xA1 Locales", "onglet.locales");
    links_ += tabCode_->clicked->connect([this] { showLocals(false); });
    links_ += tabLocals_->clicked->connect([this] { showLocals(true); });
    body->tabCode = tabCode_;
    body->tabLocals = tabLocals_;
    // Le code.
    auto e = std::make_unique<ui::MultiLineText>(base + ".code");
    e->setLanguage(ui::Language::StructuredText);
    e->setText(spec_.calc);
    e->setReadOnly(false);
    code_ = &static_cast<ui::MultiLineText&>(body->addChild(std::move(e)));
    links_ += code_->textChanged->connect([this](const std::string&) { changed(); });
    if (spec_.assist) spec_.assist(*code_, [this] { return localsText(); });
    body->code = code_;
    auto* run = button("\xE2\x96\xB6 Calculer (F5)", "calculer");
    links_ += run->clicked->connect([this] { recompute(); });
    auto* back = button("Revenir au calcul d'origine", "revenir");
    links_ += back->clicked->connect([this] { revert(); });
    moveButton_ = button("D\xC3\xA9placer dans Locales", "deplacer");
    links_ += moveButton_->clicked->connect([this] { (void)moveVarBlocks(); });
    body->codeBar = {run, back, moveButton_};
    // Les locales.
    auto table = std::make_unique<ui::TableView>(base + ".locales");
    table->setModel(model_);
    table->setColumns({{"Nom", 150.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                       {"Type", 200.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                       {"Valeur initiale", 220.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                       {"Documentation", 260.f, 60.f, true, false, true, ui::Align::Start, 0, false}});
    table_ = &static_cast<ui::TableView&>(body->addChild(std::move(table)));
    links_ += table_->cellEdited->connect([this](ui::RowIndex, std::size_t, const std::string&, bool accepted) {
        if (accepted) changed();
    });
    body->table = table_;
    auto* add = button("+ Ajouter", "locale.ajouter");
    links_ += add->clicked->connect([this] { (void)addLocal(); });
    auto* remove = button("Supprimer", "locale.supprimer");
    links_ += remove->clicked->connect([this] { (void)removeLocal(selectedLocal()); });
    auto* up = button("Monter", "locale.monter");
    links_ += up->clicked->connect([this] { (void)moveLocal(selectedLocal(), -1); });
    auto* down = button("Descendre", "locale.descendre");
    links_ += down->clicked->connect([this] { (void)moveLocal(selectedLocal(), +1); });
    body->localsBar = {add, remove, up, down};
    // Les boutons du bas.
    body->cancel = button("Annuler", "annuler");
    links_ += body->cancel->clicked->connect([this] { finish(false); });
    body->ok = button("Appliquer", "appliquer");
    body->ok->setStyle(ui::Button::Style::Primary);
    links_ += body->ok->clicked->connect([this] { finish(true); });
    body->facts = &facts_;
    body->localsShown = &localsShown_;
    body->problemClicked = [this](std::size_t i) { (void)goToProblem(i); };
    body_ = body.get();
    setRoot(std::move(body));
    recompute();
    refreshButtons();
    focus().clear();
    focus().focus(code_);
    if (spec_.line > 0) code_->selectRange(static_cast<std::size_t>(spec_.line - 1), 0, 0);
    return core::ok();
}

std::string HmiCalcDialog::calcText() const { return code_ ? code_->text() : spec_.calc; }
std::string HmiCalcDialog::localsText() const { return composeLocals(model_->rows); }
const std::vector<HmiCalcDialog::Local>& HmiCalcDialog::locals() const { return model_->rows; }

void HmiCalcDialog::setTexts(const std::string& calc, const std::string& locals) {
    if (code_) code_->setText(calc);
    else spec_.calc = calc;
    model_->rows = parseLocals(locals);
    model_->modelReset->emit();
    recompute();
    refreshButtons();
}

void HmiCalcDialog::changed() {
    dirty_ = true;
    changedAt_ = now_;
    refreshButtons();
}

void HmiCalcDialog::recompute() {
    dirty_ = false;
    live_ = spec_.preview ? spec_.preview(calcText(), localsText()) : std::string{};
    facts_ = factsOf(live_);
    if (body_) body_->invalidate();
}

void HmiCalcDialog::refreshButtons() {
    if (tabCode_) tabCode_->setStyle(localsShown_ ? ui::Button::Style::Flat : ui::Button::Style::Primary);
    if (tabLocals_) {
        std::size_t n = 0;
        for (const auto& l : model_->rows)
            if (!l.name.empty()) ++n;
        tabLocals_->setText("\xE2\x89\xA1 Locales  " + std::to_string(n));
        tabLocals_->setStyle(localsShown_ ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    }
    // « Deplacer dans Locales » : seulement quand le code a un bloc VAR.
    if (moveButton_) {
        const bool block = !hmi::decl::extract(calcText()).blocks.empty();
        const auto want = block ? (localsShown_ ? ui::Visibility::Hidden : ui::Visibility::Visible) : ui::Visibility::Collapsed;
        if (moveButton_->visibility() != want) moveButton_->setVisibility(want);
    }
    if (auto* b = dynamic_cast<CalcBody*>(body_)) {
        b->status = localsShown_ ? std::string("Double-clic ou F2 : \xC3\xA9" "crire une case ; une ligne vide ne compte pas.")
                                 : std::string("F5 : calculer ; \xC3\x89" "chap : annuler.");
        b->invalidateLayout();
        b->invalidate();
    }
}

void HmiCalcDialog::showLocals(bool on) {
    localsShown_ = on;
    refreshButtons();
    focus().clear();
    focus().focus(on ? static_cast<ui::Widget*>(table_) : static_cast<ui::Widget*>(code_));
}

std::size_t HmiCalcDialog::selectedLocal() const {
    if (!table_) return static_cast<std::size_t>(-1);
    const auto rows = table_->selectedModelRows();
    return rows.empty() ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(rows.front());
}

bool HmiCalcDialog::addLocal() {
    model_->rows.push_back({"Locale" + std::to_string(model_->rows.size() + 1), "REAL", "", ""});
    model_->modelReset->emit();
    if (table_) table_->selectModelRows({static_cast<ui::RowIndex>(model_->rows.size() - 1)});
    changed();
    return true;
}

bool HmiCalcDialog::removeLocal(std::size_t row) {
    if (row >= model_->rows.size()) return false;
    model_->rows.erase(model_->rows.begin() + static_cast<std::ptrdiff_t>(row));
    model_->modelReset->emit();
    changed();
    return true;
}

bool HmiCalcDialog::moveLocal(std::size_t row, int delta) {
    if (row >= model_->rows.size()) return false;
    const auto to = static_cast<std::ptrdiff_t>(row) + delta;
    if (to < 0 || to >= static_cast<std::ptrdiff_t>(model_->rows.size())) return false;
    std::swap(model_->rows[row], model_->rows[static_cast<std::size_t>(to)]);
    model_->modelReset->emit();
    if (table_) table_->selectModelRows({static_cast<ui::RowIndex>(to)});
    changed();
    return true;
}

bool HmiCalcDialog::setLocal(std::size_t row, std::size_t column, const std::string& text) {
    if (!model_->setCellText(static_cast<ui::RowIndex>(row), column, text)) return false;
    changed();
    return true;
}

bool HmiCalcDialog::moveVarBlocks(std::string* why) {
    std::string calc = calcText(), locals = localsText(), err;
    if (!hmi::decledit::moveVarBlocks(calc, locals, &err)) {
        if (why) *why = err;
        return false;
    }
    if (code_) code_->setText(calc);
    else spec_.calc = calc;
    model_->rows = parseLocals(locals);
    model_->modelReset->emit();
    recompute();
    showLocals(true);
    return true;
}

void HmiCalcDialog::revert() {
    const Spec& s = spec_;
    if (code_) code_->setText(s.calc);
    model_->rows = parseLocals(s.locals);
    model_->modelReset->emit();
    recompute();
    refreshButtons();
}

bool HmiCalcDialog::goToProblem(std::size_t index) {
    if (index >= facts_.problems.size() || facts_.problems[index].line <= 0 || !code_) return false;
    showLocals(false);
    code_->selectRange(static_cast<std::size_t>(facts_.problems[index].line - 1), 0, 0);
    return true;
}

void HmiCalcDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    now_ = f.totalSeconds;
    // Une demi-seconde apres la derniere frappe : l'apercu suit.
    if (dirty_ && now_ - changedAt_ >= 0.5) recompute();
}

ui::EventResult HmiCalcDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Echap : d'abord la liste de l'aide a la saisie, ou la case en cours d'ecriture.
        const bool busy = (code_ && code_->completionOpen()) || (table_ && table_->cellEditing());
        if (k->key == ui::Key::Escape && !busy) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::F5) {
            recompute();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void HmiCalcDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? core::joinFields({calcText(), localsText()}) : std::string{}});
}

} // namespace app
