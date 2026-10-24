// =============================================================================
//  web/WebRuntimeSupport.cpp - 1.12.31 : les appuis manquants de la chaine wasm
// -----------------------------------------------------------------------------
//  La libc++ wasm32-wasi d'ici est construite SANS exceptions : son operator new
//  (et le code qu'on compile, lui, avec les exceptions pour que try/catch passe)
//  cite __cxa_allocate_exception / __cxa_throw sans les definir. Sur la page, une
//  exception ne se propage pas (pas de derouleur) : on la transforme en arret net
//  (abort) - un projet bien forme embarque dans la page n'en leve aucune (les
//  try/catch du moteur gardent des entrees deja validees).
//
//  On donne aussi le petit sous-ensemble de std::filesystem que le moteur appelle
//  (il n'y a pas de disque : tout echoue proprement), et les quelques symboles que
//  WASI ne fournit pas.
//
//  Compile seulement pour la page (XPG_WEB).
// =============================================================================
#ifdef XPG_WEB

#include <cstdint>
#include <cstdlib>
#include <cstring>

// ---- exceptions : tout arret ------------------------------------------------
extern "C" {
void* __cxa_allocate_exception(std::size_t) { std::abort(); }
void  __cxa_free_exception(void*) {}
void  __cxa_throw(void*, void*, void (*)(void*)) { std::abort(); }
void  __cxa_rethrow() { std::abort(); }
void* __cxa_begin_catch(void*) { std::abort(); }
void  __cxa_end_catch() {}
void  __cxa_call_unexpected(void*) { std::abort(); }
void  __cxa_call_terminate(void*) { std::abort(); }
void  _Unwind_Resume(void*) { std::abort(); }
int   __gxx_personality_v0(int, int, std::uint64_t, void*, void*) { std::abort(); }

// Les destructeurs des variables thread_local : la page n'a qu'un fil et ne se
// termine pas (le module vit tant que la page est ouverte) - rien a enregistrer.
int __cxa_thread_atexit(void (*)(void*), void*, void*) { return 0; }
}

#endif // XPG_WEB
