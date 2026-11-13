// =============================================================================
//  project/Ladder.hpp - 1.51.0 : un ladder (LD) de Control Expert, en modele
// -----------------------------------------------------------------------------
//  Une section LD garde le XML de Control Expert (domain::keepsSourceXml) :
//  <LDSource nbColumns="11"> ... </LDSource>. Ce modele le LIT en une grille,
//  l'editeur la modifie, et l'ecriture rend du XML de Control Expert :
//
//    - RIEN DE MODIFIE : write() rend le texte lu, octet pour octet (chaque
//      reseau garde sa source ; l'en-tete, les blancs entre les reseaux aussi) ;
//    - UN RESEAU MODIFIE est reecrit avec les conventions du fichier lu (fins de
//      ligne, tabulations, VLink seul ou dans un shortCircuit) ; les autres
//      reseaux restent tels quels. Un attribut inconnu d'un element lu est garde.
//
//  LA GRILLE. Un reseau (networkLD) est une suite de rangees (typeLine) ; une
//  rangee, une suite d'elements qui occupent des colonnes de gauche a droite :
//
//    contact      1 colonne   openContact, closedContact, PContact, NContact
//    coil         1 colonne   coil, notCoil, setCoil, resetCoil, PCoil, NCoil,
//                             callCoil (appel d'un SR), haltCoil
//    HLink        nbCells     un fil horizontal
//    emptyCell    nbCells     des cases vides
//    compareBlock width       [expression] : conduit si l'expression est vraie
//    operateBlock width       [instruction ST] : executee si alimentee
//    control      1 colonne   typeControl="jumpCoil" (->> etiquette), "retCoil"
//    labelCell    1 colonne   l'etiquette (seule sur sa rangee : la cible d'un saut
//                             est la ligne suivante)
//    VLink        1 colonne   une case vide dont le BORD DROIT porte une liaison
//                             verticale, de cette rangee a la suivante
//    shortCircuit             un groupe (un element, une suite de fils) dont le
//                             BORD DROIT porte une liaison verticale : le OU, une
//                             sortie vers plusieurs bobines
//    emptyLine    nbRows      des rangees vides (seul dans sa typeLine)
//
//  Un bloc (FFBBlock) est dans la suite de sa rangee du haut (objPosition
//  posX/posY le dit aussi) et y occupe quelques colonnes (`cols` : ce que la
//  rangee laisse ; width/height sont dans les unites de Control Expert, gardes) ;
//  sous lui, ses rangees portent des cases vides. Ses broches, dans l'ordre de
//  descriptionFFB (EN et ENO compris, s'il y en a), commencent a la rangee
//  posY + 1 : les entrees a gauche, les sorties a droite, une par rangee. Une
//  broche sans parametre effectif prend le fil de sa rangee (entree) ou le
//  donne (sortie). Une fonction (EFB) a un nom d'instance ".1", ".2"...
//  Un commentaire (textBox) est place par sa position.
//
//  Un element que ce modele ne connait pas est garde tel quel (Kind::Raw) et
//  reecrit a sa place.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace project::ladder {

    // Les attributs d'un element tels que lus (valeurs ENCODEES, dans l'ordre du
    // fichier) : un attribut inconnu de ce modele est reecrit tel quel.
    using Attrs = std::vector<std::pair<std::string, std::string>>;

    enum class Kind : std::uint8_t {
        Empty,      // emptyCell
        Wire,       // HLink
        Contact,
        Coil,
        Compare,    // compareBlock : <expression>
        Operate,    // operateBlock : <statement>
        Jump,       // control typeControl="jump" : ->> etiquette
        Return,     // control typeControl="return"
        Label,      // labelCell : l'etiquette du reseau
        Covered,    // la 2e colonne et les suivantes d'un element large (compare, operate, raw)
        Raw,        // un element inconnu, garde tel quel
    };

    enum class ContactType : std::uint8_t { Open, Closed, Rising, Falling };
    enum class CoilType : std::uint8_t { Normal, Negated, Set, Reset, Rising, Falling, Call, Halt };

    // Le texte de Control Expert (typeContact / typeCoil) et l'inverse.
    [[nodiscard]] std::string_view contactTypeName(ContactType) noexcept;
    [[nodiscard]] std::string_view coilTypeName(CoilType) noexcept;
    [[nodiscard]] bool contactTypeFrom(std::string_view, ContactType&) noexcept;
    [[nodiscard]] bool coilTypeFrom(std::string_view, CoilType&) noexcept;

    struct Cell {
        Kind        kind{ Kind::Empty };
        ContactType contact{ ContactType::Open };
        CoilType    coil{ CoilType::Normal };
        std::string name;        // la variable (contact, bobine), l'etiquette (saut, etiquette), le SR (callCoil)
        std::string text;        // l'expression (compare) ou l'instruction (operate)
        int         width{ 1 };  // les colonnes occupees (compare, operate, raw)
        // Fidelite : l'element lu et ses attributs ; vide = cree par l'editeur.
        std::string element;
        Attrs       attrs;
        std::string raw;         // Raw : sa source entiere
        std::string typeText;    // typeContact / typeCoil tel que lu (une graphie inconnue est gardee)

        [[nodiscard]] bool isEmpty() const noexcept { return kind == Kind::Empty; }
        // Ce que l'element LIT ou ECRIT (les references croisees, le renommage).
        [[nodiscard]] bool writesName() const noexcept {
            return kind == Kind::Coil && coil != CoilType::Call && coil != CoilType::Halt;
        }
    };

    enum class PinDir : std::uint8_t { In, Out, InOut };

    struct Pin {
        std::string formal;       // formalParameter
        std::string effective;    // effectiveParameter ; vide : relie au fil de sa rangee
        bool        inverted{ false };
        PinDir      dir{ PinDir::In };
        std::string element;      // inputVariable / outputVariable / inOutVariable, tel que lu
        Attrs       attrs;
    };

    struct Block {
        std::string type;         // typeName : TON, ADD_INT, un DFB
        std::string instance;     // instanceName ; vide : une fonction (EFB)
        int         x{ 0 }, y{ 0 };
        int         width{ 10 }, height{ 3 };   // unites de Control Expert (height : des rangees)
        int         cols{ 2 };                  // les colonnes de la grille qu'il occupe
        bool        enEno{ false };
        int         additionalPins{ 0 };
        std::string execAfter;
        std::vector<Pin> pins;    // dans l'ordre du fichier
        Attrs       attrs;        // FFBBlock, tel que lu
        std::string descAttrsRaw; // fidelite : les attributs de descriptionFFB autres que execAfter
        std::vector<std::string> extras;   // enfants inconnus, tels quels

        // La rangee de chaque broche : la premiere de chaque cote a y + 1.
        [[nodiscard]] int pinRow(std::size_t pin) const noexcept;
        [[nodiscard]] int inputCount() const noexcept;
        [[nodiscard]] int outputCount() const noexcept;
        [[nodiscard]] bool covers(int row, int col) const noexcept {
            return row >= y && row < y + height && col >= x && col < x + cols;
        }
        // Une fonction (EFB) : pas d'instance, ou ".1", ".2"...
        [[nodiscard]] bool isFunction() const noexcept { return instance.empty() || instance.front() == '.'; }
    };

    struct Comment {
        int         x{ 0 }, y{ 0 }, width{ 4 }, height{ 1 };
        std::string text;
        Attrs       attrs;
        std::string raw;          // la source lue ; reecrite telle quelle tant que le texte et la place ne changent pas
        bool        changed{ true };
    };

    struct Network {
        // [rangee][colonne] ; toutes les rangees ont `columns` cases.
        std::vector<std::vector<Cell>> cells;
        // [rangee][frontiere 0..columns] : une liaison verticale de la rangee a
        // la suivante, sur la frontiere `frontiere` (le bord droit de la colonne
        // frontiere - 1 ; 0 : le rail de gauche, qui relie deja tout).
        std::vector<std::vector<std::uint8_t>> links;
        std::vector<Block>   blocks;
        std::vector<Comment> comments;
        std::vector<std::string> extras;   // enfants de networkLD inconnus, tels quels
        // Fidelite : la source du reseau, rendue telle quelle tant que `modified` est faux.
        std::string raw;
        bool        modified{ true };
        int         fileRows{ 0 };       // les rangees ecrites dans le fichier (les vides du bas comprises)
        bool        commentsFirst{ true };   // les textBox avant les rangees (l'ordre du fichier)

        [[nodiscard]] int rows() const noexcept { return static_cast<int>(cells.size()); }
        [[nodiscard]] bool link(int row, int boundary) const noexcept {
            return row >= 0 && row < rows() && boundary >= 0
                && boundary < static_cast<int>(links[static_cast<std::size_t>(row)].size())
                && links[static_cast<std::size_t>(row)][static_cast<std::size_t>(boundary)] != 0;
        }
        // Le bloc qui couvre cette case, ou -1.
        [[nodiscard]] int blockAt(int row, int col) const noexcept;
    };

    struct Diagram {
        int columns{ 11 };
        std::vector<Network> networks;

        // ---- fidelite : de quoi rendre le texte lu a l'identique
        std::string startTag;                 // "<LDSource nbColumns=\"11\">" tel que lu
        std::vector<std::string> gaps;        // le texte avant chaque reseau (gaps[i]) ...
        std::string tail;                     // ... et apres le dernier, jusqu'a </LDSource>
        std::string endTag{ "</LDSource>" };
        bool        selfClosed{ false };      // <LDSource/>
        // Les conventions du fichier (pour reecrire un reseau modifie).
        std::string newline{ "\r\n" };
        std::string indent;                   // l'indentation de <networkLD>
        std::string step{ "\t" };             // un niveau d'indentation
        bool        headerChanged{ false };   // nbColumns change

        // Grandit la grille d'un reseau pour qu'il ait au moins `rows` rangees.
        void ensureRows(Network& n, int rows) const;
        // Un reseau vide (une rangee vide).
        [[nodiscard]] Network makeNetwork(int rows = 1) const;
    };

    // Lit <LDSource ...>...</LDSource>. Un XML illisible : une erreur (la section
    // reste lisible en texte, et non modifiable en schema).
    [[nodiscard]] core::Result<Diagram> parse(std::string_view ldSourceXml);

    // Le XML : le texte lu a l'identique si rien n'a change.
    [[nodiscard]] std::string write(const Diagram& d);

    // Le corps d'une section LD neuve : un reseau vide, aux conventions de Control Expert.
    // `depth` : l'indentation de <LDSource> dans le fichier (2 : section de tache,
    // 3 : section d'unite ou corps de DFB).
    [[nodiscard]] std::string emptySourceXml(int columns = 11, int depth = 2);

    // ---- les noms : references croisees, renommage, statistiques -------------
    struct NameUse {
        std::string name;        // tel qu'ecrit (E64A[6], X1.T, TON_MT)
        bool        write{ false };
        int         network{ 0 }, row{ 0 }, col{ 0 };
    };
    // Les noms LUS et ECRITS : contacts, bobines, expressions, instructions, broches, instances.
    [[nodiscard]] std::vector<NameUse> namesUsed(const Diagram& d);
    // Les noms d'une expression ST (X, X.Y, X[i]), sans mots-cles, litteraux ni appels.
    [[nodiscard]] std::vector<std::string> namesInExpression(std::string_view text);

    // Renomme une variable partout (contacts, bobines, broches, instances, expressions) :
    // `from` seul ou en tete d'un chemin (from.membre, from[i]), sans la casse. Rend le
    // nombre de remplacements ; les reseaux touches sont marques modifies.
    std::size_t renameVariable(Diagram& d, std::string_view from, std::string_view to);
    // Applique `fn` a chaque texte qui nomme des variables (contacts, bobines - un
    // appel de SR compris -, comparaisons, operations, parametres des broches,
    // instances) ; `fn` rend le nombre de remplacements. Les reseaux touches sont
    // marques modifies. Rend le total.
    std::size_t rewriteNames(Diagram& d, const std::function<std::size_t(std::string&)>& fn);
    // Le meme renommage dans un texte ST (une expression, une instruction).
    [[nodiscard]] std::string renameInText(std::string_view text, std::string_view from, std::string_view to,
                                           std::size_t* count = nullptr);

} // namespace project::ladder
