// =============================================================================
//  hmi/HmiCalculator.hpp - 1.14.1 (lot N4) : LA CALCULATRICE
// -----------------------------------------------------------------------------
//  Un objet de la bibliotheque, trois variantes (sa propriete Mode, "calcMode") :
//    - standard     : 0-9, virgule, + - x /, =, %, +/-, 1/x, x2, racine, C, CE,
//                     retour arriere ; la memoire MC MR M+ M- ;
//    - scientifique : en plus les parentheses, sin cos tan (2nde : leurs inverses),
//                     DEG / RAD, log, ln, 10^x, e^x, x^y, racine y-ieme, n!, pi, e,
//                     mod, |x|, Exp (1,2E+5) ;
//    - programmeur  : un entier montre en HEX, DEC, OCT et BIN (la base active en
//                     evidence), la taille du mot (BYTE, WORD, DWORD, LWORD) et
//                     signe / non signe, A-F, AND OR XOR NOT NAND NOR, << >>,
//                     ROL ROR, SWAP (echange les deux moities du mot), mod, et la
//                     grille des bits (chaque bit se clique).
//  L'ORDRE DES OPERATIONS EST RESPECTE dans les trois (2 + 3 x 4 = 14) : l'expression
//  tapee se lit sur la petite ligne de l'afficheur ; une parenthese fermee se calcule
//  tout de suite, comme une fonction (sin, racine...) sur le nombre en cours.
//
//  Ici, sans ecran et sans moteur : l'etat d'une calculatrice et ses touches
//  (press), ce qu'elle affiche, et la geometrie partagee par le dessin et le clic
//  (layoutOf, hitPart), dans le repere de l'objet. Le moteur (Runtime) garde un
//  etat par calculatrice montree ; hmi_test verifie le calcul.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::calculator {

enum class Mode : std::uint8_t { Standard, Scientific, Programmer };
enum class Base : std::uint8_t { Hex, Dec, Oct, Bin };

// "standard", "scientifique", "programmeur" (sans casse ni accents ; "sci", "prog" aussi). Inconnu : standard.
[[nodiscard]] Mode             modeFrom(std::string_view text);
[[nodiscard]] bool             knownMode(std::string_view text);
[[nodiscard]] std::string_view modeKey(Mode) noexcept;        // "standard", "scientifique", "programmeur"
[[nodiscard]] std::string_view modeLabel(Mode) noexcept;      // "Calculatrice", "Calculatrice scientifique"...
[[nodiscard]] std::string_view baseKey(Base) noexcept;        // "HEX", "DEC", "OCT", "BIN"
[[nodiscard]] bool             baseFrom(std::string_view text, Base& out);
[[nodiscard]] int              baseRadix(Base) noexcept;      // 16, 10, 8, 2
// "BYTE" 8, "WORD" 16, "DWORD" 32, "LWORD" 64 (sans casse ; "8", "16"... aussi) ; 0 : inconnu.
[[nodiscard]] int              wordBits(std::string_view text);
[[nodiscard]] std::string_view wordName(int bits) noexcept;
// Le type IEC d'un mot : INT, UINT, SINT, USINT, DINT, UDINT, LINT, ULINT.
[[nodiscard]] std::string_view wordType(int bits, bool isSigned) noexcept;

// Ce que l'objet regle (ses proprietes, evaluees) : le moteur part de la ; l'operateur bascule ensuite DEG / RAD,
// la base, la taille du mot et le signe a son gre.
struct Settings {
    Mode mode{Mode::Standard};
    bool degrees{true};
    int  bits{16};
    bool isSigned{true};
    int  decimals{10};          // decimales montrees au plus (0..15)
    Base base{Base::Dec};
    bool operator==(const Settings&) const = default;
};
[[nodiscard]] Settings settingsOf(const Object&);

// Un element de l'expression tapee (la petite ligne de l'afficheur).
struct Token {
    enum class Type : std::uint8_t { Number, Op, Open };
    Type          type{Type::Number};
    std::string   text;            // tel qu'affiche : "12,5", "sin(30)", "(" ; un operateur : "+", "mod"...
    double        value{0};        // un nombre (standard, scientifique)
    std::uint64_t word{0};         // un nombre (programmeur) : ses bits, dans la taille du mot
    bool          plain{false};    // un nombre tape tel quel : son texte suit la base (programmeur)
    std::string   op;              // un operateur : "+", "-", "*", "/", "pow", "root", "mod", "and", "or", "xor",
                                   // "nand", "nor", "shl", "shr", "rol", "ror"
};

struct State {
    Settings           settings;            // en cours (l'operateur bascule DEG / RAD, la base, le mot, le signe)
    Settings           fromObject;          // ce que disaient les proprietes la derniere fois (applySettings)
    std::vector<Token> tokens;              // l'expression validee : nombres, operateurs, parentheses ouvertes
    std::string        entry;               // le nombre en cours de frappe ("12,5", "1,2E+5", "FF") ; vide : aucun
    bool               hasOperand{false};   // un nombre calcule attend son operateur (une fonction, pi, MR, un resultat)
    Token              operand;
    bool               afterEquals{false};  // la derniere touche etait = : la petite ligne montre "12 + 3 ="
    std::string        equalsLine;
    std::string        repeatOp;            // = encore : refait la derniere operation (2 + 3 = = : 8)
    Token              repeatOperand;
    bool               second{false};       // 2nde : sin cos tan deviennent leurs inverses
    std::string        error;               // "" : pas d'erreur ; sinon pourquoi (la grande ligne dit Erreur)
    double             memory{0};
    std::uint64_t      memoryWord{0};
    bool               hasMemory{false};
    // La touche enfoncee (l'ecran l'enfonce un instant) ; les messages (Envoye, refuse) passent par le moteur.
    std::string        lastKey;
    double             lastKeyAt{-1e9};
};

[[nodiscard]] State initialState(const Settings&);
// Les proprietes ont change (une formule, l'inspecteur) : ce qui a change s'applique, le calcul reste.
void applySettings(State&, const Settings&);

// UNE TOUCHE (son identifiant, celui des parties "touche:<id>") :
//   chiffres "0".."9", "A".."F" (programmeur), "," (virgule), "neg" (+/-), "+", "-", "*", "/", "=", "%",
//   "inv" (1/x), "sqr" (x2), "sqrt" (racine), "C", "CE", "back" (retour arriere), "MC", "MR", "M+", "M-",
//   "(", ")", "2nd", "sin", "cos", "tan" (2nde : asin, acos, atan ; "asin"... directement aussi), "pi", "e",
//   "pow" (x^y), "root" (racine y-ieme), "pow10", "powe", "log", "ln", "fact" (n!), "abs", "mod", "exp" (Exp),
//   "and", "or", "xor", "not", "nand", "nor", "shl", "shr", "rol", "ror", "swap",
//   "angle" (DEG / RAD), "word" (la taille suivante), "sign" (signe / non signe).
// Vrai : la touche a agi (une touche grisee, ou inconnue, ne fait rien).
bool press(State&, std::string_view key);
[[nodiscard]] bool keyEnabled(const State&, std::string_view key);
// La touche d'un caractere tape au clavier ("" : aucune) : chiffres, + - * / ^ ( ) . , = % ! ; en programmeur
// A-F, & | ~ < > (^ : XOR).
[[nodiscard]] std::string keyForChar(const State&, char c);

void setBase(State&, Base);
void setWord(State&, int bits);              // la valeur montree est tronquee a la nouvelle taille
void setSigned(State&, bool isSigned);
bool toggleBit(State&, int bit);             // faux : hors du mot
// Une valeur venue d'ailleurs (Valeur de depart, une expression) : elle remplace le nombre en cours.
void load(State&, double value);
// ... un entier tel quel (programmeur : ses bits, sans passer par un double ; ailleurs : sa valeur).
void loadWord(State&, std::int64_t value);

// CE QUI SE MONTRE
[[nodiscard]] double        value(const State&);        // la grande ligne, en nombre
[[nodiscard]] std::uint64_t wordValue(const State&);    // ... en programmeur : ses bits
[[nodiscard]] std::int64_t  signedValue(const State&);  // ... lue selon le signe (non signe : le mot tel quel)
[[nodiscard]] std::string   displayText(const State&);  // la grande ligne ("Erreur" apres une erreur)
[[nodiscard]] std::string   expressionText(const State&);
// Un mot dans une base : HEX "1F2E", BIN "0001 1111" (groupe par 4 : `grouped`), DEC lu selon le signe.
[[nodiscard]] std::string   baseText(std::uint64_t word, Base, int bits, bool isSigned, bool grouped = true);
// Un nombre a la francaise : "12 345,678", "1,2E+20" ; `decimals` au plus ; `group` : les milliers.
[[nodiscard]] std::string   formatNumber(double v, int decimals, bool group = true);
// "12,5", "1.5", "1,2E+5", "-3" -> nombre (faux : illisible).
[[nodiscard]] bool          parseEntry(std::string_view text, double& out);
[[nodiscard]] std::uint64_t maskOf(int bits) noexcept;
[[nodiscard]] std::int64_t  signExtend(std::uint64_t word, int bits) noexcept;

// ---- LA GEOMETRIE (le dessin et le clic) -------------------------------------
struct Key {
    enum class Role : std::uint8_t { Digit, Operator, Equals, Function, Memory, Toggle, Send };
    std::string id;           // "7", "sin", "MC", "angle"...
    std::string label;        // "7", "sin", "MC", "DEG"
    std::string sup;          // un exposant apres le libelle ("y" de x^y, "-1" de sin-1)
    std::string pre;          // ... avant (le "y" de la racine y-ieme)
    Role        role{Role::Function};
    Box         box;
    bool        on{false};    // une bascule allumee (2nde)
};
struct Layout {
    Mode             mode{Mode::Standard};
    Box              display;       // l'afficheur
    Box              expression;    // sa petite ligne (en haut, calee a droite)
    Box              result;        // sa grande ligne
    Box              indicators;    // en haut a gauche : M, DEG / RAD, le mot
    std::vector<Box> baseRows;      // programmeur : HEX, DEC, OCT, BIN
    std::vector<Box> bits;          // programmeur : la case du bit i (bit 0 a droite, en bas)
    std::vector<Box> bitGroups;     // ... sous chaque groupe de 4, la place de son numero (le bit le plus bas)
    std::vector<int> bitGroupFirst; // ... ce numero
    Box              strip;         // la rangee du haut : memoire, bascules, Charger, Envoyer
    std::vector<Key> stripKeys;
    std::vector<Key> keys;          // le pave
    double           keyFont{14};
    double           stripFont{11};
    double           resultFont{28};
    double           expressionFont{12};
    double           baseFont{11};
    double           bitFont{10};
};
// La geometrie d'une calculatrice de w x h, telle que l'etat la montre (son mode, sa taille de mot, la 2nde).
// `startValue`, `resultTo` : les boutons Charger (fleche) et Envoyer, s'ils sont regles.
[[nodiscard]] Layout layoutOf(const State&, double w, double h, bool startValue, bool resultTo);
[[nodiscard]] Layout layoutOf(const Object&, const State&, double w, double h);
// Ce qui est sous un point local : "touche:7", "touche:MC", "base:HEX", "bit:12", "afficheur" ; "" : rien.
[[nodiscard]] std::string hitPart(const Layout&, double x, double y);
// La boite d'une partie (les essais, les exemples animes) ; faux : pas de telle partie.
[[nodiscard]] bool        partBox(const Layout&, std::string_view part, Box& out);
// La taille par defaut et la taille minimale lisible de chaque variante.
[[nodiscard]] Box         defaultSize(Mode) noexcept;
[[nodiscard]] Box         minimumSize(Mode) noexcept;

} // namespace hmi::calculator
