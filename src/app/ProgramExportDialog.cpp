// =============================================================================
//  app/ProgramExportDialog.cpp - 1.8.0 : voir ProgramExportDialog.hpp
// =============================================================================
#include "ProgramExportDialog.hpp"

#include "../menu/MenuManager.hpp"
#include "../ui/Icons.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/PathBrowse.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace app {

namespace {
const gfx::FontId kSmall{13};
constexpr float kW = 800.f;

const char* const kFormatLabels[3] = {"Excel (.xlsx)", "PDF (.pdf)", "Texte (.txt)"};
const char* const kFormatDetails[3] = {
    "Sommaire cliquable, un onglet par groupe, code color\xC3\xA9, pr\xC3\xAAt \xC3\xA0 imprimer",
    "A4, couverture, sommaire pagin\xC3\xA9 et signets, code color\xC3\xA9",
    "S'ouvre partout (Bloc-notes) : 100 colonnes, UTF-8"};
const char* const kContentLabels[7] = {"Guide de lecture et sommaire", "Ce que chaque section lit et \xC3\xA9" "crit",
                                       "Num\xC3\xA9ros de ligne", "Le r\xC3\xB4le des sections (tes ic\xC3\xB4nes)",
                                       "Param\xC3\xA8tres des unit\xC3\xA9s de programme", "Annexe : code des blocs DFB",
                                       "Annexe : variables globales"};

std::vector<std::string> wrapText(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
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
    for (const char c : s) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}
} // namespace

class ExportBody final : public ui::Widget {
public:
    explicit ExportBody(ProgramExportDialog& d) : ui::Widget("programExport.body"), d_(d) {}
    ui::Widget* browse{nullptr};
    ui::Button* cancel{nullptr};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float x = 0, w = kW - 32.f;
        // La hauteur : calculee en posant tout.
        float y = 44.f;
        intro_ = wrapText("Pour qu'une autre personne comprenne le programme sans Control Expert : les sections dans l'ordre o\xC3\xB9 l'automate les "
                          "ex\xC3\xA9" "cute, ce que chacune lit et \xC3\xA9" "crit, un guide de lecture.",
                          kSmall, w);
        y += static_cast<float>(intro_.size()) * 18.f + 10.f;
        labels_.clear();
        const auto label = [&](const std::string& t) {
            labels_.push_back({t, y});
            y += 22.f;
        };
        label("QUOI");
        std::vector<gfx::Rect> radioRects;
        details_.clear();
        for (std::size_t i = 0; i < d_.radios_.size(); ++i) {
            radioRects.push_back({x, y, w, 26.f});
            y += 26.f;
            const auto lines = wrapText(d_.spec_.scopes[i].detail, kSmall, w - 30.f);
            for (const auto& l : lines) {
                details_.push_back({l, {x + 28.f, y}});
                y += 17.f;
            }
            y += 4.f;
        }
        y += 6.f;
        label("FORMATS  (plusieurs \xC3\xA0 la fois)");
        const float fw = (w - 20.f) / 3.f;
        std::array<gfx::Rect, 3> formatRects{};
        float fy = y;
        for (int i = 0; i < 3; ++i) {
            formatRects[static_cast<std::size_t>(i)] = {x + static_cast<float>(i) * (fw + 10.f), y, fw, 26.f};
            const auto lines = wrapText(kFormatDetails[i], kSmall, fw - 30.f);
            float ly = y + 26.f;
            for (const auto& l : lines) {
                details_.push_back({l, {x + static_cast<float>(i) * (fw + 10.f) + 28.f, ly}});
                ly += 17.f;
            }
            fy = std::max(fy, ly);
        }
        y = fy + 10.f;
        label("CONTENU");
        std::array<gfx::Rect, 7> contentRects{};
        const float cw = (w - 20.f) / 2.f;
        for (int i = 0; i < 7; ++i) {
            const int col = i % 2, row = i / 2;
            const float cy = y + static_cast<float>(row) * 44.f;
            contentRects[static_cast<std::size_t>(i)] = {x + static_cast<float>(col) * (cw + 20.f), cy, cw, 26.f};
            const std::string detail = i == 5 ? d_.spec_.dfbDetail : i == 6 ? d_.spec_.variablesDetail : i == 1 ? std::string("et les blocs qu'elle appelle") : std::string{};
            if (!detail.empty()) details_.push_back({detail, {x + static_cast<float>(col) * (cw + 20.f) + 28.f, cy + 26.f}});
        }
        y += 4.f * 44.f + 4.f;
        label("ENREGISTRER DANS");
        const gfx::Rect fieldRect{x, y, w - 44.f, 28.f};
        y += 34.f;
        filesAt_ = y;
        y += 20.f;
        const gfx::Rect openRect{x, y, 300.f, 26.f};
        y += 34.f;
        const float h = y + 50.f;
        panel_ = {std::floor((r.w - kW) * 0.5f), std::floor(std::max(10.f, (r.h - h) * 0.5f)), kW, std::min(h, r.h - 20.f)};
        const float ox = panel_.x + 16.f, oy = panel_.y;
        const auto at = [&](gfx::Rect q) { return gfx::Rect{q.x + ox, q.y + oy, q.w, q.h}; };
        for (std::size_t i = 0; i < d_.radios_.size(); ++i) d_.radios_[i]->setBounds(at(radioRects[i]));
        for (std::size_t i = 0; i < 3; ++i) d_.formats_[i]->setBounds(at(formatRects[i]));
        for (std::size_t i = 0; i < 7; ++i) d_.content_[i]->setBounds(at(contentRects[i]));
        d_.folder_->setBounds(at(fieldRect));
        if (browse) browse->setBounds({at(fieldRect).right() + 8.f, at(fieldRect).y, 36.f, 28.f});
        d_.open_->setBounds(at(openRect));
        float bx = panel_.x + panel_.w - 16.f;
        for (auto* b : {static_cast<ui::Widget*>(d_.ok_), static_cast<ui::Widget*>(cancel)}) {
            if (!b) continue;
            const auto* btn = static_cast<ui::Button*>(b);
            const float bw = std::max(110.f, ui::measureWidth(btn->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            b->setBounds({bx, panel_.y + panel_.h - 42.f, bw, 30.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = ctx.theme.metric.headerHeight + 6.f;
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        ui::drawIcon(ctx.r, ui::Icon::Export, {panel_.x + 12.f, panel_.y + (titleH - 16.f) * 0.5f, 16.f, 16.f}, c.text);
        ctx.r.drawText({panel_.x + 36.f, panel_.y + (titleH - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, "Exporter le programme lisible",
                       ctx.theme.font.uiBold, c.text);
        const float ox = panel_.x + 16.f, oy = panel_.y;
        float y = oy + 44.f;
        for (const auto& l : intro_) {
            ctx.r.drawText({ox, y}, l, kSmall, c.textMuted);
            y += 18.f;
        }
        for (const auto& [t, ly] : labels_) ctx.r.drawText({ox, oy + ly + 2.f}, t, kSmall, c.textMuted);
        for (const auto& [t, p] : details_) ctx.r.drawText({ox + p.x, oy + p.y}, t, kSmall, c.textMuted);
        // Les fichiers qui vont etre ecrits.
        std::string files;
        const int s = d_.group_ ? d_.group_->value() : 0;
        const std::string stem = s >= 0 && static_cast<std::size_t>(s) < d_.spec_.scopes.size() ? d_.spec_.scopes[static_cast<std::size_t>(s)].stem : std::string("Programme");
        const char* ext[3] = {".xlsx", ".pdf", ".txt"};
        for (int i = 0; i < 3; ++i)
            if (d_.formats_[static_cast<std::size_t>(i)]->isChecked()) files += (files.empty() ? "" : "   \xC2\xB7   ") + stem + ext[i];
        if (files.empty()) files = "Coche au moins un format.";
        ctx.r.drawText({ox, oy + filesAt_}, files, kSmall, d_.formatCount() ? c.text : c.warning);
        const std::string note = "En t\xC3\xA2" "che de fond : tu continues \xC3\xA0 travailler pendant l'export.";
        ctx.r.drawText({ox, panel_.y + panel_.h - 36.f}, note, kSmall, c.textMuted);
    }

private:
    ProgramExportDialog& d_;
    gfx::Rect panel_{};
    std::vector<std::string> intro_;
    std::vector<std::pair<std::string, float>> labels_;
    std::vector<std::pair<std::string, gfx::Point>> details_;
    float filesAt_{0};
};

ProgramExportDialog::ProgramExportDialog(Spec spec) : menu::WidgetMenu("dialog.programExport"), spec_(std::move(spec)) {}

menu::MenuTraits ProgramExportDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status ProgramExportDialog::buildUi() {
    auto body = std::make_unique<ExportBody>(*this);
    auto* b = body.get();
    const std::string base = "programExport";
    group_ = std::make_shared<ui::RadioGroup>();
    for (std::size_t i = 0; i < spec_.scopes.size(); ++i)
        radios_.push_back(&static_cast<ui::RadioButton&>(
            b->addChild(std::make_unique<ui::RadioButton>(spec_.scopes[i].label, group_, static_cast<int>(i), base + ".quoi" + std::to_string(i)))));
    group_->setValue(std::clamp(spec_.scope, 0, std::max(0, static_cast<int>(spec_.scopes.size()) - 1)));
    links_ += group_->valueChanged->connect([this](int) { sync(); });
    for (std::size_t i = 0; i < 3; ++i) {
        formats_[i] = &static_cast<ui::Checkbox&>(b->addChild(std::make_unique<ui::Checkbox>(kFormatLabels[i], base + ".format" + std::to_string(i))));
        formats_[i]->setState(spec_.formats[i] ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        links_ += formats_[i]->stateChanged->connect([this](ui::Checkbox::State) { sync(); });
    }
    for (std::size_t i = 0; i < 7; ++i) {
        content_[i] = &static_cast<ui::Checkbox&>(b->addChild(std::make_unique<ui::Checkbox>(kContentLabels[i], base + ".contenu" + std::to_string(i))));
        content_[i]->setState(spec_.content[i] ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    }
    auto field = std::make_unique<ui::InputText>(base + ".dossier");
    field->setText(spec_.folder);
    field->setPlaceholder("Le dossier o\xC3\xB9 \xC3\xA9" "crire les fichiers");
    folder_ = &static_cast<ui::InputText&>(b->addChild(std::move(field)));
    links_ += folder_->textChanged->connect([this](const std::string&) { sync(); });
    auto browse = std::make_unique<ui::BrowseButton>(*folder_, ui::chooseFolder(spec_.folder, "Dossier de l'export"), base + ".parcourir");
    browse->setFieldLabel("Enregistrer dans");
    b->browse = &b->addChild(std::move(browse));
    open_ = &static_cast<ui::Checkbox&>(b->addChild(std::make_unique<ui::Checkbox>("Ouvrir le dossier \xC3\xA0 la fin", base + ".ouvrir")));
    open_->setState(spec_.openFolder ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    b->cancel = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    links_ += b->cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Exporter", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { finish(true); });
    setRoot(std::move(body));
    sync();
    return core::ok();
}

int ProgramExportDialog::formatCount() const {
    int n = 0;
    for (const auto* f : formats_) n += f && f->isChecked() ? 1 : 0;
    return n;
}

void ProgramExportDialog::sync() {
    if (!ok_) return;
    const int n = formatCount();
    const std::string label = n == 0 ? std::string("Exporter") : "Exporter " + std::to_string(n) + (n > 1 ? " fichiers" : " fichier");
    if (label != ok_->text()) {
        ok_->setText(label);
        root().invalidateLayout();
    }
    ok_->setEnabled(n > 0 && folder_ && !folder_->text().empty());
    root().invalidate();
}

void ProgramExportDialog::Update(const menu::FrameContext& f) { menu::WidgetMenu::Update(f); }

ui::EventResult ProgramExportDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

std::string ProgramExportDialog::payload() const {
    std::string f, c;
    for (const auto* x : formats_) f += x && x->isChecked() ? '1' : '0';
    for (const auto* x : content_) c += x && x->isChecked() ? '1' : '0';
    return "scope=" + std::to_string(group_ ? group_->value() : 0) + "\nformats=" + f + "\ncontent=" + c + "\nopen=" + (open_ && open_->isChecked() ? "1" : "0")
         + "\nfolder=" + (folder_ ? folder_->text() : std::string{});
}

ProgramExportDialog::Answer ProgramExportDialog::parse(const std::string& payload) {
    Answer a;
    std::istringstream in(payload);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "scope") a.scope = std::atoi(value.c_str());
        else if (key == "formats") for (std::size_t i = 0; i < 3 && i < value.size(); ++i) a.formats[i] = value[i] == '1';
        else if (key == "content") for (std::size_t i = 0; i < 7 && i < value.size(); ++i) a.content[i] = value[i] == '1';
        else if (key == "open") a.openFolder = value == "1";
        else if (key == "folder") a.folder = value;
    }
    return a;
}

void ProgramExportDialog::setFormat(int i, bool on) {
    if (i >= 0 && i < 3 && formats_[static_cast<std::size_t>(i)]) formats_[static_cast<std::size_t>(i)]->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
}

void ProgramExportDialog::setScope(int i) {
    if (group_) group_->setValue(i);
}

void ProgramExportDialog::setFolder(const std::string& folder) {
    if (folder_) folder_->setText(folder);
}

void ProgramExportDialog::finish(bool ok) {
    if (done_) return;
    if (ok && (formatCount() == 0 || !folder_ || folder_->text().empty())) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload()});
}

} // namespace app
