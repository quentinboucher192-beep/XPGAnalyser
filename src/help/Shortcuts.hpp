// =============================================================================
//  help/Shortcuts.hpp - 1.11 (chantier T2) : LA table des raccourcis
// -----------------------------------------------------------------------------
//  UNE SEULE TABLE DANS LES SOURCES. Avant la 1.11, une touche s'ecrivait a
//  trois endroits : la chaine d'App.cpp (actions_.add), la table de
//  HelpDocument.cpp et le guide. Les trois divergeaient (Ctrl+H etait a la
//  fois l'Historique et l'import du .XHW).
//
//  Maintenant :
//    - la page des raccourcis du centre d'aide, sa fiche A4, Aller a... et le
//      menu Aide lisent all() ;
//    - App.cpp demande sa touche a bindingOf("help.open") ;
//    - l'essai centreAide111() verifie la table contre le registre d'actions.
//
//  Une ligne : le contexte, la touche telle que l'ecran l'ecrit (en francais :
//  "Ctrl+Maj+F", "Alt+<-"), ce qu'elle fait, la version qui l'a apportee
//  ("1.10", "1.11" : le repere dessine a cote ; vide : plus ancienne) et les
//  actions du registre qu'elle declenche ("view.variables,view.statistics"
//  pour "Ctrl+1 / Ctrl+5" : la i-eme action prend la i-eme touche).
//
//  Pur, sans ecran.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace help::keys {

enum class Context : std::uint8_t { General, HmiEditor, Scripts, Simulation, Grafcet, Help };

struct Shortcut {
    Context          context{Context::General};
    std::string_view keys;      // "Ctrl+Y / Ctrl+Maj+Z" : l'ecriture de l'ecran
    std::string_view text;      // ce qu'elle fait, une phrase courte
    std::string_view since;     // "", "1.10", "1.11"
    std::string_view actions;   // les identifiants du registre, separes par des virgules ; vide : aucun
};

// La table, dans l'ordre de la page (par contexte).
[[nodiscard]] const std::vector<Shortcut>& all();

// Les contextes, dans l'ordre de la page, et leur nom a l'ecran.
[[nodiscard]] const std::vector<Context>& contexts();
[[nodiscard]] std::string_view contextLabel(Context);

// Les lignes d'un contexte.
[[nodiscard]] std::vector<const Shortcut*> ofContext(Context);

// LA TOUCHE D'UNE ACTION, dans l'ecriture du registre ("Ctrl+Shift+H",
// "Alt+Left") : ce qu'App.cpp passe a actions_.add. Vide : l'action n'a pas
// de raccourci.
[[nodiscard]] std::string bindingOf(std::string_view action);

// L'ecriture de l'ecran -> celle du registre : "Maj" -> "Shift", les fleches
// -> "Left"/"Right"/"Up"/"Down", "Suppr" -> "Delete", "Echap" -> "Escape",
// "Entree" -> "Return", "Espace" -> "Space". Une seule touche (pas de " / ").
[[nodiscard]] std::string toBinding(std::string_view keys);

// Les variantes d'une ligne ("Ctrl+Y / Ctrl+Maj+Z" -> deux), et les touches
// de chacune ("Ctrl+Maj+Z" -> "Ctrl", "Maj", "Z") : ce que la page dessine en
// touches de clavier.
[[nodiscard]] std::vector<std::string> alternatives(std::string_view keys);
[[nodiscard]] std::vector<std::string> keyCaps(std::string_view oneAlternative);

// LA RECHERCHE : dans la touche, le texte et le contexte ; la casse et les
// accents ne comptent pas. "F8" trouve F8 et Maj+F8. Un terme vide : tout.
[[nodiscard]] std::vector<const Shortcut*> search(std::string_view term);

// Les actions d'une ligne, une par une.
[[nodiscard]] std::vector<std::string> actionsOf(const Shortcut&);

// LA FICHE A4 en texte : un titre, puis chaque contexte et ses lignes
// ("Ctrl+K  Aller a..."). L'apercu et l'impression la mettent en page ; un
// fichier texte la garde telle quelle.
[[nodiscard]] std::string sheetText(std::string_view version);

// Le repli de la recherche (minuscules, sans accents), expose pour l'index du
// centre d'aide et les essais. `origin` (facultatif) : pour chaque lettre du
// resultat, sa position dans `text`, plus text.size() a la fin - ce qui permet
// de surligner dans le texte d'origine ce qui a ete trouve dans le repli.
[[nodiscard]] std::string fold(std::string_view text, std::vector<std::size_t>* origin = nullptr);

} // namespace help::keys
