// =============================================================================
//  hmi/HmiScriptCheck110.hpp - 1.10 (S1) : ce que le dialecte IHM ajoute au
//  controle des scripts et a l'aide a la saisie
// -----------------------------------------------------------------------------
//  Le controle des scripts (chantier N, HmiScriptCheck.*) suit les noms, les
//  membres, les appels et les types du ST. Les constructions du dialecte IHM
//  (fonctions internes, references, pointeurs, tableaux a N dimensions, MAP,
//  FOR EACH) sont controlees ICI, et N appelle `analyze` :
//    - les fonctions internes d'un script (signature, lignes) : leurs noms ne
//      sont pas "fonction inconnue", leurs parametres et locales sont connus
//      dans leurs lignes ;
//    - les noms que declarent FOR EACH (k, v) sur les lignes de la boucle ;
//    - les fautes propres au dialecte, avec ligne et colonne.
//
//  L'aide a la saisie (chantier K) y prend les fonctions du dialecte (MAP_...,
//  REF, ADR, LOWER_BOUND...) et les modeles (FUNCTION, FOR EACH...).
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {
struct Project;
}

namespace hmi::lang110 {

using EnumValuesFn = std::function<std::vector<std::pair<std::string, std::int64_t>>(std::string_view)>;

// 1.10 : un type IHM du projet (structure ou enumeration) - le `knownType` des
// declarations (une locale, un parametre, un retour de ce type est permis).
[[nodiscard]] bool knownHmiType(const Project* project, std::string_view type);
// Les valeurs des enumerations du projet, pour `analyze`.
[[nodiscard]] EnumValuesFn enumValuesOf(const Project* project);

struct Param {
    enum class Mode : std::uint8_t { Value, InOut, Output } mode{Mode::Value};
    std::string name, type;
};

struct InnerFunction {
    std::string        name, returnType;     // returnType vide : pas de valeur rendue
    std::vector<Param> params;
    std::vector<Param> locals;               // VAR / VAR_TEMP de la fonction
    int                firstLine{0}, lastLine{0};
    [[nodiscard]] std::string signature() const;   // "Moyenne(t : ARRAY[0..9] OF REAL; n : INT) : REAL"
};

struct LocalName {
    std::string name, type;                  // type : vide si inconnu
    int         firstLine{0}, lastLine{0};
};

struct Finding {
    int         line{0}, column{0}, length{0};
    std::string message;
    bool        error{true};                 // faux : avertissement
    // 1.10 (decision 15) : une correction proposee ("Ajouter les valeurs manquantes") :
    // inserer `fixText` au debut de la ligne `fixLine` (0 : pas de correction).
    std::string fixLabel{};
    int         fixLine{0};
    std::string fixText{};
};

struct Analysis {
    std::vector<InnerFunction> functions;
    std::vector<LocalName>     names;
    std::vector<Finding>       findings;
    [[nodiscard]] const InnerFunction* function(std::string_view name) const noexcept;   // sans casse
};

// `known(nom)` : le nom existe-t-il hors du script (IHM, automate, SYS., parametre
// de vue, fonction du projet) ? `typeOf(nom)` : son type ("REAL", "T_FOUR",
// "ARRAY[0..9] OF REAL"), vide si inconnu. L'un et l'autre peuvent etre vides.
// 1.10 (decision 15) : `enumValues(type)` : les valeurs d'une enumeration IHM du
// projet, dans l'ordre (vide : pas une enumeration) - `enumValuesOf(projet)`.
using EnumValues = EnumValuesFn;
// `project` (facultatif) : les fonctions IHM du projet (un entier passe a un parametre enumeration).
[[nodiscard]] Analysis analyze(std::string_view code, const std::function<bool(std::string_view)>& known = {},
                               const std::function<std::string(std::string_view)>& typeOf = {},
                               const EnumValues& enumValues = {}, const Project* project = nullptr);

// ---- pour l'aide a la saisie (chantier K) ----
struct Builtin {
    std::string_view name;        // "MAP_HAS"
    std::string_view signature;   // "MAP_HAS(m, cle) : BOOL"
    std::string_view help;        // ce qu'elle fait, une phrase
};
[[nodiscard]] const std::vector<Builtin>& builtins();     // MAP_..., REF, ADR, LOWER_BOUND, UPPER_BOUND, SIZEOF
[[nodiscard]] const std::vector<Builtin>& snippets();     // FUNCTION, FOR EACH, ARRAY, MAP, REF_TO, POINTER TO...
[[nodiscard]] bool isBuiltin(std::string_view name) noexcept;   // sans casse

} // namespace hmi::lang110
