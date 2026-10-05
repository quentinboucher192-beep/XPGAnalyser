// =============================================================================
//  core/SingleInstance.hpp - 1.11.2 (UNI, decision 195) : une seule instance
// -----------------------------------------------------------------------------
//  Le client (03/10, 19 h 11) : « faire en sorte qu'on ne puisse pas ouvrir 2 fois
//  le logiciel en meme temps ». La premiere instance prend un verrou et ecoute :
//    * Windows : un mutex nomme Local\XPGAnalyser.instance.<utilisateur> (Local\ :
//      la session ; le nom : l'utilisateur) et un tube nomme, par utilisateur et par
//      session, ou elle ecoute ;
//    * Linux : un verrou flock (xpganalyser-instance.lock) dans XDG_RUNTIME_DIR,
//      sinon HOME (.xpganalyser-instance.lock), et une prise Unix a cote.
//  La deuxieme trouve le verrou pris : elle passe sa ligne de commande a la
//  premiere (sous Windows, AllowSetForegroundWindow d'abord : la premiere pourra
//  venir au premier plan), puis s'arrete. La premiere la recoit sur un fil a elle,
//  la range et reveille la boucle (wake) ; App la prend a l'image suivante.
//
//  PAS D'INSTANCE UNIQUE pour les modes d'essai (--script, --captures, --size, les
//  verifications de tutoriels, les essais caches) ni avec XPG_INSTANCES_MULTIPLES=1 :
//  les essais et les series de R111 lancent plusieurs applis a la fois.
//  --instance-unique l'impose quand meme (la session du banc wine).
//
//  Sans SDL, sans ecran : hmi_test l'essaie (Linux : le second verrou est refuse,
//  la ligne de commande arrive).
// =============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace core::instance {

// Une demande d'une autre instance : sa ligne de commande, chemins rendus absolus.
struct Request {
    std::vector<std::string> args;
};

// Le message qui passe d'une instance a l'autre : "XPGI", la version (1), la
// longueur, puis chaque argument termine par un zero. decode refuse ce qui n'est
// pas un message entier de cette version.
[[nodiscard]] std::string encode(const std::vector<std::string>& args);
[[nodiscard]] bool        decode(const std::string& bytes, std::vector<std::string>& args);

// Vrai : pas d'instance unique (un mode d'essai, ou XPG_INSTANCES_MULTIPLES posee,
// sauf a "0"). args : argv[1..] ; multiples : getenv("XPG_INSTANCES_MULTIPLES").
[[nodiscard]] bool exempted(const std::vector<std::string>& args, const char* multiples,
                            std::string* why = nullptr);

struct Options {
    std::string name{"XPGAnalyser"};   // le nom du verrou, du tube, de la prise
    std::string folder;                 // Linux : le dossier du verrou ("" : XDG_RUNTIME_DIR, sinon HOME)
    int         connectMs{10000};       // la deuxieme : combien attendre que la premiere ecoute
};

class Guard {
public:
    enum class Role {
        Primary,     // le verrou est a nous
        Secondary,   // une autre instance le tient : forward(), puis s'arreter
        Unchecked,   // impossible a savoir (why()) : on demarre quand meme
    };
    [[nodiscard]] static std::unique_ptr<Guard> acquire(const Options& options = {});
    ~Guard();
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

    [[nodiscard]] Role               role() const noexcept;
    [[nodiscard]] const std::string& why() const noexcept;

    // La deuxieme : passer la ligne de commande a la premiere ; faux (et why) si
    // elle n'a pas repondu dans le delai.
    bool forward(const std::vector<std::string>& args, std::string* why = nullptr);

    // La premiere : ecouter (un fil a part) ; wake est appele de ce fil quand une
    // demande arrive. take() rend les demandes arrivees (dans l'ordre).
    bool listen(std::function<void()> wake = {});
    void setWake(std::function<void()> wake);
    [[nodiscard]] std::vector<Request> take();

    struct Impl;

private:
    Guard() = default;
    std::unique_ptr<Impl> impl_;
};

// L'instance de l'appli : posee par main, lue par App ; nulle : pas d'instance unique.
void   setCurrent(Guard* guard) noexcept;
Guard* current() noexcept;

// Ramener la fenetre au premier plan. nativeWindow : le HWND de la fenetre (les
// proprietes de la fenetre SDL) ; sous Windows, SetForegroundWindow (permis par
// l'AllowSetForegroundWindow de la deuxieme), sinon le bouton de la barre des taches
// clignote. Ailleurs : rien (SDL_RaiseWindow suffit).
void bringToFront(void* nativeWindow) noexcept;

// Dire a l'utilisateur ce qui empeche de demarrer (Windows : une boite de message ;
// ailleurs : la sortie d'erreur).
void alert(const std::string& utf8Title, const std::string& utf8Text) noexcept;

} // namespace core::instance
