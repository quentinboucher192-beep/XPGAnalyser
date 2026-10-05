// =============================================================================
//  app/VersionsPane.cpp - le volet Versions (lot 21)
// =============================================================================
#include "VersionsPane.hpp"

#include "hmi/HmiIcons.hpp"
#include "hmi/HmiPaneKit.hpp"
#include "hmi/HmiPanels.hpp"

#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace ver = hmi::ver;
using PG = ui::PropertyGrid;

namespace {

const gfx::FontId kSmall{13};

// "2026-09-26 18:40" -> "26/09 18:40" ; "26/09" seul si `withTime` est faux.
std::string shortDate(const std::string& stamp, bool withTime = true) {
    if (stamp.size() < 16) return stamp;
    std::string out = stamp.substr(8, 2) + "/" + stamp.substr(5, 2);
    if (withTime) out += " " + stamp.substr(11, 5);
    return out;
}

ui::Tone toneOf(ver::State s) {
    switch (s) {
        case ver::State::Delivered: return ui::Tone::Ok;
        case ver::State::Validated: return ui::Tone::Info;
        case ver::State::BeforeRestore: return ui::Tone::Warning;
        case ver::State::Auto:
        case ver::State::Draft: return ui::Tone::Muted;
    }
    return ui::Tone::Muted;
}

gfx::Color colorOf(const ui::Theme& t, ver::State s) {
    switch (s) {
        case ver::State::Delivered: return t.color.ok;
        case ver::State::Validated: return t.color.accent;
        case ver::State::BeforeRestore: return t.color.warning;
        case ver::State::Auto:
        case ver::State::Draft: return t.color.textMuted;
    }
    return t.color.textMuted;
}

std::string mark(ver::Change c) {
    return c == ver::Change::Added ? "+ " : c == ver::Change::Removed ? "\xE2\x88\x92 " : "\xE2\x9C\x8E ";
}

}   // namespace

// ---- la frise ----------------------------------------------------------------------
//
// LOT API 6 : ELLE DEFILE. Les points etaient repartis sur la largeur quel que
// soit leur nombre : a trente versions, trente etiquettes de 150 pixels dans
// 1 100 se montaient dessus. Chaque version a maintenant sa place (124 pixels,
// 96 pour une automatique) ; tant que tout tient, la place en trop est
// repartie comme avant ; au-dela la frise defile - la molette, un glisser, les
// fleches aux bouts, la barre sous les dates - et montre la plus recente au
// depart. La version choisie dans le tableau vient se montrer.
class VersionTimeline final : public ui::Widget {
public:
    static constexpr float kSlot = 124.f, kAutoSlot = 96.f, kArrow = 22.f;

    explicit VersionTimeline(VersionsPane& pane) : ui::Widget(pane.id() + ".frise"), pane_(pane) {
        links_ += hoverChanged->connect([this](bool in) {
            if (!in) drag_ = Drag::None;
        });
    }

    // La bande des versions : de `left` a `right`, `content` de large.
    struct Strip {
        float left{0.f}, right{0.f}, content{0.f};
        [[nodiscard]] float width() const noexcept { return std::max(0.f, right - left); }
        [[nodiscard]] bool  scrolls() const noexcept { return content > width() + 0.5f; }
        [[nodiscard]] float maxScroll() const noexcept { return std::max(0.f, content - width()); }
    };
    [[nodiscard]] Strip strip() const {
        const auto b = bounds();
        Strip s;
        s.left = b.x + kArrow + 6.f;
        s.right = workX() - 100.f;
        for (const auto& v : pane_.store_.versions) s.content += slotOf(v);
        return s;
    }

    // Le centre du point de la version `number` (0 : le travail en cours), le
    // defilement compris : hors de la bande, le point n'est pas dessine.
    [[nodiscard]] gfx::Point centreOf(int number) const {
        settle();
        const float y = bounds().y + 40.f;
        if (number == 0) return {workX(), y};
        const float raw = rawCentre(number);
        if (raw < -9000.f) return {-1000.f, -1000.f};
        return {raw - scroll_, y};
    }
    [[nodiscard]] bool visibleDot(int number) const {
        if (number == 0) return true;
        const auto s = strip();
        const float x = centreOf(number).x;
        return x >= s.left - 1.f && x <= s.right + 1.f;
    }

    // La version choisie vient se montrer (a la prochaine mise en page si la
    // frise n'a pas encore de taille).
    void reveal(int number) {
        pending_ = number;
        settle();
        invalidate();
    }
    [[nodiscard]] float scroll() const {
        settle();
        return scroll_;
    }
    void scrollTo(float x) {
        settle();
        const auto s = strip();
        scroll_ = std::clamp(x, 0.f, s.maxScroll());
        atEnd_ = scroll_ >= s.maxScroll() - 0.5f;
        invalidate();
    }

protected:
    void onLayout() override {
        // Une nouvelle largeur : on garde la meme version au bord droit.
        pending_ = pending_ >= 0 ? pending_ : (atEnd_ ? kEnd : pending_);
        settle();
    }

    void onPaint(const ui::PaintContext& ctx) override {
        settle();
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(b, c.panelBg);
        const auto& vs = pane_.store_.versions;
        const int chosen = pane_.selectedVersion();
        const Strip s = strip();
        const gfx::Point work = centreOf(0);
        const gfx::Point first = vs.empty() ? work : centreOf(vs.front().number);
        ctx.r.line({std::max(first.x, s.left), first.y}, {work.x, work.y}, c.border, 2.f);
        const auto dot = [&](gfx::Point p, float r, gfx::Color fill) {
            ctx.r.fillRoundedRect({p.x - r, p.y - r, 2.f * r, 2.f * r}, fill, r);
        };
        // Les versions, dans leur bande : ce qui deborde est coupe net.
        ctx.r.pushClip({s.left - 2.f, b.y, s.width() + 4.f, b.h});
        for (const auto& v : vs) {
            const gfx::Point p = centreOf(v.number);
            const float slot = slotOf(v);
            if (p.x + slot * 0.5f < s.left || p.x - slot * 0.5f > s.right) continue;
            const bool small = v.state == ver::State::Auto;
            const float r = small ? 3.5f : 6.f;
            if (v.number == chosen) dot(p, r + 5.f, c.selectionBg);
            dot(p, r, colorOf(ctx.theme, v.state));
            if (v.state == ver::State::Delivered) {
                dot(p, r + 3.f, gfx::Color{c.ok.r, c.ok.g, c.ok.b, 60});
                dot(p, r, c.ok);
            }
            // Au-dessus : "V5 Livree au client" ; au-dessous : la date. Coupes a
            // la place de la version, mesures dans la police ou ils sont ecrits.
            const gfx::FontId font = small ? kSmall : ctx.theme.font.ui;
            std::string top = "V" + std::to_string(v.number);
            if (!small && !v.name.empty()) top += " " + v.name;
            if (small) top += " (auto)";
            const float room = slot - 12.f;
            if (ctx.r.measure(top, font).width > room) {
                const std::size_t keep = ctx.r.fitCharacters(top, font, room - ctx.r.measure("\xE2\x80\xA6", font).width);
                std::size_t cut = std::min(keep, top.size());
                while (cut > 0 && (static_cast<unsigned char>(top[cut]) & 0xC0u) == 0x80u) --cut;
                top = top.substr(0, cut) + "\xE2\x80\xA6";
            }
            // Au bord de la bande, une etiquette a moitie coupee se lirait mal
            // (le "36" de la fleche colle a "07:37") : elle n'est ecrite qu'entiere.
            const float tw = ctx.r.measure(top, font).width;
            if (p.x - tw * 0.5f >= s.left - 1.f && p.x + tw * 0.5f <= s.right + 1.f)
                ctx.r.drawText({p.x - tw * 0.5f, b.y + 6.f}, top, font, small ? c.textMuted : c.text);
            const std::string when = shortDate(v.date, small);
            const float ww = ctx.r.measure(when, kSmall).width;
            if (p.x - ww * 0.5f >= s.left - 1.f && p.x + ww * 0.5f <= s.right + 1.f)
                ctx.r.drawText({p.x - ww * 0.5f, p.y + 14.f}, when, kSmall, c.textMuted);
        }
        ctx.r.popClip();
        if (s.scrolls()) paintScrolling(ctx, s);
        // Le travail en cours : un anneau orange s'il differe de la derniere version.
        const bool modified = !pane_.work_.elements.empty() || (pane_.hosts_.unsaved && pane_.hosts_.unsaved() > 0);
        if (chosen == 0) dot(work, 12.f, c.selectionBg);
        dot(work, 8.f, modified ? c.warning : c.ok);
        dot(work, 5.f, c.panelBg);
        const std::string label = pane_.hosts_.workingTitle ? pane_.hosts_.workingTitle(pane_.store_) : std::string("travail en cours");
        const float lw = ctx.r.measure(label, ctx.theme.font.ui).width;
        ctx.r.drawText({std::min(work.x - lw * 0.5f, b.right() - lw - 6.f), b.y + 6.f}, label, ctx.theme.font.ui, modified ? c.warning : c.ok);
        std::string sub;
        const std::size_t n = pane_.work_.elements.size();
        const std::size_t unsaved = pane_.hosts_.unsaved ? pane_.hosts_.unsaved() : 0;
        if (vs.empty()) sub = "pas encore de version";
        else if (n == 0 && unsaved == 0) sub = "\xC3\xA0 jour";
        else sub = n > 0 ? std::to_string(n) + (n == 1 ? " changement" : " changements") : std::string("non enregistr\xC3\xA9");
        const float sw = ctx.r.measure(sub, kSmall).width;
        ctx.r.drawText({work.x - sw * 0.5f, work.y + 14.f}, sub, kSmall, c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const Strip s = strip();
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
            if (!s.scrolls()) return ui::EventResult::Ignored;
            // Molette vers le haut : les plus anciennes ; la molette horizontale suit la main.
            scrollBy((w->dx - w->dy) * 90.f);
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && bounds().contains(d->pos)) {
            if (s.scrolls()) {
                if (leftArrow().contains(d->pos)) { scrollBy(-s.width() * 0.8f); return ui::EventResult::Consumed; }
                if (rightArrow(s).contains(d->pos)) { scrollBy(s.width() * 0.8f); return ui::EventResult::Consumed; }
                if (track(s).contains(d->pos) || d->pos.y >= track(s).y - 3.f) {
                    // La barre : le pouce vient sous la souris, puis suit le glisser.
                    const auto th = thumb(s);
                    if (!th.contains(d->pos)) scrollTo((d->pos.x - s.left - th.w * 0.5f) / std::max(1.f, s.width() - th.w) * s.maxScroll());
                    drag_ = Drag::Thumb;
                    dragX_ = d->pos.x;
                    dragScroll_ = scroll_;
                    return ui::EventResult::Consumed;
                }
            }
            int best = -1;
            float bestD = 24.f;
            const auto consider = [&](int number) {
                if (!visibleDot(number)) return;
                const gfx::Point p = centreOf(number);
                const float dx = p.x - d->pos.x, dy = p.y - d->pos.y;
                const float dist = std::abs(dx) + std::abs(dy) * 0.5f;
                if (dist < bestD) { bestD = dist; best = number; }
            };
            for (const auto& v : pane_.store_.versions) consider(v.number);
            consider(0);
            if (best >= 0) {
                pane_.selectVersion(best);
            } else if (s.scrolls() && d->pos.x >= s.left && d->pos.x <= s.right) {
                // A cote des points : on attrape la frise et on la tire.
                drag_ = Drag::Strip;
                dragX_ = d->pos.x;
                dragScroll_ = scroll_;
            }
            return ui::EventResult::Consumed;
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && drag_ != Drag::None) {
            const float dx = m->pos.x - dragX_;
            if (drag_ == Drag::Strip) scrollTo(dragScroll_ - dx);
            else {
                const auto th = thumb(s);
                scrollTo(dragScroll_ + dx / std::max(1.f, s.width() - th.w) * s.maxScroll());
            }
            atEnd_ = scroll_ >= s.maxScroll() - 0.5f;
            return ui::EventResult::Consumed;
        }
        if (std::holds_alternative<ui::MouseUp>(ev) && drag_ != Drag::None) {
            drag_ = Drag::None;
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    enum class Drag { None, Strip, Thumb };
    static constexpr int kEnd = 1 << 30;   // "montrer la plus recente"

    [[nodiscard]] static float slotOf(const ver::Version& v) { return v.state == ver::State::Auto ? kAutoSlot : kSlot; }
    [[nodiscard]] float workX() const { return bounds().x + bounds().w - 70.f; }

    // La position sans defilement. Tant que tout tient, la place en trop est
    // repartie entre les points (une seule version reste a gauche, comme avant).
    [[nodiscard]] float rawCentre(int number) const {
        const auto& vs = pane_.store_.versions;
        const Strip s = strip();
        const float extra = s.scrolls() || vs.size() < 2 ? 0.f : s.width() - s.content;
        float x = s.left;
        for (std::size_t i = 0; i < vs.size(); ++i) {
            const float w = slotOf(vs[i]);
            const float spread = vs.size() < 2 ? 0.f : extra * static_cast<float>(i) / static_cast<float>(vs.size() - 1);
            if (vs[i].number == number) return x + w * 0.5f + spread;
            x += w;
        }
        return -10000.f;
    }

    // Applique une demande de `reveal` quand la frise a une taille.
    void settle() const {
        const Strip s = strip();
        if (bounds().w <= 0.f) return;
        if (!s.scrolls()) {
            scroll_ = 0.f;
            atEnd_ = true;
            pending_ = -1;
            return;
        }
        if (pending_ == kEnd || (pending_ == 0)) {
            scroll_ = s.maxScroll();
        } else if (pending_ > 0) {
            const float raw = rawCentre(pending_);
            if (raw > -9000.f) {
                const float half = kSlot * 0.5f;
                if (raw - scroll_ < s.left + half) scroll_ = raw - s.left - half;
                if (raw - scroll_ > s.right - half) scroll_ = raw - s.right + half;
            }
        }
        pending_ = -1;
        scroll_ = std::clamp(scroll_, 0.f, s.maxScroll());
        atEnd_ = scroll_ >= s.maxScroll() - 0.5f;
    }

    void scrollBy(float dx) {
        const Strip s = strip();
        scroll_ = std::clamp(scroll_ + dx, 0.f, s.maxScroll());
        atEnd_ = scroll_ >= s.maxScroll() - 0.5f;
        invalidate();
    }

    [[nodiscard]] gfx::Rect leftArrow() const { return {bounds().x + 3.f, bounds().y + 28.f, kArrow, 24.f}; }
    [[nodiscard]] gfx::Rect rightArrow(const Strip& s) const { return {s.right + 5.f, bounds().y + 28.f, kArrow, 24.f}; }
    [[nodiscard]] gfx::Rect track(const Strip& s) const { return {s.left, bounds().y + bounds().h - 5.f, s.width(), 4.f}; }
    [[nodiscard]] gfx::Rect thumb(const Strip& s) const {
        const auto t = track(s);
        const float w = std::max(36.f, t.w * (s.width() / std::max(1.f, s.content)));
        const float x = t.x + (t.w - w) * (s.maxScroll() > 0.f ? scroll_ / s.maxScroll() : 1.f);
        return {x, t.y - 2.f, w, t.h + 4.f};
    }

    // Les fleches aux bouts (le nombre de versions cachees dessous) et la barre.
    void paintScrolling(const ui::PaintContext& ctx, const Strip& s) const {
        const auto& c = ctx.theme.color;
        int before = 0, after = 0;
        for (const auto& v : pane_.store_.versions) {
            const float x = centreOf(v.number).x;
            if (x < s.left) ++before;
            else if (x > s.right) ++after;
        }
        const auto arrow = [&](gfx::Rect r, bool left, int hidden) {
            const bool on = hidden > 0;
            ctx.r.fillRoundedRect(r, on ? c.headerBg : c.panelBg, 4.f);
            ctx.r.strokeRect(r, on ? c.borderStrong : c.border, 1.f);
            const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
            const gfx::Color ink = on ? c.text : c.textDisabled;
            const float d = left ? 3.f : -3.f;
            ctx.r.line({cx + d, cy - 5.f}, {cx - d, cy}, ink, 2.f);
            ctx.r.line({cx - d, cy}, {cx + d, cy + 5.f}, ink, 2.f);
            if (on) {
                const std::string n = std::to_string(hidden);
                const float w = ctx.r.measure(n, kSmall).width;
                ctx.r.drawText({cx - w * 0.5f, r.bottom() + 1.f}, n, kSmall, c.textMuted);
            }
        };
        arrow(leftArrow(), true, before);
        arrow(rightArrow(s), false, after);
        const auto t = track(s);
        ctx.r.fillRoundedRect(t, c.border, 2.f);
        const auto th = thumb(s);
        ctx.r.fillRoundedRect(th, drag_ == Drag::Thumb ? c.accent : c.borderStrong, 3.f);
    }

    VersionsPane&         pane_;
    mutable float         scroll_{0.f};
    mutable int           pending_{kEnd};
    mutable bool          atEnd_{true};
    Drag                  drag_{Drag::None};
    float                 dragX_{0.f}, dragScroll_{0.f};
    core::ConnectionScope links_;
};

// ---- le volet ----------------------------------------------------------------------
VersionsPane::VersionsPane(std::string id, std::string folder) : ui::Widget(std::move(id)), folder_(std::move(folder)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(ACreate, HmiGlyph::Plus,
               "Cr\xC3\xA9" "er une version : une photo du projet (l'API, l'IHM et ses ressources), un nom, un \xC3\xA9tat (Ctrl+Alt+S)",
               "Cr\xC3\xA9" "er une version");
    tools->separator();
    tools->add(ACompare, HmiGlyph::Compare, "Comparer la version choisie avec la pr\xC3\xA9" "c\xC3\xA9" "dente (ou le travail en cours avec la derni\xC3\xA8re)",
               "Comparer");
    tools->add(ARestore, HmiGlyph::Refresh,
               "Restaurer la version choisie : le projet revient \xC3\xA0 cet \xC3\xA9tat ; une version \xC2\xAB Avant restauration \xC2\xBB est cr\xC3\xA9\xC3\xA9" "e d'abord",
               "Restaurer");
    tools->add(AExport, HmiGlyph::Export, "Exporter la version choisie en .zip (dans exports/)", "Exporter (.zip)");
    tools->add(AExtract, HmiGlyph::Import, "Extraire la version choisie dans un dossier : un projet complet, \xC3\xA0 ouvrir \xC3\xA0 c\xC3\xB4t\xC3\xA9",
               "Extraire dans un dossier");
    tools->add(ADelete, HmiGlyph::Delete, "Supprimer la version choisie (son contenu partag\xC3\xA9 reste pour les autres)", "Supprimer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto isVersion = [this] { return selectedVersion() > 0; };
    for (int a : {ARestore, AExport, AExtract, ADelete}) tools_->setEnabledWhen(a, isVersion);
    tools_->setEnabledWhen(ACompare, [this] { return !store_.versions.empty() && selectedVersion() >= 0; });

    auto box = std::make_unique<ui::DropDown>(base + ".auto");
    box->setItems({{"jamais", "jamais", {}, true},
                   {"\xC3\xA0 chaque enregistrement", "enregistrement", {}, true},
                   {"au plus une par heure", "heure", {}, true}});
    box->setTooltip("La version automatique : cr\xC3\xA9\xC3\xA9" "e \xC3\xA0 l'enregistrement quand le projet a chang\xC3\xA9 "
                    "(les dix plus r\xC3\xA9" "centes sont gard\xC3\xA9" "es ; jamais une version nomm\xC3\xA9" "e)");
    auto_ = &static_cast<ui::DropDown&>(addChild(std::move(box)));

    auto tl = std::make_unique<VersionTimeline>(*this);
    timeline_ = &addChild(std::move(tl));

    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"N\xC2\xB0", 50.f}, {"Nom", 200.f}, {"\xC3\x89tat", 150.f}, {"Date", 110.f}, {"Auteur", 190.f},
                       {"Depuis la pr\xC3\xA9" "c\xC3\xA9" "dente", 300.f}, {"Taille", 170.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    auto changes = std::make_unique<ui::TableView>(base + ".changes");
    changes->setColumns({{"\xC3\x89l\xC3\xA9ment", 520.f}, {"Ce qui change", 420.f}});
    changes->setSelectionMode(ui::SelectionMode::Single);
    changes_ = &static_cast<ui::TableView&>(addChild(std::move(changes)));
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::make_unique<ui::PropertyGrid>(base + ".grid")));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));

    links_ += tools_->triggered->connect([this](int a) {
        const int sel = selectedVersion();
        switch (a) {
            case ACreate: if (hosts_.create) hosts_.create(); break;
            case ACompare:
                if (hosts_.compare && sel >= 0) {
                    const auto* v = sel > 0 ? store_.find(sel) : nullptr;
                    if (sel == 0 && store_.last()) hosts_.compare(store_.last()->number, 0, {});
                    else if (v) hosts_.compare(v->base, v->number, {});
                }
                break;
            case ARestore: if (hosts_.restore && sel > 0) hosts_.restore(sel); break;
            case AExport: if (hosts_.exportZip && sel > 0) hosts_.exportZip(sel); break;
            case AExtract: if (hosts_.extract && sel > 0) hosts_.extract(sel); break;
            case ADelete: if (hosts_.remove && sel > 0) hosts_.remove(sel); break;
            default: break;
        }
    });
    links_ += auto_->selectionChanged->connect([this](int i) {
        if (filling_) return;
        const auto* it = auto_->selectedItem();
        (void)i;
        if (it) (void)setAutoMode(ver::autoFromKey(it->value));
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) {
        if (filling_) return;
        if (const int v = selectedVersion(); v >= 0) static_cast<VersionTimeline*>(timeline_)->reveal(v);
        rebuildChanges();
        rebuildProperties();
        invalidate();
    });
    links_ += changes_->activated->connect([this](ui::RowIndex r) {
        if (!hosts_.compare || r >= shown_.elements.size()) return;
        hosts_.compare(shown_.a, shown_.b, shown_.elements[r].key);
    });
    refresh();
}

void VersionsPane::setFolder(std::string folder) {
    if (folder == folder_) return;
    folder_ = std::move(folder);
    refresh();
}

void VersionsPane::say(std::string text, bool error) {
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

int VersionsPane::selectedVersion() const {
    const auto rows = table_->selectedModelRows();
    return rows.empty() || rows.front() >= rows_.size() ? -1 : rows_[rows.front()];
}

void VersionsPane::selectVersion(int number) {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i] == number) table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    static_cast<VersionTimeline*>(timeline_)->reveal(number);
    rebuildChanges();
    rebuildProperties();
    invalidate();
}

void VersionsPane::refresh() {
    const int keep = selectedVersion();
    auto s = ver::open(folder_);
    store_ = s ? std::move(*s) : ver::Store{};
    store_.folder = folder_;
    if (!s) say("versions/index.txt illisible : " + s.error().context, true);
    work_ = {};
    if (const auto* last = store_.last())
        if (auto c = ver::compare(store_, last->number, 0)) work_ = std::move(*c);
    filling_ = true;
    auto_->setSelectedIndex(static_cast<int>(store_.autoMode));
    filling_ = false;
    rebuildTable();
    const bool still = keep >= 0 && std::find(rows_.begin(), rows_.end(), keep) != rows_.end();
    selectVersion(still ? keep : store_.last() ? store_.last()->number : 0);
    updateStatus();
}

void VersionsPane::rebuildTable() {
    std::vector<std::vector<std::string>> rows;
    std::vector<ui::CellStyle> firstCol;
    rows_.clear();
    const std::size_t unsaved = hosts_.unsaved ? hosts_.unsaved() : 0;
    const bool modified = !work_.elements.empty() || unsaved > 0;
    // Le travail en cours d'abord.
    rows_.push_back(0);
    std::string since;
    if (store_.versions.empty()) since = "pas encore de version";
    else if (work_.elements.empty()) since = "rien depuis V" + std::to_string(store_.last()->number);
    else since = work_.summary();
    const std::string working = hosts_.workingTitle ? hosts_.workingTitle(store_) : std::string("Travail en cours");
    rows.push_back({"\xE2\x97\x8F", working, "", "maintenant", "\xE2\x80\x94", since,
                    unsaved > 0 ? "non enregistr\xC3\xA9 (" + std::to_string(unsaved) + ")" : std::string("enregistr\xC3\xA9")});
    for (auto it = store_.versions.rbegin(); it != store_.versions.rend(); ++it) {
        const auto& v = *it;
        rows_.push_back(v.number);
        std::string size = ver::sizeText(v.bytes);
        if (v.newBytes < v.bytes) size += " (" + ver::sizeText(v.newBytes) + " neufs)";
        rows.push_back({"V" + std::to_string(v.number), v.state == ver::State::Auto && v.name.empty() ? std::string("(automatique)") : v.name,
                        "", shortDate(v.date), v.author, v.since, size});
    }
    const auto styleOf = [this, modified](ui::RowIndex r, std::size_t c) {
        ui::CellStyle cs;
        if (r >= rows_.size()) return cs;
        const int n = rows_[r];
        if (n == 0) {
            if (c == 0) cs.fgTone = modified ? ui::Tone::Warning : ui::Tone::Ok;
            if (c == 2) {
                cs.badge = modified ? "modifi\xC3\xA9" : "\xC3\xA0 jour";
                cs.badgeTone = modified ? ui::Tone::Warning : ui::Tone::Ok;
            }
            if (c == 1) cs.bold = true;
            return cs;
        }
        const auto* v = store_.find(n);
        if (!v) return cs;
        if (c == 0) cs.bold = true;
        if (c == 2) {
            // Lot API 6 : ce que la version a fait du projet - Terminer (FINISH), Livrer (LOCK).
            cs.badge = std::string(ver::stateLabel(v->state))
                     + (v->state == ver::State::Validated ? " \xC2\xB7 FINISH" : v->state == ver::State::Delivered ? " \xC2\xB7 LOCK" : "");
            cs.badgeTone = toneOf(v->state);
            cs.fgTone = ui::Tone::Muted;
        }
        if (c == 5) cs.fgTone = ui::Tone::Info;
        return cs;
    };
    filling_ = true;
    table_->setModel(std::make_shared<hmikit::Rows>(
        std::vector<std::string>{"N\xC2\xB0", "Nom", "\xC3\x89tat", "Date", "Auteur", "Depuis la pr\xC3\xA9" "c\xC3\xA9" "dente", "Taille"},
        std::move(rows), styleOf));
    filling_ = false;
}

void VersionsPane::rebuildChanges() {
    const int sel = selectedVersion();
    shown_ = {};
    changesTitle_.clear();
    if (sel == 0) {
        shown_ = work_;
        changesTitle_ = store_.last() ? "CE QUI A CHANG\xC3\x89 DEPUIS V" + std::to_string(store_.last()->number)
                                            + " (LE TRAVAIL EN COURS, ENREGISTR\xC3\x89)"
                                      : std::string("PAS ENCORE DE VERSION : \xC2\xAB Cr\xC3\xA9" "er une version \xC2\xBB garde l'\xC3\xA9tat du projet");
    } else if (const auto* v = store_.find(sel)) {
        if (v->base > 0 && store_.find(v->base)) {
            if (auto c = ver::compare(store_, v->base, v->number)) shown_ = std::move(*c);
            changesTitle_ = "CE QUI CHANGE DE V" + std::to_string(v->base) + " \xC3\x80 V" + std::to_string(v->number);
        } else {
            changesTitle_ = "V" + std::to_string(v->number) + " : LA PREMI\xC3\x88RE (" + std::to_string(v->fileCount) + " FICHIERS)";
        }
    }
    if (!shown_.elements.empty()) changesTitle_ += "  \xE2\x80\x94  double clic : voir les diff\xC3\xA9rences";
    std::vector<std::vector<std::string>> rows;
    for (const auto& e : shown_.elements) rows.push_back({mark(e.change) + e.where, e.detail});
    const auto elements = shown_.elements;
    const auto styleOf = [elements](ui::RowIndex r, std::size_t c) {
        ui::CellStyle cs;
        if (r >= elements.size() || c != 0) return cs;
        const auto ch = elements[r].change;
        cs.fgTone = ch == ver::Change::Added ? ui::Tone::Ok : ch == ver::Change::Removed ? ui::Tone::Error : ui::Tone::None;
        return cs;
    };
    changes_->setModel(std::make_shared<hmikit::Rows>(std::vector<std::string>{"\xC3\x89l\xC3\xA9ment", "Ce qui change"}, std::move(rows), styleOf));
}

void VersionsPane::rebuildProperties() {
    const int sel = selectedVersion();
    std::vector<PG::Category> cats;
    if (sel == 0) {
        PG::Category head;
        head.name = "Travail en cours";
        const std::size_t unsaved = hosts_.unsaved ? hosts_.unsaved() : 0;
        head.properties.push_back(hmikit::prop("\xC3\x89tat", unsaved > 0 ? std::to_string(unsaved) + " modification(s) non enregistr\xC3\xA9" "e(s)"
                                                                          : std::string("enregistr\xC3\xA9"),
                                               PG::ValueType::ReadOnly));
        head.properties.push_back(hmikit::prop("Depuis la derni\xC3\xA8re version",
                                               store_.last() ? std::to_string(work_.elements.size()) + " \xC3\xA9l\xC3\xA9ment(s) chang\xC3\xA9(s)"
                                                             : std::string("pas encore de version"),
                                               PG::ValueType::ReadOnly));
        head.properties.push_back(hmikit::prop("API", std::to_string(work_.count(ver::Side::Api)) + " \xC3\xA9l\xC3\xA9ment(s)", PG::ValueType::ReadOnly));
        head.properties.push_back(hmikit::prop("IHM", std::to_string(work_.count(ver::Side::Ihm)) + " \xC3\xA9l\xC3\xA9ment(s)", PG::ValueType::ReadOnly));
        cats.push_back(std::move(head));
        grid_->setCategories(std::move(cats));
        return;
    }
    const auto* v = store_.find(sel);
    if (!v) {
        grid_->clearProperties();
        return;
    }
    const int number = v->number;
    PG::Category head;
    head.name = "Version V" + std::to_string(number);
    head.properties.push_back(hmikit::prop("Nom", v->name, PG::ValueType::Text, [this, number](std::string_view s) {
        const auto* x = store_.find(number);
        return x && setInfo(number, std::string(s), x->state, x->comment);
    }));
    head.properties.push_back(hmikit::prop("\xC3\x89tat", std::string(ver::stateLabel(v->state)), PG::ValueType::Enum,
                                           [this, number](std::string_view s) {
                                               const auto* x = store_.find(number);
                                               if (!x) return false;
                                               ver::State st = ver::State::Draft;
                                               for (auto k : {ver::State::Draft, ver::State::Validated, ver::State::Delivered, ver::State::Auto,
                                                              ver::State::BeforeRestore})
                                                   if (ver::stateLabel(k) == s) st = k;
                                               return setInfo(number, x->name, st, x->comment);
                                           },
                                           "Brouillon, Valid\xC3\xA9" "e ou Livr\xC3\xA9" "e : la frise les montre en gris, en bleu, en vert.",
                                           {"Brouillon", "Valid\xC3\xA9" "e", "Livr\xC3\xA9" "e"}));
    head.properties.push_back(hmikit::prop("Commentaire", v->comment, PG::ValueType::Text, [this, number](std::string_view s) {
        const auto* x = store_.find(number);
        return x && setInfo(number, x->name, x->state, std::string(s));
    }));
    head.properties.push_back(hmikit::prop("Date", v->date, PG::ValueType::ReadOnly));
    head.properties.push_back(hmikit::prop("Auteur", v->author, PG::ValueType::ReadOnly));
    std::string base = "\xE2\x80\x94";
    if (const auto* b = store_.find(v->base)) base = b->label();
    head.properties.push_back(hmikit::prop("Bas\xC3\xA9" "e sur", base, PG::ValueType::ReadOnly));
    cats.push_back(std::move(head));
    PG::Category content;
    content.name = "Contenu";
    content.properties.push_back(hmikit::prop("Fichiers", std::to_string(v->fileCount) + " \xC2\xB7 " + ver::sizeText(v->bytes), PG::ValueType::ReadOnly));
    content.properties.push_back(hmikit::prop("Nouveaux octets",
                                              ver::sizeText(v->newBytes) + (v->newBytes < v->bytes ? " (le reste est partag\xC3\xA9)" : std::string{}),
                                              PG::ValueType::ReadOnly));
    content.properties.push_back(hmikit::prop("Depuis la pr\xC3\xA9" "c\xC3\xA9" "dente", v->since, PG::ValueType::ReadOnly));
    content.properties.push_back(hmikit::prop("Empreinte", v->fingerprint, PG::ValueType::ReadOnly));
    cats.push_back(std::move(content));
    grid_->setCategories(std::move(cats));
}

bool VersionsPane::setInfo(int number, const std::string& name, ver::State state, const std::string& comment) {
    if (auto st = ver::update(store_, number, name, state, comment); !st) {
        say("Version V" + std::to_string(number) + " : " + st.error().context, true);
        return false;
    }
    say("V" + std::to_string(number) + " : " + name + " \xC2\xB7 " + std::string(ver::stateLabel(state)));
    rebuildTable();
    selectVersion(number);
    if (hosts_.changed) hosts_.changed();
    return true;
}

bool VersionsPane::setAutoMode(ver::AutoMode m) {
    store_.autoMode = m;
    if (store_.folder.empty()) return false;
    if (auto st = ver::saveIndex(store_); !st) {
        say("Version automatique : " + st.error().context, true);
        return false;
    }
    filling_ = true;
    auto_->setSelectedIndex(static_cast<int>(m));
    filling_ = false;
    say("Version automatique : " + std::string(ver::autoLabel(m)));
    return true;
}

void VersionsPane::updateStatus() {
    if (!message_.empty()) return;
    std::string text;
    if (store_.versions.empty()) {
        text = "Pas encore de version. \xC2\xAB Cr\xC3\xA9" "er une version \xC2\xBB (Ctrl+Alt+S) garde l'API, l'IHM et ses ressources, "
               "dans versions/ du projet.";
    } else {
        const auto* last = store_.last();
        text = std::to_string(store_.versions.size()) + (store_.versions.size() == 1 ? " version" : " versions") + " \xC2\xB7 "
             + ver::sizeText(store_.storedBytes()) + " dans versions/ (au lieu de " + ver::sizeText(store_.copyBytes()) + " en copies) \xC2\xB7 "
             + "la derni\xC3\xA8re : V" + std::to_string(last->number) + ", " + std::string(ver::stateLabel(last->state)) + " le "
             + shortDate(last->date).substr(0, 5) + " \xC3\xA0 " + (last->date.size() >= 16 ? last->date.substr(11, 5) : std::string{});
    }
    status_->setMessage(text);
}

gfx::Rect VersionsPane::timelineStrip() const {
    const auto* t = static_cast<const VersionTimeline*>(timeline_);
    const auto s = t->strip();
    return {s.left, t->bounds().y, s.width(), t->bounds().h};
}

bool VersionsPane::dotVisible(int number) const {
    return static_cast<const VersionTimeline*>(timeline_)->visibleDot(number);
}

float VersionsPane::timelineScroll() const {
    return static_cast<const VersionTimeline*>(timeline_)->scroll();
}

bool VersionsPane::timelineScrolls() const {
    return static_cast<const VersionTimeline*>(timeline_)->strip().scrolls();
}

void VersionsPane::scrollTimeline(float dx) {
    auto* t = static_cast<VersionTimeline*>(timeline_);
    t->scrollTo(t->scroll() + dx);
}

gfx::Rect VersionsPane::dotRect(int number) const {
    const auto* t = static_cast<const VersionTimeline*>(timeline_);
    const gfx::Point p = t->centreOf(number);
    return {p.x - 8.f, p.y - 8.f, 16.f, 16.f};
}

void VersionsPane::onLayout() {
    const auto b = bounds();
    const float gridW = std::min(430.f, b.w * 0.3f);
    const float mainW = b.w - gridW - 4.f;
    // La barre sur toute la largeur ; la version automatique a son bout.
    tools_->setBounds({b.x, b.y, std::max(200.f, b.w - 460.f), 38});
    auto_->setBounds({b.x + b.w - 290.f, b.y + 5.f, 280.f, 28.f});
    timeline_->setBounds({b.x, b.y + 38, mainW, 76});
    status_->setBounds({b.x, b.y + b.h - 24, b.w, 24});
    const float top = b.y + 38 + 78;
    const float avail = std::max(0.f, b.y + b.h - 24 - top);
    const float tableH = std::max(120.f, avail * 0.45f);
    table_->setBounds({b.x, top, mainW, tableH});
    changesTitleY_ = top + tableH + 6.f;
    changes_->setBounds({b.x, changesTitleY_ + 24.f, mainW, std::max(0.f, b.y + b.h - 24 - changesTitleY_ - 24.f)});
    grid_->setBounds({b.x + b.w - gridW, b.y + 38, gridW, std::max(0.f, b.h - 62)});
}

void VersionsPane::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.panelBg);
    const float gridW = std::min(430.f, b.w * 0.3f);
    const float mainW = b.w - gridW - 4.f;
    ctx.r.fillRect({b.x, changesTitleY_, mainW, 24.f}, ctx.theme.color.headerBg);
    ctx.r.drawText({b.x + 10.f, changesTitleY_ + 4.f}, changesTitle_, kSmall, ctx.theme.color.textMuted);
    // "Version automatique :" devant la liste.
    const std::string lab = "Version automatique :";
    const float lw = ctx.r.measure(lab, kSmall).width;
    const auto ab = auto_->bounds();
    ctx.r.drawText({ab.x - lw - 8.f, ab.y + 6.f}, lab, kSmall, ctx.theme.color.textMuted);
}

} // namespace app
