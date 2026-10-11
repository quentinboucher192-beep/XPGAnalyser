// =============================================================================
//  app/hmi/HmiCalcDialog.cpp - 1.12.7 : la fenetre d'une valeur calculee au
//  demarrage (voir l'en-tete)
// =============================================================================
#include "HmiCalcDialog.hpp"

#include "../../core/FieldCodec.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/Syntax.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace app {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kButton{16};

float buttonWidth(const std::string& text) { return std::max(96.f, ui::measureWidth(text, kButton) + 36.f); }

// Un texte coupe en lignes (les \n d'abord, puis les mots a la largeur).
std::vector<std::string> wrapped(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto nl = s.find('\n', from);
        const std::string para = s.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
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
        for (const char c : para) {
            if (c == ' ') flush();
            else word += c;
        }
        flush();
        if (!line.empty()) out.push_back(line);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    return out;
}

class CalcBody final : public ui::Widget {
public:
    CalcBody(std::string title, std::string codeLabel) : title_(std::move(title)), codeLabel_(std::move(codeLabel)) {}
    ui::Widget*        code{nullptr};
    ui::Widget*        locals{nullptr};
    ui::Button*        cancel{nullptr};
    ui::Button*        ok{nullptr};
    const std::string* live{nullptr};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(900.f, std::max(420.f, r.w - 40.f));
        const float h = std::min(700.f, std::max(420.f, r.h - 40.f));
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + 16.f, inner = w - 32.f;
        float y = panel_.y + 34.f + 10.f;
        codeLabelAt_ = y;
        y += 20.f;
        const float codeH = std::max(120.f, (h - 34.f - 10.f - 20.f - 8.f - 20.f - 90.f - 8.f - 20.f - 130.f - 54.f));
        if (code) code->setBounds({x, y, inner, codeH});
        y += codeH + 8.f;
        localsLabelAt_ = y;
        y += 20.f;
        if (locals) locals->setBounds({x, y, inner, 90.f});
        y += 90.f + 8.f;
        liveLabelAt_ = y;
        liveAt_ = y + 20.f;
        const float by = panel_.bottom() - 44.f;
        liveBottom_ = by - 8.f;
        float bx = panel_.right() - 16.f;
        for (auto* b : {ok, cancel}) {
            if (!b) continue;
            const float bw = buttonWidth(b->text());
            bx -= bw;
            b->setBounds({bx, by, bw, 30.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.fillRect({panel_.x, panel_.y, panel_.w, 34.f}, c.headerBg);
        const auto bold = ctx.theme.font.uiBold;
        r.drawText({panel_.x + 12.f, panel_.y + (34.f - r.lineHeight(bold)) * 0.5f}, title_, bold, c.text);
        r.drawText({panel_.x + 16.f, codeLabelAt_ + 2.f}, codeLabel_, kSmall, c.textMuted);
        r.drawText({panel_.x + 16.f, localsLabelAt_ + 2.f},
                   "Les locales, une par ligne (sans VAR ni END_VAR) : Nom : TYPE := valeur;   // documentation", kSmall, c.textMuted);
        r.drawText({panel_.x + 16.f, liveLabelAt_ + 2.f}, "Ce que donne le calcul (F5 : recalculer tout de suite)", kSmall, c.textMuted);
        if (live) {
            float y = liveAt_;
            for (const auto& l : wrapped(*live, kSmall, panel_.w - 40.f)) {
                if (y + 18.f > liveBottom_) break;
                const bool bad = l.rfind("Faute", 0) == 0 || l.rfind("Erreur", 0) == 0;
                const bool warn = l.rfind("Attention", 0) == 0;
                r.drawText({panel_.x + 20.f, y}, l, kSmall, bad ? c.error : warn ? c.warning : c.text);
                y += 18.f;
            }
        }
    }

private:
    std::string title_, codeLabel_;
    gfx::Rect   panel_{};
    float       codeLabelAt_{0.f}, localsLabelAt_{0.f}, liveLabelAt_{0.f}, liveAt_{0.f}, liveBottom_{0.f};
};

} // namespace

HmiCalcDialog::HmiCalcDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {}

menu::MenuTraits HmiCalcDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status HmiCalcDialog::buildUi() {
    const std::string what = std::string(spec_.constant ? "Le code : il \xC3\xA9" "crit " : "Le code : il \xC3\xA9" "crit la valeur initiale de ")
                           + spec_.name + " (" + spec_.type + ") une fois, au d\xC3\xA9marrage - comme un final de Java";
    auto body = std::make_unique<CalcBody>(spec_.title, what);
    const std::string base = spec_.id;
    const auto editor = [&](const std::string& id, const std::string& text) {
        auto e = std::make_unique<ui::MultiLineText>(base + "." + id);
        e->setLanguage(ui::Language::StructuredText);
        e->setText(text);
        e->setReadOnly(false);
        auto* raw = &static_cast<ui::MultiLineText&>(body->addChild(std::move(e)));
        links_ += raw->textChanged->connect([this](const std::string&) {
            dirty_ = true;
            changedAt_ = now_;
        });
        return raw;
    };
    code_ = editor("code", spec_.calc);
    locals_ = editor("locales", spec_.locals);
    body->code = code_;
    body->locals = locals_;
    body->cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    links_ += body->cancel->clicked->connect([this] { finish(false); });
    body->ok = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Appliquer", base + ".appliquer")));
    body->ok->setStyle(ui::Button::Style::Primary);
    links_ += body->ok->clicked->connect([this] { finish(true); });
    body->live = &live_;
    body_ = body.get();
    setRoot(std::move(body));
    recompute();
    focus().clear();
    focus().focus(code_);
    return core::ok();
}

std::string HmiCalcDialog::calcText() const { return code_ ? code_->text() : spec_.calc; }
std::string HmiCalcDialog::localsText() const { return locals_ ? locals_->text() : spec_.locals; }

void HmiCalcDialog::setTexts(const std::string& calc, const std::string& locals) {
    if (code_) code_->setText(calc);
    if (locals_) locals_->setText(locals);
    recompute();
}

void HmiCalcDialog::recompute() {
    dirty_ = false;
    live_ = spec_.preview ? spec_.preview(calcText(), localsText()) : std::string{};
    if (body_) body_->invalidate();
}

void HmiCalcDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    now_ = f.totalSeconds;
    // Une demi-seconde apres la derniere frappe : l'apercu suit.
    if (dirty_ && now_ - changedAt_ >= 0.5) recompute();
}

ui::EventResult HmiCalcDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::F5) {
            recompute();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void HmiCalcDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? core::joinFields({calcText(), localsText()}) : std::string{}});
}

} // namespace app
