// =============================================================================
//  app/hmi/HmiFontLibraryPane.cpp - 1.26.0 : la bibliotheque de polices (voir HmiFontLibraryPane.hpp)
// =============================================================================
#include "HmiFontLibraryPane.hpp"

#include "HmiImages.hpp"            // les textes ecrits dans une police (HmiImageCache::fontText)
#include "HmiPaneKit.hpp"
#include "HmiPanels.hpp"
#include "../../core/Command.hpp"   // CommandGroupScope (une famille : plusieurs fichiers, un Ctrl+Z)
#include "../../hmi/HmiGuide.hpp"   // fold (la recherche des polices du projet)
#include "../../ui/Icons.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/SearchField.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>

namespace app {

using hmi::Id;
using hmi::kNoId;
using namespace hmikit;
namespace fl = hmi::fontlib;

namespace {

// La phrase courte d'une description (avant les deux-points, la parenthese, le point) : << Les memes chasses qu'Arial >>.
std::string shortDescription(const std::string& d) {
    std::size_t end = d.size();
    for (const char* stop : {" :", " (", "."})
        if (const auto at = d.find(stop); at != std::string::npos) end = std::min(end, at);
    return d.substr(0, end);
}

// En capitales, les lettres accentuees du latin aussi (<< Bibliotheque >> -> BIBLIOTHEQUE, son accent garde).
std::string upperAscii(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            s[i] = static_cast<char>(std::toupper(c));
        } else if (c == 0xC3 && i + 1 < s.size()) {
            const auto n = static_cast<unsigned char>(s[i + 1]);
            if (n >= 0xA0 && n <= 0xBE && n != 0xB7) s[i + 1] = static_cast<char>(n - 0x20);   // a grave... y aigu ; pas le signe divise
            ++i;
        }
    }
    return s;
}

std::string thousands(int n) {
    std::string s = std::to_string(std::max(0, n));
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), " ");
    return s;
}

// Un texte ecrit DANS UNE POLICE (une image blanche, teintee) a `at` (en haut a gauche) ; rend sa largeur, ou -1 : pas
// d'image (le renderer n'en fait pas, une police illisible) - l'appelant ecrit alors avec la police de l'interface.
float drawInFont(gfx::IRenderer& r, const hmi::BlobPtr& data, const std::string& format, std::string_view text, float px,
                 gfx::Point at, gfx::Color color) {
    if (!data || text.empty()) return -1.f;
    hmi::Resource res;
    res.format = format.empty() ? std::string("TTF") : format;
    res.data = data;
    const auto img = HmiImageCache::instance().fontText(r, res, text, px);
    if (!img.valid()) return -1.f;
    r.drawImage(img.tex, {at.x, at.y, static_cast<float>(img.width), static_cast<float>(img.height)}, 0.f, false, false, color);
    return static_cast<float>(img.width);
}

// Ecrit dans sa police, sinon dans celle de l'interface (a la meme taille) ; rend la largeur.
float writeIn(gfx::IRenderer& r, const hmi::BlobPtr& data, const std::string& format, std::string_view text, float px, gfx::Point at,
               gfx::Color color) {
    const float w = drawInFont(r, data, format, text, px, at, color);
    if (w >= 0.f) return w;
    const gfx::FontId f{static_cast<std::uint16_t>(std::clamp(px, 8.f, 120.f))};
    r.drawText(at, text, f, color);
    return r.measure(text, f).width;
}

// Les glyphes de l'apercu (ceux que la police a).
const std::u32string& glyphCodes() {
    static const std::u32string all = [] {
        // En UTF-8 (le source reste en ASCII), lu en points de code.
        const std::string_view utf8 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                                      "\xC3\x80\xC3\x82\xC3\x84\xC3\x87\xC3\x89\xC3\x88\xC3\x8A\xC3\x8B\xC3\x8E\xC3\x8F\xC3\x94\xC3\x96\xC3\x99\xC3\x9B\xC3\x9C"
                                      "\xC3\xA0\xC3\xA2\xC3\xA4\xC3\xA7\xC3\xA9\xC3\xA8\xC3\xAA\xC3\xAB\xC3\xAE\xC3\xAF\xC3\xB4\xC3\xB6\xC3\xB9\xC3\xBB\xC3\xBC\xC3\xBF\xC5\x93\xC5\x92\xC3\xA6"
                                      "\xE2\x82\xAC\xC2\xB0\xC2\xB1\xC2\xB5\xC2\xB2\xC2\xB3\xC3\x97\xC3\xB7\xCE\xA9\xE2\x86\x92\xE2\x86\x90\xE2\x86\x91\xE2\x86\x93\xE2\x80\xA6\xC2\xAB\xC2\xBB\xE2\x80\x94\xC2\xB7%&@#";
        std::u32string out;
        for (std::size_t i = 0; i < utf8.size();) {
            const auto c = static_cast<unsigned char>(utf8[i]);
            const int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            char32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
            for (int k = 1; k < len && i + static_cast<std::size_t>(k) < utf8.size(); ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + static_cast<std::size_t>(k)]) & 0x3F);
            out.push_back(cp);
            i += static_cast<std::size_t>(len);
        }
        return out;
    }();
    return all;
}

std::string utf8Of(char32_t c) {
    std::string out;
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

// RETIRER UNE DE MES POLICES, CTRL+Z LA REND : la commande garde son contenu (le nom pris entretemps par un autre
// contenu : "Nom (2).ttf", la regle de Mes polices). Une famille : une commande par fichier, dans un groupe.
class MyFontRemoveCommand final : public core::ICommand {
public:
    MyFontRemoveCommand(fl::Font font, std::function<void()> refresh) : font_(std::move(font)), refresh_(std::move(refresh)) {}
    core::Status execute() override {
        std::string why;
        if (!fl::removeUserFont(font_, &why)) return core::fail(core::ErrorCode::FileUnreadable, why);
        if (refresh_) refresh_();
        return core::ok();
    }
    core::Status undo() override {
        const auto slash = font_.file.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? font_.file : font_.file.substr(slash + 1);
        std::string stored, why;
        if (!font_.data || !fl::addUserFont(name, *font_.data, &stored, nullptr, &why))
            return core::fail(core::ErrorCode::FileUnreadable, "rendre " + font_.fullName() + " : " + (why.empty() ? std::string("contenu perdu") : why));
        if (slash != std::string::npos) font_.file = font_.file.substr(0, slash + 1) + stored;
        font_.id = "mes/" + stored;
        if (refresh_) refresh_();
        return core::ok();
    }
    [[nodiscard]] std::string label() const override { return "Supprimer " + font_.fullName() + " de Mes polices"; }

private:
    fl::Font              font_;
    std::function<void()> refresh_;
};

} // namespace

// ============================================================================================================== la liste ===
//  Une ligne par famille, son nom ecrit dans sa police ; les en-tetes des dossiers ; en etroit, << Du projet >> d'abord.
class HmiFontList final : public ui::Widget {
public:
    enum class Kind : std::uint8_t { Header, Family, Project, Hint, Section };
    struct Row {
        Kind          kind{Kind::Hint};
        std::string   category;            // Header, Family
        std::size_t   count{0};            // Header, Section
        int           family{-1};          // Family : rang dans les familles du volet
        std::string   resource;            // Project
        std::string   fid;                 // Family : l'id de son style Normal (la cle de la ligne)
        std::string   name, sub;           // ce qui s'ecrit
        hmi::BlobPtr  data;                // la police qui l'ecrit
        std::string   format;
        bool          inProject{false};
        bool          mine{false};
        int           open{-1};            // Header (etroit) : 1 deplie, 0 replie
        float         y{0}, h{0};
        [[nodiscard]] std::string key() const {
            return kind == Kind::Project ? "res:" + resource : kind == Kind::Family ? "fam:" + fid : std::string{};
        }
    };

    HmiFontList(std::string id, HmiFontLibraryPane& pane) : ui::Widget(std::move(id)), pane_(pane) { setFocusPolicy(true); }

    void setRows(std::vector<Row> rows) {
        rows_ = std::move(rows);
        float y = 0;
        for (auto& r : rows_) {
            r.y = y;
            y += r.h;
        }
        content_ = y;
        // Ce qui etait choisi et n'est plus montre part ; rien de choisi : la premiere famille.
        std::set<std::string> keep;
        for (const auto& r : rows_)
            if (chosen_.count(r.key())) keep.insert(r.key());
        chosen_ = std::move(keep);
        if (!current_.empty() && !rowOf(current_)) current_.clear();
        if (current_.empty())
            for (const auto& r : rows_)
                if (r.kind == Kind::Family || r.kind == Kind::Project) {
                    current_ = r.key();
                    chosen_ = {current_};
                    break;
                }
        clampScroll();
        invalidate();
    }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] const Row* rowOf(const std::string& key) const {
        for (const auto& r : rows_)
            if (!key.empty() && r.key() == key) return &r;
        return nullptr;
    }
    [[nodiscard]] const Row* currentRow() const { return rowOf(current_); }
    [[nodiscard]] const std::set<std::string>& chosen() const noexcept { return chosen_; }
    void choose(const std::string& key, bool reveal = true) {
        current_ = key;
        anchor_ = key;
        chosen_ = {key};
        if (reveal) revealKey(key);
        invalidate();
    }
    void chooseMany(const std::vector<std::string>& keys) {
        chosen_ = std::set<std::string>(keys.begin(), keys.end());
        current_ = keys.empty() ? std::string{} : keys.front();
        anchor_ = current_;
        invalidate();
    }
    [[nodiscard]] bool rectOf(const std::string& key, gfx::Rect& out) const {
        const Row* r = rowOf(key);
        if (!r) return false;
        const auto b = bounds();
        out = {b.x, b.y + r->y - scroll_, b.w, r->h};
        return out.bottom() > b.y && out.y < b.bottom();
    }
    void revealKey(const std::string& key) {
        const Row* r = rowOf(key);
        if (!r) return;
        const float h = bounds().h;
        // En remontant : l'en-tete de son dossier (juste au-dessus) se voit aussi.
        float top = r->y;
        for (std::size_t i = 1; i < rows_.size(); ++i)
            if (&rows_[i] == r && rows_[i - 1].kind != Kind::Family && rows_[i - 1].kind != Kind::Project) top = rows_[i - 1].y;
        if (top < scroll_) scroll_ = top;
        else if (h > 0 && r->y + r->h > scroll_ + h) scroll_ = r->y + r->h - h;
        clampScroll();
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(b, c.panelBg);
        r.pushClip(b);
        const bool compact = pane_.compact_;
        for (const auto& row : rows_) {
            const gfx::Rect rr{b.x, b.y + row.y - scroll_, b.w, row.h};
            if (rr.bottom() < b.y || rr.y > b.bottom()) continue;
            const bool chosen = chosen_.count(row.key()) != 0 && !row.key().empty();
            switch (row.kind) {
                case Kind::Section:
                case Kind::Header: {
                    if (!compact || row.kind == Kind::Section) {
                        r.fillRect(rr, c.headerBg);
                        r.fillRect({rr.x, rr.bottom() - 1.f, rr.w, 1.f}, c.border);
                        std::string t = upperAscii(row.name);
                        if (row.kind == Kind::Header || row.count > 0) t += "  \xC2\xB7  " + std::to_string(row.count);
                        r.drawText({rr.x + 14.f, rr.y + (rr.h - r.lineHeight(ctx.theme.font.smallUi)) / 2}, t, ctx.theme.font.smallUi, c.textMuted);
                        break;
                    }
                    // En etroit : un dossier qui se replie, son nombre a droite.
                    const float cy = rr.y + rr.h / 2;
                    const gfx::Color arrow = c.textMuted;
                    if (row.open == 1) {
                        r.line({rr.x + 12.f, cy - 2.f}, {rr.x + 16.f, cy + 2.f}, arrow, 1.5f);
                        r.line({rr.x + 16.f, cy + 2.f}, {rr.x + 20.f, cy - 2.f}, arrow, 1.5f);
                    } else {
                        r.line({rr.x + 14.f, cy - 4.f}, {rr.x + 18.f, cy}, arrow, 1.5f);
                        r.line({rr.x + 18.f, cy}, {rr.x + 14.f, cy + 4.f}, arrow, 1.5f);
                    }
                    ui::drawIcon(r, row.category == fl::kMine ? ui::Icon::User : ui::Icon::Lock, {rr.x + 26.f, cy - 8.f, 16.f, 16.f},
                                 row.category == fl::kMine ? c.accent : c.warning);
                    r.drawText({rr.x + 48.f, cy - r.lineHeight(ctx.theme.font.ui) / 2}, row.name, ctx.theme.font.ui, c.text);
                    const std::string n = std::to_string(row.count);
                    r.drawText({rr.right() - 14.f - r.measure(n, ctx.theme.font.smallUi).width, cy - r.lineHeight(ctx.theme.font.smallUi) / 2},
                               n, ctx.theme.font.smallUi, c.textMuted);
                    break;
                }
                case Kind::Hint:
                    r.drawText({rr.x + (compact ? 14.f : 28.f), rr.y + (rr.h - r.lineHeight(ctx.theme.font.smallUi)) / 2}, row.name,
                               ctx.theme.font.smallUi, c.textMuted);
                    break;
                case Kind::Project:
                case Kind::Family: {
                    if (chosen) {
                        r.fillRect(rr, c.accent.withAlpha(40));
                        r.fillRect({rr.x, rr.y, 3.f, rr.h}, c.accent);
                    } else if (hover_ == row.key()) {
                        r.fillRect(rr, c.rowAltBg);
                    }
                    r.fillRect({rr.x, rr.bottom() - 1.f, rr.w, 1.f}, c.border.withAlpha(120));
                    if (compact) paintCompact(ctx, row, rr);
                    else paintFull(ctx, row, rr);
                    break;
                }
            }
        }
        r.popClip();
        if (rows_.empty())
            r.drawText({b.x + 14.f, b.y + 14.f}, "Aucune police ne r\xC3\xA9pond \xC3\xA0 la recherche.", ctx.theme.font.ui, c.textMuted);
    }

    void paintFull(const ui::PaintContext& ctx, const Row& row, const gfx::Rect& rr) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        // Le nom, dans sa police ; dessous, un cadenas (fournie) ou une personne (a toi), la phrase, les styles.
        const float nameW = writeIn(r, row.data, row.format, row.name, 26.f, {rr.x + 16.f, rr.y + 10.f}, c.text);
        const float subY = rr.y + rr.h - 26.f;
        ui::drawIcon(r, row.mine ? ui::Icon::User : ui::Icon::Lock, {rr.x + 16.f, subY + 1.f, 14.f, 14.f}, row.mine ? c.accent : c.warning);
        r.drawText({rr.x + 36.f, subY}, row.sub, ctx.theme.font.smallUi, c.textMuted);
        const float subW = 20.f + r.measure(row.sub, ctx.theme.font.smallUi).width;
        // Le texte d'apercu, dans sa police, a la taille choisie ; << dans le projet >> au bout.
        const float tagW = row.inProject ? r.measure("dans le projet", ctx.theme.font.smallUi).width + 16.f : 0.f;
        const float sx = rr.x + 16.f + std::max(nameW, subW) + 36.f;
        const float room = rr.right() - sx - (tagW > 0 ? tagW + 24.f : 12.f);
        if (room > 40.f) {
            const float px = static_cast<float>(pane_.size_);
            r.pushClip({sx, rr.y, room, rr.h});
            (void)writeIn(r, row.data, row.format, pane_.sample_, px, {sx, rr.y + (rr.h - px * 1.25f) / 2}, c.text);
            r.popClip();
        }
        if (row.inProject) {
            const gfx::Rect tag{rr.right() - tagW - 14.f, rr.y + (rr.h - 22.f) / 2, tagW, 22.f};
            r.fillRoundedRect(tag, c.ok.withAlpha(40), 4.f);
            r.drawText({tag.x + 8.f, tag.y + (tag.h - r.lineHeight(ctx.theme.font.smallUi)) / 2}, "dans le projet", ctx.theme.font.smallUi, c.ok);
        }
    }

    void paintCompact(const ui::PaintContext& ctx, const Row& row, const gfx::Rect& rr) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const float indent = row.kind == Kind::Family ? 24.f : 10.f;
        // << Aa >> dans une case, ecrit dans la police ; a droite, son nom (dans sa police) et une ligne grise.
        const gfx::Rect box{rr.x + indent, rr.y + (rr.h - 36.f) / 2, 40.f, 36.f};
        r.fillRoundedRect(box, c.inputBg, 4.f);
        r.strokeRect(box, c.border, 1.f);
        (void)writeIn(r, row.data, row.format, "Aa", 20.f, {box.x + 7.f, box.y + 6.f}, c.text);
        const float tx = box.right() + 10.f;
        r.pushClip({tx, rr.y, std::max(0.f, rr.right() - tx - 6.f), rr.h});
        (void)writeIn(r, row.data, row.format, row.name, 17.f, {tx, rr.y + 5.f}, c.text);
        const float subX = tx + (row.mine ? 16.f : 0.f);
        if (row.mine) ui::drawIcon(r, ui::Icon::User, {tx, rr.bottom() - 19.f, 12.f, 12.f}, c.accent);
        r.drawText({subX, rr.bottom() - 21.f}, row.sub, ctx.theme.font.smallUi, row.inProject && row.kind == Kind::Family ? c.ok : c.textMuted);
        r.popClip();
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const auto b = bounds();
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && b.contains(w->pos)) {
            scroll_ -= w->dy * 60.f;
            clampScroll();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const Row* row = b.contains(m->pos) ? rowAt(m->pos) : nullptr;
            const std::string k = row ? row->key() : std::string{};
            if (k != hover_) {
                hover_ = k;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && b.contains(m->pos)) {
            grabFocus();
            const Row* row = rowAt(m->pos);
            if (!row) return ui::EventResult::Consumed;
            if (row->kind == Kind::Header) {
                if (m->button == ui::MouseButton::Left) pane_.setCategoryOpen(row->category, !pane_.categoryOpen(row->category));
                return ui::EventResult::Consumed;
            }
            if (row->kind != Kind::Family && row->kind != Kind::Project) return ui::EventResult::Consumed;
            const std::string key = row->key();
            if (m->button == ui::MouseButton::Right) {
                if (!chosen_.count(key)) choose(key, false);
                pane_.rowChosen();
                pane_.openMenu(m->pos);
                return ui::EventResult::Consumed;
            }
            if (m->button != ui::MouseButton::Left) return ui::EventResult::Consumed;
            if (m->mods.ctrl && !pane_.compact_) {
                if (chosen_.count(key) && chosen_.size() > 1) chosen_.erase(key);
                else chosen_.insert(key);
                current_ = key;
                anchor_ = key;
            } else if (m->mods.shift && !pane_.compact_ && rowOf(anchor_)) {
                const float a = rowOf(anchor_)->y, z = row->y;
                chosen_.clear();
                for (const auto& x : rows_)
                    if ((x.kind == Kind::Family || x.kind == Kind::Project) && x.y >= std::min(a, z) && x.y <= std::max(a, z)) chosen_.insert(x.key());
                current_ = key;
            } else {
                choose(key, false);
            }
            invalidate();
            pane_.rowChosen();
            if (m->clickCount >= 2) pane_.rowActivated();
            else if (pane_.compact_) pane_.picked->emit(pane_.currentPick());   // le glisser commence
            return ui::EventResult::Consumed;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused()) {
            const bool plain = !k->mods.ctrl && !k->mods.alt && !k->mods.shift;
            if ((k->key == ui::Key::Up || k->key == ui::Key::Down) && plain) {
                std::vector<const Row*> items;
                for (const auto& x : rows_)
                    if (x.kind == Kind::Family || x.kind == Kind::Project) items.push_back(&x);
                if (items.empty()) return ui::EventResult::Consumed;
                std::size_t at = 0;
                for (std::size_t i = 0; i < items.size(); ++i)
                    if (items[i]->key() == current_) at = i;
                if (k->key == ui::Key::Up && at > 0) --at;
                else if (k->key == ui::Key::Down && at + 1 < items.size()) ++at;
                choose(items[at]->key());
                pane_.rowChosen();
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Return && plain) {
                pane_.rowActivated();
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Delete && plain && !pane_.compact_) {
                pane_.runAction(HmiFontLibraryPane::ActRemove);
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::C && k->mods.ctrl && !k->mods.alt && !k->mods.shift) {
                pane_.runAction(HmiFontLibraryPane::ActCopy);
                return ui::EventResult::Consumed;
            }
            if (ui::isContextMenuKey(*k)) {
                gfx::Rect rr;
                if (rectOf(current_, rr)) pane_.openMenu({rr.x + 40.f, rr.bottom()});
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }

    void onLayout() override { clampScroll(); }

private:
    [[nodiscard]] const Row* rowAt(gfx::Point p) const {
        const float y = p.y - bounds().y + scroll_;
        for (const auto& r : rows_)
            if (y >= r.y && y < r.y + r.h) return &r;
        return nullptr;
    }
    void clampScroll() { scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - bounds().h)); }

    HmiFontLibraryPane&   pane_;
    std::vector<Row>      rows_;
    std::set<std::string> chosen_;
    std::string           current_, anchor_, hover_;
    float                 scroll_{0}, content_{0};
};

// ============================================================================================================ l'apercu ===
//  Le style choisi : en grand, a quatre tailles, sur le fond d'une vue, ses glyphes, ses renseignements, deux boutons.
class HmiFontPreview final : public ui::Widget {
public:
    HmiFontPreview(std::string id, HmiFontLibraryPane& pane) : ui::Widget(std::move(id)), pane_(pane) { setFocusPolicy(true); }

    [[nodiscard]] bool glyphRect(char32_t code, gfx::Rect& out) const {
        for (const auto& g : glyphs_)
            if (g.code == code) {
                out = g.rect;
                return true;
            }
        return false;
    }
    [[nodiscard]] const std::vector<char32_t>& shownGlyphs() const noexcept { return present_; }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(b, c.panelBg);
        r.fillRect({b.x, b.y, 1.f, b.h}, c.border);
        glyphs_.clear();
        buttons_.clear();
        styleRects_.clear();
        license_ = {};
        const fl::Family* fam = pane_.selectedFamily();
        const fl::Font* f = pane_.selectedFont();
        if (!fam || !f) {
            r.drawText({b.x + 16.f, b.y + 16.f}, "Choisis une police pour la voir.", ctx.theme.font.ui, c.textMuted);
            return;
        }
        r.pushClip(b);
        const float x = b.x + 16.f, w = std::max(40.f, b.w - 32.f);
        // ---- l'en-tete : APERCU . CARLITO, Normal | Gras ----
        r.fillRect({b.x + 1.f, b.y, b.w - 1.f, 30.f}, c.headerBg);
        r.drawText({x, b.y + (30.f - r.lineHeight(ctx.theme.font.smallUi)) / 2},
                   "APER\xC3\x87U  \xC2\xB7  " + upperAscii(fam->label), ctx.theme.font.smallUi, c.textMuted);
        {
            float sx = b.right() - 12.f;
            const bool hasBold = fam->style("Gras") != nullptr;
            for (int k = 1; k >= 0; --k) {
                const std::string label = k == 0 ? "Normal" : "Gras";
                const float bw = r.measure(label, ctx.theme.font.smallUi).width + 18.f;
                sx -= bw;
                const gfx::Rect br{sx, b.y + 4.f, bw, 22.f};
                const bool on = (k == 1) == pane_.bold_;
                r.fillRoundedRect(br, on ? c.accent.withAlpha(60) : c.inputBg, 3.f);
                r.drawText({br.x + 9.f, br.y + (br.h - r.lineHeight(ctx.theme.font.smallUi)) / 2}, label, ctx.theme.font.smallUi,
                           k == 1 && !hasBold ? c.textDisabled : on ? c.text : c.textMuted);
                styleRects_.push_back({br, k == 1});
            }
        }
        float y = b.y + 40.f;
        // ---- en grand ----
        {
            const std::string big = "Aa Bb \xC3\x89\xC3\xA9 0123";
            const float px = std::min(64.f, std::max(24.f, w / 6.5f));
            (void)writeIn(r, f->data, f->format, big, px, {x, y}, c.text);
            y += px * 1.3f + 6.f;
        }
        // ---- quatre tailles ----
        for (const int px : {10, 14, 20, 30}) {
            r.drawText({x, y + (static_cast<float>(px) * 1.25f - r.lineHeight(ctx.theme.font.smallUi)) / 2}, std::to_string(px),
                       ctx.theme.font.smallUi, c.textMuted);
            r.pushClip({x + 30.f, y, std::max(0.f, w - 30.f), static_cast<float>(px) * 1.4f});
            (void)writeIn(r, f->data, f->format, pane_.sample_, static_cast<float>(px), {x + 30.f, y}, c.text);
            r.popClip();
            y += static_cast<float>(px) * 1.35f + 4.f;
        }
        // ---- dans une vue ----
        y += 6.f;
        r.drawText({x, y}, "DANS UNE VUE", ctx.theme.font.smallUi, c.textMuted);
        y += r.lineHeight(ctx.theme.font.smallUi) + 6.f;
        {
            const gfx::Rect well{x, y, w, 60.f};
            r.fillRoundedRect(well, gfx::Color::rgb(0x1E2530), 5.f);
            const fl::Font* strong = fam->style("Gras") ? fam->style("Gras") : f;
            float cx = well.x + 14.f;
            cx += writeIn(r, f->data, f->format, "Pompe P1", 18.f, {cx, well.y + 18.f}, gfx::Color::rgb(0xE6EAF0)) + 14.f;
            const gfx::Rect btn{cx, well.y + 10.f, 130.f, 40.f};
            r.fillRoundedRect(btn, gfx::Color::rgb(0x2E8B57), 4.f);
            (void)writeIn(r, strong->data, strong->format, "D\xC3\x89MARRER", 17.f, {btn.x + 12.f, btn.y + 10.f}, gfx::Color::rgb(0xFFFFFF));
            const gfx::Rect val{btn.right() + 14.f, well.y + 10.f, 110.f, 40.f};
            if (val.right() < well.right() - 4.f) {
                r.fillRoundedRect(val, gfx::Color::rgb(0x111720), 4.f);
                (void)writeIn(r, f->data, f->format, "12,4 bar", 20.f, {val.x + 10.f, val.y + 8.f}, gfx::Color::rgb(0x7CE0A0));
            }
            y = well.bottom() + 12.f;
        }
        // ---- les glyphes ----
        const float infoH = 4 * 22.f + 52.f;
        r.drawText({x, y}, "GLYPHES  \xC2\xB7  " + thousands(f->glyphs) + "  (un double-clic : l'\xC3\xA9" "diteur de polices)",
                   ctx.theme.font.smallUi, c.textMuted);
        y += r.lineHeight(ctx.theme.font.smallUi) + 6.f;
        refreshPresent(*f);
        {
            const float cw = 34.f, ch = 38.f;
            const int perRow = std::max(1, static_cast<int>(w / cw));
            const int rowsFit = std::max(1, static_cast<int>((b.bottom() - infoH - y - 8.f) / ch));
            const std::size_t room = static_cast<std::size_t>(perRow) * static_cast<std::size_t>(std::min(rowsFit, 6));
            for (std::size_t i = 0; i < present_.size() && i < room; ++i) {
                const gfx::Rect cell{x + static_cast<float>(i % static_cast<std::size_t>(perRow)) * cw,
                                     y + static_cast<float>(i / static_cast<std::size_t>(perRow)) * ch, cw, ch};
                r.strokeRect(cell, c.border, 1.f);
                const std::string g = utf8Of(present_[i]);
                const float gw = drawInFont(r, f->data, f->format, g, 20.f, {cell.x + 9.f, cell.y + 7.f}, c.text);
                if (gw < 0.f) r.drawText({cell.x + 10.f, cell.y + 10.f}, g, ctx.theme.font.ui, c.text);
                glyphs_.push_back({present_[i], cell});
            }
            const std::size_t shown = std::min(present_.size(), room);
            const int usedRows = static_cast<int>((shown + static_cast<std::size_t>(perRow) - 1) / static_cast<std::size_t>(perRow));
            y += static_cast<float>(usedRows) * ch + 12.f;
        }
        // ---- les renseignements ----
        const auto line = [&](const std::string& k, const std::string& v, gfx::Color col) {
            r.drawText({x, y}, k, ctx.theme.font.smallUi, c.textMuted);
            r.drawText({x + 120.f, y}, v, ctx.theme.font.smallUi, col);
            y += 22.f;
        };
        const std::string shownFile = f->mine() ? f->file : "resources/polices/" + fl::resourceName(*f);
        line("Fichier", shownFile, c.text);
        const std::string kind = f->format == "OTF" ? std::string("OpenType") : std::string("TrueType");
        line("Sorte", kind + "  \xC2\xB7  " + (!f->editable ? std::string("contours CFF : aper\xC3\xA7u et emploi, pas modifiable")
                                                            : f->mine() ? std::string("modifiable") : std::string("modifiable (une copie)")),
             c.text);
        {
            const std::string lic = f->license.empty() ? std::string("la tienne") : f->license;
            r.drawText({x, y}, "Licence", ctx.theme.font.smallUi, c.textMuted);
            r.drawText({x + 120.f, y}, lic, ctx.theme.font.smallUi, c.text);
            if (!f->licenseFile.empty()) {
                const float lx = x + 120.f + r.measure(lic, ctx.theme.font.smallUi).width + 6.f;
                r.drawText({lx, y}, "(lire)", ctx.theme.font.smallUi, c.accent);
                license_ = {lx, y, r.measure("(lire)", ctx.theme.font.smallUi).width, 18.f};
            }
            y += 22.f;
        }
        {
            const hmi::Resource* res = fl::importedAs(pane_.doc_->project, *f);
            line("Dans le projet", res ? "oui  \xC2\xB7  " + fl::usageText(fl::usageOf(pane_.doc_->project, res->name)) : std::string("non"),
                 res ? c.ok : c.textMuted);
        }
        // ---- les deux boutons ----
        {
            y += 6.f;
            const float bw = std::max(60.f, (w - 12.f) / 2);
            const bool imported = fl::importedAs(pane_.doc_->project, *f) != nullptr;
            const bool canEdit = f->mine() && f->editable;
            const std::string second = canEdit ? std::string("Modifier\xE2\x80\xA6") : std::string("Dupliquer pour la modifier\xE2\x80\xA6");
            const bool secondOn = canEdit ? static_cast<bool>(pane_.hosts_.editFont) : static_cast<bool>(pane_.hosts_.duplicateFont);
            const std::pair<std::string, bool> items[2] = {
                {imported ? std::string("D\xC3\xA9j\xC3\xA0 dans le projet") : std::string("Importer dans le projet"), !imported},
                {second, secondOn}};
            for (int k = 0; k < 2; ++k) {
                const gfx::Rect br{x + static_cast<float>(k) * (bw + 12.f), y, bw, 34.f};
                r.fillRoundedRect(br, c.inputBg, 4.f);
                r.strokeRect(br, c.border, 1.f);
                r.pushClip(br);
                r.drawText({br.x + 10.f, br.y + (br.h - r.lineHeight(ctx.theme.font.ui)) / 2}, items[k].first, ctx.theme.font.ui,
                           items[k].second ? c.text : c.textDisabled);
                r.popClip();
                buttons_.push_back({br, k == 0 ? HmiFontLibraryPane::ActImport : canEdit ? HmiFontLibraryPane::ActEdit : HmiFontLibraryPane::ActDuplicate,
                                    items[k].second});
            }
        }
        r.popClip();
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const auto* m = std::get_if<ui::MouseDown>(&ev);
        if (!m || !bounds().contains(m->pos) || m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        grabFocus();
        for (const auto& s : styleRects_)
            if (s.rect.contains(m->pos)) {
                pane_.setBold(s.bold);
                return ui::EventResult::Consumed;
            }
        for (const auto& bt : buttons_)
            if (bt.rect.contains(m->pos)) {
                if (bt.enabled) pane_.runAction(bt.action);
                return ui::EventResult::Consumed;
            }
        if (license_.w > 0 && license_.contains(m->pos)) {
            pane_.runAction(HmiFontLibraryPane::ActLicense);
            return ui::EventResult::Consumed;
        }
        for (const auto& g : glyphs_)
            if (g.rect.contains(m->pos)) {
                if (m->clickCount >= 2) (void)pane_.editGlyph(g.code);
                return ui::EventResult::Consumed;
            }
        return ui::EventResult::Consumed;
    }

private:
    void refreshPresent(const fl::Font& f) {
        if (f.data.get() == presentOf_ && !present_.empty()) return;
        presentOf_ = f.data.get();
        present_.clear();
        const auto& codes = glyphCodes();
        std::vector<std::uint32_t> asked(codes.begin(), codes.end());
        hmi::FontMetrics m;
        if (f.data && hmi::fontMetrics(*f.data, asked, m) && m.widths.size() == asked.size()) {
            for (std::size_t i = 0; i < asked.size(); ++i)
                if (m.widths[i] > 0) present_.push_back(codes[i]);
        } else {
            present_.assign(codes.begin(), codes.end());   // une OTF CFF : on ne sait pas, on montre tout
        }
    }
    struct Glyph { char32_t code; gfx::Rect rect; };
    struct Btn { gfx::Rect rect; int action; bool enabled; };
    struct StyleRect { gfx::Rect rect; bool bold; };
    HmiFontLibraryPane&    pane_;
    std::vector<Glyph>     glyphs_;
    std::vector<Btn>       buttons_;
    std::vector<StyleRect> styleRects_;
    gfx::Rect              license_{};
    const void*            presentOf_{nullptr};
    std::vector<char32_t>  present_;
};

// =============================================================================================================== le volet ===
HmiFontLibraryPane::HmiFontLibraryPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)), sample_(fl::sampleText()) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ActImport, HmiGlyph::Import,
               "Importer dans le projet (Entr\xC3\xA9" "e) : la police devient une ressource, \xC3\xA0 choisir dans la case Police d'un objet, "
               "d'un style ou du th\xC3\xA8me (Ctrl+Z la retire)",
               "Importer dans le projet");
    tools->add(ActAdd, HmiGlyph::Plus, "Ajouter un fichier TTF ou OTF \xC3\xA0 Mes polices (gard\xC3\xA9" "es hors du projet, pour tous tes projets)",
               "Ajouter \xC3\xA0 Mes polices\xE2\x80\xA6");
    tools->separator();
    tools->add(ActNew, HmiGlyph::Text, "Nouvelle police : l'\xC3\xA9" "diteur de polices, une police vide dans Mes polices", "Nouvelle police");
    tools->add(ActDuplicate, HmiGlyph::Duplicate,
               "Dupliquer \xC3\xA0 partir de la police choisie : une copie dans Mes polices, \xC3\xA0 modifier dans l'\xC3\xA9" "diteur de polices",
               "Dupliquer \xC3\xA0 partir de\xE2\x80\xA6");
    tools->add(ActEdit, HmiGlyph::Style, "Modifier la police choisie dans l'\xC3\xA9" "diteur de polices (une des tiennes ; une fournie : la dupliquer)",
               "Modifier");
    tools->separator();
    tools->add(ActRemove, HmiGlyph::Delete, "Supprimer la police choisie de Mes polices (Suppr) - une fournie se garde", "Supprimer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto single = [this] { return selectedFont() != nullptr && selectedIds().size() <= 1; };
    tools_->setEnabledWhen(ActImport, [this, single] { return single() && !fl::importedAs(doc_->project, *selectedFont()); });
    tools_->setEnabledWhen(ActNew, [this] { return static_cast<bool>(hosts_.newFont); });
    tools_->setEnabledWhen(ActDuplicate, [this, single] { return single() && static_cast<bool>(hosts_.duplicateFont); });
    tools_->setEnabledWhen(ActEdit, [this, single] {
        return single() && selectedFont()->mine() && selectedFont()->editable && static_cast<bool>(hosts_.editFont);
    });
    tools_->setEnabledWhen(ActRemove, [this] {
        for (const auto& fid : selectedIds())
            if (const auto* fam = familyById(fid); fam && fam->mine()) return true;
        return false;
    });
    tools_->setLabelWhen(ActRemove, [this] { return several::label("Supprimer", selectedIds().size()); });
    search_ = &static_cast<ui::SearchField&>(addChild(std::make_unique<ui::SearchField>(
        base + ".search", "Rechercher (nom, style, \xC2\xAB Arial \xC2\xBB\xE2\x80\xA6)",
        "Chaque mot est cherch\xC3\xA9 dans le nom de la police, son style, son dossier, sa description et ses mots-cl\xC3\xA9s (arial, "
        "calibri, times, courier...), sans casse ni accents.")));
    auto sample = std::make_unique<ui::InputText>(base + ".texte");
    sample->setText(sample_);
    sample->setPlaceholder("Le texte d'aper\xC3\xA7u");
    sampleField_ = &static_cast<ui::InputText&>(addChild(std::move(sample)));
    auto sizes = std::make_unique<ui::DropDown>(base + ".taille");
    {
        std::vector<ui::DropDown::Item> items;
        for (const int s : {10, 12, 15, 18, 24, 30, 40}) items.push_back({std::to_string(s), std::to_string(s)});
        sizes->setItems(std::move(items));
        sizes->setSelectedIndex(2);
    }
    sizeBox_ = &static_cast<ui::DropDown&>(addChild(std::move(sizes)));
    normalBtn_ = &static_cast<ui::ToggleButton&>(addChild(std::make_unique<ui::ToggleButton>("Normal", base + ".normal")));
    boldBtn_ = &static_cast<ui::ToggleButton&>(addChild(std::make_unique<ui::ToggleButton>("Gras", base + ".gras")));
    normalBtn_->setChecked(true);
    list_ = &static_cast<HmiFontList&>(addChild(std::make_unique<HmiFontList>(base + ".liste", *this)));
    preview_ = &static_cast<HmiFontPreview&>(addChild(std::make_unique<HmiFontPreview>(base + ".apercu", *this)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".menu")));
    links_ += menu_->itemChosen->connect([this](int a) { runAction(a); });
    links_ += tools_->triggered->connect([this](int a) { runAction(a); });
    links_ += search_->changed->connect([this] { setSearch(search_->text()); });
    links_ += sampleField_->textChanged->connect([this](const std::string& t) {
        sample_ = t.empty() ? std::string(fl::sampleText()) : t;
        invalidate();
        list_->invalidate();
        preview_->invalidate();
    });
    links_ += sizeBox_->selectionChanged->connect([this](int) {
        if (const auto* it = sizeBox_->selectedItem()) setSampleSize(std::atoi(it->value.c_str()));
    });
    // Normal | Gras : un clic sur l'un le choisit (un clic sur celui deja choisi le garde) ; setBold remet les deux boutons.
    links_ += normalBtn_->toggled->connect([this](bool) { if (!syncing_) setBold(false); });
    links_ += boldBtn_->toggled->connect([this](bool) { if (!syncing_) setBold(true); });
    links_ += doc_->changed->connect([this](Id) { refresh(); });
    refresh();
}

HmiFontLibraryPane::~HmiFontLibraryPane() = default;

ui::Widget& HmiFontLibraryPane::list() noexcept { return *list_; }
ui::Widget& HmiFontLibraryPane::preview() noexcept { return *preview_; }

void HmiFontLibraryPane::setHosts(Hosts h) {
    hosts_ = std::move(h);
    invalidate();
}

void HmiFontLibraryPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setTransientMessage(message_, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiFontLibraryPane::fail(std::string text, std::string* why) {
    say(text, true);
    if (why) *why = std::move(text);
    return false;
}

void HmiFontLibraryPane::syncFonts() {
    if (revision_ == fl::userRevision() && !families_.empty()) return;
    revision_ = fl::userRevision();
    families_ = fl::families();
}

void HmiFontLibraryPane::refresh() {
    revision_ = 0;   // Mes polices relues (un fichier change sur le disque se voit au retour sur le volet)
    rebuild();
}

const fl::Family* HmiFontLibraryPane::familyById(const std::string& id) const {
    for (const auto& fam : families_)
        for (const auto& f : fam.fonts)
            if (f.id == id) return &fam;
    return nullptr;
}

void HmiFontLibraryPane::rebuild() {
    syncFonts();
    const auto& p = doc_->project;
    using Row = HmiFontList::Row;
    using K = HmiFontList::Kind;
    std::vector<Row> rows;
    const bool searching = !trimmed(query_).empty();
    const auto familyRow = [&](std::size_t i) {
        const auto& fam = families_[i];
        const fl::Font& reg = fam.regular();
        const fl::Font* shown = bold_ && fam.style("Gras") ? fam.style("Gras") : &reg;
        Row r;
        r.kind = K::Family;
        r.category = fam.category;
        r.family = static_cast<int>(i);
        r.fid = reg.id;
        r.name = fam.label;
        r.data = shown->data;
        r.format = shown->format;
        r.mine = fam.mine();
        for (const auto& f : fam.fonts) r.inProject = r.inProject || fl::importedAs(p, f) != nullptr;
        if (compact_) {
            r.sub = fam.mine() ? reg.format + std::string(reg.editable ? "" : "  \xC2\xB7  aper\xC3\xA7u et emploi")
                               : shortDescription(reg.description);
            if (r.inProject) r.sub = "\xE2\x9C\x93 dans le projet  \xC2\xB7  " + r.sub;
            r.h = 52.f;
        } else {
            r.sub = fam.mine() ? reg.format + "  \xC2\xB7  " + std::string(reg.editable ? "une de tes polices" : "aper\xC3\xA7u et emploi (contours CFF : pas modifiable)")
                               : shortDescription(reg.description);
            r.sub += "  \xC2\xB7  " + fam.stylesText();
            r.h = 82.f;
        }
        return r;
    };
    // ---- en etroit : les polices du projet d'abord (ou elles servent), puis << Bibliotheque >> ----
    if (compact_) {
        const auto fonts = fl::projectFonts(p);
        std::vector<Row> mine;
        for (const auto* res : fonts) {
            const std::string shown = res->data ? fl::displayName(*res->data) : std::string{};
            fl::Font probe;
            probe.label = shown.empty() ? res->name : shown;
            probe.words = res->name;
            probe.format = res->format;
            if (searching && !fl::matches(probe, query_)) continue;
            Row r;
            r.kind = K::Project;
            r.resource = res->name;
            r.name = shown.empty() ? res->name : shown;
            r.data = res->data;
            r.format = res->format;
            // Ou elle sert : << police par defaut . 14 objets >>, << 5 objets . 1 style >>.
            const auto use = fl::usageOf(p, res->name);
            std::vector<std::string> parts;
            if (use.defaultFont) parts.emplace_back("police par d\xC3\xA9" "faut");
            if (use.valueFont) parts.emplace_back("police des valeurs");
            parts.push_back(std::to_string(use.objects) + (use.objects > 1 ? " objets" : " objet"));
            if (use.styles) parts.push_back(std::to_string(use.styles) + (use.styles > 1 ? " styles" : " style"));
            for (const auto& part : parts) r.sub += (r.sub.empty() ? "" : "  \xC2\xB7  ") + part;
            r.inProject = true;
            r.h = 52.f;
            mine.push_back(std::move(r));
        }
        Row head;
        head.kind = K::Section;
        head.name = "Du projet";
        head.count = mine.size();
        head.h = 28.f;
        rows.push_back(head);
        if (mine.empty()) {
            Row hint;
            hint.kind = K::Hint;
            hint.name = searching ? std::string("Aucune ne r\xC3\xA9pond.") : std::string("Aucune : glisse-en une de la biblioth\xC3\xA8que sur un objet.");
            hint.h = 28.f;
            rows.push_back(hint);
        }
        for (auto& r : mine) rows.push_back(std::move(r));
        Row lib;
        lib.kind = K::Section;
        lib.name = "Biblioth\xC3\xA8que";
        lib.h = 28.f;
        rows.push_back(lib);
    }
    for (const auto& cat : fl::categories()) {
        std::vector<std::size_t> found;
        for (std::size_t i = 0; i < families_.size(); ++i)
            if (families_[i].category == cat && (!searching || fl::matches(families_[i], query_))) found.push_back(i);
        // Mes polices : toujours la (une ligne pour dire comment en ajouter) ; un dossier sans resultat de recherche : cache.
        if (found.empty() && (searching || cat != fl::kMine)) continue;
        Row h;
        h.kind = K::Header;
        h.category = cat;
        h.name = cat;
        h.count = found.size();
        const bool open = searching || categoryOpen(cat);
        h.open = open ? 1 : 0;
        h.h = compact_ ? 32.f : 30.f;
        rows.push_back(h);
        if (!open) continue;
        if (found.empty() && !compact_) {
            Row hint;
            hint.kind = K::Hint;
            hint.name = "Ajoute un fichier TTF ou OTF (Ajouter \xC3\xA0 Mes polices\xE2\x80\xA6), ou l\xC3\xA2" "che-le ici ; tes polices cr\xC3\xA9\xC3\xA9" "es dans "
                        "l'\xC3\xA9" "diteur de polices y vont aussi.";
            hint.h = 30.f;
            rows.push_back(hint);
        }
        for (const auto i : found) rows.push_back(familyRow(i));
    }
    list_->setRows(std::move(rows));
    std::size_t total = 0, mineCount = 0;
    for (const auto& fam : families_) {
        ++total;
        mineCount += fam.mine() ? 1u : 0u;
    }
    if (compact_)
        status_->setMessage("Glisse une police sur un objet : il la prend. Double-clic : sur la s\xC3\xA9lection. Clic droit : Modifier la police\xE2\x80\xA6",
                            ui::StatusBar::Severity::Info);
    else
        status_->setMessage(std::to_string(total - mineCount) + " familles fournies, " + std::to_string(mineCount) + " \xC3\xA0 toi \xC2\xB7 Entr\xC3\xA9" "e : importer "
                                "dans le projet \xC2\xB7 Suppr : retirer une des tiennes",
                            ui::StatusBar::Severity::Info);
    preview_->invalidate();
    invalidate();
}

// ---- la ligne choisie ----
void HmiFontLibraryPane::rowChosen() {
    preview_->invalidate();
    invalidate();
}

void HmiFontLibraryPane::rowActivated() {
    if (compact_) {
        const HmiFontPick pick = currentPick();
        if (!pick.valid()) return;
        activated->emit(pick);
        if (hosts_.give) {
            std::string why;
            if (hosts_.give(pick, &why) == 0 && !why.empty()) say(why, true);
        }
        return;
    }
    (void)importSelected();
}

const fl::Family* HmiFontLibraryPane::selectedFamily() const {
    const auto* row = list_->currentRow();
    if (!row || row->kind != HmiFontList::Kind::Family || row->family < 0 || static_cast<std::size_t>(row->family) >= families_.size()) return nullptr;
    return &families_[static_cast<std::size_t>(row->family)];
}

const fl::Font* HmiFontLibraryPane::selectedFont() const {
    const fl::Family* fam = selectedFamily();
    if (!fam || fam->fonts.empty()) return nullptr;
    if (bold_)
        if (const auto* g = fam->style("Gras")) return g;
    return &fam->regular();
}

std::string HmiFontLibraryPane::selectedId() const {
    const fl::Font* f = selectedFont();
    return f ? f->id : std::string{};
}

std::vector<std::string> HmiFontLibraryPane::selectedIds() const {
    std::vector<std::string> out;
    for (const auto& r : list_->rows())
        if (r.kind == HmiFontList::Kind::Family && list_->chosen().count(r.key()) && r.family >= 0)
            out.push_back(families_[static_cast<std::size_t>(r.family)].regular().id);
    return out;
}

bool HmiFontLibraryPane::select(const std::string& fontId) {
    syncFonts();
    const fl::Family* fam = familyById(fontId);
    if (!fam) return false;
    if (!categoryOpen(fam->category)) setCategoryOpen(fam->category, true);
    const std::string key = "fam:" + fam->regular().id;
    if (!list_->rowOf(key)) return false;
    // Le style demande se montre : Gras s'il l'est.
    for (const auto& f : fam->fonts)
        if (f.id == fontId && (f.style == "Gras") != bold_) setBold(f.style == "Gras");
    list_->choose(key);
    rowChosen();
    return true;
}

bool HmiFontLibraryPane::selectIds(const std::vector<std::string>& ids) {
    syncFonts();
    std::vector<std::string> keys;
    for (const auto& id : ids)
        if (const fl::Family* fam = familyById(id)) {
            const std::string key = "fam:" + fam->regular().id;
            if (list_->rowOf(key) && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
        }
    list_->chooseMany(keys);
    rowChosen();
    return keys.size() == ids.size();
}

bool HmiFontLibraryPane::selectProjectFont(const std::string& resource) {
    if (!list_->rowOf("res:" + resource)) return false;
    list_->choose("res:" + resource);
    rowChosen();
    return true;
}

HmiFontPick HmiFontLibraryPane::currentPick() const {
    HmiFontPick pick;
    const auto* row = list_->currentRow();
    if (!row) return pick;
    if (row->kind == HmiFontList::Kind::Project) {
        pick.resource = row->resource;
        pick.label = row->name;
    } else if (const fl::Font* f = selectedFont()) {
        if (const hmi::Resource* res = fl::importedAs(doc_->project, *f)) pick.resource = res->name;   // deja la : sans importer
        else pick.font = *f;
        pick.label = f->fullName();
    }
    return pick;
}

void HmiFontLibraryPane::setBold(bool bold) {
    const bool changed = bold != bold_;
    bold_ = bold;
    if (!syncing_) {
        syncing_ = true;
        if (normalBtn_) normalBtn_->setChecked(!bold);
        if (boldBtn_) boldBtn_->setChecked(bold);
        syncing_ = false;
    }
    if (changed) rebuild();
}

void HmiFontLibraryPane::setSampleText(const std::string& text) {
    if (sampleField_->text() != text) sampleField_->setText(text);
    sample_ = text.empty() ? std::string(fl::sampleText()) : text;
    list_->invalidate();
    preview_->invalidate();
}

void HmiFontLibraryPane::setSampleSize(int px) {
    size_ = std::clamp(px, 6, 96);
    list_->invalidate();
}

void HmiFontLibraryPane::setSearch(const std::string& text) {
    query_ = text;
    if (search_ && search_->text() != text) search_->setText(text);
    rebuild();
}

namespace {
// Les dossiers replies : communs aux volets ouverts (le sous-onglet garde les siens : << etroit >>).
std::set<std::string>& closedFolders(bool compact) {
    static std::set<std::string> wide;
    static std::set<std::string> narrow{std::string(fl::kMine), "Avec empattement", "Chasse fixe"};
    return compact ? narrow : wide;
}
} // namespace

void HmiFontLibraryPane::setCategoryOpen(const std::string& category, bool open) {
    auto& closed = closedFolders(compact_);
    if (open) closed.erase(category);
    else closed.insert(category);
    rebuild();
}

bool HmiFontLibraryPane::categoryOpen(const std::string& category) const { return closedFolders(compact_).count(category) == 0; }

std::vector<std::pair<std::string, std::size_t>> HmiFontLibraryPane::shownCategories() const {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const auto& r : list_->rows())
        if (r.kind == HmiFontList::Kind::Header) out.emplace_back(r.category, r.count);
    return out;
}

std::vector<std::string> HmiFontLibraryPane::shownFamilies() const {
    std::vector<std::string> out;
    for (const auto& r : list_->rows())
        if (r.kind == HmiFontList::Kind::Family) out.push_back(r.name);
    return out;
}

std::vector<std::string> HmiFontLibraryPane::shownProjectFonts() const {
    std::vector<std::string> out;
    for (const auto& r : list_->rows())
        if (r.kind == HmiFontList::Kind::Project) out.push_back(r.resource);
    return out;
}

bool HmiFontLibraryPane::familyRect(const std::string& label, gfx::Rect& out) const {
    for (const auto& r : list_->rows())
        if (r.kind == HmiFontList::Kind::Family && r.name == label) return list_->rectOf(r.key(), out);
    return false;
}

bool HmiFontLibraryPane::projectFontRect(const std::string& resource, gfx::Rect& out) const { return list_->rectOf("res:" + resource, out); }

bool HmiFontLibraryPane::glyphRect(char32_t code, gfx::Rect& out) const { return preview_->glyphRect(code, out); }

// ---- importer, ajouter, supprimer ----
Id HmiFontLibraryPane::importSelected(std::string* why) {
    const fl::Font* f = selectedFont();
    return f ? importFont(f->id, why) : ((void)fail("Choisis une police \xC3\xA0 importer.", why), kNoId);
}

Id HmiFontLibraryPane::importFont(const std::string& fontId, std::string* why) {
    syncFonts();
    const fl::Font* found = nullptr;
    for (const auto& fam : families_)
        for (const auto& f : fam.fonts)
            if (f.id == fontId) found = &f;
    if (!found) {
        (void)fail("Choisis une police \xC3\xA0 importer.", why);
        return kNoId;
    }
    const fl::Font copy = *found;
    if (const hmi::Resource* r = fl::importedAs(doc_->project, copy)) {
        say(copy.fullName() + " est d\xC3\xA9j\xC3\xA0 dans le projet : la ressource " + r->name + ".");
        return r->id;
    }
    fl::Imported made;
    auto cmd = hmi::changeProject(doc_, "Importer la police " + copy.fullName(), [&](hmi::Project& p) { made = fl::importFont(p, copy); });
    if (!cmd) {
        (void)fail("non import\xC3\xA9" "e", why);
        return kNoId;
    }
    apply_(std::move(cmd));
    refresh();
    (void)select(copy.id);
    say(copy.fullName() + " import\xC3\xA9" "e : la ressource " + made.name
        + " - choisis-la dans la case Police d'un objet, d'un style ou du th\xC3\xA8me (Ctrl+Z la retire).");
    return made.id;
}

bool HmiFontLibraryPane::addFile(const std::string& path, std::string* why) {
    std::string stored, error;
    bool existed = false;
    if (!fl::addUserFile(path, &stored, &existed, &error)) return fail("Mes polices : " + error, why);
    refresh();
    (void)select("mes/" + stored);
    say(existed ? "D\xC3\xA9j\xC3\xA0 dans Mes polices : " + stored : "Ajout\xC3\xA9" "e \xC3\xA0 Mes polices : " + stored + " (pour tous tes projets).");
    return true;
}

// Une famille de Mes polices : un fichier par style, une commande chacun, dans un groupe (un Ctrl+Z la rend).
bool HmiFontLibraryPane::removeFamily(const fl::Family& fam, std::string* why) {
    if (!fam.mine()) {
        if (why) *why = "une police fournie se garde";
        return false;
    }
    const std::vector<fl::Font> fonts = fam.fonts;
    const std::weak_ptr<const void> alive = picked;
    core::CommandGroupScope group("Supprimer " + fam.label + " de Mes polices");
    for (const auto& f : fonts) {
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(std::u8string(f.file.begin(), f.file.end())), ec)) {
            if (why) *why = "fichier introuvable : " + f.file;
            return false;
        }
        auto cmd = std::make_unique<MyFontRemoveCommand>(f, [this, alive] {
            if (!alive.expired()) refresh();
        });
        if (apply_) {
            apply_(std::move(cmd));
        } else if (const auto st = cmd->execute(); !st) {
            if (why) *why = st.error().context.empty() ? st.error().message() : st.error().context;
            return false;
        }
    }
    return true;
}

bool HmiFontLibraryPane::removeSelected(std::string* why) {
    syncFonts();
    const auto ids = selectedIds();
    if (ids.size() > 1) {
        several::Batch b;
        b.verb = "Supprimer";
        b.noun = "police";
        b.nouns = "polices";
        b.feminine = true;
        for (const auto& id : ids) {
            const fl::Family* fam = familyById(id);
            b.names.push_back(fam ? fam->label : id);
        }
        b.remove = [this, ids](std::size_t i, std::string* no) {
            const fl::Family* fam = familyById(ids[i]);
            if (!fam) {
                if (no) *no = "introuvable";
                return false;
            }
            if (!fam->mine()) {
                if (no) *no = "fournie : elle se garde";
                return false;
            }
            const fl::Family copy = *fam;
            return removeFamily(copy, no);
        };
        b.after = [this] { refresh(); };
        b.report = [this](const std::string& text, bool warning) {
            say(text + " (les projets qui les ont import\xC3\xA9" "es gardent leur ressource)", warning);
        };
        b.alive = picked;
        several::run(std::move(b));
        return true;
    }
    const fl::Family* fam = selectedFamily();
    if (!fam) return fail("Choisis une de tes polices.", why);
    if (!fam->mine()) return fail(fam->label + " est fournie avec le logiciel : elle se garde (seules Mes polices se suppriment).", why);
    const fl::Family copy = *fam;
    std::string error;
    if (!removeFamily(copy, &error)) return fail(error, why);
    refresh();
    say(copy.label + " supprim\xC3\xA9" "e de Mes polices (les projets qui l'ont import\xC3\xA9" "e gardent leur ressource ; Ctrl+Z la rend).");
    return true;
}

bool HmiFontLibraryPane::editGlyph(char32_t code, std::string* why) {
    const fl::Font* f = selectedFont();
    if (!f) return fail("Choisis une police.", why);
    const fl::Font copy = *f;
    if (hosts_.editGlyph) {
        hosts_.editGlyph(copy, code);
        return true;
    }
    if (copy.mine() && copy.editable && hosts_.editFont) {
        hosts_.editFont(copy);
        return true;
    }
    if (hosts_.duplicateFont) {
        hosts_.duplicateFont(copy);
        return true;
    }
    return fail("L'\xC3\xA9" "diteur de polices n'est pas disponible ici.", why);
}

// ---- le clic droit, les gestes ----
std::vector<ui::PopupMenu::Item> HmiFontLibraryPane::contextItems() const {
    std::vector<ui::PopupMenu::Item> items;
    const fl::Font* f = selectedFont();
    const auto* row = list_->currentRow();
    const bool projectRow = row && row->kind == HmiFontList::Kind::Project;
    const hmi::Resource* r = f ? fl::importedAs(doc_->project, *f) : projectRow ? doc_->project.resourceByName(row->resource) : nullptr;
    const auto add = [&](int action, std::string label, std::string key, ui::Icon icon, bool enabled, std::string whyNot = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.shortcut = std::move(key);
        it.icon = icon;
        it.enabled = enabled;
        it.disabledReason = std::move(whyNot);
        it.id = action;
        items.push_back(std::move(it));
    };
    const auto sep = [&] {
        ui::PopupMenu::Item it;
        it.separator = true;
        items.push_back(it);
    };
    const auto chosen = selectedIds();
    const bool many = chosen.size() > 1;
    {
        ui::PopupMenu::Item head;
        head.heading = true;
        head.label = many ? std::to_string(chosen.size()) + " polices" : projectRow ? row->name : f ? f->fullName() : std::string("Polices");
        head.shortcut = many ? std::string{} : projectRow ? std::string("du projet") : f ? (f->mine() ? "une de tes polices" : "fournie") : std::string{};
        items.push_back(std::move(head));
    }
    const std::string none = many ? several::kOneAtATime : std::string("choisis une police");
    const bool editable = f && f->mine() && f->editable;
    if (compact_) {
        add(ActGive, "Donner \xC3\xA0 la s\xC3\xA9lection", "Entr\xC3\xA9" "e", ui::Icon::Ok, (f || projectRow) && static_cast<bool>(hosts_.give),
            "choisis d'abord un objet dans la vue");
        add(ActEdit, "Modifier la police\xE2\x80\xA6", {}, ui::Icon::Rename,
            (f || projectRow) && (editable ? static_cast<bool>(hosts_.editFont) : static_cast<bool>(hosts_.duplicateFont)),
            "l'\xC3\xA9" "diteur de polices n'est pas branch\xC3\xA9 ici");
        sep();
    }
    add(ActImport, r && !many ? "D\xC3\xA9j\xC3\xA0 dans le projet (" + r->name + ")" : std::string("Importer dans le projet"), "Entr\xC3\xA9" "e",
        ui::Icon::Import, f != nullptr && !r && !many, r && !many ? std::string("le m\xC3\xAAme fichier est d\xC3\xA9j\xC3\xA0 une ressource") : none);
    add(ActGoTo, "Aller \xC3\xA0 la ressource", {}, ui::Icon::GoTo, r != nullptr && !many && static_cast<bool>(hosts_.openResource),
        many ? none : r ? std::string("indisponible ici") : std::string("pas encore dans le projet"));
    add(ActCopy, "Copier le nom", "Ctrl+C", ui::Icon::Copy, (f != nullptr || projectRow) && !many, none);
    if (!compact_) {
        sep();
        add(ActNew, "Nouvelle police\xE2\x80\xA6", {}, ui::Icon::Plus, static_cast<bool>(hosts_.newFont), "l'\xC3\xA9" "diteur de polices n'est pas branch\xC3\xA9 ici");
        add(ActDuplicate, "Dupliquer \xC3\xA0 partir de cette police\xE2\x80\xA6", {}, ui::Icon::Copy, f && !many && static_cast<bool>(hosts_.duplicateFont),
            f && !many ? std::string("l'\xC3\xA9" "diteur de polices n'est pas branch\xC3\xA9 ici") : none);
        add(ActEdit, "Modifier\xE2\x80\xA6", {}, ui::Icon::Rename, editable && !many && static_cast<bool>(hosts_.editFont),
            many ? none : !f ? none : !f->mine() ? std::string("une police fournie : la dupliquer pour la modifier")
                                     : !f->editable ? std::string("contours CFF : pas modifiable")
                                                    : std::string("l'\xC3\xA9" "diteur de polices n'est pas branch\xC3\xA9 ici"));
        sep();
        add(ActAdd, "Ajouter un fichier \xC3\xA0 Mes polices\xE2\x80\xA6", {}, ui::Icon::Plus, true);
        add(ActReveal, "Ouvrir dans l'explorateur", {}, ui::Icon::Open, f && !many && static_cast<bool>(hosts_.reveal), none);
        add(ActLicense, "Lire la licence", {}, ui::Icon::Document, f && !many && !f->licenseFile.empty() && static_cast<bool>(hosts_.openFile),
            f && f->licenseFile.empty() ? std::string("une de tes polices : sa licence est la tienne") : none);
        bool anyMine = false;
        for (const auto& id : chosen)
            if (const auto* fam = familyById(id); fam && fam->mine()) anyMine = true;
        add(ActRemove, many ? several::label("Supprimer", chosen.size()) + " de Mes polices" : std::string("Supprimer de Mes polices"), "Suppr",
            ui::Icon::Delete, anyMine, f ? std::string("une police fournie se garde") : none);
    }
    return items;
}

void HmiFontLibraryPane::openMenu(gfx::Point at) {
    menu_->setItems(contextItems());
    menu_->openInWindow(at);
}

void HmiFontLibraryPane::runAction(int action) {
    const fl::Font* f = selectedFont();
    const auto* row = list_->currentRow();
    const bool projectRow = row && row->kind == HmiFontList::Kind::Project;
    if (selectedIds().size() > 1 && action != ActRemove && action != ActAdd && action != ActNew) {
        say("Plusieurs polices choisies : " + several::kOneAtATime + ".", true);
        return;
    }
    // Une police du projet (etroit) : celle de la bibliotheque qui a le meme contenu, sinon une police faite de la ressource.
    const auto projectFontAsFont = [&]() -> std::optional<fl::Font> {
        if (!projectRow) return std::nullopt;
        const hmi::Resource* res = doc_->project.resourceByName(row->resource);
        if (!res || !res->data) return std::nullopt;
        for (const auto& fam : families_)
            for (const auto& x : fam.fonts)
                if (x.data && (x.data == res->data || *x.data == *res->data)) return x;
        fl::Font made;
        made.id = "projet/" + res->name;
        made.label = row->name;
        made.style = "Normal";
        made.format = res->format;
        made.data = res->data;
        made.editable = fl::readable(*res->data, nullptr, &made.editable) && made.editable;
        return made;
    };
    switch (action) {
        case ActImport: (void)importSelected(); break;
        case ActAdd:
            if (hosts_.askFile) hosts_.askFile();
            else {
                ui::FilePick pick;
                pick.title = "Ajouter une police \xC3\xA0 Mes polices";
                pick.filters = {{"Polices (TTF, OTF)", "ttf;otf"}};
                if (!ui::pickFile(pick, [this](std::string path) {
                        if (!path.empty()) (void)addFile(path);
                    }))
                    say("Choisir un fichier n'est pas possible ici : l\xC3\xA2" "che un fichier TTF ou OTF sur la liste.", true);
            }
            break;
        case ActNew:
            if (hosts_.newFont) hosts_.newFont();
            else say("L'\xC3\xA9" "diteur de polices n'est pas disponible ici.", true);
            break;
        case ActDuplicate:
            if (f && hosts_.duplicateFont) hosts_.duplicateFont(*f);
            else if (const auto pf = projectFontAsFont(); pf && hosts_.duplicateFont) hosts_.duplicateFont(*pf);
            else say(f ? std::string("L'\xC3\xA9" "diteur de polices n'est pas disponible ici.") : std::string("Choisis une police."), true);
            break;
        case ActEdit: {
            std::optional<fl::Font> target = f ? std::optional<fl::Font>(*f) : projectFontAsFont();
            if (!target) {
                say("Choisis une police.", true);
                break;
            }
            if (target->mine() && target->editable && hosts_.editFont) hosts_.editFont(*target);
            else if (compact_ && hosts_.duplicateFont) hosts_.duplicateFont(*target);   // une fournie : une copie a modifier
            else if (!target->mine()) say(target->fullName() + " est fournie : Dupliquer \xC3\xA0 partir de\xE2\x80\xA6 pour la modifier.", true);
            else if (!target->editable) say(target->fullName() + " a des contours CFF : aper\xC3\xA7u et emploi, pas modifiable.", true);
            else say("L'\xC3\xA9" "diteur de polices n'est pas disponible ici.", true);
            break;
        }
        case ActRemove: (void)removeSelected(); break;
        case ActCopy: {
            const std::string name = projectRow ? row->resource : f ? fl::resourceName(*f) : std::string{};
            if (!name.empty()) {
                ui::setClipboardText(name);
                say("\xC2\xAB " + name + " \xC2\xBB copi\xC3\xA9.");
            }
            break;
        }
        case ActReveal:
            if (f) {
                const std::string why = hosts_.reveal ? hosts_.reveal(f->file) : std::string("indisponible ici");
                if (why.empty()) say("Ouvert dans l'explorateur : " + f->file);
                else say(why, true);
            }
            break;
        case ActLicense:
            if (f && !f->licenseFile.empty()) {
                const std::string why = hosts_.openFile ? hosts_.openFile(f->licenseFile) : std::string("indisponible ici");
                if (why.empty()) say("Licence de " + f->label + " : " + f->licenseFile);
                else say(why, true);
            }
            break;
        case ActGoTo: {
            const hmi::Resource* r = f ? fl::importedAs(doc_->project, *f) : projectRow ? doc_->project.resourceByName(row->resource) : nullptr;
            if (r && hosts_.openResource) hosts_.openResource(r->id);
            break;
        }
        case ActGive:
            rowActivated();
            break;
        default: break;
    }
}

void HmiFontLibraryPane::setCompact(bool on) {
    if (compact_ == on) return;
    compact_ = on;
    const auto v = on ? ui::Visibility::Collapsed : ui::Visibility::Visible;
    tools_->setVisibility(v);
    search_->setVisibility(v);
    sampleField_->setVisibility(v);
    sizeBox_->setVisibility(v);
    normalBtn_->setVisibility(v);
    boldBtn_->setVisibility(v);
    preview_->setVisibility(v);
    rebuild();
    invalidateLayout();
}

void HmiFontLibraryPane::onLayout() {
    const auto b = bounds();
    const float statusH = compact_ ? 52.f : 24.f;
    status_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
    if (compact_) {   // la palette : la liste, le mode d'emploi
        list_->setBounds({b.x, b.y, b.w, std::max(0.f, b.h - statusH)});
        return;
    }
    const float bar = 38.f, sampleH = 38.f;
    const float searchW = std::clamp(b.w * 0.26f, 180.f, 340.f);
    tools_->setBounds({b.x, b.y, std::max(0.f, b.w - searchW - 12.f), bar});
    search_->setBounds({b.x + b.w - searchW - 8.f, b.y + 5.f, searchW, 28.f});
    const float h = std::max(0.f, b.h - bar - statusH);
    const float previewW = std::clamp(b.w * 0.34f, 300.f, 520.f);
    const float listW = std::max(0.f, b.w - previewW);
    // La rangee du texte d'apercu : Texte [...] Taille [15] [Normal|Gras].
    const float y = b.y + bar + 5.f;
    const float toggles = 150.f, sizeW = 64.f;
    const float fieldW = std::max(80.f, listW - 60.f - 50.f - sizeW - toggles - 36.f);
    sampleField_->setBounds({b.x + 60.f, y, fieldW, 28.f});
    sizeBox_->setBounds({b.x + 60.f + fieldW + 60.f, y, sizeW, 28.f});
    normalBtn_->setBounds({b.x + 60.f + fieldW + 60.f + sizeW + 12.f, y, toggles / 2, 28.f});
    boldBtn_->setBounds({b.x + 60.f + fieldW + 60.f + sizeW + 12.f + toggles / 2, y, toggles / 2, 28.f});
    list_->setBounds({b.x, b.y + bar + sampleH, listW, std::max(0.f, h - sampleH)});
    preview_->setBounds({b.x + listW, b.y + bar, previewW, h});
}

void HmiFontLibraryPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.panelBg);
    if (compact_) return;
    const auto& c = ctx.theme.color;
    const float y = b.y + 38.f + 5.f + (28.f - ctx.r.lineHeight(ctx.theme.font.ui)) / 2;
    ctx.r.drawText({b.x + 12.f, y}, "Texte", ctx.theme.font.ui, c.textMuted);
    const auto f = sizeBox_->bounds();
    ctx.r.drawText({f.x - 48.f, y}, "Taille", ctx.theme.font.ui, c.textMuted);
}

// Un fichier TTF ou OTF lache sur le volet rejoint Mes polices.
ui::EventResult HmiFontLibraryPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::FileDropped>(&ev); d && bounds().contains(d->pos)) {
        (void)addFile(d->path);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
