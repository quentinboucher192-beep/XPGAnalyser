// =============================================================================
//  app/screens/IconWorkspace.cpp - l'icone du projet (lot API 6)
// -----------------------------------------------------------------------------
//  Projet > Icone du projet... : l'editeur (PixelIconEditor), puis une commande
//  - Ctrl+Z la retire. La barre du haut, la fenetre et la liste de l'accueil
//  suivent le modele (refreshTopBar, App::refreshWindowIcon, config/icone.txt).
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../PixelIconEditor.hpp"
#include "../../project/ApiCommands.hpp"
#include "../../project/ProjectIcon.hpp"

#include <filesystem>

namespace app {

using ui::StatusBar;

void MainAnalysisScreen::showIconEditor() {
    auto doc = app_.document();
    if (!doc) {
        if (status_) status_->setTransientMessage("Ic\xC3\xB4ne du projet : ouvre d'abord un projet.", 6.0, StatusBar::Severity::Warning);
        return;
    }
    std::string name = app_.projectFolder().empty() ? doc->header.projectName
                                                    : std::filesystem::path(app_.projectFolder()).filename().string();
    app_.menus().ShowDialog(std::make_unique<PixelIconEditor>(doc->icon, name), [this](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        domain::ProjectIcon icon;
        if (!r.payload.empty()) {
            auto parsed = project::icon::fromText(r.payload);
            if (!parsed) {
                if (status_) status_->setTransientMessage("Ic\xC3\xB4ne illisible : " + parsed.error().message(), 8.0, StatusBar::Severity::Warning);
                return;
            }
            icon = std::move(*parsed);
        }
        auto doc2 = app_.document();
        if (!doc2 || doc2->icon == icon) return;
        const bool none = icon.empty();
        app_.apply(std::make_unique<project::SetProjectIconCommand>(doc2, std::move(icon)), /*refreshViews=*/false);
        refreshTopBar();
        if (status_)
            status_->setTransientMessage(none ? "Ic\xC3\xB4ne retir\xC3\xA9" "e : le logo revient. Ctrl+Z la remet."
                                              : "Ic\xC3\xB4ne du projet chang\xC3\xA9" "e : la barre du haut, la fen\xC3\xAAtre, l'accueil. Ctrl+Z la retire ; "
                                                "elle s'enregistre avec le projet (config/icone.txt).",
                                         10.0, StatusBar::Severity::Success);
    });
}

} // namespace app
