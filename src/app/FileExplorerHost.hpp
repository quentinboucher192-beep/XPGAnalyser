// =============================================================================
//  app/FileExplorerHost.hpp - lot API 8 : l'explorateur de fichiers de l'appli,
//  branche derriere ui::pickFile
// -----------------------------------------------------------------------------
//  App::services.pickFile demande ici s'il faut ouvrir l'explorateur de
//  l'appli (ui/widgets/FileExplorer) ou celui du systeme (SDL, comme avant) :
//
//    - le reglage "explorateur.systeme" (faux par defaut : celui de l'appli) ;
//    - une session rejouee (--script) garde celui du systeme - ses reponses
//      sont rangees AVANT (explorateur "chemin", ui::queueFilePick : rien ne
//      s'ouvre), les anciennes sessions ne changent pas - sauf apres
//      `explorateur-montrer`, qui fait ouvrir celui de l'appli a la demande
//      suivante (pour une capture).
//
//  L'explorateur s'ouvre dans la fenetre en cours (MenuManager::ShowDialog :
//  la fenetre detachee d'ou vient la demande) ; la reponse est la meme que
//  celle du systeme : done(chemin), done("") si l'on annule.
//
//  LA MEMOIRE (recents, epingles, vues) vit dans les reglages, sous
//  "explorateur.memoire" (ui::files::Memory::save), ecrite a chaque choix.
// =============================================================================
#pragma once

#include "../ui/Widget.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ui {
    class FileExplorerDialog;
    namespace files { class Memory; }
}

namespace app {

class App;
class Settings;

namespace explorer {

    // Le reglage : vrai, celui du systeme ; faux (le defaut), celui de l'appli.
    [[nodiscard]] bool systemPreferred(const Settings& s);
    void setSystemPreferred(Settings& s, bool on);

    // explorateur-montrer : la prochaine demande ouvre celui de l'appli, meme
    // dans une session rejouee.
    void showNextInScript();
    // Celui de l'appli pour cette demande ? (consomme explorateur-montrer)
    [[nodiscard]] bool wantsAppExplorer(App& app);
    // L'ouvrir dans la fenetre en cours. Faux : pas de pile de menus.
    bool open(App& app, const ui::FilePick& pick, std::function<void(std::string)> done);
    // L'explorateur ouvert (le haut de la pile), ou nullptr.
    [[nodiscard]] ui::FileExplorerDialog* current(App& app);
    // La memoire, relue des reglages a la premiere demande.
    [[nodiscard]] std::shared_ptr<ui::files::Memory> memory(App& app);

} // namespace explorer
} // namespace app
