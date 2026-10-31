// =============================================================================
//  app/ai/AiSettingsDialog.cpp - voir AiSettingsDialog.hpp
// =============================================================================
#include "AiSettingsDialog.hpp"

#include "AiOpenAI.hpp"
#include "AiPanel.hpp"
#include "AiPaths.hpp"
#include "AiSecret.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
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

// 1.15.0 : ce que devient la cle (Windows : DPAPI ; Linux : la seance seulement).
std::string keyNote() {
    std::string n = keyProtectionAvailable()
                        ? "M\xC3\xA9moris\xC3\xA9" "e, elle est " + keyProtectionText() + " dans le dossier de l'Assistant ; sinon gard\xC3\xA9" "e pour cette s\xC3\xA9" "ance. "
                        : "Gard\xC3\xA9" "e en m\xC3\xA9moire pour cette s\xC3\xA9" "ance seulement : sous Linux, elle n'est jamais \xC3\xA9" "crite sur le disque. ";
    return n + "Jamais en clair, ni dans un journal. Un relais d'entreprise (l'adresse) peut d\xC3\xA9tenir la cl\xC3\xA9 : laisse alors la case vide.";
}
const char* const kPrivacyNote = "Ce qui part chez le fournisseur avec chaque demande, en plus de la demande et de la conversation. "
                                 "En d\xC3\xA9monstration, rien ne quitte ce PC.";
const char* const kEfforts[][2] = {{"Auto (celui du mod\xC3\xA8le)", ""}, {"Bas", "low"}, {"Moyen", "medium"}, {"Haut", "high"}};
// 1.16.0 : l'outil, et la note de la cle d'un service compatible OpenAI.
const char* const kTools[][2] = {{"Auto (outil, sinon JSON)", "auto"}, {"Appel d'outil", "outil"}, {"JSON dans le texte", "texte"}};
std::string openAIKeyNote() {
    return "Facultative pour un mod\xC3\xA8le sur ce PC (Ollama, LM Studio) ; un service en ligne donne la sienne (selon ses conditions, "
           "parfois gratuite). " + std::string(keyProtectionAvailable() ? "M\xC3\xA9moris\xC3\xA9" "e, elle est prot\xC3\xA9g\xC3\xA9" "e par Windows. "
                                                                   : "Gard\xC3\xA9" "e pour cette s\xC3\xA9" "ance seulement (Linux). ")
           + "Jamais en clair, ni dans un journal.";
}

} // namespace

class AiSettingsBody final : public ui::Widget {
public:
    explicit AiSettingsBody(AiSettingsDialog& d) : ui::Widget("ai.parametres.corps"), d_(d) {}

protected:
    struct Label { gfx::Point at; std::string text; bool heading; float w; };

    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(960.f, std::max(560.f, r.w - 40.f));
        const float h = std::min(640.f, std::max(420.f, r.h - 40.f));
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
        const auto note = [&](const std::string& text, float indent = 0.f) {
            const auto lines = wrap(text, kSmall, colW - indent);
            notes_.push_back({{x + indent, y}, text, false, colW - indent});
            y += static_cast<float>(lines.size()) * (ui::lineHeight(kSmall) + 3.f) + 6.f;
        };
        // ---- la colonne de gauche
        heading("G\xC3\x89N\xC3\x89RAL");
        // 1.16.1 : << M'avertir des reponses >> sur la meme ligne (la fenetre tient a 1280 x 800)
        d_.enabled_->setBounds({x, y, colW * 0.5f - 6.f, kRow - 4.f});
        d_.notify_->setBounds({x + colW * 0.5f, y, colW * 0.5f, kRow - 4.f});
        y += kRow;
        row("Fournisseur", d_.provider_);
        d_.localFirst_->setBounds({x, y, colW * 0.5f - 6.f, kRow - 6.f});   // 1.16.1 : l'IA integree d'abord
        d_.anyway_->setBounds({x + colW * 0.5f, y, colW * 0.5f, kRow - 6.f});    // 1.16.1 : appliquer malgre les erreurs
        y += kRow - 4.f;
        heading("CONNEXION");
        const bool openai = d_.openAIShown();   // 1.16.0 : le Service a la place de l'Effort
        if (openai) row("Service", d_.service_);
        {   // le modele : la case sur toute la largeur (l'indication grise se lit en entier), puis
            // "Lister les modeles" dessous, a droite
            const float lw = ui::measureWidth(d_.listModels_->text(), gfx::FontId{16}) + 28.f;
            row("Mod\xC3\xA8le", d_.model_);
            d_.listModels_->setBounds({x + colW - lw, y, lw, kRow - 4.f});
            y += kRow;
        }
        if (!openai) row("Effort", d_.effort_);
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
        d_.remember_->setBounds({x, y, colW, kRow - 6.f});
        y += kRow - 4.f;
        note(openai ? openAIKeyNote() : keyNote());
        {   // Tester la connexion, et son resultat a cote (a la ligne)
            const float tw = ui::measureWidth(d_.test_->text(), gfx::FontId{16}) + 34.f;
            d_.test_->setBounds({x, y, tw, kRow - 2.f});
            resultAt_ = {x + tw + 10.f, y + 1.f, colW - tw - 10.f, kRow * 2.f};
            y += kRow + 20.f;   // le resultat peut passer a la ligne (une colonne : ce qui suit reste dessous)
        }
        // ---- la colonne de droite (ou la suite)
        if (twoCols) {
            x = x0 + colW + 20.f;
            y = panel_.y + 64.f;
        }
        heading("FONCTIONNEMENT");
        row("Mode par d\xC3\xA9" "faut", d_.mode_);
        // 1.16.0 : la fenetre de contexte, l'outil, le budget (un fournisseur relie).
        row("Fen\xC3\xAAtre de contexte", d_.context_, 150.f);
        row("Outil", d_.tool_);
        {
            const float bw = 130.f;
            row("Budget (jetons)", d_.budget_, bw);
            d_.period_->setBounds({x + labelW + bw + 8.f, y - kRow, colW - labelW - bw - 8.f, kRow - 4.f});
        }
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
        d_.modelsMenu_->setBounds(panel_);
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
        {   // 1.15.0 : l'etat reel (celui des parametres enregistres)
            const float tx = x + 28.f + ctx.r.measure("Param\xC3\xA8tres de l'Assistant IA", kTitleFont).width + 14.f;
            ctx.r.drawText({tx, panel_.y + 17.f}, elide(ctx.r, d_.panel_.assistant().connectionText(), kSmall, panel_.right() - 20.f - tx), kSmall,
                           c.textMuted);
        }
        // Le resultat de Lister / Tester (en cours, reussi, l'erreur).
        {
            std::string text = d_.result_;
            gfx::Color tone = d_.resultOk_ ? c.ok : c.error;
            if (d_.querying()) {
                text = d_.query_ == AiSettingsDialog::Query::Listing ? "Demande de la liste des mod\xC3\xA8les\xE2\x80\xA6" : "Test de la connexion\xE2\x80\xA6";
                tone = c.textMuted;
            }
            float ry = resultAt_.y;
            auto lines = wrap(text, kSmall, resultAt_.w);
            if (lines.size() > 3) {
                lines.resize(3);
                lines.back() += " \xE2\x80\xA6";
            }
            for (const auto& l : lines) {
                ctx.r.drawText({resultAt_.x, ry}, l, kSmall, tone);
                ry += ctx.r.lineHeight(kSmall) + 3.f;
            }
        }
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
    gfx::Rect          resultAt_{};
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
    // 1.16.1 : m'avertir quand une reponse arrive (seulement quand je ne regarde pas le panneau)
    notify_ = check("M'avertir des r\xC3\xA9ponses", ".avertir", s.notifyReplies);
    notify_->setTooltip("Quand une r\xC3\xA9ponse de l'IA arrive et que tu ne regardes pas le panneau de l'Assistant (une autre "
                        "fen\xC3\xAAtre devant, le panneau repli\xC3\xA9, une autre conversation ouverte) : un avis dans la cloche (Voir "
                        "ouvre la conversation) et, la fen\xC3\xAAtre en arri\xC3\xA8re-plan, son bouton clignote dans la barre des "
                        "t\xC3\xA2" "ches avec un son court. D\xC3\xA9" "coch\xC3\xA9" "e : rien.");
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
    // Le modele : une case libre, VIDE par defaut ; l'indication grise cite un exemple (rien en dur).
    model_ = input(".modele", s.model, "ex. claude-opus-5-5");
    model_->setTooltip("Le nom du mod\xC3\xA8le, tel que le fournisseur le donne (\xC2\xAB Lister les mod\xC3\xA8les \xC2\xBB) : rien n'est impos\xC3\xA9 par le logiciel.");
    listModels_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Lister les mod\xC3\xA8les", base + ".lister")));
    listModels_->setTooltip("Les mod\xC3\xA8les que ta cl\xC3\xA9 peut utiliser (la liste du fournisseur, gratuite) : un clic en choisit un.");
    links_ += listModels_->clicked->connect([this] { (void)listModels(); });
    effort_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".effort")));
    {
        std::vector<ui::DropDown::Item> items;
        int sel = 0;
        for (std::size_t i = 0; i < std::size(kEfforts); ++i) {
            items.push_back({kEfforts[i][0], kEfforts[i][1]});
            if (s.effort == kEfforts[i][1]) sel = static_cast<int>(i);
        }
        effort_->setItems(std::move(items));
        effort_->setSelectedIndex(sel);
        effort_->setTooltip("Combien le mod\xC3\xA8le r\xC3\xA9" "fl\xC3\xA9" "chit avant de r\xC3\xA9pondre : Haut est plus soign\xC3\xA9, Bas plus rapide et moins cher.");
    }
    endpoint_ = input(".adresse", s.endpoint, "vide : l'adresse du fournisseur");
    endpoint_->setTooltip("Vide : l'adresse du fournisseur. Sinon, le relais de l'entreprise (https://\xE2\x80\xA6), qui d\xC3\xA9tient alors la cl\xC3\xA9 : "
                          "la demande part \xC3\xA0 <adresse>/v1/messages. http:// seulement vers ce poste (127.0.0.1).");
    // 1.16.0 : le service compatible OpenAI (choisir remplit l'adresse ; le modele reste une case libre).
    service_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".service")));
    {
        std::vector<ui::DropDown::Item> items;
        int sel = static_cast<int>(servicePresets().size()) - 1;   // Autre
        for (std::size_t i = 0; i < servicePresets().size(); ++i) {
            const auto& sp = servicePresets()[i];
            items.push_back({std::string(sp.label), std::string(sp.key)});
            if (sp.key == s.preset) sel = static_cast<int>(i);
        }
        if (s.preset.empty()) sel = 0;   // Ollama : gratuit, sur ce PC
        service_->setItems(std::move(items));
        service_->setSelectedIndex(sel);
        service_->setTooltip("Le service compatible OpenAI : choisir remplit l'Adresse (Autre : l'adresse \xC3\xA0 la main). Ollama et LM Studio "
                             "tournent sur ce PC, gratuits et sans cl\xC3\xA9.");
        links_ += service_->selectionChanged->connect([this](int i) {
            if (const auto* it = service_->selectedItem(); it && i >= 0) (void)chooseService(it->value);
        });
    }
    timeout_ = input(".delai", std::to_string(s.timeoutSeconds), "60");
    timeout_->setValidator([](std::string_view t) { return t.size() <= 3 && std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
    key_ = input(".cle", panel_.assistant().apiKey(), "la cl\xC3\xA9 du fournisseur");
    key_->setMasked(true);
    showKey_ = &static_cast<ui::ToggleButton&>(b->addChild(std::make_unique<ui::ToggleButton>("Afficher", base + ".afficher")));
    links_ += showKey_->toggled->connect([this](bool on) { showKey(on); });
    clearKey_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Effacer", base + ".effacer")));
    clearKey_->setTooltip("Effacer la cl\xC3\xA9 : de la s\xC3\xA9" "ance, et du PC si elle y \xC3\xA9tait m\xC3\xA9moris\xC3\xA9" "e.");
    links_ += clearKey_->clicked->connect([this] { clearKey(); });
    remember_ = check("M\xC3\xA9moriser la cl\xC3\xA9 (prot\xC3\xA9g\xC3\xA9" "e par Windows)", ".memoriser", panel_.assistant().keyRemembered());
    remember_->setEnabled(keyProtectionAvailable());
    remember_->setTooltip(keyProtectionAvailable()
                              ? "La cl\xC3\xA9 est chiffr\xC3\xA9" "e par Windows (DPAPI), li\xC3\xA9" "e \xC3\xA0 ta session : un autre compte, ou un autre PC, ne la relit pas."
                              : "Sous Linux, la cl\xC3\xA9 est gard\xC3\xA9" "e pour cette s\xC3\xA9" "ance seulement : elle n'est jamais \xC3\xA9" "crite.");
    test_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Tester la connexion", base + ".tester")));
    test_->setTooltip("Demande la liste des mod\xC3\xA8les au fournisseur (gratuit, sans consommer de jetons) avec ces r\xC3\xA9glages et cette cl\xC3\xA9.");
    links_ += test_->clicked->connect([this] { (void)testConnection(); });
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
    // 1.16.0 : la fenetre de contexte (vide : Auto), l'outil, le budget.
    context_ = input(".contexte", s.contextTokens > 0 ? std::to_string(s.contextTokens) : std::string{}, "Auto");
    context_->setValidator([](std::string_view t) { return t.size() <= 8 && std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
    context_->setTooltip("La fen\xC3\xAAtre de contexte du mod\xC3\xA8le, en jetons. Vide : Auto (celle que le service dit dans la liste des mod\xC3\xA8les). "
                         "Sous 16 000, les consignes sont resserr\xC3\xA9" "es. Ollama coupe en silence au-del\xC3\xA0 de sa fen\xC3\xAAtre : voir l'aide (F1).");
    tool_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".outil")));
    {
        std::vector<ui::DropDown::Item> items;
        int sel = 0;
        for (std::size_t i = 0; i < std::size(kTools); ++i) {
            items.push_back({kTools[i][0], kTools[i][1]});
            if (s.toolMode == kTools[i][1]) sel = static_cast<int>(i);
        }
        tool_->setItems(std::move(items));
        tool_->setSelectedIndex(sel);
        tool_->setTooltip("Comment le mod\xC3\xA8le propose ses op\xC3\xA9rations : l'appel d'outil, ou un bloc JSON dans sa r\xC3\xA9ponse (les petits "
                          "mod\xC3\xA8les gratuits). Les deux passent par la m\xC3\xAAme v\xC3\xA9rification et le m\xC3\xAAme aper\xC3\xA7u.");
    }
    budget_ = input(".budget", s.budgetTokens > 0 ? std::to_string(s.budgetTokens) : std::string{}, "0 : aucun");
    budget_->setValidator([](std::string_view t) { return t.size() <= 11 && std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }); });
    budget_->setTooltip("Les jetons que tu t'accordes (tous les fournisseurs reli\xC3\xA9s). La jauge du panneau passe \xC3\xA0 l'orange \xC3\xA0 80 %, au "
                        "rouge au-del\xC3\xA0 ; le budget atteint, l'envoi demande confirmation. Vide ou 0 : aucun.");
    period_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".periode")));
    period_->setItems({{"par jour", "jour"}, {"par mois", "mois"}});
    period_->setSelectedIndex(s.budgetPeriod == "mois" ? 1 : 0);
    // 1.16.1 : l'IA integree d'abord, l'IA connectee pour ce qu'elle ne comprend pas
    localFirst_ = check("IA int\xC3\xA9gr\xC3\xA9" "e d'abord", ".dabord", s.localFirst);
    // 1.16.1 : appliquer meme s'il reste des erreurs (une confirmation les liste ; elles vont au journal)
    anyway_ = check("Appliquer malgr\xC3\xA9 les erreurs", ".malgre", s.applyWithErrors);
    anyway_->setTooltip("Une proposition qui garde des erreurs peut s'appliquer quand m\xC3\xAAme : ce qui est valide s'applique, ce qui "
                        "est en erreur est laiss\xC3\xA9 de c\xC3\xB4t\xC3\xA9 (comme l'aper\xC3\xA7u le montre) et not\xC3\xA9 sous la r\xC3\xA9ponse et "
                        "dans le journal d'activit\xC3\xA9. Toujours apr\xC3\xA8s une confirmation qui liste les erreurs ; Ctrl+Z reprend tout. "
                        "Une vue qui a chang\xC3\xA9 depuis la proposition bloque toujours. D\xC3\xA9" "coch\xC3\xA9" "e : une erreur bloque.");
    localFirst_->setTooltip("Essayer d'abord l'IA int\xC3\xA9gr\xC3\xA9" "e (instantan\xC3\xA9" "e, sans connexion). Chaque demande passe d'abord par l'IA int\xC3\xA9gr\xC3\xA9" "e : ce qu'elle comprend est r\xC3\xA9pondu tout de suite, "
                            "sans connexion ni jeton. L'IA connect\xC3\xA9" "e ne re\xC3\xA7oit que ce qu'elle ne comprend pas (et les demandes "
                            "avec une pi\xC3\xA8" "ce jointe).");
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
    {   // la liste des modeles rendue par Lister (par-dessus la fenetre)
        auto menu = std::make_unique<ui::PopupMenu>(base + ".modeles");
        modelsMenu_ = &static_cast<ui::PopupMenu&>(b->addChild(std::move(menu)));
        links_ += modelsMenu_->itemChosen->connect([this](int item) { (void)chooseModel(static_cast<std::size_t>(std::max(0, item))); });
    }
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    links_ += cancel_->clicked->connect([this] { cancel(); });
    ok_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Enregistrer", base + ".enregistrer")));
    ok_->setStyle(ui::Button::Style::Primary);
    links_ += ok_->clicked->connect([this] { save(); });
    links_ += provider_->selectionChanged->connect([this](int) { providerChanged(); });
    setRoot(std::move(body));
    providerChanged();
    return core::ok();
}

// ---- 1.16.0 : le fournisseur choisi, le service --------------------------------------------------
bool AiSettingsDialog::openAIShown() const {
    const auto* it = provider_ ? provider_->selectedItem() : nullptr;
    return it && it->value == "openai";
}

void AiSettingsDialog::providerChanged() {
    const bool openai = openAIShown();
    if (service_) service_->setVisibility(openai ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (effort_) effort_->setVisibility(openai ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    if (endpoint_) {
        endpoint_->setPlaceholder(openai ? "l'adresse du service (choisis-le au-dessus)" : "vide : l'adresse du fournisseur");
        endpoint_->setTooltip(openai ? "L'adresse du service compatible OpenAI, jusqu'\xC3\xA0 /v1 (la demande part \xC3\xA0 <adresse>/chat/completions). "
                                       "http:// vers ce PC ; vers une adresse priv\xC3\xA9" "e de l'usine (192.168.x, 10.x) seulement sans cl\xC3\xA9."
                                     : "Vide : l'adresse du fournisseur. Sinon, le relais de l'entreprise (https://\xE2\x80\xA6), qui d\xC3\xA9tient alors la cl\xC3\xA9 : "
                                       "la demande part \xC3\xA0 <adresse>/v1/messages. http:// seulement vers ce poste (127.0.0.1).");
        // Le fournisseur compatible choisi, l'adresse vide : celle du service.
        if (openai && endpoint_->text().empty())
            if (const auto* it = service_->selectedItem()) (void)chooseService(it->value);
    }
    if (model_) model_->setPlaceholder(openai ? "ex. llama3.2 (Lister les mod\xC3\xA8les)" : "ex. claude-opus-5-5");
    if (key_) key_->setPlaceholder(openai ? "facultative pour un mod\xC3\xA8le sur ce PC" : "la cl\xC3\xA9 du fournisseur");
    // 1.16.0 (captures) : l'IA integree et la demonstration ne se connectent pas - la connexion, le modele, la cle,
    // la fenetre de contexte, l'outil et le budget ne servent pas : grises.
    const auto* it = provider_ ? provider_->selectedItem() : nullptr;
    const bool net = it && providerUsesNetwork(it->value);
    for (ui::Widget* w : {static_cast<ui::Widget*>(model_), static_cast<ui::Widget*>(listModels_), static_cast<ui::Widget*>(key_),
                          static_cast<ui::Widget*>(test_), static_cast<ui::Widget*>(endpoint_), static_cast<ui::Widget*>(timeout_),
                          static_cast<ui::Widget*>(effort_), static_cast<ui::Widget*>(service_), static_cast<ui::Widget*>(context_),
                          static_cast<ui::Widget*>(tool_), static_cast<ui::Widget*>(budget_), static_cast<ui::Widget*>(period_),
                          static_cast<ui::Widget*>(showKey_), static_cast<ui::Widget*>(clearKey_), static_cast<ui::Widget*>(localFirst_)})
        if (w) w->setEnabled(net);
    if (remember_) remember_->setEnabled(net && keyProtectionAvailable());
    if (auto* r = widgetRoot()) r->invalidateLayout();
}

bool AiSettingsDialog::chooseService(std::string_view key) {
    const ServicePreset* sp = findServicePreset(key);
    if (!sp) return false;
    if (service_) {
        const auto& items = service_->items();
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].value == key && service_->selectedIndex() != static_cast<int>(i)) service_->setSelectedIndex(static_cast<int>(i));
    }
    // "Autre" : l'adresse a la main (celle d'un service connu s'efface ; une adresse ecrite reste).
    const std::string current = endpoint_->text();
    bool known = current.empty();
    for (const auto& p : servicePresets()) known = known || (!p.endpoint.empty() && current == p.endpoint);
    if (!sp->endpoint.empty()) endpoint_->setText(std::string(sp->endpoint));
    else if (known) endpoint_->setText({});
    return true;
}

ui::Widget* AiSettingsDialog::field(const std::string& name) const {
    if (name == "actif") return enabled_;
    if (name == "avertir") return notify_;
    if (name == "fournisseur") return provider_;
    if (name == "modele") return model_;
    if (name == "lister") return listModels_;
    if (name == "effort") return effort_;
    if (name == "memoriser") return remember_;
    if (name == "tester") return test_;
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
    if (name == "service") return service_;
    if (name == "contexte") return context_;
    if (name == "outil") return tool_;
    if (name == "budget") return budget_;
    if (name == "periode") return period_;
    if (name == "dabord") return localFirst_;
    if (name == "malgre") return anyway_;
    return nullptr;
}

Settings AiSettingsDialog::draft() const {
    Settings s = panel_.assistant().settings();
    s.enabled = enabled_->isChecked();
    s.notifyReplies = notify_->isChecked();
    if (const auto* it = provider_->selectedItem(); it && it->enabled) s.provider = it->value;
    s.model = model_->text();
    s.endpoint = endpoint_->text();
    s.timeoutSeconds = std::clamp(std::atoi(timeout_->text().c_str()), 5, 600);
    if (const auto* it = effort_->selectedItem()) s.effort = it->value;
    // Les blancs autour d'un nom copie-colle ne font pas partie du modele ni de l'adresse.
    const auto trim = [](std::string t) {
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
        std::size_t i = 0;
        while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i;
        return t.substr(i);
    };
    s.model = trim(s.model);
    s.endpoint = trim(s.endpoint);
    if (const auto* it = mode_->selectedItem()) s.mode = it->value;
    if (const auto* it = days_->selectedItem()) s.keepDays = std::atoi(it->value.c_str());
    if (const auto* it = count_->selectedItem()) s.keepCount = std::atoi(it->value.c_str());
    s.sendView = sendView_->isChecked();
    s.sendSelection = sendSelection_->isChecked();
    s.sendVariables = sendVariables_->isChecked();
    s.sendAttachments = sendAttachments_->isChecked();
    s.localFirst = localFirst_->isChecked();
    s.applyWithErrors = anyway_->isChecked();
    // 1.16.0 : le service, la fenetre de contexte (vide : Auto), l'outil, le budget.
    if (s.provider == "openai")
        if (const auto* it = service_->selectedItem()) s.preset = it->value;
    s.contextTokens = std::clamp(std::atoi(context_->text().c_str()), 0, 10000000);
    if (const auto* it = tool_->selectedItem()) s.toolMode = it->value;
    s.budgetTokens = std::max(0LL, std::atoll(budget_->text().c_str()));
    if (const auto* it = period_->selectedItem()) s.budgetPeriod = it->value;
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
    (void)panel_.assistant().rememberKey(false);   // et du PC, si elle y etait
    remember_->setState(ui::Checkbox::State::Unchecked);
}

// ---- 1.15.0 : lister les modeles, tester la connexion (sur un fil) -----------------------------
bool AiSettingsDialog::querying() const { return query_ != Query::None && panel_.assistant().modelsBusy(); }

bool AiSettingsDialog::startQuery(bool listing) {
    const Settings s = draft();
    resultOk_ = false;
    if (!providerUsesNetwork(s.provider)) {
        result_ = "La d\xC3\xA9monstration et l'IA int\xC3\xA9gr\xC3\xA9" "e n'ont pas de connexion : choisis Anthropic (Claude) ou un service compatible OpenAI.";
        query_ = Query::None;
        return false;
    }
    std::string key = key_->text();
    std::string why;
    if (!panel_.assistant().queryModels(s, key, &why)) {
        result_ = why;
        query_ = Query::None;
        return false;
    }
    query_ = listing ? Query::Listing : Query::Testing;
    seenModels_ = panel_.assistant().modelsRevision();
    result_.clear();
    return true;
}

bool AiSettingsDialog::listModels() { return startQuery(true); }
bool AiSettingsDialog::testConnection() { return startQuery(false); }

bool AiSettingsDialog::pollQuery() {
    auto& as = panel_.assistant();
    (void)as.pollModels();
    if (query_ == Query::None || as.modelsRevision() == seenModels_ || !as.models()) return false;
    seenModels_ = as.modelsRevision();
    const ModelsResult& r = *as.models();
    const Query q = query_;
    query_ = Query::None;
    resultOk_ = r.ok;
    if (!r.ok) {
        result_ = r.error;
        return true;
    }
    const std::string model = model_->text();
    bool known = model.empty();
    for (const auto& m : r.models) known = known || m.id == model;
    if (q == Query::Testing) {
        result_ = "Connexion r\xC3\xA9ussie : " + std::to_string(r.models.size()) + (r.models.size() > 1 ? " mod\xC3\xA8les" : " mod\xC3\xA8le") + ".";
        if (!known) {
            result_ += " Le mod\xC3\xA8le \xC2\xAB " + model + " \xC2\xBB n'est pas dans la liste.";
            resultOk_ = false;
        }
        return true;
    }
    if (r.models.empty()) {
        result_ = "Le fournisseur ne donne aucun mod\xC3\xA8le pour cette cl\xC3\xA9.";
        resultOk_ = false;
        return true;
    }
    result_ = std::to_string(r.models.size()) + (r.models.size() > 1 ? " mod\xC3\xA8les : choisis-en un." : " mod\xC3\xA8le : choisis-le.");
    modelIds_.clear();
    std::vector<ui::PopupMenu::Item> items;
    for (std::size_t i = 0; i < r.models.size(); ++i) {
        ui::PopupMenu::Item it;
        it.label = r.models[i].name.empty() ? r.models[i].id : r.models[i].name;
        if (r.models[i].name != r.models[i].id) it.note = r.models[i].id;
        it.id = static_cast<int>(i);
        it.radio = true;
        it.checked = r.models[i].id == model;
        items.push_back(std::move(it));
        modelIds_.push_back(r.models[i].id);
    }
    modelsMenu_->setItems(std::move(items));
    const auto at = listModels_->bounds();
    modelsMenu_->openInWindow({at.x, at.bottom() + 2.f});
    return true;
}

bool AiSettingsDialog::chooseModel(std::size_t index) {
    if (index >= modelIds_.size()) return false;
    model_->setText(modelIds_[index]);
    if (modelsMenu_->isOpen()) modelsMenu_->close();
    result_ = "Mod\xC3\xA8le choisi : " + modelIds_[index] + " (Enregistrer le garde).";
    resultOk_ = true;
    return true;
}

void AiSettingsDialog::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    (void)pollQuery();
}

void AiSettingsDialog::openMemory() {
    if (panel_.uiHost().openMemory) panel_.uiHost().openMemory();
}

void AiSettingsDialog::save() {
    auto& as = panel_.assistant();
    as.setSettings(draft());
    as.setApiKey(key_->text());    // en memoire ; protegee par Windows si "Memoriser" est coche
    std::string why;
    if (remember_->isChecked() && keyProtectionAvailable() && !key_->text().empty()) {
        if (!as.rememberKey(true, &why) && panel_.uiHost().status) panel_.uiHost().status("Assistant IA : " + why);
    } else if (!remember_->isChecked() || key_->text().empty()) {
        (void)as.rememberKey(false);
    }
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
        for (ui::DropDown* d : {provider_, effort_, mode_, days_, count_, service_, tool_, period_})
            if (d && d->isOpen()) listOpen = true;
        if (modelsMenu_ && modelsMenu_->isOpen()) {
            modelsMenu_->close();
            return ui::EventResult::Consumed;
        }
        if (!listOpen) {
            cancel();
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app::ai
