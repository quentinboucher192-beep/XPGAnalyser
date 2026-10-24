// =============================================================================
//  platform/SdlEventPump.cpp - SDL_Event -> ui::InputEvent
// -----------------------------------------------------------------------------
//  The single translation point. Above this file nothing knows SDL exists,
//  which is what makes the widget and menu layers testable without a window.
// =============================================================================
#include "InputEvent.hpp"
#include "KeyTranslate.hpp"   // 1.12.31 : la correspondance des touches (et celle de la disposition)

#include <SDL3/SDL.h>

#include <optional>

namespace app {

namespace {

// 1.12.31 : modsFrom et keyFrom sont dans KeyTranslate.hpp (zoom_test les verifie sans SDL).
using sdlkeys::keyFrom;
using sdlkeys::modsFrom;

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
            // 1.12.31 : aussi la touche de la disposition, sans Maj et sans les options de
            // SDL (la touche 6/- d'un AZERTY : SDLK_MINUS, la ou e.key.key dit SDLK_6).
            return sdlkeys::keyDownFrom(e.key.scancode, e.key.key, e.key.mod, e.key.repeat,
                                        [](SDL_Scancode sc) { return SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false); });

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

        // 1.12.27 : du texte lache (les objets glisses depuis une autre fenetre : leur liste).
        case SDL_EVENT_DROP_TEXT:
            return ui::TextDropped{e.drop.data ? std::string(e.drop.data) : std::string{},
                                   {e.drop.x, e.drop.y}};

        default:
            return std::nullopt;
    }
}

} // namespace app
