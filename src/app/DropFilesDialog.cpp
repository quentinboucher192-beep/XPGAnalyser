// =============================================================================
//  app/DropFilesDialog.cpp - lot API 8 : "Que faire de ces fichiers ?"
//  (voir l'en-tete).
// =============================================================================
#include "DropFilesDialog.hpp"
#include "TutorialsLot8.hpp"                // Lot API 8 : didacticiels et aide (F1)

#include "hmi/HmiImages.hpp"
#include "../menu/MenuManager.hpp"
#include "../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace app {

namespace df = dropfiles;

namespace {

constexpr std::size_t npos = static_cast<std::size_t>(-1);
const gfx::FontId kUi{16};
const gfx::FontId kSmall{13};
constexpr float kTitleBar = 36.f;      // la barre de titre du panneau
constexpr float kFooter = 64.f;        // la note et les boutons
constexpr float kPad = 14.f;           // les marges du panneau
constexpr float kScrollbar = 12.f;
constexpr float kThumb = 44.f;         // la vignette d'un fichier

float smallLine() { return ui::lineHeight(kSmall) + 3.f; }

// Un texte coupe a la largeur (les \n comptent) ; un mot trop long reste entier.
std::vector<std::string> wrap(const std::string& text, gfx::FontId font, float width) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(trial, font) > width) {
            out.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else {
            word += c;
        }
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// Une ligne qui ne tient pas finit par "..." (sans couper un caractere UTF-8).
std::string fitted(const std::string& text, gfx::FontId font, float width) {
    if (ui::measureWidth(text, font) <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    std::string s = text;
    while (!s.empty() && ui::measureWidth(s + dots, font) > width) {
        s.pop_back();
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0u) == 0x80u) s.pop_back();
        if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0x80u) != 0u) s.pop_back();
    }
    return s + dots;
}

void boldText(const ui::PaintContext& ctx, gfx::Point at, const std::string& text, gfx::FontId font, gfx::Color color) {
    ctx.r.drawText(at, text, font, color);
    ctx.r.drawText({at.x + 1.f, at.y}, text, font, color);
}

std::string upperExt(const std::string& name) {
    auto ext = std::filesystem::path(name).extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return ext.size() > 5 ? ext.substr(0, 5) : ext;
}

std::string fileLabel(const df::FileRow& f, bool resource) {
    return f.name + (resource ? " : Ressources" : " : Fichiers externes");
}

// ---- Lot API 8 : glisser de fichiers, 2e partie ---- la phrase de chaque case (la maquette).
const char* const kResourceSays = "Ressources : une copie dans le projet, qui voyage avec lui, m\xC3\xAAme si le fichier d'origine dispara\xC3\xAEt.";
const char* const kFileSays = "Fichiers externes : un lien vers le fichier d'origine, pas de copie ; s'il change, l'IHM lit la "
                              "nouvelle version ; s'il est d\xC3\xA9plac\xC3\xA9, le lien casse.";
// ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

} // namespace

// =================================================================== les lignes ==
//  Le contenu qui defile : chaque section, ses lignes, leurs cases. Les
//  hauteurs dependent de la largeur (les details se coupent) : measure() les
//  calcule, onLayout pose les widgets, onPaint ecrit le reste.
class DropFilesContent final : public ui::Widget {
public:
    struct Row {
        enum class Kind : std::uint8_t { Section, Stage, Program, Action, File, Note };
        Kind             kind{Kind::Note};
        std::string      title, text;          // Section : titre et sous-titre ; Stage, Note : texte
        std::size_t      workbook{npos}, index{npos};
        ui::Checkbox*    box{nullptr};
        ui::RadioButton* radio{nullptr};
        ui::DropDown*    list{nullptr};
        ui::Checkbox*    resource{nullptr};
        ui::Checkbox*    file{nullptr};
        float            y{0.f}, h{0.f};
        std::vector<std::string> sub, why, detail;   // coupes a la largeur
    };

    explicit DropFilesContent(DropFilesDialog& d) : ui::Widget("depot.lignes"), d_(d) {}

    void build();
    void setWrapWidth(float w) {
        if (std::abs(w - wrap_) < 0.5f) return;
        wrap_ = w;
        measured_ = -1.f;
        invalidateLayout();
    }
    [[nodiscard]] float heightFor(float w) {
        if (std::abs(w - measured_) >= 0.5f) measure(w);
        return height_;
    }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {wrap_, const_cast<DropFilesContent*>(this)->heightFor(wrap_)};
        h.minimum = h.preferred;
        return h;
    }
    [[nodiscard]] std::vector<Row>& rows() noexcept { return rows_; }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void measure(float w);
    [[nodiscard]] df::Action& action(const Row& r) { return d_.plan_->workbooks[r.workbook].actions[r.index]; }
    [[nodiscard]] const df::Action& action(const Row& r) const { return d_.plan_->workbooks[r.workbook].actions[r.index]; }
    [[nodiscard]] df::FileRow& fileOf(const Row& r) { return d_.plan_->files[r.index]; }
    [[nodiscard]] const df::FileRow& fileOf(const Row& r) const { return d_.plan_->files[r.index]; }

    DropFilesDialog&                 d_;
    std::vector<Row>                 rows_;
    std::shared_ptr<ui::RadioGroup>  group_;
    float                            wrap_{640.f}, measured_{-1.f}, height_{0.f};
    core::ConnectionScope            links_;
};

void DropFilesContent::build() {
    auto& plan = *d_.plan_;
    const auto add = [this](Row r) { rows_.push_back(std::move(r)); };
    const auto checkState = [](bool on) { return on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked; };

    // ---- le .XPG / le .XHW : les choix du lot 7 ----------------------------------
    if (!plan.programOptions.empty()) {
        Row s;
        s.kind = Row::Kind::Section;
        s.title = "Programme et configuration";
        std::vector<std::string> names;
        for (const auto& p : plan.programs) names.push_back(std::filesystem::path(p).filename().string());
        for (const auto& p : plan.hardware) names.push_back(std::filesystem::path(p).filename().string());
        std::string joined;
        for (const auto& n : names) joined += (joined.empty() ? "" : " et ") + n;
        s.text = joined + " \xE2\x80\x94 un choix, comme quand on les d\xC3\xA9pose seuls ; il se fait en dernier.";
        add(std::move(s));
        group_ = std::make_shared<ui::RadioGroup>();
        for (std::size_t i = 0; i < plan.programOptions.size(); ++i) {
            Row r;
            r.kind = Row::Kind::Program;
            r.index = i;
            r.text = plan.programOptions[i].detail;
            r.radio = &static_cast<ui::RadioButton&>(addChild(std::make_unique<ui::RadioButton>(
                plan.programOptions[i].label, group_, static_cast<int>(i), "depot.programme." + std::to_string(i))));
            add(std::move(r));
        }
        group_->setValue(std::clamp(plan.programChoice, 0, static_cast<int>(plan.programOptions.size()) - 1));
        links_ += group_->valueChanged->connect([this](int v) {
            d_.plan_->programChoice = v;
            d_.sync();
        });
    }

    // ---- les classeurs ------------------------------------------------------------
    for (std::size_t w = 0; w < plan.workbooks.size(); ++w) {
        const auto& book = plan.workbooks[w];
        Row s;
        s.kind = Row::Kind::Section;
        s.title = (book.genre == df::Genre::Table ? "Tableau \xC2\xAB " : "Classeur \xC2\xAB ") + book.name + " \xC2\xBB";
        s.text = book.summary();
        s.workbook = w;
        add(std::move(s));
        int stage = -1;
        for (std::size_t i = 0; i < book.actions.size(); ++i) {
            const auto& a = book.actions[i];
            const int st = static_cast<int>(df::stageOf(a.kind));
            if (st != stage) {
                stage = st;
                Row t;
                t.kind = Row::Kind::Stage;
                t.text = df::stageTitle(df::stageOf(a.kind));
                add(std::move(t));
            }
            Row r;
            r.kind = Row::Kind::Action;
            r.workbook = w;
            r.index = i;
            r.text = a.detail;
            auto box = std::make_unique<ui::Checkbox>(a.label, "depot.classeur" + std::to_string(w) + "." + std::to_string(i));
            box->setState(checkState(a.checked && a.possible));
            box->setEnabled(a.possible);
            if (!a.possible) box->setTooltip("Impossible : " + a.why);
            r.box = &static_cast<ui::Checkbox&>(addChild(std::move(box)));
            links_ += r.box->stateChanged->connect([this, w, i](ui::Checkbox::State state) {
                d_.plan_->workbooks[w].actions[i].checked = state == ui::Checkbox::State::Checked;
                d_.sync();
            });
            if (!a.targets.empty()) {
                auto list = std::make_unique<ui::DropDown>("depot.classeur" + std::to_string(w) + "." + std::to_string(i) + ".ou");
                std::vector<ui::DropDown::Item> items;
                for (const auto& t : a.targets) items.push_back({t.label, t.value, {}, true});
                list->setItems(std::move(items));
                list->setSelectedIndex(std::clamp(a.target, 0, static_cast<int>(a.targets.size()) - 1));
                list->setEnabled(a.possible);
                r.list = &static_cast<ui::DropDown&>(addChild(std::move(list)));
                links_ += r.list->selectionChanged->connect([this, w, i](int sel) {
                    d_.plan_->workbooks[w].actions[i].target = sel;
                    d_.sync();
                });
            }
            add(std::move(r));
        }
    }

    // ---- les autres fichiers --------------------------------------------------------
    if (!plan.files.empty()) {
        Row s;
        s.kind = Row::Kind::Section;
        s.title = plan.files.size() > 1 ? "Ajouter ces " + std::to_string(plan.files.size()) + " fichiers au projet"
                                        : std::string("Ajouter ce fichier au projet");
        // Lot API 8 (2e partie) : tout fichier va dans l'un, l'autre ou les deux.
        s.text = "Ressources : une copie dans le projet, qui voyage avec lui (une image, un son, un PDF, un ZIP...). Fichiers externes : "
                 "un lien vers le fichier d'origine, pas de copie. Les deux cases coch\xC3\xA9" "es : les deux.";
        add(std::move(s));
        for (std::size_t i = 0; i < plan.files.size(); ++i) {
            const auto& f = plan.files[i];
            Row r;
            r.kind = Row::Kind::File;
            r.index = i;
            const auto box = [&](bool resource) {
                const bool ok = resource ? f.resourceOk : f.fileOk;
                auto b = std::make_unique<ui::Checkbox>(resource ? "Ressources" : "Fichiers externes",
                                                        "depot.fichier" + std::to_string(i) + (resource ? ".ressources" : ".externes"));
                b->setState(checkState(ok && (resource ? f.resource : f.file)));
                b->setEnabled(ok);
                if (!ok) b->setTooltip("Impossible : " + (resource ? f.resourceWhy : f.fileWhy));
                else b->setTooltip(resource ? kResourceSays : kFileSays);          // lot API 8 : sa phrase
                auto* raw = &static_cast<ui::Checkbox&>(addChild(std::move(b)));
                links_ += raw->stateChanged->connect([this, i, resource](ui::Checkbox::State st) {
                    auto& row = d_.plan_->files[i];
                    (resource ? row.resource : row.file) = st == ui::Checkbox::State::Checked;
                    d_.sync();
                });
                return raw;
            };
            r.resource = box(true);
            r.file = box(false);
            if (f.present()) {
                auto list = std::make_unique<ui::DropDown>("depot.fichier" + std::to_string(i) + ".deja");
                std::vector<ui::DropDown::Item> items;
                for (const auto c : {df::Conflict::Replace, df::Conflict::KeepBoth, df::Conflict::Ignore})
                    items.push_back({df::conflictLabel(c), std::to_string(static_cast<int>(c)), {}, true});
                list->setItems(std::move(items));
                list->setSelectedIndex(static_cast<int>(f.conflict));
                r.list = &static_cast<ui::DropDown&>(addChild(std::move(list)));
                links_ += r.list->selectionChanged->connect([this, i](int sel) {
                    d_.plan_->files[i].conflict = static_cast<df::Conflict>(std::clamp(sel, 0, 2));
                    d_.sync();
                });
            }
            add(std::move(r));
        }
    }

    // ---- ce qui est laisse ------------------------------------------------------------
    if (!plan.ignored.empty()) {
        Row n;
        n.kind = Row::Kind::Note;
        std::string joined;
        for (const auto& s : plan.ignored) joined += (joined.empty() ? "" : " ; ") + s;
        n.text = "Laiss\xC3\xA9" "s de c\xC3\xB4t\xC3\xA9 : " + joined + ".";
        add(std::move(n));
    }
}

void DropFilesContent::measure(float w) {
    measured_ = w;
    const float line = smallLine();
    float y = 0.f;
    bool firstSection = true;
    for (auto& r : rows_) {
        r.sub.clear();
        r.why.clear();
        r.detail.clear();
        r.y = y;
        switch (r.kind) {
            case Row::Kind::Section:
                if (!firstSection) y += 10.f;
                firstSection = false;
                r.y = y;
                r.sub = wrap(r.text, kSmall, w - 24.f);
                r.h = 10.f + 22.f + static_cast<float>(r.sub.size()) * line + 4.f;
                break;
            case Row::Kind::Stage:
                r.h = 26.f;
                break;
            case Row::Kind::Program:
                r.detail = wrap(r.text, kSmall, w - 60.f);
                r.h = 28.f + static_cast<float>(r.detail.size()) * line + 6.f;
                break;
            case Row::Kind::Action: {
                const auto& a = action(r);
                if (!a.possible) r.why = wrap("Impossible : " + a.why, kSmall, w - 60.f);
                r.detail = wrap(a.detail, kSmall, w - 60.f);
                r.h = 28.f + static_cast<float>(r.why.size() + r.detail.size()) * line + (r.list ? 32.f : 0.f) + 8.f;
                break;
            }
            case Row::Kind::File: {
                const auto& f = fileOf(r);
                const float textW = std::max(160.f, w - 70.f - 360.f);
                // Lot API 8 (2e partie) : chaque case a sa phrase ; grisee, sa raison.
                for (auto& l : wrap(f.resourceOk ? std::string(kResourceSays) : "Ressources : " + f.resourceWhy, kSmall, textW))
                    r.why.push_back(std::move(l));
                for (auto& l : wrap(f.fileOk ? std::string(kFileSays) : "Fichiers externes : " + f.fileWhy, kSmall, textW))
                    r.why.push_back(std::move(l));
                if (f.present()) {
                    std::string said = "D\xC3\xA9j\xC3\xA0 l\xC3\xA0 : ";
                    if (!f.resourceExisting.empty())
                        said += "la ressource " + f.resourceExisting + (f.identical ? ", le m\xC3\xAAme contenu" : ", un autre contenu");
                    if (!f.fileExisting.empty())
                        said += std::string(f.resourceExisting.empty() ? "" : " ; ") + "le fichier externe " + f.fileExisting;
                    r.detail = wrap(said, kSmall, textW);
                }
                const float text = 22.f + line + static_cast<float>(r.detail.size()) * line + (r.list ? 32.f : 0.f)
                                 + static_cast<float>(r.why.size()) * line;
                r.h = std::max(kThumb + 8.f, text) + 10.f;
                break;
            }
            case Row::Kind::Note:
                r.detail = wrap(r.text, kSmall, w - 24.f);
                r.h = 12.f + static_cast<float>(r.detail.size()) * line + 6.f;
                break;
        }
        y += r.h;
    }
    height_ = y + 8.f;
}

void DropFilesContent::onLayout() {
    const auto b = bounds();
    if (std::abs(b.w - measured_) >= 0.5f) measure(b.w);
    const float line = smallLine();
    for (auto& r : rows_) {
        const float y = b.y + r.y;
        switch (r.kind) {
            case Row::Kind::Program:
                if (r.radio) r.radio->setBounds({b.x + 16.f, y + 2.f, std::min(b.w - 32.f, ui::measureWidth(r.radio->label(), kUi) + 34.f), 24.f});
                break;
            case Row::Kind::Action: {
                if (r.box) r.box->setBounds({b.x + 16.f, y + 2.f, std::min(b.w - 32.f, ui::measureWidth(r.box->label(), kUi) + 34.f), 24.f});
                if (r.list) {
                    const float ly = y + 28.f + static_cast<float>(r.why.size() + r.detail.size()) * line + 2.f;
                    r.list->setBounds({b.x + 44.f, ly, std::min(460.f, b.w - 60.f), 28.f});
                }
                break;
            }
            case Row::Kind::File: {
                const float right = b.x + b.w - 8.f;
                if (r.file) r.file->setBounds({right - 170.f, y + 6.f, 170.f, 24.f});
                if (r.resource) r.resource->setBounds({right - 170.f - 150.f, y + 6.f, 140.f, 24.f});
                if (r.list) {
                    const float ly = y + 6.f + 22.f + line + static_cast<float>(r.detail.size()) * line + 2.f;
                    r.list->setBounds({b.x + 70.f, ly, 190.f, 28.f});
                }
                break;
            }
            default:
                break;
        }
    }
}

void DropFilesContent::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float line = smallLine();
    ctx.r.fillRect(b, c.panelBg);
    bool firstSection = true;
    for (const auto& r : rows_) {
        const float y = b.y + r.y;
        if (y > ctx.clip.bottom() || y + r.h < ctx.clip.y) continue;      // hors de la vue
        switch (r.kind) {
            case Row::Kind::Section: {
                if (!firstSection) ctx.r.fillRect({b.x + 4.f, y - 6.f, b.w - 8.f, 1.f}, c.border);
                firstSection = false;
                boldText(ctx, {b.x + 12.f, y + 10.f}, fitted(r.title, kUi, b.w - 24.f), kUi, c.text);
                float ty = y + 10.f + 22.f;
                for (const auto& l : r.sub) {
                    ctx.r.drawText({b.x + 12.f, ty}, l, kSmall, c.textMuted);
                    ty += line;
                }
                break;
            }
            case Row::Kind::Stage:
                ctx.r.drawText({b.x + 16.f, y + 7.f}, fitted(r.text, kSmall, b.w - 32.f), kSmall, c.accent);
                ctx.r.fillRect({b.x + 16.f, y + 7.f + ui::lineHeight(kSmall) + 2.f, std::min(b.w - 32.f, ui::measureWidth(r.text, kSmall)), 1.f},
                               c.accent.withAlpha(90));
                break;
            case Row::Kind::Program: {
                float ty = y + 28.f;
                for (const auto& l : r.detail) {
                    ctx.r.drawText({b.x + 44.f, ty}, l, kSmall, c.textMuted);
                    ty += line;
                }
                break;
            }
            case Row::Kind::Action: {
                float ty = y + 28.f;
                for (const auto& l : r.why) {
                    ctx.r.drawText({b.x + 44.f, ty}, l, kSmall, c.warning);
                    ty += line;
                }
                const bool on = r.box && r.box->isChecked();
                for (const auto& l : r.detail) {
                    ctx.r.drawText({b.x + 44.f, ty}, l, kSmall, on ? c.text : c.textMuted);
                    ty += line;
                }
                break;
            }
            case Row::Kind::File: {
                const auto& f = fileOf(r);
                const gfx::Rect thumb{b.x + 16.f, y + 6.f, kThumb, kThumb};
                bool drawn = false;
                if (f.hasPreview) {
                    const auto img = HmiImageCache::instance().resource(ctx.r, f.preview);
                    if (img.valid()) {
                        const float scale = std::min(thumb.w / static_cast<float>(img.width), thumb.h / static_cast<float>(img.height));
                        const float w = static_cast<float>(img.width) * scale, h = static_cast<float>(img.height) * scale;
                        ctx.r.fillRect(thumb, c.inputBg);
                        ctx.r.drawImage(img.tex, {thumb.x + (thumb.w - w) * 0.5f, thumb.y + (thumb.h - h) * 0.5f, w, h});
                        drawn = true;
                    }
                }
                if (!drawn) {
                    ctx.r.fillRoundedRect(thumb, c.inputBg, 4.f);
                    const auto ext = upperExt(f.name);
                    const float tw = ui::measureWidth(ext, kSmall);
                    ctx.r.drawText({thumb.x + (thumb.w - tw) * 0.5f, thumb.y + (thumb.h - ui::lineHeight(kSmall)) * 0.5f}, ext, kSmall, c.textMuted);
                }
                ctx.r.strokeRect(thumb, c.border, 1.f);
                const float tx = b.x + 70.f;
                const float textW = std::max(160.f, b.w - 70.f - 360.f);
                boldText(ctx, {tx, y + 6.f}, fitted(f.name, kUi, textW), kUi, c.text);
                float ty = y + 6.f + 22.f;
                ctx.r.drawText({tx, ty}, fitted(f.kind + " \xC2\xB7 " + hmi::formatBytes(f.bytes), kSmall, textW), kSmall, c.textMuted);
                ty += line;
                for (const auto& l : r.detail) {
                    ctx.r.drawText({tx, ty}, l, kSmall, c.warning);
                    ty += line;
                }
                if (r.list) ty += 32.f;
                for (const auto& l : r.why) {
                    ctx.r.drawText({tx, ty}, l, kSmall, c.textMuted);
                    ty += line;
                }
                break;
            }
            case Row::Kind::Note: {
                float ty = y + 12.f;
                for (const auto& l : r.detail) {
                    ctx.r.drawText({b.x + 12.f, ty}, l, kSmall, c.textMuted);
                    ty += line;
                }
                break;
            }
        }
    }
}

// =================================================================== le cadre ==
//  Le panneau : son titre, la question, les lignes qui defilent, la note de
//  l'ordre et les boutons.
class DropFilesBody final : public ui::Widget {
public:
    explicit DropFilesBody(DropFilesDialog& d) : ui::Widget("depot.corps"), d_(d) {}

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::floor(std::min(1000.f, std::max(480.f, r.w - 40.f)));
        const float innerW = w - 2.f * kPad;
        intro_ = wrap(intro(), kUi, innerW);
        const float introH = static_cast<float>(intro_.size()) * (ui::lineHeight(kUi) + 4.f) + 10.f;
        const float listW = innerW - kScrollbar;
        if (d_.content_) d_.content_->setWrapWidth(listW);
        const float contentH = d_.content_ ? d_.content_->heightFor(listW) : 0.f;
        const float maxH = std::max(260.f, r.h - 40.f);
        const float h = std::floor(std::min(maxH, kTitleBar + 10.f + introH + contentH + 8.f + kFooter));
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float listY = panel_.y + kTitleBar + 10.f + introH;
        const float listH = std::max(60.f, panel_.bottom() - kFooter - 8.f - listY);
        if (d_.scroll_) d_.scroll_->setBounds({panel_.x + kPad, listY, innerW, listH});
        listRect_ = {panel_.x + kPad, listY, innerW, listH};
        float bx = panel_.right() - kPad;
        for (ui::Button* b : {d_.ok_, d_.cancel_}) {
            if (!b) continue;
            const float bw = std::max(110.f, ui::measureWidth(b->text(), kUi) + 36.f);
            bx -= bw;
            b->setBounds({bx, panel_.bottom() - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        noteRight_ = bx - 6.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, kTitleBar}, c.headerBg);
        ctx.r.drawText({panel_.x + 14.f, panel_.y + (kTitleBar - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f},
                       fitted(d_.title(), ctx.theme.font.uiBold, panel_.w - 28.f), ctx.theme.font.uiBold, c.text);
        float y = panel_.y + kTitleBar + 10.f;
        for (const auto& l : intro_) {
            ctx.r.drawText({panel_.x + kPad, y}, l, kUi, c.text);
            y += ui::lineHeight(kUi) + 4.f;
        }
        ctx.r.strokeRect({listRect_.x - 1.f, listRect_.y - 1.f, listRect_.w + 2.f, listRect_.h + 2.f}, c.border, 1.f);
        // L'ordre de Faire, a gauche des boutons (deux lignes au plus).
        const float noteW = std::max(120.f, noteRight_ - panel_.x - kPad);
        float ny = panel_.bottom() - kFooter + 12.f;
        const auto note = wrap(d_.orderNote(), kSmall, noteW);
        for (std::size_t i = 0; i < note.size() && i < 3; ++i) {
            ctx.r.drawText({panel_.x + kPad, ny}, i == 2 && note.size() > 3 ? fitted(note[i] + " \xE2\x80\xA6", kSmall, noteW) : note[i], kSmall,
                           c.textMuted);
            ny += smallLine();
        }
    }

private:
    [[nodiscard]] std::string intro() const {
        const auto& p = *d_.plan_;
        return "Que faire de " + std::string(p.files.size() + p.workbooks.size() + p.programs.size() + p.hardware.size() > 1 ? "ces fichiers" : "ce fichier")
             + (p.projectName.empty() ? std::string{} : " avec le projet ouvert (" + p.projectName + ")")
             + " ? Coche ce qu'il faut faire ; Faire le fait, dans l'ordre de la liste. Rien ne se fait sans Faire.";
    }
    DropFilesDialog&         d_;
    gfx::Rect                panel_{}, listRect_{};
    float                    noteRight_{0.f};
    std::vector<std::string> intro_;
};

// ================================================================= le dialogue ==
DropFilesDialog::DropFilesDialog(std::shared_ptr<df::Plan> plan)
    : menu::WidgetMenu("dialog.dropFiles"), plan_(std::move(plan)) {
    if (!plan_) plan_ = std::make_shared<df::Plan>();
}

DropFilesDialog::~DropFilesDialog() = default;

menu::MenuTraits DropFilesDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    t.closableWithEscape = true;
    return t;
}

std::string DropFilesDialog::title() const {
    std::vector<std::string> names;
    for (const auto& p : plan_->programs) names.push_back(std::filesystem::path(p).filename().string());
    for (const auto& p : plan_->hardware) names.push_back(std::filesystem::path(p).filename().string());
    for (const auto& w : plan_->workbooks) names.push_back(w.name);
    for (const auto& f : plan_->files) names.push_back(f.name);
    std::string joined;
    for (std::size_t i = 0; i < names.size() && i < 3; ++i) joined += (i == 0 ? "" : i + 1 == names.size() ? " et " : ", ") + names[i];
    if (names.size() > 3) joined += " et " + std::to_string(names.size() - 3) + (names.size() - 3 > 1 ? " autres" : " autre");
    return (names.size() > 1 ? "Fichiers d\xC3\xA9pos\xC3\xA9s : " : "Fichier d\xC3\xA9pos\xC3\xA9 : ") + joined;
}

core::Status DropFilesDialog::buildUi() {
    auto body = std::make_unique<DropFilesBody>(*this);
    body_ = body.get();
    auto scroll = std::make_unique<ui::ScrollablePanel>("depot.defilement");
    scroll->setScrollPolicy(false, true);
    auto content = std::make_unique<DropFilesContent>(*this);
    content_ = content.get();
    content_->build();
    scroll->setContent(std::move(content));
    scroll_ = &static_cast<ui::ScrollablePanel&>(body->addChild(std::move(scroll)));
    cancel_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", "depot.annuler")));
    links_ += cancel_->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Faire", "depot.faire")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { finish(true); });
    setRoot(std::move(body));
    sync();
    return core::ok();
}

std::size_t DropFilesDialog::count() const { return plan_->count(); }

std::string DropFilesDialog::orderNote() const {
    const auto& p = *plan_;
    if (count() == 0) return "Rien n'est coch\xC3\xA9 : Faire ne ferait rien.";
    std::size_t imports = 0, adds = 0;
    std::vector<std::string> macros;
    for (const auto& w : p.workbooks)
        for (const auto& a : w.actions) {
            if (!a.checked || !a.possible) continue;
            switch (df::stageOf(a.kind)) {
                case df::Stage::Import: ++imports; break;
                case df::Stage::Add: ++adds; break;
                case df::Stage::Macros:
                    if (a.kind == df::ActionKind::Macro) macros.push_back(a.macro);
                    else ++adds;
                    break;
            }
        }
    for (const auto& f : p.files)
        if (f.wanted()) ++adds;
    std::vector<std::string> parts;
    if (imports) parts.push_back(imports > 1 ? "les imports (un Ctrl+Z chacun)" : "l'import (un Ctrl+Z)");
    // ---- Lot API 8 : glisser de fichiers, 2e partie ---- combien de copies, combien de liens (la maquette).
    std::size_t copies = 0, linksTo = 0;
    for (const auto& f : p.files) {
        if (f.resource && f.resourceOk) ++copies;
        if (f.file && f.fileOk) ++linksTo;
    }
    for (const auto& w : p.workbooks)
        for (const auto& a : w.actions)
            if (a.kind == df::ActionKind::ExternalFile && a.checked && a.possible) ++linksTo;
    std::string what;
    if (copies) what = std::to_string(copies) + (copies > 1 ? " copies" : " copie") + " dans les Ressources";
    if (linksTo) what += (what.empty() ? "" : ", ") + std::to_string(linksTo) + (linksTo > 1 ? " liens" : " lien") + " dans les Fichiers externes";
    if (adds) parts.push_back("les ajouts au projet" + (what.empty() ? std::string{} : " (" + what + ")"));
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
    if (macros.size() == 1) parts.push_back("la macro " + macros.front() + " : son formulaire, jusqu'\xC3\xA0 l'aper\xC3\xA7u");
    else if (macros.size() > 1)
        parts.push_back("les " + std::to_string(macros.size()) + " macros l'une apr\xC3\xA8s l'autre (" + macros.front()
                        + " d'abord ; dans l'onglet Macros, Fermer ou Annuler ouvre la suivante)");
    const bool program = (!p.programs.empty() || !p.hardware.empty()) && p.programChoice >= 0
                      && static_cast<std::size_t>(p.programChoice) < p.programOptions.size()
                      && p.programOptions[static_cast<std::size_t>(p.programChoice)].code >= 0;
    if (program) parts.push_back("enfin le .XPG / .XHW (son r\xC3\xA9" "capitulatif d'abord)");
    std::string s = "Faire : ";
    for (std::size_t i = 0; i < parts.size(); ++i) s += (i == 0 ? "" : i + 1 == parts.size() ? ", puis " : ", ") + parts[i];
    return s + ".";
}

void DropFilesDialog::sync() {
    if (!ok_) return;
    const auto n = count();
    const std::string label = n == 0 ? std::string("Faire") : "Faire (" + std::to_string(n) + ")";
    if (ok_->text() != label) {
        ok_->setText(label);
        root().invalidateLayout();
    }
    ok_->setEnabled(n > 0);
    root().invalidate();
    if (content_) content_->invalidate();
}

void DropFilesDialog::finish(bool ok) {
    if (done_) return;
    if (ok && count() == 0) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, ok ? "faire" : ""});
}

void DropFilesDialog::confirm() { finish(true); }
void DropFilesDialog::cancel() { finish(false); }

// ---- Lot API 8 : glisser de fichiers, 2e partie ----
std::size_t DropFilesDialog::addFiles(const std::vector<std::string>& paths, std::string* why) {
    namespace fs = std::filesystem;
    // Deja dans la liste : le meme chemin (au besoin, une fois resolu).
    const auto same = [](const std::string& a, const std::string& b) {
        if (a == b) return true;
        std::error_code ea, eb;
        const auto ca = fs::weakly_canonical(fs::path(a), ea), cb = fs::weakly_canonical(fs::path(b), eb);
        return !ea && !eb && ca == cb;
    };
    const auto known = [&](const std::string& p) {
        for (const auto& x : plan_->programs) if (same(x, p)) return true;
        for (const auto& x : plan_->hardware) if (same(x, p)) return true;
        for (const auto& w : plan_->workbooks) if (same(w.path, p)) return true;
        for (const auto& f : plan_->files) if (same(f.path, p)) return true;
        return false;
    };
    std::vector<std::string> fresh;
    for (const auto& p : paths)
        if (!p.empty() && !known(p) && std::find(fresh.begin(), fresh.end(), p) == fresh.end()) fresh.push_back(p);
    if (fresh.empty()) {
        if (why) *why = "d\xC3\xA9j\xC3\xA0 dans la liste";
        return 0;
    }
    auto more = df::analyse(fresh, context_);
    std::size_t added = 0;
    for (auto& w : more.workbooks) { plan_->workbooks.push_back(std::move(w)); ++added; }
    for (auto& f : more.files) { plan_->files.push_back(std::move(f)); ++added; }
    for (auto& s : more.ignored) plan_->ignored.push_back(std::move(s));
    // Un .XPG / .XHW de plus : sa question (le lot 7) se pose quand on le depose seul.
    for (const auto* list : {&more.programs, &more.hardware})
        for (const auto& p : *list)
            plan_->ignored.push_back(fs::path(p).filename().string()
                                     + " (un .XPG / .XHW se d\xC3\xA9pose seul, ou avec les autres d\xC3\xA8s le d\xC3\xA9" "but)");
    // Les lignes refaites depuis le plan : les cases, les listes et le choix du
    // .XPG y sont ecrits, rien ne se perd.
    if (content_ && scroll_) {
        auto content = std::make_unique<DropFilesContent>(*this);
        content_ = content.get();
        content_->build();
        scroll_->setContent(std::move(content));
    }
    if (body_) body_->invalidateLayout();
    sync();
    if (!added && why) *why = "rien \xC3\xA0 ajouter (un dossier, un fichier introuvable, un .XPG / .XHW de plus)";
    return added;
}
// ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

ui::EventResult DropFilesDialog::HandleEvent(const ui::InputEvent& ev) {
    // ---- Lot API 8 : glisser de fichiers, 2e partie ---- un fichier lache sur le dialogue : il s'y ajoute.
    if (const auto* drop = std::get_if<ui::FileDropped>(&ev)) {
        if (!done_) (void)addFiles({drop->path});
        return ui::EventResult::Consumed;
    }
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // ---- Lot API 8 : didacticiels et aide ---- F1 : la page "Glisser un fichier" de l'aide.
        if (k->key == ui::Key::F1 && k->mods.none() && !k->repeat) {
            lot8::setPendingHelpAnchor(lot8::helpAnchorFor("glisser"));
            manager().PushMenu("help");
            return ui::EventResult::Consumed;
        }
        // ---- fin Lot API 8 : didacticiels et aide ----
        // Une liste deroulee garde ses touches (Entree y choisit, Echap la ferme).
        bool listOpen = false;
        if (content_)
            for (const auto& r : content_->rows())
                if (r.list && r.list->isOpen()) listOpen = true;
        if (!listOpen && k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (!listOpen && k->key == ui::Key::Return && ok_ && ok_->enabled()) {
            finish(true);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

// ------------------------------------------------------------- les scripts ----
bool DropFilesDialog::check(const std::string& label, bool on, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (!content_) return fail("dialogue pas construit");
    for (auto& r : content_->rows()) {
        using Kind = DropFilesContent::Row::Kind;
        if (r.kind == Kind::Program && r.radio && df::startsFolded(r.radio->label(), label)) {
            if (!on) return fail("un choix du .XPG ne se d\xC3\xA9" "coche pas : choisis-en un autre (\xC2\xAB Ne pas les importer \xC2\xBB)");
            r.radio->select();
            return true;
        }
        if (r.kind == Kind::Action && r.box && df::startsFolded(r.box->label(), label)) {
            const auto& a = plan_->workbooks[r.workbook].actions[r.index];
            if (!a.possible) return fail("gris\xC3\xA9" "e : " + a.why);
            r.box->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            return true;
        }
        if (r.kind == Kind::File) {
            const auto& f = plan_->files[r.index];
            for (const bool resource : {true, false}) {
                ui::Checkbox* box = resource ? r.resource : r.file;
                if (!box || !df::startsFolded(fileLabel(f, resource), label)) continue;
                if (!(resource ? f.resourceOk : f.fileOk)) return fail("gris\xC3\xA9" "e : " + (resource ? f.resourceWhy : f.fileWhy));
                box->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
                return true;
            }
        }
    }
    return fail("aucune ligne ne commence par \xC2\xAB " + label + " \xC2\xBB");
}

bool DropFilesDialog::choose(const std::string& label, const std::string& item, std::string* why) {
    const auto fail = [why](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (!content_) return fail("dialogue pas construit");
    for (auto& r : content_->rows()) {
        using Kind = DropFilesContent::Row::Kind;
        if (!r.list) continue;
        const bool match = (r.kind == Kind::Action && r.box && df::startsFolded(r.box->label(), label))
                        || (r.kind == Kind::File && df::startsFolded(plan_->files[r.index].name, label));
        if (!match) continue;
        const auto& items = r.list->items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (df::startsFolded(items[i].label, item)) {
                r.list->setSelectedIndex(static_cast<int>(i));
                return true;
            }
        std::string have;
        for (const auto& it : items) have += (have.empty() ? "" : ", ") + it.label;
        return fail("pas dans la liste : " + item + " (" + have + ")");
    }
    return fail("aucune ligne \xC3\xA0 liste ne commence par \xC2\xAB " + label + " \xC2\xBB");
}

std::vector<std::string> DropFilesDialog::lines() const {
    std::vector<std::string> out;
    if (!content_) return out;
    for (const auto& r : content_->rows()) {
        using Kind = DropFilesContent::Row::Kind;
        switch (r.kind) {
            case Kind::Section: out.push_back("== " + r.title); break;
            case Kind::Stage: out.push_back("-- " + r.text); break;
            case Kind::Program:
                if (r.radio) out.push_back(std::string(r.radio->selected() ? "(o) " : "( ) ") + r.radio->label());
                break;
            case Kind::Action: {
                const auto& a = plan_->workbooks[r.workbook].actions[r.index];
                std::string s = std::string(!a.possible ? "[-] " : a.checked ? "[x] " : "[ ] ") + a.label;
                if (!a.possible) s += " (gris\xC3\xA9" "e : " + a.why + ")";
                if (r.list && r.list->selectedItem()) s += " -> " + r.list->selectedItem()->label;
                out.push_back(std::move(s));
                break;
            }
            case Kind::File: {
                const auto& f = plan_->files[r.index];
                for (const bool resource : {true, false}) {
                    const bool ok = resource ? f.resourceOk : f.fileOk;
                    const bool on = resource ? f.resource : f.file;
                    out.push_back(std::string(!ok ? "[-] " : on ? "[x] " : "[ ] ") + fileLabel(f, resource)
                                  + (ok ? std::string{} : " (gris\xC3\xA9" "e)"));
                }
                if (r.list && r.list->selectedItem()) out.push_back("    d\xC3\xA9j\xC3\xA0 l\xC3\xA0 : " + r.list->selectedItem()->label);
                break;
            }
            case Kind::Note: out.push_back(r.text); break;
        }
    }
    return out;
}

} // namespace app
