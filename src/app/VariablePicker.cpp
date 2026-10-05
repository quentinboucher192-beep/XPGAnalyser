// app/VariablePicker.cpp - le choix des variables d'une table d'animation (lot API 3).
#include "VariablePicker.hpp"

#include "../menu/MenuManager.hpp"
#include "../ui/Icons.hpp"
#include "../ui/TextSearch.hpp"          // lot recherche : la recherche de toutes les listes
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/ScrollBar.hpp"   // 1.11.4 : la barre de defilement qu'on tire

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <sstream>

namespace app {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kBody{15};
const gfx::FontId kMono{14};
constexpr float kRow = 30.f;

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool isBoolType(std::string_view t) {
    const auto u = upper(t);
    return u == "BOOL" || u == "EBOOL";
}

bool isNumericType(std::string_view t) {
    static const char* const kNum[] = {"INT", "UINT", "DINT", "UDINT", "WORD", "DWORD", "BYTE", "REAL", "LREAL",
                                       "SINT", "USINT", "LINT", "ULINT", "TIME"};
    const auto u = upper(t);
    return std::any_of(std::begin(kNum), std::end(kNum), [&](const char* k) { return u == k; });
}

bool isTextType(std::string_view t) { return upper(t).rfind("STRING", 0) == 0; }

// Un texte coupe a la largeur, avec des points de suspension.
std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (r.measure(s, f).width <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    const auto n = r.fitCharacters(s, f, std::max(0.f, w - r.measure(dots, f).width));
    return s.substr(0, n) + dots;
}

} // namespace

// ================================================================ le corps ====
//  Tout est dessine ici (les filtres, les lignes a cocher) ; seuls le champ de
//  recherche et les deux boutons sont des widgets.
class VariablePicker::Body final : public ui::Widget {
public:
    Body(VariablePicker& owner, const Spec& spec) : ui::Widget("dialog.variablePicker"), owner_(owner), spec_(spec) {
        checked_.assign(spec_.candidates.size(), false);
        search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.variablePicker.chercher")));
        search_->setPlaceholder("Chercher : nom, description\xE2\x80\xA6");
        links_ += search_->textChanged->connect([this](const std::string&) { rebuild(); });
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.variablePicker.annuler")));
        ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ajouter", "dialog.variablePicker.ajouter")));
        ok_->setStyle(ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += ok_->clicked->connect([this] { owner_.finish(true); });
        // Les filtres : leur libelle et leur predicat.
        filters_.push_back({"Toutes", [](const Candidate&) { return true; }});
        filters_.push_back({"BOOL", [](const Candidate& c) { return isBoolType(c.type); }});
        filters_.push_back({"Num\xC3\xA9riques", [](const Candidate& c) { return isNumericType(c.type); }});
        if (spec_.hmi) {
            filters_.push_back({"Cha\xC3\xAEnes", [](const Candidate& c) { return isTextType(c.type); }});
            filters_.push_back({"Li\xC3\xA9" "es \xC3\xA0 l'automate", [](const Candidate& c) { return c.linked; }});
        } else {
            filters_.push_back({"Structures", [](const Candidate& c) {
                return !isBoolType(c.type) && !isNumericType(c.type) && !isTextType(c.type);
            }});
            filters_.push_back({"Situ\xC3\xA9" "es", [](const Candidate& c) { return c.linked; }});
        }
        rebuild();
    }

    void rebuild() {
        shown_.clear();
        // Lot recherche : la recherche de toutes les listes - chaque mot (ou
        // "phrase") dans le nom, le type ou la description / le commentaire,
        // aucun -mot exclu, sans casse ni accents.
        const ui::SearchQuery query(search_ ? search_->text() : std::string{});
        for (std::size_t i = 0; i < spec_.candidates.size(); ++i) {
            const auto& c = spec_.candidates[i];
            if (!filters_[filter_].keep(c)) continue;
            if (!query.matches({c.name, c.type, c.detail})) continue;
            shown_.push_back(i);
        }
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
        sync();
        invalidate();
    }

    void sync() {
        const auto n = checkedCount();
        ok_->setText(n == 0 ? std::string("Ajouter") : n == 1 ? std::string("Ajouter 1 variable")
                                                               : "Ajouter " + std::to_string(n) + " variables");
        ok_->setEnabled(n > 0);
        invalidateLayout();
    }

    [[nodiscard]] std::size_t checkedCount() const {
        return static_cast<std::size_t>(std::count(checked_.begin(), checked_.end(), true));
    }
    [[nodiscard]] std::size_t shownCount() const { return shown_.size(); }

    bool toggleByName(std::string_view name) {
        for (std::size_t i = 0; i < spec_.candidates.size(); ++i)
            if (spec_.candidates[i].name == name) {
                if (spec_.candidates[i].present) return false;
                checked_[i] = !checked_[i];
                // Amenee en vue, comme apres un clic (une capture la montre cochee).
                for (std::size_t k = 0; k < shown_.size(); ++k)
                    if (shown_[k] == i) {
                        const float y = static_cast<float>(k) * kRow;
                        if (y < scroll_ || y + kRow > scroll_ + list_.h) scroll_ = std::clamp(y - kRow * 2.f, 0.f, maxScroll());
                    }
                sync();
                invalidate();
                return true;
            }
        return false;
    }

    bool chooseFilter(std::string_view label) {
        for (std::size_t i = 0; i < filters_.size(); ++i)
            if (filters_[i].label.rfind(std::string(label), 0) == 0) {
                filter_ = i;
                rebuild();
                return true;
            }
        return false;
    }

    void setSearch(const std::string& text) { search_->setText(text); rebuild(); }

    [[nodiscard]] std::string payload() const {
        std::string out;
        for (std::size_t i = 0; i < checked_.size(); ++i)
            if (checked_[i]) out += spec_.candidates[i].name + "\n";
        return out;
    }

    [[nodiscard]] const ui::Button* okButton() const noexcept { return ok_; }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(920.f, r.w - 40.f), h = std::min(700.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        search_->setBounds({panel_.x + 16.f, panel_.y + 52.f, std::min(300.f, w * 0.34f), 30.f});
        list_ = {panel_.x + 1.f, panel_.y + 94.f, panel_.w - 2.f, panel_.h - 94.f - 60.f};
        const float bw = std::max(150.f, ui::measureWidth(ok_->text(), gfx::FontId{16}) + 36.f);
        ok_->setBounds({panel_.right() - 16.f - bw, panel_.bottom() - 46.f, bw, 32.f});
        cancel_->setBounds({ok_->bounds().x - 10.f - 100.f, panel_.bottom() - 46.f, 100.f, 32.f});
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        // Le titre, et sa croix.
        ui::drawIcon(r, spec_.hmi ? ui::Icon::Screen : ui::Icon::Variable, {panel_.x + 16.f, panel_.y + 14.f, 18.f, 18.f}, c.accent);
        r.drawText({panel_.x + 42.f, panel_.y + 13.f}, fit(r, spec_.title, gfx::FontId{17}, panel_.w - 100.f), gfx::FontId{17}, c.text);
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", gfx::FontId{17}, hoverClose_ ? c.text : c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        // Les filtres, a droite du champ.
        chips_.clear();
        float x = search_->bounds().right() + 12.f;
        for (std::size_t i = 0; i < filters_.size(); ++i) {
            std::size_t count = 0;
            for (const auto& cand : spec_.candidates) count += filters_[i].keep(cand) ? 1u : 0u;
            const std::string text = filters_[i].label + (i < 2 ? "  " + std::to_string(count) : std::string{});
            const float tw = r.measure(text, kSmall).width + 22.f;
            const gfx::Rect chip{x, search_->bounds().y + 3.f, tw, 24.f};
            const bool on = i == filter_;
            r.fillRoundedRect(chip, on ? c.accent.withAlpha(60) : c.headerBg, 12.f);
            if (on) r.strokeRect(chip, c.accent, 1.f);
            r.drawText({chip.x + 11.f, chip.y + (chip.h - r.lineHeight(kSmall)) * 0.5f}, text, kSmall, on ? c.text : c.textMuted);
            chips_.push_back(chip);
            x += tw + 8.f;
        }
        // Les lignes.
        r.fillRect(list_, c.windowBg);
        r.pushClip(list_);
        const float typeX = list_.right() - 250.f, detailX = list_.right() - 150.f;
        const std::size_t first = static_cast<std::size_t>(std::max(0.f, scroll_ / kRow));
        for (std::size_t k = first; k < shown_.size(); ++k) {
            const float y = list_.y + static_cast<float>(k) * kRow - scroll_;
            if (y > list_.bottom()) break;
            const auto i = shown_[k];
            const auto& cand = spec_.candidates[i];
            const gfx::Rect row{list_.x, y, list_.w, kRow};
            if (checked_[i]) r.fillRect(row, c.selectionBg);
            else if (static_cast<int>(k) == hover_) r.fillRect(row, c.rowAltBg);
            r.fillRect({row.x, row.bottom() - 1.f, row.w, 1.f}, c.gridLine);
            const auto ink = cand.present ? c.textDisabled : c.text;
            // La case.
            const gfx::Rect box{row.x + 18.f, row.y + 7.f, 16.f, 16.f};
            if (checked_[i]) {
                r.fillRoundedRect(box, c.accent, 3.f);
                r.line({box.x + 3.5f, box.y + 8.5f}, {box.x + 6.5f, box.y + 11.5f}, c.textInverted, 2.f);
                r.line({box.x + 6.5f, box.y + 11.5f}, {box.x + 12.5f, box.y + 4.5f}, c.textInverted, 2.f);
            } else {
                r.strokeRect(box, cand.present ? c.border : c.borderStrong, 1.f);
            }
            // La pastille de la source.
            const std::string tag = spec_.hmi ? "IHM" : "API";
            const gfx::Rect pill{row.x + 46.f, row.y + 7.f, 36.f, 16.f};
            const auto tone = ctx.theme.tone(spec_.hmi ? ui::Tone::Family1 : ui::Tone::Info, c.accent);
            r.fillRoundedRect(pill, tone.withAlpha(cand.present ? 30 : 70), 3.f);
            r.drawText({pill.x + (pill.w - r.measure(tag, gfx::FontId{11}).width) * 0.5f, pill.y + 1.f}, tag, gfx::FontId{11},
                       cand.present ? c.textDisabled : ctx.theme.onSurface(tone));
            r.drawText({row.x + 92.f, row.y + (kRow - r.lineHeight(kMono)) * 0.5f},
                       fit(r, cand.name + (cand.present ? "   (d\xC3\xA9j\xC3\xA0 dans la table)" : std::string{}), kMono, typeX - row.x - 100.f),
                       kMono, ink);
            r.drawText({typeX, row.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, fit(r, cand.type, kSmall, detailX - typeX - 8.f), kSmall,
                       cand.present ? c.textDisabled : c.textMuted);
            r.drawText({detailX, row.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, fit(r, cand.detail, kSmall, list_.right() - detailX - 14.f),
                       kSmall, cand.present ? c.textDisabled : c.textMuted);
        }
        if (shown_.empty())
            r.drawText({list_.x + 20.f, list_.y + 16.f}, "Aucune variable ne correspond.", kBody, c.textMuted);
        r.popClip();
        // La barre de defilement (1.11.4 : elle se tire).
        sbar_.paint(ctx, list_, static_cast<float>(shown_.size()) * kRow, list_.h, scroll_);
        r.fillRect({panel_.x, list_.bottom(), panel_.w, 1.f}, c.border);
        if (!spec_.note.empty())
            r.drawText({panel_.x + 16.f, panel_.bottom() - 38.f}, fit(r, spec_.note, kSmall, cancel_->bounds().x - panel_.x - 30.f), kSmall,
                       c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        {
            float off = scroll_;   // 1.11.4 : la barre de defilement se tire
            if (sbar_.handle(*this, ev, list_, static_cast<float>(shown_.size()) * kRow, list_.h, off)) {
                scroll_ = std::clamp(off, 0.f, maxScroll());
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const int h = rowAt(m->pos);
            const bool hc = close_.contains(m->pos);
            if (h != hover_ || hc != hoverClose_) {
                hover_ = h;
                hoverClose_ = hc;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!list_.contains(w->pos)) return ui::EventResult::Ignored;
            scroll_ = std::clamp(scroll_ - w->dy * kRow * 3.f, 0.f, maxScroll());
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (close_.contains(d->pos)) {
                owner_.finish(false);
                return ui::EventResult::Consumed;
            }
            for (std::size_t i = 0; i < chips_.size(); ++i)
                if (chips_[i].contains(d->pos)) {
                    filter_ = i;
                    rebuild();
                    return ui::EventResult::Consumed;
                }
            const int k = rowAt(d->pos);
            if (k >= 0) {
                const auto i = shown_[static_cast<std::size_t>(k)];
                if (!spec_.candidates[i].present) {
                    checked_[i] = !checked_[i];
                    sync();
                    invalidate();
                }
                return ui::EventResult::Consumed;
            }
            return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Filter {
        std::string                             label;
        std::function<bool(const Candidate&)>   keep;
    };
    [[nodiscard]] float maxScroll() const {
        return std::max(0.f, static_cast<float>(shown_.size()) * kRow - list_.h);
    }
    [[nodiscard]] int rowAt(gfx::Point p) const {
        if (!list_.contains(p)) return -1;
        const auto k = static_cast<std::size_t>((p.y - list_.y + scroll_) / kRow);
        return k < shown_.size() ? static_cast<int>(k) : -1;
    }

    VariablePicker&            owner_;
    const Spec&                spec_;
    std::vector<bool>          checked_;
    std::vector<std::size_t>   shown_;
    std::vector<Filter>        filters_;
    std::size_t                filter_{0};
    ui::InputText*             search_{nullptr};
    ui::Button*                cancel_{nullptr};
    ui::Button*                ok_{nullptr};
    gfx::Rect                  panel_{}, list_{}, close_{};
    std::vector<gfx::Rect>     chips_;
    float                      scroll_{0.f};
    ui::EdgeScrollBar          sbar_;   // 1.11.4
    int                        hover_{-1};
    bool                       hoverClose_{false};
    core::ConnectionScope      links_;
};

// ============================================================== le dialogue ====
VariablePicker::VariablePicker(Spec spec) : menu::WidgetMenu("dialog.variablePicker"), spec_(std::move(spec)) {}

menu::MenuTraits VariablePicker::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;       // la simulation continue dessous : les valeurs de la table bougent
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status VariablePicker::buildUi() {
    auto body = std::make_unique<Body>(*this, spec_);
    body_ = body.get();
    setRoot(std::move(body));
    return core::ok();
}

ui::EventResult VariablePicker::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && body_ && body_->checkedCount() > 0) {
            finish(true);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

bool VariablePicker::toggle(std::string_view name) { return body_ && body_->toggleByName(name); }
bool VariablePicker::chooseFilter(std::string_view label) { return body_ && body_->chooseFilter(label); }
void VariablePicker::setSearch(const std::string& text) { if (body_) body_->setSearch(text); }
std::size_t VariablePicker::checkedCount() const { return body_ ? body_->checkedCount() : 0; }
std::size_t VariablePicker::shownCount() const { return body_ ? body_->shownCount() : 0; }

void VariablePicker::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             body_ ? body_->payload() : std::string{}});
}

std::vector<std::string> VariablePicker::parse(const std::string& payload) {
    std::vector<std::string> out;
    std::stringstream in(payload);
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) out.push_back(line);
    return out;
}

} // namespace app
