// =============================================================================
//  app/hmi/HmiFunctionPicker.cpp - voir HmiFunctionPicker.hpp
// =============================================================================
#include "HmiFunctionPicker.hpp"

#include "../../hmi/HmiNatives.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>

namespace app {

namespace {

constexpr gfx::FontId kBody{15};
constexpr gfx::FontId kSmall{13};
constexpr gfx::FontId kMono{14};
constexpr gfx::FontId kTitle{17};
constexpr float       kRow = 24.f;      // un dossier
constexpr float       kItem = 40.f;     // une fonction : son appel, ce qu'elle rend
constexpr float       kSub = 24.f;      // une instance, un titre dans la liste
constexpr float       kIndent = 14.f;
constexpr char        kSep = '\x1F';
constexpr std::size_t kNone = static_cast<std::size_t>(-1);
const std::string     kNatives = "Natives";
const std::string     kMine = "Mes fonctions";
const std::string     kArrow = " \xE2\x80\xBA ";

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (r.measure(s, f).width <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    const auto n = r.fitCharacters(s, f, std::max(0.f, w - r.measure(dots, f).width));
    return s.substr(0, n) + dots;
}

// Un texte coupe en lignes de `w` pixels au plus (aux espaces ; un retour a la ligne garde).
std::vector<std::string> wrap(gfx::IRenderer& r, std::string_view text, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    const auto flush = [&] {
        out.push_back(line);
        line.clear();
    };
    while (i <= text.size()) {
        if (i == text.size()) {
            if (!line.empty()) flush();
            break;
        }
        if (text[i] == '\n') {
            flush();
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\n') ++j;
        const std::string word(text.substr(i, j - i));
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(candidate, f).width > w) {
            flush();
            line = word;
        } else {
            line = candidate;
        }
        i = j;
        while (i < text.size() && text[i] == ' ') ++i;
    }
    return out;
}

std::string upperOf(std::string_view s) {
    std::string u(s);
    for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return u;
}

std::string trimmedOf(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> linesOf(std::string_view text) {
    std::vector<std::string> out{std::string{}};
    for (const char ch : text) {
        if (ch == '\n') out.emplace_back();
        else if (ch != '\r') out.back() += ch;
    }
    return out;
}

// Les dossiers ouverts ou fermes a la main, gardes d'une ouverture a l'autre (la seance).
std::map<std::string, bool>& remembered() {
    static std::map<std::string, bool> m;
    return m;
}

} // namespace

// ============================================================== le corps ====
class HmiFunctionPicker::Body final : public ui::Widget {
public:
    Body(HmiFunctionPicker& owner, const Spec& spec) : ui::Widget("dialog.functionPicker"), owner_(owner), spec_(spec) {
        for (const auto& e : spec_.extra) {
            all_.push_back(e);
            if (all_.back().group.empty() || all_.back().group == kNatives) all_.back().group = kMine;
        }
        for (auto& e : HmiFunctionPicker::natives()) all_.push_back(std::move(e));
        buildFolders();
        returns_ = spec_.expected.empty() ? Returns::All : Returns::Fits;
        exprOnly_ = spec_.expressionOnly;
        search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.functionPicker.chercher")));
        search_->setPlaceholder("Chercher : un nom, ce qu'elle fait (moyenne, borner, cl\xC3\xA9\xE2\x80\xA6)");
        links_ += search_->textChanged->connect([this](const std::string&) {
            sel_ = 0;
            scroll_ = 0.f;
            filter();
        });
        example_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ins\xC3\xA9rer l'exemple", "dialog.functionPicker.exemple")));
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.functionPicker.annuler")));
        ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ins\xC3\xA9rer", "dialog.functionPicker.inserer")));
        ok_->setStyle(ui::Button::Style::Primary);
        example_->setTooltip("L'exemple de la fiche, \xC3\xA0 la place de l'appel seul");
        links_ += example_->clicked->connect([this] {
            const auto* e = selectedEntry();
            if (e && mine(*e)) owner_.openDefinition();
            else owner_.insert(true);
        });
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += ok_->clicked->connect([this] { owner_.insert(false); });
        filter();
    }

    // ---- l'etat ----
    [[nodiscard]] const Entry* selectedEntry() const {
        const auto* row = selectedRow();
        return row ? &all_[row->entry] : nullptr;
    }
    [[nodiscard]] int selectedInstance() const {
        const auto* row = selectedRow();
        return row ? row->instance : -1;
    }
    [[nodiscard]] std::vector<std::string> shownNames() const {
        std::vector<std::string> out;
        for (const auto& row : rows_)
            if (row.header.empty() && row.instance < 0) out.push_back(all_[row.entry].name);
        return out;
    }
    [[nodiscard]] std::vector<std::string> folderKeys() const {
        std::vector<std::string> out;
        for (const auto f : visible_) out.push_back(folders_[f].key);
        return out;
    }
    [[nodiscard]] std::string hereKey() const { return here_ == kNone ? std::string{} : folders_[here_].key; }
    [[nodiscard]] Returns returnsFilter() const { return returns_; }
    [[nodiscard]] std::vector<std::pair<Returns, std::size_t>> returnCounts() const {
        std::vector<std::pair<Returns, std::size_t>> out;
        const auto count = [this](Returns r) {
            std::size_t n = 0;
            for (const auto& e : all_)
                if (keeps(e, r)) ++n;
            return n;
        };
        for (const auto r : {Returns::All, Returns::Fits, Returns::Void, Returns::Bool, Returns::Integer, Returns::Real, Returns::Text,
                             Returns::Time, Returns::Word, Returns::Container, Returns::Reference, Returns::Generic, Returns::Other}) {
            if (r == Returns::Fits && spec_.expected.empty()) continue;
            const std::size_t n = count(r);
            if (r == Returns::Other && n == 0) continue;   // les types du projet : seulement s'il y en a
            out.push_back({r, n});
        }
        return out;
    }
    [[nodiscard]] bool searchFocused() const { return search_->focused(); }
    void focusFirst() { owner_.focus().focus(search_); }
    void setSearch(const std::string& t) {
        search_->setText(t);
        filter();
    }
    bool setFolder(std::string_view key) {
        if (key.empty()) {
            folder_ = kNone;
        } else {
            const auto f = folderByKey(key);
            if (f == kNone) return false;
            folder_ = f;
            for (auto p = folders_[f].parent; p != kNone; p = folders_[p].parent) remembered()[folders_[p].key] = true;
        }
        sel_ = 0;
        scroll_ = 0.f;
        unfold_ = kNone;
        filter();
        return true;
    }
    bool setReturns(Returns r) {
        if (r == Returns::Fits && spec_.expected.empty()) return false;
        if (r == Returns::Void && spec_.expressionOnly) return false;   // une case fx : rien sans retour n'y sert
        returns_ = r;
        sel_ = 0;
        unfold_ = kNone;
        filter();
        return true;
    }
    void setExpressionOnly(bool on) {
        exprOnly_ = on || spec_.expressionOnly;
        sel_ = 0;
        unfold_ = kNone;
        filter();
    }
    bool expand(std::string_view key, bool open) {
        const auto f = folderByKey(key);
        if (f == kNone || folders_[f].children.empty()) return false;
        remembered()[folders_[f].key] = open;
        rebuildVisible();
        invalidate();
        return true;
    }
    void showReturnsMenu(bool open) {
        rendOpen_ = open;
        rendHover_ = returns_;
        invalidate();
    }
    bool select(std::string_view name, int instance) {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].header.empty() && rows_[i].instance < 0 && upperOf(all_[rows_[i].entry].name) == upperOf(name)) {
                selectRow(static_cast<int>(i));
                if (instance < 0) return true;
                for (std::size_t k = 0; k < rows_.size(); ++k)
                    if (rows_[k].entry == rows_[static_cast<std::size_t>(sel_)].entry && rows_[k].instance == instance) {
                        selectRow(static_cast<int>(k));
                        return true;
                    }
                return false;
            }
        return false;
    }
    void move(int delta) {
        if (rows_.empty()) return;
        int at = sel_;
        const int step = delta > 0 ? 1 : -1;
        for (int left = std::abs(delta); left > 0;) {
            const int next = at + step;
            if (next < 0 || next >= static_cast<int>(rows_.size())) break;
            at = next;
            if (rows_[static_cast<std::size_t>(at)].header.empty()) --left;
        }
        if (!rows_[static_cast<std::size_t>(at)].header.empty()) return;
        selectRow(at);
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1080.f, r.w - 40.f), h = std::min(680.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        bottom_ = panel_.bottom() - 52.f;
        const float tools = 250.f + 190.f;     // la liste Rend, la puce Dans une expression
        search_->setBounds({panel_.x + 16.f, panel_.y + 54.f, std::max(120.f, panel_.w - 32.f - tools), 28.f});
        const float top = panel_.y + 96.f, bot = bottom_ - 10.f;
        fam_ = {panel_.x + 16.f, top, 260.f, bot - top};
        list_ = {fam_.right() + 8.f, top, 340.f, bot - top};
        detail_ = {list_.right() + 8.f, top, panel_.right() - 16.f - (list_.right() + 8.f), bot - top};
        const float bw = 120.f;
        ok_->setBounds({panel_.right() - 16.f - bw, bottom_ + 10.f, bw, 32.f});
        cancel_->setBounds({ok_->bounds().x - 10.f - 100.f, bottom_ + 10.f, 100.f, 32.f});
        example_->setBounds({cancel_->bounds().x - 10.f - 180.f, bottom_ + 10.f, 180.f, 32.f});
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
        famScroll_ = std::clamp(famScroll_, 0.f, maxFamScroll());
        if (revealPending_) {
            revealPending_ = false;
            reveal();
        }
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        violet_ = ctx.theme.onSurface(ctx.theme.tone(ui::Tone::InOut, c.accent));
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        // ---- le titre : d'ou l'on ecrit, ce qui est attendu ----
        const std::string title = "Choisir une fonction" + (spec_.from.empty() ? std::string{} : " depuis " + spec_.from);
        r.drawText({panel_.x + 16.f, panel_.y + 13.f}, title, kTitle, c.text);
        if (!spec_.expected.empty()) {
            const std::string want = "attendu ici : " + spec_.expected;
            const float tx = panel_.x + 16.f + r.measure(title, kTitle).width + 16.f;
            const gfx::Rect b{tx, panel_.y + 12.f, r.measure(want, kSmall).width + 16.f, 22.f};
            r.fillRoundedRect(b, c.accent.withAlpha(40), 10.f);
            r.drawText({b.x + 8.f, b.y + (b.h - r.lineHeight(kSmall)) * 0.5f}, want, kSmall, c.accent);
        }
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, hoverClose_ ? c.text : c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        // ---- Rend, Dans une expression ----
        float x = search_->bounds().right() + 10.f;
        {
            const float lw = r.measure("Rend", kSmall).width;
            r.drawText({x, panel_.y + 55.f + (26.f - r.lineHeight(kSmall)) * 0.5f}, "Rend", kSmall, c.textMuted);
            rendBtn_ = {x + lw + 6.f, panel_.y + 55.f, 236.f - lw - 6.f, 26.f};
            r.fillRoundedRect(rendBtn_, rendOpen_ ? c.accent.withAlpha(50) : c.headerBg, 6.f);
            r.strokeRect(rendBtn_, rendOpen_ ? c.accent : c.border, 1.f);
            const std::string label = HmiFunctionPicker::returnsLabel(returns_, spec_.expected);
            r.drawText({rendBtn_.x + 8.f, rendBtn_.y + (26.f - r.lineHeight(kSmall)) * 0.5f}, fit(r, label, kSmall, rendBtn_.w - 30.f), kSmall, c.text);
            r.drawText({rendBtn_.right() - 18.f, rendBtn_.y + (26.f - r.lineHeight(kSmall)) * 0.5f}, "\xE2\x96\xBE", kSmall, c.textMuted);
            x = rendBtn_.right() + 10.f;
        }
        {
            const std::string label = "Dans une expression";
            const float tw = r.measure(label, kSmall).width + 22.f;
            exprChip_ = {x, panel_.y + 55.f, tw, 26.f};
            r.fillRoundedRect(exprChip_, exprOnly_ ? c.accent.withAlpha(60) : c.headerBg, 12.f);
            if (exprOnly_) r.strokeRect(exprChip_, spec_.expressionOnly ? c.border : c.accent, 1.f);
            r.drawText({exprChip_.x + 11.f, exprChip_.y + (26.f - r.lineHeight(kSmall)) * 0.5f}, label, kSmall,
                       exprOnly_ && !spec_.expressionOnly ? c.text : c.textMuted);
        }
        paintFolders(ctx);
        paintList(ctx);
        paintDetail(ctx);
        // ---- le pied ----
        r.fillRect({panel_.x, bottom_, panel_.w, 1.f}, c.border);
        const std::string help = "Haut, Bas : parcourir \xC2\xB7 Entr\xC3\xA9" "e : ins\xC3\xA9rer \xC2\xB7 Ctrl+Entr\xC3\xA9" "e : ouvrir la d\xC3\xA9" "finition \xC2\xB7 \xC3\x89" "chap : fermer";
        r.drawText({panel_.x + 16.f, bottom_ + 18.f}, fit(r, help, kSmall, example_->bounds().x - panel_.x - 32.f), kSmall, c.textMuted);
        const auto* e = selectedEntry();
        const bool isMine = e && mine(*e);
        if (isMine != exampleIsOpen_) {
            exampleIsOpen_ = isMine;
            example_->setText(isMine ? "Ouvrir sa d\xC3\xA9" "finition" : "Ins\xC3\xA9rer l'exemple");
            example_->setTooltip(isMine ? "Le sous-onglet Fonctions, sur elle (Ctrl+Entr\xC3\xA9" "e)" : "L'exemple de la fiche, \xC3\xA0 la place de l'appel seul");
        }
        ok_->setEnabled(e != nullptr && !(e->byInstance && e->instances.empty() && selectedInstance() < 0));
        example_->setEnabled(e != nullptr && (isMine ? e->function != 0 : !e->example.empty()));
        if (rendOpen_) paintReturnsMenu(ctx);
    }

    void paintFolders(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(fam_, c.headerBg);
        r.strokeRect(fam_, c.border, 1.f);
        famRects_.clear();
        r.pushClip(fam_);
        float fy = fam_.y + 4.f - famScroll_;
        const auto row = [&](std::size_t f, const std::string& label, std::size_t count, int depth, bool group, bool isMine, bool here,
                             bool hasChildren, bool open) {
            const gfx::Rect rr{fam_.x + 1.f, fy, fam_.w - 2.f, kRow};
            const bool on = folder_ == f;
            if (on) r.fillRect(rr, c.selectionBg);
            const gfx::Color base = isMine ? violet_ : c.text;
            const gfx::Color t = on ? c.selectionText : base;
            const float lx = rr.x + 8.f + static_cast<float>(depth) * kIndent;
            const float ty = rr.y + (kRow - r.lineHeight(group ? kBody : kSmall)) * 0.5f;
            if (hasChildren && !group) r.drawText({lx, rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", kSmall, on ? c.selectionText : c.textMuted);
            const float textX = lx + (group ? 0.f : kIndent);
            const std::string n = std::to_string(count);
            const float nw = r.measure(n, kSmall).width;
            float room = rr.right() - 10.f - nw - 8.f - textX;
            float tagW = 0.f;
            if (here) {
                tagW = r.measure("ici", kSmall).width + 12.f;
                room -= tagW + 6.f;
            }
            const std::string shown = (isMine && !hasChildren && !group ? "\xC6\x92 " : "") + label;
            const std::string fitted = fit(r, shown, group ? kBody : kSmall, room);
            r.drawText({textX, ty}, fitted, group ? kBody : kSmall, count == 0 && !on ? c.textMuted : t);
            if (here) {
                const float hx = textX + r.measure(fitted, group ? kBody : kSmall).width + 6.f;
                const gfx::Rect tag{hx, rr.y + 4.f, tagW, kRow - 8.f};
                r.fillRoundedRect(tag, violet_.withAlpha(on ? 120 : 50), 7.f);
                r.drawText({tag.x + 6.f, tag.y + (tag.h - r.lineHeight(kSmall)) * 0.5f}, "ici", kSmall, on ? c.selectionText : violet_);
            }
            r.drawText({rr.right() - 10.f - nw, rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, n, kSmall, on ? c.selectionText : c.textMuted);
            famRects_.push_back({rr, f, lx + kIndent});
            fy += kRow;
        };
        row(kNone, "Toutes", total_, 0, true, false, false, false, true);
        for (const auto f : visible_) {
            const auto& fo = folders_[f];
            if (fo.depth == 0) fy += 6.f;
            row(f, fo.label, counts_[f], fo.depth == 0 ? 0 : fo.depth - 1, fo.depth == 0, fo.mine, f == here_, !fo.children.empty(), isOpen(f));
        }
        famContent_ = fy + famScroll_ - fam_.y + 4.f;
        r.popClip();
        famBar_.paint(ctx, fam_, famContent_, fam_.h, famScroll_);
    }

    void paintList(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(list_, c.windowBg);
        r.strokeRect(list_, c.border, 1.f);
        rowRects_.clear();
        r.pushClip(list_);
        for (std::size_t k = 0; k < rows_.size(); ++k) {
            const float y = list_.y + rowY_[k] - scroll_;
            const float h = rowH(k);
            if (y + h < list_.y) continue;
            if (y > list_.bottom()) break;
            const auto& row = rows_[k];
            const gfx::Rect rr{list_.x + 1.f, y, list_.w - 2.f, h};
            if (!row.header.empty()) {
                r.drawText({rr.x + 10.f, rr.y + (h - r.lineHeight(kSmall)) * 0.5f + 2.f}, fit(r, row.header, kSmall, rr.w - 20.f), kSmall,
                           row.mine ? violet_ : c.textMuted);
                r.fillRect({rr.x + 8.f, rr.bottom() - 1.f, rr.w - 16.f, 1.f}, (row.mine ? violet_ : c.border).withAlpha(120));
                continue;
            }
            rowRects_.push_back({rr, static_cast<int>(k)});
            const auto& e = all_[row.entry];
            const bool on = static_cast<int>(k) == sel_;
            if (on) r.fillRect(rr, c.selectionBg);
            else if (static_cast<int>(k) == hover_) r.fillRect(rr, c.rowAltBg);
            const gfx::Color t = on ? c.selectionText : (mine(e) ? violet_ : c.text);
            const gfx::Color m = on ? c.selectionText : c.textMuted;
            if (row.instance >= 0) {
                const auto call = HmiFunctionPicker::callOf(e, row.instance).text;
                r.drawText({rr.x + 28.f, rr.y + (h - r.lineHeight(kSmall)) * 0.5f}, fit(r, "\xE2\x86\xB3 " + call, kMono, rr.w - 38.f), kMono, t);
                continue;
            }
            const std::string call = e.byInstance ? e.name + callTail(e) : HmiFunctionPicker::callOf(e).text;
            r.drawText({rr.x + 10.f, rr.y + 4.f}, fit(r, call, kMono, rr.w - 20.f), kMono, t);
            r.drawText({rr.x + 10.f, rr.y + 21.f}, fit(r, rowNote(row), kSmall, rr.w - 20.f), kSmall, m);
            r.fillRect({rr.x, rr.bottom() - 1.f, rr.w, 1.f}, c.border.withAlpha(90));
        }
        if (rows_.empty())
            r.drawText({list_.x + 12.f, list_.y + 12.f}, fit(r, emptyText(), kSmall, list_.w - 24.f), kSmall, c.textMuted);
        r.popClip();
        bar_.paint(ctx, list_, contentH_, list_.h, scroll_);
    }

    void paintReturnsMenu(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto choices = returnCounts();
        const float rowH = 26.f;
        const float w = std::max(rendBtn_.w + 60.f, 300.f);
        const gfx::Rect box{rendBtn_.x, rendBtn_.bottom() + 2.f, w, rowH * static_cast<float>(choices.size()) + 8.f};
        r.fillRect({box.x + 3.f, box.y + 3.f, box.w, box.h}, ctx.theme.brand.cardShadow);
        r.fillRect(box, c.panelBg);
        r.strokeRect(box, c.borderStrong, 1.f);
        rendRects_.clear();
        float y = box.y + 4.f;
        for (const auto& [ret, n] : choices) {
            const gfx::Rect rr{box.x + 1.f, y, box.w - 2.f, rowH};
            const bool on = ret == returns_;
            const bool grey = ret == Returns::Void && spec_.expressionOnly;
            if (on) r.fillRect(rr, c.selectionBg);
            else if (ret == rendHover_ && !grey) r.fillRect(rr, c.rowAltBg);
            const gfx::Color t = grey ? c.textMuted.withAlpha(120) : on ? c.selectionText : c.text;
            const std::string count = std::to_string(n);
            const float nw = r.measure(count, kSmall).width;
            r.drawText({rr.x + 10.f, rr.y + (rowH - r.lineHeight(kSmall)) * 0.5f},
                       fit(r, HmiFunctionPicker::returnsLabel(ret, spec_.expected), kSmall, rr.w - 30.f - nw), kSmall, t);
            r.drawText({rr.right() - 10.f - nw, rr.y + (rowH - r.lineHeight(kSmall)) * 0.5f}, count, kSmall, grey ? t : on ? c.selectionText : c.textMuted);
            if (!grey) rendRects_.push_back({rr, ret});
            y += rowH;
        }
        rendBox_ = box;
    }

    void paintDetail(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(detail_, c.windowBg);
        r.strokeRect(detail_, c.border, 1.f);
        const auto* e = selectedEntry();
        if (!e) return;
        r.pushClip(detail_);
        const float x = detail_.x + 12.f, w = detail_.w - 24.f;
        float y = detail_.y + 10.f;
        const auto text = [&](const std::string& s, gfx::FontId f, gfx::Color col, float gap = 2.f) {
            for (const auto& line : wrap(r, s, f, w)) {
                r.drawText({x, y}, line, f, col);
                y += r.lineHeight(f) + gap;
            }
        };
        const auto codeBox = [&](const std::vector<std::string>& lines) {
            const float h = static_cast<float>(lines.size()) * (r.lineHeight(kMono) + 2.f) + 12.f;
            r.fillRect({x, y, w, h}, c.headerBg);
            float ly = y + 6.f;
            for (const auto& l : lines) {
                r.drawText({x + 8.f, ly}, fit(r, l, kMono, w - 16.f), kMono, c.text);
                ly += r.lineHeight(kMono) + 2.f;
            }
            y += h + 8.f;
        };
        const bool isMine = mine(*e);
        // Le chemin de son dossier.
        std::string path = isMine ? e->where : kNatives;
        if (!isMine)
            for (const auto& p : e->path) path += kArrow + p;
        if (isMine && !e->access.empty()) path += " \xC2\xB7 " + e->access;
        text(path, kSmall, isMine ? violet_ : c.textMuted);
        y += 4.f;
        text(e->signature, kMono, isMine ? violet_ : c.accent);
        y += 6.f;
        text(e->summary, kBody, c.text);
        if (!e->params.empty()) {
            y += 8.f;
            r.drawText({x, y}, "Ses arguments", kSmall, c.textMuted);
            y += r.lineHeight(kSmall) + 4.f;
            for (const auto& p : e->params) {
                const std::string head = p.name + (p.optional ? " (facultatif)" : std::string{});
                r.drawText({x, y}, fit(r, head, kMono, w * 0.34f), kMono, c.text);
                const float tx = x + w * 0.36f;
                const auto lines = wrap(r, p.type + (p.role.empty() ? std::string{} : " \xE2\x80\x94 " + p.role), kSmall, w - (tx - x));
                for (const auto& l : lines) {
                    r.drawText({tx, y + 1.f}, l, kSmall, c.textMuted);
                    y += r.lineHeight(kSmall) + 2.f;
                }
                if (lines.empty()) y += r.lineHeight(kSmall) + 2.f;
            }
        }
        y += 8.f;
        text("Rend : " + (e->returns.empty() ? std::string("rien (sans retour)") : e->returns) + "   \xC2\xB7   " + e->family
                 + (e->expression ? std::string{} : "   \xC2\xB7   instruction seule"),
             kSmall, c.textMuted);
        y += 8.f;
        if (isMine) {
            // L'appel tel qu'il s'ecrit d'ici ; une fonction de symbole : par une instance.
            const std::string from = spec_.from.empty() ? std::string("Programmation g\xC3\xA9n\xC3\xA9rale") : spec_.from;
            if (e->byInstance && e->instances.empty()) {
                text("Depuis " + from + ", elle s'appelle par une instance du symbole : il n'y en a aucune. Posez-en une pour l'appeler d'ici.", kSmall,
                     c.textMuted);
            } else {
                const int inst = selectedInstance();
                text(e->byInstance ? "Depuis " + from + ", elle s'appelle par une instance" + (e->instances.size() > 1 ? " (" + std::to_string(e->instances.size()) + ")" : std::string{})
                                   : "Depuis " + from + ", elle s'appelle",
                     kSmall, c.textMuted);
                y += 2.f;
                codeBox({HmiFunctionPicker::callOf(*e, inst).text});
                if (e->byInstance && e->instances.size() > 1 && inst < 0) text("D\xC3\xA9pli\xC3\xA9" "e dans la liste : une ligne par instance.", kSmall, c.textMuted);
            }
            if (!e->code.empty()) {
                y += 4.f;
                r.drawText({x, y}, "Son code", kSmall, c.textMuted);
                y += r.lineHeight(kSmall) + 4.f;
                codeBox(linesOf(e->code));
            }
        } else if (!e->example.empty()) {
            r.drawText({x, y}, "Exemple", kSmall, c.textMuted);
            y += r.lineHeight(kSmall) + 4.f;
            codeBox(linesOf(e->example));
        }
        r.popClip();
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (rendOpen_) {
            if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
                Returns h = Returns::All;
                bool any = false;
                for (const auto& [rect, ret] : rendRects_)
                    if (rect.contains(m->pos)) {
                        h = ret;
                        any = true;
                    }
                if (!any) h = static_cast<Returns>(255);
                if (h != rendHover_) {
                    rendHover_ = h;
                    invalidate();
                }
                return ui::EventResult::Consumed;
            }
            if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
                for (const auto& [rect, ret] : rendRects_)
                    if (rect.contains(d->pos)) {
                        rendOpen_ = false;
                        (void)setReturns(ret);
                        return ui::EventResult::Consumed;
                    }
                rendOpen_ = rendBox_.contains(d->pos);   // un clic ailleurs la ferme
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (std::holds_alternative<ui::MouseWheel>(ev)) return ui::EventResult::Consumed;
        }
        {
            float off = scroll_;
            if (bar_.handle(*this, ev, off)) {
                scroll_ = std::clamp(off, 0.f, maxScroll());
                invalidate();
                return ui::EventResult::Consumed;
            }
            float foff = famScroll_;
            if (famBar_.handle(*this, ev, foff)) {
                famScroll_ = std::clamp(foff, 0.f, maxFamScroll());
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            int h = -1;
            for (const auto& [rect, k] : rowRects_)
                if (rect.contains(m->pos) && list_.contains(m->pos)) h = k;
            const bool hc = close_.contains(m->pos);
            if (h != hover_ || hc != hoverClose_) {
                hover_ = h;
                hoverClose_ = hc;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (list_.contains(w->pos)) {
                scroll_ = std::clamp(scroll_ - w->dy * kItem * 3.f, 0.f, maxScroll());
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (fam_.contains(w->pos)) {
                famScroll_ = std::clamp(famScroll_ - w->dy * kRow * 3.f, 0.f, maxFamScroll());
                invalidate();
                return ui::EventResult::Consumed;
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            // 1.12.18 : le clic droit sur une ligne la choisit et ouvre son menu.
            if (d->button == ui::MouseButton::Right)
                for (const auto& [rect, k] : rowRects_)
                    if (rect.contains(d->pos) && list_.contains(d->pos)) {
                        selectRow(k);
                        owner_.openRowMenu(d->pos);
                        return ui::EventResult::Consumed;
                    }
            if (d->button != ui::MouseButton::Left) return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
            if (close_.contains(d->pos)) {
                owner_.finish(false);
                return ui::EventResult::Consumed;
            }
            if (rendBtn_.contains(d->pos)) {
                rendOpen_ = true;
                rendHover_ = returns_;
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (exprChip_.contains(d->pos)) {
                if (!spec_.expressionOnly) setExpressionOnly(!exprOnly_);   // une case fx : cochee d'office
                return ui::EventResult::Consumed;
            }
            for (const auto& fr : famRects_)
                if (fr.rect.contains(d->pos) && fam_.contains(d->pos)) {
                    const bool onArrow = d->pos.x < fr.arrowEnd;
                    if (fr.folder != kNone && !folders_[fr.folder].children.empty() && folders_[fr.folder].depth > 0
                        && (onArrow || folder_ == fr.folder)) {
                        (void)expand(folders_[fr.folder].key, !isOpen(fr.folder));
                        if (onArrow) return ui::EventResult::Consumed;
                    }
                    if (folder_ != fr.folder) (void)setFolder(fr.folder == kNone ? std::string_view{} : std::string_view(folders_[fr.folder].key));
                    return ui::EventResult::Consumed;
                }
            for (const auto& [rect, k] : rowRects_) {
                if (!rect.contains(d->pos) || !list_.contains(d->pos)) continue;
                selectRow(k);
                if (d->clickCount >= 2) owner_.insert(false);
                return ui::EventResult::Consumed;
            }
            return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Folder {
        std::string              key, label;   // "Natives > Conteneurs > Listes", "Listes"
        std::size_t              parent{kNone};
        int                      depth{0};     // 0 : un groupe (Natives, Mes fonctions)
        bool                     mine{false};
        bool                     openByDefault{false};
        std::vector<std::size_t> children, entries;
    };
    struct Row {
        std::size_t entry{kNone};
        int         instance{-1};   // une instance d'une fonction de symbole
        std::string header{};       // un titre (" Ailleurs dans Mes fonctions ")
        bool        mine{false};
        bool        elsewhere{false};
    };
    struct FolderRect {
        gfx::Rect   rect;
        std::size_t folder;
        float       arrowEnd;
    };

    [[nodiscard]] static bool mine(const Entry& e) { return e.group != kNatives; }

    void buildFolders() {
        folders_.clear();
        const auto group = [&](const std::string& name, bool isMine) {
            Folder f;
            f.key = name;
            f.label = name;
            f.mine = isMine;
            f.openByDefault = true;
            folders_.push_back(std::move(f));
            return folders_.size() - 1;
        };
        const std::size_t natives = group(kNatives, false);
        const std::size_t mineGroup = group(kMine, true);
        entryFolder_.assign(all_.size(), kNone);
        for (std::size_t i = 0; i < all_.size(); ++i) {
            const auto& e = all_[i];
            std::size_t at = mine(e) ? mineGroup : natives;
            for (std::size_t p = 0; p < e.path.size(); ++p) {
                std::size_t found = kNone;
                for (const auto ch : folders_[at].children)
                    if (folders_[ch].label == e.path[p]) found = ch;
                if (found == kNone) {
                    Folder f;
                    f.key = folders_[at].key + kArrow + e.path[p];
                    f.label = e.path[p];
                    f.parent = at;
                    f.depth = folders_[at].depth + 1;
                    f.mine = mine(e);
                    f.openByDefault = f.mine && f.depth == 1;   // les onglets du projet ; les natives : fermees
                    folders_.push_back(std::move(f));
                    found = folders_.size() - 1;
                    folders_[at].children.push_back(found);
                }
                at = found;
                // Le dossier d'ou l'on vient : la vue (sous Vues), Fonctions IHM ; ouvert jusqu'a lui.
                if (e.here && p == std::min<std::size_t>(1, e.path.size() - 1)) here_ = at;
            }
            folders_[at].entries.push_back(i);
            entryFolder_[i] = at;
            if (e.here)
                for (auto f = at; f != kNone; f = folders_[f].parent) folders_[f].openByDefault = true;
        }
        // Peu de dossiers a moi : tous ouverts (la maquette) ; beaucoup : les onglets et ici.
        std::size_t mineCount = 0;
        for (const auto& f : folders_) mineCount += f.mine && f.depth > 0 ? 1 : 0;
        if (mineCount <= 24)
            for (auto& f : folders_)
                if (f.mine) f.openByDefault = true;
    }

    [[nodiscard]] std::size_t folderByKey(std::string_view key) const {
        for (std::size_t f = 0; f < folders_.size(); ++f)
            if (folders_[f].key == key) return f;
        return kNone;
    }
    [[nodiscard]] bool isOpen(std::size_t f) const {
        if (f == kNone || folders_[f].depth == 0) return true;
        const auto it = remembered().find(folders_[f].key);
        return it != remembered().end() ? it->second : folders_[f].openByDefault;
    }
    [[nodiscard]] bool inside(std::size_t f, std::size_t root) const {
        for (; f != kNone; f = folders_[f].parent)
            if (f == root) return true;
        return false;
    }
    void rebuildVisible() {
        visible_.clear();
        std::function<void(std::size_t)> walk = [&](std::size_t f) {
            visible_.push_back(f);
            if (!isOpen(f)) return;
            for (const auto ch : folders_[f].children) walk(ch);
        };
        for (std::size_t g = 0; g < folders_.size() && folders_[g].depth == 0; ++g)
            if (!folders_[g].children.empty() || !folders_[g].entries.empty()) walk(g);
    }

    [[nodiscard]] bool fitsReturns(const Entry& e, Returns r) const {
        switch (r) {
            case Returns::All: return true;
            case Returns::Fits: return spec_.fits ? spec_.fits(e.returns) : upperOf(e.returns) == upperOf(spec_.expected);
            default: return HmiFunctionPicker::classify(e.returns) == r;
        }
    }
    [[nodiscard]] bool keeps(const Entry& e, Returns r) const {
        if (exprOnly_ && !e.expression) return false;
        if (!fitsReturns(e, r)) return false;
        return query_.matches({e.name, e.summary});
    }

    void filter() {
        query_ = ui::SearchQuery(search_->text());
        passes_.assign(all_.size(), false);
        counts_.assign(folders_.size(), 0);
        total_ = 0;
        for (std::size_t i = 0; i < all_.size(); ++i) {
            if (!keeps(all_[i], returns_)) continue;
            passes_[i] = true;
            ++total_;
            for (auto f = entryFolder_[i]; f != kNone; f = folders_[f].parent) ++counts_[f];
        }
        rebuildVisible();
        rebuildRows();
        if (sel_ >= static_cast<int>(rows_.size())) sel_ = static_cast<int>(rows_.size()) - 1;
        if (sel_ < 0 && !rows_.empty()) sel_ = 0;
        while (sel_ >= 0 && sel_ < static_cast<int>(rows_.size()) && !rows_[static_cast<std::size_t>(sel_)].header.empty()) ++sel_;
        if (sel_ >= static_cast<int>(rows_.size())) sel_ = rows_.empty() ? 0 : static_cast<int>(rows_.size()) - 1;
        syncUnfold();
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
        invalidate();
    }

    // Une recherche : le nom qui commence par le texte d'abord.
    void bySearch(std::vector<std::size_t>& order) const {
        if (query_.empty() || query_.terms().empty()) return;
        const std::string head = query_.terms().front();
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            const bool pa = ui::foldForSearch(all_[a].name).rfind(head, 0) == 0, pb = ui::foldForSearch(all_[b].name).rfind(head, 0) == 0;
            return pa && !pb;
        });
    }
    void rebuildRows() {
        rows_.clear();
        const auto push = [&](std::size_t i, bool elsewhere) {
            rows_.push_back({i, -1, {}, mine(all_[i]), elsewhere});
            if (unfold_ == i && all_[i].byInstance)
                for (std::size_t k = 0; k < all_[i].instances.size(); ++k) rows_.push_back({i, static_cast<int>(k), {}, true, elsewhere});
        };
        std::vector<std::size_t> order;
        if (folder_ == kNone) {
            // Toutes : les miennes d'ici, les autres miennes, puis les natives.
            for (int pass = 0; pass < 3; ++pass)
                for (std::size_t i = 0; i < all_.size(); ++i) {
                    if (!passes_[i]) continue;
                    const bool m = mine(all_[i]);
                    if ((pass == 0 && m && all_[i].here) || (pass == 1 && m && !all_[i].here) || (pass == 2 && !m)) order.push_back(i);
                }
            bySearch(order);
            for (const auto i : order) push(i, false);
        } else {
            std::function<void(std::size_t)> walk = [&](std::size_t f) {
                for (const auto i : folders_[f].entries)
                    if (passes_[i]) order.push_back(i);
                for (const auto ch : folders_[f].children) walk(ch);
            };
            walk(folder_);
            bySearch(order);
            for (const auto i : order) push(i, false);
            if (folders_[folder_].mine && folders_[folder_].depth > 0) {
                std::vector<std::size_t> rest;
                for (std::size_t i = 0; i < all_.size(); ++i)
                    if (passes_[i] && mine(all_[i]) && !inside(entryFolder_[i], folder_)) rest.push_back(i);
                bySearch(rest);
                if (!rest.empty()) {
                    rows_.push_back({kNone, -1, "Ailleurs dans Mes fonctions", true, true});
                    for (const auto i : rest) push(i, true);
                }
            }
        }
        rowY_.assign(rows_.size(), 0.f);
        float y = 0.f;
        for (std::size_t k = 0; k < rows_.size(); ++k) {
            rowY_[k] = y;
            y += rowH(k);
        }
        contentH_ = y;
    }
    [[nodiscard]] float rowH(std::size_t k) const {
        return !rows_[k].header.empty() ? kSub + 4.f : rows_[k].instance >= 0 ? kSub : kItem;
    }
    [[nodiscard]] const Row* selectedRow() const {
        if (sel_ < 0 || static_cast<std::size_t>(sel_) >= rows_.size()) return nullptr;
        const auto& row = rows_[static_cast<std::size_t>(sel_)];
        return row.header.empty() ? &row : nullptr;
    }
    // Une fonction de symbole choisie se deplie sur ses instances.
    void syncUnfold() {
        const auto* row = selectedRow();
        const std::size_t want = row && all_[row->entry].byInstance && all_[row->entry].instances.size() > 0 ? row->entry : kNone;
        if (want == unfold_) return;
        const std::size_t entry = row ? row->entry : kNone;
        const int instance = row ? row->instance : -1;
        unfold_ = want;
        rebuildRows();
        for (std::size_t k = 0; k < rows_.size(); ++k)
            if (rows_[k].header.empty() && rows_[k].entry == entry && rows_[k].instance == instance) sel_ = static_cast<int>(k);
    }
    void selectRow(int k) {
        if (k < 0 || static_cast<std::size_t>(k) >= rows_.size() || !rows_[static_cast<std::size_t>(k)].header.empty()) return;
        sel_ = k;
        syncUnfold();
        reveal();
        invalidate();
    }
    [[nodiscard]] std::string callTail(const Entry& e) const {
        const auto a = HmiFunctionPicker::callOf(e);
        const auto open = a.text.find('(');
        return open == std::string::npos ? std::string("()") : a.text.substr(open);
    }
    [[nodiscard]] std::string rowNote(const Row& row) const {
        const auto& e = all_[row.entry];
        std::string s = e.returns.empty() ? std::string("sans retour") : "rend " + e.returns;
        if (mine(e)) {
            const bool own = folder_ != kNone && !row.elsewhere && folders_[folder_].mine && folders_[folder_].depth > 0;
            if (own) {
                if (!e.access.empty()) s += " \xC2\xB7 " + e.access;
            } else {
                s += " \xC2\xB7 " + e.family + (e.here ? " (ici)" : "");
                if (e.access.rfind("priv", 0) == 0) s += " \xC2\xB7 " + e.access;
            }
            if (e.byInstance) s += e.instances.empty() ? " \xC2\xB7 aucune instance" : " \xC2\xB7 par une instance (" + std::to_string(e.instances.size()) + ")";
        } else {
            s += " \xC2\xB7 " + e.family;
        }
        if (!e.expression) s += " \xC2\xB7 pas dans une expression";
        return s;
    }
    [[nodiscard]] std::string emptyText() const {
        if (returns_ == Returns::Fits) return "Aucune ne convient : choisissez \xC2\xAB Tout \xC2\xBB dans Rend.";
        if (returns_ != Returns::All) return "Aucune ne rend cela ici : choisissez \xC2\xAB Tout \xC2\xBB dans Rend.";
        return "Aucune fonction ne correspond \xC3\xA0 la recherche.";
    }
    [[nodiscard]] float maxScroll() const { return std::max(0.f, contentH_ - list_.h); }
    [[nodiscard]] float maxFamScroll() const { return std::max(0.f, famContent_ - fam_.h); }
    void reveal() {
        if (list_.h <= 0.f) {
            revealPending_ = true;
            return;
        }
        if (sel_ < 0 || static_cast<std::size_t>(sel_) >= rows_.size()) return;
        const float top = rowY_[static_cast<std::size_t>(sel_)];
        const float h = rowH(static_cast<std::size_t>(sel_));
        if (top < scroll_) scroll_ = top;
        else if (top + h > scroll_ + list_.h) scroll_ = top + h - list_.h;
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }

    HmiFunctionPicker&                     owner_;
    const Spec&                            spec_;
    std::vector<Entry>                     all_;
    std::vector<Folder>                    folders_;
    std::vector<std::size_t>               entryFolder_, visible_, counts_;
    std::vector<bool>                      passes_;
    std::vector<Row>                       rows_;
    std::vector<float>                     rowY_;
    std::size_t                            total_{0}, folder_{kNone}, here_{kNone}, unfold_{kNone};
    ui::SearchQuery                        query_;
    Returns                                returns_{Returns::All};
    Returns                                rendHover_{Returns::All};
    bool                                   exprOnly_{false}, rendOpen_{false}, exampleIsOpen_{false};
    int                                    sel_{0}, hover_{-1};
    bool                                   hoverClose_{false}, revealPending_{false};
    float                                  scroll_{0.f}, famScroll_{0.f}, famContent_{0.f}, contentH_{0.f}, bottom_{0.f};
    gfx::Color                             violet_{};
    ui::InputText*                         search_{nullptr};
    ui::Button*                            example_{nullptr};
    ui::Button*                            cancel_{nullptr};
    ui::Button*                            ok_{nullptr};
    gfx::Rect                              panel_{}, fam_{}, list_{}, detail_{}, close_{}, rendBtn_{}, rendBox_{}, exprChip_{};
    ui::PaintedScrollBar                   bar_, famBar_;
    std::vector<std::pair<gfx::Rect, Returns>> rendRects_;
    std::vector<FolderRect>                famRects_;
    std::vector<std::pair<gfx::Rect, int>> rowRects_;
    core::ConnectionScope                  links_;
};

// ============================================================== le dialogue ====
HmiFunctionPicker::HmiFunctionPicker(Spec spec) : menu::WidgetMenu("dialog.functionPicker"), spec_(std::move(spec)) {}
HmiFunctionPicker::~HmiFunctionPicker() = default;

menu::MenuTraits HmiFunctionPicker::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiFunctionPicker::title() const { return "Choisir une fonction"; }

core::Status HmiFunctionPicker::buildUi() {
    auto body = std::make_unique<Body>(*this, spec_);
    body_ = body.get();
    setRoot(std::move(body));
    return core::ok();
}

void HmiFunctionPicker::onEnter() {
    if (body_) body_->focusFirst();
}

ui::EventResult HmiFunctionPicker::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && body_) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return) {
            if (k->mods.ctrl) openDefinition();   // 1.12.17 : une de mes fonctions, sa definition
            else insert(false);
            return ui::EventResult::Consumed;
        }
        // 1.12.18 : les touches du menu de la ligne (F12 la definition, F1 la fiche, Ctrl+C le nom).
        if (k->key == ui::Key::F12 && k->mods.none()) {
            openDefinition();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::F1 && k->mods.none()) {
            openHelp();
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::C && k->mods.ctrl && !k->mods.shift && !k->mods.alt && !body_->searchFocused() && copyName())
            return ui::EventResult::Consumed;
        if (k->key == ui::Key::Down || k->key == ui::Key::Up) {
            body_->move(k->key == ui::Key::Down ? 1 : -1);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::PageDown || k->key == ui::Key::PageUp) {
            body_->move(k->key == ui::Key::PageDown ? 10 : -10);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

std::vector<HmiFunctionPicker::Entry> HmiFunctionPicker::natives() {
    namespace n = hmi::natives;
    std::vector<Entry> out;
    std::vector<std::string> path;
    // 1.12.17 : rangees comme l'arbre (les dossiers de shelves()) ; les X_TO_Y n'y sont pas.
    std::function<void(std::size_t)> walk = [&](std::size_t s) {
        const auto& sh = n::shelves()[s];
        if (s != 0) path.push_back(sh.title);
        for (const auto f : sh.folders) walk(f);
        for (const auto i : sh.functions) {
            const auto& f = n::functions()[i];
            Entry e;
            e.name = std::string(f.name);
            e.family = s == 0 ? std::string("Fonctions") : sh.title;
            e.group = kNatives;
            e.path = path;
            e.signature = n::signature(f);
            e.returns = std::string(f.returns);
            e.summary = std::string(f.summary);
            e.example = std::string(f.st);
            e.expression = f.expression;
            for (const auto& p : f.params) e.params.push_back({std::string(p.name), std::string(p.type), std::string(p.role), p.optional});
            out.push_back(std::move(e));
        }
        if (s != 0) path.pop_back();
    };
    if (!n::shelves().empty()) walk(0);
    return out;
}

HmiFunctionPicker::Answer HmiFunctionPicker::callOf(const Entry& e, int instance) {
    Answer a;
    a.name = e.name;
    const bool byInstance = instance >= 0 && static_cast<std::size_t>(instance) < e.instances.size();
    a.text = (byInstance ? e.instances[static_cast<std::size_t>(instance)] + "." : e.prefix) + e.name + "(";
    bool first = true;
    for (const auto& p : e.params) {
        if (p.optional) continue;
        if (!first) a.text += ", ";
        first = false;
        a.holes.push_back({static_cast<int>(a.text.size()), static_cast<int>(p.name.size())});
        a.text += p.name;
    }
    a.text += ")";
    return a;
}

HmiFunctionPicker::Returns HmiFunctionPicker::classify(std::string_view returns) {
    const std::string r = trimmedOf(returns);
    const std::string u = upperOf(r);
    if (r.empty() || u == "VOID") return Returns::Void;
    const auto cut = u.find_first_of(" ([,");
    const std::string head = cut == std::string::npos ? u : u.substr(0, cut);
    const auto in = [&head](std::initializer_list<const char*> names) {
        return std::any_of(names.begin(), names.end(), [&head](const char* n) { return head == n; });
    };
    if (u.rfind("UN ENTIER", 0) == 0) return Returns::Integer;
    const bool lowerCase = std::any_of(r.begin(), r.end(), [](char ch) { return std::islower(static_cast<unsigned char>(ch)) != 0; });
    if (in({"MAP_ITERATOR", "REF_TO"}) || u.rfind("POINTER TO", 0) == 0 || u.rfind("REF TO", 0) == 0) return Returns::Reference;
    if (in({"LIST", "SET", "VECTOR", "MAP", "ARRAY", "TUPLE", "RING"})) return Returns::Container;
    if (lowerCase || head == "T" || head.rfind("ANY", 0) == 0) return Returns::Generic;   // comme IN, T, le type du champ...
    if (u == "BOOL") return Returns::Bool;
    if (in({"SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT"})) return Returns::Integer;
    if (in({"REAL", "LREAL"})) return Returns::Real;
    if (in({"STRING", "WSTRING", "CHAR", "WCHAR"})) return Returns::Text;
    if (in({"TIME", "LTIME", "DATE", "LDATE", "TIME_OF_DAY", "TOD", "LTOD", "DATE_AND_TIME", "DT", "LDT"})) return Returns::Time;
    if (in({"BYTE", "WORD", "DWORD", "LWORD"})) return Returns::Word;
    return Returns::Other;     // un type du projet : une structure, une enumeration
}

std::string HmiFunctionPicker::returnsLabel(Returns r, std::string_view expected) {
    switch (r) {
        case Returns::All:       return "Tout";
        case Returns::Fits:      return "Ce qui convient ici" + (expected.empty() ? std::string{} : " : " + std::string(expected));
        case Returns::Void:      return "Sans retour (VOID)";
        case Returns::Bool:      return "BOOL";
        case Returns::Integer:   return "Entiers (INT, DINT\xE2\x80\xA6)";
        case Returns::Real:      return "R\xC3\xA9" "els (REAL, LREAL)";
        case Returns::Text:      return "Textes (STRING\xE2\x80\xA6)";
        case Returns::Time:      return "Dur\xC3\xA9" "es et dates";
        case Returns::Word:      return "Mots (BYTE, WORD\xE2\x80\xA6)";
        case Returns::Container: return "Conteneurs (LIST, SET\xE2\x80\xA6)";
        case Returns::Reference: return "R\xC3\xA9" "f\xC3\xA9rences (REF_TO\xE2\x80\xA6)";
        case Returns::Generic:   return "Comme son argument (ANY\xE2\x80\xA6)";
        case Returns::Other:     return "Vos types (structures, \xC3\xA9num\xC3\xA9rations)";
    }
    return {};
}

void HmiFunctionPicker::setSearch(const std::string& text) { if (body_) body_->setSearch(text); }
bool HmiFunctionPicker::setFolder(std::string_view path) { return body_ && body_->setFolder(path); }
void HmiFunctionPicker::setTyped(bool on) {
    if (body_) (void)body_->setReturns(on && !spec_.expected.empty() ? Returns::Fits : Returns::All);
}
bool HmiFunctionPicker::setReturns(Returns r) { return body_ && body_->setReturns(r); }
HmiFunctionPicker::Returns HmiFunctionPicker::returnsFilter() const { return body_ ? body_->returnsFilter() : Returns::All; }
std::vector<std::pair<HmiFunctionPicker::Returns, std::size_t>> HmiFunctionPicker::returnCounts() const {
    return body_ ? body_->returnCounts() : std::vector<std::pair<Returns, std::size_t>>{};
}
void HmiFunctionPicker::setExpressionOnly(bool on) { if (body_) body_->setExpressionOnly(on); }
bool HmiFunctionPicker::select(std::string_view name, int instance) { return body_ && body_->select(name, instance); }
bool HmiFunctionPicker::expand(std::string_view path, bool open) { return body_ && body_->expand(path, open); }
void HmiFunctionPicker::showReturnsMenu(bool open) { if (body_) body_->showReturnsMenu(open); }
std::vector<std::string> HmiFunctionPicker::shownNames() const { return body_ ? body_->shownNames() : std::vector<std::string>{}; }
std::vector<std::string> HmiFunctionPicker::folders() const { return body_ ? body_->folderKeys() : std::vector<std::string>{}; }
std::string HmiFunctionPicker::hereFolder() const { return body_ ? body_->hereKey() : std::string{}; }
std::string HmiFunctionPicker::selectedName() const {
    const auto* e = body_ ? body_->selectedEntry() : nullptr;
    return e ? e->name : std::string{};
}
std::string HmiFunctionPicker::selectedCall() const {
    const auto* e = body_ ? body_->selectedEntry() : nullptr;
    return e ? callOf(*e, body_->selectedInstance()).text : std::string{};
}

void HmiFunctionPicker::insert(bool example) {
    if (!body_ || done_) return;
    const auto* e = body_->selectedEntry();
    if (!e) return;
    const int instance = body_->selectedInstance();
    if (example && !e->example.empty() && e->group == kNatives) {
        answer_ = Answer{};
        answer_.name = e->name;
        answer_.text = e->example;
    } else {
        if (e->byInstance && e->instances.empty() && instance < 0) return;   // aucune instance : rien a appeler d'ici
        answer_ = callOf(*e, instance);
    }
    finish(true);
}

void HmiFunctionPicker::openDefinition() {
    if (!body_ || done_) return;
    const auto* e = body_->selectedEntry();
    if (!e || e->group == kNatives || e->function == 0) return;
    answer_ = Answer{};
    answer_.name = e->name;
    answer_.open = true;
    answer_.view = e->view;
    answer_.function = e->function;
    finish(true);
}

void HmiFunctionPicker::openHelp() {
    if (!body_ || done_) return;
    const auto* e = body_->selectedEntry();
    if (!e || e->group != kNatives) return;
    answer_ = Answer{};
    answer_.name = e->name;
    answer_.help = true;
    finish(true);
}

bool HmiFunctionPicker::copyName() {
    const auto* e = body_ ? body_->selectedEntry() : nullptr;
    if (!e) return false;
    ui::setClipboardText(e->prefix + e->name);
    return true;
}

void HmiFunctionPicker::openRowMenu(gfx::Point at) {
    enum : int { MChoose = 1, MCopy, MDefinition, MHelp };
    const auto* e = body_ ? body_->selectedEntry() : nullptr;
    if (!e || !body_) return;
    if (!rowMenu_) {
        rowMenu_ = &static_cast<ui::PopupMenu&>(body_->addChild(std::make_unique<ui::PopupMenu>(id() + ".rowmenu")));
        links_ += rowMenu_->itemChosen->connect([this](int action) {
            switch (action) {
                case MChoose: insert(false); break;
                case MCopy: (void)copyName(); break;
                case MDefinition: openDefinition(); break;
                case MHelp: openHelp(); break;
                default: break;
            }
        });
    }
    const bool native = e->group == kNatives;
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = e->prefix + e->name;
    head.shortcut = native ? std::string("native") : e->group;
    items.push_back(std::move(head));
    const auto entry = [](std::string label, int id, std::string key, bool enabled = true, std::string why = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.id = id;
        it.shortcut = std::move(key);
        it.enabled = enabled;
        it.disabledReason = enabled ? std::string{} : std::move(why);
        return it;
    };
    items.push_back(entry("Choisir", MChoose, "Entr\xC3\xA9" "e"));
    items.push_back(entry("Copier le nom", MCopy, "Ctrl+C"));
    if (native) items.push_back(entry("Aide sur " + e->name, MHelp, "F1"));
    else items.push_back(entry("Aller \xC3\xA0 la d\xC3\xA9" "finition", MDefinition, "F12", e->function != 0, "pas de d\xC3\xA9" "finition \xC3\xA0 ouvrir"));
    rowMenu_->setItems(std::move(items));
    rowMenu_->setBounds(body_->bounds());
    gfx::Size surface = ui::surfaceSize();
    if (surface.w <= 0.f || surface.h <= 0.f) surface = {body_->bounds().right() + 400.f, body_->bounds().bottom() + 400.f};
    rowMenu_->openAt(at, surface);
}

void HmiFunctionPicker::finish(bool ok) {
    if (done_) return;
    done_ = true;
    std::string holes;
    for (const auto& [at, len] : answer_.holes) holes += (holes.empty() ? "" : ",") + std::to_string(at) + ":" + std::to_string(len);
    const std::string payload = answer_.name + kSep + answer_.text + kSep + holes + kSep + (answer_.help ? "2" : answer_.open ? "1" : "0") + kSep
                                + std::to_string(answer_.view) + kSep + std::to_string(answer_.function);
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload});
}

HmiFunctionPicker::Answer HmiFunctionPicker::parse(const std::string& payload) {
    Answer a;
    std::vector<std::string> parts{""};
    for (const char ch : payload) {
        if (ch == kSep) parts.emplace_back();
        else parts.back() += ch;
    }
    a.name = parts[0];
    a.text = parts.size() > 1 ? parts[1] : std::string{};
    if (parts.size() > 2) {
        std::size_t i = 0;
        const std::string& h = parts[2];
        while (i < h.size()) {
            const auto comma = h.find(',', i);
            const std::string one = h.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
            const auto colon = one.find(':');
            if (colon != std::string::npos)
                a.holes.push_back({std::atoi(one.substr(0, colon).c_str()), std::atoi(one.substr(colon + 1).c_str())});
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
    }
    a.open = parts.size() > 3 && parts[3] == "1";
    a.help = parts.size() > 3 && parts[3] == "2";   // 1.12.18 : Aide sur LIMIT
    a.view = parts.size() > 4 ? std::strtoull(parts[4].c_str(), nullptr, 10) : 0;
    a.function = parts.size() > 5 ? std::strtoull(parts[5].c_str(), nullptr, 10) : 0;
    return a;
}

namespace functionpicker {
namespace {
Host& host() {
    static Host h;
    return h;
}
} // namespace
void setHost(Host h) { host() = std::move(h); }
bool available() { return static_cast<bool>(host()); }
void ask(HmiFunctionPicker::Spec spec, Done done) {
    if (host()) host()(std::move(spec), std::move(done));
}
} // namespace functionpicker

} // namespace app
