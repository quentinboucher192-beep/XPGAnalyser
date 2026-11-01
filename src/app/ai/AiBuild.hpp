// =============================================================================
//  app/ai/AiBuild.hpp - 1.22.5 : L'AVIS DU LOGICIEL sur une proposition de l'IA
// -----------------------------------------------------------------------------
//  Le client (01/11) : << l'ia doit utiliser la fonction compiler / generer du
//  logiciel pour avoir l'avis du logiciel, choper les erreurs et avertissements,
//  les montrer en previsualisation ; si elle fait des modifs a cause de ca, le
//  montrer dans un processus de developpement (l'action courante), et modifier
//  pour corriger >>.
//
//  verdict() fait ce que font GENERER et COMPILER (hmi::generateWith,
//  hmi::compileWith, comme hmi::audit et la barre Generer / Compiler), sur une
//  COPIE du projet ou la proposition est appliquee (appliedCopy, malgre ses
//  erreurs de validation : ce qui s'applique), et garde LES CONSTATS NOUVEAUX -
//  ceux que le projet d'avant n'avait pas (les constats d'avant sont gardes en
//  cache par empreinte du projet : un deuxieme verdict sur le meme projet ne
//  les recalcule pas). Chaque constat dit son etape (Generer, Compiler), sa
//  severite (erreur, avertissement), son message tel que la fenetre Sorties
//  l'ecrit, son endroit, et l'objet (et l'operation) en cause quand il y en a un.
//
//  Il est LONG sur un gros projet (tout le projet est genere et compile) : il
//  tourne TOUJOURS hors du fil de l'interface (la verification de fond de
//  l'Assistant) ; jamais dans validate().
//
//  merge() le verse dans un Check : les erreurs dans `errors` (prefixees
//  << Generer : >> / << Compiler : >>), les avertissements dans `warnings`, les
//  objets dans `spots` (les cadres de l'apercu), un Fault << build >> par
//  erreur. feedbackText() l'ecrit pour l'IA (le retour de l'outil, la demande de
//  correction).
// =============================================================================
#pragma once

#include "AiOperations.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace app::ai::build {

// Un constat NOUVEAU de Generer ou de Compiler (absent du projet d'avant la proposition).
struct Finding {
    bool        error{true};          // une erreur (bloquante) ; faux : un avertissement
    std::string step;                 // "G\xC3\xA9n\xC3\xA9rer" ou "Compiler"
    std::string category;             // "Expression", "Script", "Objet", "Variable"... (celle de hmi::Issue)
    std::string message;              // tel que la fenetre Sorties l'ecrit
    std::string where;                // lisible : "Vue_Station . Btn_P2 . value", "script Calcul, ligne 3"
    std::string view;                 // le nom de la vue en cause (vide : hors d'une vue)
    hmi::Id     object{hmi::kNoId};   // l'objet en cause, dans la copie (kNoId : aucun)
    std::string objectName;
    std::string property;
    std::size_t op{Check::Fault::kNoOp};   // l'operation de la proposition qui touche cet objet (kNoOp : inconnue)
    std::string fixFrom, fixTo;       // la correction d'un clic du logiciel (hmi::Issue), vide : aucune
};

struct Verdict {
    bool                 ran{false};      // Generer et Compiler ont tourne sur la copie
    std::string          why;             // sinon pourquoi (la proposition ne s'applique pas du tout...)
    std::vector<Finding> findings;        // les constats nouveaux : les erreurs d'abord, puis les avertissements
    std::size_t          errors{0}, warnings{0};
    std::size_t          before{0}, after{0};   // tous les constats (erreurs + avertissements) avant / apres
    double               ms{0.0};         // la duree
    // "G\xC3\xA9n\xC3\xA9rer et Compiler : 2 erreurs, 1 avertissement nouveaux" ; "... : aucun constat nouveau" ; "pas lanc\xC3\xA9s : ...".
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] bool clean() const noexcept { return ran && errors == 0; }
};

// L'AVIS DU LOGICIEL : la proposition appliquee a une copie de `before`, Generer et Compiler, les constats nouveaux.
// `env` : celui de validate (fige pour un fil de fond : frozenEnv). Sur le fil qui l'appelle (pas celui de l'interface).
[[nodiscard]] Verdict verdict(const hmi::Project& before, const Proposal&, const Env& env = {});

// Verse le verdict dans un Check (voir l'en-tete) ; rien si le verdict n'a pas tourne.
void merge(Check&, const Verdict&);

// Le texte pour l'IA : ce que Generer et Compiler disent de sa proposition (les `max` premiers constats, l'endroit, la
// correction du logiciel quand il en a une), et la consigne de corriger. Vide : rien a dire (verdict propre).
[[nodiscard]] std::string feedbackText(const Verdict&, std::size_t max = 20);

// Le verdict en JSON (garde dans Proposal::extra << logiciel >>, relu a la relance) et retour.
[[nodiscard]] json::Value toJson(const Verdict&);
[[nodiscard]] Verdict     fromJson(const json::Value&);

// ESSAIS : remplace verdict() par `fake` (nul : le vrai). Les essais de l'Assistant (sans projet lourd) s'en servent.
void setVerdictForTests(std::function<Verdict(const hmi::Project&, const Proposal&)> fake);

// ---- ajouts (lot-mu) -----------------------------------------------------------------------------------------------------
// Le meme verdict, l'operation en cause lue d'abord dans les objets que la validation a notes (`validated` : le Check de
// validate sur `before` ; Check::spots). Sans lui (verdict ci-dessus), l'operation se retrouve par le nom de l'objet dans la
// proposition (celle qui le cree, le modifie, l'associe, le pose en code ; celle qui donne la propriete en cause d'abord).
[[nodiscard]] Verdict verdict(const hmi::Project& before, const Proposal&, const Env& env, const Check& validated);
// La ligne d'un constat telle que merge() la verse (errors, warnings, Spot::message, Fault::message) :
// "Compiler : Vue_Station . Voyant_1 . visible \xE2\x80\x94 <le message>".
[[nodiscard]] std::string lineOf(const Finding&);
// "2 erreurs, 1 avertissement nouveaux", "1 erreur nouvelle", "3 avertissements nouveaux", "aucun constat nouveau".
[[nodiscard]] std::string countText(const Verdict&);
// La duree a l'ecran : "320 ms", "1,4 s".
[[nodiscard]] std::string durationText(double ms);
// ESSAIS : combien de projets d'avant ont ete generes et compiles (le cache a manque) ; vider le cache.
[[nodiscard]] std::size_t beforeRunsForTests() noexcept;
void clearCacheForTests();

} // namespace app::ai::build
