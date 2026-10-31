// =============================================================================
//  app/FileViewerWindow.cpp - 1.21.0 : la fenetre d'apercu (un Explorateur de fichiers
//  de l'IHM trop etroit pour montrer l'apercu en lui). Voir FileViewer.hpp.
// -----------------------------------------------------------------------------
//  A part de FileViewer.cpp : les fenetres detachees (SDL) ne sont que dans
//  l'appli ; les essais de l'editeur (hmi_editor_test) lient l'apercu sans elles.
// =============================================================================
#include "FileViewer.hpp"

#include "DetachedWindows.hpp"

#include <memory>
#include <string>

namespace app {

namespace {
FileViewPane* g_previewWindow = nullptr;   // la fenetre d'apercu ouverte (nulle : fermee)
} // namespace

bool showPreviewWindow(DetachedWindows& windows, const fileview::Source& source, bool allowOpen) {
    const std::string title = "Aper\xC3\xA7u : " + (source.name.empty() ? source.path : source.name);
    if (g_previewWindow && windows.isDetached(g_previewWindow)) {
        g_previewWindow->viewer().setActions(allowOpen, allowOpen);
        g_previewWindow->show(source);
        windows.setTitle(g_previewWindow, title);
        windows.raise(g_previewWindow);
        return true;
    }
    auto pane = std::make_unique<FileViewPane>("apercu.fenetre");
    FileViewPane* raw = pane.get();
    raw->viewer().setActions(allowOpen, allowOpen);
    raw->show(source);
    ui::WidgetPtr page = std::move(pane);
    // La croix : la page revient ici, et part (son apercu, ses textures) ; le choix suivant rouvre une fenetre.
    const bool opened = windows.detach(title, page, [](ui::WidgetPtr back) {
        if (back.get() == g_previewWindow) g_previewWindow = nullptr;
    }, 760, 600);
    if (opened) g_previewWindow = raw;
    return opened;
}

} // namespace app
