// =============================================================================
//  app/SimDebugText.hpp - lot API 8 : Simulation > Debogage, sans ecran
// -----------------------------------------------------------------------------
//  Ce que l'onglet du debogage (SimDebugPane) et les points d'arret de
//  l'editeur de code calculent, sans widget ni moteur - donc testable
//  (tests/simdebugui_test.cpp) :
//
//   * les NOMBRES en francais : 1 243 ; 0,42 ms ; 12 % ; « < 0,1 % » ;
//   * les NOMS DES SECTIONS du simulateur : "SFC_PurgeA", "BLOC.Section" (le
//     corps d'un DFB), et la section du projet qu'ils designent ;
//   * un POINT D'ARRET TAPE : "SFC_PurgeA 42", "SFC_PurgeA:42",
//     "SFC_PurgeA, ligne 42", "SFC_PurgeA 42 si Armoires[0].etat = 3" ;
//   * LA PILE (MAST > Logigrammes_A > SFC_PurgeA > Gc_PurgeA (DFB_GRAFCETENGINE)
//     > Main), niveau par niveau : ce qu'il est, ce qu'un clic montre ;
//   * L'ETAT EN CLAIR (une phrase) et « POURQUOI ICI » (quelques phrases) ;
//   * OU PASSE LE TEMPS DU CYCLE (les sections, la plus lente en tete, en ms et
//     en % de la periode de MAST) et LA TRACE DU CYCLE (l'ordre de MAST) ;
//   * les NOMS d'une ligne de ST (les valeurs a droite de chaque ligne) ;
//   * les espions : depuis quand leur valeur a change.
// =============================================================================
#pragma once

#include "SimulationHost.hpp"
#include "../domain/ProjectModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sim { class Value; class Runtime; }   // Runtime : lot API 8 (2e partie), conditionPreview

namespace app::simdebug {

// ---- les nombres, en francais ---------------------------------------------------
// 1 243 : les milliers separes d'une espace fine insecable (comme l'onglet Simulation).
[[nodiscard]] std::string grouped(std::uint64_t n);
// Un temps de calcul : "0,042 ms", "1,25 ms", "12,5 ms", "1 204 ms".
[[nodiscard]] std::string millis(std::int64_t micros);
// Un pourcentage : "12 %", "4,5 %", "< 0,1 %", "0 %".
[[nodiscard]] std::string percent(double p);
// "1 fois", "3 fois" ; "1 instruction", "12 instructions".
[[nodiscard]] std::string plural(std::uint64_t n, std::string_view one, std::string_view many);
// Une valeur de la simulation comme l'onglet Automate l'ecrit : TRUE ; 12,5 ;
// 18 094 ; T#1s500ms ; 'texte'.
[[nodiscard]] std::string formatValue(const sim::Value& v);

// ---- les sections du simulateur ----------------------------------------------------
// Le nom qu'une section porte pour le simulateur : le sien ; le corps d'un DFB :
// "BLOC.Section". Vide : pas de section a cet indice.
[[nodiscard]] std::string sectionKey(const domain::Project& p, domain::Index section);
// La section du projet qu'un nom du simulateur designe (sans la casse) : le nom
// entier ; "Bloc.Section" (la section de ce nom dont le bloc est le
// proprietaire, a defaut la seule de ce nom). kNoIndex : aucune.
[[nodiscard]] domain::Index findSection(const domain::Project& p, std::string_view key);
// Deux noms du simulateur pour la meme section (sans la casse).
[[nodiscard]] bool sameSection(std::string_view a, std::string_view b);

// ---- un point d'arret tape --------------------------------------------------------
struct TypedBreakpoint {
    std::string section;
    int         line{0};          // 1 = la premiere
    std::string condition;        // apres « si » (ou « if ») ; vide : toujours
};
// "SFC_PurgeA 42", "SFC_PurgeA:42", "SFC_PurgeA, ligne 42", "SFC_PurgeA l.42",
// "DFB_X.Main 7 si x > 3". Faux : `why` dit ce qui manque.
bool parseBreakpoint(std::string_view text, TypedBreakpoint& out, std::string* why = nullptr);

// ---- la pile -------------------------------------------------------------------
struct StackLevel {
    enum class Kind : std::uint8_t { Task, Unit, Section, Instance, BlockSection, Other };
    Kind        kind{Kind::Other};
    std::string label;            // tel qu'ecrit : "Gc_PurgeA (DFB_GRAFCETENGINE)"
    std::string name;             // "Gc_PurgeA"
    std::string type;             // une instance : "DFB_GRAFCETENGINE"
    std::string section;          // le nom de simulateur de la section qu'un clic montre ("" : aucune)
    std::string instance;         // une section de bloc : le chemin de l'instance ("Gc_PurgeA", "a.b")
};
// La pile d'un passage, niveau par niveau. La premiere : la tache ; celle qui
// precede la premiere instance (ou la derniere) : la section ; entre les deux :
// les unites ; apres une instance « nom (TYPE) », la section de son bloc
// ("TYPE.Main"). Une pile vide : la section du passage seule.
[[nodiscard]] std::vector<StackLevel> stackLevels(const std::vector<std::string>& stack, const std::string& hitSection);
// Ce qu'un niveau dit en francais (l'infobulle) : "La tache MAST : ..."
[[nodiscard]] std::string levelTip(const StackLevel& level);

// ---- l'etat en clair -------------------------------------------------------------
enum class LastGesture : std::uint8_t { None, Continue, StepSection, StepCycle, Pause, RunToLine };
struct StateFacts {
    SimulationHost::State state{SimulationHost::State::Stopped};
    bool                  attached{false};
    std::uint64_t         cycle{0};
    std::string           haltMessage;          // deja en francais
    const SimBreakHit*    hit{nullptr};         // en pause sur un passage (nul sinon)
    const SimBreakpoint*  breakpoint{nullptr};  // le point d'arret du passage (nul : un pas, une pause)
    std::size_t           number{0};            // son rang dans la liste (1 = le premier)
    std::size_t           active{0};            // les points d'arret actifs
    std::size_t           total{0};             // tous
    std::string           nextSection;          // ce que « Section suivante » executera ("" : un cycle commence)
    LastGesture           gesture{LastGesture::None};
    std::string           runToLine;            // "SFC_PurgeA, ligne 42" : Executer jusqu'a la ligne en cours
};
// Une phrase : « En pause au point d'arret 2 : SFC_PurgeA, ligne 42, cycle 1 243
// - la condition Armoires[0].etat = 3 est vraie ».
[[nodiscard]] std::string stateSentence(const StateFacts& f);
// « Pourquoi ici » : ce qui a arrete le programme la, ce que la ligne fait
// maintenant, et ce que chaque bouton fera ensuite. Un paragraphe par element.
[[nodiscard]] std::vector<std::string> whyHere(const StateFacts& f);

// ---- le temps du cycle -----------------------------------------------------------
struct TimeBar {
    std::string   entry, section;
    std::string   label;           // "SFC_PurgeA" ; "Logigrammes_A > SFC_PurgeA" dans une unite
    std::int64_t  micros{0};
    std::uint64_t statements{0};
    double        ofPeriod{0.0};   // en % de la periode de MAST
    double        ofCycle{0.0};    // en % du cycle (la somme des sections)
};
// Les sections, la plus lente en tete (a egalite : l'ordre de MAST). Une periode
// nulle ou negative : ofPeriod reste a 0.
[[nodiscard]] std::vector<TimeBar> timeBars(const std::vector<SimSectionTime>& times, std::int64_t periodMs);
[[nodiscard]] std::int64_t totalMicros(const std::vector<SimSectionTime>& times);
// "Le cycle : 6,2 ms sur 20 ms (31 %) . la plus lente : SFC_PurgeA, 2,1 ms".
[[nodiscard]] std::string cycleSummary(const std::vector<SimSectionTime>& times, std::int64_t periodMs);

// ---- la trace du cycle ----------------------------------------------------------
struct TraceRow {
    std::size_t   rank{0};             // 1, 2, 3... : l'ordre de MAST
    std::string   entry, section;
    std::uint64_t statements{0};
    std::int64_t  micros{0};
    bool          firstOfEntry{false}; // la premiere section de son entree
    bool          active{true};        // 1.10.2 : sa condition d'activation etait vraie
    std::string   condition;           // 1.10.2 : sa condition d'activation (vide : aucune)
};
[[nodiscard]] std::vector<TraceRow> traceRows(const std::vector<SimSectionTime>& times);

// ---- les noms d'une ligne de ST ---------------------------------------------------
// Les variables qu'une ligne nomme, dans l'ordre, sans doublon : les chemins
// entiers (Armoires[0].ana.PT1.mes, x[i]), sans les mots du langage, les
// nombres, les litteraux types (T#2s, 16#FF), les chaines, les commentaires ni
// les fonctions appelees (un nom suivi de « ( »). `inComment` : un commentaire
// (* ... *) ouvert au debut de la ligne ; il dit en sortie s'il l'est encore.
[[nodiscard]] std::vector<std::string> lineSymbols(std::string_view line, bool& inComment);

// ---- les espions -----------------------------------------------------------------
// "change au cycle 1 204 (il y a 39 cycles, 0,8 s)", "change a ce cycle",
// "pas change depuis le debut".
[[nodiscard]] std::string sinceText(bool everChanged, std::uint64_t changedScan, std::uint64_t nowScan, std::int64_t periodMs);
// Qui a ecrit : "SFC_PurgeA, ligne 42, au cycle 1 243 (ce cycle)".
[[nodiscard]] std::string writeText(const SimLastWrite& w, std::uint64_t nowScan);
// 1.11.2 (D25, decision 143) : une ligne du code qui ecrit la variable, dans la
// section `section` de l'unite `unit` (vide : une section de tache). Si cette
// section n'a pas tourne au dernier cycle complet (sa condition d'activation
// etait fausse) : "section inactive (condition fausse : configuree)". Vide si
// elle a tourne, s'il n'y a pas encore de cycle, si elle n'est pas dans la
// trace (une section de bloc), ou si deux sections du meme nom ne disent pas
// la meme chose (une unite sans rang tourne section par section).
[[nodiscard]] std::string inactiveSectionText(const std::vector<SimSectionTime>& times, std::string_view unit, std::string_view section);

// ---- Lot API 8 (2e partie) : la condition d'un point d'arret, les espions tapes ----
// Une valeur montree ("12,5", "1 243", "TRUE", "T#1s500ms", "4 (forcee)") en
// litteral ST ("12.5", "1243", "TRUE", "T#1s500ms", "4") ; vide : rien a
// comparer ("?", "(structure ou tableau)", "").
[[nodiscard]] std::string stLiteral(std::string_view shown);
// Les idees du dialogue de la condition, a partir des noms de la ligne et de
// leur valeur (montree) : « nom = valeur », et pour un nombre « nom > valeur » ;
// un booleen : « nom = TRUE » puis « nom = FALSE ». Sans doublon, au plus `max`.
[[nodiscard]] std::vector<std::string> conditionIdeas(const std::vector<std::pair<std::string, std::string>>& values,
                                                      std::size_t max = 6);
// « Mettre la condition nom = valeur » (Pourquoi ici) : le premier entier de la
// ligne (un etat : « etat = 6 »), a defaut la premiere valeur utilisable ;
// vide : aucune.
[[nodiscard]] std::string suggestedCondition(const std::vector<std::pair<std::string, std::string>>& values);
// Le titre du dialogue : « Condition du point d'arret 2 : SFC_PurgeA, ligne 42 ».
[[nodiscard]] std::string conditionTitle(std::size_t number, const std::string& section, int line);
// Les noms qui commencent comme `typed` (sans la casse ; tel quel d'abord, puis
// l'ordre alphabetique), au plus `max` ; `typed` vide : aucun.
[[nodiscard]] std::vector<std::string> namesStartingWith(const std::vector<std::string>& names, std::string_view typed,
                                                         std::size_t max = 12);
// « « X » n'existe pas dans le projet. »
[[nodiscard]] std::string unknownName(std::string_view typed);
// Les noms d'une condition (chemins entiers : a.b[i].c), ou ils sont, et si elle
// appelle quelque chose (un nom suivi de « ( ») ; sans les mots du langage, les
// nombres, les litteraux types ni les chaines.
struct ConditionName {
    std::size_t from{0}, to{0};
    std::string name;
};
[[nodiscard]] std::vector<ConditionName> conditionNames(std::string_view condition, bool& call);
// En direct (le dialogue de la condition) : la condition lue dans la simulation
// MAINTENANT, sans rien ecrire ni appeler - chaque nom court complete du premier
// prefixe qui le trouve ("Unite.", l'instance du bloc ; "" : tel quel).
// « Sans condition : la simulation s'arretera a chaque cycle. », « Maintenant,
// c'est vrai (x = 3). », « Elle ne se lit pas : ... », rt nul : pas prete.
[[nodiscard]] std::string conditionPreview(sim::Runtime* rt, const std::vector<std::string>& prefixes, std::string_view condition);
// ---- fin Lot API 8 (2e partie) ----

} // namespace app::simdebug
