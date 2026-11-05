// =============================================================================
//  app/hmi/HmiReportLayoutPane.cpp - 1.25.0 : la mise en page des rapports (voir HmiReportLayoutPane.hpp)
// =============================================================================
#include "HmiReportLayoutPane.hpp"

#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "../../hmi/HmiRuntime.hpp"     // wallEpoch, wallStamp
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app {

using hmi::Id;
using hmi::kNoId;
using namespace hmikit;
namespace rp = hmi::report;
using PG = ui::PropertyGrid;

namespace {

gfx::Color rgbOf(std::uint32_t c) {
    return gfx::Color{static_cast<std::uint8_t>((c >> 16) & 0xFF), static_cast<std::uint8_t>((c >> 8) & 0xFF), static_cast<std::uint8_t>(c & 0xFF), 255};
}

std::string numberText(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string s = b;
    if (s.size() > 2 && s.substr(s.size() - 2) == ".0") s.resize(s.size() - 2);
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

bool parseReal(std::string text, double& out) {
    for (auto& c : text)
        if (c == ',') c = '.';
    text = trimmed(text);
    while (!text.empty() && (std::isalpha(static_cast<unsigned char>(text.back())) || text.back() == ' ')) text.pop_back();   // "12 mm", "9 pt"
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

const char* const kPortrait = "Portrait";
const char* const kLandscape = "Paysage";
const char* const kNoLogo = "(aucun)";
const char* const kLeft = "\xC3\x80 gauche";
const char* const kRight = "\xC3\x80 droite";

// Les parties : une case a cocher, son nom, ce qu'elle est ; on les glisse pour l'ordre.
class PartsModel final : public ui::ITableModel {
public:
    PartsModel(std::vector<hmi::ReportLayoutPart> parts, bool locked) : parts_(std::move(parts)), locked_(locked) {}
    [[nodiscard]] std::size_t rowCount() const override { return parts_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "Partie" : "Quoi"; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= parts_.size()) return {};
        const auto* info = rp::partInfo(parts_[r].key);
        if (!info) return parts_[r].key;
        if (c == 0) return std::string(info->label);
        if (parts_[r].key == "courbes") return "PDF seulement";
        return info->table ? std::string("tableau") : parts_[r].key == "synthese" ? std::string("les chiffres") : std::string("graphique");
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= parts_.size()) return st;
        if (c == 0) {
            st.check = parts_[r].on ? 1 : 0;
            if (!parts_[r].on) st.fgTone = ui::Tone::Muted;
            if (const auto* info = rp::partInfo(parts_[r].key)) st.tip = std::string(info->hint);
        } else {
            st.fgTone = ui::Tone::Muted;
        }
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }   // l'ordre est le leur
    [[nodiscard]] bool canDropRows(const std::vector<ui::RowIndex>& from, ui::RowIndex, ui::TreeView::DropWhere where) const override {
        return !locked_ && from.size() == 1 && where != ui::TreeView::DropWhere::Into;
    }

private:
    std::vector<hmi::ReportLayoutPart> parts_;
    bool                               locked_;
};

} // namespace

// =================================================================================================== l'apercu ===
HmiReportPreview::HmiReportPreview(std::string id) : ui::Widget(std::move(id)) {}

void HmiReportPreview::setDocument(std::shared_ptr<const hmi::report::PageDoc> doc) {
    doc_ = std::move(doc);
    if (doc_ && page_ >= doc_->pages.size()) page_ = doc_->pages.empty() ? 0 : doc_->pages.size() - 1;
    invalidate();
}

void HmiReportPreview::setPage(std::size_t page) {
    if (!doc_ || doc_->pages.empty()) return;
    page_ = std::min(page, doc_->pages.size() - 1);
    scrollY_ = 0;
    invalidate();
}

void HmiReportPreview::setZoomPercent(int percent) {
    zoom_ = percent <= 0 ? 0 : std::clamp(percent, 25, 300);
    scrollY_ = 0;
    invalidate();
}

double HmiReportPreview::scaleFor(const gfx::Rect& area) const {
    if (!doc_) return 1.0;
    const double w = std::max(40.0, static_cast<double>(area.w) - 32.0), h = std::max(40.0, static_cast<double>(area.h) - 32.0);
    if (thumbnail_) return std::min(w / doc_->width, h / doc_->height);
    if (zoom_ > 0) return 96.0 / 72.0 * zoom_ / 100.0;
    return w / doc_->width;
}

int HmiReportPreview::shownZoom() const {
    if (zoom_ > 0) return zoom_;
    return static_cast<int>(std::lround(scaleFor(bounds()) / (96.0 / 72.0) * 100.0));
}

std::vector<std::string> HmiReportPreview::shownTexts() const {
    std::vector<std::string> out;
    if (!doc_ || page_ >= doc_->pages.size()) return out;
    for (const auto& o : doc_->pages[page_])
        if (o.kind == rp::DrawOp::Kind::Text) out.push_back(rp::fromWinAnsi(o.text));
    return out;
}

void HmiReportPreview::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, thumbnail_ ? c.panelBg : c.inputBg);
    if (!doc_ || doc_->pages.empty()) {
        pageRect_ = {};
        ctx.r.drawText({b.x + 16, b.y + 16}, "Pas d'aper\xC3\xA7u.", ctx.theme.font.ui, c.textMuted);
        return;
    }
    const double s = scaleFor(b);
    const float pw = static_cast<float>(doc_->width * s), ph = static_cast<float>(doc_->height * s);
    const float maxScroll = std::max(0.f, ph + 32.f - b.h);
    scrollY_ = std::clamp(scrollY_, 0.f, maxScroll);
    const float left = b.x + std::max(16.f, (b.w - pw) * 0.5f);
    const float top = b.y + (thumbnail_ ? std::max(8.f, (b.h - ph) * 0.5f) : 16.f) - scrollY_;
    pageRect_ = {left, top, pw, ph};
    ctx.r.pushClip(b);
    ctx.r.fillRect({left + 3, top + 3, pw, ph}, gfx::Color{0, 0, 0, 60});
    ctx.r.fillRect(pageRect_, gfx::Color{255, 255, 255, 255});
    ctx.r.strokeRect(pageRect_, gfx::Color{180, 186, 196, 255}, 1.f);
    // Le logo : une texture par image du document (refaite quand le document change).
    if (texturesFor_ != doc_.get()) {
        for (const auto t : textures_) ctx.r.releaseImage(t);
        textures_.clear();
        for (const auto& img : doc_->images) textures_.push_back(ctx.r.createImage(img.pixels.data(), img.width, img.height));
        texturesFor_ = doc_.get();
    }
    const auto X = [&](double x) { return static_cast<float>(left + x * s); };
    const auto Y = [&](double y) { return static_cast<float>(top + (doc_->height - y) * s); };
    for (const auto& o : doc_->pages[page_]) {
        switch (o.kind) {
            case rp::DrawOp::Kind::Text: {
                const int px = static_cast<int>(std::lround(o.size * s));
                if (px < 3) {   // trop petit pour se lire : un trait gris a sa place
                    const float w = static_cast<float>(rp::textWidth(doc_->font, o.text, o.size, o.bold) * s);
                    ctx.r.fillRect({X(o.x), Y(o.y) - 1.f, std::max(1.f, w), 1.f}, rgbOf(o.color).withAlpha(120));
                    break;
                }
                const gfx::FontId f{static_cast<std::uint16_t>(std::clamp(px, 5, 72))};
                const std::string text = rp::fromWinAnsi(o.text);
                const float lh = ctx.r.lineHeight(f);
                const gfx::Point at{X(o.x), Y(o.y) - lh * 0.8f};
                ctx.r.drawText(at, text, f, rgbOf(o.color));
                if (o.bold) ctx.r.drawText({at.x + 0.6f, at.y}, text, f, rgbOf(o.color));
                break;
            }
            case rp::DrawOp::Kind::Fill:
                ctx.r.fillRect({X(o.x), Y(o.y + o.h), static_cast<float>(o.w * s), static_cast<float>(o.h * s)}, rgbOf(o.color));
                break;
            case rp::DrawOp::Kind::Stroke:
                ctx.r.strokeRect({X(o.x), Y(o.y + o.h), static_cast<float>(o.w * s), static_cast<float>(o.h * s)}, rgbOf(o.color),
                                 std::max(1.f, static_cast<float>(o.size * s)));
                break;
            case rp::DrawOp::Kind::Line:
            case rp::DrawOp::Kind::Polyline:
                for (std::size_t i = 1; i < o.points.size(); ++i)
                    ctx.r.line({X(o.points[i - 1].first), Y(o.points[i - 1].second)}, {X(o.points[i].first), Y(o.points[i].second)}, rgbOf(o.color),
                               std::max(1.f, static_cast<float>(o.size * s)));
                break;
            case rp::DrawOp::Kind::Image:
                if (o.image >= 0 && static_cast<std::size_t>(o.image) < textures_.size() && textures_[static_cast<std::size_t>(o.image)].v != 0)
                    ctx.r.drawImage(textures_[static_cast<std::size_t>(o.image)], {X(o.x), Y(o.y + o.h), static_cast<float>(o.w * s), static_cast<float>(o.h * s)});
                else
                    ctx.r.strokeRect({X(o.x), Y(o.y + o.h), static_cast<float>(o.w * s), static_cast<float>(o.h * s)}, gfx::Color{160, 160, 160, 255}, 1.f);
                break;
        }
    }
    ctx.r.popClip();
}

ui::EventResult HmiReportPreview::onEvent(const ui::InputEvent& ev) {
    if (thumbnail_) return ui::EventResult::Ignored;
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
        scrollY_ -= w->dy * 48.f;
        if (scrollY_ < 0) scrollY_ = 0;
        invalidate();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ============================================================================================ l'onglet Mise en page ===
HmiReportLayoutPane::HmiReportLayoutPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    // ---- la liste
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActNew, HmiGlyph::Plus, "Nouvelle mise en page : une copie de Standard, dans le projet", "Nouvelle");
    tools->add(ActDuplicate, HmiGlyph::Duplicate, "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 : une copie d'une fournie ou d'une du projet, \xC3\xA0 r\xC3\xA9gler",
               "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6");
    tools->add(ActDefault, HmiGlyph::Star, "Par d\xC3\xA9" "faut : celle d'un nouveau rapport (et d'un rapport sans la sienne)", "Par d\xC3\xA9" "faut");
    tools->add(ActRemove, HmiGlyph::Delete, "Supprimer la mise en page choisie (Suppr) - les rapports qui l'emploient prennent celle par d\xC3\xA9" "faut ; "
                                            "une fournie se garde", "Supprimer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(ActRemove, [this] {
        for (const auto& n : chosenNames())
            if (!rp::isBuiltIn(n)) return true;
        return false;
    });
    tools_->setLabelWhen(ActRemove, [this] { return several::label("Supprimer", chosenNames().size()); });
    tools_->setEnabledWhen(ActDefault, [this] { return chosenNames().size() <= 1 && rp::defaultLayoutName(doc_->project) != selected_; });
    tools_->setEnabledWhen(ActDuplicate, [this] { return chosenNames().size() <= 1; });
    auto list = std::make_unique<ui::TableView>(base + ".list");
    list->setColumns({{"Mise en page", 150.f}, {"Page", 82.f}});
    list->setSelectionMode(ui::SelectionMode::Extended);
    list_ = &static_cast<ui::TableView&>(addChild(std::move(list)));
    tools_->bindListKeys(*list_, {ActRemove, ActDuplicate, ActNew});
    // ---- les reglages, puis les parties
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".settings")));
    auto parts = std::make_unique<ui::TableView>(base + ".parts");
    parts->setColumns({{"Partie", 220.f}, {"Quoi", 90.f}});
    parts->setSelectionMode(ui::SelectionMode::Single);
    parts->setRowDragEnabled(true);
    parts_ = &static_cast<ui::TableView&>(addChild(std::move(parts)));
    // ---- l'apercu
    auto pt = std::make_unique<HmiToolStrip>(base + ".previewTools");
    pt->add(ActSample, HmiGlyph::Template, "L'aper\xC3\xA7u avec des donn\xC3\xA9" "es d'exemple (une journ\xC3\xA9" "e invent\xC3\xA9" "e)", "Donn\xC3\xA9" "es d'exemple");
    pt->add(ActLast, HmiGlyph::History, "L'aper\xC3\xA7u avec la derni\xC3\xA8re p\xC3\xA9riode r\xC3\xA9" "elle (l'historique du projet) d'un rapport qui l'emploie",
            "Derni\xC3\xA8re p\xC3\xA9riode");
    pt->separator();
    pt->add(ActPrev, HmiGlyph::Up, "La page d'avant", "\xE2\x97\x80");
    pt->add(ActPage, HmiGlyph::None, "La page montr\xC3\xA9" "e", "Page 1 / 1");
    pt->add(ActNext, HmiGlyph::Down, "La page d'apr\xC3\xA8s", "\xE2\x96\xB6");
    pt->separator();
    pt->add(ActZoomOut, HmiGlyph::ZoomOut, "Plus petit", {});
    pt->add(ActZoomShown, HmiGlyph::ZoomFit, "\xC3\x80 la largeur", "100 %");
    pt->add(ActZoomIn, HmiGlyph::ZoomIn, "Plus grand", {});
    previewTools_ = &static_cast<HmiToolStrip&>(addChild(std::move(pt)));
    previewTools_->setCheckedWhen(ActSample, [this] { return sample_; });
    previewTools_->setCheckedWhen(ActLast, [this] { return !sample_; });
    previewTools_->setEnabledWhen(ActPrev, [this] { return preview_->page() > 0; });
    previewTools_->setEnabledWhen(ActNext, [this] { return preview_->page() + 1 < preview_->pageCount(); });
    previewTools_->setEnabledWhen(ActPage, [] { return false; });
    previewTools_->setLabelWhen(ActPage, [this] {
        return "Page " + std::to_string(std::min(preview_->page() + 1, std::max<std::size_t>(1, preview_->pageCount()))) + " / "
             + std::to_string(std::max<std::size_t>(1, preview_->pageCount()));
    });
    previewTools_->setLabelWhen(ActZoomShown, [this] { return std::to_string(preview_->shownZoom()) + " %"; });
    preview_ = &static_cast<HmiReportPreview&>(addChild(std::make_unique<HmiReportPreview>(base + ".preview")));
    // ---- le bas
    auto bottom = std::make_unique<HmiToolStrip>(base + ".bottom");
    bottom->add(ActCancel, HmiGlyph::Undo, "Annuler les modifications : revenir \xC3\xA0 la mise en page enregistr\xC3\xA9" "e", "Annuler les modifications");
    bottom->add(ActSave, HmiGlyph::Check, "Enregistrer la mise en page dans le projet (Ctrl+Z ensuite la rend comme avant)", "Enregistrer la mise en page");
    bottom_ = &static_cast<HmiToolStrip&>(addChild(std::move(bottom)));
    bottom_->setEnabledWhen(ActCancel, [this] { return modified(); });
    bottom_->setEnabledWhen(ActSave, [this] { return modified(); });
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".menu")));

    const auto run = [this](int a) {
        switch (a) {
            case ActNew: (void)newLayout(); break;
            case ActDuplicate: {
                const gfx::Rect r = tools_->rectOf(ActDuplicate);
                const gfx::Point at = r.w > 0 ? gfx::Point{r.x, r.y + r.h} : gfx::Point{bounds().x + 20, bounds().y + 40};
                menu_->setItems(duplicateItems());
                menu_->setBounds(bounds());
                gfx::Size surface = ui::surfaceSize();
                if (surface.w <= 0.f || surface.h <= 0.f) surface = {bounds().right() + 400.f, bounds().bottom() + 400.f};
                menu_->openAt(at, surface);
                break;
            }
            case ActRemove: (void)removeChosen(); break;
            case ActDefault: (void)setDefaultLayout(selected_); break;
            case ActSample: setPreviewSample(true); break;
            case ActLast: setPreviewSample(false); break;
            case ActPrev: preview_->setPage(preview_->page() > 0 ? preview_->page() - 1 : 0); break;
            case ActNext: preview_->setPage(preview_->page() + 1); break;
            case ActZoomOut: preview_->setZoomPercent(std::max(25, (preview_->shownZoom() / 10) * 10 - 10)); break;
            case ActZoomShown: preview_->setZoomPercent(0); break;
            case ActZoomIn: preview_->setZoomPercent(std::min(300, (preview_->shownZoom() / 10) * 10 + 10)); break;
            case ActCancel: cancel(); break;
            case ActSave: (void)save(); break;
            default: break;
        }
        invalidate();
    };
    links_ += tools_->triggered->connect(run);
    links_ += previewTools_->triggered->connect(run);
    links_ += bottom_->triggered->connect(run);
    links_ += menu_->itemChosen->connect([this](int i) {
        const auto names = listedLayouts();
        if (i >= 1000 && static_cast<std::size_t>(i - 1000) < names.size()) (void)duplicateLayout(names[static_cast<std::size_t>(i - 1000)]);
    });
    links_ += list_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (refreshing_ || rows.empty()) return;
        const auto r = rows.front();
        if (r < lines_.size() && !lines_[r].header && lines_[r].name != selected_) {
            selected_ = lines_[r].name;
            rebuildSettings();
            rebuildParts();
            updatePreview();
        }
    });
    links_ += grid_->propertyRejected->connect([this](const std::string& name, const std::string& value) {
        if (message_.empty()) say(name + " : \xC2\xAB " + value + " \xC2\xBB refus\xC3\xA9", true);
    });
    links_ += parts_->checkClicked->connect([this](ui::RowIndex r) {
        if (r < partKeys_.size()) {
            for (const auto& part : rp::partsOf(current()))
                if (part.key == partKeys_[r]) (void)setPart(part.key, !part.on);
        }
    });
    links_ += parts_->rowsDropped->connect([this](const std::vector<ui::RowIndex>& from, ui::RowIndex to, ui::TreeView::DropWhere where) {
        if (from.size() != 1 || from.front() >= partKeys_.size()) return;
        std::size_t rank = to == ui::TableView::kNoRow ? partKeys_.size() - 1 : static_cast<std::size_t>(to);
        if (where == ui::TreeView::DropWhere::After) ++rank;
        if (rank > from.front()) --rank;
        (void)movePart(partKeys_[from.front()], std::min(rank, partKeys_.size() - 1));
    });
    const auto keys = parts_->keyHandler();
    parts_->setKeyHandler([this, keys](const ui::KeyDown& k) {
        // Alt+Haut, Alt+Bas : monter, descendre la partie choisie ; Espace : la cocher.
        const auto rows = parts_->selectedModelRows();
        if (!rows.empty() && rows.front() < partKeys_.size()) {
            const auto r = static_cast<std::size_t>(rows.front());
            if (k.mods.alt && !k.mods.ctrl && (k.key == ui::Key::Up || k.key == ui::Key::Down)) {
                const std::size_t to = k.key == ui::Key::Up ? (r > 0 ? r - 1 : 0) : std::min(r + 1, partKeys_.size() - 1);
                if (to != r && movePart(partKeys_[r], to)) parts_->selectModelRows({static_cast<ui::RowIndex>(to)});
                return true;
            }
            if (k.key == ui::Key::Space && !k.mods.ctrl && !k.mods.alt && !k.mods.shift) {
                for (const auto& part : rp::partsOf(current()))
                    if (part.key == partKeys_[r]) (void)setPart(part.key, !part.on);
                return true;
            }
        }
        return keys ? keys(k) : false;
    });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

// ---------------------------------------------------------------------------------------------------- l'etat ---
void HmiReportLayoutPane::say(std::string text, bool error) {
    if (error) ui::PropertyGrid::refuse(text);
    message_ = std::move(text);
    invalidate();
}

bool HmiReportLayoutPane::fail(std::string text, std::string* why) {
    say(text, true);
    if (why) *why = std::move(text);
    return false;
}

bool HmiReportLayoutPane::change(const std::string& label, const std::function<void(hmi::Project&)>& fn) {
    auto cmd = hmi::changeProject(doc_, label, fn);
    if (!cmd) return false;
    apply_(std::move(cmd));
    refresh();
    layoutsChanged->emit();
    return true;
}

const hmi::ReportLayout* HmiReportLayoutPane::stored(const std::string& name) const { return rp::findLayout(doc_->project, name); }

std::string HmiReportLayoutPane::selectedLayout() const { return selected_; }

std::vector<std::string> HmiReportLayoutPane::listedLayouts() const {
    std::vector<std::string> out;
    for (const auto& l : rp::builtInLayouts()) out.push_back(l.name);
    for (const auto& l : doc_->project.reportLayouts) out.push_back(l.name);
    return out;
}

std::vector<std::string> HmiReportLayoutPane::chosenNames() const {
    std::vector<std::string> out;
    auto rows = list_->selectedModelRows();
    std::sort(rows.begin(), rows.end());
    for (const auto r : rows)
        if (r < lines_.size() && !lines_[r].header) out.push_back(lines_[r].name);
    return out;
}

bool HmiReportLayoutPane::locked() const { return rp::isBuiltIn(selected_) || !doc_->project.reportLayoutByName(selected_); }

hmi::ReportLayout HmiReportLayoutPane::current() const {
    const auto* s = stored(selected_);
    if (!s) return *rp::builtInLayout(rp::kStandard);
    if (const auto it = drafts_.find(s->id); s->id != kNoId && it != drafts_.end()) return it->second.layout;
    return *s;
}

bool HmiReportLayoutPane::modified(const std::string& name) const {
    const auto* s = doc_->project.reportLayoutByName(name);
    if (!s) return false;
    const auto it = drafts_.find(s->id);
    return it != drafts_.end() && !(it->second.layout == *s);
}

bool HmiReportLayoutPane::modified() const { return modified(selected_); }

std::string HmiReportLayoutPane::statusText() const {
    const auto users = rp::usersOf(doc_->project, selected_);
    const std::string used = users.empty() ? std::string("aucun rapport ne l'emploie")
                                           : "utilis\xC3\xA9" "e par " + std::to_string(users.size()) + (users.size() > 1 ? " rapports" : " rapport");
    if (locked()) return selected_ + " : fournie, verrouill\xC3\xA9" "e (\xC2\xAB Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 \xC2\xBB en fait une \xC3\xA0 toi) \xC2\xB7 " + used;
    if (modified())
        return selected_ + " : modifi\xC3\xA9" "e (non enregistr\xC3\xA9" "e) \xC2\xB7 " + used
             + " \xC2\xB7 les rapports qui l'emploient la prendront \xC3\xA0 leur prochaine \xC3\xA9" "criture";
    return selected_ + " : enregistr\xC3\xA9" "e \xC2\xB7 " + used;
}

// ------------------------------------------------------------------------------------------------ la liste ---
void HmiReportLayoutPane::refresh() {
    // Les brouillons d'une mise en page partie (supprimee, Ctrl+Z) partent avec elle.
    for (auto it = drafts_.begin(); it != drafts_.end();) {
        bool alive = false;
        for (const auto& l : doc_->project.reportLayouts) alive = alive || l.id == it->first;
        it = alive ? std::next(it) : drafts_.erase(it);
    }
    if (!stored(selected_)) selected_ = std::string(rp::kStandard);
    else selected_ = stored(selected_)->name;   // la casse enregistree
    rebuildList();
    rebuildSettings();
    rebuildParts();
    updatePreview();
}

void HmiReportLayoutPane::rebuildList() {
    refreshing_ = true;
    const auto& p = doc_->project;
    const std::string def = rp::defaultLayoutName(p);
    lines_.clear();
    lines_.push_back({true, "FOURNIES \xE2\x80\x94 verrouill\xC3\xA9" "es", {}, true});
    for (const auto& l : rp::builtInLayouts()) lines_.push_back({false, l.name, l.name, true});
    lines_.push_back({true, "DU PROJET", {}, false});
    for (const auto& l : p.reportLayouts) lines_.push_back({false, l.name, l.name, false});
    std::vector<std::vector<std::string>> rows;
    std::vector<int> kinds;   // 0 en-tete, 1 fournie, 2 du projet
    std::vector<bool> isDefault, dirty;
    for (const auto& l : lines_) {
        if (l.header) {
            const std::size_t n = l.builtIn ? rp::builtInLayouts().size() : p.reportLayouts.size();
            rows.push_back({l.text + "  (" + std::to_string(n) + ")",
                            !l.builtIn && n == 0 ? std::string("Dupliquer \xC3\xA0 partir de\xE2\x80\xA6") : std::string{}});
            kinds.push_back(0);
            isDefault.push_back(false);
            dirty.push_back(false);
            continue;
        }
        const auto* s = stored(l.name);
        const bool d = same(l.name, def), m = modified(l.name);
        std::string page = s ? rp::pageText(*s) : std::string{};
        if (s) {
            const auto parts = rp::partsOf(*s);
            const auto on = std::count_if(parts.begin(), parts.end(), [](const hmi::ReportLayoutPart& x) { return x.on; });
            if (on <= 2) page = "synth\xC3\xA8se";
        }
        rows.push_back({l.name + (d ? "  \xE2\x98\x85" : "") + (m ? "  \xE2\x97\x8F" : ""), page});
        kinds.push_back(l.builtIn ? 1 : 2);
        isDefault.push_back(d);
        dirty.push_back(m);
    }
    listModel_ = std::make_shared<Rows>(std::vector<std::string>{"Mise en page", "Page"}, std::move(rows), [kinds, isDefault, dirty](ui::RowIndex r, std::size_t c) {
        ui::CellStyle st;
        if (r >= kinds.size()) return st;
        if (kinds[r] == 0) {
            st.bold = c == 0;
            st.fgTone = ui::Tone::Muted;
            return st;
        }
        if (c == 0) {
            st.indent = 10.f;
            st.icon = kinds[r] == 1 ? ui::Icon::Lock : ui::Icon::User;
            st.iconTone = kinds[r] == 1 ? ui::Tone::Muted : ui::Tone::Accent;
            if (isDefault[r]) st.bold = true;
            if (dirty[r]) st.fgTone = ui::Tone::Warning;
        } else {
            st.fgTone = ui::Tone::Muted;
        }
        return st;
    });
    list_->setModel(listModel_);
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (!lines_[i].header && same(lines_[i].name, selected_)) list_->selectModelRows({static_cast<ui::RowIndex>(i)});
    refreshing_ = false;
}

bool HmiReportLayoutPane::selectLayout(const std::string& name) {
    const auto* s = stored(name);
    if (!s) return false;
    selected_ = s->name;
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (!lines_[i].header && same(lines_[i].name, selected_)) list_->selectModelRows({static_cast<ui::RowIndex>(i)});
    rebuildSettings();
    rebuildParts();
    updatePreview();
    return true;
}

std::vector<ui::PopupMenu::Item> HmiReportLayoutPane::duplicateItems() const {
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6";
    items.push_back(head);
    const auto names = listedLayouts();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i == rp::builtInLayouts().size()) {
            ui::PopupMenu::Item sep;
            sep.separator = true;
            items.push_back(sep);
        }
        ui::PopupMenu::Item it;
        it.label = names[i];
        it.icon = i < rp::builtInLayouts().size() ? ui::Icon::Lock : ui::Icon::User;
        if (const auto* s = stored(names[i])) it.shortcut = rp::pageText(*s);
        it.id = 1000 + static_cast<int>(i);
        items.push_back(std::move(it));
    }
    return items;
}

bool HmiReportLayoutPane::newLayout(std::string* why) {
    std::string made;
    if (!change("Nouvelle mise en page", [&](hmi::Project& p) {
            auto l = rp::duplicate(p, *rp::builtInLayout(rp::kStandard), "Mise_en_page");
            made = l.name;
            p.reportLayouts.push_back(std::move(l));
        }))
        return fail("non cr\xC3\xA9\xC3\xA9" "e", why);
    (void)selectLayout(made);
    say(made + " : une copie de Standard - r\xC3\xA8gle-la \xC3\xA0 droite, l'aper\xC3\xA7u suit ; Enregistrer la garde.");
    return true;
}

bool HmiReportLayoutPane::duplicateLayout(const std::string& from, std::string* why) {
    // Le brouillon de la source compte (ce qu'on voit) : on duplique ce qu'on a regle.
    const auto* s = stored(from);
    if (!s) return fail("pas de mise en page \xC2\xAB " + from + " \xC2\xBB", why);
    hmi::ReportLayout source = *s;
    if (const auto it = drafts_.find(s->id); s->id != kNoId && it != drafts_.end()) source = it->second.layout;
    std::string made;
    if (!change("Dupliquer la mise en page " + s->name, [&](hmi::Project& p) {
            auto l = rp::duplicate(p, source, rp::isBuiltIn(source.name) ? source.name + "_projet" : source.name + " (copie)");
            made = l.name;
            p.reportLayouts.push_back(std::move(l));
        }))
        return fail("non dupliqu\xC3\xA9" "e", why);
    (void)selectLayout(made);
    say(made + " : la copie de " + s->name + ", \xC3\xA0 toi (Ctrl+Z la retire).");
    return true;
}

bool HmiReportLayoutPane::removeLayout(const std::string& name, std::string* why) {
    if (rp::isBuiltIn(name)) return fail(name + " est fournie : elle se garde (Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 en fait une \xC3\xA0 toi)", why);
    const auto* s = doc_->project.reportLayoutByName(name);
    if (!s) return fail("pas de mise en page \xC2\xAB " + name + " \xC2\xBB", why);
    const std::string real = s->name;
    std::size_t moved = 0;
    if (!change("Supprimer la mise en page " + real, [&](hmi::Project& p) {
            moved = rp::forget(p, real);
            std::erase_if(p.reportLayouts, [&](const hmi::ReportLayout& l) { return same(l.name, real); });
        }))
        return fail("non supprim\xC3\xA9" "e", why);
    say(real + " supprim\xC3\xA9" "e" + (moved ? " - " + std::to_string(moved) + (moved > 1 ? " rapports prennent" : " rapport prend") + " celle par d\xC3\xA9" "faut" : std::string{})
        + " (Ctrl+Z la rend).");
    return true;
}

bool HmiReportLayoutPane::removeChosen() {
    auto names = chosenNames();
    if (names.empty()) names.push_back(selected_);
    if (names.size() == 1) {
        const auto users = rp::usersOf(doc_->project, names.front());
        if (rp::isBuiltIn(names.front()) || users.empty()) return removeLayout(names.front());
    }
    std::vector<std::string> users;
    for (const auto& n : names)
        for (const auto& u : rp::usersOf(doc_->project, n))
            if (!rp::isBuiltIn(n)) users.push_back(u + " (" + n + ")");
    several::Batch b;
    b.noun = "mise en page";
    b.nouns = "mises en page";
    b.feminine = true;
    b.ask = !users.empty();
    if (!users.empty()) b.warning = "Les rapports qui les emploient prendront celle par d\xC3\xA9" "faut : " + several::names(users, 3) + ".";
    b.names = names;
    b.remove = [this, names](std::size_t i, std::string* why) { return removeLayout(names[i], why); };
    b.report = [this](const std::string& text, bool warning) { say(text, warning); };
    b.alive = layoutsChanged;
    several::run(std::move(b));
    return true;
}

bool HmiReportLayoutPane::setDefaultLayout(const std::string& name, std::string* why) {
    const auto* s = stored(name);
    if (!s) return fail("pas de mise en page \xC2\xAB " + name + " \xC2\xBB", why);
    const std::string real = s->name;
    if (same(rp::defaultLayoutName(doc_->project), real)) return true;
    if (!change("Mise en page par d\xC3\xA9" "faut : " + real, [&](hmi::Project& p) { p.reportLayoutDefault = same(real, rp::kStandard) ? std::string{} : real; }))
        return fail("non chang\xC3\xA9" "e", why);
    say(real + " : la mise en page par d\xC3\xA9" "faut (celle d'un nouveau rapport, et d'un rapport sans la sienne).");
    return true;
}

// -------------------------------------------------------------------------------------------- le brouillon ---
bool HmiReportLayoutPane::editDraft(const std::function<void(hmi::ReportLayout&)>& fn, std::string* why) {
    if (locked()) return fail(selected_ + " est fournie, verrouill\xC3\xA9" "e : \xC2\xAB Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 \xC2\xBB en fait une \xC3\xA0 toi", why);
    const auto* s = doc_->project.reportLayoutByName(selected_);
    if (!s) return fail("pas de mise en page choisie", why);
    auto& d = drafts_[s->id];
    if (d.undo.empty() && d.redo.empty() && !(d.layout.id == s->id)) d.layout = *s;
    hmi::ReportLayout next = d.layout;
    fn(next);
    if (next == d.layout) return true;
    // Un reglage qui ajoute un probleme est refuse (le brouillon reste lisible et enregistrable).
    const auto before = rp::problems(doc_->project, d.layout), after = rp::problems(doc_->project, next);
    for (const auto& pb : after)
        if (std::find(before.begin(), before.end(), pb) == before.end()) return fail(pb, why);
    d.undo.push_back(d.layout);
    d.redo.clear();
    d.layout = std::move(next);
    message_.clear();
    rebuildList();
    rebuildSettings();
    rebuildParts();
    updatePreview();
    return true;
}

bool HmiReportLayoutPane::setField(const std::string& key, const std::string& raw, std::string* why) {
    const std::string v = trimmed(raw);
    const auto real = [&](double lo, double hi, const char* what, double& out) {
        double x = 0;
        if (!parseReal(v, x)) return fail(std::string(what) + " : un nombre", why);
        if (x < lo || x > hi) return fail(std::string(what) + " : de " + numberText(lo) + " \xC3\xA0 " + numberText(hi), why);
        out = x;
        return true;
    };
    double num = 0;
    if (key == "marge_haut" || key == "marge_bas" || key == "marge_gauche" || key == "marge_droite") {
        if (!real(3, 60, "la marge (mm)", num)) return false;
        return editDraft([&](hmi::ReportLayout& l) {
            (key == "marge_haut" ? l.marginTop : key == "marge_bas" ? l.marginBottom : key == "marge_gauche" ? l.marginLeft : l.marginRight) = num;
        }, why);
    }
    if (key == "taille_tableaux") {
        if (!real(5, 14, "la taille des tableaux (pt)", num)) return false;
        return editDraft([&](hmi::ReportLayout& l) { l.tableSize = num; }, why);
    }
    if (key == "taille_titre") {
        if (!real(8, 32, "le titre (pt)", num)) return false;
        return editDraft([&](hmi::ReportLayout& l) { l.titleSize = num; }, why);
    }
    if (key == "taille_corps") {
        if (!real(6, 16, "le corps (pt)", num)) return false;
        return editDraft([&](hmi::ReportLayout& l) { l.bodySize = num; }, why);
    }
    if (key == "format") {
        const hmi::report::PaperInfo* paper = nullptr;
        for (const auto& pi : rp::papers())
            if (same(pi.key, v) || same(pi.label, v) || lower(v).rfind(lower(std::string(pi.key)) + " ", 0) == 0) paper = &pi;
        if (!paper) return fail("format : A4, A3, A5 ou Lettre", why);
        return editDraft([&](hmi::ReportLayout& l) { l.paper = std::string(paper->key); }, why);
    }
    if (key == "orientation") {
        const std::string l = lower(v);
        if (l != "portrait" && l != "paysage") return fail("orientation : Portrait ou Paysage", why);
        return editDraft([&](hmi::ReportLayout& x) { x.landscape = l == "paysage"; }, why);
    }
    if (key == "logo") {
        const std::string name = v == kNoLogo ? std::string{} : v;
        return editDraft([&](hmi::ReportLayout& l) { l.logo = name; }, why);
    }
    if (key == "logo_place") {
        const std::string l = lower(v);
        const bool right = l.find("droite") != std::string::npos;
        if (!right && l.find("gauche") == std::string::npos) return fail("le logo : \xC3\xA0 gauche ou \xC3\xA0 droite", why);
        return editDraft([&](hmi::ReportLayout& x) { x.logoRight = right; }, why);
    }
    if (key == "police") return editDraft([&](hmi::ReportLayout& l) { l.font = v; }, why);
    if (key == "accent") return editDraft([&](hmi::ReportLayout& l) { l.accent = v; }, why);
    if (key == "visa" || key == "lignes_alternees" || key == "entete_repete") {
        const bool on = yes(v);
        return editDraft([&](hmi::ReportLayout& l) { (key == "visa" ? l.visa : key == "lignes_alternees" ? l.zebra : l.repeatHeader) = on; }, why);
    }
    if (key == "nom") return editDraft([&](hmi::ReportLayout& l) { l.name = v; }, why);
    if (key == "titre") return editDraft([&](hmi::ReportLayout& l) { l.title = v; }, why);
    if (key == "sous_titre") return editDraft([&](hmi::ReportLayout& l) { l.subtitle = v; }, why);
    if (key == "pied_gauche") return editDraft([&](hmi::ReportLayout& l) { l.footLeft = v; }, why);
    if (key == "pied_centre") return editDraft([&](hmi::ReportLayout& l) { l.footCenter = v; }, why);
    if (key == "pied_droite") return editDraft([&](hmi::ReportLayout& l) { l.footRight = v; }, why);
    if (key == "visas") return editDraft([&](hmi::ReportLayout& l) { l.visaLabels = v; }, why);
    return fail("r\xC3\xA9glage inconnu : " + key, why);
}

bool HmiReportLayoutPane::setPart(const std::string& key, bool on, std::string* why) {
    if (!rp::partInfo(key)) return fail("partie inconnue : " + key, why);
    return editDraft([&](hmi::ReportLayout& l) {
        l.parts = rp::partsOf(l);
        for (auto& part : l.parts)
            if (part.key == key) part.on = on;
    }, why);
}

bool HmiReportLayoutPane::movePart(const std::string& key, std::size_t rank, std::string* why) {
    if (!rp::partInfo(key)) return fail("partie inconnue : " + key, why);
    return editDraft([&](hmi::ReportLayout& l) {
        l.parts = rp::partsOf(l);
        const auto it = std::find_if(l.parts.begin(), l.parts.end(), [&](const hmi::ReportLayoutPart& x) { return x.key == key; });
        if (it == l.parts.end()) return;
        const auto part = *it;
        l.parts.erase(it);
        l.parts.insert(l.parts.begin() + static_cast<std::ptrdiff_t>(std::min(rank, l.parts.size())), part);
    }, why);
}

bool HmiReportLayoutPane::undoDraft() {
    const auto* s = doc_->project.reportLayoutByName(selected_);
    if (!s) return false;
    const auto it = drafts_.find(s->id);
    if (it == drafts_.end() || it->second.undo.empty()) return false;
    it->second.redo.push_back(it->second.layout);
    it->second.layout = it->second.undo.back();
    it->second.undo.pop_back();
    say("D\xC3\xA9" "fait (le brouillon de " + selected_ + ").");
    rebuildList();
    rebuildSettings();
    rebuildParts();
    updatePreview();
    return true;
}

bool HmiReportLayoutPane::redoDraft() {
    const auto* s = doc_->project.reportLayoutByName(selected_);
    if (!s) return false;
    const auto it = drafts_.find(s->id);
    if (it == drafts_.end() || it->second.redo.empty()) return false;
    it->second.undo.push_back(it->second.layout);
    it->second.layout = it->second.redo.back();
    it->second.redo.pop_back();
    rebuildList();
    rebuildSettings();
    rebuildParts();
    updatePreview();
    return true;
}

bool HmiReportLayoutPane::save(std::string* why) {
    const auto* s = doc_->project.reportLayoutByName(selected_);
    if (!s) return fail(selected_ + " est fournie : rien \xC3\xA0 enregistrer", why);
    const auto it = drafts_.find(s->id);
    if (it == drafts_.end() || it->second.layout == *s) return true;
    const hmi::ReportLayout next = it->second.layout;
    if (const auto pb = rp::problems(doc_->project, next); !pb.empty()) return fail("Pas enregistr\xC3\xA9" "e : " + pb.front(), why);
    const std::string oldName = s->name;
    const Id id = s->id;
    std::size_t followed = 0;
    drafts_.erase(it);
    selected_ = next.name;
    if (!change("Enregistrer la mise en page " + next.name, [&](hmi::Project& p) {
            for (auto& l : p.reportLayouts)
                if (l.id == id) l = next;
            if (oldName != next.name) followed = rp::renameReferences(p, oldName, next.name);
        }))
        return fail("non enregistr\xC3\xA9" "e", why);
    const auto users = rp::usersOf(doc_->project, next.name);
    say(next.name + " enregistr\xC3\xA9" "e" + (users.empty() ? std::string{} : " - " + std::to_string(users.size()) + (users.size() > 1 ? " rapports la prendront" : " rapport la prendra")
                                                + " \xC3\xA0 leur prochaine \xC3\xA9" "criture")
        + (followed ? " (renomm\xC3\xA9" "e : les rapports suivent)" : std::string{}) + ".");
    return true;
}

void HmiReportLayoutPane::cancel() {
    const auto* s = doc_->project.reportLayoutByName(selected_);
    if (!s) return;
    drafts_.erase(s->id);
    say(selected_ + " : les modifications sont annul\xC3\xA9" "es (l'enregistr\xC3\xA9" "e revient).");
    rebuildList();
    rebuildSettings();
    rebuildParts();
    updatePreview();
}

// ------------------------------------------------------------------------------------------- les reglages ---
void HmiReportLayoutPane::rebuildSettings() {
    const auto l = current();
    const bool ro = locked();
    const auto field = [this](const char* key) {
        return [this, key](std::string_view v) {
            message_.clear();
            return setField(key, std::string(v));
        };
    };
    const auto P = [&](std::string name, std::string value, PG::ValueType type, const char* key, std::string help, std::vector<std::string> choices = {}) {
        auto pr = prop(std::move(name), std::move(value), type, ro ? std::function<bool(std::string_view)>{} : field(key), std::move(help), std::move(choices));
        return pr;
    };
    const std::string holes = " Les trous : {Titre} {Rapport} {Nom} {Periode} {Site} {Debut} {Fin} {Date} {Heure} {Page} {Pages}.";
    std::vector<PG::Category> cats;
    {
        PG::Category c;
        c.name = ro ? "Mise en page fournie (verrouill\xC3\xA9" "e)" : "Mise en page";
        c.properties.push_back(P("Nom", l.name, PG::ValueType::Text, "nom", "Le nom que choisissent les rapports. Renomm\xC3\xA9" "e : les rapports suivent."));
        const auto users = rp::usersOf(doc_->project, l.name);
        c.properties.push_back(prop("Utilis\xC3\xA9" "e par", users.empty() ? std::string("aucun rapport") : joinList(users, ", "), PG::ValueType::ReadOnly));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "Page";
        std::vector<std::string> papers;
        std::string paper;
        for (const auto& pi : rp::papers()) {
            papers.emplace_back(pi.label);
            if (same(pi.key, l.paper)) paper = std::string(pi.label);
        }
        c.properties.push_back(P("Format", paper, PG::ValueType::Enum, "format", "A4, A3, A5 ou Lettre US.", papers));
        c.properties.push_back(P("Orientation", l.landscape ? kLandscape : kPortrait, PG::ValueType::Enum, "orientation", "Paysage : les tableaux larges tiennent mieux.",
                                 {kPortrait, kLandscape}));
        c.properties.push_back(P("Marge haute (mm)", numberText(l.marginTop), PG::ValueType::Real, "marge_haut", "De 3 \xC3\xA0 60 mm."));
        c.properties.push_back(P("Marge basse (mm)", numberText(l.marginBottom), PG::ValueType::Real, "marge_bas", "De 3 \xC3\xA0 60 mm : le pied de page s'y \xC3\xA9" "crit."));
        c.properties.push_back(P("Marge gauche (mm)", numberText(l.marginLeft), PG::ValueType::Real, "marge_gauche", "De 3 \xC3\xA0 60 mm."));
        c.properties.push_back(P("Marge droite (mm)", numberText(l.marginRight), PG::ValueType::Real, "marge_droite", "De 3 \xC3\xA0 60 mm."));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "En-t\xC3\xAAte";
        std::vector<std::string> images{kNoLogo};
        for (const auto& r : doc_->project.assets.resources)
            if (r.kind() == hmi::MediaKind::Image) images.push_back(r.name);
        c.properties.push_back(P("Logo", l.logo.empty() ? std::string(kNoLogo) : l.logo, PG::ValueType::Enum, "logo",
                                 "Une image des ressources (IHM > Ressources) : en haut de chaque page, en grand sur la premi\xC3\xA8re.", images));
        c.properties.push_back(P("Place du logo", l.logoRight ? kRight : kLeft, PG::ValueType::Enum, "logo_place", {}, {kLeft, kRight}));
        c.properties.push_back(P("Titre", l.title, PG::ValueType::Text, "titre", "{Titre} : le titre du rapport (son Titre, sinon \xC2\xAB Rapport journalier \xE2\x80\x94 le projet \xC2\xBB)." + holes));
        c.properties.push_back(P("Sous-titre", l.subtitle, PG::ValueType::Text, "sous_titre", "{SousTitre} : la p\xC3\xA9riode, le rapport, quand il est \xC3\xA9" "crit." + holes));
        c.properties.push_back(P("Couleur d'accent", l.accent, PG::ValueType::Color, "accent",
                                 "Le titre, les intertitres, le trait sous l'en-t\xC3\xAAte, les courbes : #RRGGBB, ou @Nom (une couleur s\xC3\xA9mantique)."));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "Pied de page";
        c.properties.push_back(P("\xC3\x80 gauche", l.footLeft, PG::ValueType::Text, "pied_gauche", "En bas de chaque page." + holes));
        c.properties.push_back(P("Au centre", l.footCenter, PG::ValueType::Text, "pied_centre", "Page {Page}/{Pages}." + holes));
        c.properties.push_back(P("\xC3\x80 droite", l.footRight, PG::ValueType::Text, "pied_droite", "\xC3\x89" "crit le {Date} \xC3\xA0 {Heure}." + holes));
        c.properties.push_back(P("Cases de visa", tf(l.visa), PG::ValueType::Boolean, "visa", "Des cases \xC3\xA0 signer, en fin de rapport."));
        c.properties.push_back(P("Libell\xC3\xA9s des cases", l.visaLabels, PG::ValueType::Text, "visas", "S\xC3\xA9par\xC3\xA9s par ; : \xC3\x89tabli par; V\xC3\xA9rifi\xC3\xA9 par."));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "Tableaux";
        c.expanded = false;
        c.properties.push_back(P("Taille (pt)", numberText(l.tableSize), PG::ValueType::Real, "taille_tableaux", "De 5 \xC3\xA0 14 pt."));
        c.properties.push_back(P("Lignes altern\xC3\xA9" "es", tf(l.zebra), PG::ValueType::Boolean, "lignes_alternees", "Une ligne sur deux en gris clair."));
        c.properties.push_back(P("En-t\xC3\xAAte r\xC3\xA9p\xC3\xA9t\xC3\xA9", tf(l.repeatHeader), PG::ValueType::Boolean, "entete_repete",
                                 "Un tableau coup\xC3\xA9 : son en-t\xC3\xAAte en haut de chaque page."));
        cats.push_back(std::move(c));
    }
    {
        PG::Category c;
        c.name = "Texte";
        c.expanded = false;
        c.properties.push_back(P("Police", l.font, PG::ValueType::Enum, "police", "Les polices de tout PDF : rien \xC3\xA0 installer.", {"Helvetica", "Times", "Courier"}));
        c.properties.push_back(P("Titre (pt)", numberText(l.titleSize), PG::ValueType::Real, "taille_titre", "De 8 \xC3\xA0 32 pt."));
        c.properties.push_back(P("Corps (pt)", numberText(l.bodySize), PG::ValueType::Real, "taille_corps", "De 6 \xC3\xA0 16 pt : la synth\xC3\xA8se, les intertitres (3 pt de plus)."));
        cats.push_back(std::move(c));
    }
    grid_->setCategories(std::move(cats));
}

void HmiReportLayoutPane::rebuildParts() {
    const auto l = current();
    const auto parts = rp::partsOf(l);
    partKeys_.clear();
    for (const auto& p : parts) partKeys_.push_back(p.key);
    const auto keep = parts_->selectedModelRows();
    partsModel_ = std::make_shared<PartsModel>(parts, locked());
    parts_->setModel(partsModel_);
    if (!keep.empty() && keep.front() < partKeys_.size()) parts_->selectModelRows({keep.front()});
}

void HmiReportLayoutPane::setPreviewSample(bool sample) {
    sample_ = sample;
    updatePreview();
}

void HmiReportLayoutPane::setPreviewReport(std::string report) {
    previewReport_ = std::move(report);
    if (!sample_) updatePreview();
}

void HmiReportLayoutPane::updatePreview() {
    const auto& p = doc_->project;
    const auto l = current();
    // Le rapport de l'apercu : celui choisi dans l'onglet Rapports s'il l'emploie, sinon un qui l'emploie, sinon le premier.
    const hmi::Report* r = p.reportByName(previewReport_);
    if (!r || !same(rp::layoutOf(p, *r).name, l.name))
        for (const auto& x : p.reports)
            if (same(rp::layoutOf(p, x).name, l.name)) {
                r = &x;
                break;
            }
    if (!r && !p.reports.empty()) r = &p.reports.front();
    hmi::report::Content content;
    if (sample_) {
        content = rp::sample(p, r);
    } else {
        hmi::Report rr;
        if (r) rr = *r;
        else {
            rr.name = "Rapport_journalier";
            rr.events = true;
        }
        const double wall = hmi::wallEpoch();
        const double to = rp::lastDue(rr, wall), from = rp::periodStart(rr, to);
        content = rp::build(p, doc_->history, rr, from, to, {}, hmi::wallStamp());
    }
    preview_->setDocument(std::make_shared<const rp::PageDoc>(rp::paginate(p, content, l)));
    invalidate();
}

// ---------------------------------------------------------------------------------------------- l'ecran ---
void HmiReportLayoutPane::onLayout() {
    const auto b = bounds();
    const float bar = 36.f, bottomH = 36.f;
    const float listW = std::clamp(b.w * 0.2f, 200.f, 280.f), setW = std::clamp(b.w * 0.3f, 280.f, 400.f);
    const float h = std::max(0.f, b.h - bottomH);
    const float footH = 44.f;
    tools_->setBounds({b.x, b.y, listW, bar});
    list_->setBounds({b.x, b.y + bar, listW, std::max(0.f, h - bar - footH)});
    listFoot_ = {b.x, b.y + h - footH, listW, footH};
    const float sx = b.x + listW + 1.f;
    const float gridH = std::max(120.f, (h - 24.f) * 0.6f);
    grid_->setBounds({sx, b.y, setW, gridH});
    partsHead_ = {sx, b.y + gridH, setW, 24.f};
    parts_->setBounds({sx, b.y + gridH + 24.f, setW, std::max(0.f, h - gridH - 24.f)});
    const float px = sx + setW + 1.f, pw = std::max(0.f, b.x + b.w - px);
    previewTools_->setBounds({px, b.y, pw, bar});
    preview_->setBounds({px, b.y + bar, pw, std::max(0.f, h - bar)});
    const float bw = std::min(b.w * 0.5f, 430.f);
    bottom_->setBounds({b.x + b.w - bw, b.y + h, bw, bottomH});
    bottomText_ = {b.x + 10.f, b.y + h, std::max(0.f, b.w - bw - 20.f), bottomH};
}

void HmiReportLayoutPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    // Sous la liste : qui emploie la mise en page choisie ; celle par defaut.
    ctx.r.fillRect(listFoot_, c.headerBg);
    const auto users = rp::usersOf(doc_->project, selected_);
    const std::string usedBy = "Utilis\xC3\xA9" "e par : " + (users.empty() ? std::string("aucun rapport") : joinList(users, ", "));
    const std::string def = "\xE2\x98\x85 " + rp::defaultLayoutName(doc_->project) + " : celle d'un nouveau rapport";
    ctx.r.pushClip(listFoot_);
    ctx.r.drawText({listFoot_.x + 8, listFoot_.y + 5}, usedBy, ctx.theme.font.smallUi, c.text);
    ctx.r.drawText({listFoot_.x + 8, listFoot_.y + 23}, def, ctx.theme.font.smallUi, c.textMuted);
    ctx.r.popClip();
    // Le titre des parties.
    ctx.r.fillRect(partsHead_, c.headerBg);
    ctx.r.drawText({partsHead_.x + 8, partsHead_.y + 4}, locked() ? "CONTENU" : "CONTENU \xE2\x80\x94 coche, glisse pour l'ordre (Alt+\xE2\x86\x91 \xE2\x86\x93)",
                   ctx.theme.font.smallUi, c.textMuted);
    // Le bas : l'etat de la mise en page choisie, ou ce qui vient d'etre fait.
    ctx.r.fillRect({b.x, bottomText_.y, b.w, bottomText_.h}, c.headerBg);
    ctx.r.pushClip(bottomText_);
    const std::string text = message_.empty() ? statusText() : message_;
    ctx.r.drawText({bottomText_.x, bottomText_.y + (bottomText_.h - ctx.r.lineHeight(ctx.theme.font.smallUi)) * 0.5f}, text, ctx.theme.font.smallUi,
                   modified() && message_.empty() ? c.warning : c.text);
    ctx.r.popClip();
}

ui::EventResult HmiReportLayoutPane::onEvent(const ui::InputEvent& ev) {
    // Ctrl+Z, Ctrl+Y : le brouillon d'abord (le reste, la pile du projet, passe au-dessus).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->mods.ctrl && !k->mods.alt && visible()) {
        if (k->key == ui::Key::Z && !k->mods.shift && undoDraft()) return ui::EventResult::Consumed;
        if ((k->key == ui::Key::Y || (k->key == ui::Key::Z && k->mods.shift)) && redoDraft()) return ui::EventResult::Consumed;
        if (k->key == ui::Key::S && !k->mods.shift && modified()) {
            (void)save();
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
