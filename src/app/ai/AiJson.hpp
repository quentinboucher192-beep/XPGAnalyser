// =============================================================================
//  app/ai/AiJson.hpp - l'Assistant IA : un petit JSON (lire, ecrire, garder)
// -----------------------------------------------------------------------------
//  POURQUOI UN DE PLUS. sim/Json.hpp lit et ecrit du JSON, mais dans les valeurs
//  du dialecte ST (sim::ObjRef, des MAP aux cles triees) : il est fait pour
//  JSON_LIRE des scripts de l'IHM. Les fichiers de l'Assistant (IA/*.json, le
//  contrat app/ai/AiPaths.hpp) demandent autre chose :
//    - L'ORDRE DES CLES EST GARDE : un fichier relu puis reecrit reste lisible
//      et se compare a l'oeil ;
//    - UN CHAMP INCONNU SE GARDE : ce qu'une autre version (ou le centre de
//      memoire) a ecrit repart tel quel a la reecriture ;
//    - sans dependance, sans exception qui s'echappe, en UTF-8 (\uXXXX decodes,
//      paires de substitution comprises).
//  Un arbre de valeurs : nul, booleen, nombre, chaine, tableau, objet.
// =============================================================================
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ai::json {

class Value {
public:
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };
    using Member = std::pair<std::string, Value>;

    Value() = default;
    Value(bool b) : type_(Type::Bool), bool_(b) {}                                   // NOLINT : implicite voulu
    Value(int n) : type_(Type::Number), number_(n) {}                                // NOLINT
    Value(std::int64_t n) : type_(Type::Number), number_(static_cast<double>(n)) {}  // NOLINT
    Value(std::uint64_t n) : type_(Type::Number), number_(static_cast<double>(n)) {} // NOLINT
    Value(double n) : type_(Type::Number), number_(n) {}                             // NOLINT
    Value(std::string s) : type_(Type::String), text_(std::move(s)) {}               // NOLINT
    Value(const char* s) : type_(Type::String), text_(s ? s : "") {}                 // NOLINT
    [[nodiscard]] static Value array() { Value v; v.type_ = Type::Array; return v; }
    [[nodiscard]] static Value object() { Value v; v.type_ = Type::Object; return v; }

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool isNull() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool isObject() const noexcept { return type_ == Type::Object; }
    [[nodiscard]] bool isArray() const noexcept { return type_ == Type::Array; }
    [[nodiscard]] bool isString() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool isNumber() const noexcept { return type_ == Type::Number; }
    [[nodiscard]] bool isBool() const noexcept { return type_ == Type::Bool; }

    // Les lectures tolerantes : le mauvais type rend la valeur de repli.
    [[nodiscard]] bool               asBool(bool fallback = false) const noexcept;
    [[nodiscard]] double             asNumber(double fallback = 0) const noexcept;
    [[nodiscard]] std::int64_t       asInt(std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] const std::string& asString() const noexcept;   // "" si ce n'est pas une chaine

    // ---- tableau
    [[nodiscard]] const std::vector<Value>& items() const noexcept { return items_; }
    [[nodiscard]] std::vector<Value>&       items() noexcept { return items_; }
    void push(Value v);                                          // un nul devient un tableau

    // ---- objet (l'ordre d'ajout est garde)
    [[nodiscard]] const std::vector<Member>& members() const noexcept { return members_; }
    [[nodiscard]] std::vector<Member>&       members() noexcept { return members_; }
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;
    [[nodiscard]] Value*       find(std::string_view key) noexcept;
    [[nodiscard]] bool         has(std::string_view key) const noexcept { return find(key) != nullptr; }
    // La valeur de cette cle (nul si absente).
    [[nodiscard]] const Value& at(std::string_view key) const noexcept;
    // La cle, ajoutee a la fin si elle manque (un nul devient un objet).
    Value& operator[](std::string_view key);
    void   set(std::string_view key, Value v) { (*this)[key] = std::move(v); }
    bool   erase(std::string_view key);
    // Raccourcis : la cle lue avec un repli.
    [[nodiscard]] std::string  str(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] double       num(std::string_view key, double fallback = 0) const noexcept;
    [[nodiscard]] std::int64_t integer(std::string_view key, std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] bool         flag(std::string_view key, bool fallback = false) const noexcept;

    bool operator==(const Value& o) const noexcept;

private:
    Type                type_{Type::Null};
    bool                bool_{false};
    double              number_{0};
    std::string         text_;
    std::vector<Value>  items_;
    std::vector<Member> members_;
};

// Lit un texte JSON ; faux et `error` ("ligne 3, colonne 12 : ...") s'il est mal forme.
bool parse(std::string_view text, Value& out, std::string* error = nullptr);
// Ecrit une valeur (indent : deux espaces par niveau, une cle par ligne).
[[nodiscard]] std::string write(const Value& v, bool indent = true);

// Les cles d'un objet qui ne sont pas dans `known` (ce qu'une autre version a ecrit) :
// copiees dans un objet a part, pour etre remises a la reecriture.
[[nodiscard]] Value unknownMembers(const Value& object, std::initializer_list<std::string_view> known);
// Remet ces membres a la fin de `object` (sans ecraser une cle deja la).
void addMembers(Value& object, const Value& extra);

} // namespace app::ai::json
