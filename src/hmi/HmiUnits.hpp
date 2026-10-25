// =============================================================================
//  hmi/HmiUnits.hpp - 1.13.0 (lot J6) : LA BIBLIOTHEQUE DES UNITES DE MESURE
// -----------------------------------------------------------------------------
//  Les familles (dimensions physiques) et les unites fournies d'office, plus
//  celles du projet (Config::unitFamilies / units). Chaque unite se convertit
//  vers l'unite de reference de sa famille par une AFFINE (facteur, decalage) :
//  bar/mbar/Pa = facteur seul ; degC/K/degF = facteur ET decalage.
//
//  Le raisonnement sur les unites est STATIQUE (exprcheck, Generer) : l'unite
//  vit sur la declaration, pas sur sim::Value. Ce module dit familyOf(symbole),
//  la conversion entre deux unites, le verdict de compatibilite (identique /
//  meme famille / autre famille / inconnu), et la forme montree « REAL [bar] ».
//
//  Nm3/h (debit normalise) est une famille A PART, non convertible vers m3/h :
//  on ne ment pas sur les conditions normales.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::units {

// La conversion de l'unite source vers l'unite cible : valeur_cible = valeur * factor + offset.
struct Affine {
    double factor{1.0};
    double offset{0.0};
    [[nodiscard]] bool   identity() const noexcept { return factor == 1.0 && offset == 0.0; }
    [[nodiscard]] double apply(double v) const noexcept { return v * factor + offset; }
};

// Le verdict entre deux unites (ou une unite et une cible de conversion).
enum class Rel : std::uint8_t {
    None,         // au moins une sans unite : tout passe (le nu adopte l'autre)
    Same,         // meme symbole exact (apres resolution des alias)
    SameFamily,   // meme famille, unite differente : conversion affine possible
    OtherFamily,  // deux familles differentes : refus (on nomme les deux)
    Unknown       // un symbole inconnu (ni fourni, ni du projet)
};

// La bibliotheque fusionnee : les unites et familles fournies + celles du projet.
class Catalog {
public:
    Catalog();                             // les fournies seules
    explicit Catalog(const Config&);       // les fournies + celles du projet (Config::units / unitFamilies)

    // Sans casse ni accents, avec les alias courants (m3/h, degC, nm3/h...) ; nul : inconnu.
    [[nodiscard]] const UnitDef*    unit(std::string_view symbol) const;
    [[nodiscard]] const UnitFamily* family(std::string_view name) const;
    [[nodiscard]] std::string       familyOf(std::string_view symbol) const;     // "" : inconnu
    [[nodiscard]] bool              known(std::string_view symbol) const noexcept;
    // Le symbole fourni (resout un alias : "m3/h" reste "m3/h", "degc" devient "degC") ; vide : inconnu.
    [[nodiscard]] std::string       canonical(std::string_view symbol) const;
    [[nodiscard]] std::string       familyLabel(std::string_view name) const;    // "pression" -> "Pression"
    [[nodiscard]] std::string       unitLabel(std::string_view symbol) const;    // le libelle d'une unite

    [[nodiscard]] const std::vector<UnitFamily>& families() const noexcept { return families_; }
    [[nodiscard]] const std::vector<UnitDef>&    units() const noexcept { return units_; }

    // La conversion de `from` vers `to` (meme famille) : une affine ; identite si egales ; vide si refus
    // (familles differentes, symbole inconnu).
    [[nodiscard]] std::optional<Affine> convert(std::string_view from, std::string_view to) const;
    [[nodiscard]] Rel                   relation(std::string_view a, std::string_view b) const;

private:
    void add(const UnitFamily&);
    void add(const UnitDef&);
    std::vector<UnitFamily> families_;
    std::vector<UnitDef>    units_;
};

// Les fournies (sans projet), pour l'interface et les tests.
[[nodiscard]] const std::vector<UnitFamily>& builtinFamilies();
[[nodiscard]] const std::vector<UnitDef>&    builtinUnits();

// Une unite ne va que sur un type numerique (REAL, INT... via typereg::isNumber) ; un ARRAY OF REAL :
// l'unite porte sur l'element. Faux : un BOOL, un STRING, une structure, un tableau d'autre chose.
[[nodiscard]] bool typeTakesUnit(std::string_view type);

// La forme combinee montree a la saisie, au survol et au selecteur : "REAL [bar]" ; sans unite : le type seul.
[[nodiscard]] std::string withUnit(std::string_view type, std::string_view unit);

// 1.13.0 (lot J6) : l'unite physique au bout d'un chemin de variable IHM ("Ligne.Pression", "Vannes[2]") ;
// "" si aucune ou si le chemin ne se resout pas. L'unite d'un ARRAY porte sur l'element (un indice ne la
// change pas). Sert au simulateur (EN / UNITE) via HmiRuntime::declaredUnit.
[[nodiscard]] std::string unitOfPath(const Project&, std::string_view path);

} // namespace hmi::units
