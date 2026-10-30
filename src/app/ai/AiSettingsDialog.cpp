// =============================================================================
//  app/ai/AiSettingsDialog.cpp - voir AiSettingsDialog.hpp
// =============================================================================
#include "AiSettingsDialog.hpp"

#include "AiPanel.hpp"
#include "AiPaths.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>   // std::size

namespace app::ai {

namespace {

const gfx::FontId kTitleFont{20};
const gfx::FontId kText{14};
const gfx::FontId kSmall{13};
constexpr float kRow = 32.f;

const int kDays[] = {0, 7, 30, 90, 365};
const char* const kDaysLabel[] = {"Toujours", "7 jours", "30 jours", "90 jours", "1 an"};
const int kCounts[] = {0, 20, 50, 100, 500};
const char* const kCountsLabel[] = {"Sans limite", "20 conversations", "50 conversations", "100 conversations", "500 conversations"};

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line, word;
    const auto push = [&] {
        if (word.empty()) return;
        const std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(t, f) > width) {
            out.push_back(line);
            line = word;
        } else {
            line = t;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') push();
        else word += c;
    }
    push();
    if (!line.empty()) out.push_back(line);
    return out;
}

const char* const kKeyNote = "Gard\xC3\xA9" "e en m\xC3\xA9moire pour cette s\xC3\xA9" "ance seulement, jamais \xC3\xA9" "crite sur le disque. "
                             "Quand l'IA sera branch\xC3\xA9" "e, elle sera prot\xC3\xA9g\xC3\xA9" "e par Windows (DPAPI), ou d\xC3\xA9tenue par un "
                             "serveur relais de l'entreprise.";
const char* const kPrivacyNote = "Ce qui partirait chez le fournisseur avec chaque demande. En d\xC3\xA9monstration, rien ne quitte ce PC.";

} // namespace

class AiSettingsBody final : public ui::Widget {
public:
    explicit AiSettingsBody(AiSettingsDialog& d) : ui::Widget("ai.parametres.corps"), d_(d) {}

protected:
    struct Label { gfx::Point at; std::string text; bool heading; float w; };

    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(960.f, std::max(560.f, r.w - 40.f));
        const float h = std::min(560.f, std::max(420.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        labels_.clear();
        notes_.clear();
        const float x0 = panel_.x + 20.f;
        const bool twoCols = panel_.w >= 820.f;
        const float colW = twoCols ? (panel_.w - 60.f) / 2.f : panel_.w - 40.f;
        const float labelW = 150.f;
        float y = panel_.y + 64.f;
        float x = x0;
        const auto heading = [&](const char* text) {
            y += 6.f;
            labels_.push_back({{x, y}, text, true, colW});
            y += 24.f;
        };
        const auto row = [&](const char* label, ui::Widget* wgt, float fieldW = 0.f) {
            labels_.push_back({{x, y + 6.f}, label, false, labelW - 8.f});
            const float fw = fieldW > 0.f ? fieldW : colW - labelW;
            if (wgt) wgt->setBounds({x + labelW, y, fw, kRow - 4.f});
            y += kRow;
        };
        const auto note = [&](const char* text, float indent = 0.f) {
            const auto lines = wrap(text, kSmall, colW - indent);
            notes_.push_back({{x + indent, y}, text, false, colW - indent});
            y += static_cast<float>(lines.size()) * (ui::lineHeight(kSmall) + 3.f) + 6.f;
        };
        // ---- la colonne de gauche
        heading("G\xC3\x89N\xC3\x89RAL");
        d_.enabled_->setBounds({x, y, colW, kRow - 4.f});
        y += kRow;
        row("Fournisseur", d_.provider_);
        heading("CONNEXION");
        row("Mod\xC3\xA8le", d_.model_);
        row("Adresse", d_.endpoint_);
        row("D\xC3\xA9lai (secondes)", d_.timeout_, 90.f);
        heading("CL\xC3\x89 API");
        {
            const float bw1 = ui::measureWidth("Afficher", gfx::FontId{16}) + 24.f;
            const float bw2 = ui::measureWidth("Effacer", gfx::FontId{16}) + 28.f;
            const float kw = colW - bw1 - bw2 - 12.f;
            d_.key_->setBounds({x, y, kw, kRow - 4.f});
            d_.showKey_->setBounds({x + kw + 6.f, y, bw1, kRow - 4.f});
            d_.clearKey_->setBounds({x + kw + 12.f + bw1, y, bw2, kRow - 4.f});
            y += kRow + 2.f;
        }
        note(kKeyNote);
        // ---- la colonne de droite (ou la suite)
        if (twoCols) {
            x = x0 + colW + 20.f;
            y = panel_.y + 64.f;
        }
        heading("FONCTIONNEMENT");
        row("Mode par d\xC3\xA9" "faut", d_.mode_);
        heading("HISTORIQUE DES CONVERSATIONS");
        row("Conserver", d_.days_);
        row("Au plus", d_.count_);
        heading("CONFIDENTIALIT\xC3\x89");
        note(kPrivacyNote);
        for (ui::Checkbox* cb : {d_.sendView_, d_.sendSelection_, d_.sendVariables_, d_.sendAttachments_}) {
            cb->setBounds({x, y, colW, kRow - 6.f});
            y += kRow - 4.f;
        }
        heading("M\xC3\x89MOIRE");
        {
            const float bw = ui::measureWidth(d_.memory_->text(), gfx::FontId{16}) + 34.f;
            d_.memory_->setBounds({x, y, bw, kRow - 2.f});
            y += kRow + 2.f;
        }
        // ---- le bas
        float bx = panel_.right() - 20.f;
        for (ui::Button* b : {d_.ok_, d_.cancel_}) {
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 34.f);
            bx -= bw;
            b->setBounds({bx, panel_.bottom() - 46.f, bw, 30.f});
            bx -= 10.f;
        }
        footerW_ = bx - x0 - 10.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect(panel_, c.panelBg, 8.f);
        ctx.r.strokeRect(panel_, c.border, 1.f);
        const float x = panel_.x + 20.f;
        ui::drawIcon(ctx.r, ui::Icon::Assistant, {x, panel_.y + 13.f, 20.f, 20.f}, c.accent);
        ctx.r.drawText({x + 28.f, panel_.y + 10.f}, "Param\xC3\xA8tres de l'Assistant IA", kTitleFont, c.text);
        ctx.r.drawText({x + 28.f + ctx.r.measure("Param\xC3\xA8tres de l'Assistant IA", kTitleFont).width + 14.f, panel_.y + 17.f},
                       "D\xC3\xA9monstration \xE2\x80\x93 aucune IA connect\xC3\xA9" "e", kSmall, c.textMuted);
        for (const auto& l : labels_) {
            if (l.heading) {
                ctx.r.drawText(l.at, l.text, kSmall, c.textMuted);
                ctx.r.fillRect({l.at.x, l.at.y + ctx.r.lineHeight(kSmall) + 3.f, l.w, 1.f}, c.border);
            } else {
                ctx.r.drawText(l.at, elide(ctx.r, l.text, kText, l.w), kText, c.text);
            }
        }
        for (const auto& n : notes_) {
            float y = n.at.y;
            for (const auto& line : wrap(n.text, kSmall, n.w)) {
                ctx.r.drawText({n.at.x, y}, line, kSmall, c.textMuted);
                y += ctx.r.lineHeight(kSmall) + 3.f;
            }
        }
        const std::string folder = root().empty() ? std::string("Dossier de l'Assistant : aucun (rien n'est \xC3\xA9" "crit)")
                                                  : "Dossier de l'Assistant : " + root();
        ctx.r.drawText({x, panel_.bottom() - 38.f}, elide(ctx.r, folder, kSmall, footerW_), kSmall, c.textMuted);
    }

private:
    AiSettingsDialog&  d_;
    gfx::Rect          panel_{};
    std::vector<Label> labels_, notes_;
    float              footerW_{0.f};
};

AiSettingsDialog::AiSettingsDialog(AiPanel& panel) : menu::WidgetMenu("dialog.ai.parametres"), panel_(panel) {}

menu::MenuTraits AiSettingsDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status AiSettingsDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<AiSettingsBody>(*this);
    auto* b = body.get();
    const Settings& s = panel_.assistant().settings();
    const auto check = [&](const char* label, const char* suffix, bool on) {
        auto cb = std::make_unique<ui::Checkbox>(label, base + suffix);
        cb->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        return &static_cast<ui::Checkbox&>(b->addChild(std::move(cb)));
    };
    const auto input = [&](const char* suffix, const std::string& text, const char* placeholder) {
        auto in = std::make_unique<ui::InputText>(base + suffix);
        in->setText(text);
        in->setPlaceholder(placeholder);
        return &static_cast<ui::InputText&>(b->addChild(std::move(in)));
    };
    enabled_ = check("Activer l'Assistant IA", ".actif", s.enabled);
    provider_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".fournisseur")));
    {
        std::vector<ui::DropDown::Item> items;
        int sel = 0;
        for (std::size_t i = 0; i < providers().size(); ++i) {
            const auto& p = providers()[i];
            items.push_back({std::string(p.label), std::string(p.key), {}, p.available});
            if (p.key == s.provider) sel = static_cast<int>(i);
        }
        provider_->setItems(std::move(items));
        provider_->setSelectedIndex(sel);
    }
    model_ = input(".modele", s.model, "donn\xC3\xA9 par le fournisseur");
    model_->setTooltip("Le nom du mod\xC3\xA8le, tel que le fournisseur le donne : rien n'est impos\xC3\xA9 par le logiciel.");
    endpoint_ = input(".adresse", s.endpoint, "https://\xE2\x80\xA6");
    endpoint_->setTooltip("L'adresse du service, ou celle du serveur relais de l'entreprise (qui d\xC3\xA9tient alors la cl\xC3\xA9).");
    timeout_ = input(".delai", std::to_string(s.timeoutSeconds), "60");
    timeout_->setValidator([](std::string_view t) { return t.size() <= 3 && std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
    key_ = input(".cle", panel_.assistant().apiKey(), "la cl\xC3\xA9 du fournisseur");
    key_->setMasked(true);
    showKey_ = &static_cast<ui::ToggleButton&>(b->addChild(std::make_unique<ui::ToggleButton>("Afficher", base + ".afficher")));
    links_ += showKey_->toggled->connect([this](bool on) { showKey(on); });
    clearKey_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Effacer", base + ".effacer")));
    links_ += clearKey_->clicked->connect([this] { clearKey(); });
    mode_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".mode")));
    mode_->setItems({{"Pr\xC3\xA9visualisation (l'aper\xC3\xA7u d'abord)", "previsualisation"}, {"Direct (appliquer d\xC3\xA8s la r\xC3\xA9ponse)", "direct"}});
    mode_->setSelectedIndex(s.mode == "direct" ? 1 : 0);
    days_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".jours")));
    {
        std::vector<ui::DropDown::Item> items;
        int sel = 0;
        for (std::size_t i = 0; i < std::size(kDays); ++i) {
            items.push_back({kDaysLabel[i], std::to_string(kDays[i])});
            if (kDays[i] == s.keepDays) sel = static_cast<int>(i);
        }
        days_->setItems(std::move(items));
        days_->setSelectedIndex(sel);
    }
    count_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".nombre")));
    {
        std::vector<ui::DropDown::Item> items;
        int sel = 0;
        for (std::size_t i = 0; i < std::size(kCounts); ++i) {
            items.push_back({kCountsLabel[i], std::to_string(kCounts[i])});
            if (kCounts[i] == s.keepCount) sel = static_cast<int>(i);
        }
        count_->setItems(std::move(items));
        count_->setSelectedIndex(sel);
    }
    sendView_ = check("La vue active (ses objets et leurs propri\xC3\xA9t\xC3\xA9s)", ".vue", s.sendView);
    sendSelection_ = check("La s\xC3\xA9lection", ".selection", s.sendSelection);
    sendVariables_ = check("Les variables (noms et types, jamais leurs valeurs)", ".variables", s.sendVariables);
    sendAttachments_ = check("Les pi\xC3\xA8" "ces jointes", ".pieces", s.sendAttachments);
    memory_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("M\xC3\xA9moire et apprentissage\xE2\x80\xA6", base + ".memoire")));
    const bool memory = panel_.uiHost().memoryAvailable && panel_.uiHost().memoryAvailable();
    memory_->setEnabled(memory);
    memory_->setTooltip(memory ? "Ce que l'IA conna\xC3\xAEt et m\xC3\xA9morise, et la place qu'elle occupe sur le PC."
                               : "Le centre \xC2\xAB M\xC3\xA9moire et apprentissage de l'IA \xC2\xBB n'est pas encore dans cette version.");
    links_ += memory_->clicked->connect([this] { openMemory(); });
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    links_ += cancel_->clicked->connect([this] { cancel(); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Enregistrer", base + ".enregistrer")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { save(); });
    setRoot(std::move(body));
    return core::ok();
}

ui::Widget* AiSettingsDialog::field(const std::string& name) const {
    if (name == "actif") return enabled_;
    if (name == "fournisseur") return provider_;
    if (name == "modele") return model_;
    if (name == "adresse") return endpoint_;
    if (name == "delai") return timeout_;
    if (name == "cle") return key_;
    if (name == "mode") return mode_;
    if (name == "jours") return days_;
    if (name == "nombre") return count_;
    if (name == "vue") return sendView_;
    if (name == "selection") return sendSelection_;
    if (name == "variables") return sendVariables_;
    if (name == "pieces") return sendAttachments_;
    if (name == "memoire") return memory_;
    return nullptr;
}

Settings AiSettingsDialog::draft() const {
    Settings s = panel_.assistant().settings();
    s.enabled = enabled_->isChecked();
    if (const auto* it = provider_->selectedItem(); it && it->enabled) s.provider = it->value;
    s.model = model_->text();
    s.endpoint = endpoint_->text();
    s.timeoutSeconds = std::clamp(std::atoi(timeout_->text().c_str()), 5, 600);
    if (const auto* it = mode_->selectedItem()) s.mode = it->value;
    if (const auto* it = days_->selectedItem()) s.keepDays = std::atoi(it->value.c_str());
    if (const auto* it = count_->selectedItem()) s.keepCount = std::atoi(it->value.c_str());
    s.sendView = sendView_->isChecked();
    s.sendSelection = sendSelection_->isChecked();
    s.sendVariables = sendVariables_->isChecked();
    s.sendAttachments = sendAttachments_->isChecked();
    return s;
}

std::string AiSettingsDialog::keyText() const { return key_ ? key_->text() : std::string{}; }

void AiSettingsDialog::showKey(bool on) {
    key_->setMasked(!on);
    if (showKey_->checked() != on) showKey_->setChecked(on);
}

void AiSettingsDialog::clearKey() {
    key_->setText({});
    panel_.assistant().setApiKey({});
}

void AiSettingsDialog::openMemory() {
    if (panel_.uiHost().openMemory) panel_.uiHost().openMemory();
}

void AiSettingsDialog::save() {
    panel_.assistant().setSettings(draft());
    panel_.assistant().setApiKey(key_->text());    // en memoire seulement
    panel_.sync();
    finish(true);
}

void AiSettingsDialog::cancel() { finish(false); }

void AiSettingsDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
}

ui::EventResult AiSettingsDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        bool listOpen = false;
        for (ui::DropDown* d : {provider_, mode_, days_, count_})
            if (d && d->isOpen()) listOpen = true;
        if (!listOpen) {
            cancel();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app::ai
