// =============================================================================
//  project/TypeUsage.hpp - lot API 5 : qui se sert de quoi
// -----------------------------------------------------------------------------
//  Les onglets Types derives, Blocs DFB, Unites de programme, Variables et
//  Sous-routines posent tous la meme question : « qui s'en sert ? ».
//
//   * un TYPE DERIVE (DDT) : les variables globales de ce type, les unites et
//     les DFB qui en declarent, les autres DDT qui l'ont comme champ -
//     « 1 variable · 3 unites · TOUTFERMER », « dans armoire » ;
//   * un BLOC DFB : ses instances, globales ou locales a une unite, et les
//     sections qui les appellent ;
//   * une VARIABLE : les sections qui la nomment (un index fait une fois pour
//     tout le projet - le recalculer par variable ferait 251 parcours), son
//     genre (instance de DFB, bloc standard, instance de DDT, situee, autre) ;
//   * une SOUS-ROUTINE : les sections qui l'appellent.
//
//  Tout est calcule a la demande, sans rien garder : le projet change a chaque
//  commande et un volet refait ses lignes a chaque rafraichissement.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace project::usage {

// Le type « de base » d'une variable : l'element pour un tableau
// (« ARRAY[0..1] OF armoire » -> « armoire »), sinon le type lui-meme.
[[nodiscard]] std::string baseTypeOf(const domain::Project&, const domain::Variable&);

// Les sections qui nomment chaque identifiant : le code lu une fois (commentaires
// et chaines sautes), chaque identifiant en minuscules, et pour « a.b[1].c »
// la racine « a » seulement.
class Where {
public:
    explicit Where(const domain::Project&);
    [[nodiscard]] const std::vector<domain::Index>& sectionsOf(std::string_view name) const;
    // « Acquisitions_ANA, Gestion_reports +2 » ; "" : aucune section.
    [[nodiscard]] std::string sectionNames(std::string_view name, std::size_t max = 3) const;
private:
    const domain::Project*                                      project_;
    std::unordered_map<std::string, std::vector<domain::Index>> map_;
};

// -------------------------------------------------------- les types derives ----
struct TypeUse {
    std::size_t              globals{0};    // variables globales de ce type (ou tableaux de ce type)
    std::vector<std::string> units;         // unites qui en declarent (locales, parametres)
    std::vector<std::string> dfbs;          // DFB qui en declarent (interface, locales)
    std::vector<std::string> types;         // DDT qui l'ont comme champ
    [[nodiscard]] bool used() const noexcept { return globals || !units.empty() || !dfbs.empty() || !types.empty(); }
    // « 1 variable · 3 unites · TOUTFERMER » ; « dans armoire » ; « pas utilise ».
    [[nodiscard]] std::string summary() const;
};
[[nodiscard]] TypeUse usesOfType(const domain::Project&, std::string_view typeName);

// ------------------------------------------------------------- les blocs DFB ----
struct Instance {
    domain::Index variable{domain::kNoIndex};
    std::string   name;
    std::string   owner;        // "" : globale ; sinon l'unite ou le DFB qui la declare
    std::string   calledIn;     // les sections qui la nomment
};
[[nodiscard]] std::vector<Instance> instancesOf(const domain::Project&, std::string_view blockName, const Where&);
// Les variables de l'interface d'un DFB, par sens.
struct Interface {
    std::vector<domain::Index> inputs, outputs, inouts, publics, privates;
};
[[nodiscard]] Interface interfaceOf(const domain::Project&, domain::Index pou);

// ------------------------------------------------------------- les variables ----
enum class Genre : std::uint8_t { DfbInstance, StandardBlock, DdtInstance, Located, Other };
[[nodiscard]] Genre genreOf(const domain::Project&, const domain::Variable&);
[[nodiscard]] std::string_view genreLabel(Genre) noexcept;   // « Instances de DFB »...

// Les unites de programme et les DFB qui declarent localement cette variable
// (le nom de leur POU) ; "" : globale.
[[nodiscard]] std::string ownerName(const domain::Project&, const domain::Variable&);

// --------------------------------------------------------- les sous-routines ----
// Les sections qui appellent cette sous-routine (« SR_Nom(); » ou « SR_Nom; »).
[[nodiscard]] std::vector<std::string> callersOf(const domain::Project&, domain::Index section);

} // namespace project::usage
