// =============================================================================
//  app/ai/AiFileModelDialog.cpp - voir AiFileModelDialog.hpp
// =============================================================================
#include "AiFileModelDialog.hpp"

#include "AiAssistant.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace app::ai {

namespace {

const gfx::FontId kTitleFont{20};
const gfx::FontId kText{14};
const gfx::FontId kSmall{13};
constexpr float kRow = 32.f;

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

std::string guill(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }
std::string percent(double v) { return std::to_string(static_cast<int>(v * 100.0 + 0.5)) + " %"; }
const std::string kNone = "(aucune)";

ui::TableView::Column col(const char* title, float w) {
    ui::TableView::Column c;
    c.title = title;
    c.width = w;
    return c;
}

} // namespace

// ------------------------------------------------------------- la correspondance ---
class AiFileModelDialog::MapModel final : public ui::ITableModel {
public:
    explicit MapModel(AiFileModelDialog& d) : d_(d) {}
    std::vector<std::string> roles;
    [[nodiscard]] std::size_t rowCount() const override { return roles.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "R\xC3\xB4le" : "Colonne"; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= roles.size()) return {};
        if (c == 0) return sheets::roleLabel(roles[r]);
        const int at = d_.layout_.colOf(roles[r]);
        if (at < 0) return kNone;
        return choiceOf(static_cast<std::size_t>(at));
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r < roles.size() && c == 1 && d_.layout_.colOf(roles[r]) < 0) s.fgTone = ui::Tone::Muted;
        if (r < roles.size() && c == 0 && d_.layout_.colOf(roles[r]) >= 0) s.bold = true;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override { return r < roles.size() && c == 1 && d_.ready(); }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex, std::size_t c) const override {
        if (c != 1) return {};
        return d_.columnChoices();
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        if (r >= roles.size() || c != 1) return false;
        if (text == kNone || text.empty()) return d_.setColumn(roles[r], -1);
        // "B \xE2\x80\x94 Designation" : la lettre avant le tiret
        std::string letter;
        for (const char ch : text) {
            if (!std::isalpha(static_cast<unsigned char>(ch))) break;
            letter += ch;
        }
        if (letter.empty()) return false;
        std::size_t col = 0;
        for (const char ch : letter) col = col * 26 + static_cast<std::size_t>(std::toupper(static_cast<unsigned char>(ch)) - 'A' + 1);
        return d_.setColumn(roles[r], static_cast<int>(col) - 1);
    }
    [[nodiscard]] std::string choiceOf(std::size_t c) const {
        const sheets::Sheet* s = d_.workbook_ ? d_.workbook_->sheet(d_.layout_.sheet) : nullptr;
        const std::string h = s ? sheets::headerText(*s, d_.layout_.headerRow, c) : std::string{};
        return sheets::columnLetter(c) + (h.empty() ? std::string{} : " \xE2\x80\x94 " + h);
    }
    void reset() { modelReset->emit(); }

private:
    AiFileModelDialog& d_;
};

// --------------------------------------------------------------------- l'apercu ---
class AiFileModelDialog::PreviewModel final : public ui::ITableModel {
public:
    explicit PreviewModel(AiFileModelDialog& d) : d_(d) {}
    [[nodiscard]] std::size_t rowCount() const override { return std::min<std::size_t>(d_.preview_.rows.size(), 40); }
    [[nodiscard]] std::size_t columnCount() const override { return 1 + d_.preview_.columns.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        if (c == 0) return "Ligne";
        return c - 1 < d_.preview_.columns.size() ? sheets::roleLabel(d_.preview_.columns[c - 1].role) : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= d_.preview_.rows.size()) return {};
        const auto& row = d_.preview_.rows[r];
        if (c == 0) return std::to_string(row.line);
        return c - 1 < row.values.size() ? row.values[c - 1].second : std::string{};
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        if (c == 0) return a < b;
        return cellText(a, c) < cellText(b, c);
    }
    void reset() { modelReset->emit(); }

private:
    AiFileModelDialog& d_;
};

// ------------------------------------------------------------------------ le corps ---
class AiFileModelDialog::Body final : public ui::Widget {
public:
    explicit Body(AiFileModelDialog& d) : ui::Widget("ai.modele-fichier.corps"), d_(d) {}

protected:
    struct Label {
        gfx::Point  at;
        std::string text;
        bool        heading;
        float       w;
    };

    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1080.f, std::max(720.f, r.w - 40.f));
        const float h = std::min(720.f, std::max(540.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        labels_.clear();
        const float x = panel_.x + 20.f, cw = panel_.w - 40.f;
        float y = panel_.y + 52.f;
        headlineAt_ = {x, y, cw, 22.f};
        y += 30.f;
        // la feuille, le genre, la ligne d'en-tete, les lignes sautees
        {
            float bx = x;
            const auto field = [&](const char* label, ui::Widget* wdg, float fw) {
                const float lw = ui::measureWidth(label, kText) + 8.f;
                labels_.push_back({{bx, y + 6.f}, label, false, lw});
                wdg->setBounds({bx + lw, y, fw, kRow - 4.f});
                bx += lw + fw + 18.f;
            };
            field("Feuille", d_.sheetBox_, std::min(260.f, cw * 0.26f));
            field("Genre", d_.genreBox_, std::min(250.f, cw * 0.24f));
            field("En-t\xC3\xAA" "te ligne", d_.headerIn_, 58.f);
            field("Sauter", d_.skipIn_, 50.f);
        }
        y += kRow + 10.f;
        // les deux tables
        const float bottomBlock = 2.f * kRow + 90.f;
        const float tableH = std::max(140.f, panel_.bottom() - bottomBlock - 24.f - y);
        const float leftW = std::floor(cw * 0.40f);
        labels_.push_back({{x, y}, "CORRESPONDANCE DES COLONNES (un choix par r\xC3\xB4le : double-clic)", true, leftW - 10.f});
        previewHeadAt_ = {x + leftW + 14.f, y};
        y += 22.f;
        d_.mapTable_->setBounds({x, y, leftW - 10.f, tableH});
        d_.previewTable_->setBounds({x + leftW + 14.f, y, cw - leftW - 14.f, tableH});
        y += tableH + 12.f;
        // les modeles de fichiers
        labels_.push_back({{x, y}, "MOD\xC3\x88LES DE FICHIERS (m\xC3\xA9moire de l'IA, partag\xC3\xA9s par l'apprentissage .xia)", true, cw});
        y += 22.f;
        {
            const float mw = std::min(380.f, cw * 0.42f);
            d_.modelBox_->setBounds({x, y, mw, kRow - 4.f});
            const float aw = ui::measureWidth(d_.applyModelB_->text(), gfx::FontId{16}) + 28.f;
            d_.applyModelB_->setBounds({x + mw + 8.f, y, aw, kRow - 4.f});
            const float nx = x + mw + 8.f + aw + 24.f;
            const float sw = ui::measureWidth(d_.saveModelB_->text(), gfx::FontId{16}) + 28.f;
            const float nw = std::max(140.f, x + cw - nx - sw - 8.f);
            d_.nameIn_->setBounds({nx, y, nw, kRow - 4.f});
            d_.saveModelB_->setBounds({nx + nw + 8.f, y, sw, kRow - 4.f});
        }
        y += kRow + 8.f;
        messageAt_ = {x, y + 4.f, cw, 20.f};
        // le pied : revenir, utiliser, fermer
        const float by = panel_.bottom() - 46.f;
        float bx = panel_.right() - 20.f;
        for (ui::Button* b : {d_.closeB_, d_.useB_, d_.resetB_}) {
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 34.f);
            bx -= bw;
            b->setBounds({bx, by, bw, 30.f});
            bx -= 8.f;
        }
        footerW_ = bx - x - 10.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect(panel_, c.panelBg, 8.f);
        ctx.r.strokeRect(panel_, c.border, 1.f);
        const float x = panel_.x + 20.f;
        ui::drawIcon(ctx.r, ui::Icon::Columns, {x, panel_.y + 13.f, 20.f, 20.f}, c.accent);
        const std::string title = "Mod\xC3\xA8le de fichier";
        ctx.r.drawText({x + 28.f, panel_.y + 10.f}, title, kTitleFont, c.text);
        {
            const float tx = x + 28.f + ctx.r.measure(title, kTitleFont).width + 14.f;
            ctx.r.drawText({tx, panel_.y + 17.f}, elide(ctx.r, d_.name_, kSmall, panel_.right() - 60.f - tx), kSmall, c.textMuted);
        }
        ctx.r.drawText({headlineAt_.x, headlineAt_.y}, elide(ctx.r, d_.headline(), kText, headlineAt_.w), kText, d_.ready() ? c.info : c.textMuted);
        for (const auto& l : labels_) {
            if (l.heading) {
                ctx.r.drawText(l.at, elide(ctx.r, l.text, kSmall, l.w), kSmall, c.textMuted);
                ctx.r.fillRect({l.at.x, l.at.y + ctx.r.lineHeight(kSmall) + 3.f, l.w, 1.f}, c.border);
            } else {
                ctx.r.drawText(l.at, elide(ctx.r, l.text, kText, l.w), kText, c.text);
            }
        }
        {
            const float pw = panel_.right() - 20.f - previewHeadAt_.x;
            const std::string head = "APER\xC3\x87U \xE2\x80\x94 " + std::to_string(d_.rows_) + (d_.rows_ > 1 ? " lignes lues" : " ligne lue")
                                     + (d_.preview_.ignored ? ", " + std::to_string(d_.preview_.ignored) + " sans cl\xC3\xA9" : std::string{});
            ctx.r.drawText(previewHeadAt_, elide(ctx.r, head, kSmall, pw), kSmall, c.textMuted);
            ctx.r.fillRect({previewHeadAt_.x, previewHeadAt_.y + ctx.r.lineHeight(kSmall) + 3.f, pw, 1.f}, c.border);
        }
        if (!d_.message_.empty())
            ctx.r.drawText({messageAt_.x, messageAt_.y}, elide(ctx.r, d_.message_, kSmall, messageAt_.w), kSmall, d_.messageOk_ ? c.ok : c.error);
        const std::string foot = "Le choix reste avec la pi\xC3\xA8" "ce jointe ; un mod\xC3\xA8le enregistr\xC3\xA9 se voit dans M\xC3\xA9moire et apprentissage "
                                 "\xE2\x80\xBA Apprentissage \xE2\x80\xBA Mod\xC3\xA8les de fichiers. Le mod\xC3\xA8le distant n'est pas r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9.";
        ctx.r.drawText({x, panel_.bottom() - 38.f}, elide(ctx.r, foot, kSmall, footerW_), kSmall, c.textMuted);
    }

private:
    AiFileModelDialog& d_;
    gfx::Rect          panel_{}, headlineAt_{}, messageAt_{};
    gfx::Point         previewHeadAt_{};
    std::vector<Label> labels_;
    float              footerW_{0.f};
};

// ---------------------------------------------------------------------- la fenetre ---
AiFileModelDialog::AiFileModelDialog(Assistant& assistant, std::string attachmentId, std::function<void(const std::string&)> say)
    : menu::WidgetMenu("dialog.ai.modele-fichier"), assistant_(assistant), id_(std::move(attachmentId)), sayOut_(std::move(say)) {
    models_ = sheets::loadModels(true);
    (void)load();
}

AiFileModelDialog::~AiFileModelDialog() = default;

menu::MenuTraits AiFileModelDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

bool AiFileModelDialog::load() {
    const Attachment* a = assistant_.attachmentById(id_);
    if (!a) {
        say("Ce fichier n'est plus dans la discussion.", false);
        return false;
    }
    name_ = a->name;
    recognition_ = sheets::recognitionOf(*a);
    const auto choice = sheets::choiceOf(*a);
    chosenByUser_ = choice && choice->byUser;
    workbook_ = assistant_.workbookOf(id_);
    if (choice) {
        layout_ = choice->layout;
        modelId_ = choice->modelId;
        modelName_ = choice->modelName;
        confidence_ = choice->confidence;
        if (!modelId_.empty()) modelLayout_ = layout_;
    } else if (recognition_) {
        layout_ = recognition_->layout;
        confidence_ = recognition_->confidence;
    }
    if (!workbook_) {
        say(a->analysis == "en-cours" ? std::string("Analyse du classeur en cours\xE2\x80\xA6") : std::string("Lecture du classeur\xE2\x80\xA6"), true);
        return false;
    }
    if (layout_.sheet.empty() || !workbook_->sheet(layout_.sheet)) {
        // ni choix ni reconnaissance (une analyse d'avant) : la detection, ici (une feuille, a la demande)
        const sheets::Detection d = sheets::detect(*workbook_);
        if (const auto* s = d.chosen()) {
            layout_ = s->layout;
            confidence_ = s->confidence;
        }
    }
    message_.clear();
    recompute();
    return true;
}

std::string AiFileModelDialog::headline() const {
    if (!workbook_) return "Lecture du classeur\xE2\x80\xA6";
    if (chosenByUser_)
        return "Choisi pour ce fichier : " + sheets::genreLabel(layout_.genre) + (modelName_.empty() ? std::string{} : " \xE2\x80\x94 mod\xC3\xA8le " + guill(modelName_));
    if (recognition_ && !recognition_->modelName.empty()) return "Mod\xC3\xA8le reconnu : " + recognition_->modelName + " (" + sheets::genreLabel(recognition_->genre) + ")";
    if (recognition_ && recognition_->hasLayout)
        return sheets::genreLabel(recognition_->genre) + " \xE2\x80\x94 d\xC3\xA9tect\xC3\xA9, confiance " + percent(recognition_->confidence);
    return "Autre : ni liste d'E/S, ni r\xC3\xA9seau, ni reports d\xC3\xA9tect\xC3\xA9s \xE2\x80\x94 choisis la correspondance";
}

void AiFileModelDialog::say(std::string text, bool ok) {
    message_ = std::move(text);
    messageOk_ = ok;
}

std::vector<std::string> AiFileModelDialog::mapRoles() const {
    std::vector<std::string> roles = sheets::rolesFor(layout_.genre);
    for (const auto& c : layout_.columns)
        if (std::find(roles.begin(), roles.end(), c.role) == roles.end()) roles.push_back(c.role);
    return roles;
}

std::vector<std::string> AiFileModelDialog::columnChoices() const {
    std::vector<std::string> out{kNone};
    const sheets::Sheet* s = workbook_ ? workbook_->sheet(layout_.sheet) : nullptr;
    if (!s) return out;
    const std::size_t w = std::max(sheets::headerWidthOf(*s, layout_.headerRow), std::min<std::size_t>(s->width(), 60));
    for (std::size_t c = 0; c < w; ++c) {
        const std::string h = sheets::headerText(*s, layout_.headerRow, c);
        out.push_back(sheets::columnLetter(c) + (h.empty() ? std::string{} : " \xE2\x80\x94 " + h));
    }
    return out;
}

void AiFileModelDialog::recompute() {
    if (workbook_) {
        const sheets::Reading all = sheets::read(*workbook_, layout_);
        rows_ = all.rows.size();
        preview_ = all;
        if (preview_.rows.size() > 40) preview_.rows.resize(40);
    } else {
        rows_ = 0;
        preview_ = sheets::Reading{};
    }
    fillWidgets();
}

void AiFileModelDialog::fillWidgets() {
    syncing_ = true;
    if (sheetBox_) {
        std::vector<ui::DropDown::Item> items;
        int sel = -1;
        if (workbook_)
            for (const auto& s : workbook_->sheets) {
                std::string label = s.name + (s.hidden ? " (masqu\xC3\xA9" "e)" : "");
                if (recognition_)
                    for (const auto& d : recognition_->detection.sheets)
                        if (d.sheet == s.name && d.genre != sheets::Genre::Other) label += " \xE2\x80\x94 " + sheets::genreShort(d.genre) + " " + percent(d.confidence);
                if (s.name == layout_.sheet) sel = static_cast<int>(items.size());
                items.push_back({label, s.name});
            }
        sheetBox_->setItems(std::move(items));
        if (sel >= 0) sheetBox_->setSelectedIndex(sel);
    }
    if (genreBox_) {
        int sel = 0;
        for (std::size_t i = 0; i < std::size(sheets::kGenres); ++i)
            if (sheets::kGenres[i] == layout_.genre) sel = static_cast<int>(i);
        genreBox_->setSelectedIndex(sel);
    }
    if (headerIn_) headerIn_->setText(std::to_string(layout_.headerRow + 1));
    if (skipIn_) skipIn_->setText(std::to_string(layout_.rules.skipAfterHeader));
    if (mapModel_) {
        mapModel_->roles = mapRoles();
        mapModel_->reset();
    }
    if (previewTable_ && previewModel_) {
        std::vector<ui::TableView::Column> cols{col("Ligne", 56.f)};
        for (const auto& c : preview_.columns) cols.push_back(col(sheets::roleLabel(c.role).c_str(), 130.f));
        previewTable_->setColumns(std::move(cols));
        previewModel_->reset();
    }
    if (modelBox_) {
        std::vector<ui::DropDown::Item> items{{"(choisir un mod\xC3\xA8le existant)", ""}};
        int sel = 0;
        for (const auto& m : models_) {
            if (m.id == modelId_) sel = static_cast<int>(items.size());
            items.push_back({m.name + " \xE2\x80\x94 " + sheets::genreShort(m.layout.genre) + (m.active ? std::string{} : " (d\xC3\xA9sactiv\xC3\xA9)"), m.id});
        }
        modelBox_->setItems(std::move(items));
        modelBox_->setSelectedIndex(sel);
    }
    const bool on = workbook_ != nullptr;
    for (ui::Button* b : {applyModelB_, saveModelB_, useB_})
        if (b) b->setEnabled(on);
    if (resetB_) resetB_->setEnabled(chosenByUser_);
    syncing_ = false;
    if (auto* body = widgetRoot()) body->invalidateLayout();
}

bool AiFileModelDialog::setSheet(const std::string& name) {
    if (!workbook_) return false;
    const sheets::Sheet* s = workbook_->sheet(name);
    if (!s) return false;
    std::size_t index = 0;
    for (std::size_t i = 0; i < workbook_->sheets.size(); ++i)
        if (&workbook_->sheets[i] == s) index = i;
    const sheets::SheetDetection d = sheets::detectSheet(*s, index);
    const sheets::Genre g = d.genre != sheets::Genre::Other ? d.genre : layout_.genre;
    layout_ = d.genre != sheets::Genre::Other ? d.layout : sheets::layoutAt(*s, d.layout.headerRow, g);
    layout_.sheet = s->name;
    confidence_ = d.confidence;
    say("Feuille " + guill(s->name) + (d.genre != sheets::Genre::Other ? " : " + sheets::genreLabel(d.genre) + ", confiance " + percent(d.confidence) : std::string(" : aucun genre reconnu"))
            + ".",
        true);
    recompute();
    return true;
}

bool AiFileModelDialog::setGenre(sheets::Genre g) {
    layout_.genre = g;
    layout_.rules.keyRoles = sheets::defaultKeyRoles(g);
    recompute();
    return true;
}

bool AiFileModelDialog::setHeaderLine(std::size_t line) {
    if (!workbook_ || line == 0) return false;
    const sheets::Sheet* s = workbook_->sheet(layout_.sheet);
    if (!s || line > s->rows.size()) {
        say("Ligne d'en-t\xC3\xAA" "te hors de la feuille.", false);
        return false;
    }
    const sheets::Rules keep = layout_.rules;
    layout_ = sheets::layoutAt(*s, line - 1, layout_.genre);
    layout_.rules.skipAfterHeader = keep.skipAfterHeader;
    layout_.rules.emptyValues = keep.emptyValues;
    layout_.rules.mergedCells = keep.mergedCells;
    say("En-t\xC3\xAA" "te ligne " + std::to_string(line) + " : " + std::to_string(layout_.columns.size()) + " colonnes reconnues.", true);
    recompute();
    return true;
}

bool AiFileModelDialog::setSkip(std::size_t n) {
    if (n > 50) return false;
    layout_.rules.skipAfterHeader = n;
    recompute();
    return true;
}

bool AiFileModelDialog::setColumn(const std::string& role, int col) {
    if (!sheets::knownRole(role)) return false;
    const sheets::Sheet* s = workbook_ ? workbook_->sheet(layout_.sheet) : nullptr;
    layout_.set(role, col, s && col >= 0 ? sheets::headerText(*s, layout_.headerRow, static_cast<std::size_t>(col)) : std::string{});
    recompute();
    return true;
}

bool AiFileModelDialog::applyModel(const std::string& idOrName) {
    if (!workbook_) return false;
    models_ = sheets::loadModels(true);
    const sheets::FileModel* m = nullptr;
    for (const auto& x : models_)
        if (x.id == idOrName || x.name == idOrName) m = &x;
    if (!m) {
        say("Mod\xC3\xA8le introuvable.", false);
        return false;
    }
    std::vector<sheets::FileModel> one{*m};
    one.front().active = true;
    layout_ = m->layout;
    if (const auto found = sheets::match(*workbook_, one)) {
        layout_.sheet = workbook_->sheets[found->sheet].name;
        layout_.headerRow = found->headerRow;
        say("Mod\xC3\xA8le " + guill(m->name) + " : ses intitul\xC3\xA9s sont ceux de ce fichier (feuille " + guill(layout_.sheet) + ", en-t\xC3\xAA" "te ligne "
                + std::to_string(layout_.headerRow + 1) + ").",
            true);
        confidence_ = std::min(1.0, found->score);
    } else {
        if (!workbook_->sheet(layout_.sheet) && !workbook_->sheets.empty()) layout_.sheet = preview_.sheet.empty() ? workbook_->sheets.front().name : preview_.sheet;
        say("Mod\xC3\xA8le " + guill(m->name) + " appliqu\xC3\xA9, mais ses intitul\xC3\xA9s ne sont pas ceux de ce fichier : v\xC3\xA9rifie la correspondance.", false);
        confidence_ = 0.5;
    }
    modelId_ = m->id;
    modelName_ = m->name;
    modelLayout_ = layout_;
    recompute();
    return true;
}

bool AiFileModelDialog::saveAsModel(const std::string& name) {
    if (!workbook_) return false;
    sheets::FileModel m = sheets::makeModel(*workbook_, layout_, name);
    std::string why;
    if (!sheets::saveModel(m, &why)) {
        say("Pas enregistr\xC3\xA9 : " + why + ".", false);
        return false;
    }
    modelId_ = m.id;
    modelName_ = m.name;
    modelLayout_ = layout_;
    models_ = sheets::loadModels(true);
    (void)use();
    say("Mod\xC3\xA8le " + guill(m.name) + " enregistr\xC3\xA9 dans la m\xC3\xA9moire de l'IA : un classeur aux m\xC3\xAAmes intitul\xC3\xA9s sera lu directement.", true);
    if (sayOut_) sayOut_("Assistant IA : mod\xC3\xA8le de fichier " + guill(m.name) + " enregistr\xC3\xA9 (" + sheets::genreLabel(layout_.genre) + ").");
    return true;
}

bool AiFileModelDialog::use() {
    if (!workbook_) return false;
    sheets::Choice c;
    c.layout = layout_;
    c.byUser = true;
    c.confidence = confidence_ > 0 ? confidence_ : 1.0;
    c.rows = rows_;
    if (!modelId_.empty() && modelLayout_ == layout_) {
        c.modelId = modelId_;
        c.modelName = modelName_;
    }
    std::string why;
    if (!assistant_.setWorkbookChoice(id_, c, &why)) {
        say("Pas gard\xC3\xA9 : " + why + ".", false);
        return false;
    }
    chosenByUser_ = true;
    if (c.modelId.empty()) modelName_.clear();
    say("Gard\xC3\xA9 pour " + guill(name_) + " : " + sheets::genreLabel(layout_.genre) + ", " + std::to_string(rows_) + (rows_ > 1 ? " lignes lues." : " ligne lue."), true);
    fillWidgets();
    return true;
}

bool AiFileModelDialog::reset() {
    std::string why;
    if (!assistant_.clearWorkbookChoice(id_, &why)) {
        say(why, false);
        return false;
    }
    modelId_.clear();
    modelName_.clear();
    (void)load();
    say("Revenu \xC3\xA0 ce que l'analyse a reconnu.", true);
    return true;
}

void AiFileModelDialog::close() {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, {}});
}

core::Status AiFileModelDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<Body>(*this);
    auto* b = body.get();
    const auto button = [&](const char* text, const char* suffix) {
        return &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>(text, base + suffix)));
    };
    const auto input = [&](const char* suffix, const char* placeholder) {
        auto in = std::make_unique<ui::InputText>(base + suffix);
        in->setPlaceholder(placeholder);
        return &static_cast<ui::InputText&>(b->addChild(std::move(in)));
    };
    sheetBox_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".feuille")));
    sheetBox_->setMaxVisibleRows(14);
    genreBox_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".genre")));
    {
        std::vector<ui::DropDown::Item> items;
        for (const auto g : sheets::kGenres) items.push_back({sheets::genreLabel(g), std::string(sheets::genreKey(g))});
        genreBox_->setItems(std::move(items));
    }
    headerIn_ = input(".entete", "3");
    skipIn_ = input(".sauter", "0");
    mapModel_ = std::make_shared<MapModel>(*this);
    {
        auto t = std::make_unique<ui::TableView>(base + ".correspondance");
        t->setSelectionMode(ui::SelectionMode::Single);
        t->setAlternatingRowColors(true);
        t->setColumns({col("R\xC3\xB4le", 150.f), col("Colonne", 230.f)});
        t->setModel(mapModel_);
        mapTable_ = &static_cast<ui::TableView&>(b->addChild(std::move(t)));
    }
    previewModel_ = std::make_shared<PreviewModel>(*this);
    {
        auto t = std::make_unique<ui::TableView>(base + ".apercu");
        t->setSelectionMode(ui::SelectionMode::Single);
        t->setAlternatingRowColors(true);
        t->setColumns({col("Ligne", 56.f)});
        t->setModel(previewModel_);
        previewTable_ = &static_cast<ui::TableView&>(b->addChild(std::move(t)));
    }
    modelBox_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".modeles")));
    modelBox_->setMaxVisibleRows(12);
    applyModelB_ = button("Appliquer ce mod\xC3\xA8le", ".appliquer");
    applyModelB_->setTooltip("Le mod\xC3\xA8le choisi est cherch\xC3\xA9 dans ce classeur (sa feuille, sa ligne d'en-t\xC3\xAA" "te) ; sa correspondance remplace celle-ci.");
    nameIn_ = input(".nom", "le nom du mod\xC3\xA8le (Liste E/S armoire 3)");
    saveModelB_ = button("Enregistrer comme mod\xC3\xA8le", ".enregistrer");
    saveModelB_->setTooltip("Garde ce mod\xC3\xA8le dans la m\xC3\xA9moire de l'IA (avec l'apprentissage .xia) : un classeur aux m\xC3\xAAmes intitul\xC3\xA9s sera lu directement, sans analyse.");
    resetB_ = button("Revenir \xC3\xA0 la d\xC3\xA9tection", ".revenir");
    useB_ = button("Utiliser pour ce fichier", ".utiliser");
    useB_->setStyle(ui::Button::Style::Primary);
    closeB_ = button("Fermer", ".fermer");
    links_ += sheetBox_->selectionChanged->connect([this](int) {
        if (!syncing_ && sheetBox_->selectedItem()) (void)setSheet(sheetBox_->selectedItem()->value);
    });
    links_ += genreBox_->selectionChanged->connect([this](int) {
        if (syncing_ || !genreBox_->selectedItem()) return;
        if (const auto g = sheets::genreFromKey(genreBox_->selectedItem()->value)) (void)setGenre(*g);
    });
    links_ += headerIn_->editingDone->connect([this](const std::string& t) {
        if (syncing_) return;
        const long n = std::strtol(t.c_str(), nullptr, 10);
        if (n <= 0 || !setHeaderLine(static_cast<std::size_t>(n))) fillWidgets();
    });
    links_ += skipIn_->editingDone->connect([this](const std::string& t) {
        if (syncing_) return;
        const long n = std::strtol(t.c_str(), nullptr, 10);
        if (n < 0 || !setSkip(static_cast<std::size_t>(n))) fillWidgets();
    });
    links_ += applyModelB_->clicked->connect([this] {
        const auto* it = modelBox_->selectedItem();
        if (!it || it->value.empty()) {
            say("Choisis d'abord un mod\xC3\xA8le dans la liste.", false);
            return;
        }
        (void)applyModel(it->value);
    });
    links_ += saveModelB_->clicked->connect([this] { (void)saveAsModel(nameIn_->text()); });
    links_ += resetB_->clicked->connect([this] { (void)reset(); });
    links_ += useB_->clicked->connect([this] {
        if (use()) close();
    });
    links_ += closeB_->clicked->connect([this] { close(); });
    setRoot(std::move(body));
    fillWidgets();
    return core::ok();
}

void AiFileModelDialog::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    // le classeur pas encore lu : le panneau (dessous, mis a jour) fait avancer l'Assistant ; on l'attend ici
    if (!workbook_ && !done_ && assistant_.workbookOf(id_)) (void)load();
}

ui::EventResult AiFileModelDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        const bool listOpen = (sheetBox_ && sheetBox_->isOpen()) || (genreBox_ && genreBox_->isOpen()) || (modelBox_ && modelBox_->isOpen())
                              || (mapTable_ && mapTable_->cellEditing());
        if (!listOpen) {
            close();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app::ai
