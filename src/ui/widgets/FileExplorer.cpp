// =============================================================================
//  ui/widgets/FileExplorer.cpp - lot API 8 : l'explorateur de fichiers de l'appli
// =============================================================================
#include "FileExplorer.hpp"

#include "DataViews.hpp"
#include "../../menu/MenuManager.hpp"
#include "../Icons.hpp"
#include "../TextSearch.hpp"

#include <algorithm>
#include <chrono>   // Lot API 8 : l'explorateur, 2e partie (le budget des vignettes)
#include <cmath>
#include <filesystem>
#include <system_error>

namespace ui {

    namespace {

        namespace fs = std::filesystem;
        namespace f = files;

        constexpr float kTitleBar = 40.f;
        constexpr float kNavBar = 44.f;
        constexpr float kPlacesW = 220.f;
        constexpr float kBottom = 86.f;
        constexpr float kStatus = 26.f;
        constexpr float kPad = 12.f;

        fs::path pathOf(std::string_view utf8) {
            const std::u8string u8(utf8.begin(), utf8.end());
            return fs::path(u8);
        }

        bool isDir(const std::string& p) {
            std::error_code ec;
            return !p.empty() && fs::is_directory(pathOf(p), ec);
        }

        bool isFile(const std::string& p) {
            std::error_code ec;
            return !p.empty() && fs::is_regular_file(pathOf(p), ec);
        }

        std::string trimmed(std::string_view s) {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
            std::string out(s.substr(a, b - a));
            if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
            return out;
        }

        bool absolutePath(std::string_view p) {
            return (p.size() >= 2 && p[1] == ':') || (!p.empty() && (p[0] == '/' || p[0] == '\\'));
        }

        // Un texte trop long pour sa place : coupe, avec des points de suspension.
        std::string fitted(std::string_view text, gfx::FontId font, float width) {
            if (ui::measureWidth(text, font) <= width) return std::string(text);
            std::string s(text);
            while (!s.empty() && ui::measureWidth(s + "\xE2\x80\xA6", font) > width) {
                s.pop_back();
                while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
            }
            return s + "\xE2\x80\xA6";
        }

        std::string plural(std::size_t n, const char* one, const char* many) {
            return std::to_string(n) + " " + (n > 1 ? many : one);
        }

    } // namespace

    // ================================================================ le modele ==
    class FileExplorerTableModel final : public ITableModel {
    public:
        explicit FileExplorerTableModel(FileExplorerDialog& d) : d_(d) {}

        [[nodiscard]] bool hasNoteRow() const {
            return d_.hiddenByFilter_ > 0 || !d_.listing_.message.empty() || (d_.rows_.empty() && !d_.folder_.empty());
        }
        [[nodiscard]] std::string noteText() const {
            if (!d_.listing_.message.empty()) return d_.listing_.message;
            if (d_.hiddenByFilter_ > 0)
                return (d_.hiddenByFilter_ > 1 ? std::to_string(d_.hiddenByFilter_) + " autres fichiers cach\xC3\xA9s par le filtre"
                                              : std::string("1 autre fichier cach\xC3\xA9 par le filtre"))
                     + (d_.showFiltered_ ? std::string(" \xC2\xB7 Double-clic : les cacher") : std::string(" \xC2\xB7 Tout afficher (double-clic)"));
            return d_.search_.empty() ? "Ce dossier est vide" : "Rien ne correspond \xC3\xA0 \xC2\xAB " + d_.search_ + " \xC2\xBB";
        }

        [[nodiscard]] std::size_t rowCount() const override { return d_.rows_.size() + (hasNoteRow() ? 1u : 0u); }
        [[nodiscard]] std::size_t columnCount() const override { return 4; }
        [[nodiscard]] std::string headerText(std::size_t col) const override {
            switch (col) {
                case 0: return "Nom";
                case 1: return "Modifi\xC3\xA9";
                case 2: return "Type";
                default: return "Taille";
            }
        }
        [[nodiscard]] std::string cellText(RowIndex row, std::size_t col) const override {
            if (row >= d_.rows_.size()) return col == 0 ? noteText() : std::string{};
            const auto& e = d_.rows_[row];
            if (e.family == f::Family::Drive || d_.folder_.empty()) {
                const f::Place* p = placeOf(e.path);
                switch (col) {
                    case 0: return e.name;
                    case 1: return {};
                    case 2: return p && !p->detail.empty() ? p->detail : std::string(e.family == f::Family::Drive ? "Lecteur" : "Dossier");
                    default: return p && p->total > 0 ? f::sizeLabel(p->free) + " libres" : std::string{};
                }
            }
            switch (col) {
                case 0: return e.name;
                case 1: return f::relativeTime(e.modified, d_.now_);
                case 2: return f::typeLabel(e.name, e.folder);
                default: return e.folder ? std::string{} : f::sizeLabel(e.size);
            }
        }
        [[nodiscard]] CellStyle cellStyle(RowIndex row, std::size_t col) const override {
            CellStyle s;
            if (row >= d_.rows_.size()) {
                if (col == 0) s.spanRow = true;
                s.fgTone = d_.listing_.message.empty() ? Tone::Muted : Tone::Error;
                return s;
            }
            const auto& e = d_.rows_[row];
            const bool greyed = d_.mode_ == f::PickMode::Folder && !e.folder && e.family != f::Family::Drive;
            if (greyed) s.fgTone = Tone::Muted;
            if (col == 0) {
                s.icon = f::iconOf(e.family);
                if (d_.theme_) s.iconColor = greyed ? d_.theme_->color.textDisabled : f::tintColor(*d_.theme_, f::tintOf(e.family));
                if (!e.folder && d_.context_.memory && d_.context_.memory->isRecentFile(e.path)) {
                    s.badge = "r\xC3\xA9" "cent";
                    s.badgeTone = Tone::Accent;
                }
            } else if (col == 3) {
                s.fgTone = Tone::Muted;
            }
            return s;
        }
        [[nodiscard]] bool less(RowIndex a, RowIndex b, std::size_t) const override { return a < b; }
        [[nodiscard]] std::string rowTooltip(RowIndex row) const override {
            return row < d_.rows_.size() ? d_.rows_[row].path : std::string{};
        }

    private:
        [[nodiscard]] const f::Place* placeOf(const std::string& path) const {
            for (const auto& p : d_.places_)
                if (p.group == f::Place::Group::Computer && p.path == path) return &p;
            return nullptr;
        }
        FileExplorerDialog& d_;
    };

    // ================================================================ la liste ===
    //  Une TableView dont les titres trient a la maniere de l'explorateur : le
    //  dialogue trie (les dossiers restent devant, la ligne du filtre en bas),
    //  la table ne fait que montrer. La fleche du tri est dessinee ici.
    class FileExplorerTable final : public TableView {
    public:
        explicit FileExplorerTable(FileExplorerDialog& d) : TableView("explorateur.liste"), d_(d) {}
        void takeFocus() { grabFocus(); }

    protected:
        void onPaint(const PaintContext& ctx) override {
            TableView::onPaint(ctx);
            headerH_ = ctx.theme.metric.headerHeight;
            rowH_ = ctx.theme.metric.rowHeight;   // ---- Lot API 8 : l'explorateur, 2e partie (le clic droit) ----
            float x = bounds().x;
            const auto& cols = columns();
            const auto sorted = static_cast<std::size_t>(d_.sort_);
            for (std::size_t i = 0; i < cols.size(); ++i) {
                if (!cols[i].visible) continue;
                if (i == sorted) {
                    // Un petit triangle : pointe en haut (croissant) ou en bas.
                    const float cx = x + cols[i].width - 14.f, cy = bounds().y + headerH_ * 0.5f;
                    for (int k = 0; k < 4; ++k) {
                        const float half = static_cast<float>(k) + 0.5f;
                        const float y = d_.ascending_ ? cy - 2.f + static_cast<float>(k) : cy + 2.f - static_cast<float>(k);
                        ctx.r.fillRect({cx - half, y, half * 2.f, 1.f}, ctx.theme.color.textMuted);
                    }
                }
                x += cols[i].width;
            }
        }

        EventResult onEvent(const InputEvent& ev) override {
            // ---- Lot API 8 : l'explorateur, 2e partie : le clic droit sur une ligne (son menu) ----
            if (const auto* d = std::get_if<MouseDown>(&ev);
                d && d->button == MouseButton::Right && bounds().contains(d->pos) && d->pos.y >= bounds().y + headerH_) {
                const float row = std::floor((d->pos.y - bounds().y - headerH_ + scrollOffset()) / std::max(1.f, rowH_));
                if (row >= 0.f && static_cast<std::size_t>(row) < d_.rows_.size()) {
                    selectModelRows({static_cast<RowIndex>(row)}, true);
                    d_.openRowMenu(d->pos);
                }
                return EventResult::Consumed;
            }
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
            if (const auto* d = std::get_if<MouseDown>(&ev);
                d && d->button == MouseButton::Left && d->pos.y >= bounds().y && d->pos.y < bounds().y + headerH_) {
                float x = bounds().x;
                const auto& cols = columns();
                for (std::size_t i = 0; i < cols.size(); ++i) {
                    if (!cols[i].visible) continue;
                    const float edge = x + cols[i].width;
                    if (std::abs(d->pos.x - edge) < 4.f) return TableView::onEvent(ev);   // redimensionner
                    if (d->pos.x < edge) {
                        d_.sortByColumn(i);
                        return EventResult::Consumed;
                    }
                    x = edge;
                }
                return EventResult::Consumed;
            }
            return TableView::onEvent(ev);
        }

    private:
        FileExplorerDialog& d_;
        float               headerH_{28.f};
        float               rowH_{24.f};   // ---- Lot API 8 : l'explorateur, 2e partie ----
    };

    // ========================================================== les emplacements ==
    class FileExplorerPlaces final : public Widget {
    public:
        explicit FileExplorerPlaces(FileExplorerDialog& d) : Widget("explorateur.emplacements"), d_(d) {}

        void rebuild() {
            rows_.clear();
            const auto group = [this](f::Place::Group g, const char* title) {
                bool first = true;
                for (std::size_t i = 0; i < d_.places_.size(); ++i) {
                    if (d_.places_[i].group != g) continue;
                    if (first) rows_.push_back({true, title, static_cast<std::size_t>(-1)});
                    first = false;
                    rows_.push_back({false, {}, i});
                }
            };
            group(f::Place::Group::Project, "CE PROJET");
            group(f::Place::Group::Recent, "R\xC3\x89" "CENTS");
            group(f::Place::Group::Pinned, "\xC3\x89PINGL\xC3\x89S");
            group(f::Place::Group::Computer, "CE PC");
            invalidate();
        }

    protected:
        void onPaint(const PaintContext& ctx) override {
            const auto& c = ctx.theme.color;
            const auto r = bounds();
            ctx.r.fillRect(r, c.railBg);
            float y = r.y + 6.f - scroll_;
            rects_.clear();
            for (const auto& row : rows_) {
                const float h = rowHeight(row);
                const gfx::Rect rr{r.x, y, r.w, h};
                rects_.push_back(rr);
                if (y + h >= r.y && y <= r.bottom()) {
                    if (row.header) {
                        ctx.r.drawText({r.x + 12.f, y + 8.f}, row.title, ctx.theme.font.smallUi, c.textMuted);
                    } else {
                        const auto& p = d_.places_[row.place];
                        const bool current = !d_.folder_.empty() && f::samePath(p.path, d_.folder_);
                        if (current) ctx.r.fillRect({r.x + 4.f, y + 1.f, r.w - 8.f, h - 2.f}, c.selectionBg);
                        const auto family = p.family == f::Family::Drive ? f::Family::Drive : f::Family::Folder;
                        drawIcon(ctx.r, f::iconOf(family), {r.x + 12.f, y + 5.f, 16.f, 16.f},
                                 f::tintColor(ctx.theme, family == f::Family::Drive ? f::Tint::Blue : f::Tint::Yellow));
                        const gfx::Color fg = current ? c.selectionText : c.text;
                        std::string label = p.label;
                        if (!p.detail.empty() && p.group != f::Place::Group::Project) label += "  " + p.detail;
                        ctx.r.drawText({r.x + 34.f, y + 4.f}, fitted(label, ctx.theme.font.ui, r.w - 44.f), ctx.theme.font.ui, fg);
                        if (p.total > 0) {
                            // La place libre : une petite barre (pleine = lecteur plein).
                            const float bw = r.w - 46.f;
                            const float used = 1.f - static_cast<float>(static_cast<double>(p.free) / static_cast<double>(p.total));
                            ctx.r.fillRect({r.x + 34.f, y + 25.f, bw, 4.f}, c.border);
                            ctx.r.fillRect({r.x + 34.f, y + 25.f, bw * std::clamp(used, 0.f, 1.f), 4.f}, used > 0.9f ? c.error : c.accent);
                        }
                    }
                }
                y += h;
            }
            contentH_ = y + scroll_ - r.y;
        }

        EventResult onEvent(const InputEvent& ev) override {
            if (const auto* w = std::get_if<MouseWheel>(&ev)) {
                const float maxScroll = std::max(0.f, contentH_ - bounds().h + 6.f);
                scroll_ = std::clamp(scroll_ - w->dy * 40.f, 0.f, maxScroll);
                invalidate();
                return EventResult::Consumed;
            }
            if (const auto* d = std::get_if<MouseDown>(&ev)) {
                for (std::size_t i = 0; i < rects_.size() && i < rows_.size(); ++i) {
                    if (rows_[i].header || !rects_[i].contains(d->pos)) continue;
                    const auto p = d_.places_[rows_[i].place];   // une copie : naviguer refait la liste
                    if (d->button == MouseButton::Right) {
                        if (p.group == f::Place::Group::Pinned && d_.context_.memory && d_.context_.memory->unpin(p.path)) {
                            if (d_.context_.memoryChanged) d_.context_.memoryChanged();
                            d_.load(true);
                        }
                        return EventResult::Consumed;
                    }
                    (void)d_.navigate(p.path);
                    return EventResult::Consumed;
                }
                return EventResult::Consumed;
            }
            return EventResult::Ignored;
        }

    private:
        struct Row {
            bool        header{false};
            std::string title;
            std::size_t place{0};
        };
        [[nodiscard]] float rowHeight(const Row& row) const {
            if (row.header) return 28.f;
            return d_.places_[row.place].total > 0 ? 34.f : 26.f;
        }
        FileExplorerDialog&    d_;
        std::vector<Row>       rows_;
        std::vector<gfx::Rect> rects_;
        float                  scroll_{0.f};
        float                  contentH_{0.f};
    };

    // ---- Lot API 8 : l'explorateur, 2e partie (l'apercu, les vignettes) ----
    namespace {
        constexpr float kPreviewW = 260.f;       // la colonne Apercu
        constexpr float kPreviewFolded = 26.f;   // repliee : une languette
        bool g_previewFolded = false;            // retenu d'une ouverture a l'autre

        // Les textures des vignettes : une par image decodee ; rendue au
        // renderer quand l'image n'a plus de proprietaire (le dialogue ferme).
        struct ThumbTexture {
            const gfx::IRenderer*             owner{nullptr};
            std::weak_ptr<const PreviewImage> source;
            gfx::TextureId                    tex{};
        };

        std::vector<ThumbTexture>& thumbTextures() {
            static std::vector<ThumbTexture> cache;
            return cache;
        }

        gfx::TextureId thumbTexture(gfx::IRenderer& r, const std::shared_ptr<const PreviewImage>& img) {
            if (!img || img->width <= 0 || img->height <= 0
                || img->rgba.size() < static_cast<std::size_t>(img->width) * static_cast<std::size_t>(img->height) * 4u)
                return {};
            auto& cache = thumbTextures();
            for (auto it = cache.begin(); it != cache.end();) {
                if (it->owner == &r && it->source.expired()) {
                    if (it->tex.v != 0) r.releaseImage(it->tex);
                    it = cache.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& t : cache)
                if (t.owner == &r && t.source.lock() == img) return t.tex;
            const auto tex = r.createImage(img->rgba.data(), img->width, img->height);
            if (tex.v != 0) cache.push_back(ThumbTexture{&r, img, tex});
            return tex;
        }

        // Une image de w x h dans `box`, a ses proportions, centree ; agrandie
        // au plus `maxScale` fois (une icone de 16 px ne devient pas floue).
        gfx::Rect fitInto(const gfx::Rect& box, int w, int h, float maxScale) {
            if (w <= 0 || h <= 0) return box;
            const float s = std::min({box.w / static_cast<float>(w), box.h / static_cast<float>(h), maxScale});
            const float dw = std::floor(static_cast<float>(w) * s), dh = std::floor(static_cast<float>(h) * s);
            return {std::floor(box.x + (box.w - dw) * 0.5f), std::floor(box.y + (box.h - dh) * 0.5f), dw, dh};
        }
    } // namespace
    // ---- fin Lot API 8 : l'explorateur, 2e partie ----

    // ============================================================ les vignettes ==
    //  Une grille de cartes : une grande icone a la teinte de la famille, la
    //  pastille de l'extension, le nom sur deux lignes. La selection est celle
    //  de la liste (la table, cachee) : un clic choisit, un double-clic ouvre ou
    //  choisit, les fleches se deplacent, Entree ouvre.
    class FileExplorerGrid final : public Widget {
    public:
        explicit FileExplorerGrid(FileExplorerDialog& d) : Widget("explorateur.grille"), d_(d) { setFocusPolicy(true); }
        void takeFocus() { grabFocus(); }
        void resetScroll() {
            scroll_ = 0.f;
            invalidate();
        }

    protected:
        static constexpr float kCardW = 132.f, kCardH = 120.f, kGap = 10.f;

        [[nodiscard]] int columnCount() const {
            return std::max(1, static_cast<int>((bounds().w - kGap) / (kCardW + kGap)));
        }
        [[nodiscard]] gfx::Rect cardRect(std::size_t i) const {
            const auto cols = static_cast<std::size_t>(columnCount());
            const float x = bounds().x + kGap + static_cast<float>(i % cols) * (kCardW + kGap);
            const float y = bounds().y + kGap + static_cast<float>(i / cols) * (kCardH + kGap) - scroll_;
            return {x, y, kCardW, kCardH};
        }
        [[nodiscard]] int selected() const {
            const auto sel = d_.table_ ? d_.table_->selectedModelRows() : std::vector<RowIndex>{};
            return sel.empty() || sel.front() >= d_.rows_.size() ? -1 : static_cast<int>(sel.front());
        }
        void choose(int i) {
            if (!d_.table_ || i < 0 || static_cast<std::size_t>(i) >= d_.rows_.size()) return;
            d_.table_->selectModelRows({static_cast<RowIndex>(i)}, true);
            // Amener la carte en vue.
            const auto r = cardRect(static_cast<std::size_t>(i));
            if (r.y < bounds().y) scroll_ -= bounds().y - r.y + kGap;
            else if (r.bottom() > bounds().bottom()) scroll_ += r.bottom() - bounds().bottom() + kGap;
            scroll_ = std::max(0.f, scroll_);
            invalidate();
        }

        void onPaint(const PaintContext& ctx) override {
            const auto& c = ctx.theme.color;
            const auto r = bounds();
            ctx.r.fillRect(r, c.inputBg);
            ctx.r.strokeRect(r, focused() ? ctx.theme.brand.focusRing : c.border, 1.f);
            ctx.r.pushClip(r);
            const int sel = selected();
            for (std::size_t i = 0; i < d_.rows_.size(); ++i) {
                const auto card = cardRect(i);
                if (card.bottom() < r.y || card.y > r.bottom()) continue;
                const auto& e = d_.rows_[i];
                const bool on = static_cast<int>(i) == sel;
                const bool greyed = d_.mode_ == f::PickMode::Folder && !e.folder && e.family != f::Family::Drive;
                ctx.r.fillRoundedRect(card, on ? c.selectionBg : ctx.theme.brand.card, ctx.theme.metric.radius);
                ctx.r.strokeRect(card, on ? c.accent : ctx.theme.brand.cardBorder, 1.f);
                const gfx::Color tint = greyed ? c.textDisabled : f::tintColor(ctx.theme, f::tintOf(e.family));
                // ---- Lot API 8 : l'explorateur, 2e partie : la vraie vignette d'une image
                //  (decodee en differe par decodeThumbnails, gardee en cache) ----
                bool pictured = false;
                if (e.family == f::Family::Image && !e.folder) {
                    const auto it = d_.thumbs_.find(e.path);
                    if (it == d_.thumbs_.end()) {
                        d_.thumbWanted_.push_back(e.path);
                    } else if (it->second) {
                        if (const auto tex = thumbTexture(ctx.r, it->second); tex.v != 0) {
                            ctx.r.drawImage(tex, fitInto({card.x + 8.f, card.y + 6.f, card.w - 16.f, 68.f}, it->second->width,
                                                         it->second->height, 1.f));
                            pictured = true;
                        }
                    }
                }
                if (!pictured) drawIcon(ctx.r, f::iconOf(e.family), {card.x + (card.w - 44.f) * 0.5f, card.y + 10.f, 44.f, 44.f}, tint);
                // ---- fin Lot API 8 : l'explorateur, 2e partie ----
                const std::string badge = pictured ? std::string{} : f::badgeText(e.name, e.folder);
                if (!badge.empty()) {
                    const float bw = ui::measureWidth(badge, ctx.theme.font.smallUi) + 10.f;
                    const gfx::Rect pill{card.x + (card.w - bw) * 0.5f, card.y + 58.f, bw, 17.f};
                    ctx.r.fillRoundedRect(pill, tint, 3.f);
                    ctx.r.drawText({pill.x + 5.f, pill.y + 1.f}, badge, ctx.theme.font.smallUi, c.panelBg);
                }
                const gfx::Color fg = on ? c.selectionText : greyed ? c.textDisabled : c.text;
                const float nameW = card.w - 12.f;
                // Le nom sur deux lignes au plus (coupe a la place, puis "...").
                const std::string& name = e.name;
                std::size_t cut = 0;
                while (cut < name.size() && ui::measureWidth(std::string_view(name).substr(0, cut + 1), ctx.theme.font.smallUi) <= nameW) ++cut;
                while (cut < name.size() && cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
                const std::string first = name.substr(0, cut);
                const std::string second = cut < name.size() ? fitted(std::string_view(name).substr(cut), ctx.theme.font.smallUi, nameW) : std::string{};
                ctx.r.drawText({card.x + 6.f + (nameW - ui::measureWidth(first, ctx.theme.font.smallUi)) * 0.5f, card.y + 80.f}, first,
                               ctx.theme.font.smallUi, fg);
                if (!second.empty())
                    ctx.r.drawText({card.x + 6.f + (nameW - ui::measureWidth(second, ctx.theme.font.smallUi)) * 0.5f, card.y + 97.f}, second,
                                   ctx.theme.font.smallUi, fg);
            }
            // La ligne du filtre (ou l'erreur), sous les cartes.
            if (d_.model_ && d_.model_->hasNoteRow()) {
                const std::size_t n = d_.rows_.size();
                const auto cols = static_cast<std::size_t>(columnCount());
                const float y = r.y + kGap + static_cast<float>((n + cols - 1) / cols) * (kCardH + kGap) - scroll_;
                noteRect_ = {r.x + kGap, y, r.w - 2.f * kGap, 24.f};
                ctx.r.drawText({noteRect_.x, noteRect_.y + 4.f}, fitted(d_.model_->noteText(), ctx.theme.font.ui, noteRect_.w), ctx.theme.font.ui,
                               d_.listing_.message.empty() ? c.textMuted : c.error);
            } else {
                noteRect_ = {};
            }
            ctx.r.popClip();
        }

        EventResult onEvent(const InputEvent& ev) override {
            if (const auto* w = std::get_if<MouseWheel>(&ev)) {
                const auto cols = static_cast<std::size_t>(columnCount());
                const float content = static_cast<float>((d_.rows_.size() + cols - 1) / cols) * (kCardH + kGap) + 40.f;
                scroll_ = std::clamp(scroll_ - w->dy * 60.f, 0.f, std::max(0.f, content - bounds().h));
                invalidate();
                return EventResult::Consumed;
            }
            if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Left) {
                grabFocus();
                if (noteRect_.w > 0.f && noteRect_.contains(d->pos) && d->clickCount >= 2) {
                    d_.activateRow(d_.rows_.size());
                    return EventResult::Consumed;
                }
                for (std::size_t i = 0; i < d_.rows_.size(); ++i) {
                    if (!cardRect(i).contains(d->pos)) continue;
                    choose(static_cast<int>(i));
                    if (d->clickCount >= 2) d_.activateRow(i);
                    return EventResult::Consumed;
                }
                return EventResult::Consumed;
            }
            // ---- Lot API 8 : l'explorateur, 2e partie : le clic droit sur une carte (son menu) ----
            if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Right && bounds().contains(d->pos)) {
                for (std::size_t i = 0; i < d_.rows_.size(); ++i) {
                    if (!cardRect(i).contains(d->pos)) continue;
                    choose(static_cast<int>(i));
                    d_.openRowMenu(d->pos);
                    break;
                }
                return EventResult::Consumed;
            }
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
            if (const auto* k = std::get_if<KeyDown>(&ev); k && focused()) {
                const int n = static_cast<int>(d_.rows_.size());
                const int cols = columnCount();
                int i = selected();
                switch (k->key) {
                    case Key::Left: i = i < 0 ? 0 : std::max(0, i - 1); break;
                    case Key::Right: i = i < 0 ? 0 : std::min(n - 1, i + 1); break;
                    case Key::Up: i = i < 0 ? 0 : std::max(0, i - cols); break;
                    case Key::Down: i = i < 0 ? 0 : std::min(n - 1, i + cols); break;
                    case Key::Home: i = 0; break;
                    case Key::End: i = n - 1; break;
                    default: return EventResult::Ignored;
                }
                choose(i);
                return EventResult::Consumed;
            }
            return EventResult::Ignored;
        }

    private:
        FileExplorerDialog& d_;
        float               scroll_{0.f};
        gfx::Rect           noteRect_{};
    };

    // ---- Lot API 8 : l'explorateur, 2e partie ----
    // ================================================================= l'apercu ==
    //  La colonne de droite : l'element choisi (calcule en differe par l'appli,
    //  FileExplorerContext::preview). Repliee : une languette ; un clic sur la
    //  fleche du titre (ou la languette) la replie ou la montre.
    class FileExplorerPreview final : public Widget {
    public:
        explicit FileExplorerPreview(FileExplorerDialog& d) : Widget("explorateur.apercu"), d_(d) {}

    protected:
        void onPaint(const PaintContext& ctx) override {
            const auto& c = ctx.theme.color;
            const auto& fnt = ctx.theme.font;
            const auto r = bounds();
            ctx.r.fillRect({r.x, r.y, 1.f, r.h}, c.border);
            openRect_ = {};
            if (!d_.previewShown_) {
                toggle_ = {r.x + 1.f, r.y, std::max(1.f, r.w - 1.f), r.h};
                ctx.r.drawText({r.x + 9.f, r.y + 6.f}, "\xE2\x80\xB9", fnt.uiBold, c.textMuted);
                return;
            }
            toggle_ = {r.right() - 28.f, r.y + 2.f, 26.f, 24.f};
            ctx.r.pushClip(r);
            const float x = r.x + 12.f, w = std::max(40.f, r.w - 24.f);
            float y = r.y + 6.f;
            ctx.r.drawText({x, y}, "APER\xC3\x87U", fnt.caption, c.textMuted);
            ctx.r.drawText({toggle_.x + 9.f, toggle_.y + 1.f}, "\xE2\x80\xBA", fnt.uiBold, c.textMuted);
            y += 28.f;
            const FilePreviewInfo& p = d_.preview_;
            if (p.path.empty()) {
                const char* hint = d_.previewDue_ >= 0.0 ? "\xE2\x80\xA6" : "Choisis un \xC3\xA9l\xC3\xA9ment pour le voir ici";
                ctx.r.drawText({x, y}, fitted(hint, fnt.smallUi, w), fnt.smallUi, c.textMuted);
                ctx.r.popClip();
                return;
            }
            // La vignette d'une image, ou la grande icone de la famille.
            bool pictured = false;
            if (p.image) {
                const gfx::Rect box = fitInto({x, y, w, 150.f}, p.image->width, p.image->height, 2.f);
                if (const auto tex = thumbTexture(ctx.r, p.image); tex.v != 0) {
                    ctx.r.drawImage(tex, box);
                    pictured = true;
                } else {
                    ctx.r.strokeRect(box, c.border, 1.f);   // un renderer sans images (tests)
                }
                y += box.h + 10.f;
            }
            if (!pictured && !p.image) {
                const auto fam = f::familyOf(p.name, p.folder);
                drawIcon(ctx.r, f::iconOf(fam), {x + (w - 56.f) * 0.5f, y, 56.f, 56.f}, f::tintColor(ctx.theme, f::tintOf(fam)));
                y += 64.f;
            }
            ctx.r.drawText({x, y}, fitted(p.name, fnt.uiBold, w), fnt.uiBold, c.text);
            y += 22.f;
            ctx.r.drawText({x, y}, fitted(p.kind, fnt.smallUi, w), fnt.smallUi, c.textMuted);
            y += 24.f;
            if (!p.error.empty()) {
                ctx.r.drawText({x, y}, fitted(p.error, fnt.smallUi, w), fnt.smallUi, c.error);
                y += 20.f;
            }
            const float keyW = std::min(96.f, w * 0.42f);
            for (const auto& [key, value] : p.facts) {
                ctx.r.drawText({x, y}, fitted(key, fnt.smallUi, keyW - 4.f), fnt.smallUi, c.textMuted);
                ctx.r.drawText({x + keyW, y}, fitted(value, fnt.smallUi, w - keyW), fnt.smallUi, c.text);
                y += 19.f;
            }
            for (const auto& note : p.notes) {
                ctx.r.drawText({x, y}, fitted(note, fnt.smallUi, w), fnt.smallUi, c.accent);
                y += 19.f;
            }
            if (!p.sheets.empty()) {
                y += 4.f;
                ctx.r.drawText({x, y}, "Onglets", fnt.caption, c.textMuted);
                y += 18.f;
                const std::size_t shown = std::min<std::size_t>(p.sheets.size(), 5);
                for (std::size_t i = 0; i < shown; ++i) {
                    ctx.r.drawText({x + 6.f, y}, fitted("\xE2\x80\xA2 " + p.sheets[i], fnt.smallUi, w - 6.f), fnt.smallUi, c.text);
                    y += 18.f;
                }
                if (p.sheets.size() > shown) {
                    ctx.r.drawText({x + 6.f, y}, "+ " + plural(p.sheets.size() - shown, "autre", "autres"), fnt.smallUi, c.textMuted);
                    y += 18.f;
                }
            }
            if (!p.cells.empty()) {
                y += 4.f;
                std::size_t cols = 0;
                for (const auto& row : p.cells) cols = std::max(cols, row.size());
                if (cols > 0) {
                    const float cw = std::floor(w / static_cast<float>(cols));
                    for (const auto& row : p.cells) {
                        for (std::size_t j = 0; j < cols; ++j) {
                            const gfx::Rect cell{x + static_cast<float>(j) * cw, y, cw, 20.f};
                            ctx.r.strokeRect(cell, c.border, 1.f);
                            if (j < row.size())
                                ctx.r.drawText({cell.x + 3.f, cell.y + 3.f}, fitted(row[j], fnt.smallUi, cw - 6.f), fnt.smallUi, c.text);
                        }
                        y += 20.f;
                    }
                    y += 6.f;
                }
            }
            if (!p.text.empty()) {
                y += 4.f;
                const float lh = ctx.r.lineHeight(fnt.mono);
                const gfx::Rect box{x, y, w, static_cast<float>(p.text.size()) * lh + 8.f};
                ctx.r.fillRect(box, c.inputBg);
                ctx.r.strokeRect(box, c.border, 1.f);
                float ty = y + 4.f;
                for (const auto& line : p.text) {
                    ctx.r.drawText({x + 4.f, ty}, fitted(line, fnt.mono, w - 8.f), fnt.mono, c.text);
                    ty += lh;
                }
                y = box.bottom() + 6.f;
            }
            if (p.openable && (d_.context_.openExternally)) {
                y += 6.f;
                openRect_ = {x, y, w, 28.f};
                ctx.r.fillRoundedRect(openRect_, ctx.theme.brand.card, ctx.theme.metric.radius);
                ctx.r.strokeRect(openRect_, c.border, 1.f);
                const std::string label = fitted("Ouvrir avec le programme du syst\xC3\xA8me", fnt.smallUi, w - 12.f);
                ctx.r.drawText({x + (w - ui::measureWidth(label, fnt.smallUi)) * 0.5f, y + 6.f}, label, fnt.smallUi, c.accent);
            }
            ctx.r.popClip();
        }

        EventResult onEvent(const InputEvent& ev) override {
            if (const auto* m = std::get_if<MouseDown>(&ev); m && m->button == MouseButton::Left) {
                if (toggle_.contains(m->pos)) {
                    d_.setPreviewShown(!d_.previewShown_);
                    return EventResult::Consumed;
                }
                if (openRect_.w > 0.f && openRect_.contains(m->pos)) {
                    const std::string why = d_.openSelectedExternally();
                    if (!why.empty()) d_.preview_.error = why;
                    invalidate();
                }
                return EventResult::Consumed;
            }
            return EventResult::Ignored;
        }

    private:
        FileExplorerDialog& d_;
        gfx::Rect           toggle_{}, openRect_{};
    };
    // ---- fin Lot API 8 : l'explorateur, 2e partie ----

    // ================================================================= le cadre ==
    class FileExplorerBody final : public Widget {
    public:
        explicit FileExplorerBody(FileExplorerDialog& d) : Widget("explorateur.corps"), d_(d) {}

        [[nodiscard]] const gfx::Rect& panel() const noexcept { return panel_; }

    protected:
        void onLayout() override {
            const auto r = bounds();
            // ---- Lot API 8 : l'explorateur, 2e partie : l'apercu elargit le dialogue ----
            const float previewW = d_.previewView_ ? (d_.previewShown_ ? kPreviewW : kPreviewFolded) : 0.f;
            const float w = std::floor(std::clamp(r.w - 40.f, 640.f, 980.f + previewW));
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
            const float h = std::floor(std::clamp(r.h - 40.f, 420.f, 620.f));
            panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
            close_ = {panel_.right() - 36.f, panel_.y + 6.f, 28.f, 28.f};
            // La barre de navigation.
            const float ny = panel_.y + kTitleBar + 7.f;
            float x = panel_.x + kPad;
            for (Button* b : {d_.backButton_, d_.forwardButton_, d_.upButton_}) {
                if (!b) continue;
                b->setBounds({x, ny, 32.f, 30.f});
                x += 36.f;
            }
            float right = panel_.right() - kPad;
            const auto place = [&right, ny](Button* b, float minW) {
                if (!b) return;
                const float bw = std::max(minW, ui::measureWidth(b->text(), gfx::FontId{16}) + 24.f);
                right -= bw;
                b->setBounds({right, ny, bw, 30.f});
                right -= 6.f;
            };
            place(d_.newFolderButton_, 120.f);
            place(d_.pinButton_, 32.f);
            place(d_.thumbsButton_, 80.f);
            place(d_.detailsButton_, 70.f);
            // ---- Lot API 8 : l'explorateur, 2e partie : la case "et ses sous-dossiers", apres Chercher ----
            if (d_.deepBox_) {
                const float cw = ui::measureWidth(d_.deepBox_->label(), gfx::FontId{16}) + 30.f;
                right -= cw;
                d_.deepBox_->setBounds({right, ny + 3.f, cw, 24.f});
                right -= 6.f;
            }
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
            if (d_.searchField_) {
                const float sw = std::min(220.f, std::max(140.f, (right - x) * 0.3f));
                right -= sw;
                d_.searchField_->setBounds({right, ny, sw, 30.f});
                right -= 8.f;
            }
            crumbs_ = {x + 4.f, ny, std::max(60.f, right - x - 4.f), 30.f};
            if (d_.pathField_) d_.pathField_->setBounds(crumbs_);
            // Le centre.
            const float top = ny + 30.f + 8.f;
            const float bottomTop = panel_.bottom() - kStatus - kBottom;
            places_ = {panel_.x + 1.f, top, kPlacesW, bottomTop - top};
            if (d_.placesView_) d_.placesView_->setBounds(places_);
            const gfx::Rect list{panel_.x + kPlacesW + 8.f, top, panel_.right() - kPad - (panel_.x + kPlacesW + 8.f), bottomTop - top};
            // ---- Lot API 8 : l'explorateur, 2e partie : l'apercu a droite de la liste ----
            const gfx::Rect view{list.x, list.y, std::max(120.f, list.w - previewW + kPad - 8.f), list.h};
            if (d_.previewView_) d_.previewView_->setBounds({view.right() + 8.f, top, panel_.right() - 1.f - (view.right() + 8.f), bottomTop - top});
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
            if (d_.grid_) d_.grid_->setBounds(view);
            if (d_.table_) {
                d_.table_->setBounds(view);
                // Les colonnes suivent la largeur : le nom prend ce qui reste.
                auto cols = d_.table_->columns();
                if (cols.size() == 4) {
                    const float others = cols[1].width + cols[2].width + cols[3].width;
                    const float nameW = std::max(160.f, view.w - others - 14.f);
                    if (std::abs(cols[0].width - nameW) > 0.5f) {
                        cols[0].width = nameW;
                        d_.table_->setColumns(cols);
                    }
                }
            }
            // Le bas : le nom et les types, puis le message et les boutons.
            const float by = bottomTop + 10.f;
            nameLabel_ = {list.x, by, 110.f, 30.f};
            const float typeW = std::min(320.f, list.w * 0.42f);
            if (d_.typeList_) d_.typeList_->setBounds({list.right() - typeW, by, typeW, 30.f});
            if (d_.nameField_) d_.nameField_->setBounds({list.x + 110.f, by, std::max(120.f, list.w - typeW - 118.f), 30.f});
            float bx = panel_.right() - kPad;
            for (Button* b : {d_.okButton_, d_.cancelButton_}) {
                if (!b) continue;
                const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
                bx -= bw;
                b->setBounds({bx, by + 40.f, bw, 30.f});
                bx -= 8.f;
            }
            messageRect_ = {list.x, by + 40.f, std::max(60.f, bx - list.x - 8.f), 30.f};
        }

        void onPaint(const PaintContext& ctx) override {
            d_.theme_ = &ctx.theme;
            const auto& c = ctx.theme.color;
            const auto& fnt = ctx.theme.font;
            ctx.r.fillRect(panel_, c.panelBg);
            ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
            ctx.r.fillRect({panel_.x, panel_.y, panel_.w, kTitleBar}, c.headerBg);
            ctx.r.drawText({panel_.x + 14.f, panel_.y + (kTitleBar - ctx.r.lineHeight(fnt.uiBold)) * 0.5f},
                           fitted(d_.title(), fnt.uiBold, panel_.w - 70.f), fnt.uiBold, c.text);
            drawIcon(ctx.r, Icon::Close, {close_.x + 6.f, close_.y + 6.f, 16.f, 16.f}, c.textMuted);
            // Le fil d'Ariane.
            crumbRects_.clear();
            if (!d_.pathMode_) {
                ctx.r.fillRect(crumbs_, c.inputBg);
                ctx.r.strokeRect(crumbs_, c.border, 1.f);
                const auto crumbs = f::breadcrumb(d_.folder_);
                // Trop long : les premiers morceaux cedent la place ("...").
                std::size_t first = 0;
                const auto widthFrom = [&](std::size_t k) {
                    float w = 0.f;
                    for (std::size_t i = k; i < crumbs.size(); ++i) w += ui::measureWidth(crumbs[i].label, fnt.ui) + 22.f;
                    return w;
                };
                while (first + 1 < crumbs.size() && widthFrom(first) > crumbs_.w - 40.f) ++first;
                float x = crumbs_.x + 8.f;
                const float ty = crumbs_.y + (crumbs_.h - ctx.r.lineHeight(fnt.ui)) * 0.5f;
                if (first > 0) {
                    ctx.r.drawText({x, ty}, "\xE2\x80\xA6 \xE2\x80\xBA", fnt.ui, c.textMuted);
                    x += ui::measureWidth("\xE2\x80\xA6 \xE2\x80\xBA", fnt.ui) + 6.f;
                }
                for (std::size_t i = first; i < crumbs.size(); ++i) {
                    const float w = ui::measureWidth(crumbs[i].label, fnt.ui);
                    const bool last = i + 1 == crumbs.size();
                    ctx.r.drawText({x, ty}, crumbs[i].label, fnt.ui, last ? c.text : c.accent);
                    crumbRects_.push_back({{x - 3.f, crumbs_.y, w + 6.f, crumbs_.h}, crumbs[i].path});
                    x += w + 6.f;
                    if (!last) {
                        ctx.r.drawText({x, ty}, "\xE2\x80\xBA", fnt.ui, c.textMuted);
                        x += ui::measureWidth("\xE2\x80\xBA", fnt.ui) + 8.f;
                    }
                }
            }
            // Separateurs du centre.
            ctx.r.fillRect({places_.right(), places_.y, 1.f, places_.h}, c.border);
            // Le bas.
            ctx.r.drawText({nameLabel_.x, nameLabel_.y + 6.f}, d_.mode_ == f::PickMode::Folder ? "Dossier :" : "Nom du fichier :",
                           fnt.smallUi, c.textMuted);
            const std::string msg = d_.message();
            if (!msg.empty()) {
                const bool error = d_.messageIsError_;
                ctx.r.drawText({messageRect_.x, messageRect_.y + 7.f}, fitted(msg, fnt.smallUi, messageRect_.w), fnt.smallUi,
                               error ? c.error : c.warning);
            }
            // La ligne d'etat.
            const gfx::Rect status{panel_.x + 1.f, panel_.bottom() - kStatus, panel_.w - 2.f, kStatus - 1.f};
            ctx.r.fillRect(status, c.headerBg);
            // ---- Lot API 8 : l'explorateur, 2e partie : le lien discret du reglage, a droite ----
            float statusW = status.w - 24.f;
            systemLink_ = {};
            if (d_.context_.useSystemExplorer) {
                const std::string link = d_.systemChosen_ ? "Not\xC3\xA9 : l'explorateur du syst\xC3\xA8me la prochaine fois"
                                                          : "Utiliser l'explorateur du syst\xC3\xA8me";
                const float lw = ui::measureWidth(link, fnt.smallUi);
                systemLink_ = {status.right() - 12.f - lw, status.y, lw, status.h};
                ctx.r.drawText({systemLink_.x, status.y + 5.f}, link, fnt.smallUi, d_.systemChosen_ ? c.textMuted : c.accent);
                statusW = std::max(40.f, statusW - lw - 16.f);
            }
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
            ctx.r.drawText({status.x + 12.f, status.y + 5.f}, fitted(d_.statusText(), fnt.smallUi, statusW), fnt.smallUi,
                           c.textMuted);
        }

        EventResult onEvent(const InputEvent& ev) override {
            if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Left) {
                if (close_.contains(d->pos)) {
                    d_.cancel();
                    return EventResult::Consumed;
                }
                if (!d_.pathMode_ && crumbs_.contains(d->pos)) {
                    for (const auto& [rect, path] : crumbRects_) {
                        if (!rect.contains(d->pos)) continue;
                        const std::string target = path;   // naviguer refait les morceaux
                        (void)d_.navigate(target);
                        return EventResult::Consumed;
                    }
                    d_.showPathField(true);   // un clic dans le vide du fil : le chemin a taper
                    return EventResult::Consumed;
                }
                // ---- Lot API 8 : l'explorateur, 2e partie : le lien du reglage ----
                if (systemLink_.w > 0.f && systemLink_.contains(d->pos) && !d_.systemChosen_) {
                    (void)d_.chooseSystemExplorer();
                    return EventResult::Consumed;
                }
                // ---- fin Lot API 8 : l'explorateur, 2e partie ----
                return panel_.contains(d->pos) ? EventResult::Consumed : EventResult::Ignored;
            }
            return EventResult::Ignored;
        }

    private:
        FileExplorerDialog& d_;
        gfx::Rect           panel_{}, close_{}, crumbs_{}, places_{}, nameLabel_{}, messageRect_{};
        std::vector<std::pair<gfx::Rect, std::string>> crumbRects_;
        gfx::Rect           systemLink_{};   // ---- Lot API 8 : l'explorateur, 2e partie ----
    };

    // ============================================================== le dialogue ==
    FileExplorerDialog::FileExplorerDialog(FilePick pick, FileExplorerContext context, std::function<void(std::string)> done)
        : menu::WidgetMenu("dialog.fileExplorer"), pick_(std::move(pick)), context_(std::move(context)), done_(std::move(done)) {
        if (!context_.memory) context_.memory = std::make_shared<f::Memory>();
        mode_ = pick_.folder ? f::PickMode::Folder : pick_.save ? f::PickMode::Save : f::PickMode::Open;
        filters_ = f::filterGroups(pick_.filters);
        kind_ = f::requestKind(mode_, filters_);
        const auto view = context_.memory->view(kind_);
        thumbnails_ = view.thumbnails;
        sort_ = view.sort;
        ascending_ = view.ascending;
        now_ = f::nowSeconds();
    }

    FileExplorerDialog::~FileExplorerDialog() {
        // Ferme sans reponse (le projet ferme, l'appli quitte) : l'appelant
        // l'apprend comme une annulation - son bouton ... se rallume.
        if (!answered_ && done_) {
            answered_ = true;
            auto done = std::move(done_);
            done(std::string{});
        }
    }

    menu::MenuTraits FileExplorerDialog::traits() const {
        menu::MenuTraits t;
        t.kind = menu::MenuKind::Dialog;
        t.rendersBelow = true;
        t.blocksInput = true;
        t.dimsBelow = true;
        t.closableWithEscape = true;
        return t;
    }

    std::string FileExplorerDialog::title() const {
        if (!pick_.title.empty()) return pick_.title;
        return mode_ == f::PickMode::Folder ? "Choisir un dossier" : mode_ == f::PickMode::Save ? "Enregistrer sous" : "Choisir un fichier";
    }

    core::Status FileExplorerDialog::buildUi() {
        auto body = std::make_unique<FileExplorerBody>(*this);
        body_ = body.get();
        const auto addButton = [&body](std::string text, std::string id, std::string tip) {
            auto& b = static_cast<Button&>(body->addChild(std::make_unique<Button>(std::move(text), std::move(id))));
            b.setTooltip(std::move(tip));
            return &b;
        };
        backButton_ = addButton("\xE2\x86\x90", "explorateur.precedent", "Pr\xC3\xA9" "c\xC3\xA9" "dent");
        forwardButton_ = addButton("\xE2\x86\x92", "explorateur.suivant", "Suivant");
        upButton_ = addButton("\xE2\x86\x91", "explorateur.parent", "Dossier parent (Retour arri\xC3\xA8re)");
        links_ += backButton_->clicked->connect([this] { (void)back(); });
        links_ += forwardButton_->clicked->connect([this] { (void)forward(); });
        links_ += upButton_->clicked->connect([this] { (void)up(); });

        auto path = std::make_unique<InputText>("explorateur.chemin");
        path->setPlaceholder("Tape ou colle un chemin, puis Entr\xC3\xA9" "e");
        path->setAssist([](std::string_view before, std::size_t& from, std::vector<InputText::Suggestion>& out) {
            const auto cut = before.find_last_of("/\\");
            if (cut == std::string_view::npos) return;
            from = cut + 1;
            for (const auto& full : f::completeFolder(before)) {
                InputText::Suggestion s;
                s.text = f::breadcrumb(full).back().label;
                s.icon = Icon::Folder;
                s.chain = false;
                out.push_back(std::move(s));
            }
        });
        pathField_ = &static_cast<InputText&>(body->addChild(std::move(path)));
        pathField_->setVisibility(Visibility::Collapsed);

        auto search = std::make_unique<InputText>("explorateur.chercher");
        search->setPlaceholder("Chercher");
        search->setEscapeClears(true);
        searchField_ = &static_cast<InputText&>(body->addChild(std::move(search)));
        links_ += searchField_->textChanged->connect([this](const std::string& text) {
            search_ = text;
            rebuildRows();
            sync();
        });

        detailsButton_ = addButton("D\xC3\xA9tails", "explorateur.details", "La liste : nom, date, type, taille");
        thumbsButton_ = addButton("Vignettes", "explorateur.vignettes", "Une grille de cartes (les images en vignette)");
        links_ += detailsButton_->clicked->connect([this] { setThumbnails(false); });
        links_ += thumbsButton_->clicked->connect([this] { setThumbnails(true); });
        pinButton_ = addButton({}, "explorateur.epingler", "\xC3\x89pingler ce dossier (il revient dans \xC3\x89PINGL\xC3\x89S)");
        pinButton_->setIcon(Icon::Star);
        pinButton_->setCompact(true);
        links_ += pinButton_->clicked->connect([this] { togglePin(); });
        newFolderButton_ = addButton("Nouveau dossier", "explorateur.nouveau", "Cr\xC3\xA9" "er un dossier ici");
        links_ += newFolderButton_->clicked->connect([this] { createFolder(); });

        auto places = std::make_unique<FileExplorerPlaces>(*this);
        placesView_ = &static_cast<FileExplorerPlaces&>(body->addChild(std::move(places)));

        auto table = std::make_unique<FileExplorerTable>(*this);
        table_ = &static_cast<FileExplorerTable&>(body->addChild(std::move(table)));
        table_->setSelectionMode(SelectionMode::Single);
        // Lot API 8 : Modifie tient "aujourd'hui 14:18" (130 px le coupait).
        table_->setColumns({{"Nom", 300.f, 120.f, true, false},
                            {"Modifi\xC3\xA9", 150.f, 60.f, true, false},
                            {"Type", 190.f, 60.f, true, false},
                            {"Taille", 90.f, 50.f, true, false, true, Align::End}});
        const auto widths = context_.memory->view(kind_).widths;
        if (widths.size() == 4) {
            auto cols = table_->columns();
            for (std::size_t i = 1; i < 4; ++i) cols[i].width = std::max(cols[i].minWidth, widths[i]);
            table_->setColumns(cols);
        }
        model_ = std::make_shared<FileExplorerTableModel>(*this);
        table_->setModel(model_);
        links_ += table_->selectionChanged->connect([this](const std::vector<RowIndex>&) { selectionChanged(); });
        links_ += table_->activated->connect([this](RowIndex row) { activateRow(row); });
        auto grid = std::make_unique<FileExplorerGrid>(*this);
        grid_ = &static_cast<FileExplorerGrid&>(body->addChild(std::move(grid)));
        grid_->setVisibility(Visibility::Collapsed);
        // ---- Lot API 8 : l'explorateur, 2e partie : la colonne Apercu ----
        previewShown_ = !g_previewFolded;
        auto preview = std::make_unique<FileExplorerPreview>(*this);
        previewView_ = &static_cast<FileExplorerPreview&>(body->addChild(std::move(preview)));
        rowMenu_ = &static_cast<PopupMenu&>(body->addChild(std::make_unique<PopupMenu>("explorateur.menu")));   // le clic droit
        links_ += rowMenu_->itemChosen->connect([this](int id) { rowMenuAction(id); });
        deepBox_ = &static_cast<Checkbox&>(body->addChild(std::make_unique<Checkbox>("et ses sous-dossiers", "explorateur.sous-dossiers")));
        deepBox_->setTooltip("Chercher aussi dans les sous-dossiers");
        links_ += deepBox_->stateChanged->connect([this](Checkbox::State s) { setSearchSubfolders(s == Checkbox::State::Checked); });
        // ---- fin Lot API 8 : l'explorateur, 2e partie ----

        auto name = std::make_unique<InputText>("explorateur.nom");
        nameField_ = &static_cast<InputText&>(body->addChild(std::move(name)));
        links_ += nameField_->textChanged->connect([this](const std::string&) {
            nameFromSelection_ = false;
            lastError_.clear();
            sync();
        });
        if (mode_ == f::PickMode::Folder) nameField_->setReadOnly(true);
        // Taper le nom complete : les elements du dossier qui commencent ainsi.
        else nameField_->setAssist([this](std::string_view before, std::size_t& from, std::vector<InputText::Suggestion>& out) {
            from = 0;
            const std::string want = foldForSearch(before);
            if (want.empty()) return;
            for (const auto& e : rows_) {
                if (out.size() >= 8) break;
                if (foldForSearch(e.name).rfind(want, 0) != 0) continue;
                InputText::Suggestion sug;
                sug.text = e.name;
                sug.icon = f::iconOf(e.family);
                sug.detail = f::typeLabel(e.name, e.folder);
                out.push_back(std::move(sug));
            }
        });

        auto types = std::make_unique<DropDown>("explorateur.types");
        typeList_ = &static_cast<DropDown&>(body->addChild(std::move(types)));
        std::vector<DropDown::Item> items;
        if (mode_ == f::PickMode::Folder) {
            items.push_back({"Dossiers", "dossiers"});
        } else {
            for (const auto& g : filters_) items.push_back({g.label(), g.name});
        }
        typeList_->setItems(std::move(items));
        typeList_->setSelectedIndex(0);
        typeList_->setEnabled(mode_ != f::PickMode::Folder);
        links_ += typeList_->selectionChanged->connect([this](int i) {
            if (i < 0 || i == filterIndex_) return;
            filterIndex_ = i;
            showFiltered_ = false;
            rebuildRows();
            sync();
        });

        cancelButton_ = addButton("Annuler", "explorateur.annuler", "Fermer sans rien choisir (\xC3\x89" "chap)");
        links_ += cancelButton_->clicked->connect([this] { cancel(); });
        okButton_ = addButton("Ouvrir", "explorateur.valider", {});
        okButton_->setStyle(Button::Style::Primary);
        links_ += okButton_->clicked->connect([this] { (void)accept(); });

        setRoot(std::move(body));

        // Les emplacements, puis le dossier de depart.
        load(false);
        std::string selectName;
        const std::string start = startFolder(&selectName);
        (void)navigate(start);
        backStack_.clear();
        if (!selectName.empty()) {
            if (mode_ == f::PickMode::Save) setFileName(selectName);
            else (void)select(selectName);
        }
        setThumbnails(thumbnails_);
        table_->takeFocus();
        return core::ok();
    }

    std::string FileExplorerDialog::startFolder(std::string* selectName) {
        // Le `start` de la demande l'emporte : un dossier, ou un fichier (propose
        // ou a choisir) dont le dossier existe.
        const std::string start = trimmed(pick_.start);
        if (!start.empty()) {
            if (isDir(start)) return start;
            const std::string parent = f::parentOf(start);
            if (isDir(parent)) {
                if (selectName) *selectName = f::breadcrumb(start).back().label;
                return parent;
            }
        }
        const std::string last = context_.memory->lastFolder(kind_);
        if (isDir(last)) return last;
        if (isDir(context_.projectDir)) return context_.projectDir;
        for (const auto& p : places_)
            if (p.group == f::Place::Group::Computer && isDir(p.path)) return p.path;
        return {};
    }

    // ---------------------------------------------------------------- lire ---
    void FileExplorerDialog::load(bool keepSelection) {
        const f::Entry* was = keepSelection ? selectedEntry() : nullptr;
        const std::string keep = was ? was->name : std::string{};
        now_ = f::nowSeconds();
        // Les emplacements : le projet, les recents de ce genre de demande, les epingles, ce PC.
        places_ = f::projectPlaces(context_.projectDir, context_.sourceXpg);
        for (const auto& r : context_.memory->recents(kind_)) {
            f::Place p;
            p.group = f::Place::Group::Recent;
            p.path = r.folder;
            p.label = f::breadcrumb(r.folder).back().label;
            p.detail = r.file.empty() ? std::string{} : f::breadcrumb(r.file).back().label;
            places_.push_back(std::move(p));
        }
        for (const auto& pin : context_.memory->pins()) {
            f::Place p;
            p.group = f::Place::Group::Pinned;
            p.path = pin;
            p.label = f::breadcrumb(pin).back().label;
            places_.push_back(std::move(p));
        }
        for (auto& p : f::computerPlaces()) places_.push_back(std::move(p));
        if (placesView_) placesView_->rebuild();

        if (folder_.empty()) {
            // Ce PC : les dossiers et les lecteurs.
            listing_ = {};
            for (const auto& p : places_) {
                if (p.group != f::Place::Group::Computer) continue;
                f::Entry e;
                e.name = p.label;
                e.path = p.path;
                e.folder = true;
                e.family = p.family;
                listing_.entries.push_back(std::move(e));
            }
        } else {
            listing_ = f::listFolder(folder_);
        }
        // La place libre du lecteur du dossier ouvert (une fois par lecture, pas a chaque touche).
        space_.clear();
        std::uint64_t total = 0, freeBytes = 0;
        if (!folder_.empty() && listing_.error == f::ListError::None && f::spaceOf(folder_, total, freeBytes)) {
            const auto crumbs = f::breadcrumb(folder_);
            space_ = (crumbs.size() > 1 ? crumbs[1].label + " " : std::string{}) + f::sizeLabel(freeBytes) + " libres";
        }
        rebuildRows();
        if (!keep.empty()) (void)select(keep);
        sync();
    }

    void FileExplorerDialog::rebuildRows() {
        if (folder_.empty()) {
            rows_ = listing_.entries;
            hiddenByFilter_ = 0;
        } else {
            // ---- Lot API 8 : l'explorateur, 2e partie : chercher aussi dans les sous-dossiers ----
            const bool deep = searchDeep_ && !trimmed(search_).empty();
            const std::vector<f::Entry> deepAll = deep ? deepEntries() : std::vector<f::Entry>{};
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
            auto shown = f::shownEntries(deep ? deepAll : listing_.entries, currentFilter(), search_, sort_, ascending_, showFiltered_);
            rows_ = std::move(shown.rows);
            hiddenByFilter_ = shown.hiddenByFilter;
            // ---- Lot API 8 : l'explorateur, 2e partie : "sous/a.csv" (le nom sous le dossier ouvert) ----
            if (deep) {
                const std::size_t skip = folder_.size() + ((folder_.back() == '/' || folder_.back() == '\\') ? 0u : 1u);
                for (auto& e : rows_)
                    if (e.path.size() > skip && e.path.compare(0, folder_.size(), folder_) == 0 && e.path.substr(skip) != e.name)
                        e.name = e.path.substr(skip);
            }
            // ---- fin Lot API 8 : l'explorateur, 2e partie ----
        }
        if (model_) model_->modelReset->emit();
        if (table_) table_->selectModelRows({}, false);
        if (grid_) grid_->resetScroll();
    }

    const f::FilterGroup* FileExplorerDialog::currentFilter() const {
        if (mode_ == f::PickMode::Folder || filters_.empty()) return nullptr;   // un dossier : les fichiers grises, pas caches
        const auto i = static_cast<std::size_t>(std::clamp(filterIndex_, 0, static_cast<int>(filters_.size()) - 1));
        return filters_[i].all() ? nullptr : &filters_[i];
    }

    const f::Entry* FileExplorerDialog::selectedEntry() const {
        if (!table_) return nullptr;
        const auto sel = table_->selectedModelRows();
        if (sel.empty() || sel.front() >= rows_.size()) return nullptr;
        return &rows_[sel.front()];
    }

    // ------------------------------------------------------------ naviguer ---
    bool FileExplorerDialog::navigate(const std::string& pathIn, std::string* why) {
        std::string target = trimmed(pathIn);
        // "D:\Affaires\" -> "D:\Affaires" (le fil d'Ariane et le champ du chemin) ; "D:\" et "/" restent.
        while (target.size() > 1 && (target.back() == '/' || target.back() == '\\') && !(target.size() == 3 && target[1] == ':')
               && !(target.size() == 2 && (target[0] == '/' || target[0] == '\\')))
            target.pop_back();
        if (target.size() == 2 && target[1] == ':') target += '\\';   // "D:" : la racine du lecteur
        std::string fileToSelect;
        if (!target.empty() && isFile(target)) {
            fileToSelect = f::breadcrumb(target).back().label;
            target = f::parentOf(target);
        }
        const std::string previous = folder_;
        folder_ = target;
        notice_.clear();
        lastError_.clear();
        showFiltered_ = false;
        if (table_) table_->setScrollOffset(0.f);
        load(false);
        bool ok = listing_.error == f::ListError::None;
        if (!ok && why) *why = listing_.message;
        if (listing_.error == f::ListError::DriveGone) {
            // La cle retiree : on le dit, et on remonte a Ce PC.
            notice_ = listing_.message;
            folder_.clear();
            load(false);
        }
        if (!f::samePath(previous, folder_) || previous.empty() != folder_.empty()) {
            backStack_.push_back(previous);
            forwardStack_.clear();
        }
        if (!fileToSelect.empty()) {
            if (mode_ == f::PickMode::Save) setFileName(fileToSelect);
            else (void)select(fileToSelect);
        }
        if (pathMode_) showPathField(false);
        sync();
        return ok;
    }

    bool FileExplorerDialog::back() {
        if (backStack_.empty()) return false;
        const std::string target = backStack_.back();
        backStack_.pop_back();
        forwardStack_.push_back(folder_);
        auto fwd = forwardStack_;
        const auto backs = backStack_;
        (void)navigate(target);
        backStack_ = backs;
        forwardStack_ = std::move(fwd);
        sync();
        return true;
    }

    bool FileExplorerDialog::forward() {
        if (forwardStack_.empty()) return false;
        const std::string target = forwardStack_.back();
        forwardStack_.pop_back();
        auto backs = backStack_;
        backs.push_back(folder_);
        const auto fwd = forwardStack_;
        (void)navigate(target);
        backStack_ = std::move(backs);
        forwardStack_ = fwd;
        sync();
        return true;
    }

    bool FileExplorerDialog::up() {
        if (folder_.empty()) return false;
        return navigate(f::parentOf(folder_)) || true;
    }

    void FileExplorerDialog::refresh() { load(true); }

    void FileExplorerDialog::createFolder() {
        if (folder_.empty()) return;
        std::string name = "Nouveau dossier";
        for (int i = 2; i < 100 && fs::exists(pathOf(f::joinPath(folder_, name))); ++i) name = "Nouveau dossier (" + std::to_string(i) + ")";
        std::error_code ec;
        if (!fs::create_directory(pathOf(f::joinPath(folder_, name)), ec) || ec) {
            notice_ = "Le dossier ne se cr\xC3\xA9" "e pas ici" + (ec ? " (" + ec.message() + ")" : std::string{});
            sync();
            return;
        }
        load(false);
        (void)select(name);
    }

    void FileExplorerDialog::togglePin() {
        if (folder_.empty()) return;
        if (!context_.memory->unpin(folder_)) (void)context_.memory->pin(folder_);
        if (context_.memoryChanged) context_.memoryChanged();
        load(true);
    }

    void FileExplorerDialog::showPathField(bool on) {
        pathMode_ = on;
        if (!pathField_) return;
        pathField_->setVisibility(on ? Visibility::Visible : Visibility::Collapsed);
        if (on) {
            pathField_->setText(folder_);
            (void)pathField_->focusAndSelectAll();
        } else if (table_) {
            table_->takeFocus();
        }
        if (body_) body_->invalidate();
    }

    void FileExplorerDialog::sortByColumn(std::size_t col) {
        const f::SortKey key = col == 1 ? f::SortKey::Modified : col == 2 ? f::SortKey::Type : col == 3 ? f::SortKey::Size : f::SortKey::Name;
        if (key == sort_) ascending_ = !ascending_;
        else {
            sort_ = key;
            ascending_ = key != f::SortKey::Modified;   // les dates : les plus recentes d'abord
        }
        const f::Entry* was = selectedEntry();
        const std::string keep = was ? was->name : std::string{};
        rebuildRows();
        if (!keep.empty()) (void)select(keep);
        sync();
    }

    // ------------------------------------------------------------- choisir ---
    void FileExplorerDialog::selectionChanged() {
        lastError_.clear();
        const f::Entry* e = selectedEntry();
        if (e && !e->folder && mode_ != f::PickMode::Folder && nameField_) {
            nameField_->setText(e->name);
            nameFromSelection_ = true;
        }
        sync();
    }

    void FileExplorerDialog::activateRow(std::size_t row) {
        if (row >= rows_.size()) {
            if (hiddenByFilter_ > 0) showAllFiltered(!showFiltered_);
            return;
        }
        const f::Entry e = rows_[row];
        if (e.folder) {
            (void)navigate(e.path);
            return;
        }
        if (mode_ == f::PickMode::Open) finish(e.path);
        else if (mode_ == f::PickMode::Save) setFileName(e.name);   // l'avertissement dit qu'il sera remplace
    }

    bool FileExplorerDialog::select(const std::string& name, std::string* why) {
        const std::string want = foldForSearch(trimmed(name));
        int found = -1, prefix = -1, prefixes = 0;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const std::string n = foldForSearch(rows_[i].name);
            if (n == want) {
                found = static_cast<int>(i);
                break;
            }
            if (!want.empty() && n.rfind(want, 0) == 0) {
                if (prefix < 0) prefix = static_cast<int>(i);
                ++prefixes;
            }
        }
        if (found < 0 && prefixes == 1) found = prefix;
        if (found < 0) {
            if (why) *why = prefixes > 1 ? "plusieurs \xC3\xA9l\xC3\xA9ments commencent ainsi" : "introuvable dans " + (folder_.empty() ? std::string("Ce PC") : folder_);
            return false;
        }
        if (table_) table_->selectModelRows({static_cast<RowIndex>(found)}, true);
        return true;
    }

    bool FileExplorerDialog::setFilter(const std::string& prefix, std::string* why) {
        if (mode_ == f::PickMode::Folder) {
            if (why) *why = "un dossier : pas de types";
            return false;
        }
        const int i = f::findFilter(filters_, prefix);
        if (i < 0) {
            if (why) *why = "aucun type ne commence ainsi";
            return false;
        }
        filterIndex_ = i;
        if (typeList_) typeList_->setSelectedIndex(i);
        showFiltered_ = false;
        rebuildRows();
        sync();
        return true;
    }

    void FileExplorerDialog::setThumbnails(bool on) {
        thumbnails_ = on;
        if (grid_ && table_) {
            const bool hadFocus = table_->focused() || grid_->focused();
            grid_->setVisibility(on ? Visibility::Visible : Visibility::Collapsed);
            table_->setVisibility(on ? Visibility::Collapsed : Visibility::Visible);
            if (hadFocus) {
                if (on) grid_->takeFocus();
                else table_->takeFocus();
            }
        }
        if (detailsButton_) detailsButton_->setStyle(on ? Button::Style::Default : Button::Style::Primary);
        if (thumbsButton_) thumbsButton_->setStyle(on ? Button::Style::Primary : Button::Style::Default);
        if (body_) body_->invalidate();
    }

    void FileExplorerDialog::setSearch(const std::string& text) {
        if (searchField_) searchField_->setText(text);   // textChanged refait la liste
        search_ = text;
        rebuildRows();
        sync();
    }

    void FileExplorerDialog::setFileName(const std::string& name) {
        if (!nameField_) return;
        nameField_->setText(name);
        nameFromSelection_ = false;
        lastError_.clear();
        sync();
    }

    void FileExplorerDialog::showAllFiltered(bool on) {
        showFiltered_ = on;
        rebuildRows();
        sync();
    }

    bool FileExplorerDialog::accept(std::string* why) {
        const auto fail = [this, why](std::string m) {
            if (why) *why = m;
            lastError_ = std::move(m);
            sync();
            return false;
        };
        lastError_.clear();
        const f::Entry* sel = selectedEntry();
        const std::string text = nameField_ ? trimmed(nameField_->text()) : std::string{};
        switch (mode_) {
            case f::PickMode::Folder: {
                const std::string target = sel && sel->folder ? sel->path : folder_;
                if (target.empty()) return fail("Choisis un dossier");
                finish(target);
                return true;
            }
            case f::PickMode::Open: {
                if (text.empty() || (sel && nameFromSelection_ && sel->name == text)) {
                    if (!sel) return fail("Choisis un fichier");
                    if (sel->folder) return navigate(sel->path, why);
                    finish(sel->path);
                    return true;
                }
                const std::string path = absolutePath(text) ? text : f::joinPath(folder_, text);
                if (isDir(path)) return navigate(path, why);
                if (!isFile(path)) return fail("Ce fichier n'existe pas : " + text);
                finish(path);
                return true;
            }
            case f::PickMode::Save: {
                if (text.empty() && sel && sel->folder) return navigate(sel->path, why);
                if (folder_.empty() && !absolutePath(text)) return fail("Choisis d'abord un dossier");
                if (isDir(absolutePath(text) ? text : f::joinPath(folder_, text))) return navigate(absolutePath(text) ? text : f::joinPath(folder_, text), why);
                const auto check = f::checkSaveName(folder_, text, currentFilter(), now_, filters_.empty() || filters_.front().all() ? std::string_view{} : std::string_view(filters_.front().extensions.front()));
                if (!check.error.empty()) return fail(check.error);
                finish(check.path);
                return true;
            }
        }
        return false;
    }

    void FileExplorerDialog::cancel() { finish(std::string{}); }

    void FileExplorerDialog::rememberView() {
        f::Memory::ViewState v;
        v.thumbnails = thumbnails_;
        v.sort = sort_;
        v.ascending = ascending_;
        if (table_)
            for (const auto& c : table_->columns()) v.widths.push_back(c.width);
        context_.memory->setView(kind_, std::move(v));
    }

    void FileExplorerDialog::finish(std::string path) {
        if (answered_) return;
        answered_ = true;
        rememberView();
        if (!path.empty()) context_.memory->remember(kind_, path, mode_ == f::PickMode::Folder);
        if (context_.memoryChanged) context_.memoryChanged();
        // D'abord fermer (la demande part dans la file), puis repondre : un
        // dialogue que la reponse ouvrirait se pose au-dessus, pas a la place.
        manager().CloseDialog(menu::DialogResult{path.empty() ? menu::DialogResult::Button::Cancel : menu::DialogResult::Button::Ok, path});
        auto done = std::move(done_);
        done_ = nullptr;
        if (done) done(std::move(path));
    }

    // ------------------------------------------------------------ montrer ---
    std::string FileExplorerDialog::primaryLabel() const {
        switch (mode_) {
            case f::PickMode::Folder: {
                const f::Entry* sel = selectedEntry();
                const std::string target = sel && sel->folder ? sel->path : folder_;
                if (target.empty()) return "Choisir ce dossier";
                return "Choisir \xC2\xAB " + f::breadcrumb(target).back().label + " \xC2\xBB";
            }
            case f::PickMode::Save: return saveExists_ ? "Remplacer" : "Enregistrer";
            case f::PickMode::Open: return "Ouvrir";
        }
        return "Ouvrir";
    }

    std::string FileExplorerDialog::message() const {
        if (!lastError_.empty()) return lastError_;
        if (!saveMessage_.empty()) return saveMessage_;
        return notice_;
    }

    std::string FileExplorerDialog::statusText() const {
        std::string s = folder_.empty() ? plural(rows_.size(), "emplacement", "emplacements")
                                        : plural(rows_.size(), "\xC3\xA9l\xC3\xA9ment", "\xC3\xA9l\xC3\xA9ments");
        if (const auto* flt = currentFilter()) s += " \xC2\xB7 filtre : " + flt->name;
        if (!search_.empty()) s += " \xC2\xB7 cherch\xC3\xA9 : " + search_;
        if (!space_.empty()) s += " \xC2\xB7 " + space_;
        return s;
    }

    std::vector<std::string> FileExplorerDialog::lines() const {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const auto& e = rows_[i];
            out.push_back(std::string(e.folder ? "[D] " : "[F] ") + e.name + (e.folder ? std::string{} : " | " + f::typeLabel(e.name, false)));
        }
        if (model_ && model_->hasNoteRow()) out.push_back("(" + model_->noteText() + ")");
        return out;
    }

    void FileExplorerDialog::sync() {
        // Le nom a enregistrer : existe deja, impossible.
        saveExists_ = false;
        saveMessage_.clear();
        messageIsError_ = !lastError_.empty();
        bool canAccept = true;
        if (mode_ == f::PickMode::Save) {
            const std::string text = nameField_ ? trimmed(nameField_->text()) : std::string{};
            if (text.empty()) {
                const f::Entry* sel = selectedEntry();
                canAccept = sel && sel->folder;
            } else if (!folder_.empty() || absolutePath(text)) {
                const auto check = f::checkSaveName(folder_, text, currentFilter(), now_,
                                                    filters_.empty() || filters_.front().all() ? std::string_view{}
                                                                                               : std::string_view(filters_.front().extensions.front()));
                if (!check.error.empty() && !isDir(absolutePath(text) ? text : f::joinPath(folder_, text))) {
                    saveMessage_ = check.error;
                    messageIsError_ = true;
                    canAccept = false;
                } else if (check.exists) {
                    saveExists_ = true;
                    saveMessage_ = check.warning;
                }
            } else {
                canAccept = false;
            }
        } else if (mode_ == f::PickMode::Open) {
            const std::string text = nameField_ ? trimmed(nameField_->text()) : std::string{};
            canAccept = !text.empty() || selectedEntry() != nullptr;
        } else {
            canAccept = !folder_.empty() || (selectedEntry() && selectedEntry()->folder);
        }
        if (!notice_.empty() && saveMessage_.empty() && lastError_.empty()) messageIsError_ = true;
        if (okButton_) {
            const std::string label = primaryLabel();
            if (okButton_->text() != label) {
                okButton_->setText(label);
                if (body_) body_->invalidateLayout();
            }
            okButton_->setEnabled(canAccept);
        }
        if (backButton_) backButton_->setEnabled(!backStack_.empty());
        if (forwardButton_) forwardButton_->setEnabled(!forwardStack_.empty());
        if (upButton_) upButton_->setEnabled(!folder_.empty());
        if (newFolderButton_) newFolderButton_->setEnabled(!folder_.empty() && listing_.error == f::ListError::None);
        if (pinButton_) {
            const bool pinned = !folder_.empty() && context_.memory->pinned(folder_);
            pinButton_->setIcon(pinned ? Icon::StarFilled : Icon::Star);
            pinButton_->setTooltip(pinned ? "D\xC3\xA9s\xC3\xA9pingler ce dossier" : "\xC3\x89pingler ce dossier (il revient dans \xC3\x89PINGL\xC3\x89S)");
            pinButton_->setEnabled(!folder_.empty());
        }
        if (table_) schedulePreview();   // ---- Lot API 8 : l'explorateur, 2e partie ----
        if (body_) body_->invalidate();
    }

    // ------------------------------------------------------------- clavier ---
    void FileExplorerDialog::Update(const menu::FrameContext& fc) {
        clock_ = fc.totalSeconds;
        if (fc.theme) theme_ = fc.theme;
        // ---- Lot API 8 : l'explorateur, 2e partie : l'apercu et les vignettes, en differe ----
        if (previewShown_ && previewDue_ >= 0.0 && clock_ >= previewDue_) computePreview();
        decodeThumbnails();
        // ---- fin Lot API 8 : l'explorateur, 2e partie ----
        menu::WidgetMenu::Update(fc);
    }

    // ---- Lot API 8 : l'explorateur, 2e partie (l'apercu, les vignettes) ----
    void FileExplorerDialog::setPreviewShown(bool on) {
        previewShown_ = on;
        g_previewFolded = !on;
        if (on) schedulePreview();
        if (body_) body_->invalidateLayout();
        if (previewView_) previewView_->invalidate();
    }

    void FileExplorerDialog::schedulePreview() {
        const f::Entry* e = selectedEntry();
        const std::string path = e && e->family != f::Family::Drive ? e->path : std::string{};
        if (path == previewWanted_ && (previewDue_ >= 0.0 || preview_.path == path)) return;
        previewWanted_ = path;
        preview_ = {};
        previewDue_ = path.empty() ? -1.0 : clock_ + 0.15;   // on attend que la selection se pose (fleches tenues)
        if (previewView_) previewView_->invalidate();
    }

    void FileExplorerDialog::computePreview() {
        previewDue_ = -1.0;
        preview_ = {};
        if (previewWanted_.empty()) return;
        if (context_.preview) preview_ = context_.preview(previewWanted_);
        preview_.path = previewWanted_;
        if (preview_.name.empty()) {
            const auto cut = previewWanted_.find_last_of("/\\");
            preview_.name = cut == std::string::npos ? previewWanted_ : previewWanted_.substr(cut + 1);
        }
        if (preview_.kind.empty()) {
            preview_.folder = isDir(previewWanted_);
            preview_.kind = f::typeLabel(preview_.name, preview_.folder);
        }
        if (previewView_) previewView_->invalidate();
    }

    const FilePreviewInfo& FileExplorerDialog::currentPreview() {
        schedulePreview();
        if (previewDue_ >= 0.0) computePreview();
        return preview_;
    }

    std::vector<std::string> FileExplorerDialog::previewLines() {
        const FilePreviewInfo& p = currentPreview();
        std::vector<std::string> out;
        if (!previewShown_) out.emplace_back("(aper\xC3\xA7u repli\xC3\xA9)");
        if (p.path.empty()) {
            out.emplace_back("(rien de choisi)");
            return out;
        }
        out.push_back(p.name + " | " + p.kind);
        if (p.image) out.push_back("vignette " + std::to_string(p.image->width) + " x " + std::to_string(p.image->height));
        if (!p.error.empty()) out.push_back("erreur : " + p.error);
        for (const auto& [key, value] : p.facts) out.push_back(key + " : " + value);
        for (const auto& note : p.notes) out.push_back("! " + note);
        if (!p.sheets.empty()) {
            std::string s = "onglets :";
            for (std::size_t i = 0; i < p.sheets.size(); ++i) s += (i ? ", " : " ") + p.sheets[i];
            out.push_back(std::move(s));
        }
        for (const auto& row : p.cells) {
            std::string s = "#";
            for (const auto& cell : row) s += " " + cell + " |";
            out.push_back(std::move(s));
        }
        for (const auto& line : p.text) out.push_back("> " + line);
        if (p.openable) out.emplace_back("[Ouvrir avec le programme du syst\xC3\xA8me]");
        return out;
    }

    std::string FileExplorerDialog::openSelectedExternally() {
        const f::Entry* e = selectedEntry();
        if (!e || e->folder) return "choisis d'abord un fichier";
        if (!context_.openExternally) return "pas de programme du syst\xC3\xA8me ici";
        return context_.openExternally(e->path);
    }

    void FileExplorerDialog::decodeThumbnails() {
        if (!thumbnails_ || !context_.thumbnail) {
            thumbWanted_.clear();
            return;
        }
        if (thumbWanted_.empty()) return;
        if (thumbs_.size() > 800) thumbs_.clear();   // un tres grand dossier : on repart
        // Au plus ~40 ms par image (une grosse photo JPEG en prend presque autant) :
        // au moins une vignette, puis tant que le temps le permet (6 au plus).
        const auto start = std::chrono::steady_clock::now();
        int budget = 6;
        for (const auto& path : thumbWanted_) {
            if (thumbs_.count(path) != 0) continue;
            if (budget-- <= 0 || (budget < 5 && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(40))) break;
            thumbs_[path] = context_.thumbnail(path, 160);
        }
        thumbWanted_.clear();
        if (grid_) grid_->invalidate();
    }

    namespace {
        // Sans casse ni accents (E accent aigu / E majuscule accent : e) : "Epingler" choisit l'entree.
        std::string menuKey(std::string s) {
            for (const std::string_view acc : {std::string_view("\xC3\x89"), std::string_view("\xC3\xA9")})
                for (auto at = s.find(acc); at != std::string::npos; at = s.find(acc, at)) s.replace(at, acc.size(), "e");
            for (auto& ch : s)
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            return s;
        }
        int menuId(const std::string& label) { return label.rfind("Copier", 0) == 0 ? 3 : label.rfind("Ouvrir", 0) == 0 ? 2 : 1; }
    } // namespace

    std::vector<std::string> FileExplorerDialog::rowMenuLabels() const {
        std::vector<std::string> out;
        const f::Entry* e = selectedEntry();
        if (!e) return out;
        if (e->folder || e->family == f::Family::Drive)
            out.emplace_back(context_.memory && context_.memory->pinned(e->path) ? "D\xC3\xA9s\xC3\xA9pingler" : "\xC3\x89pingler");
        else
            out.emplace_back("Ouvrir avec le programme du syst\xC3\xA8me");
        out.emplace_back("Copier le chemin");
        return out;
    }

    void FileExplorerDialog::openRowMenu(gfx::Point at) {
        if (!rowMenu_ || !selectedEntry()) return;
        std::vector<PopupMenu::Item> items;
        for (const auto& label : rowMenuLabels()) {
            PopupMenu::Item it;
            it.label = label;
            it.id = menuId(label);
            if (it.id == 2 && !context_.openExternally) {
                it.enabled = false;
                it.disabledReason = "pas de programme du syst\xC3\xA8me ici";
            }
            items.push_back(std::move(it));
        }
        rowMenu_->setItems(std::move(items));
        const gfx::Rect r = body_ ? body_->bounds() : gfx::Rect{};
        rowMenu_->openAt(at, {r.right(), r.bottom()});
    }

    bool FileExplorerDialog::rowMenuChoose(const std::string& prefix, std::string* why) {
        const auto labels = rowMenuLabels();
        if (labels.empty()) {
            if (why) *why = "rien de choisi";
            return false;
        }
        for (const auto& label : labels) {
            if (menuKey(label).rfind(menuKey(prefix), 0) != 0) continue;
            rowMenuAction(menuId(label));
            return true;
        }
        if (why) {
            *why = "pas dans le menu :";
            for (const auto& l : labels) *why += " \xC2\xAB " + l + " \xC2\xBB";
        }
        return false;
    }

    void FileExplorerDialog::rowMenuAction(int id) {
        const f::Entry* e = selectedEntry();
        if (!e) return;
        const std::string path = e->path;
        if (id == 1) {
            if (!context_.memory) return;
            if (!context_.memory->unpin(path)) (void)context_.memory->pin(path);
            if (context_.memoryChanged) context_.memoryChanged();
            load(true);
        } else if (id == 2) {
            notice_ = openSelectedExternally();   // "" : ouvert
            sync();
        } else if (id == 3) {
            ui::setClipboardText(path);
        }
    }

    bool FileExplorerDialog::chooseSystemExplorer(std::string* why) {
        if (!context_.useSystemExplorer) {
            if (why) *why = "pas de r\xC3\xA9glage ici";
            return false;
        }
        context_.useSystemExplorer();
        systemChosen_ = true;
        if (body_) body_->invalidate();
        return true;
    }

    void FileExplorerDialog::setSearchSubfolders(bool on) {
        if (deepBox_ && deepBox_->isChecked() != on) deepBox_->setState(on ? Checkbox::State::Checked : Checkbox::State::Unchecked);
        if (searchDeep_ == on) return;
        searchDeep_ = on;
        rebuildRows();
        sync();
    }

    std::vector<f::Entry> FileExplorerDialog::deepEntries() const {
        // En largeur, borne : un disque entier ne fige pas la fenetre.
        std::vector<f::Entry> all = listing_.entries;
        std::vector<std::string> queue;
        for (const auto& e : listing_.entries)
            if (e.folder) queue.push_back(e.path);
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t q = 0; q < queue.size(); ++q) {
            if (all.size() > 5000 || std::chrono::steady_clock::now() - start > std::chrono::milliseconds(300)) break;
            const f::Listing sub = f::listFolder(queue[q]);
            if (sub.error != f::ListError::None) continue;
            for (const auto& e : sub.entries) {
                if (e.folder) queue.push_back(e.path);
                all.push_back(e);
            }
        }
        return all;
    }
    // ---- fin Lot API 8 : l'explorateur, 2e partie ----

    ui::EventResult FileExplorerDialog::HandleEvent(const ui::InputEvent& ev) {
        if (const auto* k = std::get_if<KeyDown>(&ev)) {
            const bool typing = (nameField_ && nameField_->focused()) || (searchField_ && searchField_->focused())
                             || (pathField_ && pathField_->focused());
            const bool listOpen = (typeList_ && typeList_->isOpen()) || (pathField_ && pathField_->suggestionsOpen())
                               || (nameField_ && nameField_->suggestionsOpen());
            if (!listOpen) {
                if (k->key == Key::Escape) {
                    if (pathMode_) {
                        showPathField(false);
                        return EventResult::Consumed;
                    }
                    if (searchField_ && searchField_->focused() && !searchField_->text().empty())
                        return menu::WidgetMenu::HandleEvent(ev);   // Echap efface la recherche d'abord
                    cancel();
                    return EventResult::Consumed;
                }
                if (k->key == Key::L && k->mods.ctrl) {
                    showPathField(true);
                    return EventResult::Consumed;
                }
                if (k->key == Key::F && k->mods.ctrl) {
                    if (searchField_) (void)searchField_->focusAndSelectAll();
                    return EventResult::Consumed;
                }
                if (k->key == Key::F5) {
                    refresh();
                    return EventResult::Consumed;
                }
                if (k->key == Key::Backspace && !typing) {
                    (void)up();
                    return EventResult::Consumed;
                }
                if (k->key == Key::Return) {
                    if (pathMode_ && pathField_ && pathField_->focused()) {
                        const std::string typed = trimmed(pathField_->text());
                        std::string why;
                        if (navigate(typed, &why)) {
                            // Un partage reseau tape revient dans les recents.
                            if (typed.size() > 2 && (typed[0] == '\\' || typed[0] == '/') && (typed[1] == '\\' || typed[1] == '/')) {
                                context_.memory->rememberFolder(kind_, folder_);
                                if (context_.memoryChanged) context_.memoryChanged();
                                load(true);
                            }
                        }
                        return EventResult::Consumed;
                    }
                    if (searchField_ && searchField_->focused()) {
                        if (table_) table_->takeFocus();
                        return EventResult::Consumed;
                    }
                    if (table_ && table_->focused()) {
                        const auto sel = table_->selectedModelRows();
                        if (!sel.empty()) activateRow(sel.front());
                        return EventResult::Consumed;
                    }
                    if (okButton_ && okButton_->enabled()) (void)accept();
                    return EventResult::Consumed;
                }
            }
        }
        // Les premieres lettres, dans la liste : l'element qui commence ainsi.
        if (const auto* t = std::get_if<TextInput>(&ev); t && table_ && table_->focused()) {
            if (clock_ - typedAt_ > 1.0) typed_.clear();
            typedAt_ = clock_;
            typed_ += t->utf8;
            const auto sel = table_->selectedModelRows();
            const int from = typed_.size() == t->utf8.size() && !sel.empty() ? static_cast<int>(sel.front()) + 1 : sel.empty() ? 0 : static_cast<int>(sel.front());
            const int i = f::typeAhead(rows_, typed_, from);
            if (i >= 0) table_->selectModelRows({static_cast<RowIndex>(i)}, true);
            return EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }

} // namespace ui
