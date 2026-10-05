// app/VersionClose.cpp - Terminer et Livrer une version (lot API 6).
#include "VersionClose.hpp"

#include "App.hpp"
#include "../hmi/HmiVersionState.hpp"
#include "../hmi/HmiVersions.hpp"

#include <utility>

namespace app {

core::Result<int> closeVersion(App& app, project::State target, std::string name, std::string comment, const std::string& password) {
    namespace ver = hmi::ver;
    const std::string folder = app.projectFolder();
    if (folder.empty())
        return core::fail(core::ErrorCode::InvalidArgument, "le projet n'a pas encore de dossier : Enregistrer sous\xE2\x80\xA6 d'abord");
    if (target != project::State::Finish && target != project::State::Lock)
        return core::fail(core::ErrorCode::InvalidArgument, "seuls FINISH et LOCK ferment une version");
    if (target == project::State::Lock && password.empty())
        return core::fail(core::ErrorCode::InvalidArgument, "verrouiller demande un mot de passe");
    const auto from = app.manifest().state;
    if (from == project::State::Lock)
        return core::fail(core::ErrorCode::InvalidArgument, "le projet est d\xC3\xA9j\xC3\xA0 verrouill\xC3\xA9 (LOCK)");
    if (target == project::State::Finish && from == project::State::Finish)
        return core::fail(core::ErrorCode::InvalidArgument, "la version est d\xC3\xA9j\xC3\xA0 termin\xC3\xA9" "e (FINISH)");

    // 1. Ce qui est a l'ecran va sur le disque (l'etat ne change pas encore).
    if (app.pendingChanges() > 0)
        if (auto st = app.saveProject(); !st) return core::Err<core::Error>(st.error());

    auto store = ver::open(folder);
    if (!store) return core::Err<core::Error>(store.error());
    const bool changed = ver::changedSinceLast(*store);
    const auto plan = ver::closing(from, target, *store, changed);
    if (name.empty()) {
        const auto* last = store->last();
        name = plan.promote && last && !last->name.empty() ? last->name : ver::defaultName(target);
    }

    int number = plan.number;
    if (plan.promote) {
        const auto* last = store->last();
        if (auto st = ver::update(*store, plan.number, name, ver::State::Delivered, comment.empty() && last ? last->comment : comment); !st)
            return core::Err<core::Error>(st.error());
    } else {
        // La version dit FINISH : on pose l'etat, puis on photographie.
        if (auto st = app.setProjectState(project::State::Finish); !st) return core::Err<core::Error>(st.error());
        auto made = ver::create(*store, name, plan.versionState, comment, ver::defaultAuthor());
        if (!made) return core::Err<core::Error>(made.error());
        number = made->number;
    }
    if (target == project::State::Lock)
        if (auto st = app.setProjectState(project::State::Lock, password); !st) return core::Err<core::Error>(st.error());
    app.events().publish(ProjectSaved{});
    return number;
}

} // namespace app
