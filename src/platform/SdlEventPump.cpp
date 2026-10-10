// =============================================================================
//  platform/SdlEventPump.cpp - SDL_Event -> ui::InputEvent
// -----------------------------------------------------------------------------
//  The single translation point. Above this file nothing knows SDL exists,
//  which is what makes the widget and menu layers testable without a window.
// =============================================================================
#include "InputEvent.hpp"

#include <SDL3/SDL.h>

#include <optional>

namespace app {

namespace {

ui::KeyMods modsFrom(SDL_Keymod m) {
    ui::KeyMods k;
    k.ctrl  = (m & SDL_KMOD_CTRL)  != 0;
    k.shift = (m & SDL_KMOD_SHIFT) != 0;
    k.alt   = (m & SDL_KMOD_ALT)   != 0;
    k.super = (m & SDL_KMOD_GUI)   != 0;
    return k;
}

ui::Key keyFrom(SDL_Keycode k) {
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
        case SDLK_D:         return ui::Key::D;          // 1.10.2 (chantier D) : Ctrl+D Dupliquer...
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
        default:             return ui::Key::Unknown;
    }
}

ui::MouseButton buttonFrom(Uint8 b) {
    switch (b) {
        case SDL_BUTTON_RIGHT:  return ui::MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return ui::MouseButton::Middle;
        case SDL_BUTTON_X1:     return ui::MouseButton::X1;
        case SDL_BUTTON_X2:     return ui::MouseButton::X2;
        default:                return ui::MouseButton::Left;
    }
}

} // namespace

std::optional<ui::InputEvent> translate(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            return ui::MouseMove{{e.motion.x, e.motion.y},
                                 {e.motion.xrel, e.motion.yrel},
                                 modsFrom(SDL_GetModState())};

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            return ui::MouseDown{{e.button.x, e.button.y},
                                 buttonFrom(e.button.button),
                                 static_cast<int>(e.button.clicks),
                                 modsFrom(SDL_GetModState())};

        case SDL_EVENT_MOUSE_BUTTON_UP:
            return ui::MouseUp{{e.button.x, e.button.y},
                               buttonFrom(e.button.button),
                               modsFrom(SDL_GetModState())};

        case SDL_EVENT_MOUSE_WHEEL:
            return ui::MouseWheel{{e.wheel.mouse_x, e.wheel.mouse_y},
                                  e.wheel.x, e.wheel.y,
                                  modsFrom(SDL_GetModState())};

        case SDL_EVENT_KEY_DOWN:
            return ui::KeyDown{keyFrom(e.key.key), modsFrom(e.key.mod), e.key.repeat};

        case SDL_EVENT_KEY_UP:
            return ui::KeyUp{keyFrom(e.key.key), modsFrom(e.key.mod)};

        case SDL_EVENT_TEXT_INPUT:
            return ui::TextInput{e.text.text ? std::string(e.text.text) : std::string{}};

        case SDL_EVENT_WINDOW_FOCUS_GAINED: return ui::FocusChange{true};
        case SDL_EVENT_WINDOW_FOCUS_LOST:   return ui::FocusChange{false};

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
            return ui::WindowResize{{static_cast<float>(e.window.data1),
                                     static_cast<float>(e.window.data2)}, 1.f};

        case SDL_EVENT_DROP_FILE:
            // Dropping a .XPG on the window is the same code path as File > Open.
            return ui::FileDropped{e.drop.data ? std::string(e.drop.data) : std::string{},
                                   {e.drop.x, e.drop.y}};

        default:
            return std::nullopt;
    }
}

} // namespace app
