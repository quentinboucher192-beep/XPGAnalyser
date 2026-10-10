// =============================================================================
//  ui/KeyMap.hpp - 1.12.2 : les raccourcis des editeurs de code (profil Visual
//  Studio, accords Ctrl+K puis Ctrl+C)
// -----------------------------------------------------------------------------
//  UNE TABLE PAR PROFIL. Un raccourci est un accord : une touche (Ctrl+D), ou
//  deux de suite (Ctrl+K, Ctrl+C), qui donne une commande ("commenter"). Les
//  editeurs (MultiLineText avec setCommandKeys) demandent a un Resolver ce que
//  fait la touche : rien, l'attente de la seconde touche, une commande, ou un
//  accord inconnu ("La combinaison (Ctrl+K, Ctrl+Q) n'est pas une commande.").
//
//  DEUX PROFILS. "vs" (le defaut de la 1.12.2) : celui de Visual Studio. Les
//  touches qui avaient un sens dans l'appli (F12 la capture, F8 la simulation,
//  Ctrl+W fermer l'onglet, Ctrl+H l'historique, Ctrl+K Aller a) le gardent
//  HORS des editeurs ; dans un editeur, elles font ce que fait Visual Studio.
//  "classique" : la 1.12.1 sans accords - Ctrl+K reste Aller a meme dans
//  l'editeur, F8 et F12 gardent leur sens ; les commandes a une touche qui ne
//  prenaient rien a personne restent.
//
//  L'ECRITURE est celle de l'appli : "Ctrl+Maj+K", "Alt+Haut", "Ctrl+K, Ctrl+C".
//  parse() lit aussi "ctrl+shift+k", "Ctrl+K Ctrl+C" et les fleches.
//
//  Pur, sans ecran : keymap_test le verifie, et verifie la page d'aide contre
//  la table.
// =============================================================================
#pragma once

#include "../platform/InputEvent.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui::keymap {

// Un appui : une touche et ses modificateurs (AltGr arrive en Ctrl+Alt : une
// liaison Ctrl+x ne le prend pas).
struct Stroke {
    Key  key{ Key::Unknown };
    bool ctrl{ false }, shift{ false }, alt{ false };
    friend bool operator==(const Stroke&, const Stroke&) = default;
};

// Un accord : un appui, ou deux de suite (second.key == Unknown : un seul).
struct Chord {
    Stroke first;
    Stroke second;
    [[nodiscard]] bool twoStrokes() const noexcept { return second.key != Key::Unknown; }
    friend bool operator==(const Chord&, const Chord&) = default;
};

[[nodiscard]] Stroke strokeOf(const KeyDown& k) noexcept;

// Le nom d'une touche a l'ecran ("K", "Entree", "Haut", "/", "Pg.Suiv") ; vide : inconnue.
[[nodiscard]] std::string keyName(Key k);
[[nodiscard]] std::string label(const Stroke&);      // "Ctrl+Maj+K"
[[nodiscard]] std::string label(const Chord&);       // "Ctrl+K, Ctrl+C"
[[nodiscard]] std::optional<Stroke> parseStroke(std::string_view);
[[nodiscard]] std::optional<Chord>  parse(std::string_view);

// ---- les profils ----------------------------------------------------------------
enum class Profile : std::uint8_t { VisualStudio, Classic };
[[nodiscard]] std::string_view profileKey(Profile) noexcept;     // "vs", "classique" (les reglages)
[[nodiscard]] std::string_view profileLabel(Profile) noexcept;   // "Visual Studio", "Classique (1.12.1)"
[[nodiscard]] std::optional<Profile> profileFromKey(std::string_view) noexcept;
// Le profil de l'application (lu dans les reglages au lancement, change par Aller a).
[[nodiscard]] Profile current() noexcept;
void setCurrent(Profile) noexcept;

// ---- les commandes ----------------------------------------------------------------
//  `host` : l'editeur ne sait pas la faire seul (aller a la definition, compiler,
//  demarrer la simulation...) : il la DEMANDE (MultiLineText::commandRequested)
//  et l'ecran la fait.
struct Command {
    std::string_view id;         // "commenter" : la cle des essais et des sessions
    std::string_view label;      // "Commenter les lignes (//)"
    std::string_view family;     // "Edition", "Rechercher"...
    bool             host{ false };
};
[[nodiscard]] const std::vector<Command>& commands();
[[nodiscard]] const Command* command(std::string_view id) noexcept;

struct Binding {
    Chord            chord;
    std::string_view command;
};
[[nodiscard]] const std::vector<Binding>& bindings(Profile);
// Les accords d'une commande dans un profil, dans l'ordre de la table.
[[nodiscard]] std::vector<Chord> chordsOf(Profile, std::string_view command);
// La commande d'un appui seul ; vide : aucune.
[[nodiscard]] std::string_view single(Profile, const Stroke&);
// Cet appui commence-t-il un accord a deux touches ?
[[nodiscard]] bool startsChord(Profile, const Stroke&);

// ---- l'attente de la seconde touche ----------------------------------------------------
class Resolver {
public:
    enum class Outcome : std::uint8_t { None, Pending, Command, Unknown };
    struct Result {
        Outcome          outcome{ Outcome::None };
        std::string_view command;   // Outcome::Command
        std::string      chord;     // Pending : "Ctrl+K" ; Command, Unknown : l'accord entier
    };
    // Une touche : Maj, Ctrl et Alt seuls ne comptent pas (None, l'attente continue).
    Result feed(Profile, const KeyDown&);
    void   cancel() noexcept { first_.reset(); }
    [[nodiscard]] bool pending() const noexcept { return first_.has_value(); }
    [[nodiscard]] std::string pendingLabel() const { return first_ ? label(*first_) : std::string{}; }

private:
    std::optional<Stroke> first_;
};

// Les phrases de la barre d'etat, celles de Visual Studio.
[[nodiscard]] std::string pendingMessage(std::string_view firstStroke);   // "(Ctrl+K) a ete appuye. Attente..."
[[nodiscard]] std::string unknownMessage(std::string_view chord);         // "La combinaison (Ctrl+K, Ctrl+Q) n'est pas une commande."

}   // namespace ui::keymap
