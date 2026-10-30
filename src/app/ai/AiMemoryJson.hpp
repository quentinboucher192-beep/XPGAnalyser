// =============================================================================
//  app/ai/AiMemoryJson.hpp - le JSON des fichiers de l'IA (le centre de memoire)
// -----------------------------------------------------------------------------
//  Les fichiers du dossier IA/ (AiPaths.hpp) sont du JSON : une conversation,
//  une connaissance, l'etat du centre de memoire ; le journal d'activite est du
//  JSON en lignes. Le centre les LIT (les conversations : ecrites par
//  l'Assistant), en ECRIT certains (les connaissances, l'etat) et doit garder ce
//  qu'il ne connait pas : un champ inconnu se relit et se reecrit tel quel.
//
//  D'ou une valeur JSON toute simple, sans dependance : null, vrai/faux, un
//  nombre (son texte d'origine garde : 1 reste 1), une chaine (UTF-8, \uXXXX
//  decodes, paires de substitution comprises), un tableau, un objet dont les
//  cles restent DANS L'ORDRE du fichier. La lecture refuse un texte mal forme
//  (la position dite), trop imbrique (64 niveaux) ; elle ne leve rien.
//  (sim/Json.hpp est celui du dialecte IHM, porte par ses valeurs riches.)
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ai::memory {

struct Json {
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };
    Type                                      type{Type::Null};
    bool                                      boolean{false};
    double                                    number{0.0};
    std::string                               text;      // une chaine ; un nombre : son texte d'origine
    std::vector<Json>                         items;     // un tableau
    std::vector<std::pair<std::string, Json>> fields;    // un objet, dans l'ordre

    [[nodiscard]] static Json makeString(std::string s);
    [[nodiscard]] static Json makeNumber(double v);
    [[nodiscard]] static Json makeBool(bool v);
    [[nodiscard]] static Json makeObject();
    [[nodiscard]] static Json makeArray();

    [[nodiscard]] bool isObject() const noexcept { return type == Type::Object; }
    [[nodiscard]] bool isArray() const noexcept { return type == Type::Array; }
    [[nodiscard]] bool isString() const noexcept { return type == Type::String; }

    // Un champ d'un objet (nul : absent, ou pas un objet).
    [[nodiscard]] const Json* get(std::string_view key) const noexcept;
    [[nodiscard]] Json*       get(std::string_view key) noexcept;
    // Remplace le champ, ou l'ajoute a la fin (l'objet le devient s'il ne l'etait pas).
    Json& set(std::string key, Json value);
    bool  erase(std::string_view key);

    // Lire un champ, avec une valeur par defaut quand il manque ou n'est pas du bon genre.
    [[nodiscard]] std::string str(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] double      num(std::string_view key, double fallback = 0.0) const;
    [[nodiscard]] bool        flag(std::string_view key, bool fallback = false) const;
};

// Lit un texte JSON. Faux : mal forme (`why` : "position 12 : ...").
bool parseJson(std::string_view text, Json& out, std::string* why = nullptr);
// Ecrit une valeur. `indent` : deux espaces par niveau (les fichiers) ; sinon une ligne (le journal).
[[nodiscard]] std::string writeJson(const Json& value, bool indent);

} // namespace app::ai::memory
