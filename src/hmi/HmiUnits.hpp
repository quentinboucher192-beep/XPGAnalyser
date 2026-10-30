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
    // La famille d'une unite pour un message, avec son article : "une pression", "un debit", "un temps" ;
    // une famille du projet : "une grandeur Angle" ; une unite inconnue : "une grandeur".
    [[nodiscard]] std::string       familyPhrase(std::string_view symbol) const;

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
// change pas ; son contenu n'est pas lu : "Vannes[0]" vaut pour toutes les cases). Un membre herite (EXTENDS)
// garde son unite. Sert au simulateur (EN / UNITE, l'affectation convertie) via HmiRuntime::declaredUnit, et au
// controle statique (exprcheck) : les deux lisent la meme unite.
[[nodiscard]] std::string unitOfPath(const Project&, std::string_view path);
// 1.13.0 (seconde passe) : la meme lecture depuis un depart connu - une declaration (locale, parametre, constante de
// vue) de type `type` et d'unite `unit`, suivie de `rest` (".Debit", "[2]", "[i].Pression" ; vide : elle-meme).
[[nodiscard]] std::string unitAlong(const Project&, std::string_view type, std::string_view unit, std::string_view rest);

// =============================================================================
//  1.14.1 (lot N5) : CONVERTIR UNE VALEUR - l'onglet Convertir de Configuration >
//  Unites de mesure (un banc d'essai) et l'objet Convertisseur d'unites. LE MEME
//  CALCUL QUE EN(x, 'mbar') dans un script : Catalog::convert, l'affine de la
//  bibliotheque (fournies + projet + bibliotheques reliees) ; aucune table a part.
// =============================================================================
struct Conversion {
    bool        ok{false};
    double      value{0};       // la valeur de depart
    double      result{0};      // la valeur convertie (ok)
    std::string from, to;       // les symboles du catalogue (degc -> degC) ; vides : inconnus
    std::string error;          // le refus, dit clairement : unite inconnue, dimensions differentes (les unites
                                // comme les listes les montrent : "\xC2\xB0" "C", pas degC)
    std::string formula;        // "1 bar = 1000 mbar" ; "\xC2\xB0" "C -> K : x + 273,15" (les nombres a la francaise)
};
[[nodiscard]] Conversion convertValue(const Catalog&, double value, std::string_view from, std::string_view to);
// La formule d'une conversion, a lire : un facteur seul "1 bar = 1000 mbar" ; une affine "\xC2\xB0" "C -> K : x + 273,15",
// "\xC2\xB0" "F -> \xC2\xB0" "C : x \xC3\x97 0,555556 - 17,7778" ; la meme unite "1 bar = 1 bar". Vide : pas de conversion.
// Les nombres a la francaise (la virgule), comme le resultat.
[[nodiscard]] std::string formulaText(const Catalog&, std::string_view from, std::string_view to);
// L'expression a copier dans un script ou une fx : "EN(Valeur, 'mbar')" (`value` vide : Valeur).
[[nodiscard]] std::string enExpression(std::string_view value, std::string_view to);
// Un resultat MONTRE (l'onglet, l'objet, l'historique, le message Ecrit) a `decimals` decimales (0..12), sans zeros
// inutiles, a la francaise comme la Calculatrice : 1500, 21,755656 ; une valeur tres petite ou tres grande garde ses
// chiffres significatifs (1,15741E-8) au lieu de s'ecrire 0. Pas de separateur de milliers (il se retape tel quel).
[[nodiscard]] std::string formatResult(double v, int decimals);
// Le meme nombre avec un point (21.755656, 1.15741e-08) : une valeur, pas un texte - la Console (Var := 1500), comme
// les autres ecritures qu'elle trace.
[[nodiscard]] std::string resultNumber(double v, int decimals);
// Une valeur tapee : "1,5", " 2.5e3 ", "-40" ; faux : illisible (vide, du texte, infini).
[[nodiscard]] bool parseValue(std::string_view text, double& out);
// Les unites d'une dimension (le nom ou le libelle de la famille : "pression", "Pression" ; vide, "toutes" : toutes),
// dans l'ordre du catalogue.
[[nodiscard]] std::vector<const UnitDef*> unitsOf(const Catalog&, std::string_view family);
// La famille d'apres un nom ou un libelle ("Temp\xC3\xA9rature", "temperature") ; vide : toutes, ou inconnue.
[[nodiscard]] std::string familyFrom(const Catalog&, std::string_view text);

// ---- 1.14.1 (lot N5) : la geometrie de l'objet Convertisseur d'unites (le dessin et le clic la partagent) ----
//  [ 1.5          ] [bar   v] = [mbar   v]          la ligne de saisie : la valeur, De, Vers
//  [ \xE2\x87\x84 ]       1500 mbar                  le bouton inverser, le resultat
//  1 bar = 1000 mbar                    [Ecrire]     la formule en petit ; Ecrire (Resultat vers)
//  Une liste ouverte (De ou Vers) s'etale sous sa case, par-dessus le reste de l'objet.
struct ConverterBox { double x{0}, y{0}, w{0}, h{0}; [[nodiscard]] bool contains(double px, double py) const noexcept {
    return w > 0 && h > 0 && px >= x && px < x + w && py >= y && py < y + h; } };
struct ConverterLayout {
    ConverterBox title, value, from, to, swap, result, formula, write;
    ConverterBox list;                   // la liste ouverte (vide : fermee)
    std::vector<ConverterBox> rows;      // ses lignes montrees
    std::vector<std::size_t>  rowIndex;  // ... leur rang dans la liste
    ConverterBox up, down;               // les fleches (des unites au-dessus, au-dessous) ; vides : aucune
    std::size_t first{0};                // le rang de la premiere ligne montree (ramene dans la liste)
    double rowHeight{22};
};
// `open` : 0 aucune liste, 1 De, 2 Vers ; `count` : ses unites ; `first` : la premiere montree.
[[nodiscard]] ConverterLayout converterLayout(double w, double h, double fontSize, bool titled, bool writes, int open, std::size_t count,
                                              std::size_t first);
// La partie touchee : "valeur", "de", "vers", "inverser", "ecrire", "choix:N" (une ligne de la liste ouverte),
// "liste:haut", "liste:bas" (la liste, au-dela de ses lignes) ; "" : rien.
[[nodiscard]] std::string converterHit(const ConverterLayout&, double x, double y);

} // namespace hmi::units
