// =============================================================================
//  platform/KeyTranslate.hpp - 1.12.31 : une touche de SDL -> ui::KeyDown
// -----------------------------------------------------------------------------
//  LA CORRESPONDANCE, A PART DE SDL_Event. SdlEventPump.cpp s'en sert ; zoom_test
//  la verifie sans fenetre ni SDL (seuls les en-tetes de third_party/SDL3 :
//  les valeurs des touches, rien a lier).
//
//  POURQUOI DEUX TOUCHES. SDL rend `key` selon son option SDL_HINT_KEYCODE_OPTIONS,
//  dont le defaut contient french_numbers : la rangee des chiffres d'un AZERTY
//  arrive en chiffres (la touche 6/- en SDLK_6, la touche 0/a en SDLK_0) - ce que
//  veulent Ctrl+1, Ctrl+5, Ctrl+0 et les touches des vues. Mais Ctrl + la touche
//  6/- est le Ctrl + moins de l'utilisateur (le zoom, Revenir dans un code) :
//  `layout` est la touche telle que la disposition l'ecrit sans Maj, demandee a
//  SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false) (sans les options) -
//  SDLK_MINUS pour cette touche. ui::commandKey et ui::zoomKeyStep (InputEvent.hpp)
//  choisissent entre les deux.
// =============================================================================
#pragma once

#include "InputEvent.hpp"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

namespace app::sdlkeys {

[[nodiscard]] inline ui::KeyMods modsFrom(SDL_Keymod m) noexcept {
    ui::KeyMods k;
    k.ctrl  = (m & SDL_KMOD_CTRL)  != 0;
    k.shift = (m & SDL_KMOD_SHIFT) != 0;
    k.alt   = (m & SDL_KMOD_ALT)   != 0;
    k.super = (m & SDL_KMOD_GUI)   != 0;
    return k;
}

[[nodiscard]] inline ui::Key keyFrom(SDL_Keycode k) noexcept {
    switch (k) {
        case SDLK_ESCAPE:    return ui::Key::Escape;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  return ui::Key::Return;
        case SDLK_TAB:       return ui::Key::Tab;
        case SDLK_BACKSPACE: return ui::Key::Backspace;
        case SDLK_DELETE:    return ui::Key::Delete;
        case SDLK_INSERT:    return ui::Key::Insert;
        case SDLK_LEFT:      return ui::Key::Left;
        case SDLK_RIGHT:     return ui::Key::Right;
        case SDLK_UP:        return ui::Key::Up;
        case SDLK_DOWN:      return ui::Key::Down;
        case SDLK_HOME:      return ui::Key::Home;
        case SDLK_END:       return ui::Key::End;
        case SDLK_PAGEUP:    return ui::Key::PageUp;
        case SDLK_PAGEDOWN:  return ui::Key::PageDown;
        case SDLK_SPACE:     return ui::Key::Space;
        case SDLK_A:         return ui::Key::A;
        case SDLK_C:         return ui::Key::C;
        case SDLK_F:         return ui::Key::F;
        case SDLK_N:         return ui::Key::N;
        case SDLK_O:         return ui::Key::O;
        case SDLK_P:         return ui::Key::P;
        case SDLK_S:         return ui::Key::S;
        case SDLK_V:         return ui::Key::V;
        case SDLK_X:         return ui::Key::X;
        case SDLK_Y:         return ui::Key::Y;
        case SDLK_Z:         return ui::Key::Z;
        case SDLK_Q:         return ui::Key::Q;
        case SDLK_H:         return ui::Key::H;
        case SDLK_K:         return ui::Key::K;
        case SDLK_D:         return ui::Key::D;          // Ctrl+D Dupliquer, Ctrl+Maj+D Dupliquer... (1.12.18)
        case SDLK_J:         return ui::Key::J;          // 1.11.14 : Ctrl+J le panneau du bas
        case SDLK_W:         return ui::Key::W;          // lot API 7
        case SDLK_F9:        return ui::Key::F9;
        case SDLK_F10:       return ui::Key::F10;        // Lot API 8 : Simulation > Debogage
        case SDLK_F7:        return ui::Key::F7;         // 1.10 (chantier N) : Compiler l'IHM
        case SDLK_F8:        return ui::Key::F8;         // 1.10 : l'IHM demarrer / arreter (chantier L)
        case SDLK_L:         return ui::Key::L;          // ---- Lot API 8 : l'explorateur de fichiers (Ctrl+L, le chemin) ----
        case SDLK_1:         return ui::Key::Num1;
        case SDLK_KP_1:      return ui::Key::Num1;
        case SDLK_5:         return ui::Key::Num5;
        case SDLK_KP_5:      return ui::Key::Num5;
        case SDLK_F1:        return ui::Key::F1;
        case SDLK_F2:        return ui::Key::F2;
        case SDLK_F3:        return ui::Key::F3;
        case SDLK_F5:        return ui::Key::F5;
        case SDLK_F11:       return ui::Key::F11;
        case SDLK_F12:       return ui::Key::F12;
        // 1.11.23 : les raccourcis des vues - les autres lettres, chiffres (rangee et pave), F4, F6.
        case SDLK_B:         return ui::Key::B;
        case SDLK_E:         return ui::Key::E;
        case SDLK_G:         return ui::Key::G;
        case SDLK_I:         return ui::Key::I;
        case SDLK_M:         return ui::Key::M;
        case SDLK_R:         return ui::Key::R;
        case SDLK_T:         return ui::Key::T;
        case SDLK_U:         return ui::Key::U;
        case SDLK_0: case SDLK_KP_0: return ui::Key::Num0;
        case SDLK_2: case SDLK_KP_2: return ui::Key::Num2;
        case SDLK_3: case SDLK_KP_3: return ui::Key::Num3;
        case SDLK_4: case SDLK_KP_4: return ui::Key::Num4;
        case SDLK_6: case SDLK_KP_6: return ui::Key::Num6;
        case SDLK_7: case SDLK_KP_7: return ui::Key::Num7;
        case SDLK_8: case SDLK_KP_8: return ui::Key::Num8;
        case SDLK_9: case SDLK_KP_9: return ui::Key::Num9;
        case SDLK_F4:        return ui::Key::F4;
        case SDLK_F6:        return ui::Key::F6;
        // 1.12.2 : les signes des raccourcis des editeurs (la touche telle que le clavier
        // l'ecrit sans Maj : Ctrl+: et Ctrl+$ sur un AZERTY, Ctrl+/ et Ctrl+] sur un QWERTY).
        case SDLK_SLASH:     case SDLK_KP_DIVIDE: return ui::Key::Slash;
        case SDLK_PERIOD:    return ui::Key::Period;
        case SDLK_COMMA:     return ui::Key::Comma;
        case SDLK_MINUS:     case SDLK_KP_MINUS: return ui::Key::Minus;
        case SDLK_RIGHTBRACKET: return ui::Key::RightBracket;
        case SDLK_COLON:     return ui::Key::Colon;
        case SDLK_DOLLAR:    return ui::Key::Dollar;
        case SDLK_SEMICOLON: return ui::Key::Semicolon;
        case SDLK_APPLICATION: case SDLK_MENU: return ui::Key::Menu;   // 1.12.18
        case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: return ui::Key::Plus;   // 1.12.30 : le zoom
        default:             return ui::Key::Unknown;
    }
}

// Un appui : `key` comme SDL le rend dans l'evenement (ses options comprises),
// `scancode` la touche physique ; `layoutOf(scancode)` rend la touche de la
// disposition sans Maj (SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false)
// dans l'appli ; une disposition simulee dans les essais).
template <class LayoutOf>
[[nodiscard]] ui::KeyDown keyDownFrom(SDL_Scancode scancode, SDL_Keycode key, SDL_Keymod mod, bool repeat, LayoutOf&& layoutOf) {
    ui::KeyDown k{keyFrom(key), modsFrom(mod), repeat};
    k.layout = keyFrom(layoutOf(scancode));
    return k;
}

} // namespace app::sdlkeys
