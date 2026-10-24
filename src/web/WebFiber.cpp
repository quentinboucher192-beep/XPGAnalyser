// =============================================================================
//  web/WebFiber.cpp - 1.12.31 : le pont Asyncify (voir WebFiber.hpp)
// -----------------------------------------------------------------------------
//  UN SCRIPT DETACHE, UNE COROUTINE, SUR UN SEUL FIL. Dans l'application, un
//  script detache tourne sur son propre fil et rend la main a l'IHM sur un WAIT.
//  La page n'a qu'un fil : Asyncify (wasm-opt --asyncify) sait SUSPENDRE la pile
//  C++ d'un appel et la REPRENDRE plus tard. On s'en sert comme d'une coroutine :
//
//    - chaque tache a SA pile C (la memoire des variables locales dont on prend
//      l'adresse) et SON tampon Asyncify (les variables des fonctions suspendues) ;
//    - xpg_fiber_run() est la FRONTIERE : elle n'est PAS instrumentee
//      (asyncify-removelist). Quand le script suspend (WAIT, sa tranche finie), la
//      pile se deroule jusqu'a elle, et elle seule : elle rend la main au moteur de
//      l'IHM, qui continue son cycle. Le moteur la rappelle plus tard pour reprendre
//      (rewind) : le script repart exactement apres son WAIT ;
//    - le point de suspension est un import JS (env.xpg_host_sleep, dans
//      asyncify-imports) : il declenche le deroulement, ou l'arrete a la reprise.
//
//  Une seule tache tourne a la fois (comme dans l'application : le moteur passe
//  la main et attend qu'elle la rende) : il n'y a rien a proteger.
// =============================================================================
#ifdef XPG_WEB

#include "WebFiber.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <vector>

// ---- les imports donnes par l'hote JS (web_host.js, essai_node.mjs) ---------
extern "C" {
// Le point de suspension (asyncify-imports) : normal -> commence le deroulement ;
// pendant une reprise -> l'arrete, et l'execution continue apres l'appel.
__attribute__((import_module("env"), import_name("xpg_host_sleep"))) void xpg_host_sleep(void* asyncData);
__attribute__((import_module("env"), import_name("xpg_fiber_stop_unwind"))) void xpg_fiber_stop_unwind();
__attribute__((import_module("env"), import_name("xpg_fiber_start_rewind"))) void xpg_fiber_start_rewind(void* asyncData);
__attribute__((import_module("env"), import_name("xpg_fiber_state"))) int xpg_fiber_state();      // 0 normal, 1 deroule, 2 reprise
__attribute__((import_module("env"), import_name("xpg_get_sp"))) std::uint32_t xpg_get_sp();
__attribute__((import_module("env"), import_name("xpg_set_sp"))) void xpg_set_sp(std::uint32_t sp);
}

namespace app::web {

namespace {

constexpr std::size_t kStackBytes = 1u << 20;    // la pile C d'une tache : 1 Mo
constexpr std::size_t kAsyncBytes = 1u << 20;    // son tampon Asyncify : 1 Mo

struct Rec {
    std::function<void()>     body;
    std::unique_ptr<std::uint8_t[]> stack;        // la pile C (croit vers le bas)
    std::unique_ptr<std::uint8_t[]> async;        // { pos, end, donnees... }
    std::uint32_t             savedSp{0};         // la pile au moment de la suspension
    bool                      suspended{false};
    bool                      finished{false};
    bool                      cancelled{false};
};

std::map<void*, std::unique_ptr<Rec>>& recs() {
    static std::map<void*, std::unique_ptr<Rec>> r;
    return r;
}
Rec* g_current = nullptr;

std::uint32_t stackTop(Rec& r) {
    auto top = reinterpret_cast<std::uintptr_t>(r.stack.get()) + kStackBytes;
    return static_cast<std::uint32_t>(top & ~std::uintptr_t{15});   // aligne sur 16
}

void initAsync(Rec& r) {
    auto* base = r.async.get();
    const auto start = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base) + 8);
    const auto end = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base) + kAsyncBytes);
    std::memcpy(base, &start, 4);
    std::memcpy(base + 4, &end, 4);
}

} // namespace

} // namespace app::web

// ---- la pile de la tache commence ICI (instrumentee) ------------------------
extern "C" __attribute__((noinline)) void xpg_fiber_trampoline(app::web::Rec* r) { r->body(); }

// ---- LA FRONTIERE : non instrumentee (asyncify-removelist) -------------------
//  Lance (rewind = 0) ou reprend (rewind = 1) la tache sur sa propre pile. Rend 1
//  si elle s'est suspendue (a reprendre), 0 si elle a fini.
extern "C" __attribute__((noinline)) int xpg_fiber_run(app::web::Rec* r, int rewind) {
    using namespace app::web;
    const std::uint32_t mainSp = xpg_get_sp();
    Rec* outer = g_current;
    g_current = r;
    if (rewind) {
        xpg_set_sp(r->savedSp);
        xpg_fiber_start_rewind(r->async.get());
    } else {
        xpg_set_sp(stackTop(*r));
    }
    xpg_fiber_trampoline(r);
    int suspended = 0;
    if (xpg_fiber_state() == 1) {        // deroulee jusqu'ici : elle a suspendu
        xpg_fiber_stop_unwind();
        suspended = 1;
    }
    xpg_set_sp(mainSp);
    g_current = outer;
    r->suspended = suspended != 0;
    if (!suspended) r->finished = true;
    return suspended;
}

namespace app::web {

bool Fiber::available() { return true; }

bool Fiber::start(void* key, std::function<void()> body) {
    auto rec = std::make_unique<Rec>();
    rec->body = std::move(body);
    rec->stack.reset(new (std::nothrow) std::uint8_t[kStackBytes]);
    rec->async.reset(new (std::nothrow) std::uint8_t[kAsyncBytes]);
    if (!rec->stack || !rec->async) return false;    // pas de memoire : la tache ne part pas
    initAsync(*rec);
    Rec* r = rec.get();
    recs()[key] = std::move(rec);
    const bool suspended = xpg_fiber_run(r, 0) != 0;
    if (!suspended) recs().erase(key);
    return suspended;
}

bool Fiber::resume(void* key) {
    const auto it = recs().find(key);
    if (it == recs().end()) return false;
    Rec* r = it->second.get();
    if (r->finished) { recs().erase(it); return false; }
    const bool suspended = xpg_fiber_run(r, 1) != 0;
    if (!suspended) recs().erase(key);
    return suspended;
}

// Dans le corps de la tache : suspendre (le WAIT, la tranche finie).
bool Fiber::yield(void* key) {
    Rec* r = g_current;
    if (!r) return false;
    (void)key;
    if (r->cancelled) return false;
    r->savedSp = xpg_get_sp();
    xpg_host_sleep(r->async.get());      // 1er passage : derouler ; a la reprise : on revient ici
    return !r->cancelled;
}

void Fiber::cancelCurrentIfIs(void* key) {
    const auto it = recs().find(key);
    if (it == recs().end()) return;
    it->second->cancelled = true;
    // Deroulee jusqu'a sa fin : chaque yield rend faux tout de suite (pas de suspension).
    while (resume(key)) {}
}

} // namespace app::web

#endif // XPG_WEB
