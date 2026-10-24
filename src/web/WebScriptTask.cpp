// =============================================================================
//  web/WebScriptTask.cpp - 1.12.31 : un script detache, cote page web
// -----------------------------------------------------------------------------
//  Dans l'application, un script DETACHE (qui peut faire WAIT sans bloquer l'IHM)
//  tourne sur son propre fil, comme une coroutine : l'IHM lui passe la main
//  (resume) et attend qu'il la rende (yield : un WAIT, sa tranche finie). La page
//  web n'a qu'un fil : ce fil-la n'existe pas.
//
//  LE MECANISME FIDELE EST ASYNCIFY (wasm-opt --asyncify) : il suspend et reprend
//  la pile C++ du module. Le pont est dans src/web/WebFiber (l'unwind/rewind) ; ce
//  ScriptTask s'y branche. Tant qu'Asyncify n'est pas pose (wasm-opt absent a la
//  construction), un script detache NE TOURNE PAS (il se termine tout de suite,
//  sans rien faire) au lieu de bloquer ou de boucler - l'export de la page le dit.
//
//  Compile seulement pour la page (XPG_WEB) ; l'application garde HmiScriptTask.cpp.
// =============================================================================
#ifdef XPG_WEB

#include "../hmi/HmiScriptTask.hpp"
#include "WebFiber.hpp"

namespace hmi {

ScriptTask::ScriptTask(Body body) : body_(std::move(body)) {}

ScriptTask::~ScriptTask() {
    // Le fibre (s'il tourne) est deroule jusqu'a sa fin a l'annulation.
    if (started_ && !finished_) {
        cancel();
        app::web::Fiber::cancelCurrentIfIs(this);
    }
}

void ScriptTask::resume() {
    if (finished_) return;
    if (!started_) {
        started_ = true;
        // Lance le corps dans un fibre Asyncify. Sans Asyncify, runOnce rend faux
        // tout de suite : le script detache ne tourne pas (dit par l'export).
        finished_ = !app::web::Fiber::start(this, [this] { body_(*this); });
    } else {
        finished_ = !app::web::Fiber::resume(this);
    }
}

bool ScriptTask::yield() {
    if (cancelled_) return false;
    // Rend la main au fil de l'IHM : Asyncify suspend la pile ici.
    return app::web::Fiber::yield(this) && !cancelled_;
}

bool ScriptTask::finished() const { return finished_; }
bool ScriptTask::started() const { return started_; }

} // namespace hmi

#endif // XPG_WEB
