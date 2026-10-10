// =============================================================================
//  platform/InputEvent.hpp - SDL3 events translated into UI-layer events
// -----------------------------------------------------------------------------
//  The widget tree never sees an SDL_Event. Translation happens once, in
//  SdlEventPump, which means the whole UI layer is testable without SDL and
//  could be retargeted (offscreen renderer, remote UI) without touching a widget.
// =============================================================================
#pragma once

#include "Geometry.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace ui {

enum class Key : std::uint16_t {
    Unknown = 0, Escape, Return, Tab, Backspace, Delete, Insert,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    Space, A, C, F, N, O, P, S, V, X, Y, Z,
    F1, F2, F3, F5, F11, F12,
    Q,                          // lot 14 : Ctrl+Alt+Q, la sortie du poste d'exploitation
    H, K,                       // lot 19 : Ctrl+H l'historique, Ctrl+K (lot 20) Aller a
    // Lot API 7 : Ctrl+W ferme l'onglet ouvert ; F9 l'onglet API > Simulation ;
    // Ctrl+1 API > Variables, Ctrl+5 API > Statistiques (ecrits dans les menus
    // depuis le debut, jamais relies a une touche).
    W, F9, Num1, Num5,
    // ---- Lot API 8 : F10, "Section suivante" dans Simulation > Debogage ----
    F10,
    // ---- Lot API 8 : l'explorateur de fichiers - Ctrl+L, le champ du chemin ----
    L,
    // ---- 1.10 (chantier N) : F7, Compiler l'IHM ----
    F7,
    F8,                         // 1.10 (integration) : F8 l'IHM demarrer / arreter, Maj+F8 (branchee par L)
    D,                          // 1.10.2 (chantier D) : Ctrl+D Dupliquer..., Ctrl+Maj+D Dupliquer tel quel
    J,                          // 1.11.14 : Ctrl+J le panneau du bas (Sorties, Console, Diagnostics)
    // 1.11.23 : les raccourcis des vues (toutes les lettres, tous les chiffres, F4 et F6) -
    // ajoutes a la fin : les valeurs d'avant ne bougent pas.
    B, E, G, I, M, R, T, U, Num0, Num2, Num3, Num4, Num6, Num7, Num8, Num9, F4, F6,
    // 1.12.2 : les signes des raccourcis de Visual Studio dans les editeurs de code -
    // Ctrl+/ (Ctrl+: sur un clavier AZERTY), Ctrl+] (Ctrl+$), Ctrl+. , Ctrl+, , Ctrl+-.
    Slash, Period, Comma, Minus, RightBracket, Colon, Dollar, Semicolon,
};

struct KeyMods {
    bool ctrl{}, shift{}, alt{}, super{};
    [[nodiscard]] bool none() const noexcept { return !ctrl && !shift && !alt && !super; }
};

enum class MouseButton : std::uint8_t { Left, Right, Middle, X1, X2 };

struct MouseMove   { gfx::Point pos; gfx::Point delta; KeyMods mods; };
struct MouseDown   { gfx::Point pos; MouseButton button; int clickCount; KeyMods mods; };
struct MouseUp     { gfx::Point pos; MouseButton button; KeyMods mods; };
struct MouseWheel  { gfx::Point pos; float dx, dy; KeyMods mods; };
struct KeyDown     { Key key; KeyMods mods; bool repeat; };
struct KeyUp       { Key key; KeyMods mods; };
struct TextInput   { std::string utf8; };
struct FocusChange { bool gained; };
struct WindowResize{ gfx::Size size; float dpiScale; };
struct FileDropped { std::string path; gfx::Point pos; };

using InputEvent = std::variant<MouseMove, MouseDown, MouseUp, MouseWheel,
                                KeyDown, KeyUp, TextInput, FocusChange,
                                WindowResize, FileDropped>;

// Returned by HandleEvent to drive propagation (see docs/ARCHITECTURE.md).
enum class EventResult : std::uint8_t {
    Ignored,   // keep bubbling to the parent / next menu on the stack
    Consumed,  // stop here
};

} // namespace ui
