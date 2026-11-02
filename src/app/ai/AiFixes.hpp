// =============================================================================
//  app/ai/AiFixes.hpp - 1.22.1 : CORRIGER UNE PROPOSITION DEPUIS SES ERREURS
// -----------------------------------------------------------------------------
//  Le clic droit sur une erreur (ou un avertissement) de l'apercu et de la carte
//  de la conversation ouvre le menu du clic droit de l'appli (ui::PopupMenu)
//  avec des actions ADAPTEES A L'ERREUR, lues dans ses defauts structures
//  (Check::faults) - jamais dans son texte :
//    une variable introuvable    << Utiliser << Y >> >> (les plus proches, 5 au plus),
//                                << Choisir une variable... >> (le choix de l'apercu),
//                                << Creer la variable << X >> >> (un type a choisir,
//                                le conseille d'abord ; un membre << Vanne.Cmd >> se
//                                cree << Vanne_Cmd >>), << Retirer les operations qui
//                                l'utilisent >> ;
//    une erreur d'une operation  << Retirer cette operation >> (Proposal::excluded :
//                                ce qui en depend suit, comme la bande du plan) ;
//    toujours                    << Copier le message >>, << Demander a l'IA de
//                                corriger >> (le texte prepare dans la saisie : rien
//                                ne part seul).
//
//  LA PROPOSITION CHANGE, JAMAIS LE PROJET (Assistant::chooseVariable, clearChoice,
//  excludeOps) : elle est gardee avec la conversation et se revalide par son chemin
//  habituel - l'erreur disparait, la categorie << Corrections >> dit ce qui a ete
//  fait (<< 7 variables remplacees >>) et chaque correction se reprend (le clic
//  droit : << Revenir >>). Appliquer applique ensuite la proposition corrigee. Une
//  variable << creee >> l'est parce que l'utilisateur l'a demande : l'Assistant
//  n'invente toujours aucune variable.
//
//  LES DOUBLONS : deux erreurs identiques (deux operations sur la meme variable
//  introuvable) se lisent sur une seule ligne << ... (2 operations) >>.
//
//  1.22.7 : CORRIGER RAPIDEMENT - en TETE du menu de chaque erreur et de chaque
//  avertissement (de l'apercu, de la carte ; les constats de GENERER ET COMPILER
//  aussi) : LA meilleure correction SURE que le logiciel sait faire pour cette
//  ligne, posee dans la PROPOSITION (jamais dans le projet), lue dans ses defauts
//  structures (jamais dans son texte). Jamais d'invention : pas de variable
//  inventee, pas de nom choisi au hasard. Ligne par ligne (quick) :
//    une variable introuvable    << Utiliser << Y >> >> si Y est NETTEMENT la plus
//                                proche (clearlyClosest : le meme nom aux separateurs
//                                pres - << Vanne.Cmd >> / Vanne_Cmd -, sinon les memes
//                                mots, un mot pouvant etre l'abrege de l'autre - Cmd /
//                                Commande -, sinon une faute de frappe de deux lettres
//                                au plus ; les chiffres toujours les memes : Pompe_2
//                                n'est jamais Pompe_1 ; et UNE seule candidate au
//                                meilleur rang) ; sinon grisee, le menu dit pourquoi
//                                (aucune nettement, ou plusieurs autant) et << Choisir
//                                une variable... >> reste juste dessous ;
//    un constat du logiciel      sa correction (fixFrom -> fixTo : Assistant::
//    avec sa correction          applyBuildFix) ;
//    une SUPPRESSION refusee     (elle a des dependances directes, sa strategie est
//    parce que l'element est     << seul >>) : sa strategie passe a << references >>
//    cite ailleurs               (Assistant::setDeleteStrategy : seules les references
//                                qui le citent partent ; un type, un dossier : pas de
//                                << references >> qui tienne - l'operation est retiree) ;
//    une autre erreur d'une      la retirer (excludeOps : ce qui en depend suit) - un
//    operation (ou un constat    constat du logiciel sans correction aussi ;
//    d'erreur du logiciel)
//    une erreur sans operation   grisee : rien de sur (la proposition entiere, perimee) ;
//    des objets sous l'en-tete   si une operation sait les deplacer (un modifier_vue,
//    ou le pied (1.22.6)         un creer_vue avec << decaler : non >>) : << decaler :
//                                oui >> (Assistant::setOperationProperty) ; sinon grisee ;
//    << N objets crees ne sont   grisee : relier un objet, c'est choisir sa variable -
//    relies a aucune variable >> l'Assistant n'en invente pas ;
//    un autre avertissement      grisee, avec la raison.
//  << TOUT CORRIGER RAPIDEMENT >> (applyAll) : toutes les corrections rapides sures
//  d'une categorie en une fois - les corrections du logiciel d'abord (tout autre
//  changement efface le verdict), les retraits en une seule ecriture.
//  Les RETOUCHES (une strategie, une propriete, la correction du logiciel) sont
//  gardees avec la forme d'avant de l'operation (Proposal::extra << retouches >>) :
//  la categorie CORRECTIONS les liste, Revenir les reprend (Undo).
//
//  1.22.8 : UNE UNITE INCONNUE (<< unite inconnue << mmCE >> >>, un defaut << unite >>
//  de creer_variables, modifier_variable) : << Creer l'unite << mmCE >>... >> ouvre une
//  petite fenetre (sa famille, son facteur, son libelle ; PRE-REMPLIE quand le logiciel
//  reconnait le symbole - la table des unites courantes, hmi::units::knownUnits) ;
//  Corriger rapidement la cree SEULEMENT si le symbole est dans cette table (sinon
//  grisee : famille et facteur inconnus). Dans les deux cas, une operation creer_unite
//  AJOUTEE A LA PROPOSITION (Assistant::addUnit : une retouche << ajout >>, Revenir la
//  retire) - jamais au projet ; Tout corriger rapidement n'en cree qu'une par symbole
//  (26 variables en mmCE : une seule unite).
// =============================================================================
#pragma once

#include "AiOperations.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class PopupMenu; }

namespace app::ai {

class Assistant;

namespace fixes {

// Une ligne d'erreur ou d'avertissement, telle que l'apercu et la carte la montrent.
struct Line {
    std::string              message;      // le message, tel que la validation l'a ecrit
    std::string              text;         // ce qui se lit : le message, et << (2 operations) >> pour un doublon
    std::size_t              count{1};     // combien de fois la validation l'a dit
    std::vector<std::size_t> ops;          // les operations en cause (indices de Proposal::ops, sans doublon)
    std::vector<std::size_t> faults;       // ses defauts structures (indices de Check::faults ; une erreur seulement)
    bool                     error{true};
    // 1.22.7 : un constat du logiciel (GENERER ET COMPILER) - son rang dans le verdict de la proposition (kNone : une ligne de
    // la verification) et la correction du logiciel (fixFrom -> fixTo ; vide : aucune). Son menu offre alors << Appliquer la
    // correction du logiciel >>, et Corriger rapidement la fait.
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t              finding{kNone};
    std::string              fixFrom{}, fixTo{};
};
[[nodiscard]] std::vector<Line> errorLines(const Check&);
[[nodiscard]] std::vector<Line> warningLines(const Check&);

// Une entree du menu. `children` : un sous-menu (Creer la variable : les types) - l'entree elle-meme ne se choisit pas.
struct Action {
    enum class Kind : std::uint8_t { Separator, Use, Pick, Create, RemoveUsing, RemoveOps, Copy, AskAi, Revert, Restore,
                                     BuildFix /* 1.22.5 : la correction du logiciel d'un constat (ops[0] : son rang dans le verdict) */,
                                     // 1.22.7 : Corriger rapidement (`does` : ce qui est fait, avec ses champs) ; la strategie d'une
                                     // suppression (ops[0], value) ; une propriete d'une operation (ops[0], name : la cle, value) ;
                                     // revenir sur une retouche (value : son id) ; Tout corriger rapidement (ops[0] : la categorie,
                                     // a l'ecran qui l'offre).
                                     Quick, Strategy, Prop, Undo, QuickAll,
                                     // 1.22.8 : ajouter une unite a la proposition (name : le symbole ; unit : l'operation creer_unite) ;
                                     // << Creer l'unite... >> (name : le symbole) - la fenetre, a l'ecran qui l'offre.
                                     Unit, UnitAsk };
    Kind                     kind{Kind::Separator};
    Kind                     does{Kind::Separator};   // 1.22.7 : Quick - l'action faite (Use, BuildFix, RemoveOps, Strategy, Prop)
    std::string              label;
    std::string              note;         // la petite ligne sous le libelle
    std::string              name;         // la variable introuvable (Use, Pick, Create, RemoveUsing, Revert)
    std::string              value;        // Use : la variable a la place ; Create : le nom a creer ; Copy, AskAi : le texte
    std::string              type;         // Create : le type
    std::vector<std::size_t> ops;          // RemoveUsing, RemoveOps, Restore
    bool                     enabled{true};
    std::string              why;          // grisee : pourquoi
    std::vector<Action>      children;
    Operation                unit{};       // 1.22.8 : Unit - l'operation creer_unite ajoutee a la proposition
};
struct Menu {
    std::string         title;             // la ligne de tete du menu
    std::vector<Action> actions;
};
// Le menu d'une ligne d'erreur ou d'avertissement ; `project` (nul : inconnu) : une variable a creer qui existe deja se
// propose a l'usage. 1.22.7 : << Corriger rapidement >> en tete (quick), puis la correction du logiciel d'un constat qui en a
// une.
[[nodiscard]] Menu menuFor(const Proposal&, const Check&, const Line&, const hmi::Project* project);

// 1.22.7 : CORRIGER RAPIDEMENT une ligne (voir l'en-tete) - une action Quick : choisissable (`does` et ses champs : ce qui sera
// fait ; `note` : ce qui changera), ou grisee (`why`, redit dans `note` : pourquoi). `project` (nul : inconnu) : les variables
// du projet, ou chercher la nettement plus proche (sinon les plus proches de la validation).
[[nodiscard]] Action quick(const Proposal&, const Check&, const Line&, const hmi::Project* project);
// La variable NETTEMENT la plus proche d'un nom introuvable parmi `candidates` (vide : aucune, `why` dit pourquoi).
[[nodiscard]] std::string clearlyClosest(std::string_view missing, const std::vector<std::string>& candidates, std::string* why = nullptr);
// TOUT CORRIGER RAPIDEMENT : les actions Quick choisissables de `quick` (les grisees sont laissees), en une fois - les
// corrections du logiciel d'abord, les retraits en une seule ecriture. Le nombre de lignes corrigees ; `said` : ce que la
// barre d'etat dit.
std::size_t applyAll(Assistant&, int message, const std::vector<Action>& quick, std::string* said = nullptr);

// LES CORRECTIONS FAITES : une ligne chacune (un choix de variable, une operation retiree), et le geste qui la reprend.
struct Correction {
    std::string text;                      // "<< Vanne.Cmd >> -> Vanne_Ouverte", "operation 3 (associer Vanne_1)"
    std::string state;                     // "remplacee", "a creer (BOOL)", "retiree"
    int         what{1};                   // 0 : une variable a creer, 1 : remplacee, 2 : une operation retiree ; 1.22.7 : 3 retouchee
    Action      undo;                      // Revenir
    // 1.22.7 : ce qui l'identifie d'un etat de la proposition a l'autre ("c:vanne.cmd", "x:3", "r:r2") ; une retouche : son
    // operation.
    std::string key{};
    std::size_t op{Check::Fault::kNoOp};
};
[[nodiscard]] std::vector<Correction> corrections(const Proposal&);
[[nodiscard]] Menu menuFor(const Proposal&, const Correction&);
// Ce qui a ete corrige, en une ligne : "7 variables remplac\xC3\xA9" "es, 1 op\xC3\xA9ration retir\xC3\xA9" "e" ; vide : rien.
[[nodiscard]] std::string summary(const Proposal&);
// Ce qui, change par une correction, refait la verification (la cle de la carte, de l'apercu).
[[nodiscard]] std::string key(const Proposal&);
// L'operation `op`, en clair : "op\xC3\xA9ration 3 (associer Vanne_1)".
[[nodiscard]] std::string opLabel(const Proposal&, std::size_t op);
// Le texte prepare pour << Demander a l'IA de corriger >> (mis dans la saisie, jamais envoye seul).
[[nodiscard]] std::string askText(const Line&);
// Les types proposes pour creer une variable (le conseille vient en tete du sous-menu).
[[nodiscard]] const std::vector<std::string>& types();

// 1.22.8 : << CREER L'UNITE << X >>... >> - la petite fenetre. Ses champs (dans l'ordre : symbole, famille, facteur, decalage,
// libelle, puis une famille neuve : nom, reference, libelle) et leurs valeurs PRE-REMPLIES quand le logiciel reconnait le
// symbole (la table des unites courantes, ramenee au catalogue du projet) ; sinon a remplir. Le choix d'une famille :
// << pression (Pa) >>... ou kNewFamily ; un symbole inconnu commence par kPickFamily (aucune famille choisie d'office).
inline const std::string kNewFamily = "nouvelle famille\xE2\x80\xA6";
inline const std::string kPickFamily = "(\xC3\xA0 choisir)";
struct UnitForm {
    std::vector<std::string> labels, values;
    std::vector<std::string> families;     // les choix du champ Famille
    bool                     known{false};  // pre-remplie (la table connait le symbole)
    std::string              explanation;
};
[[nodiscard]] UnitForm unitForm(const hmi::Project&, std::string_view symbol);
// Les valeurs de la fenetre -> l'operation creer_unite.
[[nodiscard]] Operation unitFromForm(const std::vector<std::string>& values);
// Ce que la fenetre lit de ses valeurs : la ligne de la conversion (<< Unite mmCE (pression) : 1 mmCE = 9,80665 Pa >>, et un
// avertissement de coherence), ou la raison du refus - `field` : le champ en cause (-1 : aucun).
[[nodiscard]] std::string unitFormCheck(const hmi::Config&, const std::vector<std::string>& values, int* field = nullptr, bool* ok = nullptr);

// FAIRE une action qui change la proposition (Use, Create, RemoveUsing, RemoveOps, Revert, Restore, BuildFix ; 1.22.7 : Quick -
// ce qu'elle fait -, Strategy, Prop, Undo) - par l'Assistant ; `said` : ce que la barre d'etat dit. Faux : rien n'a change.
// Pick, Copy, AskAi et QuickAll sont a l'ecran qui les offre ; 1.22.8 : UnitAsk aussi (la fenetre), Unit ici (Assistant::addUnit).
bool apply(Assistant&, int message, const Action&, std::string* said = nullptr);

// LE MENU DU CLIC DROIT : ses entrees posees dans `menu` (une tete, les actions, leurs icones, les sous-menus) ; `byId` recoit
// les actions choisissables (l'id d'une entree est son rang dans `byId`). Les libelles montres (un sous-menu :
// "parent > enfant") et l'id d'un libelle (-1 : aucun) - pour les essais et les scripts.
void fill(ui::PopupMenu& menu, const Menu&, std::vector<Action>& byId);
[[nodiscard]] std::vector<std::string> labels(const ui::PopupMenu& menu);
[[nodiscard]] int idOf(const ui::PopupMenu& menu, std::string_view label);

} // namespace fixes
} // namespace app::ai
