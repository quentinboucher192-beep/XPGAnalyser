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
}

} // namespace app
