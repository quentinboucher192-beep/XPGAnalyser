// =============================================================================
//  hmi/HmiExpr.hpp — les expressions des vues : compilees une fois, evaluees
//                    a chaque cycle contre le simulateur
// -----------------------------------------------------------------------------
//  LE LANGAGE EST LE ST DU SIMULATEUR, PAS UN SECOND LANGAGE. Une expression
//  "Pression > Consigne * 1.2" ou "Pompes[i + 15 + y * 2].Valid" est compilee
//  par sim::parse, sous la forme "__hmi_r := <expression>;", et executee par
//  sim::execute. Ce que le ST du simulateur sait lire, une vue le sait aussi :
//  index calcules, membres de structures, T#5s, 16#FF, INT_TO_REAL(...), et la
//  meme arithmetique automate (un INT boucle a 32767 ici comme dans l'API).
//
//  LES NOMS LOCAUX : une vue ou un composant peut definir des noms qui
//  n'existent pas dans l'automate - un index `i`, un parametre `Moteur` relie
//  a `Pompes[3]`. `Scope` les porte : une valeur (i = 3), ou un ALIAS de
//  chemin (Moteur -> Pompes[3], donc Moteur.Active -> Pompes[3].Active). C'est
//  ainsi qu'une expression accepte une STRUCT : on la designe, on lit ses
//  membres.
//
//  LECTURE SEULE : une expression ne contient ni ";" ni ":=" - elle ne peut
//  pas ecrire dans l'automate. Les ecritures passent par les actions.
//
//  UN TEXTE A TROUS : "Temperature : {Temperature} degC", "{Niveau:0.0} %".
//  Chaque {...} est une expression, avec un format facultatif apres ':'.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../sim/Interpreter.hpp"
#include "../sim/Value.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

class Scope {
public:
    explicit Scope(const Scope* parent = nullptr) : parent_(parent) {}
    void setValue(std::string name, sim::Value v);
    void setAlias(std::string name, std::string path);
    [[nodiscard]] const sim::Value*  value(std::string_view name) const;
    [[nodiscard]] const std::string* alias(std::string_view name) const;
    // "Moteur.Active" -> "Pompes[3].Active" ; un nom sans alias revient tel quel.
    [[nodiscard]] std::string resolve(std::string_view path) const;
    // Lot 8 : les noms sont sans casse ; ceux que porte la portee (en majuscules).
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::vector<std::string> names() const;
private:
    const Scope* parent_;
    std::map<std::string, sim::Value, std::less<>>  values_;
    std::map<std::string, std::string, std::less<>> aliases_;
};

// Lot 7 : LES FONCTIONS IHM DU PROJET dans une expression de vue. Un
// environnement qui en porte (le moteur de l'IHM) le dit ici ; l'expression
// les appelle alors comme les fonctions standard. `callFromExpression` les
// execute en LECTURE SEULE : elles calculent, elles n'ecrivent rien hors de
// leurs variables locales et n'ouvrent aucune vue.
class FunctionHost {
public:
    virtual ~FunctionHost() = default;
    [[nodiscard]] virtual bool hostsFunction(std::string_view name) const = 0;
    virtual bool callFromExpression(std::string_view name, const std::vector<std::pair<std::string, sim::Value>>& args,
                                    sim::Value& result) = 0;
};

class Expression {
public:
    Expression() = default;
    [[nodiscard]] static Expression compile(std::string_view source);

    [[nodiscard]] bool               empty() const noexcept { return source_.empty(); }
    [[nodiscard]] bool               valid() const noexcept { return program_ != nullptr; }
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    // Les racines des noms lus : "Pompes[i + 15].Valid" -> Pompes, i.
    [[nodiscard]] const std::vector<std::string>& roots() const noexcept { return roots_; }

    [[nodiscard]] core::Result<sim::Value> evaluate(sim::Environment& plc, const Scope* scope = nullptr) const;

private:
    std::string                   source_;
    std::string                   error_;
    std::shared_ptr<sim::Program> program_;
    std::vector<std::string>      roots_;
};

class TextTemplate {
public:
    [[nodiscard]] static TextTemplate compile(std::string_view text);
    [[nodiscard]] bool dynamic() const noexcept;
    [[nodiscard]] std::string render(sim::Environment& plc, const Scope* scope = nullptr) const;
    // Le texte tel qu'on le montre dans l'editeur, sans simulation : les trous
    // restent visibles ("Temperature : {Temperature} degC").
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    [[nodiscard]] std::vector<std::string> errors() const;
    [[nodiscard]] std::vector<std::string> roots() const;
private:
    struct Piece {
        bool        isExpr{false};
        std::string literal;
        Expression  expr;
        std::string format;
    };
    std::string        source_;
    std::vector<Piece> pieces_;
};

// LES FORMATS D'UN TROU {Expression:format} :
//   ""          l'affichage du type
//   0  0.0  0.00        decimales ; 000 : au moins 3 chiffres (007) ; + : le signe
//   0.##        2 decimales au plus (les zeros de fin tombent)
//   0.0%        pourcentage (x 100)
//   x  X  X4    hexadecimal (4 chiffres au moins)
//   b  b8       binaire
//   e  e2       scientifique
//   t           une duree (TIME, ou des secondes) : "2 min 05 s"
//   Oui|Non     le texte d'un booleen : vrai, puis faux
[[nodiscard]] std::string formatValue(const sim::Value&, std::string_view format = {});
// Ce qui ressemble a un format (et pas a la fin d'une expression).
[[nodiscard]] bool looksLikeFormat(std::string_view) noexcept;
// "\n" ecrit dans un texte : un retour a la ligne.
[[nodiscard]] std::string unescapeText(std::string_view);
// Une fonction standard sans etat (ABS, MIN, LIMIT, INT_TO_REAL...) : ce
// qu'une expression de vue peut appeler de l'automate (lot 7 : et le banc
// d'essai des fonctions IHM).
[[nodiscard]] bool isStandardFunction(std::string_view name);

// Les racines lues par une expression, sans la compiler.
[[nodiscard]] std::vector<std::string> scanRoots(std::string_view source);
// Refuse ce qui ecrirait : ";" et ":=" hors chaines et commentaires.
[[nodiscard]] bool isReadOnly(std::string_view source, std::string* why = nullptr);

} // namespace hmi
