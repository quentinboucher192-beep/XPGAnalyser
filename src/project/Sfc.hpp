// =============================================================================
//  project/Sfc.hpp - 1.51.0 (S) : un GRAFCET natif de Control Expert (SFC), lu,
//  ecrit, relie
// -----------------------------------------------------------------------------
//  Une section SFC est gardee telle que Control Expert l'a ecrite : tout le
//  <SFCProgram>...</SFCProgram> (domain::keepsSourceXml). Ceci en fait un MODELE
//  que l'editeur et le simulateur lisent, et qui se reecrit :
//
//    <SFCProgram areaNum="0" operatorCtrl="0">
//      <identProgram name="G7" type="section" task="MAST"></identProgram>
//      <chartSource name="Chart">
//        <networkSFC>
//          <step stepType="initialStep" stepName="S_0">       initialStep, step,
//            <objPosition posX="0" posY="0"></objPosition>     macroStep, inStep, outStep
//            <action qualifier="N">                            N NONE S R L D P P0 P1 DS SD SL
//              <actionName><variableName>V</variableName></actionName>   (ou sectionName)
//              <tValue><tLiteral>t#5s</tLiteral></tValue>      (L, D, DS, SD, SL)
//            </action>
//            <literals max="" min="t#1s" delay=""></literals>  supervision, retard
//            <comment>...</comment>
//          </step>
//          <transition>
//            <objPosition .../>
//            <transitionCondition invertLogic="false">
//              <variableName>V</variableName>   ou <sectionName>T1</sectionName>
//            </transitionCondition>
//          </transition>
//          <altBranch width="2" relativePos="0"> <altJoint ...>   divergence / convergence en OU
//          <parBranch ...> <parJoint ...>                         en ET
//          <jumpSFC stepName="S_0">                               saut
//          <linkSFC> directedLinkSource / directedLinkDestination / gridObjPosition
//          <textBox width="2" height="1">texte<objPosition .../></textBox>
//        </networkSFC>
//      </chartSource>
//      <transitionSource name="T1"><LDSource>...</LDSource></transitionSource>
//      <actionSource name="A1"><STSource>...</STSource></actionSource>
//    </SFCProgram>
//
//  LA GRILLE EST LA VERITE. Rien dans le fichier ne dit qu'une etape mene a une
//  transition : elles sont l'une sous l'autre (meme colonne, rangees qui se
//  suivent). Une divergence est un trait en haut de sa rangee (ses sorties : les
//  objets de cette rangee qu'elle couvre), une convergence un trait en bas de la
//  sienne (sa sortie : la case sous sa colonne relativePos). Un lien (linkSFC)
//  relie ce qui n'est pas voisin. resolve() en tire le graphe : pour chaque
//  transition, ses etapes d'amont et d'aval.
//
//  OCTET POUR OCTET. Chaque element garde son XML et l'espace qui le precede ;
//  write() ne refait que ce qui a ete modifie (dirty), ajoute ou retire. Un
//  graphe relu et reecrit sans modification est identique a l'original, et une
//  modification ne change que les lignes de l'element touche.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sfc {

    enum class ItemKind : std::uint8_t {
        Step, Transition, AltBranch, AltJoint, ParBranch, ParJoint, Jump, Link, TextBox, Other,
    };
    enum class StepKind : std::uint8_t { Initial, Normal, Macro, In, Out };
    // Ce que dit la receptivite : une variable (ou une adresse, un litteral), une
    // section de transition (ST, LD, FBD, IL), une expression ecrite en ligne (pas
    // du format de Control Expert : lue si on la trouve, jamais ecrite).
    enum class CondKind : std::uint8_t { None, Variable, Section, Expression };

    struct Action {
        std::string qualifier{ "N" };   // tel qu'ecrit : "NONE" vaut N
        bool        isSection{ false }; // actionName/sectionName (sinon variableName)
        std::string name;
        std::string time;               // t#5s (tLiteral) ou une variable TIME
        bool        timeIsVariable{ false };
    };

    struct Item {
        ItemKind kind{ ItemKind::Other };
        std::string tag;                // le nom de l'element XML
        int x{ 0 }, y{ 0 };             // objPosition
        // ---- step / jumpSFC (stepName de la cible)
        std::string name;
        StepKind    stepKind{ StepKind::Normal };
        std::vector<Action> actions;
        bool        hasLiterals{ false };
        std::string tmin, tmax, delay;  // literals min / max / delay
        bool        hasComment{ false };
        std::string comment;
        // ---- transition
        CondKind    cond{ CondKind::None };
        std::string condText;           // la variable, le nom de la section, l'expression
        bool        invert{ false };
        // ---- branches
        int width{ 1 }, relativePos{ 0 };
        // ---- linkSFC
        std::string srcType, dstType;
        int sx{ 0 }, sy{ 0 }, dx{ 0 }, dy{ 0 };
        std::vector<std::pair<std::string, std::string>> bends;   // gridObjPosition, tels quels
        // ---- textBox
        int boxW{ 1 }, boxH{ 1 };
        std::string text;

        // ---- le XML
        std::vector<std::pair<std::string, std::string>> attrs;   // les attributs lus, dans l'ordre (decodes)
        std::vector<std::string> extraChildren;                   // les enfants inconnus, tels quels
        std::string gap;     // l'espace qui le precede dans le fichier
        std::string xml;     // son XML d'origine (vide : a generer)
        bool        dirty{ false };

        [[nodiscard]] bool isBar() const noexcept {
            return kind == ItemKind::AltBranch || kind == ItemKind::AltJoint
                || kind == ItemKind::ParBranch || kind == ItemKind::ParJoint;
        }
        [[nodiscard]] bool isDivergence() const noexcept {
            return kind == ItemKind::AltBranch || kind == ItemKind::ParBranch;
        }
        [[nodiscard]] bool isConvergence() const noexcept {
            return kind == ItemKind::AltJoint || kind == ItemKind::ParJoint;
        }
        // Un objet d'une case : etape, transition, saut.
        [[nodiscard]] bool isCellObject() const noexcept {
            return kind == ItemKind::Step || kind == ItemKind::Transition || kind == ItemKind::Jump;
        }
        [[nodiscard]] bool initial() const noexcept { return kind == ItemKind::Step && stepKind == StepKind::Initial; }
    };

    // Une section de transition ou d'action (transitionSource / actionSource).
    struct Source {
        std::string tag;                // transitionSource, actionSource...
        std::string name;
        domain::PouLanguage language{ domain::PouLanguage::Unknown };
        std::string innerTag;           // STSource, LDSource...
        // ST / IL : le texte decode ; LD / FBD : le XML de l'element (<LDSource>...</LDSource>)
        std::string body;
        std::string gap, xml;
        bool        dirty{ false };
    };

    struct Chart {
        bool        ok{ false };
        bool        editable{ false };   // un networkSFC trouve : on sait ou ecrire
        std::string error;
        std::string programName;         // identProgram/@name
        std::string eol{ "\r\n" };
        std::string itemIndent{ "\t\t\t\t" };
        std::string sourceIndent{ "\t\t" };

        std::string head;                // jusqu'a <networkSFC> compris
        std::vector<Item> items;
        std::string mid;                 // de la fin du dernier element a </chartSource> compris
        std::vector<Source> sources;
        std::string tail;                // apres la derniere source : </SFCProgram>
        bool        structureChanged{ false };   // ajout ou retrait
        std::string original;

        [[nodiscard]] int findStep(std::string_view name) const;          // -1 : aucune (sans la casse)
        [[nodiscard]] int itemAt(int x, int y) const;                     // un objet d'une case, -1
        [[nodiscard]] const Source* source(std::string_view tag, std::string_view name) const;
        [[nodiscard]] Source* source(std::string_view tag, std::string_view name);
        [[nodiscard]] std::vector<int> steps() const;
        [[nodiscard]] std::vector<int> transitions() const;
        [[nodiscard]] bool modified() const noexcept;
        // La derniere rangee occupee (-1 : vide).
        [[nodiscard]] int lastRow() const;
        [[nodiscard]] int lastColumn() const;
    };

    // Lit un <SFCProgram>...</SFCProgram> (ou un <SFCSource>) ; ok faux : illisible.
    [[nodiscard]] Chart parse(const std::string& xml);

    // Le XML : l'original quand rien n'a change, sinon chaque element tel qu'il
    // etait, sauf ceux qui ont change (refaits), ajoutes ou retires.
    [[nodiscard]] std::string write(const Chart& chart);

    // Le XML d'un element tel que write() le refait (pour les essais et l'apercu).
    [[nodiscard]] std::string itemXml(const Item& item, const std::string& indent, const std::string& eol);
    [[nodiscard]] std::string sourceXml(const Source& s, const std::string& indent, const std::string& eol);

    // Un GRAFCET neuf : une etape initiale (Control Expert : S_0 en 0,0 n'est pas
    // obligatoire, mais une etape initiale l'est). `depth` : l'indentation de la
    // balise SFCProgram dans le fichier (1 : une section de la tache, 2 : d'une unite).
    [[nodiscard]] std::string newProgram(const std::string& sectionName, const std::string& task,
                                         const std::string& initialStep, int depth = 1,
                                         const std::string& eol = "\r\n");

    // Le qualificatif normalise : "NONE" -> "N", en majuscules.
    [[nodiscard]] std::string normalQualifier(std::string_view q);
    // Un qualificatif qui demande une duree (L, D, DS, SD, SL).
    [[nodiscard]] bool qualifierTimed(std::string_view q);
    // Les qualificatifs que l'editeur propose, dans l'ordre de Control Expert.
    [[nodiscard]] const std::vector<std::string>& qualifiers();
    // Le sens d'un qualificatif, en francais.
    [[nodiscard]] std::string qualifierMeaning(std::string_view q);

    // ---------------------------------------------------------------- le graphe
    // Un lien du dessin : de l'objet `from` a l'objet `to` (indices dans items ;
    // une divergence ou une convergence est un objet), par un linkSFC ou non.
    struct Edge {
        int from{ -1 }, to{ -1 };
        int link{ -1 };       // l'element linkSFC qui le porte, -1 : voisins
        int column{ -1 };     // la colonne de la branche concernee (sortie d'une divergence, entree d'une convergence)
    };

    struct TransitionLinks {
        int              item{ -1 };
        std::vector<int> sources;   // etapes d'amont (indices dans items) : toutes actives pour franchir
        std::vector<int> targets;   // etapes d'aval (un saut est suivi jusqu'a son etape)
        bool             andJoin{ false }, andSplit{ false };
    };

    struct Topology {
        std::vector<Edge>            edges;
        std::vector<TransitionLinks> transitions;
        // En francais, ce qui ne tient pas : un saut vers une etape inconnue, une
        // transition sans etape d'amont, deux etapes de meme nom...
        std::vector<std::pair<int, std::string>> problems;   // (element, message), element -1 : le graphe

        [[nodiscard]] const TransitionLinks* of(int item) const;
        [[nodiscard]] std::vector<int> into(int step) const;    // transitions qui menent a l'etape
        [[nodiscard]] std::vector<int> outOf(int step) const;   // transitions qui en partent
    };

    [[nodiscard]] Topology resolve(const Chart& chart);

    // Les noms lus par la receptivite d'une transition et les actions d'une etape
    // (pour les references croisees et le renommage).
    [[nodiscard]] std::string conditionText(const Chart& chart, const Item& transition);

} // namespace sfc
