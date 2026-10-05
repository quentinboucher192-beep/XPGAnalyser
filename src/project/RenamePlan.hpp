// =============================================================================
//  project/RenamePlan.hpp - renommer en voyant d'abord TOUT ce qui suit
// -----------------------------------------------------------------------------
//  Renommer une variable de l'automate, c'est aussi reecrire le code qui la
//  nomme, les tables d'animation, les parametres des unites qui la recoivent -
//  et, de l'autre cote, les vues, les scripts, les alarmes de l'IHM qui la
//  lisent, la variable IHM qui lui est liee. Le plan le calcule AVANT : le
//  dialogue le montre (un arbre, par endroit), puis UNE commande le fait (un
//  seul Ctrl+Z pour les deux cotes).
//
//  CE QUE LE PLAN SAIT RENOMMER (les cles de MainAnalysisScreen::askRename) :
//    variable      une variable globale ; "Unite.var" : une variable d'unite
//    ddt           un type derive           dfb     un bloc DFB
//    unite         une unite de programme   section une section, une sous-routine
//    table         une table d'animation
//    ihm-variable  une variable de l'IHM    ihm-vue une vue de l'IHM
//    ddt-champ     (lot API 8) un champ d'un type derive : "Type.champ"
//
//  LOT API 8 - UN CHAMP DE DDT. Seuls les acces qui passent par CE type sont
//  reecrits : chaque chemin (ConfigsGaz[i].Nom_gaz, generalites.gaz.Nom_gaz,
//  le parametre d'une unite ou d'un DFB de ce type) est suivi de sa racine a son
//  champ (PlcTypes). Un acces dont le type ne se lit pas (x.Nom_gaz, x inconnu)
//  suit le champ quand aucun autre type n'a de membre de ce nom - ce que faisait
//  l'onglet Types - sinon le plan est refuse (Verdict::Ambiguous) et dit ou.
//
//  LE NOM EST VERIFIE DES DEUX COTES. Un identifiant Control Expert (ou IHM),
//  pas un mot du langage ; libre la ou il vivra : une variable globale ne prend
//  pas le nom d'une variable IHM (l'IHM lit d'abord les siennes : elle
//  cesserait de lire l'automate), une variable IHM pas celui d'une globale.
//
//  L'IHM N'EST PAS LIEE ICI. xpg_import (ce fichier) ne connait pas la
//  bibliotheque de l'IHM - c'est elle qui depend de lui. L'application branche
//  sa moitie par HmiSide (app/RenameDialog.cpp) : les noms de l'IHM (pour
//  verifier), ce qui y change (pour montrer), la commande qui le fait.
//
//  LE PLAN SE CALCULE AUSSI SANS NOUVEAU NOM (ou avec un nom refuse) : il dit
//  alors ou l'ancien nom est cite - c'est ce que le dialogue montre a
//  l'ouverture, avant la premiere lettre tapee. Les lignes y portent un
//  marqueur a la place du nouveau nom (voir previewMark).
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace project::rename {

// ------------------------------------------------------------ ce qu'on renomme -
// Lot API 8 : DdtField (en dernier : les rangs d'avant ne bougent pas).
enum class Kind : std::uint8_t { Variable, Ddt, Dfb, Unit, Section, Table, HmiVariable, HmiView, DdtField };

[[nodiscard]] std::optional<Kind> kindFromKey(std::string_view key) noexcept;   // "variable", "ihm-vue"... (sans casse)
[[nodiscard]] std::string_view    kindKey(Kind) noexcept;
[[nodiscard]] std::string_view    kindLabel(Kind) noexcept;                     // "la variable", "le type derive"... (UTF-8)
[[nodiscard]] std::string_view    supportedKinds() noexcept;                    // pour le message d'un genre inconnu
[[nodiscard]] constexpr bool isHmiKind(Kind k) noexcept { return k == Kind::HmiVariable || k == Kind::HmiView; }

struct Target {
    Kind          kind{Kind::Variable};
    std::string   name;                        // son nom (une variable d'unite : sans l'unite)
    std::string   display;                     // comme on le lit : "Logigrammes_A.compteur"
    domain::Index index{domain::kNoIndex};     // la variable, le type, le POU, la section, la table
    domain::Index unit{domain::kNoIndex};      // une variable d'unite : son unite
    bool          global{false};               // une variable globale (l'IHM la voit)
    std::string   address;                     // une globale situee : "%MW100"
    std::string   detail;                      // "variable globale INT AT %MW100", "popup"...
    // ---- Lot API 8 : un champ de DDT (name : le champ, index : sa variable)
    domain::Index type{domain::kNoIndex};      // le type derive qui le porte
    std::string   typeName{};                  // son nom ("config_gaz")
};

// ---------------------------------------------------------------- lot API 8 --
//  LE TYPE D'UN CHEMIN DE L'AUTOMATE. Une photo du projet (les globales, les
//  variables de chaque unite et de chaque bloc, les champs des DDT, les membres
//  des DFB) : le plan d'un champ de DDT y suit chaque chemin, du programme ou de
//  l'IHM. Les types sont ceux que le projet ecrit ("ARRAY[0..19] OF config_gaz").
enum class RootIs : std::uint8_t {
    Unknown,   // rien ne dit ce que c'est
    Other,     // connu, mais pas une valeur de l'automate (une variable IHM, une vue, SYS...)
    Plc,       // une variable de l'automate : `type`
};
struct RootType {
    RootIs      is{RootIs::Unknown};
    std::string type;
};

class PlcTypes {
public:
    PlcTypes() = default;
    explicit PlcTypes(const domain::Project& p);
    // Une racine : une variable de l'unite ou du bloc `pou` (ses parametres, ses
    // publiques, ses privees), sinon une globale ; Unknown sinon.
    [[nodiscard]] RootType root(std::string_view name, domain::Index pou = domain::kNoIndex) const;
    [[nodiscard]] bool     isGlobal(std::string_view name) const;
    // Le type du membre `member` d'une valeur de type `type` : un champ de DDT, un
    // parametre ou une publique de DFB, une broche d'un bloc de la bibliotheque.
    // "" : pas de tel membre (ou le type ne se lit pas).
    [[nodiscard]] std::string memberType(std::string_view type, std::string_view member) const;
    // Le type d'un element de ce tableau ("" : pas un tableau).
    [[nodiscard]] static std::string elementType(std::string_view type);
    // Les types (DDT, DFB, blocs de la bibliotheque) qui ont un membre de ce nom.
    [[nodiscard]] std::vector<std::string> owners(std::string_view member) const;
private:
    using Names = std::map<std::string, std::string, std::less<>>;   // NOM (majuscules) -> type
    Names                                      globals_;
    std::map<std::string, Names, std::less<>>  types_;     // DDT, DFB (majuscules) -> leurs membres
    Names                                      typeNames_; // DDT, DFB (majuscules) -> leur nom ecrit
    std::map<domain::Index, Names>             pous_;      // une unite, un bloc -> ses variables
    // Une unite de programme comme racine ("Unite.var", ce que la simulation et
    // l'IHM lisent) : son type est "\x02" suivi de son rang.
    std::map<std::string, domain::Index, std::less<>> units_;
};

// Un renommage a faire : le plan en porte un (la cible), plus ceux des
// variables liees quand la case est cochee.
struct Rename {
    Target      target;
    std::string to;
    // Lot API 8 : un champ de DDT - les types de l'automate au moment du plan,
    // pour la moitie IHM (les chemins de l'automate qu'elle lit).
    std::shared_ptr<const PlcTypes> plc{};
};

// Lot API 8 : LES ACCES A UN CHAMP DANS UN TEXTE. `text` : du code (ST, ou C /
// C++ : Syntax::C), une expression, un chemin ("ConfigsGaz[2].Nom_gaz"). Chaque
// ".champ" (ou "->champ" en C) sur une valeur du type `type` devient `to` ; les
// commentaires et les chaines restent. `root` dit ce qu'est une racine ; un acces
// sur une valeur dont le type ne se lit pas suit le champ si `unique` (aucun
// autre type n'a de membre de ce nom), sinon il reste et sa ligne va dans
// `scan->ambiguous` (1 : la premiere).
struct FieldAccess {
    std::string type, field, to;
    bool        unique{true};
};
struct FieldScan {
    std::size_t      rewritten{0};
    std::vector<int> ambiguous;
};
enum class Syntax : std::uint8_t { St, C };
using RootTyper = std::function<RootType(std::string_view name)>;
[[nodiscard]] std::string rewriteFieldAccesses(std::string_view text, const FieldAccess& access, const PlcTypes& types,
                                               const RootTyper& root, Syntax syntax = Syntax::St, FieldScan* scan = nullptr);

// ------------------------------------------------------------ ce qui change -
//  Trois onglets dans le dialogue : le programme (API), les tables d'animation
//  (le pont : elles tiennent des variables des deux cotes), l'IHM.
enum class Tab : std::uint8_t { Api, Tables, Hmi };

struct Change {
    Tab                      tab{Tab::Api};
    std::vector<std::string> path;             // l'endroit, du plus large au plus precis
    std::string              label;            // "ligne 12", "visible (expression)", "nom"
    std::string              before, after;    // le texte entier (une ligne, une expression)
    int                      line{0};          // du code : sa ligne (1 : la premiere)
    bool                     info{false};      // rien n'y est ecrit, il s'y lit sous le nouveau nom
    bool                     code{false};      // dans une expression ou du code : le nom doit y etre un identifiant
    // Lot API 8 : un acces au champ dont le type ne se lit pas (x.Nom_gaz, x
    // inconnu) alors qu'un autre type a un membre de ce nom : rien n'y est ecrit
    // (info aussi), il est a verifier a la main - le plan est alors refuse.
    bool                     doubt{false};
};

// La valeur mise a la place du nouveau nom quand il n'y en a pas encore (ou
// qu'il est refuse) : le plan montre alors ou l'ancien nom est cite.
[[nodiscard]] const std::string& previewMark();

// ------------------------------------------------------------- l'IHM, vue d'ici -
struct HmiNames {
    struct Variable {
        std::string name, type, equipment, address;   // equipment non vide : liee (address : %MW100, 40101, ou un nom)
    };
    struct View {
        std::string name, role;                        // role : "vue", "popup", "symbole", "modele"...
    };
    std::vector<Variable>    variables;
    std::vector<std::string> functions;
    std::vector<View>        views;
};

class HmiSide {
public:
    virtual ~HmiSide() = default;
    [[nodiscard]] virtual HmiNames names() const = 0;
    // Ce que ces renommages changent dans l'IHM (ajoute a `out`).
    virtual void collect(const std::vector<Rename>& renames, std::vector<Change>& out) const = 0;
    // Pourquoi ils changeraient le sens de quelque chose ("" : rien) : un script
    // qui a une variable locale du nouveau nom et cite l'ancien, une popup qui a
    // un parametre de ce nom... Le plan est alors refuse (Taken).
    [[nodiscard]] virtual std::string conflict(const std::vector<Rename>& renames) const { (void)renames; return {}; }
    // La commande qui le fait ; nul : rien a changer dans l'IHM.
    [[nodiscard]] virtual core::CommandPtr command(const std::vector<Rename>& renames, const std::string& label) const = 0;
};

// ------------------------------------------------------------------ le plan -
enum class Verdict : std::uint8_t {
    NotFound,      // rien de ce nom
    Unsupported,   // ce genre (ou cette variable) ne se renomme pas ici
    Unchanged,     // c'est deja son nom (ou rien n'est tape)
    Invalid,       // pas un nom permis
    Taken,         // deja pris
    Ok,
    Ambiguous,     // lot API 8 : un acces ne dit pas s'il vise ce champ - a renommer a la main
};

// Une variable liee de l'autre cote : la variable IHM liee a une globale (a la
// meme adresse, ou par son nom), ou la globale d'une variable IHM liee. Le
// dialogue propose de la renommer aussi (une case a cocher).
struct Link {
    Kind        kind{Kind::HmiVariable};   // ce qu'elle est
    std::string name;                      // son nom
    std::string proposed;                  // son nouveau nom ("" : on ne sait pas le deduire)
    std::string how;                       // "liee a la meme adresse %MW100", "liee par son nom" (UTF-8)
    std::string problem;                   // pourquoi `proposed` ne va pas ("" : il va)
    [[nodiscard]] bool usable() const noexcept { return !proposed.empty() && problem.empty(); }
};

struct Plan {
    Target              target;
    std::string         newName;
    Verdict             verdict{Verdict::NotFound};
    std::string         problem;           // NotFound, Unsupported, Invalid, Taken : pourquoi
    std::vector<Link>   links;
    bool                withLinks{false};  // les liees utilisables sont dans `renames`
    std::vector<Rename> renames;           // ce que la commande fera (la cible d'abord)
    std::vector<Change> changes;           // un apercu (previewMark) tant que verdict != Ok

    [[nodiscard]] bool ok() const noexcept { return verdict == Verdict::Ok; }
    [[nodiscard]] bool found() const noexcept { return verdict != Verdict::NotFound && verdict != Verdict::Unsupported; }
    // Les changements (hors lignes d'information) d'un onglet ; nul : tous.
    [[nodiscard]] std::size_t count(std::optional<Tab> tab = std::nullopt) const;
    // Les endroits distincts (un endroit : un chemin) qui ont un changement.
    [[nodiscard]] std::size_t places(std::optional<Tab> tab = std::nullopt) const;
    [[nodiscard]] bool touchesApi() const;   // le programme ou ses tables changent
    [[nodiscard]] bool touchesHmi() const;
};

// Le nom en clair ("Unite.var", "Vitesse") -> la cible ; faux (et why) : introuvable.
[[nodiscard]] bool resolve(const domain::Project* p, const HmiSide* hmi, Kind kind, std::string_view name,
                           Target& out, std::string* why = nullptr);
// Pourquoi `newName` ne va pas pour cette cible ("" : il va) ; `verdict` : lequel.
[[nodiscard]] std::string nameProblem(const domain::Project* p, const HmiSide* hmi, const Target& target,
                                      std::string_view newName, Verdict* verdict = nullptr);
// Le plan complet : la cible, le verdict du nom, les liees, les changements.
// `withLinks` : renommer aussi les liees dont le nouveau nom se deduit et va.
[[nodiscard]] Plan makePlan(const domain::Project* p, const HmiSide* hmi, Kind kind, std::string_view oldName,
                            std::string_view newName, bool withLinks = false);

// ------------------------------------------------------------------ appliquer -
//  UNE commande : le programme (et ses tables) repris tel qu'il etait avant, et
//  la commande de l'IHM (HmiSide::command) faite dans la foulee ; si l'IHM
//  refuse, le programme revient a son etat d'avant. Nul : le plan n'est pas Ok.
class RenamePlanCommand final : public core::ICommand {
public:
    RenamePlanCommand(std::shared_ptr<domain::Project> p, std::vector<Rename> api, core::CommandPtr hmi, std::string label);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] bool hasHmiPart() const noexcept { return static_cast<bool>(hmi_); }
private:
    std::shared_ptr<domain::Project> project_;
    std::vector<Rename>              api_;
    core::CommandPtr                 hmi_;
    std::string                      label_;
    std::optional<domain::Project>   before_;
};
[[nodiscard]] core::CommandPtr makeCommand(std::shared_ptr<domain::Project> p, const HmiSide* hmi, const Plan& plan);
// Le libelle de la commande (et de l'historique) : "Renommer la variable X en Y".
[[nodiscard]] std::string commandLabel(const Plan& plan);

// ----------------------------------------------------------- pour le dialogue -
//  Une difference en ligne : les morceaux communs, l'ancien nom (1), le
//  nouveau (2). Mot a mot quand les deux textes ont le meme nombre de mots ;
//  sinon le debut et la fin communs, et le milieu.
struct Piece {
    std::string text;
    int         kind{0};   // 0 : commun, 1 : ote, 2 : ajoute
};
[[nodiscard]] std::vector<Piece> inlineDiff(std::string_view before, std::string_view after);

// Les petits outils du plan, publics pour la moitie IHM et les essais.
[[nodiscard]] bool        sameName(std::string_view a, std::string_view b) noexcept;   // sans casse
[[nodiscard]] bool        isIdentifier(std::string_view s) noexcept;                   // lettre ou _, puis lettres, chiffres, _
[[nodiscard]] bool        isReservedWord(std::string_view s) noexcept;                 // un mot du ST, un type elementaire
// `text` avec chaque occurrence (sans casse) de `from` remplacee par `to` : le
// nom propose a une variable liee (Vitesse_IHM -> Debit_IHM).
[[nodiscard]] std::string replaceInsensitive(std::string_view text, std::string_view from, std::string_view to);

// ---- Lot API 8 : les forcages enregistres de la simulation (<projet>/simulation/*.txt) ----
// Une ligne "nom = valeur" (SimulationPane) : le nom suit le renommage `r` - une
// variable globale (sa racine), une variable d'unite ("Unite.var"), une unite (la
// racine "Unite"), un champ de DDT (les chemins qui passent par lui, lus comme le
// plan : rewriteFieldAccesses). Un commentaire (#), la valeur, une ligne qui n'est
// pas un forcage : tels quels. L'ecran les montre et les ecrit (RenameWorkspace.cpp).
[[nodiscard]] std::string renameForcingLine(const std::string& line, const Rename& r);
// Les renommages qui peuvent toucher un forcage enregistre.
[[nodiscard]] bool        citedByForcings(const Rename& r);

} // namespace project::rename
