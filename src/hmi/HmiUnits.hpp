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

// =============================================================================
//  1.22.8 : CREER UNE UNITE - les controles de Configuration > Unites de mesure, PARTAGES avec l'Assistant IA
//  (creer_unite, modifier_unite) : une seule regle, une seule raison. Et une petite table d'unites COURANTES que le
//  logiciel reconnait sans les fournir (mmCE, %HR, t/h, kW...) : leur famille, leur facteur vers la reference de la
//  famille - pour pre-remplir << Creer l'unite... >>, et pour VERIFIER le facteur que l'IA donne (un avertissement,
//  jamais un refus).
// =============================================================================
// Un symbole : court (16 octets au plus), sans espace, crochet, apostrophe, guillemet ni point-virgule (bar, m3/h, %).
[[nodiscard]] bool symbolOk(std::string_view symbol);
// Un nom de famille : des minuscules, des chiffres et _ (pas de chiffre en tete).
[[nodiscard]] bool familyNameOk(std::string_view name);
// Une unite NEUVE (`u.family` : une famille du catalogue, par son nom ou son libelle) : "" = bonne ; sinon la raison - un
// symbole mal forme, deja pris (une unite fournie ou du projet : on le nomme), une famille inconnue, un facteur nul, un
// decalage illisible. `family` (non nul) : le nom de la famille trouvee.
[[nodiscard]] std::string newUnitProblem(const Catalog&, const UnitDef& u, std::string* family = nullptr);
// Une famille NEUVE et sa reference (un symbole neuf : l'unite de reference est creee avec elle) : "" = bonne.
[[nodiscard]] std::string newFamilyProblem(const Catalog&, std::string_view name, std::string_view reference);
// Changer le facteur et le decalage de l'unite `symbol` du projet (`cfg.units`) : "" = permis. Une fournie est verrouillee ;
// la reference d'une famille garde facteur 1 et decalage 0.
[[nodiscard]] std::string unitChangeProblem(const Config& cfg, std::string_view symbol, double factor, double offset);
// Vrai : une unite, une famille de la bibliotheque fournie (verrouillee).
[[nodiscard]] bool builtinUnit(std::string_view symbol);
[[nodiscard]] bool builtinFamily(std::string_view name);

// Une unite courante connue (la table) : son facteur et son decalage vers `reference`, la reference de sa famille ;
// `familyLabel` : le libelle d'une famille neuve (puissance, humidite, debit massique...).
struct KnownUnit {
    std::string_view symbol, family;
    double           factor{1.0}, offset{0.0};
    std::string_view label, reference, familyLabel;
};
[[nodiscard]] const std::vector<KnownUnit>& knownUnits();
[[nodiscard]] const KnownUnit* knownUnit(std::string_view symbol);   // sans casse ni accents, comme le catalogue
// Ce que la table dit d'un symbole, RAMENE AU CATALOGUE : l'unite (sa famille telle que le catalogue la nomme, son
// facteur vers la reference de CETTE famille) et, quand la famille manque, la famille a creer avec sa reference (son
// nom vide : elle existe). Rien : un symbole hors de la table, ou une famille du catalogue dont la reference ne se relie
// pas a la table. `reference` (non vide) : la reference d'une famille neuve deja choisie (l'IA, la fenetre).
struct Suggestion {
    UnitDef    unit;
    UnitFamily family;
};
[[nodiscard]] std::optional<Suggestion> expected(const Catalog&, std::string_view symbol, std::string_view reference = {});
// La meme chose pour un symbole que le catalogue n'a PAS encore (<< Creer l'unite... >> pre-rempli) ; rien s'il l'a deja.
[[nodiscard]] std::optional<Suggestion> suggestUnit(const Catalog&, std::string_view symbol);
// LA COHERENCE d'une unite proposee avec la table (meme symbole) : "" si rien a dire (inconnue de la table, ou
// conforme a 1 % pres) ; sinon l'avertissement - une autre famille, un facteur loin de l'habituel. `reference` : celle
// d'une famille neuve.
[[nodiscard]] std::string coherenceWarning(const Catalog&, const UnitDef& u, std::string_view reference = {});
// Le nombre exact d'un facteur, pour le garder en texte (le plus court qui se relit au meme double : 9.80665, 1e-06).
[[nodiscard]] std::string exactNumber(double v);

} // namespace hmi::units
