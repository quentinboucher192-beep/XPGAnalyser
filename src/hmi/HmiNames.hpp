// =============================================================================
//  hmi/HmiNames.hpp - 1.13.0 (lot J8) : les espaces de noms - le resolveur de noms
// -----------------------------------------------------------------------------
//  UN ESPACE DE NOMS EST UN PREFIXE DE NOM, PAS UN CONTENEUR. Un type, une
//  fonction, un script portent un champ `ns` (Gaz) : leur nom complet est
//  Gaz.T_Vanne, Util.Moyenne, Gaz.Horloge. En marche tout reste GLOBAL : ce
//  module reecrit le code AVANT le moteur (comme viewmembers::qualify) - un nom
//  court devient son nom complet, les directives USING disparaissent. Le moteur
//  (sim::, partage avec API Studio) ne voit que des noms complets, jamais USING.
//
//  LA PORTEE. Un item nomme les voisins de SON espace sans prefixe (un USING
//  implicite de soi-meme, et de ses espaces parents : Gaz.Ligne voit Gaz). Ailleurs
//  on ecrit Gaz.T_Vanne, ou on met `USING Gaz;` en tete du corps. Une locale ou un
//  parametre de meme nom passe avant (le meme `hidden` que viewmembers::qualify) ;
//  un item de la racine aussi.
//
//  LA REGLE DE RESOLUTION DE A.B.C (le chemin le plus long d'abord) :
//    1) A (ou A.B) est un espace declare et nomme un type, une fonction, un script
//       -> une reference qualifiee (un seul nom : Gaz.T_Vanne) ;
//    2) sinon A est une vue -> un membre de vue (Vue.Nom, existant) ;
//    3) sinon A est une variable, un parametre, une locale -> un acces membre ou une
//       methode (lot J1). L'ensemble des espaces est ferme (connu a la compilation) :
//       Four1.Temperature reste un acces membre ; Generer refuse une variable, une vue
//       ou un type qui porte le nom d'un espace.
//
//  AMBIGUITE : deux espaces importes qui definissent le meme nom - l'emploi nu est
//  refuse a sa ligne (" ambigu entre Gaz.Borne et Util.Borne - qualifie-le "), avec la
//  correction d'un clic qui insere le prefixe.
//
//  Les fonctions de ce module ne lisent que le modele : elles servent au moteur
//  (HmiRuntime), au controle (HmiScriptCheck, HmiExprCheck, Generer) et a l'editeur.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::names {

// ---- les directives USING ----------------------------------------------------------
//  `USING Gaz;` (ou USING Gaz.Ligne;) : hors des chaines et des commentaires, la ou une
//  instruction commence. USING n'est pas du ST : la directive est lue ici, puis retiree.
struct Using {
    std::string ns;             // tel qu'ecrit : "Gaz"
    std::size_t at{0};          // le U de USING
    std::size_t length{0};      // jusqu'au ';' compris
    int         line{1};        // 1 = la premiere
    int         column{1};      // 1 = le premier octet de la ligne
};
[[nodiscard]] std::vector<Using> usings(std::string_view code);
// Le code sans ses directives USING : chacune remplacee par des espaces - les lignes et les
// colonnes ne bougent pas (les erreurs du moteur restent a leur place). Sans USING : tel quel.
[[nodiscard]] std::string blankUsings(std::string_view code);
// Le code contient-il (peut-etre) une directive USING ? Un test rapide, sans casse.
[[nodiscard]] bool mentionsUsing(std::string_view code) noexcept;

// ---- l'index des espaces d'un projet ---------------------------------------------------
enum class ItemKind : std::uint8_t { None, Type, Function, Script };
[[nodiscard]] std::string_view itemKindLabel(ItemKind) noexcept;   // "le type", "la fonction", "le script"

class Index {
public:
    explicit Index(const Project&);
    [[nodiscard]] const Project& project() const noexcept { return *project_; }
    [[nodiscard]] bool empty() const noexcept { return spaces_.empty(); }
    // Un espace declare (sans casse) ; son ecriture du projet.
    [[nodiscard]] bool        isNamespace(std::string_view) const;
    [[nodiscard]] std::string spelled(std::string_view ns) const;
    [[nodiscard]] const std::vector<std::string>& spaces() const noexcept { return spaceList_; }
    // L'item `name` (nom court) de l'espace `ns` ; None : aucun.
    [[nodiscard]] ItemKind kindIn(std::string_view ns, std::string_view name) const;
    // Les noms courts de l'espace (son ecriture), tries ; les sous-espaces directs a part.
    [[nodiscard]] std::vector<std::string> itemsOf(std::string_view ns) const;
    [[nodiscard]] std::vector<std::string> childSpaces(std::string_view ns) const;
    // Un item de la racine (sans espace) porte-t-il ce nom ?
    [[nodiscard]] bool rootHas(std::string_view name) const;
    // Les espaces qui definissent ce nom court (leur ecriture).
    [[nodiscard]] std::vector<std::string> spacesDefining(std::string_view name) const;
    // L'item vient-il d'une bibliotheque reliee (lecture seule) ? Laquelle (vide : du projet).
    [[nodiscard]] std::string libraryOf(std::string_view full) const;

private:
    struct Item { std::string name; ItemKind kind{ItemKind::None}; std::string library; };
    const Project* project_{nullptr};
    std::map<std::string, std::string> spaces_;                       // MAJ -> ecriture
    std::vector<std::string> spaceList_;
    std::map<std::string, std::map<std::string, Item>> items_;        // MAJ ns -> MAJ nom -> item
    std::set<std::string> root_;                                      // MAJ des noms de la racine
};

// A.B.C (des segments) : le nom qualifie le plus long - un prefixe qui est un espace declare suivi
// d'un item de cet espace. Rend le nombre de segments pris (0 : ce n'est pas un nom qualifie) ; `full`
// recoit le nom complet (l'ecriture du projet pour l'espace).
[[nodiscard]] std::size_t qualifiedPrefix(const Index&, const std::vector<std::string>& segments, std::string* full = nullptr,
                                          ItemKind* kind = nullptr);

// ---- un nom court, vu d'un item -----------------------------------------------------------
struct Scope {
    std::string              own;        // l'espace de l'item (vide : la racine)
    std::vector<std::string> imports;    // ses USING (tels qu'ecrits)
    std::set<std::string>    hidden;     // en MAJUSCULES : ses locales et parametres (ils passent avant)
};
struct Resolved {
    std::string              full;        // le nom complet retenu ; vide : rien a qualifier
    ItemKind                 kind{ItemKind::None};
    std::vector<std::string> ambiguous;   // deux espaces importes au moins : leurs noms complets
    std::string              hint;        // un item d'un espace qui n'est ni le sien ni importe (Gaz.Borne)
    std::string              via;         // l'espace importe qui l'a fourni (vide : le sien)
};
// La regle : cache (locale, parametre) -> rien ; l'espace de l'item (et ses parents) -> qualifie ;
// la racine (un item, une variable IHM) -> rien ; un seul espace importe -> qualifie ; plusieurs ->
// ambigu ; un autre espace le definit -> `hint` (le nom complet a ecrire).
[[nodiscard]] Resolved resolveBare(const Index&, const Scope&, std::string_view name);

// Les noms que le code declare lui-meme (VAR... END_VAR : ses locales, ses parametres), en MAJUSCULES.
[[nodiscard]] std::set<std::string> declaredNames(std::string_view code, bool function);

// ---- la reecriture pour le moteur -----------------------------------------------------------
//  Le code de l'item d'espace `own` : ses USING retires (blancs), chaque nom court qu'ils (ou son
//  espace) designent ecrit en entier (Borne -> Util.Borne, T_Vanne -> Gaz.T_Vanne). Les chaines, les
//  commentaires, les membres (x.Nom), les noms d'arguments (f(a := 1)) et `hidden` ne bougent pas ;
//  ses locales et parametres non plus (lus dans le code). Un nom ambigu reste tel quel (Generer le dit).
[[nodiscard]] std::string qualify(const Project&, std::string_view code, std::string_view own,
                                  const std::vector<std::string>& hidden = {}, bool function = false);
// Le meme, avec un index deja fait (plusieurs codes du meme projet).
[[nodiscard]] std::string qualify(const Index&, std::string_view code, std::string_view own,
                                  const std::vector<std::string>& hidden = {}, bool function = false);
// Le projet emploie-t-il des espaces (un item, une bibliotheque, un espace vide) ? Rien a reecrire sinon.
[[nodiscard]] bool active(const Project&) noexcept;
// Un texte de type ecrit en noms complets, vu de l'espace `own` : T_Vanne -> Gaz.T_Vanne (son espace, puis le
// seul type de ce nom), ARRAY[1..3] OF T_Vanne -> ARRAY[1..3] OF Gaz.T_Vanne. Deux ecritures d'un meme type
// (court, complet) se comparent alors (les signatures, les surcharges). Sans espace : tel quel.
[[nodiscard]] std::string canonicalType(const Project&, std::string_view type, std::string_view own = {});

// ---- les constats (Compiler, Generer, le soulignement pendant la frappe) ----------------------
struct Finding {
    int         line{1}, column{1}, length{0};
    bool        warning{false};
    std::string message;          // en francais, tutoie, dit quoi faire
    std::string name;             // le nom en cause
    std::string suggestion;       // le nom propose
    std::string fixFrom, fixTo;   // la correction d'un clic (remplacer sur la ligne) ; vide : aucune
};
//  - USING d'un espace inconnu (avec le plus proche) ; USING inutile (avertissement, " Retire le USING ") ;
//  - un nom nu ambigu sous USING (" Qualifie en Gaz.Max ") ;
//  - un nom nu d'un autre espace, sans USING (" Qualifie en Util.Moyenne ") ;
//  - Gaz.Inconnu : l'espace n'a pas ce nom (avec le plus proche de l'espace).
//  `knownElsewhere` : ce nom court est-il connu autrement (une fonction standard, une native, une variable de
//  l'automate, une locale...) ? Fourni, un nom court d'un espace ni le sien ni importe, que rien d'autre ne
//  connait, est refuse (" Moyenne est dans l'espace Util : ecris Util.Moyenne ") ; vide : rien n'est dit.
[[nodiscard]] std::vector<Finding> check(const Index&, std::string_view code, std::string_view own,
                                         const std::vector<std::string>& hidden = {}, bool function = false,
                                         bool allowUsing = true,
                                         const std::function<bool(std::string_view)>& knownElsewhere = {});

// ---- la fusion des jetons d'un analyseur (HmiScriptCheck, HmiExprCheck) ----------------------
//  Les jetons d'un code, vus par leur texte : un identifiant, un point, autre chose. qualifiedRuns rend
//  les remplacements : un nom qualifie de plusieurs jetons (Gaz . T_Vanne -> un seul jeton
//  "Gaz.T_Vanne"), ou un nom court que la portee designe (Borne -> "Util.Borne", count 1).
struct TokenView {
    std::string_view text;
    bool             ident{false};
    bool             dot{false};
};
struct Run {
    std::size_t first{0};          // le premier jeton
    std::size_t count{1};          // le nombre de jetons remplaces (2 x segments - 1)
    std::string text;              // le nom complet
};
[[nodiscard]] std::vector<Run> qualifiedRuns(const Index&, const std::vector<TokenView>&, const Scope* bare = nullptr);

// ---- renommer un espace ------------------------------------------------------------------
// Dans un texte (code ou expression) : from.X -> to.X (les sous-espaces suivent), USING from; -> USING to;.
[[nodiscard]] std::string renameInCode(std::string_view code, std::string_view from, std::string_view to);
// 1.13.0 : RENOMMER UNE FONCTION D'UN ESPACE (ou de la racine) DANS UN CODE, PAR IDENTITE - `targetFull` (Util.Moyenne,
// ou Moyenne a la racine) devient `newShort` : la forme qualifiee (Util.Moyenne(a, b) -> Util.MoyenneGlissante(a, b)) et
// chaque nom court qui la vise depuis l'espace `own` du code et ses USING (son espace, la racine, un import, le seul de ce
// nom). Un nom court qui vise une homonyme (Gaz.Moyenne depuis un script de Gaz), une locale, un argument nomme ou un
// membre ne change pas. Seuls les appels et les noms passes en valeur (un rappel) changent ; `bare` : le corps de la
// fonction elle-meme (son nom y est aussi son retour) - chaque nom court qui la vise.
[[nodiscard]] std::string renameItemInCode(const Index&, std::string_view code, std::string_view own, std::string_view targetFull,
                                           std::string_view newShort, bool bare = false);
// Dans un texte de type (Variable::type, un membre, ARRAY[..] OF from.T, LIST OF from.T...).
[[nodiscard]] std::string renameInType(std::string_view type, std::string_view from, std::string_view to);
// Partout dans le projet : l'espace des items (from et ses sous-espaces), les espaces vides gardes, tous
// les codes (forEachCode), les textes de type, les heritages. Rend le nombre de changements. Une
// bibliotheque reliee ne se renomme pas ici (son nom : Relier sous le nom...).
std::size_t renameNamespace(Project&, std::string_view from, std::string_view to);
// Le nom d'espace propose est-il libre et lisible ? `why` dit pourquoi non.
[[nodiscard]] bool validNewNamespace(const Project&, std::string_view name, std::string* why = nullptr);
// Les noms qu'un espace reserve sont pris par... (une variable, une vue, un type de la racine) : la
// phrase de Generer, ou vide si rien ne le prend.
[[nodiscard]] std::string reservedClash(const Project&, std::string_view ns);

// ---- l'edition (l'inspecteur, la page Espaces et bibliotheques) ------------------------------
// L'espace d'un item du projet (un type IHM, une fonction IHM, un script general) : `ns` vide le remet a la racine.
// Faux et `why` : un nom illisible, l'espace d'une bibliotheque reliee (lecture seule), un nom complet deja pris
// (Gaz.T_Vanne existe), un nom reserve (une variable Gaz).
bool setItemNamespace(Project&, ItemKind, Id, std::string_view ns, std::string* why = nullptr);
// Les espaces du projet (hors bibliotheques), leurs items comptes ; un espace vide garde y est (kept).
struct SpaceCount {
    std::string ns;
    std::size_t types{0}, functions{0}, scripts{0};
    bool        kept{false};          // garde vide (listFolders["espaces"])
    [[nodiscard]] std::size_t total() const noexcept { return types + functions + scripts; }
};
[[nodiscard]] std::vector<SpaceCount> projectSpaces(const Project&);
// Un espace vide, garde comme un dossier vide (on y range ensuite des items). Faux et `why` : refuse.
bool addEmptyNamespace(Project&, std::string_view ns, std::string* why = nullptr);
// Retire un espace vide garde (un espace qui a des items reste : il existe par eux). Faux : rien a retirer.
bool removeEmptyNamespace(Project&, std::string_view ns);

} // namespace hmi::names
