// =============================================================================
//  app/screens/TrashWorkspace.cpp - 1.25.1 : la Corbeille, cote ecran du projet
// -----------------------------------------------------------------------------
//  Le client (05/11) : << fait une corbeille bien rangee avec recherche possible >>.
//  L'ecran inscrit les corbeilles (app/Corbeille.hpp) : celle du projet (ses
//  enregistrements, ses incidents, ses versions), celle de l'Assistant IA, celle
//  des macros ; il ouvre le volet (le noeud << Corbeille [N] >> de l'arbre,
//  Projet > Corbeille...) et tient le compte de l'arbre a jour. A l'ouverture d'un
//  projet, sa corbeille se purge (plus de 7 jours).
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../Corbeille.hpp"
#include "../ai/AiPanel.hpp"
#include "../ai/AiStore.hpp"
#include "../hmi/HmiAssetPanes.hpp"
#include "../hmi/HmiCorbeillePane.hpp"
#include "../../hmi/HmiRecording.hpp"
#include "../../project/SharedLibrary.hpp"

namespace app {

void MainAnalysisScreen::registerTrashSources() {
    const std::weak_ptr<bool> alive = hookGuard_;
    const std::string folder = app_.projectFolder();
    if (!folder.empty()) {
        auto src = corbeille::projectSource(folder, [this, alive, folder]() -> std::string {
            if (alive.expired()) return {};
            auto doc = app_.hmi();
            return doc ? hmi::rec::folderOf(doc->project.recording, folder) : std::string{};
        });
        src.changed = [this, alive] {   // les enregistrements, les incidents, les versions se relisent
            if (alive.expired()) return;
            if (auto* rp = dynamic_cast<HmiRecordingsPane*>(hmiTab("enregistrements"))) rp->refresh();
            app_.incidents().refresh();
            refreshVersions();
        };
        corbeille::registerSource("projet", std::move(src));
        // A l'ouverture d'un projet : ce qui est dans sa corbeille depuis plus de 7 jours part pour de bon.
        if (trashFolder_ != folder) (void)corbeille::purgeOld("projet");
    } else {
        corbeille::unregisterSource("projet");
    }
    trashFolder_ = folder;   // Update la reinscrit quand le projet change (un autre ouvert, Enregistrer sous)
    auto ai = corbeille::aiSource();
    ai.changed = [this, alive] {   // une conversation rendue revient dans la liste du panneau
        if (alive.expired() || !aiPanel_) return;
        for (auto& c : ai::loadConversations()) (void)aiPanel_->assistant().restoreConversation(std::move(c));
        aiPanel_->chat().rebuild();
        aiPanel_->invalidateLayout();
    };
    corbeille::registerSource("ia", std::move(ai));
    auto macros = corbeille::macroSource(project::SharedLibrary::defaultRoot());
    macros.changed = [this, alive] {
        if (!alive.expired()) macrosChanged();
    };
    corbeille::registerSource("macros", std::move(macros));
}

void MainAnalysisScreen::openTrash() {
    registerTrashSources();
    openHmiPane("corbeille");
    if (auto* pane = dynamic_cast<HmiCorbeillePane*>(hmiTab("corbeille"))) pane->refresh();
}

} // namespace app
