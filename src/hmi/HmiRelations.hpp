// =============================================================================
//  hmi/HmiRelations.hpp - 1.19.0 : le graphe des relations du projet
// -----------------------------------------------------------------------------
//  Qui utilise quoi : les vues et leurs objets, les symboles, les popups, les
//  ecrans modeles, les types, les variables, les equipements, les alarmes, les
//  scripts, les fonctions, les ressources, les recettes, les dossiers. Calcule
//  sur les donnees reelles du projet ; JAMAIS UNE DEPENDANCE INVENTEE : chaque
//  arete vient d'un texte ou d'un champ du projet, et dit ou (Edge::where).
//
//  UN NOEUD par element du projet (une vue, un objet de chaque vue, une
//  variable IHM, un type IHM...). UNE ARETE "de -> vers" se lit "de DEPEND de
//  vers" : l'objet LIT la variable, la variable a pour TYPE T_Pompe, l'instance
//  INSTANCIE son symbole, l'action OUVRE la popup. Deux exceptions de sens
//  inverse, structurelles : une vue CONTIENT ses objets, un dossier CONTIENT ses
//  elements (supprimer le contenant emporte le contenu).
//
//  LES LECTURES D'UN TEXTE sont celles du logiciel : chaque texte du projet est
//  visite par hmi::forEachCode (scripts, fonctions, methodes, actions,
//  proprietes, textes a trous, alarmes, recettes...), ses noms lus hors des
//  chaines et des commentaires, confirmes par les lecteurs du logiciel
//  (decledit::referencesIn pour une expression, hmi::scriptNames pour un code) ;
//  un nom que le code (ses declarations, un bloc VAR) ou la vue (une constante,
//  un parametre du meme nom) cache n'est pas la variable globale. Une chaine qui
//  NOMME une vue ouverte (IHM_POPUP('Pop_Pompe')), un script appele
//  (IHM_APPELER('Calcul')) ou une ressource citee compte, comme pour le logiciel.
//
//  CE QUI N'EST PAS UNE ARETE mais un emploi : la vue de demarrage, la vue
//  d'accueil d'un groupe, une variable archivee par l'historique, le son d'une
//  priorite d'alarme... - Node::notes. Un element qui a une note n'est pas
//  orphelin.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hmi::rel {

// ---- les noeuds -------------------------------------------------------------
//  Template : un ecran modele, un modele d'en-tete ou de pied de page (role
//  "modele", "entete", "pied"). Recipe : une recette (ses champs ecrivent des
//  variables).
enum class Kind : std::uint8_t {
    View, Symbol, Popup, Template, Object, Type, Variable, Equipment, Alarm, Script, Function, Resource, Folder, Recipe,
};
// "vue", "symbole", "popup", "\xC3\xA9" "cran mod\xC3\xA8le", "objet", "type", "variable", "\xC3\xA9quipement", "alarme",
// "script", "fonction", "ressource", "dossier", "recette".
[[nodiscard]] std::string_view kindLabel(Kind) noexcept;

struct Node {
    Kind        kind{Kind::Variable};
    // Le nom de l'element : "Pompe_1", "Vue_Station", "Pop_Pompe" ; un objet : son nom dans sa vue ("Pompe_1") ; un
    // script de vue : "Vue_Station.OnOpen" ; une fonction de vue : "Vue_Station.Calcul" ; une methode : "T_Pompe.Demarrer" ;
    // une alarme d'objet : son nom genere ("Vue_Station.Pompe_1.Defaut") ; un dossier : son chemin ("Ligne 1/Pompes").
    std::string name;
    Id          id{kNoId};            // son identifiant dans le projet (kNoId : une alarme d'objet, un dossier)
    Id          view{kNoId};          // la vue qui le porte (un objet, un script ou une fonction de vue, une alarme d'objet)
    std::string detail;               // "Pompe (Vue_Station)", "T_Pompe, li\xC3\xA9" "e \xC3\xA0 Station_Modbus (40001)", "vues"...
    // Les emplois hors des aretes (voir plus haut) : "vue de d\xC3\xA9marrage", "archiv\xC3\xA9" "e par l'historique"...
    std::vector<std::string> notes;
};

// ---- les aretes -------------------------------------------------------------
//  Reads    : un objet, un script, une fonction, une alarme (son message), une
//             recette (un jeu) lit une variable.
//  Writes   : ... l'ecrit (Nom := ..., une commande, une action Affecter, une
//             recette appliquee).
//  TypeOf   : une variable (un parametre de popup, une declaration de code, un
//             retour de fonction) est de ce type ; un type ETEND ou IMPLEMENTE un
//             autre type.
//  Member   : un type a un membre de ce type.
//  Address  : une variable est liee a un equipement (son adresse dans `path`).
//  Calls    : un code (ou une action Appeler, Appeler un script) appelle une
//             fonction ou un script.
//  Instance : une instance (un objet) pose ce symbole.
//  Opens    : un objet, une vue (ses actions), un script ouvre une vue ou une
//             popup ; une vue vers sa vue parente (le fil d'Ariane).
//  Contains : une vue contient ses objets ; un symbole ses popups ; un objet ses
//             alarmes ; un type ses methodes ; un dossier ses elements et ses
//             sous-dossiers.
//  Resource : un objet, une vue, un script cite une ressource.
//  Watches  : une alarme surveille une variable (sa condition) ; un script sur
//             changement surveille son expression.
//  Inherits : une vue herite d'un ecran modele, montre un en-tete, un pied.
//  Uses     : un objet ou une action emploie une recette.
enum class Link : std::uint8_t {
    Reads, Writes, TypeOf, Member, Address, Calls, Instance, Opens, Contains, Resource, Watches, Inherits, Uses,
};
// "lit", "\xC3\xA9" "crit", "est du type", "a un membre de type", "est li\xC3\xA9" "e \xC3\xA0", "appelle", "instancie",
// "ouvre", "contient", "cite la ressource", "surveille", "h\xC3\xA9rite de", "emploie".
[[nodiscard]] std::string_view linkLabel(Link) noexcept;

struct Edge {
    std::size_t from{0}, to{0};
    Link        link{Link::Reads};
    // Ou, lisible : "Vue_Station . Pompe_1 . value", "Vue_Station . Btn_P2 . action 1 (Ouvrir une popup)",
    // "script Calcul_Niveau, ligne 3", "alarme Niveau_Haut (condition)".
    std::string where;
    // Reads, Writes, Watches : le chemin tel qu'il est lu ("Cuve_1.Niveau", "Cuves[2].Niveau") ; TypeOf, Member : le
    // texte du type ("ARRAY[1..4] OF T_Pompe") ; Address : l'adresse ("40001") ; vide ailleurs.
    std::string path;
};

class Graph {
public:
    [[nodiscard]] const std::vector<Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<Edge>& edges() const noexcept { return edges_; }
    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] const Node& node(std::size_t i) const { return nodes_[i]; }
    // Le premier noeud de ce genre et de ce nom (sans casse). Un objet se trouve aussi par "Vue.Objet" ; View trouve
    // aussi une popup, un symbole, un ecran modele (toute vue du projet) ; Script et Function, un nom court ou complet.
    [[nodiscard]] std::optional<std::size_t> find(Kind, std::string_view name) const;
    // Tous les noeuds de ce nom (sans casse), tous genres, dans l'ordre du graphe.
    [[nodiscard]] std::vector<std::size_t> findAll(std::string_view name) const;
    // Le noeud d'un element par son identifiant (vue, objet, variable, type, script, fonction, alarme...).
    [[nodiscard]] std::optional<std::size_t> byId(Id) const;
    // Les aretes qui partent du noeud, qui y arrivent (des rangs dans edges()).
    [[nodiscard]] const std::vector<std::size_t>& out(std::size_t node) const { return out_[node]; }
    [[nodiscard]] const std::vector<std::size_t>& in(std::size_t node) const { return in_[node]; }

    // La construction (build) : un noeud ; une arete (la meme deux fois n'en fait qu'une ; faux : deja la).
    std::size_t add(Node);
    bool        link(std::size_t from, std::size_t to, Link, std::string where, std::string path = {});
    void        note(std::size_t node, std::string text);
    void        alias(std::size_t node, std::string_view name);   // un autre nom pour find (Vue.Objet)

private:
    std::vector<Node>                                         nodes_;
    std::vector<Edge>                                         edges_;
    std::vector<std::vector<std::size_t>>                     out_, in_;
    std::unordered_map<std::string, std::vector<std::size_t>> byName_;   // nom en majuscules -> noeuds
    std::unordered_map<Id, std::size_t>                       byId_;
    std::unordered_set<std::string>                           seen_;     // les aretes deja posees
};

// Le graphe du projet, tel qu'il est (rien n'est modifie).
[[nodiscard]] Graph build(const Project&);

// ---- les questions ----------------------------------------------------------
// Qui l'utilise directement : les sources des aretes qui y arrivent (sans les contenants : une vue ne << se sert >> pas
// de son objet, un dossier pas de son element), sans doublon, dans l'ordre des aretes.
[[nodiscard]] std::vector<std::size_t> users(const Graph&, std::size_t node);
// Ce qui serait touche si on le supprimait, de proche en proche :
//   - SUPPRIME avec lui : ce qu'il contient (les objets d'une vue, les popups d'un symbole, les alarmes d'un objet) ;
//   - TOUCHE : ce qui depend d'un element supprime (toute arete qui y arrive) ; puis ce qui depend d'un element touche
//     par sa valeur ou son code (lit, ecrit, surveille, est du type, a un membre, appelle, instancie, herite) - pas
//     ce qui l'ouvre seulement (une vue touchee s'ouvre toujours) ; le contenant d'un element touche est touche.
//   - DEPLACE : les elements et sous-dossiers d'un dossier supprime (ils montent d'un cran ; rien d'autre ne bouge).
struct Hit {
    std::size_t node{0};
    enum class How : std::uint8_t { Removed, Affected, Moved } how{How::Affected};
    std::size_t edge{0};              // l'arete par laquelle il est atteint (son << parce que >>)
};
[[nodiscard]] std::vector<Hit>         impactDetails(const Graph&, std::size_t node);
[[nodiscard]] std::vector<std::size_t> impact(const Graph&, std::size_t node);   // les noeuds de impactDetails, dans l'ordre
// Les orphelins : les variables jamais lues ni ecrites (ni surveillees, ni dans une recette, ni archivees), les
// symboles jamais instancies, les popups jamais ouvertes, les fonctions du projet jamais appelees, les scripts
// << sur appel >> jamais appeles, les ressources inutilisees, les equipements sans variable, les types sans emploi.
// Dans l'ordre du graphe.
[[nodiscard]] std::vector<std::size_t> orphans(const Graph&);
// Pourquoi il est orphelin, en une phrase : "variable jamais lue ni \xC3\xA9" "crite" ; vide : il ne l'est pas.
[[nodiscard]] std::string orphanReason(const Graph&, std::size_t node);

// Ce qui lit, ecrit ou surveille `path` : une variable (Niveau_Cuve_1) ou un membre (Cuve_1.Niveau : le chemin lu y
// commence, ou c'est la variable entiere qui est lue : Cuve_1). Sans casse. `kinds` vide : tous les genres. Sans
// doublon, dans l'ordre des aretes.
[[nodiscard]] std::vector<std::size_t> readers(const Graph&, std::string_view path, const std::vector<Kind>& kinds = {});
// Les objets qui utilisent `path` (une instance de symbole qui le passe en argument comprise).
[[nodiscard]] std::vector<std::size_t> objectsUsing(const Graph&, std::string_view path);
// Le code qui utilise `path` : les scripts (generaux et de vue) et les fonctions (du projet, de vue, les methodes).
[[nodiscard]] std::vector<std::size_t> scriptsUsing(const Graph&, std::string_view path);
// Les equipements d'une vue : ses objets, ses actions et ses scripts (et ceux de l'ecran modele, de l'en-tete et du
// pied qu'elle montre) -> les variables qu'ils lisent ou ecrivent -> les equipements de ces variables.
[[nodiscard]] std::vector<std::size_t> equipmentsOfView(const Graph&, std::size_t view);
// Les alarmes qui concernent `name` (une variable, un membre, un objet : "Pompe_2") : leur condition (ou leur
// message) le lit, ou leur nom le contient (une alarme d'objet "Vue_Station.Pompe_2.Defaut"). Sans casse.
[[nodiscard]] std::vector<std::size_t> alarmsAbout(const Graph&, std::string_view name);

// "variable Pompe_1 (T_Pompe, li\xC3\xA9" "e \xC3\xA0 Station_Modbus)", "objet Pompe_1 (Pompe, Vue_Station)".
[[nodiscard]] std::string describe(const Graph&, std::size_t node);
// "Vue_Station . Pompe_1 . value : objet Pompe_1 lit Pompe_1.Marche".
[[nodiscard]] std::string edgeText(const Graph&, const Edge&);

} // namespace hmi::rel
