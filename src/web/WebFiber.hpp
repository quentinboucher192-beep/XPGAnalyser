// =============================================================================
//  web/WebFiber.hpp - 1.12.31 : suspendre/reprendre une pile (Asyncify)
// -----------------------------------------------------------------------------
//  Un script detache de l'IHM tourne comme une coroutine : il rend la main sur un
//  WAIT et la reprend plus tard. Sur un seul fil (la page web), c'est Asyncify qui
//  suspend et reprend la pile C++. Ce petit pont cache le mecanisme au reste du
//  moteur (hmi::ScriptTask s'en sert).
//
//  Tant qu'Asyncify n'est pas pose (construction sans wasm-opt), start() rend faux
//  et le script detache ne tourne pas - au lieu de bloquer.
// =============================================================================
#pragma once
#ifdef XPG_WEB

#include <functional>

namespace app::web {

struct Fiber {
    // Lance `body` pour la tache `key`. Rend vrai si elle s'est SUSPENDUE (un WAIT,
    // sa tranche finie) et attend resume() ; faux si elle a fini (ou n'a pas pu partir).
    static bool start(void* key, std::function<void()> body);
    // Reprend la tache suspendue `key`. Vrai : suspendue de nouveau ; faux : finie.
    static bool resume(void* key);
    // Depuis le corps de la tache : rend la main. Vrai au retour (reprise) ; faux si annulee.
    static bool yield(void* key);
    // A la destruction : deroule la tache `key` jusqu'a sa fin si c'est elle qui tourne.
    static void cancelCurrentIfIs(void* key);
    // Asyncify est-il reellement disponible dans ce module ?
    static bool available();
};

} // namespace app::web

#endif // XPG_WEB
