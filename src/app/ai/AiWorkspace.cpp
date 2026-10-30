// =============================================================================
//  app/ai/AiWorkspace.cpp - 1.14.2 : l'Assistant IA dans l'ecran du projet
// -----------------------------------------------------------------------------
//  LE VOLET (app/ai/AiPanel) NE FAIT AUCUN GESTE SUR L'APPLICATION : ses hotes
//  sont ici. Il est le quatrieme volet du separateur analysis.upper (IHM Studio),
//  replie au depart ; sa case "Assistant IA (Ctrl+Maj+I)" d'Affichage > Panneaux
//  le montre et le retient, comme les autres panneaux (la Disposition aussi :
//  sa ligne "assistant" de La fenetre).
//
//  Ce que la seance (ai::Assistant) demande a l'application :
//    - le document IHM (App::hmi), la vue de l'onglet ouvert (son HmiEditor) et
//      sa selection - jamais un widget de l'editeur pour le modifier ;
//    - empiler une commande : App::apply, comme l'editeur (un pas d'annulation) ;
//    - les noms de l'automate relie (les globales) : une variable de l'automate
//      existe aussi ;
//    - les fenetres (MenuManager::ShowDialog), la barre d'etat, la commande
//      ia.memoire (le centre de memoire, s'il est la).
// =============================================================================
#include "AiPanel.hpp"

#include "../App.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../screens/Screens.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/widgets/TabArea.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>

namespace app {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// 1.14.2 : LA LARGEUR PAR DEFAUT DU VOLET (les captures a 1280 x 800 : l'inspecteur de l'editeur
// ne montrait plus que des "..."). Le separateur range ses volets en parts : la part du volet est
// calculee pour ai::defaultPanelWidth(fenetre), les autres volets montres gardant les leurs. Tant
// que la part est celle d'origine (0.26, MainAnalysisScreen.cpp) ou celle posee ici, elle suit la
// fenetre ; une autre est une largeur tiree par l'utilisateur : elle reste (et se retient, comme avant).
void fitAssistantWidth(ui::Splitter& split, ai::AiPanel& panel) {
    auto r = split.ratios();
    if (r.size() != 4 || split.children().size() < 4) return;
    float& mine = r[3];
    const bool untouched = std::abs(mine - 0.26f) < 1e-4f || (panel.autoRatio() > 0.f && std::abs(mine - panel.autoRatio()) < 1e-4f);
    if (!untouched) return;
    const float total = split.bounds().w - 15.f;   // trois poignees de 5 px
    if (total < 600.f) return;
    const auto surface = ui::surfaceSize();
    const float want = ai::defaultPanelWidth(surface.w > 0.f ? surface.w : split.bounds().w);
    float others = 0.f;
    for (std::size_t i = 0; i < 3; ++i)
        if (split.children()[i]->visibility() != ui::Visibility::Collapsed) others += r[i];
    const float ratio = want * others / std::max(1.f, total - want);
    if (std::abs(ratio - mine) < 1e-4f) return;
    mine = ratio;
    panel.setAutoRatio(ratio);
    split.setRatios(r);
}

} // namespace

ui::WidgetPtr MainAnalysisScreen::buildAssistantPanel() {
    ai::Assistant::Host core;
    core.document = [this] { return app_.hmi(); };
    // La vue de l'onglet ouvert : celle que l'Assistant modifie.
    const auto editor = [this]() -> HmiEditor* {
        if (!centre_ || centre_->tabCount() == 0) return nullptr;
        return dynamic_cast<HmiEditor*>(centre_->page(centre_->currentIndex()));
    };
    core.activeView = [editor] {
        auto* ed = editor();
        return ed ? ed->viewId() : hmi::kNoId;
    };
    core.selection = [editor] {
        auto* ed = editor();
        return ed ? ed->canvas().selection() : std::vector<hmi::Id>{};
    };
    core.apply = [this](core::CommandPtr c) {
        if (c) app_.apply(std::move(c), /*refreshViews=*/false);
    };
    core.projectName = [this] {
        const auto u = std::filesystem::path(std::u8string(app_.projectFolder().begin(), app_.projectFolder().end())).filename().u8string();
        return std::string(u.begin(), u.end());
    };
    core.projectPath = [this] { return app_.projectFolder(); };
    // Les globales de l'automate relie : une variable de l'automate existe aussi (sans casse).
    core.env.plcName = [this](std::string_view root) {
        const auto plc = app_.project();
        if (!plc) return false;
        const std::string want = upper(root);
        for (const auto& v : plc->variables)
            if (v.scope == domain::VariableScope::Global && upper(plc->strings.text(v.name)) == want) return true;
        return false;
    };

    ai::AiPanel::UiHost ui;
    ui.showDialog = [this](menu::MenuPtr dialog, std::function<void(const menu::DialogResult&)> onClose) {
        app_.menus().ShowDialog(std::move(dialog), std::move(onClose));
    };
    ui.status = [this](const std::string& text) {
        if (status_) status_->setTransientMessage(text, 6.0);
    };
    ui.close = [this] { showAssistant(false); };
    ui.memoryAvailable = [this] { return app_.actions().find("ia.memoire") != nullptr; };
    ui.openMemory = [this] { (void)app_.actions().trigger("ia.memoire", app_.commands()); };

    auto panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
    aiPanel_ = panel.get();
    return panel;
}

bool MainAnalysisScreen::assistantShown() const {
    for (const auto& p : panels_)
        if (std::string_view(p.key) == "view.assistant") return p.box->isChecked();
    return false;
}

void MainAnalysisScreen::showAssistant(bool show) {
    if (!aiPanel_) {
        if (status_) status_->setTransientMessage("L'Assistant IA est dans IHM Studio (l'\xC3\xA9" "diteur de vues).", 5.0);
        return;
    }
    for (const auto& p : panels_)
        if (std::string_view(p.key) == "view.assistant" && p.box->isChecked() != show)
            p.box->setState(show ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);   // replie et retient
    if (show) {
        aiPanel_->ensureLoaded();
        aiPanel_->prompt().takeFocus();
    }
    if (status_)
        status_->setTransientMessage(show ? "Assistant IA (d\xC3\xA9monstration) : il propose des modifications de la vue ouverte. Ctrl+Maj+I le replie."
                                          : "Assistant IA repli\xC3\xA9 (Ctrl+Maj+I le rouvre).", 4.0);
}

void MainAnalysisScreen::toggleAssistant() { showAssistant(!assistantShown()); }

void MainAnalysisScreen::tickAssistant(double now) {
    if (!aiPanel_) return;
    // Montre au demarrage (la case retenue, la Disposition) : charge a la premiere image.
    if (assistantShown()) aiPanel_->ensureLoaded();
    if (aiPanel_->loaded()) aiPanel_->tick(now);
    if (upper_ && assistantShown()) fitAssistantWidth(*upper_, *aiPanel_);
}

} // namespace app
