// =============================================================================
//  app/hmi/HmiAssist.hpp - l'aide a la saisie de l'IHM
// -----------------------------------------------------------------------------
//  LE MEME PRINCIPE QUE LES SECTIONS DE L'AUTOMATE : on tape, une liste propose
//  ce qui existe ; une parenthese ouverte montre ce que l'appel attend ; le nom
//  sous le curseur (ou sous la souris) dit ce qu'il est.
//
//  CE QUI EST PROPOSE DEPEND DE L'ENDROIT, lu dans le texte qui precede :
//    du code ST            les variables IHM, les fonctions IHM_, puis ce que
//                          l'automate declare (globales, blocs, mots-cles,
//                          structures IF/FOR/CASE... en entier) ;
//    apres "nom."          les membres de la structure (DDT, DFB) ;
//    IHM_NAVIGUER('        les vues (puis, apres la virgule, les transitions) ;
//    IHM_POPUP('           les vues ;
//    IHM_APPELER('         les scripts generaux ;
//    IHM_SON('             les sons des ressources ;
//    un texte a trous {    les variables (IHM_JOURNAL, messages d'alarme,
//                          textes dynamiques) ;
//    un commentaire, une chaine quelconque : rien - la liste y serait du bruit.
//
//  LOT 7 : les variables locales du script (ses blocs VAR, VAR_TEMP, et les
//  VAR_INPUT d'une fonction) passent en tete ; les fonctions IHM du projet
//  sont proposees avec leurs parametres (et dans les champs : une expression
//  de vue peut les appeler) ; dans un bloc de declaration, apres "nom :",
//  les types permis.
//
//  LES CHAMPS (une variable, une expression, un texte a trous) : la meme
//  liste, sans les mots-cles d'un script, par fieldAssist().
// =============================================================================
#pragma once

#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiDecl.hpp"   // 1.11.18 (refonte, lot 3) : decl::Role, composeCode
#include "../../hmi/HmiDesign.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace domain { class Project; }

namespace app::assist {

// ---------------------------------------------------------------- contexte ---
enum class Context : std::uint8_t {
    Code,         // du code : variables, fonctions, mots-cles
    Comment,      // (* ... *) ou // ... : rien
    Text,         // une chaine ordinaire : rien
    ViewName,     // IHM_NAVIGUER('  IHM_POPUP('
    Transition,   // IHM_NAVIGUER('Vue', '
    ScriptName,   // IHM_APPELER('
    SoundName,    // IHM_SON('
    Placeholder,  // '... {  : un nom dont la valeur ira dans le texte
    Member,       // a.b.  : les membres de a.b
    LocalType,    // VAR ... x : |  : les types d'une variable locale (lot 7)
    EquipmentName,// IHM_ESCLAVE_SIMULE('  IHM_EQUIPEMENT_OK('  : les equipements (1.9)
    LogLevel,     // IHM_LOG(|  NIVEAU_LOG#|  : les niveaux (1.11.14)
};

struct Where {
    Context     context{Context::Code};
    std::size_t from{0};        // ou commence ce que le choix remplace (dans `before`)
    std::string path;           // Member : le chemin avant le dernier point ("Armoires[0].ana")
};
// `before` : tout le texte qui precede le mot en cours. `templateText` : le
// texte entier est un texte a trous (un message) et non du code.
[[nodiscard]] Where locate(std::string_view before, bool templateText = false);

// ------------------------------------------------------------ propositions ---
enum class Kind : std::uint8_t {
    HmiVariable, PlcVariable, PlcLocated, Member, HmiFunction, Function, Block, Keyword, Structure, Type,
    DerivedType, View, Script, Sound, Transition,
    LocalVariable, UserFunction,            // lot 7 : les locales du script, les fonctions IHM du projet
    SysVariable, Instance, InstanceVariable, // lot 9 : SYS.X, un objet d'une vue, ses variables
    Equipment,                               // 1.9 : un equipement du reseau (IHM_ESCLAVE_SIMULE('...'))
    Api,                                     // 1.11.1 (API-V) : API, la racine des variables de l'automate
    ApiUnit,                                 // 1.11.1 (API-V) : API.<Unite> (une unite de programme)
    LogLevel,                                // 1.11.14 : un niveau de IHM_LOG (INFO, ERROR...)
};

// ---- 1.11.2 (API-V, decision 161) : ce qu'est une proposition, et d'ou elle vient --
//  L'icone devant chaque proposition dit sa NATURE (par la forme : un pictogramme
//  au trait) et sa PROVENANCE (par la couleur) ; le pied de la liste donne la cle
//  des couleurs et dit la ligne choisie en mots. LES DEUX TABLES (lookOf, dans
//  HmiAssist.cpp) SONT LE SEUL ENDROIT ou vivent les pictogrammes, les couleurs et
//  les mots : la scene 4 de la maquette de MQ5 (decision 163).
enum class Nature : std::uint8_t {
    Variable, Member, Constant, Type, EnumValue, Function, Method, Keyword, View, Object, Property, Alarm, Script,
    Unit, Root, Resource,   // en plus de la liste du client : une unite de programme, la racine API, un son
};
enum class Origin : std::uint8_t {
    Plc,      // l'automate (API.)
    Hmi,      // le projet IHM
    Object,   // un objet d'une vue (ses proprietes, ses alarmes, ses methodes)
    System,   // SYS.*
    Iec,      // le standard CEI 61131-3 (mots-cles, types elementaires, fonctions standard)
};
inline constexpr std::size_t kNatureCount = 16;
inline constexpr std::size_t kOriginCount = 5;

struct Item {
    std::string text;
    std::string detail;
    Kind        kind{Kind::HmiVariable};
    int         rank{0};                    // plus petit d'abord
    std::string insert;                     // vide : `text`
    std::size_t caret{std::string::npos};   // dans `insert`
    std::size_t extend{0};                  // octets remplaces avant le mot en cours
    bool        chain{false};               // rouvrir la liste apres
    // 1.11.2 (API-V, decision 161) : rangees par suggest() (d'apres kind, le
    // chemin avant le point et les deux projets).
    Nature      nature{Nature::Variable};
    Origin      origin{Origin::Hmi};
};

// Ce qu'on propose pour `prefix` (le mot en cours), `before` etant le texte qui
// le precede. `code` faux : un champ (variable, expression) - ni mots-cles ni
// structures ni fonctions IHM_.
// 1.10.1 (U2) : `op` - le script de cet operateur (HmiOperator) : a, b, Resultat
// (et TO_X pour une conversion) sont connus avec leurs types (a. : les membres
// du type de gauche ; sans rien de tape, ils viennent en tete).
[[nodiscard]] std::vector<Item> suggest(const hmi::Project&, const domain::Project* plc, std::string_view before,
                                        std::string_view prefix, bool code = true, bool templateText = false,
                                        hmi::Id op = hmi::kNoId);

[[nodiscard]] ui::Icon iconOf(Kind) noexcept;
void toCompletions(const std::vector<Item>&, std::vector<ui::MultiLineText::Completion>&);

// 1.11.2 (API-V, decision 161) : les deux tables, et la pastille qu'elles donnent.
struct NatureLook { std::string_view label; const ui::KindBadge::Picto* picto; };
struct OriginLook { std::string_view label; std::string_view key; gfx::Color color; };   // key : le mot de la cle du pied
[[nodiscard]] const NatureLook& lookOf(Nature) noexcept;
[[nodiscard]] const OriginLook& lookOf(Origin) noexcept;
// La pastille : la forme et la lettre de la nature, la couleur de la provenance,
// la legende « variable · automate (API.) » ; une methode : son nom en violet aussi.
[[nodiscard]] ui::KindBadge badgeOf(Nature, Origin);
[[nodiscard]] ui::KindBadge badgeOf(const Item&);

// ------------------------------------------------------------ fonctions IHM ---
struct Function {
    std::string_view              name;
    std::vector<std::string_view> parameters;   // "vue : STRING"
    std::string_view              returns;
    std::string_view              help;
    std::string_view              snippet;      // '|' : le curseur
    bool                          chain{false}; // la liste se rouvre (une vue, un script...)
};
[[nodiscard]] const std::vector<Function>& functions();
[[nodiscard]] const Function* function(std::string_view name) noexcept;   // sans casse

// La signature d'un appel : IHM_ d'abord, puis l'automate (DFB, instances,
// fonctions et blocs de la bibliotheque).
[[nodiscard]] bool signature(const domain::Project* plc, std::string_view name, ui::MultiLineText::Signature& out);
// Lot 7 : les fonctions IHM du projet d'abord (leurs VAR_INPUT), puis comme ci-dessus.
[[nodiscard]] bool signature(const hmi::Project* hp, const domain::Project* plc, std::string_view name,
                             ui::MultiLineText::Signature& out);

// ------------------------------------------------------------- description ---
struct Description {
    bool        found{false};
    bool        keyword{false};   // un mot du langage : rien a en dire
    std::string type;             // BOOL, REAL, T_ANA...
    std::string line;             // "Mode_Maintenance : BOOL   variable IHM   initiale FALSE   // ..."
};
// Un nom (ou un chemin a[0].b.c) : variable IHM, variable (ou membre) de
// l'automate, fonction IHM_, bloc. `found` faux : inconnu (la barre en ambre).
// `code` (lot 7) : le script ou la fonction ou se trouve le nom - ses
// variables locales passent avant tout le reste.
[[nodiscard]] Description describe(const hmi::Project&, const domain::Project* plc, std::string_view symbol,
                                   std::string_view code = {}, hmi::Id op = hmi::kNoId);

// Les membres d'un chemin de l'automate ("Armoires[0].ana") : nom, type, commentaire.
struct MemberInfo { std::string name, type, comment; bool structured{false}; };
[[nodiscard]] bool members(const domain::Project& plc, std::string_view path, std::vector<MemberInfo>& out);

// ------------------------------------------------------------- brancher -----
//  Tout ce qu'un editeur de script IHM demande : la completion (et les membres
//  apres un point), la signature des appels, l'infobulle au survol. `live` :
//  en simulation, la valeur du moment ("= 42") ; faux sinon.
struct Sources {
    std::function<const hmi::Project*()>                    hmi;
    std::function<std::shared_ptr<const domain::Project>()> plc;
    std::function<bool(std::string_view, std::string&)>     live;
    // 1.10.1 (U2) : l'editeur montre le script de cet operateur (kNoId : un autre
    // script) - a, b, Resultat et leurs types (suggest, describe).
    std::function<hmi::Id()>                                op;
    // 1.11.18 (refonte, lot 3) : les declarations du modele du code montre, reconstruites
    // (declarationsPrefix) : l'aide les lit comme si elles etaient tapees en tete de la
    // ligne 1 - ses constantes, variables et parametres sont proposes. Vide : aucune.
    std::function<std::string()>                            declarations{};
};
// Le texte des declarations du modele d'un code, tel que le moteur le lit devant sa ligne 1
// (hmi::decl::composeCode) ; vide : aucune.
[[nodiscard]] std::string declarationsPrefix(const std::vector<hmi::Declaration>&, hmi::decl::Role,
                                             const std::vector<hmi::Declaration>* inherited = nullptr);
void attach(ui::MultiLineText&, Sources);

// La meme aide pour un champ d'une ligne (une variable, une expression, un
// texte a trous) : ce que ui::InputText::setAssist attend.
[[nodiscard]] ui::InputText::Assist fieldAssist(Sources, bool templateText = false);

// Le programme de l'automate et la simulation, pour tous les volets : poses
// une fois par l'ecran, comme les services de plateforme des widgets.
void installProgram(std::function<std::shared_ptr<const domain::Project>()> plc,
                    std::function<bool(std::string_view, std::string&)> live = {});
[[nodiscard]] Sources sourcesFor(hmi::DocumentPtr doc);

// LES GRILLES DE PROPRIETES : quelle aide pour quelle case, d'apres son nom.
//   un texte a trous      Texte, Message               (les variables dans { })
//   une expression        Condition, Expression surveillee, Variable, Variable API,
//                         Expression d'autorisation, et tout "... (expression)"
//                         (l'animation : Visibilite, Couleur dynamique...)
//   une liste (a; b)      Variables archivees, Plumes : variables tracees
//   toute autre case      apres "=" (la piloter par une expression)
[[nodiscard]] ui::PropertyGrid::FieldAssistFor gridAssist(Sources);
// Une case de tableau, un libelle : un texte a trous ({ ouvre la liste des
// variables), ou "=expression" (la liste des variables et fonctions).
[[nodiscard]] ui::InputText::Assist templateOrExpression(Sources);

// Lot 12 : les variables que la bibliotheque propose (onglet Variables) -
// celles de l'automate (globales ; un tableau de 16 elements au plus, element
// par element ; une structure avec ses membres), puis les variables IHM.
[[nodiscard]] std::vector<hmi::design::VarInfo> designVariables(const hmi::Project&, const domain::Project* plc);
// Les membres d'une variable structuree de l'automate, pour generer sa vue.
[[nodiscard]] std::vector<hmi::design::VarInfo> designMembers(const domain::Project& plc, std::string_view path);

// Les couleurs deja employees dans le projet (fonds de vue, remplissages,
// contours, textes...), les plus frequentes d'abord : la rangee "Dans le
// projet" de la palette, pour garder un synoptique coherent.
[[nodiscard]] std::vector<std::string> projectColors(const hmi::Project&, std::size_t max = 10);

} // namespace app::assist
