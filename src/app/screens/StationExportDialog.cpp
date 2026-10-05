// =============================================================================
//  app/screens/StationExportDialog.cpp - Lot API 8 : ou enregistrer, au poste
// =============================================================================
#include "StationExportDialog.hpp"

#include "../../menu/MenuManager.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace app {

namespace {

// AU DOIGT : des cibles de 76 a 84 px de haut, du texte de 20 a 30 px.
constexpr gfx::FontId kTitleFont{30};
constexpr gfx::FontId kTextFont{22};
constexpr gfx::FontId kFieldFont{24};
constexpr gfx::FontId kButtonFont{28};
constexpr gfx::FontId kHintFont{18};
constexpr float       kTitleH  = 72.f;
constexpr float       kFieldH  = 76.f;
constexpr float       kBrowseW = 124.f;
constexpr float       kButtonH = 84.f;
constexpr float       kPad     = 28.f;

// Le theme de l'ecran, le texte des controles agrandi : ils dessinent avec
// font.ui (Button, InputText), le reste ne change pas.
ui::Theme withFont(const ui::Theme& base, gfx::FontId f) {
    ui::Theme t = base;
    t.font.ui = f;
    t.font.uiBold = f;
    return t;
}

// Un texte coupe a la largeur, mot a mot (les \n comptent).
std::vector<std::string> wrap(const std::string& s, gfx::FontId f, float w) {
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
        if (c == ' ') {
            flush();
        } else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else {
            word += c;
        }
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// Un grand bouton : celui de la bibliotheque (ses etats, son clic, ce que
// `bouton "Exporter"` retrouve), son texte en grand.
class TouchButton final : public ui::Button {
public:
    TouchButton(std::string text, std::string id) : ui::Button(std::move(text), std::move(id)) {}
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {std::max(220.f, ui::measureWidth(text(), kButtonFont) + 64.f), kButtonH};
        h.minimum = {160.f, kButtonH};
        return h;
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const ui::Theme big = withFont(ctx.theme, kButtonFont);
        ui::Button::onPaint(ui::PaintContext{ctx.r, big, ctx.clip, ctx.time, ctx.overlays});
    }
};

// Le chemin : un champ ordinaire (un clavier branche le corrige), en grand. Un
// toucher y met le focus sans placer le curseur sous le doigt : le champ compte
// ses caracteres en petite police, la place serait fausse ; le curseur reste
// au bout du chemin (Debut, les fleches le deplacent).
class TouchPathField final : public ui::InputText {
public:
    using ui::InputText::InputText;
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const ui::Theme big = withFont(ctx.theme, kFieldFont);
        ui::InputText::onPaint(ui::PaintContext{ctx.r, big, ctx.clip, ctx.time, ctx.overlays});
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && bounds().contains(d->pos)) {
            grabFocus();
            return ui::EventResult::Consumed;
        }
        return ui::InputText::onEvent(ev);
    }
};

// Le corps : tout, place a la main, au centre de l'ecran.
class StationExportBody final : public ui::Widget {
public:
    StationExportBody(std::string id, std::string title, std::string denied)
        : ui::Widget(std::move(id)), title_(std::move(title)), denied_(std::move(denied)) {}

    ui::InputText* field{nullptr};
    ui::Widget*    browse{nullptr};
    ui::Button*    cancel{nullptr};
    ui::Button*    ok{nullptr};

    // Les lignes d'aide a une largeur donnee (la hauteur du dialogue en depend) ;
    // le bouton ... refuse (le kiosque) : seulement ce qui est propose.
    [[nodiscard]] std::vector<std::string> hintLines(float w) const { return wrap(denied_.empty() ? kHint : kHintFixed, kHintFont, w); }
    [[nodiscard]] std::vector<std::string> deniedLines(float w) const {
        return denied_.empty() ? std::vector<std::string>{} : wrap(denied_, kHintFont, w);
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::max(360.f, std::min(1080.f, r.w - 32.f));
        const float inner = w - 2.f * kPad;
        const float hintLine = ui::lineHeight(kHintFont) + 4.f;
        const float texts = static_cast<float>(hintLines(inner).size() + deniedLines(inner).size()) * hintLine;
        const float wanted = kTitleH + 22.f + ui::lineHeight(kTextFont) + 12.f + kFieldH + 16.f + texts + 26.f + kButtonH + 14.f
                             + ui::lineHeight(kHintFont) + 20.f;
        const float h = std::min(std::max(200.f, r.h - 24.f), wanted);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + kPad;
        float y = panel_.y + kTitleH + 22.f;
        questionAt_ = y;
        y += ui::lineHeight(kTextFont) + 12.f;
        if (field) field->setBounds({x, y, std::max(120.f, inner - kBrowseW - 14.f), kFieldH});
        if (browse) browse->setBounds({x + inner - kBrowseW, y, kBrowseW, kFieldH});
        y += kFieldH + 16.f;
        hintAt_ = y;
        // Les boutons en bas, a droite : Annuler, puis Exporter (le principal).
        const float by = panel_.y + panel_.h - 20.f - ui::lineHeight(kHintFont) - 14.f - kButtonH;
        const float bw = std::min(300.f, (inner - 24.f) * 0.5f);
        if (ok) ok->setBounds({x + inner - bw, by, bw, kButtonH});
        if (cancel) cancel->setBounds({x + inner - 2.f * bw - 24.f, by, bw, kButtonH});
        keysAt_ = by + kButtonH + 14.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 2.f);
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, kTitleH}, c.headerBg);
        const float x = panel_.x + kPad, inner = panel_.w - 2.f * kPad;
        ctx.r.drawText({x, panel_.y + (kTitleH - ctx.r.lineHeight(kTitleFont)) * 0.5f}, title_, kTitleFont, c.text);
        ctx.r.drawText({x, questionAt_}, "O\xC3\xB9 enregistrer le fichier ?", kTextFont, c.text);
        const float hintLine = ctx.r.lineHeight(kHintFont) + 4.f;
        float y = hintAt_;
        for (const auto& l : hintLines(inner)) {
            ctx.r.drawText({x, y}, l, kHintFont, c.textMuted);
            y += hintLine;
        }
        for (const auto& l : deniedLines(inner)) {
            ctx.r.drawText({x, y}, l, kHintFont, c.warning);
            y += hintLine;
        }
        ctx.r.drawText({x, keysAt_}, "Entr\xC3\xA9" "e : Exporter      \xC2\xB7      \xC3\x89" "chap : Annuler", kHintFont, c.textMuted);
    }

    // Le bouton ... en grand : trois points DESSINES (un glyphe hors Latin-1
    // se dessine plutot qu'il ne s'ecrit, voir ui/Theme.hpp) et « Parcourir »
    // dessous - par-dessus le bouton, qui garde son fond, ses etats, son clic.
    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (!browse) return;
        const auto b = browse->bounds();
        const auto& c = ctx.theme.color;
        const gfx::Color ink = browse->enabled() ? c.text : c.textDisabled;
        const float d = 10.f, gap = 9.f;
        const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.36f;
        for (int k = -1; k <= 1; ++k)
            ctx.r.fillRoundedRect({cx + static_cast<float>(k) * (d + gap) - d * 0.5f, cy - d * 0.5f, d, d}, ink, d * 0.5f);
        const std::string caption = "Parcourir";
        const float tw = ctx.r.measure(caption, kHintFont).width;
        ctx.r.drawText({cx - tw * 0.5f, b.y + b.h * 0.60f}, caption, kHintFont, ink);
    }

private:
    static constexpr const char* kHint =
        "Propos\xC3\xA9 : le dossier exports/ du projet, sous le nom habituel (Exporter : comme avant). "
        "Parcourir ouvre l'explorateur : un autre dossier, un autre nom. Un dossier tap\xC3\xA9 garde le nom habituel.";
    static constexpr const char* kHintFixed = "Le fichier va dans le dossier exports/ du projet, sous le nom habituel.";
    std::string title_, denied_;
    gfx::Rect   panel_{};
    float       questionAt_{0}, hintAt_{0}, keysAt_{0};
};

std::string trimmedPath(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

} // namespace

StationExportDialog::StationExportDialog(Spec spec) : menu::WidgetMenu("dialog.stationExport"), spec_(std::move(spec)) {}

menu::MenuTraits StationExportDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;       // l'IHM reste la, et tourne dessous (le poste ne s'arrete pas)
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string StationExportDialog::title() const { return "Exporter " + spec_.what; }

core::Status StationExportDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<StationExportBody>(base + ".body", title(), spec_.browseDenied);
    auto* b = body.get();
    auto field = std::make_unique<TouchPathField>(base + ".field");
    field->setText(spec_.proposed);
    field->setReadOnly(spec_.fixedPath);
    field_ = &static_cast<ui::InputText&>(b->addChild(std::move(field)));
    b->field = field_;
    // Le bouton ... : l'explorateur ("Enregistrer sous"), la ou pointe le champ.
    // Son texte est dessine en grand par le corps (des points, « Parcourir »).
    auto browse = std::make_unique<ui::BrowseButton>(*field_, spec_.browse, base + ".parcourir", " ");
    browse->setFieldLabel(kFieldLabel);
    browse_ = &static_cast<ui::BrowseButton&>(b->addChild(std::move(browse)));
    b->browse = browse_;
    if (!spec_.browseDenied.empty() || !spec_.browse.active()) {
        browse_->setEnabled(false);
        browse_->setTooltip(spec_.browseDenied);
    }
    // Le chemin choisi : Exporter a le focus (Entree exporte, comme toujours ici).
    links_ += browse_->chosen->connect([this](const std::string&) {
        focus().clear();
        if (ok_) focus().focus(ok_);
    });
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<TouchButton>("Annuler", base + ".cancel")));
    b->cancel = cancel_;
    links_ += cancel_->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<TouchButton>("Exporter", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    b->ok = ok_;
    links_ += ok_->clicked->connect([this] { finish(true); });
    setRoot(std::move(body));
    focus().focus(ok_);
    return core::ok();
}

ui::EventResult StationExportDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        // Entree : Exporter, ou qu'est le focus (le champ compris).
        if (k->key == ui::Key::Return) {
            finish(true);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void StationExportDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    const std::string path = ok && field_ ? trimmedPath(field_->text()) : std::string{};
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, path});
}

} // namespace app
