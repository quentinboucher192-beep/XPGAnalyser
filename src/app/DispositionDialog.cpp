// =============================================================================
//  app/DispositionDialog.cpp - 1.12.3 : voir DispositionDialog.hpp
// =============================================================================
#include "DispositionDialog.hpp"

#include "../menu/MenuManager.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace dp = disposition;

namespace {

const gfx::FontId kTitleFont{20};
const gfx::FontId kSmall{13};
constexpr float kW = 880.f;

const char* const kHidden = "Cach\xC3\xA9";
const std::string kSep = " \xE2\x80\xBA ";      // ›

std::string labelOf(const std::vector<dp::Choice>& list, std::string_view key) {
    for (const auto& c : list)
        if (c.key == key) return c.label;
    return list.empty() ? std::string{} : list.front().label;
}
std::string keyOf(const std::vector<dp::Choice>& list, std::string_view label) {
    for (const auto& c : list)
        if (c.label == label || c.key == label) return c.key;
    return {};
}
std::vector<std::string> labelsOf(const std::vector<dp::Choice>& list) {
    std::vector<std::string> out;
    for (const auto& c : list) out.push_back(c.label);
    return out;
}

} // namespace

// Le corps : le titre, les dispositions toutes faites, la grille, le resume, les boutons.
class DispositionBody final : public ui::Widget {
public:
    explicit DispositionBody(DispositionDialog& d) : ui::Widget("disposition.body"), d_(d) {}

protected:
    void onLayout() override {
        const auto r = bounds();
        const float h = std::min(760.f, std::max(320.f, r.h - 40.f));
        const float w = std::min(kW, std::max(420.f, r.w - 40.f));
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + 16.f, right = panel_.right() - 16.f;
        float y = panel_.y + 56.f;
        if (d_.presets_) d_.presets_->setBounds({x + 110.f, y, 240.f, 28.f});
        if (d_.mine_) d_.mine_->setBounds({x + 362.f, y, 250.f, 28.f});
        y += 40.f;
        const float bottom = panel_.bottom() - 52.f - 26.f;
        if (d_.grid_) d_.grid_->setBounds({x, y, right - x, std::max(80.f, bottom - y)});
        summaryAt_ = bottom + 6.f;
        float bx = right;
        for (ui::Button* b : {d_.ok_, d_.apply_, d_.cancel_}) {
            if (!b) continue;
            const float bw = std::max(100.f, ui::measureWidth(b->text(), gfx::FontId{15}) + 34.f);
            bx -= bw;
            b->setBounds({bx, panel_.bottom() - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        if (d_.reset_) {
            const float bw = ui::measureWidth(d_.reset_->text(), gfx::FontId{15}) + 34.f;
            d_.reset_->setBounds({x, panel_.bottom() - 44.f, bw, 30.f});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect(panel_, c.panelBg, 8.f);
        ctx.r.strokeRect(panel_, c.border, 1.f);
        const float x = panel_.x + 16.f;
        const std::string head = "Disposition \xE2\x80\x93 " + d_.spec_.edition;
        ctx.r.drawText({x, panel_.y + 14.f}, head, kTitleFont, c.text);
        ctx.r.drawText({x + ui::measureWidth(head, kTitleFont) + 14.f, panel_.y + 20.f},
                       "gard\xC3\xA9" "e dans les r\xC3\xA9glages de l'\xC3\xA9" "dition", kSmall, c.textMuted);
        ctx.r.drawText({x, panel_.y + 62.f}, "Toute faite :", kSmall, c.text);
        ctx.r.drawText({x, summaryAt_}, d_.summary(), kSmall, c.textMuted);
    }

private:
    DispositionDialog& d_;
    gfx::Rect          panel_{};
    float              summaryAt_{0.f};
};

DispositionDialog::DispositionDialog(Spec spec)
    : menu::WidgetMenu("dialog.disposition"), spec_(std::move(spec)), draft_(spec_.applied) {}

menu::MenuTraits DispositionDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status DispositionDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<DispositionBody>(*this);
    auto* b = body.get();
    auto presets = std::make_unique<ui::DropDown>(base + ".presets");
    std::vector<ui::DropDown::Item> items;
    for (const auto& p : dp::presets()) items.push_back({p.label, p.key});
    presets->setItems(std::move(items));
    presets->setSelectedIndex(-1);
    presets_ = &static_cast<ui::DropDown&>(b->addChild(std::move(presets)));
    links_ += presets_->selectionChanged->connect([this](int i) {
        const auto list = dp::presets();
        if (i >= 0 && static_cast<std::size_t>(i) < list.size()) choosePreset(list[static_cast<std::size_t>(i)].key);
    });
    mine_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Garder comme \xC2\xAB Ma disposition \xC2\xBB", base + ".mine")));
    links_ += mine_->clicked->connect([this] { keepMine(); });
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setShowDescriptionPane(true);
    grid->setNameColumnRatio(0.52f);
    grid_ = &static_cast<ui::PropertyGrid&>(b->addChild(std::move(grid)));
    reset_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("R\xC3\xA9tablir la disposition d'origine", base + ".reset")));
    links_ += reset_->clicked->connect([this] { resetOrigin(); });
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    links_ += cancel_->clicked->connect([this] { cancel(); });
    apply_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Appliquer", base + ".apply")));
    links_ += apply_->clicked->connect([this] { applyNow(); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("OK", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { accept(); });
    setRoot(std::move(body));
    rebuild();
    return core::ok();
}

ui::EventResult DispositionDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape && !(grid_ && grid_->activeField())) {
            cancel();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

std::string DispositionDialog::summary() const {
    std::size_t hidden = 0, onlySim = 0;
    for (const auto& r : dp::rows()) {
        const auto& it = draft_.item(r.id);
        if (r.kind == dp::Kind::Page) continue;
        if (!it.shown) ++hidden;
        else if (it.when == "simulation") ++onlySim;
    }
    const std::size_t n = dp::differences(draft_, spec_.applied);
    return std::to_string(hidden) + " \xC3\xA9l\xC3\xA9ment(s) cach\xC3\xA9(s), " + std::to_string(onlySim) + " seulement en simulation. "
           + (n ? std::to_string(n) + " changement(s) pas encore appliqu\xC3\xA9(s)." : std::string("Rien \xC3\xA0 appliquer."));
}

bool DispositionDialog::set(const std::string& id, const std::string& field, const std::string& key) {
    if (field == "choisi") {                       // un groupe de sous-onglets : celui du depart
        const dp::Row* r = dp::row(key);
        if (!r || r->group != id || r->kind != dp::Kind::Tab) return false;
        draft_.startTab[id] = key;
        rebuild();
        return true;
    }
    const dp::Row* r = dp::row(id);
    if (!r) return false;
    auto& it = draft_.items[id];
    if (field == "quand") {
        if (key == "cache") {
            if (r->kind == dp::Kind::Page) return false;
            it.shown = false;
        } else if (r->kind == dp::Kind::Option) {
            it.shown = key != "non";
        } else {
            const auto& when = dp::whenChoices(r->kind);
            if (std::none_of(when.begin(), when.end(), [&](const dp::Choice& c) { return c.key == key; })) return false;
            it.shown = true;
            it.when = key;
        }
    } else if (field == "ou") {
        if (std::none_of(r->where.begin(), r->where.end(), [&](const dp::Choice& c) { return c.key == key; })) return false;
        it.where = key;
    } else if (field == "depart") {
        if (std::none_of(r->start.begin(), r->start.end(), [&](const dp::Choice& c) { return c.key == key; })) return false;
        it.start = key;
    } else {
        return false;
    }
    rebuild();
    return true;
}

void DispositionDialog::choosePreset(const std::string& key) {
    draft_ = dp::preset(key, &spec_.mine);
    rebuild();
}

void DispositionDialog::applyNow() {
    spec_.applied = draft_;
    if (spec_.apply) spec_.apply(draft_);
    rebuild();
}

void DispositionDialog::accept() {
    applyNow();
    finish(true);
}

void DispositionDialog::cancel() { finish(false); }

void DispositionDialog::keepMine() {
    spec_.mine = draft_;
    if (spec_.keepMine) spec_.keepMine(draft_);
    rebuild();
}

void DispositionDialog::resetOrigin() {
    draft_ = dp::defaults();
    spec_.applied = draft_;
    if (spec_.reset) spec_.reset();
    if (spec_.apply) spec_.apply(draft_);
    rebuild();
}

void DispositionDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
}

void DispositionDialog::rebuild() {
    if (!grid_) return;
    using PG = ui::PropertyGrid;
    std::vector<PG::Category> cats;
    for (const auto& g : dp::groups()) {
        PG::Category cat;
        cat.name = g.title;
        for (const auto& r : dp::rows()) {
            if (r.group != g.id) continue;
            const auto& it = draft_.item(r.id);
            const std::string id = r.id;
            const bool changed = !(it == spec_.applied.item(r.id));
            PG::Property p;
            p.name = r.label;
            p.key = r.id;
            if (r.kind == dp::Kind::Option) {
                p.type = PG::ValueType::Boolean;
                p.value = it.shown ? "TRUE" : "FALSE";
                p.commit = [this, id](std::string_view v) {
                    const bool on = v == "TRUE" || v == "1" || v == "oui";
                    return set(id, "quand", on ? "oui" : "non");
                };
            } else {
                const auto& when = dp::whenChoices(r.kind);
                p.type = PG::ValueType::Enum;
                p.enumValues = labelsOf(when);
                if (r.kind != dp::Kind::Page) p.enumValues.push_back(kHidden);
                p.value = it.shown || r.kind == dp::Kind::Page ? labelOf(when, it.when) : std::string(kHidden);
                p.commit = [this, id, when](std::string_view v) {
                    if (v == kHidden) return set(id, "quand", "cache");
                    const std::string k = keyOf(when, v);
                    return !k.empty() && set(id, "quand", k);
                };
                p.description = r.kind == dp::Kind::Page
                                    ? std::string("Quand la page s'ouvre toute seule : \xC3\xA0 la demande (jamais seule), \xC3\xA0 l'ouverture du projet, "
                                                  "au d\xC3\xA9marrage de la simulation.")
                                    : std::string("Toujours, seulement en \xC3\xA9" "dition ou seulement en simulation (il appara\xC3\xAEt au "
                                                  "d\xC3\xA9marrage de la simulation et repart \xC3\xA0 l'arr\xC3\xAAt) ; Cach\xC3\xA9 : jamais.");
            }
            if (changed) p.valueTone = ui::Tone::Accent;
            cat.properties.push_back(std::move(p));
            if (!r.where.empty()) {
                PG::Property w;
                w.name = r.label + kSep + "o\xC3\xB9";
                w.key = r.id + ".ou";
                w.type = r.where.size() > 1 ? PG::ValueType::Enum : PG::ValueType::ReadOnly;
                w.enumValues = labelsOf(r.where);
                w.value = labelOf(r.where, it.where);
                const auto where = r.where;
                w.commit = [this, id, where](std::string_view v) {
                    const std::string k = keyOf(where, v);
                    return !k.empty() && set(id, "ou", k);
                };
                cat.properties.push_back(std::move(w));
            }
            if (!r.start.empty()) {
                PG::Property s;
                s.name = r.label + kSep + "au d\xC3\xA9part";
                s.key = r.id + ".depart";
                s.type = PG::ValueType::Enum;
                s.enumValues = labelsOf(r.start);
                s.value = labelOf(r.start, it.start);
                const auto start = r.start;
                s.commit = [this, id, start](std::string_view v) {
                    const std::string k = keyOf(start, v);
                    return !k.empty() && set(id, "depart", k);
                };
                cat.properties.push_back(std::move(s));
            }
        }
        if (g.tabs) {
            // Le sous-onglet choisi a l'ouverture de la page.
            std::vector<dp::Choice> tabs;
            for (const auto& r : dp::rows())
                if (r.group == g.id && r.kind == dp::Kind::Tab) tabs.push_back({r.id, r.tabTitle});
            PG::Property s;
            s.name = "Au d\xC3\xA9part, l'onglet";
            s.key = "depart." + g.id;
            s.type = PG::ValueType::Enum;
            s.enumValues = labelsOf(tabs);
            const auto at = draft_.startTab.find(g.id);
            s.value = labelOf(tabs, at == draft_.startTab.end() ? std::string_view{} : std::string_view(at->second));
            const std::string gid = g.id;
            s.commit = [this, gid, tabs](std::string_view v) {
                const std::string k = keyOf(tabs, v);
                return !k.empty() && set(gid, "choisi", k);
            };
            s.description = "Le sous-onglet ouvert quand la page s'ouvre (un sous-onglet cach\xC3\xA9 ne l'est jamais).";
            cat.properties.push_back(std::move(s));
        }
        cats.push_back(std::move(cat));
    }
    grid_->setCategories(std::move(cats));
}

} // namespace app
