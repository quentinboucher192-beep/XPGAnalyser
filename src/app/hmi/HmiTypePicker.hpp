// =============================================================================
//  app/hmi/HmiTypePicker.hpp - 1.11.19 (refonte des scripts, lot 6) : LE
//  SELECTEUR DE TYPES
// -----------------------------------------------------------------------------
//  "Choisir un type..." (la case Type d'une grille de declarations, le type d'un
//  parametre de popup, d'une variable IHM, d'un membre, le retour d'une fonction)
//  ouvre ce selecteur, bati sur le registre des types (hmi::typereg) :
//
//    +-- Choisir un type - "Type de Mini" ------------- pour : une declaration --+
//    | [Rechercher un type (nom, categorie, provenance)_________________________] |
//    | [Tous] [Recents] [Elementaires] [Chaines et durees] [Structures IHM] ...    |
//    | Type          Categorie           Provenance            Identifiant        |
//    | REAL          Elementaires        IEC 61131-3           base:REAL          |
//    | T_Four        Structures IHM      Projet - Types IHM    ihm:615            |
//    | T_Four : un four. Membres : Temperature : REAL, Consigne : REAL            |
//    | Forme : [Simple] [Tableau] [Tableau 2D] [Liste] [Vecteur] [Dictionnaire]...|
//    |         Bornes : [0..9______]   [ ] REF_TO (une reference)                 |
//    | Resultat : ARRAY[0..9] OF T_Four                                           |
//    |                          [Annuler] [Ouvrir la definition] [Choisir]        |
//    +----------------------------------------------------------------------------+
//
//  - LA LISTE : les types permis pour l'usage demande (Spec::use), tous, pas
//    seulement ceux qu'une liste propose d'office : LINT, LWORD, les DDT...
//    La recherche plie la casse et les accents ; les puces filtrent par
//    categorie ; "Recents" montre les derniers types choisis (l'hote les garde).
//  - RAFRAICHI : un type cree, renomme ou supprime pendant que le selecteur est
//    ouvert y apparait aussitot (le projet est relu a chaque changement).
//  - LE TYPE ACTUEL est choisi d'office (un tableau : ses bornes cochees) ; s'il
//    n'existe plus (un type supprime), le selecteur le dit.
//  - LA FORME (1.12.2) : Simple, Tableau, Tableau 2D, Liste, Vecteur, Dictionnaire
//    (MAP), Tuple (hmi::typeform) - et une reference (REF_TO) - seulement la ou
//    l'usage les permet ; le resultat est verifie (une borne illisible : Choisir
//    est grise et la raison est dite).
//  - "OUVRIR LA DEFINITION" d'un type IHM ou d'un DDT : l'hote y mene.
// =============================================================================
#pragma once

#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiPopupParams.hpp"
#include "../../hmi/HmiTypeForms.hpp"
#include "../../hmi/HmiTypeRegistry.hpp"
#include "../../sim/Rich.hpp"   // 1.12.7 : sim::ObjRef (le deballage des locales d'un calcul)
#include "../../menu/IMenu.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class MultiLineText; }

namespace app {

class HmiTypePicker final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string              field;                                    // « Type de Mini » (le titre) ; vide : rien
        std::string              current;                                  // le type actuel (choisi d'office)
        unsigned                 use{hmi::typereg::UseDeclaration};        // l'usage : les types permis
        hmi::DocumentPtr         doc;                                      // le projet (ses types IHM, relus a chaque changement)
        hmi::params::PlcTypes    plc{};                                    // les DDT du programme (vides : inconnus)
        std::vector<std::string> recents{};                                // les derniers types choisis (les plus recents d'abord)
        bool                     fixedOnly{false};                         // 1.12.2 : un membre de type IHM (ni liste, ni MAP, ni tuple)
    };
    struct Answer {
        std::string type;                                                  // le type choisi (ecrit : "ARRAY[0..9] OF REAL")
        std::string open;                                                  // non vide : ouvrir la definition de cette cle
        std::string element;                                               // le type nomme (pour les recents)
    };

    explicit HmiTypePicker(Spec spec);
    ~HmiTypePicker() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    [[nodiscard]] static Answer parse(const std::string& payload);
    // Les recents, mis a jour : `name` en tete, sans double, huit au plus.
    static void remember(std::vector<std::string>& recents, const std::string& name);

    // Pour les sessions et les essais.
    void setSearch(const std::string& text);
    bool setCategory(std::string_view label);          // "Tous", "Recents" (UTF-8) ou un libelle de categorie
    bool select(std::string_view name);                // une ligne de la liste, par son nom
    void setArray(bool on, const std::string& bounds = {});   // "0..9", "0..3, 0..9"
    void setReference(bool on);
    void setMap(bool on);
    bool setForm(std::string_view labelOrKey);         // 1.12.2 : "Liste", "vecteur", "Dictionnaire (MAP)"...
    void composeElement();                             // 1.12.14 : Composer l'element... (un second selecteur)
    [[nodiscard]] std::vector<std::string> shownNames() const;
    [[nodiscard]] std::string result() const;          // le type qu'on choisirait
    [[nodiscard]] std::string resultProblem() const;   // vide : il est bon
    [[nodiscard]] std::string notice() const;          // le type actuel introuvable, dit
    [[nodiscard]] bool selectionVisible() const;       // la ligne choisie se voit dans la liste (une fois placee)
    void choose();                                     // Choisir (rien si le resultat est mauvais)
    void openDefinition();                             // Ouvrir la definition
    void finish(bool ok);

protected:
    core::Status buildUi() override;
    void         onEnter() override;

private:
    class Body;
    Spec   spec_;
    Body*  body_{nullptr};
    bool   done_{false};
    Answer answer_;
};

// ---- l'hote : qui ouvre le selecteur ----------------------------------------------------
//  Une grille, un volet ne connaissent pas la fenetre : ils DEMANDENT un type ; l'hote (l'ecran
//  principal) ouvre le selecteur, garde les recents, mene a une definition, puis rend le type
//  choisi a `done` (jamais appele si l'on annule). Les essais posent leur propre hote.
namespace typepicker {
// L'entree des listes de types qui ouvre le selecteur (les grilles, les proprietes).
inline const std::string kChoose = "Choisir un type\xE2\x80\xA6";
using Done = std::function<void(const HmiTypePicker::Answer&)>;
using Host = std::function<void(HmiTypePicker::Spec, Done)>;
void setHost(Host host);
[[nodiscard]] bool available();
void ask(HmiTypePicker::Spec spec, Done done);    // rien sans hote
// 1.12.4 : LES FORMES dans une liste de types (une seule ligne Type) - Tableau..., Liste...,
// Vecteur..., Dictionnaire (MAP)..., Tuple... : le selecteur s'ouvre sur cette forme. La cle de la
// forme d'une entree ("tableau", "liste", "vecteur", "map", "tuple") ; vide : pas une telle entree.
[[nodiscard]] const std::vector<std::string>& formChoices();
[[nodiscard]] std::string formOfChoice(std::string_view text);
// Le type `current` mis dans la forme `form` (sa cle) : LIST OF REAL depuis REAL ; ANY : INT.
[[nodiscard]] std::string inForm(std::string_view current, std::string_view form);
} // namespace typepicker

// ---- 1.12.2 : L'EDITEUR DES ELEMENTS D'UNE VALEUR --------------------------------------
//  La valeur d'une liste, d'un vecteur, d'un tableau, d'un dictionnaire ou d'un tuple (une
//  constante, une valeur initiale) : un element par ligne ('Pompe 1', 12.5, TRUE ; un
//  dictionnaire : 'cle' := valeur), rendu en litteral ([..], (..)). Comme le selecteur :
//  la grille demande, l'hote (l'ecran) ouvre la fenetre ; `done` recoit le litteral.
namespace itemseditor {
struct Spec {
    std::string title;      // « Elements de Noms »
    std::string type;       // LIST OF STRING, MAP[STRING] OF INT, TUPLE(INT, STRING), ARRAY[1..4] OF REAL
    std::string value;      // la valeur actuelle ([..], (..), ou vide)
};
using Done = std::function<void(const std::string&)>;
using Host = std::function<void(Spec, Done)>;
void setHost(Host host);
[[nodiscard]] bool available();
void ask(Spec spec, Done done);                   // rien sans hote
// Le type a-t-il des elements a editer ainsi ? (un tableau, une liste, un vecteur, une MAP, un tuple)
[[nodiscard]] bool editable(std::string_view type);
// Les lignes de l'editeur pour une valeur, et le litteral pour des lignes (le type dit [..] ou (..)).
[[nodiscard]] std::string linesOf(std::string_view value);
[[nodiscard]] std::string literalOf(std::string_view type, std::string_view lines);
} // namespace itemseditor

// ---- 1.12.7 : LA VALEUR CALCULEE AU DEMARRAGE d'une constante ou d'une variable ----------------
//  Son code ST (qui ecrit la declaration : Seuils := ...;) et ses locales (une par ligne, sans VAR ni
//  END_VAR) ; `info` : ce que le calcul donne et ses fautes. Comme l'editeur des elements : la
//  grille demande, l'hote ouvre la fenetre ; `done` recoit le code et les locales.
namespace calceditor {
struct Spec {
    std::string title;      // « Valeur calculee de Seuils »
    std::string name;       // Seuils
    std::string type;       // MAP[STRING] OF REAL
    bool        constant{true};
    std::string calc;       // le code actuel (vide : un modele)
    std::string locals;     // les locales, une par ligne
    int         line{0};    // > 0 : ouvrir le code a cette ligne (une faute de la Console, de Compiler)
    std::string info;       // la valeur calculee, les fautes
    // En direct : ce que donnent ce code et ces locales (la valeur au demarrage, l'erreur, les fautes,
    // « Lit : ... », « Lu par : ... »).
    std::function<std::string(const std::string& calc, const std::string& locals)> preview{};
    // L'aide a la saisie du code (Ctrl+Espace) : branche l'editeur ; `locals` rend les locales du moment.
    std::function<void(ui::MultiLineText& code, std::function<std::string()> locals)> assist{};
    // Le deballage d'une locale : sa valeur initiale (vide : celle de son type) ; nulle : `why` dit pourquoi.
    std::function<sim::ObjRef(const std::string& type, const std::string& value, std::string* why)> valueOf{};
};
using Done = std::function<void(const std::string& calc, const std::string& locals)>;
using Host = std::function<void(Spec, Done)>;
void setHost(Host host);
[[nodiscard]] bool available();
void ask(Spec spec, Done done);                   // rien sans hote
// Le code propose a une declaration sans calcul : "Seuils := ...;" avec un exemple selon son type.
[[nodiscard]] std::string starter(std::string_view name, std::string_view type);
} // namespace calceditor

} // namespace app
