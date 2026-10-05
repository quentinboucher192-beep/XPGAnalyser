// =============================================================================
//  app/SimConditionDialog.cpp - lot API 8 (2e partie) : la condition d'un point
//  d'arret (voir l'en-tete)
// =============================================================================
#include "SimConditionDialog.hpp"

#include "../menu/MenuManager.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kButton{16};

std::string& nextText() {
    static std::string text;
    return text;
}
bool& hasNextText() {
    static bool has = false;
    return has;
}

std::vector<std::string> wrapWords(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
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
    for (const char c : s) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

float buttonWidth(const std::string& text) { return std::max(96.f, ui::measureWidth(text, kButton) + 36.f); }
float pillWidth(const std::string& text) { return ui::measureWidth(text, kButton) + 24.f; }

// Le corps : place a la main de haut en bas, comme les autres petits dialogues.
class ConditionBody final : public ui::Widget {
public:
    ConditionBody(std::string title, std::string code) : title_(std::move(title)), code_(std::move(code)) {}
    ui::InputText*           field{nullptr};
    std::vector<ui::Button*> pills;
    ui::Button*              remove{nullptr};
    ui::Button*              cancel{nullptr};
    ui::Button*              ok{nullptr};
    const std::string*       live{nullptr};
    float                    width{600.f};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(width, std::max(320.f, r.w - 40.f));
        const float inner = w - 32.f;
        // Les pastilles, en lignes.
        std::vector<gfx::Rect> at;
        float px = 70.f, rows = pills.empty() ? 0.f : 1.f;
        for (auto* p : pills) {
            const float pw = std::min(inner - 70.f, pillWidth(p->text()));
            if (px + pw > inner && px > 70.f) {
                px = 70.f;
                rows += 1.f;
            }
            at.push_back({px, (rows - 1.f) * 32.f, pw, 26.f});
            px += pw + 8.f;
        }
        const float liveH = static_cast<float>(std::max<std::size_t>(1, live ? wrapWords(*live, kSmall, inner).size() : 1)) * 18.f;
        const float titleH = 34.f;
        const float h = titleH + 12.f + 32.f + 12.f + 20.f + 30.f + 12.f + std::max(26.f, rows * 32.f) + 10.f + liveH + 16.f + 44.f;
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + 16.f;
        float y = panel_.y + titleH + 12.f;
        codeBox_ = {x, y, inner, 32.f};
        y += 32.f + 12.f;
        labelAt_ = y;
        y += 20.f;
        if (field) field->setBounds({x, y, inner, 30.f});
        y += 30.f + 12.f;
        ideasAt_ = y;
        for (std::size_t i = 0; i < pills.size() && i < at.size(); ++i)
            pills[i]->setBounds({x + at[i].x, y + at[i].y, at[i].w, at[i].h});
        y += std::max(26.f, rows * 32.f) + 10.f;
        liveAt_ = y;
        const float by = panel_.bottom() - 44.f;
        if (remove) remove->setBounds({x, by, buttonWidth(remove->text()), 30.f});
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
        const float titleH = 34.f;
        r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        const auto bold = ctx.theme.font.uiBold;
        r.drawText({panel_.x + 12.f, panel_.y + (titleH - r.lineHeight(bold)) * 0.5f}, title_, bold, c.text);
        // Le code de la ligne.
        r.fillRect(codeBox_, c.inputBg);
        r.strokeRect(codeBox_, c.border, 1.f);
        const auto mono = ctx.theme.font.mono;
        std::string code = code_;
        while (code.size() > 4 && ui::measureWidth(code, mono) > codeBox_.w - 16.f) code.resize(code.size() - 4);
        if (code.size() < code_.size()) code += "...";
        r.drawText({codeBox_.x + 8.f, codeBox_.y + (codeBox_.h - r.lineHeight(mono)) * 0.5f}, code, mono, c.text);
        r.drawText({panel_.x + 16.f, labelAt_ + 2.f}, "S'arr\xC3\xAAter seulement si", kSmall, c.textMuted);
        r.drawText({panel_.x + 16.f, ideasAt_ + 5.f}, pills.empty() ? "Id\xC3\xA9" "es : aucune (pas de valeur lue sur cette ligne)" : "Id\xC3\xA9" "es :",
                   kSmall, c.textMuted);
        if (live) {
            float y = liveAt_;
            for (const auto& l : wrapWords(*live, kSmall, panel_.w - 32.f)) {
                r.drawText({panel_.x + 16.f, y}, l, kSmall, c.text);
                y += 18.f;
            }
        }
    }

private:
    std::string title_, code_;
    gfx::Rect   panel_{}, codeBox_{};
    float       labelAt_{0.f}, ideasAt_{0.f}, liveAt_{0.f};
};

} // namespace

void SimConditionDialog::setNextText(std::string text) {
    nextText() = std::move(text);
    hasNextText() = true;
}

SimConditionDialog::SimConditionDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {}

menu::MenuTraits SimConditionDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;       // la simulation continue dessous : « Maintenant » suit
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status SimConditionDialog::buildUi() {
    auto body = std::make_unique<ConditionBody>(spec_.title, spec_.code);
    const std::string base = spec_.id;
    auto f = std::make_unique<ui::InputText>(base + ".champ");
    std::string initial = spec_.condition;
    if (hasNextText()) {
        initial = nextText();
        hasNextText() = false;
        nextText().clear();
    }
    f->setText(initial);
    f->setPlaceholder("vide = \xC3\xA0 chaque passage");
    field_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
    body->field = field_;
    links_ += field_->textChanged->connect([this](const std::string&) {
        nextLive_ = 0.0;                 // la phrase suit tout de suite
        if (body_) body_->invalidate();
    });
    for (std::size_t i = 0; i < spec_.ideas.size(); ++i) {
        auto* pill = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(spec_.ideas[i], base + ".idee" + std::to_string(i))));
        pill->setStyle(ui::Button::Style::Flat);
        links_ += pill->clicked->connect([this, i] { chooseIdea(i); });
        body->pills.push_back(pill);
    }
    body->remove = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Retirer le point", base + ".retirer")));
    body->remove->setStyle(ui::Button::Style::Danger);
    links_ += body->remove->clicked->connect([this] { finish(menu::DialogResult::Button::No); });
    body->cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    links_ += body->cancel->clicked->connect([this] { finish(menu::DialogResult::Button::Cancel); });
    body->ok = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Valider", base + ".valider")));
    body->ok->setStyle(ui::Button::Style::Primary);
    links_ += body->ok->clicked->connect([this] { finish(menu::DialogResult::Button::Ok); });
    live_ = spec_.preview ? spec_.preview(initial) : std::string{};
    body->live = &live_;
    body_ = body.get();
    setRoot(std::move(body));
    // Le champ a le focus : on tape tout de suite (Entree valide).
    focus().clear();
    focus().focus(field_);
    return core::ok();
}

std::string SimConditionDialog::fieldText() const { return field_ ? field_->text() : std::string{}; }

std::string SimConditionDialog::liveText() const { return live_; }

void SimConditionDialog::chooseIdea(std::size_t index) {
    if (!field_ || index >= spec_.ideas.size()) return;
    field_->setText(spec_.ideas[index]);
    nextLive_ = 0.0;
    focus().clear();
    focus().focus(field_);
}

void SimConditionDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    // En direct (quatre fois par seconde : la simulation tourne peut-etre dessous).
    if (spec_.preview && field_ && f.totalSeconds >= nextLive_) {
        nextLive_ = f.totalSeconds + 0.25;
        auto said = spec_.preview(field_->text());
        if (said != live_) {
            live_ = std::move(said);
            if (body_) {
                body_->invalidateLayout();
                body_->invalidate();
            }
        }
    }
}

ui::EventResult SimConditionDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape && !(field_ && field_->suggestionsOpen())) {
            finish(menu::DialogResult::Button::Cancel);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && !(field_ && field_->suggestionsOpen())) {
            finish(menu::DialogResult::Button::Ok);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void SimConditionDialog::finish(menu::DialogResult::Button button) {
    if (done_) return;
    done_ = true;
    std::string payload;
    if (button == menu::DialogResult::Button::Ok && field_) payload = field_->text();
    manager().CloseDialog(menu::DialogResult{button, std::move(payload)});
}

} // namespace app
