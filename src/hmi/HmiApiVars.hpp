// =============================================================================
//  hmi/HmiApiVars.hpp - 1.11.1 : les variables de l'automate sous `API.`
// -----------------------------------------------------------------------------
//  LA DEMANDE DU CLIENT DU 03/10 (scene 2 de la maquette 1.10.4, validee) :
//  toutes les variables de l'automate, rangees en arbre, et un seul nom pour
//  les ecrire partout dans l'IHM :
//
//      API.<globale>                 API.Pression, API.Armoires[0].ana.PT1.mes
//      API.<Unite>.<variable>        API.Gestion_armoires.Etat
//      ... jusqu'au bout des membres API.Gestion_armoires.Regul.PID1.SP
//      ... et des tableaux           API.V[3].Pos
//
//  LE NOM QUE L'AUTOMATE CONNAIT. Le simulateur range une variable d'unite sous
//  "Unite.variable" (sim::Runtime::prepare) et un membre sous "Racine.membre" ;
//  le plan d'adressage (hmi::comm::Plan) cherche le meme nom. `API.` se
//  traduit donc en retirant le prefixe : "API.Gestion_armoires.Etat" se lit
//  "Gestion_armoires.Etat", "API.Pression" se lit "Pression" - la meme lecture,
//  la meme ecriture, la meme qualite que le nom nu. Le nom nu (une globale)
//  continue de marcher, sans avertissement.
//
//  `API` NE DISTINGUE PAS LA CASSE (le ST non plus) ; il s'ecrit `API.`. Si le
//  projet IHM a lui-meme une variable ou une vue nommee API, elle garde son
//  sens (un projet d'avant la 1.11.1 se calcule a l'identique) et la
//  verification le dit.
//
//  L'ARBRE (IHM > Configuration, "Variables du programme") :
//
//      Globales                       les variables du <dataBlock>
//      <Unite>                        une branche par unite de programme
//          Publiques                  publicLocalVariables
//          Privees                    privateLocalVariables
//          E/S                        inputParameters, outputParameters, inOutParameters
//
//  Une instance de DDT ou de DFB se deplie en ses membres, a toute profondeur
//  (champs d'un DDT ; broches, publiques et privees d'un DFB ; broches d'un bloc
//  de la bibliotheque) ; un tableau en ses premiers elements, puis un noeud
//  "... et N autres" qui se deplie a son tour. Seuls les deux premiers niveaux
//  sont construits d'avance : le reste l'est quand on deplie (children).
//
//  L'ACCES (la colonne Accessible), d'apres ce que l'appli sait deja - le plan
//  d'adressage de hmi::comm (buildPlan, placeAddress) et Link::write :
//   - Lecture / ecriture : le plan donne une place ecrivable a ce nom - une
//     globale localisee en %M, %MW, %MD, %MF (%MW i.j : un bit de mot), un
//     tableau localise d'un type simple, ou une ligne de la table des adresses
//     (Configuration > Communication) sans "lecture seule" ;
//   - Lecture seule : une place qui se lit seulement - %I, %IW, %ID, %IF (les
//     entrees), une ligne de la table cochee "lecture seule" ; ou la liaison
//     entiere en lecture seule (Communication.writes faux) ;
//   - Sans adresse - simulation seulement : le plan n'a pas de place pour ce
//     nom (une globale non localisee, une structure localisee, %S/%SW, %K/%KW,
//     %Q/%QW, une adresse topologique, une variable d'unite ou un membre que la
//     table ne nomme pas...). La raison est celle du plan quand il en donne une.
//  En simulation, toutes se lisent et s'ecrivent.
//
//  CE QUI SE VERIFIE (Compiler, Generer, le soulignement pendant la frappe) :
//   - un `API.` inconnu dit ce qui manque, avec le nom le plus proche
//     ("Gestion_armoires n'a pas de variable Etat ; veux-tu dire Etat_Armoire ?") ;
//   - une ecriture par `API.` vers une variable en lecture seule : une erreur,
//     avec la raison ;
//   - une variable `API.` sans adresse, quand le projet parle a l'automate reel
//     (Communication.modbus()) : un avertissement, "lue en simulation seulement".
//
//  UN EN-TETE LEGER : ni HmiModel.hpp ni ProjectModel.hpp ne sont lus ici.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace domain { class Project; }

namespace hmi {
struct Project;

namespace apivars {

// ------------------------------------------------------------- le prefixe ---
// "API" : le nom de l'espace, tel qu'il s'ecrit et se propose.
inline constexpr std::string_view kRoot = "API";

// Vrai si `text` commence par `API.` (sans casse ; des espaces autour du point
// sont permis, comme en ST).
[[nodiscard]] bool isApiPath(std::string_view text) noexcept;
// Le nom que l'automate connait : "API.Unite.Var" -> "Unite.Var", " api . V[3]" ->
// "V[3]" ; un texte sans `API.` est rendu tel quel.
[[nodiscard]] std::string stripApi(std::string_view text);
// L'inverse : "Unite.Var" -> "API.Unite.Var" (un nom qui a deja `API.` ne change pas).
[[nodiscard]] std::string withApi(std::string_view plcName);

// 1.11.2 (R1111-15, decision 144) : une raison du plan d'adressage, sans son
// conseil « : donne-lui une adresse (...) », quand le remede qui la suit dit
// deja de donner une adresse (l'infobulle de la marque, celle de la colonne
// Accessible, Generer) : une seule fois, au « tu ».
// "non localisee : donne-lui une adresse (Configuration > Communication, table
// des adresses)" -> "non localisee" ; une autre raison est rendue telle quelle.
[[nodiscard]] std::string withoutAddressAdvice(std::string_view why);

// ---------------------------------------------------------------- l'arbre ---
enum class NodeKind : std::uint8_t {
    Group,      // "Globales", une unite, "Publiques", "Privees", "E/S" : ils rangent
    Variable,   // une variable declaree (globale, ou d'une unite)
    Member,     // un membre d'une instance (champ de DDT ; broche, publique, privee de DFB)
    Element,    // un element de tableau ([3], [2, 5])
    More,       // "... et N autres" : les elements suivants d'un tableau
};

// Ce qu'est un groupe (NodeKind::Group).
enum class GroupKind : std::uint8_t { None, Globals, Unit, Public, Private, InOut };

// La portee d'une variable, et ce qu'est un membre.
enum class Scope : std::uint8_t {
    Global,     // le <dataBlock>
    Constant,   // une constante globale
    Public,     // une publique d'unite (ou d'un DFB, comme membre)
    Private,    // une privee d'unite (ou d'un DFB, comme membre)
    Input,      // une entree (IN) d'unite ou de DFB
    Output,     // une sortie (OUT)
    InOut,      // une entree-sortie (IN_OUT)
    Field,      // un champ de DDT
    Pin,        // une broche d'un bloc de la bibliotheque (TON.Q)
    Element,    // un element de tableau
};

// La colonne Accessible : l'IHM sur l'automate reel.
enum class Access : std::uint8_t {
    ReadWrite,   // "Lecture / ecriture"  (L/E)
    ReadOnly,    // "Lecture seule"       (L)
    NoAddress,   // "Sans adresse - simulation seulement"
};

// "Lecture / \xC3\xA9" "criture", "Lecture seule", "Sans adresse \xE2\x80\x94 simulation seulement".
[[nodiscard]] std::string_view accessLabel(Access) noexcept;
// "L/\xC3\x89", "L", "sans adresse" : l'aide a la saisie.
[[nodiscard]] std::string_view accessShort(Access) noexcept;
// "Globale", "Constante", "Publique", "Priv\xC3\xA9" "e", "Entr\xC3\xA9" "e", "Sortie",
// "Entr\xC3\xA9" "e / sortie", "Champ", "Broche", "\xC3\x89" "l\xC3\xA9" "ment".
[[nodiscard]] std::string_view scopeLabel(Scope) noexcept;
// "Globales", "Publiques", "Priv\xC3\xA9" "es", "E/S" ; une unite : "" (son nom).
[[nodiscard]] std::string_view groupLabel(GroupKind) noexcept;

// Un emploi par l'IHM : ou (pour le dire, et pour y aller d'un clic, comme un
// constat de Compiler : hmi::Issue).
struct Use {
    std::string where;          // "Vue_Accueil/Valve_1.fill", "script Initialisation, ligne 6", "alarme Haute"
    std::string written;        // le chemin tel qu'il est ecrit ("API.V[0].Pos", "V[0].Pos")
    std::uint64_t view{0};      // hmi::Id de la vue (0 : aucune)
    std::uint64_t object{0};    // ... de l'objet
    std::string   property;     // la propriete, ou le nom du script
    std::uint64_t script{0};    // un script : y aller
    int           line{0};      // ... a cette ligne (1 = la premiere ; 0 : sans ligne)
    std::uint64_t item{0};      // l'alarme, la recette, l'utilisateur en cause
};

struct Node {
    NodeKind    kind{NodeKind::Variable};
    GroupKind   group{GroupKind::None};   // un groupe : lequel
    std::string key;            // unique et stable : l'etat deplie se retient par elle
    std::string name;           // la colonne Nom : "Etat", "PT1", "[3]", "Publiques", "... et 90 autres"
    std::string path;           // "API.Gestion_armoires.Etat" (vide : un groupe, un More)
    std::string plcName;        // le nom que l'automate connait : "Gestion_armoires.Etat"
    std::string type;           // "INT", "ARRAY[0..9] OF BOOL", "T_Armoire" (vide : un groupe)
    Scope       scope{Scope::Global};
    std::string scopeText;      // la colonne Portee : "Globale", "Publique \xC2\xB7 Gestion_armoires", "Champ"
    std::string unit;           // l'unite qui la declare (vide : une globale et ses membres)
    Access      access{Access::NoAddress};
    std::string accessWhy;      // la raison (l'infobulle) ; sans adresse : comment la rendre accessible
    std::string reference;      // l'adresse : "%MW100" (le programme) ou celle de la table ; vide : aucune
    std::uint64_t bits{0};      // la taille, en bits (0 : inconnue)
    std::string comment;        // le commentaire de la declaration
    std::uint32_t uses{0};      // les emplois par l'IHM (de ce chemin et de ce qu'il contient)
    bool        constant{false};   // une constante : l'IHM ne l'ecrit pas
    bool        children{false};   // il se deplie (sans fabriquer ses enfants)
    // Un groupe : ses enfants directs ; un More : les elements [first, last] du tableau.
    std::size_t count{0};
    std::int64_t first{0}, last{-1};
    // La declaration dans le projet de l'automate (domain::Index ; 0xFFFFFFFF : aucune).
    std::uint32_t decl{0xFFFFFFFFu};

    [[nodiscard]] bool real() const noexcept { return kind == NodeKind::Variable || kind == NodeKind::Member || kind == NodeKind::Element; }
    // L'IHM peut-elle l'ecrire sur l'automate reel ?
    [[nodiscard]] bool writable() const noexcept { return real() && !constant && access == Access::ReadWrite; }
};

// Les premiers elements d'un tableau montres sous lui ; puis "... et N autres",
// qui en montre kMoreBatch de plus (et un nouveau "... et N autres").
inline constexpr std::int64_t kFirstElements = 10;
inline constexpr std::int64_t kMoreBatch = 100;

// ------------------------------------------------------------ la resolution ---
struct Resolved {
    bool        ok{false};
    // Trouve : ce qu'est le chemin.
    std::string path;           // ecrit proprement : "API.Gestion_armoires.Etat"
    std::string plcName;        // ce que l'automate lit : "Gestion_armoires.Etat"
    std::string type;           // le type du bout ("" : inconnu - un indice calcule, un bit de mot)
    Scope       scope{Scope::Global};
    std::string unit;           // l'unite (vide : une globale)
    Access      access{Access::NoAddress};
    std::string accessWhy;
    std::string reference;
    bool        constant{false};
    bool        unitOnly{false};   // "API.Unite" seul : une unite n'est pas une valeur
    // Pas trouve : ce qui manque.
    std::string error;          // "Gestion_armoires n'a pas de variable Etat ; veux-tu dire Etat_Armoire ?"
    std::string missing;        // le morceau inconnu : "Etat"
    std::string suggestion;     // le plus proche a sa place : "Etat_Armoire" (vide : aucun)
    std::string suggestedPath;  // le chemin corrige : "API.Gestion_armoires.Etat_Armoire"
};

// ------------------------------------------------------ l'aide a la saisie ---
struct Proposal {
    enum class Kind : std::uint8_t { Root, Unit, Variable, Member, Element };
    Kind        kind{Kind::Variable};
    std::string text;           // ce qui s'insere a la place du mot tape : "API", "Gestion_armoires", "Etat"
    std::string path;           // le chemin complet : "API.Gestion_armoires.Etat"
    std::string type;           // "INT", "T_Armoire" ; une unite : "unit\xC3\xA9 de programme"
    std::string group;          // le rangement : "Globales", "Unit\xC3\xA9s", "Publiques", "Priv\xC3\xA9" "es", "E/S", "Membres"
    Access      access{Access::NoAddress};
    std::string accessText;     // accessShort(access) ; une unite, la racine : vide
    std::string comment;        // le commentaire de la declaration
};

// ---------------------------------------------------------------- le modele ---
struct BuildOptions {
    bool uses{true};            // compter les emplois par l'IHM (parcourt tout le projet IHM)
};

class Model {
public:
    Model();
    ~Model();
    Model(const Model&);
    Model& operator=(const Model&);
    Model(Model&&) noexcept;
    Model& operator=(Model&&) noexcept;

    // LA CONSTRUCTION. `plc` : le programme de l'automate (nul : un modele vide,
    // rien n'est connu) ; `hmi` : le projet IHM - sa table des adresses et sa
    // liaison (l'acces), ses emplois (nul : aucun emploi, la table vide).
    [[nodiscard]] static Model build(std::shared_ptr<const domain::Project> plc, const Project* hmi,
                                     const BuildOptions& options = {});
    // La meme, depuis un projet que l'appelant ne partage pas : le modele en
    // garde une copie des declarations (variables, types, unites ; pas le code).
    [[nodiscard]] static Model build(const domain::Project& plc, const Project* hmi, const BuildOptions& options = {});

    // Le meme modele (les memes declarations, partagees), l'acces calcule sur la
    // liaison de ce projet IHM (sa table des adresses, Modbus ou simulateur) ;
    // sans les emplois. Lui-meme s'il a deja cette liaison. Compiler et Generer
    // s'en servent : l'acces suit la Configuration > Communication du moment.
    [[nodiscard]] Model withLink(const Project& hmi) const;

    [[nodiscard]] bool empty() const noexcept;
    // Le projet IHM parle-t-il a l'automate reel (Communication.modbus()) ?
    [[nodiscard]] bool realPlc() const noexcept;

    // L'ARBRE : "Globales" puis une branche par unite (dans l'ordre du projet).
    [[nodiscard]] const std::vector<Node>& roots() const noexcept;
    // Les enfants d'un noeud (un groupe, une instance, un tableau, un More), dans
    // l'ordre de la declaration ; fabriques a la demande.
    [[nodiscard]] std::vector<Node> children(const Node& n) const;
    // Le noeud d'un chemin (API.… ou nu) ; faux si inconnu.
    [[nodiscard]] bool node(std::string_view path, Node& out) const;
    // LA RECHERCHE (nom, type, reference ; sans casse) dans tout l'arbre : les
    // chemins des noeuds qui correspondent (au plus `limit`), pour les deplier.
    // Un tableau n'y montre que ses kFirstElements premiers elements.
    [[nodiscard]] std::vector<std::string> search(std::string_view text, std::size_t limit = 200) const;

    // LA RESOLUTION d'un chemin `API.…` (ou nu : une globale), indices constants
    // compris ("API.V[3].Pos") ; un indice calcule ("API.V[i].Pos") se resout
    // sur le type de l'element, sans borne.
    [[nodiscard]] Resolved resolve(std::string_view path) const;
    // L'acces d'un nom de l'automate (sans `API.`) : la place que le plan lui donne.
    [[nodiscard]] Access accessOf(std::string_view plcName, std::string* why = nullptr, std::string* reference = nullptr) const;

    // LES PROPOSITIONS pour ce qui est tape avant le curseur : "A", "AP" -> API ;
    // "API." -> les unites et les globales ; "API.<Unite>." -> ses variables
    // (Publiques, Privees, E/S) ; "API.<instance>." -> ses membres. Le dernier
    // morceau filtre (debut du nom, sans casse). Vide : rien a proposer.
    [[nodiscard]] std::vector<Proposal> propose(std::string_view typed) const;

    // Les unites de programme ; les noms des globales (pour "veux-tu dire").
    [[nodiscard]] std::vector<std::string> unitNames() const;
    [[nodiscard]] bool isUnit(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> globalNames() const;

    // LES EMPLOIS d'un chemin (et de ce qu'il contient), dans l'ordre du projet.
    [[nodiscard]] std::vector<Use> usesOf(std::string_view path) const;

private:
    struct Data;
    std::shared_ptr<const Data> d_;
};

} // namespace apivars
} // namespace hmi
