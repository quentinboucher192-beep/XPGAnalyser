// =============================================================================
//  app/SessionDialogs.hpp - 1.43.0 : les fenetres des sessions
// -----------------------------------------------------------------------------
//  Projet > Demarrer une session... (le nom, qui peut entrer, ce qu'ils font en
//  entrant) ; Projet > Rejoindre une session... (celles du reseau du bureau, ou une
//  adresse ; le code si elle le demande) ; la fenetre de la session (la pastille du
//  haut) : l'equipe (ou chacun est, Suivre, son role), ceux qui veulent entrer, le
//  code, la discussion, Arreter / Quitter.
//  1.44.0 : par Internet aussi (le relais du service XPG) - Demarrer : Ou (le reseau
//  du bureau, ou Internet : le code de 10 caracteres montre, Copier) ; Rejoindre :
//  << Sur le reseau du bureau >> ou << Par Internet >> (le code seul), depuis
//  l'accueil aussi (aucun projet ouvert).
// =============================================================================
#pragma once

namespace app {

class App;

void showSessionStart(App&);
void showSessionJoin(App&);
void showSessionPanel(App&);

} // namespace app
