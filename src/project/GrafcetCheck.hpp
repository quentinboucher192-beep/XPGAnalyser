// =============================================================================
//  project/GrafcetCheck.hpp - le vocabulaire du GRAFCET, la structure et les
//  controles d'un grafcet (1.10, chantier R)
// -----------------------------------------------------------------------------
//  Tout ce que l'editeur dit d'un grafcet sans le dessiner, et sans fenetre :
//
//    * LE VOCABULAIRE : les douze genres d'actions du moteur (K0..K11) dits en
//      francais - "continue", "retardee de 5 s", "limitee a 10 s" - avec leur
//      code moteur en petit ; un lecteur n'a pas a se souvenir de ce que k=11
//      voulait dire.
//
//    * LA STRUCTURE (IEC 60848) : divergence et convergence en OU (une etape a
//      plusieurs transitions de sortie / d'entree : un trait simple) et en ET
//      (une transition a plusieurs destinations / sources : deux traits
//      paralleles). Lue dans le modele, jamais dans le dessin.
//
//    * LES RACCOURCIS des receptivites, dans les deux sens : ce que le programme
//      dit (Steps_DetoxalA[1].ActiveTime >= t#1s AND Acts_DetoxalA[0].Started AND
//      Acts_DetoxalA[0].Finished) et ce qu'un lecteur lit (DUREE(X1) >= t#1s AND
//      FIN(A0)). Les memes que la macro ImporterGrafcet : FIN(An), ACTIF(Xn),
//      DUREE(Xn), FINI(Autre). L'aller-retour est exact : developper ce qu'on a
//      rendu lisible redonne le texte du programme, octet pour octet.
//
//    * LES CONTROLES, ceux de VerifierClasseur / ImporterGrafcet, sur le
//      grafcet tel que le programme le construit : chacun dit sur quoi il porte
//      (etape, transition, action) pour que le dessin le montre a sa place.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"
#include "Grafcet.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace grafcet {

    // ---- le vocabulaire --------------------------------------------------------

    // "K4" : le code du moteur, montre en petit a cote du libelle.
    [[nodiscard]] std::string kindCode(ActionKind);
    // Le qualificatif court, dans la case de gauche du rectangle d'action :
    // N (continue), P1 (a l'activation), D (retardee), L (limitee), P0 (a la
    // desactivation), C... (conditionnelle). Celui de la norme quand il existe.
    [[nodiscard]] std::string_view kindQualifier(ActionKind);
    // Le genre en francais, avec sa duree quand le genre en a une :
    // "retard\xC3\xA9" "e de 5 s", "limit\xC3\xA9" "e \xC3\xA0 10 s". `delay` est le
    // texte du programme (t#5s, T#500ms, une expression) ; une expression est
    // laissee telle quelle.
    [[nodiscard]] std::string kindLabel(ActionKind, std::string_view delay = {});
    // Ce qui la fait sortir, en une phrase (pour l'infobulle et les proprietes).
    [[nodiscard]] std::string_view kindMeaning(ActionKind);
    // Les douze, dans l'ordre du moteur, pour une liste de choix.
    [[nodiscard]] std::vector<ActionKind> allKinds();
    // "t#5s" -> "5 s", "T#1500ms" -> "1,5 s", "t#2m" -> "2 min". Vide si ce n'est
    // pas un litteral de duree (une expression reste a afficher telle quelle).
    [[nodiscard]] std::string durationText(std::string_view literal);

    // ---- la structure ------------------------------------------------------------

    enum class BranchKind : std::uint8_t {
        OrDivergence,    // une etape, plusieurs transitions de sortie
        OrConvergence,   // une etape, plusieurs transitions d'entree
        AndDivergence,   // une transition, plusieurs destinations
        AndConvergence,  // une transition, plusieurs sources
    };

    struct Branch {
        BranchKind       kind{ BranchKind::OrDivergence };
        int              at{ -1 };       // l'etape (OU) ou la transition (ET)
        std::vector<int> members;        // les transitions (OU) ou les etapes (ET)
    };

    // Toutes les divergences et convergences, dans l'ordre des etapes puis des
    // transitions. Une auto-boucle (une transition de X3 vers X3) compte des deux
    // cotes, comme le moteur la voit.
    [[nodiscard]] std::vector<Branch> branches(const Chart&);
    [[nodiscard]] std::string_view branchName(BranchKind);

    // Les transitions qui sortent d'une etape / qui y entrent.
    [[nodiscard]] std::vector<int> transitionsFrom(const Chart&, int stepId);
    [[nodiscard]] std::vector<int> transitionsInto(const Chart&, int stepId);
    [[nodiscard]] const Transition* transitionById(const Chart&, int id);
    [[nodiscard]] const Action* actionById(const Chart&, int id);

    // ---- les raccourcis ------------------------------------------------------------

    // Le texte du programme rendu lisible : Steps_P[3].Active -> ACTIF(X3),
    // Steps_P[3].ActiveTime -> DUREE(X3), Acts_P[2].Started AND Acts_P[2].Finished
    // -> FIN(A2), (Gc_Autre.Finished AND NOT Ctrl_Autre.Cmd.InitReq) -> FINI(Autre),
    // et Gc_Autre.Finished seul -> FINI?(Autre) n'est PAS invente : il reste tel
    // quel (ce n'est pas la garde exacte, le lecteur doit le voir).
    // Hors commentaires et chaines.
    [[nodiscard]] std::string readableCondition(const Chart&, std::string_view expr);

    // L'inverse : FIN(A2), ACTIF(X3), DUREE(X3), FINI(Autre) developpes dans la
    // forme du programme. Ce qui n'est pas un raccourci passe tel quel.
    // `engineOf` donne le nom de l'instance du moteur d'un autre grafcet (vide :
    // "Gc_" + le nom) ; `ctrlOf` celui de sa variable de commande.
    [[nodiscard]] std::string expandShortcuts(const Chart&, std::string_view text,
        const std::vector<Chart>* others = nullptr);

    // Un raccourci trouve dans un texte (lisible) : FIN/ACTIF/DUREE/FINI et sa cible.
    struct Shortcut {
        std::string word;     // "FIN", "ACTIF", "DUREE", "FINI", "INIT"
        std::string target;   // "A2", "X3", "Autre"
        std::size_t offset{ 0 }, length{ 0 };
    };
    [[nodiscard]] std::vector<Shortcut> shortcutsIn(std::string_view readable);

    // Les grafcets qu'un grafcet cite (FINI(Autre) dans une receptivite, INIT(Autre)
    // ou Ctrl_Autre.Cmd.InitReq dans un corps d'action) : les liens cliquables.
    struct ChartLink {
        std::string other;          // le nom de l'autre grafcet, ou de son instance
        bool        launches{ false };  // INIT : celui-ci lance l'autre ; sinon il l'attend
        char        part{ 'T' };    // 'T' transition, 'A' action
        int         id{ -1 };
    };
    [[nodiscard]] std::vector<ChartLink> chartLinks(const Chart&, const std::vector<Chart>& all);

    // ---- les controles ---------------------------------------------------------------

    enum class CheckKind : std::uint8_t {
        NoExitNotFinal,     // etape sans transition de sortie qui n'est pas finale
        Unreachable,        // etape jamais atteinte depuis une etape initiale
        NoInitial,          // pas d'etape initiale
        DuplicateId,        // deux etapes / transitions / actions de meme numero
        TooManyEnds,        // plus de 3 sources ou destinations
        FinOfContinuous,    // FIN d'une action continue dans la receptivite de son etape
        ShortcutToMissing,  // raccourci (ou tableau) vers ce qui n'existe pas
        UnknownVariable,    // variable inexistante dans une receptivite / condition
        NoCondition,        // transition sans receptivite : ne franchit jamais
        DanglingLink,       // transition ou action vers une etape qui n'existe pas
    };

    struct Check {
        CheckKind   kind{ CheckKind::NoExitNotFinal };
        bool        severe{ true };   // faux : une remarque (le grafcet marche)
        char        part{ 0 };        // 'X' etape, 'T' transition, 'A' action, 0 le grafcet
        int         id{ -1 };
        std::string message;          // en francais, une phrase
    };

    [[nodiscard]] std::string_view checkTitle(CheckKind);

    // Tous les controles d'un grafcet. `project` (peut etre nul) sert aux
    // variables inexistantes ; `all` (peut etre nul) aux FINI vers un grafcet absent.
    [[nodiscard]] std::vector<Check> runChecks(const Chart&, const domain::Project* project,
        const std::vector<Chart>* all = nullptr);

    // Les noms de variables d'une expression ST : la racine de chaque chemin
    // (armoires dans armoires[0].entrees.detoxal), sans mots cles, fonctions,
    // litteraux ni constantes de duree. Pour le controle et pour l'infobulle des
    // valeurs en simulation.
    [[nodiscard]] std::vector<std::string> variablePaths(std::string_view expr);
    [[nodiscard]] std::vector<std::string> variableRoots(std::string_view expr);

} // namespace grafcet
