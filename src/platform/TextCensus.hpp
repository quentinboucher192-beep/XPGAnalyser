// =============================================================================
//  platform/TextCensus.hpp - 1.12.14 : le releve des textes de l'image
// -----------------------------------------------------------------------------
//  Quand XPG_TEXT_CENSUS est pose (le chemin d'un fichier), chaque texte que le
//  rendu dessine pendant l'image courante est garde, mot pour mot ; la commande
//  capture des sessions l'ecrit dans ce fichier a cote de l'image. Ce que
//  l'ecran dit se relit alors sans OCR : une session de XPGAnalyser IHM qui passe
//  partout dit si un mot de l'automate traine encore quelque part.
//  Sans la variable : un booleen lu, rien d'autre.
// =============================================================================
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace gfx::census {

struct State {
    bool                     on{false};
    std::string              path;      // le fichier du releve
    std::vector<std::string> texts;     // l'image courante, dans l'ordre du dessin
};

inline State& state() {
    static State s = [] {
        State st;
        if (const char* p = std::getenv("XPG_TEXT_CENSUS"); p && *p) {
            st.on = true;
            st.path = p;
        }
        return st;
    }();
    return s;
}

[[nodiscard]] inline bool enabled() { return state().on; }

// Une image commence : le releve de la precedente s'efface.
inline void newFrame() {
    if (auto& s = state(); s.on) s.texts.clear();
}

inline void record(std::string_view utf8) {
    if (auto& s = state(); s.on && !utf8.empty() && s.texts.size() < 20000) s.texts.emplace_back(utf8);
}

[[nodiscard]] inline const std::vector<std::string>& frame() { return state().texts; }

} // namespace gfx::census
