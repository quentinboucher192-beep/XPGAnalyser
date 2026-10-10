// =============================================================================
//  app/Disposition.hpp - 1.12.3 : PROJET > DISPOSITION... (le modele)
// -----------------------------------------------------------------------------
//  QUOI, QUAND, OU, COMMENT - ET CA SE GARDE. La fenetre de l'application a des
//  panneaux (l'explorateur du projet, les documents ouverts, le panneau du bas,
//  la bande d'etat), chaque page a ses sous-onglets (le panneau du bas, l'
//  inspecteur de l'editeur de vue, la simulation, la programmation generale, les
//  equipements), et le centre ses pages. Pour chacun :
//
//    l'afficher       oui / non
//    quand            toujours, en edition, en simulation (une page : a la
//                     demande, a l'ouverture du projet, au demarrage de la
//                     simulation)
//    ou               a gauche / a droite, sous l'editeur / a sa droite, dans un
//                     onglet / cote a cote / dans une fenetre detachee
//    au depart        ouvert ou replie ; le sous-onglet choisi a l'ouverture
//
//  Ce fichier est le modele, sans widget : le catalogue (par edition : API et IHM
//  ont chacune le leur), une disposition (Layout), ses dispositions toutes faites,
//  sa lecture et son ecriture dans les reglages de l'edition (cles disposition.*),
//  et la disposition EN VIGUEUR (current) avec l'etat de la simulation : chaque
//  volet la consulte a sa mise en page (shown), et `changed` le previent.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace app::disposition {

// Ce qu'est une ligne du catalogue.
enum class Kind : unsigned char {
    Panel,     // un panneau de la fenetre (afficher, quand, ou, au depart)
    Tab,       // un sous-onglet d'une page (afficher, quand ; le choisi au depart : par groupe)
    Option,    // un reglage d'affichage (afficher seulement : les lignes alternees)
    Page,      // une page du centre (quand elle s'ouvre, ou)
};

struct Choice {
    std::string key;      // la valeur gardee ("gauche", "replie", "cote")
    std::string label;    // ce que dit la fenetre ("A gauche", "Replie (Ctrl+J)")
};

struct Row {
    std::string         id;       // "explorateur", "bas.console", "page.simulation"
    std::string         label;    // "Explorateur du projet"
    std::string         group;    // l'identifiant de son groupe ("fenetre", "bas", "insp"...)
    Kind                kind{Kind::Panel};
    std::string         tabTitle; // un sous-onglet : le titre de son onglet ("Console")
    std::vector<Choice> where;    // vide : pas de choix de place
    std::vector<Choice> start;    // vide : pas de choix au depart (un panneau : ouvert / replie)
};

struct Group {
    std::string id;       // "fenetre", "bas", "insp"...
    std::string title;    // "La fenetre", "Panneau du bas"...
    bool        tabs{false};   // des sous-onglets : un « choisi au depart »
};

// Le catalogue de l'edition qui tourne (core::hasApi) - lu a la demande.
[[nodiscard]] const std::vector<Group>& groups();
[[nodiscard]] const std::vector<Row>&   rows();
[[nodiscard]] const Row*                row(std::string_view id);
// Les choix de « quand » : panneaux et sous-onglets / pages du centre.
[[nodiscard]] const std::vector<Choice>& whenChoices(Kind kind);

// ---- une disposition ------------------------------------------------------
struct Item {
    bool        shown{true};
    std::string when;     // un panneau, un sous-onglet : toujours | edition | simulation ;
                          // une page : demande | projet | simulation
    std::string where;    // vide : la place d'origine
    std::string start;    // vide : comme d'origine
    bool operator==(const Item&) const = default;
};
struct Layout {
    std::map<std::string, Item, std::less<>>        items;      // une par ligne du catalogue
    std::map<std::string, std::string, std::less<>> startTab;   // groupe de sous-onglets -> la ligne choisie au depart
    bool operator==(const Layout&) const = default;
    [[nodiscard]] const Item& item(std::string_view id) const;
};

[[nodiscard]] Layout defaults();
// Les dispositions toutes faites : (cle, libelle). « mienne » : celle gardee par
// l'utilisateur (« Garder comme Ma disposition »), sinon celle par defaut.
[[nodiscard]] std::vector<Choice> presets();
[[nodiscard]] Layout preset(std::string_view key, const Layout* mine = nullptr);

// Montre-t-on cette ligne, la simulation en marche ou non ? Une ligne inconnue : oui.
[[nodiscard]] bool shown(const Layout&, std::string_view id, bool simulating);
// Le sous-onglet choisi au depart d'un groupe (son titre d'onglet ; vide : le premier).
[[nodiscard]] std::string startTitle(const Layout&, std::string_view group);
// Combien de lignes (et de choix au depart) different.
[[nodiscard]] std::size_t differences(const Layout&, const Layout&);

// ---- les reglages de l'edition : disposition.<ligne> = "v=1;q=...;ou=...;dep=..." ----
using Getter = std::function<std::string(const std::string& key)>;   // vide : absente
using Setter = std::function<void(const std::string& key, const std::string& value)>;
inline constexpr std::string_view kPrefix = "disposition.";
inline constexpr std::string_view kMineKey = "dispositionMienne";   // « Ma disposition », d'un bloc
[[nodiscard]] Layout fromSettings(const Getter&);
// Seules les lignes qui different de la disposition d'origine s'ecrivent (`clear` :
// effacer les autres) - « Retablir » efface toutes les cles disposition.*.
void toSettings(const Layout&, const Setter& set, const std::function<void(const std::string&)>& clear);
// D'un bloc (« Ma disposition ») : "explorateur=v=1,q=toujours,ou=droite|...".
[[nodiscard]] std::string serialize(const Layout&);
[[nodiscard]] Layout      parse(std::string_view);

// ---- la disposition en vigueur ---------------------------------------------
[[nodiscard]] const Layout& current();
void setCurrent(Layout);
[[nodiscard]] bool simulating();
void setSimulating(bool);
// La disposition en vigueur, ou la simulation, a change : les volets se remettent en page.
[[nodiscard]] const core::SignalPtr<>& changed();
// Raccourcis : la ligne est-elle montree maintenant ? Le sous-onglet choisi au depart ?
[[nodiscard]] bool shownNow(std::string_view id);
[[nodiscard]] std::string startTitleNow(std::string_view group);

} // namespace app::disposition

namespace ui { class TabControl; }

namespace app::disposition {
// Les sous-onglets d'un groupe, montres ou caches selon la disposition en vigueur
// (par leur titre) ; `first` : choisir le sous-onglet du depart (a l'ouverture), s'il a
// ete choisi (celui d'origine : la page garde le sien).
void applyTabs(ui::TabControl& tabs, std::string_view group, bool first = false);
} // namespace app::disposition
