// =============================================================================
//  app/GlisserDehors.hpp - 1.12.27 : GLISSER DU TEXTE HORS DE LA FENETRE
// -----------------------------------------------------------------------------
//  Des objets tires hors de la fenetre d'IHM Studio : leur liste (le texte que
//  Ctrl+C met pour Excel) part avec la souris, en glisser-deposer du systeme.
//  Une autre fenetre d'IHM Studio la recoit (SDL : SDL_EVENT_DROP_TEXT) et y
//  retrouve les objets publies (app/hmi/HmiPartage.hpp) ; Excel ou le Bloc-notes
//  recoivent la liste.
//
//  Sous Windows : OLE (DoDragDrop, CF_UNICODETEXT). Ailleurs : rien.
// =============================================================================
#pragma once

#include <string_view>

namespace app::glisser {

// Le glisser du systeme, jusqu'au lacher (la fonction attend) : 1 le texte est
// depose quelque part, 0 annule (Echap, lache sur rien), -1 pas de glisser ici.
int texteDehors(std::string_view utf8);

} // namespace app::glisser
