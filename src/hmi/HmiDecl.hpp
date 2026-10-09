// =============================================================================
//  hmi/HmiDecl.hpp - 1.11.18 (refonte des scripts, lot 2) : LES DECLARATIONS D'UN
//  CODE ST, LUES SANS PERTE
// -----------------------------------------------------------------------------
//  Les blocs VAR, VAR_TEMP, VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT (et leurs
//  qualificatifs CONSTANT, RETAIN, NON_RETAIN, PERSISTENT) d'un script, d'une
//  fonction, d'un operateur ou d'une redefinition :
//    - chaque declaration : plusieurs noms par ligne (a, b : INT), le type et la
//      valeur initiale en texte, le commentaire rattache, la ligne et la colonne
//      du nom ;
//    - les commentaires du bloc qui ne sont a aucune declaration ;
//    - les fonctions internes (FUNCTION Nom(a : T) : R ... END_FUNCTION, dialecte
//      1.10) : leurs parametres d'en-tete et leurs blocs ;
//    - le code sans les blocs de premier niveau (blanchis : lignes et colonnes
//      gardees, comme hmi::splitDeclarations).
//  Une lecture de SURFACE : rien n'est execute ni type ici (le simulateur et
//  scriptcheck le font), aucune declaration n'est perdue (un commentaire, une
//  chaine qui contient ';', un repere $...$ restent a leur place). C'est elle que
//  liront la migration des blocs (lot 4) et les grilles de declarations (lot 5) ;
//  la signature du pipeline et l'arbre du projet la lisent deja.
//
//  Dessous, LE LEXER DE SURFACE partage (lex) : les jetons d'un code ST de l'IHM
//  avec leur place, commentaires compris. Il suit les regles du lexeur du
//  simulateur (sim/Interpreter.cpp), qu'il ne remplace pas : celui-la lit aussi le
//  code de l'automate, et il jette les commentaires.
// =============================================================================
#pragma once

#include "HmiScript.hpp"   // ScriptDiagnostic

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::decl {

// ---- Le lexer ----------------------------------------------------------------
//  Une chaine '...' ou "..." et ses echappements ($' $$ $N $0D...), un commentaire
//  (* ... *) (non imbrique) ou // jusqu'a la fin de la ligne : comme le simulateur.
//  Les blancs ne sont pas des jetons. Un repere $...$ se lit tel quel ($ : une
//  ponctuation) ; un litteral type (T#5s, 16#FF, E_Mode#Auto) : un seul jeton.
enum class TokenKind : std::uint8_t { Word, Number, String, Comment, Punct };
struct Token {
    TokenKind   kind{TokenKind::Punct};
    std::size_t begin{0};           // ses octets [begin, end)
    std::size_t end{0};
    int         line{0};            // sa ligne (1 = la premiere)...
    int         column{0};          // ... et sa colonne (1 = le premier octet de la ligne)
    int         lastLine{0};        // la ligne de son dernier octet (un commentaire sur plusieurs lignes)
    [[nodiscard]] std::string_view text(std::string_view code) const { return code.substr(begin, end - begin); }
};
// Les ponctuations de deux octets : := => <= >= <> ** .. ; les autres : un octet.
[[nodiscard]] std::vector<Token> lex(std::string_view code);
// Le texte d'un commentaire, sans (* *) ni //, blancs resserres ("" : vide).
[[nodiscard]] std::string commentText(std::string_view comment);

// ---- Les declarations --------------------------------------------------------
enum class Section : std::uint8_t { Var, Temp, Input, InOut, Output };
// "VAR", "VAR_TEMP", "VAR_INPUT", "VAR_IN_OUT", "VAR_OUTPUT".
[[nodiscard]] std::string_view keyword(Section) noexcept;
// Un parametre (Input, InOut, Output) ; faux : une variable (Var, Temp).
[[nodiscard]] bool isParameter(Section) noexcept;

// Decl::block d'un parametre ecrit dans l'en-tete d'une fonction interne : FUNCTION F(a : T).
inline constexpr std::size_t kHeader = static_cast<std::size_t>(-1);

struct Decl {
    std::string name;               // tel qu'ecrit
    std::string type;               // le texte du type, blancs resserres, commentaires otes
    std::string initial;            // le texte de la valeur initiale ("" : aucune)
    std::string comment;            // le commentaire rattache (voir plus bas) ; "" : aucun
    Section     section{Section::Var};
    bool        constant{false};
    bool        retain{false};
    int         line{0};            // le nom : sa ligne (1 = la premiere)...
    int         column{0};          // ... et sa colonne (1 = le premier octet de la ligne)
    std::size_t block{0};           // l'indice de son bloc (kHeader : l'en-tete d'une fonction interne)
    std::size_t group{0};           // les noms d'une meme declaration (a, b : INT) partagent leur groupe
};

//  LE COMMENTAIRE D'UNE DECLARATION : ceux juste au-dessus d'elle (sans ligne vide
//  entre), ceux de dedans (a (* x *) : INT) et ceux qui suivent son ';' sur sa ligne,
//  joints par une espace, sans (* *) ni //, blancs resserres. Les autres (plus haut,
//  apres une ligne vide ; sur la ligne du mot VAR ; avant END_VAR) : ceux du bloc.
struct Block {
    Section                  section{Section::Var};
    bool                     constant{false};
    bool                     retain{false};
    std::vector<std::string> qualifiers;   // apres le mot, tels qu'ecrits : "CONSTANT", "RETAIN", "NON_RETAIN"...
    std::size_t              begin{0};     // les octets du bloc : du mot VAR...
    std::size_t              end{0};       // ... jusqu'apres END_VAR (et son ';')
    int                      firstLine{0};
    int                      lastLine{0};
    bool                     closed{true};
    int                      errors{0};    // ses fautes (dans `Extract::errors`) : 0, il se recompose sans perte
    std::vector<std::string> comments;     // ses commentaires rattaches a aucune declaration
};

struct InnerFunction {
    std::string        name;
    std::string        returnType;         // "" : sans retour
    std::vector<Block> blocks;             // ses blocs
    std::vector<Decl>  decls;              // ses parametres d'en-tete, puis les declarations de ses blocs
    std::size_t        begin{0};           // du mot FUNCTION...
    std::size_t        end{0};             // ... jusqu'apres END_FUNCTION
    int                firstLine{0};
    int                lastLine{0};
};

struct Extract {
    std::vector<Block>            blocks;      // le premier niveau (hors des fonctions internes)
    std::vector<Decl>             decls;       // le premier niveau, dans l'ordre
    std::vector<InnerFunction>    functions;
    std::string                   body;        // le code, les blocs du premier niveau blanchis
    std::vector<ScriptDiagnostic> errors;      // un bloc sans END_VAR, une declaration illisible
    // Les declarations d'une section, dans l'ordre (des pointeurs dans `decls`).
    [[nodiscard]] std::vector<const Decl*> of(Section) const;
    // Les parametres (Input, InOut, Output), dans l'ordre.
    [[nodiscard]] std::vector<const Decl*> parameters() const;
    // Une declaration du premier niveau, par son nom (sans casse) ; nulle : aucune.
    [[nodiscard]] const Decl* find(std::string_view name) const noexcept;
};

[[nodiscard]] Extract extract(std::string_view code);

// Le texte d'un bloc recompose de ses declarations (celles dont `block` vaut `index`) :
//   VAR CONSTANT
//       Max, Min : INT := 10;   (* les bornes *)
//   END_VAR
// Une ligne par groupe (ses noms ensemble), le commentaire rattache en fin de ligne,
// ceux du bloc en tete. Relu par extract, il redonne les memes declarations.
[[nodiscard]] std::string compose(const Block&, const std::vector<Decl>& decls, std::size_t index,
                                  std::string_view indent = "    ");

// La signature des parametres, sous une forme canonique : "A : REAL; B : REAL := 0.5;
// VAR_IN_OUT V : T_VEC;" (VAR_INPUT sans son mot ; les autres sections le disent). Deux
// codes aux memes parametres (commentaires, blancs, ordre des lignes de code a part)
// rendent la meme signature : l'empreinte d'interface du build (pipeline::functionIface).
[[nodiscard]] std::string parameterSignature(const Extract&);

// ---- 1.11.18 (refonte des scripts, lot 3) : LE PONT (voie A) ---------------------------
//  Les declarations du modele (hmi::Declaration, HmiModel.hpp) rendues en texte pour le
//  moteur et les controles, qui lisent du texte : les blocs VAR... END_VAR reconstruits
//  devant le corps, SUR SA PREMIERE LIGNE. Les lignes du corps ne bougent pas ; seules les
//  colonnes de sa ligne 1 glissent de `prefix`. Un code sans declaration du modele : son
//  corps, tel quel - un projet d'avant la 1.11.18 se lit et tourne a l'identique.
//    Script   : constante VAR CONSTANT ; variable Execution VAR_TEMP, Conservee et
//               Persistante VAR (un parametre : VAR_INPUT, que le controle refuse) ;
//    Function : parametres VAR_INPUT / VAR_IN_OUT / VAR_OUTPUT dans leur ordre, constante
//               VAR CONSTANT, variable VAR (une fonction n'a pas de memoire).
enum class Role : std::uint8_t { Script, Function, Operator };   // Operator : comme Function, sans parametre (A, B)
struct Composed {
    std::string text;                  // les blocs reconstruits, puis le corps
    std::size_t prefix{0};             // les octets ajoutes devant la ligne 1 du corps
    struct Span {
        Id          id{kNoId};
        std::size_t begin{0};          // "Nom : TYPE := valeur; " dans `text`
        std::size_t end{0};
    };
    std::vector<Span> spans;
    // Une colonne (1 = le premier octet) de `text`, sur la ligne `line` : dans les
    // declarations reconstruites ? Sinon, sa colonne dans le corps.
    [[nodiscard]] bool inDeclarations(int line, int column) const noexcept;
    [[nodiscard]] int  bodyColumn(int line, int column) const noexcept;
    [[nodiscard]] Id   declarationAt(int line, int column) const noexcept;   // kNoId : aucune
};
// `inherited` : une redefinition lit les parametres de la fonction redefinie.
[[nodiscard]] Composed composeCode(std::string_view body, const std::vector<Declaration>& decls, Role,
                                   const std::vector<Declaration>* inherited = nullptr);
// Le texte complet d'un code (un script C ou C++ : son corps).
[[nodiscard]] std::string codeOf(const Script&);
[[nodiscard]] std::string codeOf(const HmiFunction&);
[[nodiscard]] std::string codeOf(const FunctionOverride&, const HmiFunction* base);
[[nodiscard]] std::string codeOf(const HmiOperator&);
// Les memes, sans copie quand il n'y a rien a reconstruire (le moteur, a chaque appel) :
// le corps lui-meme, sinon `storage`, qui garde le texte reconstruit.
[[nodiscard]] const std::string& codeOf(const Script&, std::string& storage);
[[nodiscard]] const std::string& codeOf(const HmiFunction&, std::string& storage);
// Le bloc ou une declaration se reconstruit : "VAR CONSTANT", "VAR_TEMP", "VAR_INPUT"...
[[nodiscard]] std::string_view blockOf(const Declaration&, Role) noexcept;

// Les fautes des declarations elles-memes, sans le moteur : un nom vide, illisible,
// reserve ou en double (dans le modele, ou aussi declare dans un bloc VAR du corps :
// "declare deux fois") ; un type manquant ou non pris en charge ; une constante sans
// valeur ; un parametre dans un script ou dans une redefinition (elle garde ceux de sa
// fonction) ; une variable Conservee ou Persistante dans une fonction (sans memoire).
// Ligne 0 (tout le code) ; le message nomme la declaration. `valid` : celles qui sont
// justes (le controle du corps se fait avec elles seules : une faute de declaration ne
// devient pas une faute de sa ligne 1).
[[nodiscard]] std::vector<ScriptDiagnostic> checkDeclarations(const std::vector<Declaration>& decls, Role,
                                                              std::string_view body, const TypeKnown& knownType = {},
                                                              const std::vector<Declaration>* inherited = nullptr,
                                                              std::vector<Declaration>* valid = nullptr);

} // namespace hmi::decl
