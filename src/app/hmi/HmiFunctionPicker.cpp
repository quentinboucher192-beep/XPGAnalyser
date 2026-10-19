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

namespace app {

namespace {

constexpr gfx::FontId kBody{15};
constexpr gfx::FontId kSmall{13};
constexpr gfx::FontId kMono{14};
constexpr gfx::FontId kTitle{17};
constexpr float       kRow = 24.f;      // une famille
constexpr float       kItem = 40.f;     // une fonction : son appel, ce qu'elle rend
constexpr char        kSep = '\x1F';

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

} // namespace

// ============================================================== le corps ====
class HmiFunctionPicker::Body final : public ui::Widget {
public:
    Body(HmiFunctionPicker& owner, const Spec& spec) : ui::Widget("dialog.functionPicker"), owner_(owner), spec_(spec) {
        all_ = spec_.extra;
        for (auto& e : HmiFunctionPicker::natives()) all_.push_back(std::move(e));
        typed_ = !spec_.expected.empty();
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
        links_ += example_->clicked->connect([this] { owner_.insert(true); });
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += ok_->clicked->connect([this] { owner_.insert(false); });
        filter();
    }

    // ---- l'etat ----
    [[nodiscard]] const Entry* selected() const {
        return sel_ >= 0 && static_cast<std::size_t>(sel_) < shown_.size() ? shown_[static_cast<std::size_t>(sel_)] : nullptr;
    }
    [[nodiscard]] std::vector<std::string> shownNames() const {
        std::vector<std::string> out;
        for (const auto* e : shown_) out.push_back(e->name);
        return out;
    }
    [[nodiscard]] std::vector<std::string> familyNames() const {
        std::vector<std::string> out;
        for (const auto& f : families_) out.push_back(f.name);
        return out;
    }
    [[nodiscard]] bool searchFocused() const { return search_->focused(); }
    void focusFirst() { owner_.focus().focus(search_); }
    void setSearch(const std::string& t) {
        search_->setText(t);
        filter();
    }
    bool setFamily(std::string_view f) {
        if (!f.empty() && std::none_of(all_.begin(), all_.end(), [&](const Entry& e) { return e.family == f; })) return false;
        family_ = std::string(f);
        sel_ = 0;
        scroll_ = 0.f;
        filter();
        return true;
    }
    void setTyped(bool on) {
        typed_ = on && !spec_.expected.empty();
        sel_ = 0;
        filter();
    }
    void setExpressionOnly(bool on) {
        exprOnly_ = on;
        sel_ = 0;
        filter();
    }
    bool select(std::string_view name) {
        for (std::size_t i = 0; i < shown_.size(); ++i)
            if (upperOf(shown_[i]->name) == upperOf(name)) {
                sel_ = static_cast<int>(i);
                reveal();
                invalidate();
                return true;
            }
        return false;
    }
    void move(int delta) {
        if (shown_.empty()) return;
        sel_ = std::clamp(sel_ + delta, 0, static_cast<int>(shown_.size()) - 1);
        reveal();
        invalidate();
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1040.f, r.w - 40.f), h = std::min(660.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        bottom_ = panel_.bottom() - 52.f;
        const float chipsW = (spec_.expected.empty() ? 0.f : 190.f) + 190.f;
        search_->setBounds({panel_.x + 16.f, panel_.y + 54.f, std::max(120.f, panel_.w - 32.f - chipsW), 28.f});
        const float top = panel_.y + 96.f, bot = bottom_ - 10.f;
        fam_ = {panel_.x + 16.f, top, 230.f, bot - top};
        list_ = {fam_.right() + 8.f, top, 330.f, bot - top};
        detail_ = {list_.right() + 8.f, top, panel_.right() - 16.f - (list_.right() + 8.f), bot - top};
        const float bw = 120.f;
        ok_->setBounds({panel_.right() - 16.f - bw, bottom_ + 10.f, bw, 32.f});
        cancel_->setBounds({ok_->bounds().x - 10.f - 100.f, bottom_ + 10.f, 100.f, 32.f});
        example_->setBounds({cancel_->bounds().x - 10.f - 170.f, bottom_ + 10.f, 170.f, 32.f});
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
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        // ---- le titre ----
        r.drawText({panel_.x + 16.f, panel_.y + 13.f}, "Choisir une fonction", kTitle, c.text);
        if (!spec_.expected.empty()) {
            const std::string want = "attendu ici : " + spec_.expected;
            const float tx = panel_.x + 16.f + r.measure("Choisir une fonction", kTitle).width + 16.f;
            const gfx::Rect b{tx, panel_.y + 12.f, r.measure(want, kSmall).width + 16.f, 22.f};
            r.fillRoundedRect(b, c.accent.withAlpha(40), 10.f);
            r.drawText({b.x + 8.f, b.y + (b.h - r.lineHeight(kSmall)) * 0.5f}, want, kSmall, c.accent);
        }
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, hoverClose_ ? c.text : c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        // ---- les deux filtres ----
        chips_.clear();
        float x = search_->bounds().right() + 10.f;
        const auto chip = [&](const std::string& label, int id, bool on) {
            const float tw = r.measure(label, kSmall).width + 22.f;
            const gfx::Rect b{x, panel_.y + 55.f, tw, 26.f};
            r.fillRoundedRect(b, on ? c.accent.withAlpha(60) : c.headerBg, 12.f);
            if (on) r.strokeRect(b, c.accent, 1.f);
            r.drawText({b.x + 11.f, b.y + (b.h - r.lineHeight(kSmall)) * 0.5f}, label, kSmall, on ? c.text : c.textMuted);
            chips_.push_back({b, id});
            x += tw + 6.f;
        };
        if (!spec_.expected.empty()) chip("Rend un " + spec_.expected, 0, typed_);
        chip("Dans une expression", 1, exprOnly_);
        // ---- les familles ----
        r.fillRect(fam_, c.headerBg);
        r.strokeRect(fam_, c.border, 1.f);
        famRects_.clear();
        r.pushClip(fam_);
        float fy = fam_.y + 4.f - famScroll_;
        const auto famRow = [&](const std::string& label, const std::string& id, std::size_t count) {
            const gfx::Rect rr{fam_.x + 1.f, fy, fam_.w - 2.f, kRow};
            if (family_ == id) r.fillRect(rr, c.selectionBg);
            const gfx::Color t = family_ == id ? c.selectionText : c.text;
            const std::string n = std::to_string(count);
            const float nw = r.measure(n, kSmall).width;
            r.drawText({rr.x + 14.f, rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, fit(r, label, kSmall, rr.w - 34.f - nw), kSmall, t);
            r.drawText({rr.right() - 10.f - nw, rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, n, kSmall,
                       family_ == id ? c.selectionText : c.textMuted);
            famRects_.push_back({rr, id});
            fy += kRow;
        };
        famRow("Toutes", {}, countIn({}));
        std::string group;
        for (const auto& f : families_) {
            if (f.group != group) {
                group = f.group;
                fy += 4.f;
                r.drawText({fam_.x + 10.f, fy + 3.f}, fit(r, group, kSmall, fam_.w - 20.f), kSmall, c.textMuted);
                fy += kRow - 4.f;
            }
            famRow(f.name, f.name, countIn(f.name));
        }
        famContent_ = fy + famScroll_ - fam_.y + 4.f;
        r.popClip();
        famBar_.paint(ctx, fam_, famContent_, fam_.h, famScroll_);
        // ---- les fonctions ----
        r.fillRect(list_, c.windowBg);
        r.strokeRect(list_, c.border, 1.f);
        rowRects_.clear();
        r.pushClip(list_);
        const std::size_t first = static_cast<std::size_t>(std::max(0.f, scroll_ / kItem));
        for (std::size_t k = first; k < shown_.size(); ++k) {
            const float y = list_.y + static_cast<float>(k) * kItem - scroll_;
            if (y > list_.bottom()) break;
            const auto* e = shown_[k];
            const gfx::Rect rr{list_.x + 1.f, y, list_.w - 2.f, kItem};
            rowRects_.push_back({rr, static_cast<int>(k)});
            const bool on = static_cast<int>(k) == sel_;
            if (on) r.fillRect(rr, c.selectionBg);
            else if (static_cast<int>(k) == hover_) r.fillRect(rr, c.rowAltBg);
            const gfx::Color t = on ? c.selectionText : c.text;
            const gfx::Color m = on ? c.selectionText : c.textMuted;
            r.drawText({rr.x + 10.f, rr.y + 4.f}, fit(r, HmiFunctionPicker::callOf(*e).text, kMono, rr.w - 20.f), kMono, t);
            r.drawText({rr.x + 10.f, rr.y + 21.f}, fit(r, e->returns + "  \xC2\xB7  " + e->family, kSmall, rr.w - 20.f), kSmall, m);
            r.fillRect({rr.x, rr.bottom() - 1.f, rr.w, 1.f}, c.border.withAlpha(90));
        }
        if (shown_.empty())
            r.drawText({list_.x + 12.f, list_.y + 12.f},
                       fit(r, typed_ ? "Aucune ne convient : d\xC3\xA9" "coche \xC2\xAB Rend un " + spec_.expected + " \xC2\xBB."
                                     : std::string("Aucune fonction ne correspond \xC3\xA0 la recherche."),
                           kSmall, list_.w - 24.f),
                       kSmall, c.textMuted);
        r.popClip();
        bar_.paint(ctx, list_, static_cast<float>(shown_.size()) * kItem, list_.h, scroll_);
        // ---- la fiche ----
        paintDetail(ctx);
        // ---- le pied ----
        r.fillRect({panel_.x, bottom_, panel_.w, 1.f}, c.border);
        const std::string help = "Haut, Bas : parcourir \xC2\xB7 Entr\xC3\xA9" "e ou double-clic : ins\xC3\xA9rer \xC2\xB7 \xC3\x89" "chap : fermer";
        r.drawText({panel_.x + 16.f, bottom_ + 18.f}, fit(r, help, kSmall, example_->bounds().x - panel_.x - 32.f), kSmall, c.textMuted);
        ok_->setEnabled(selected() != nullptr);
        example_->setEnabled(selected() != nullptr && !selected()->example.empty());
    }

    void paintDetail(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(detail_, c.windowBg);
        r.strokeRect(detail_, c.border, 1.f);
        const auto* e = selected();
        if (!e) return;
        r.pushClip(detail_);
        const float x = detail_.x + 12.f, w = detail_.w - 24.f;
        float y = detail_.y + 10.f;
        for (const auto& line : wrap(r, e->signature, kMono, w)) {
            r.drawText({x, y}, line, kMono, c.accent);
            y += r.lineHeight(kMono) + 2.f;
        }
        y += 6.f;
        for (const auto& line : wrap(r, e->summary, kBody, w)) {
            r.drawText({x, y}, line, kBody, c.text);
            y += r.lineHeight(kBody) + 2.f;
        }
        if (!e->params.empty()) {
            y += 8.f;
            r.drawText({x, y}, "Ses arguments", kSmall, c.textMuted);
            y += r.lineHeight(kSmall) + 4.f;
            for (const auto& p : e->params) {
                const std::string head = p.name + (p.optional ? " (facultatif)" : std::string{});
                r.drawText({x, y}, fit(r, head, kMono, w * 0.34f), kMono, c.text);
                const float tx = x + w * 0.36f;
                const auto lines = wrap(r, p.type + " \xE2\x80\x94 " + p.role, kSmall, w - (tx - x));
                for (const auto& l : lines) {
                    r.drawText({tx, y + 1.f}, l, kSmall, c.textMuted);
                    y += r.lineHeight(kSmall) + 2.f;
                }
                if (lines.empty()) y += r.lineHeight(kSmall) + 2.f;
            }
        }
        y += 8.f;
        r.drawText({x, y}, "Rend : " + e->returns + "   \xC2\xB7   " + e->family + (e->expression ? std::string{} : "   \xC2\xB7   instruction seule"),
                   kSmall, c.textMuted);
        y += r.lineHeight(kSmall) + 10.f;
        if (!e->example.empty()) {
            r.drawText({x, y}, "Exemple", kSmall, c.textMuted);
            y += r.lineHeight(kSmall) + 4.f;
            std::vector<std::string> lines;
            std::string cur;
            for (const char ch : e->example) {
                if (ch == '\n') {
                    lines.push_back(cur);
                    cur.clear();
                } else {
                    cur += ch;
                }
            }
            lines.push_back(cur);
            const float h = static_cast<float>(lines.size()) * (r.lineHeight(kMono) + 2.f) + 12.f;
            r.fillRect({x, y, w, h}, c.headerBg);
            float ly = y + 6.f;
            for (const auto& l : lines) {
                r.drawText({x + 8.f, ly}, fit(r, l, kMono, w - 16.f), kMono, c.text);
                ly += r.lineHeight(kMono) + 2.f;
            }
        }
        r.popClip();
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
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
            if (d->button != ui::MouseButton::Left) return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
            if (close_.contains(d->pos)) {
                owner_.finish(false);
                return ui::EventResult::Consumed;
            }
            for (const auto& [rect, id] : chips_)
                if (rect.contains(d->pos)) {
                    if (id == 0) setTyped(!typed_);
                    else setExpressionOnly(!exprOnly_);
                    return ui::EventResult::Consumed;
                }
            for (const auto& [rect, id] : famRects_)
                if (rect.contains(d->pos) && fam_.contains(d->pos)) {
                    (void)setFamily(id);
                    return ui::EventResult::Consumed;
                }
            for (const auto& [rect, k] : rowRects_) {
                if (!rect.contains(d->pos) || !list_.contains(d->pos)) continue;
                sel_ = k;
                invalidate();
                if (d->clickCount >= 2) owner_.insert(false);
                return ui::EventResult::Consumed;
            }
            return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Family {
        std::string name, group;
    };
    [[nodiscard]] bool keeps(const Entry& e) const {
        if (exprOnly_ && !e.expression) return false;
        if (typed_) {
            const bool ok = spec_.fits ? spec_.fits(e.returns) : upperOf(e.returns) == upperOf(spec_.expected);
            if (!ok) return false;
        }
        return true;
    }
    [[nodiscard]] std::size_t countIn(const std::string& family) const {
        std::size_t n = 0;
        for (const auto& e : all_)
            if ((family.empty() || e.family == family) && keeps(e) && query_.matches({e.name, e.summary})) ++n;
        return n;
    }
    void filter() {
        query_ = ui::SearchQuery(search_->text());
        families_.clear();
        for (const auto& e : all_)
            if (std::none_of(families_.begin(), families_.end(), [&](const Family& f) { return f.name == e.family; }))
                families_.push_back({e.family, e.group});
        shown_.clear();
        for (const auto& e : all_)
            if ((family_.empty() || e.family == family_) && keeps(e) && query_.matches({e.name, e.summary})) shown_.push_back(&e);
        // Une recherche : le nom qui commence par le texte d'abord.
        if (!query_.empty() && !query_.terms().empty()) {
            const std::string head = query_.terms().front();
            std::stable_sort(shown_.begin(), shown_.end(), [&](const Entry* a, const Entry* b) {
                const bool pa = ui::foldForSearch(a->name).rfind(head, 0) == 0, pb = ui::foldForSearch(b->name).rfind(head, 0) == 0;
                return pa && !pb;
            });
        }
        if (sel_ >= static_cast<int>(shown_.size())) sel_ = static_cast<int>(shown_.size()) - 1;
        if (sel_ < 0 && !shown_.empty()) sel_ = 0;
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
        invalidate();
    }
    [[nodiscard]] float maxScroll() const { return std::max(0.f, static_cast<float>(shown_.size()) * kItem - list_.h); }
    [[nodiscard]] float maxFamScroll() const { return std::max(0.f, famContent_ - fam_.h); }
    void reveal() {
        if (list_.h <= 0.f) {
            revealPending_ = true;
            return;
        }
        const float top = static_cast<float>(sel_) * kItem;
        if (top < scroll_) scroll_ = top;
        else if (top + kItem > scroll_ + list_.h) scroll_ = top + kItem - list_.h;
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }

    HmiFunctionPicker&                     owner_;
    const Spec&                            spec_;
    std::vector<Entry>                     all_;
    std::vector<const Entry*>              shown_;
    std::vector<Family>                    families_;
    ui::SearchQuery                        query_;
    std::string                            family_;
    bool                                   typed_{false}, exprOnly_{false};
    int                                    sel_{0}, hover_{-1};
    bool                                   hoverClose_{false}, revealPending_{false};
    float                                  scroll_{0.f}, famScroll_{0.f}, famContent_{0.f}, bottom_{0.f};
    ui::InputText*                         search_{nullptr};
    ui::Button*                            example_{nullptr};
    ui::Button*                            cancel_{nullptr};
    ui::Button*                            ok_{nullptr};
    gfx::Rect                              panel_{}, fam_{}, list_{}, detail_{}, close_{};
    ui::PaintedScrollBar                   bar_, famBar_;
    std::vector<std::pair<gfx::Rect, int>> chips_;
    std::vector<std::pair<gfx::Rect, std::string>> famRects_;
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
            insert(false);
            return ui::EventResult::Consumed;
        }
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
    for (const auto& cat : n::categories())
        for (const auto& f : n::functions()) {
            if (f.category != cat.id) continue;
            Entry e;
            e.name = std::string(f.name);
            e.family = std::string(cat.title);
            e.group = std::string(cat.group);
            e.signature = n::signature(f);
            e.returns = std::string(f.returns);
            e.summary = std::string(f.summary);
            e.example = std::string(f.st);
            e.expression = f.expression;
            for (const auto& p : f.params) e.params.push_back({std::string(p.name), std::string(p.type), std::string(p.role), p.optional});
            out.push_back(std::move(e));
        }
    return out;
}

HmiFunctionPicker::Answer HmiFunctionPicker::callOf(const Entry& e) {
    Answer a;
    a.name = e.name;
    a.text = e.name + "(";
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

void HmiFunctionPicker::setSearch(const std::string& text) { if (body_) body_->setSearch(text); }
bool HmiFunctionPicker::setFamily(std::string_view family) { return body_ && body_->setFamily(family); }
void HmiFunctionPicker::setTyped(bool on) { if (body_) body_->setTyped(on); }
void HmiFunctionPicker::setExpressionOnly(bool on) { if (body_) body_->setExpressionOnly(on); }
bool HmiFunctionPicker::select(std::string_view name) { return body_ && body_->select(name); }
std::vector<std::string> HmiFunctionPicker::shownNames() const { return body_ ? body_->shownNames() : std::vector<std::string>{}; }
std::vector<std::string> HmiFunctionPicker::families() const { return body_ ? body_->familyNames() : std::vector<std::string>{}; }
std::string HmiFunctionPicker::selectedName() const {
    const auto* e = body_ ? body_->selected() : nullptr;
    return e ? e->name : std::string{};
}

void HmiFunctionPicker::insert(bool example) {
    if (!body_ || done_) return;
    const auto* e = body_->selected();
    if (!e) return;
    if (example && !e->example.empty()) {
        answer_ = Answer{};
        answer_.name = e->name;
        answer_.text = e->example;
    } else {
        answer_ = callOf(*e);
    }
    finish(true);
}

void HmiFunctionPicker::finish(bool ok) {
    if (done_) return;
    done_ = true;
    std::string holes;
    for (const auto& [at, len] : answer_.holes) holes += (holes.empty() ? "" : ",") + std::to_string(at) + ":" + std::to_string(len);
    const std::string payload = answer_.name + kSep + answer_.text + kSep + holes;
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
