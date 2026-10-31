// =============================================================================
//  hmi/HmiAudit.hpp - 1.19.0 : la verification globale du projet, l'analyse et
//                     l'optimisation des adresses
// -----------------------------------------------------------------------------
//  << VERIFIE TOUT MON PROJET ET PROPOSE LES CORRECTIONS. >> run() rassemble :
//    - GENERER et COMPILER, tels que le logiciel les fait (hmi::generateWith,
//      hmi::compileWith : vues, objets, expressions, scripts, appels, arguments
//      des symboles, types qui se contiennent, symboles qui s'instancient
//      eux-memes, navigations et appels en boucle, vues, popups, scripts et
//      ressources introuvables...) - un constat dit une fois meme si les deux le
//      disent ;
//    - LES ADRESSES EN CONFLIT (hmi::zones::buildMap, la carte memoire des
//      equipements) : chaque chevauchement entre deux variables, avec une
//      correction proposee - l'adresse libre la plus proche ;
//    - LES ORPHELINS (hmi::rel, le graphe des relations) : variables jamais lues
//      ni ecrites, popups jamais ouvertes, fonctions et scripts sur appel jamais
//      appeles, types sans emploi (les symboles sans instance, les ressources
//      inutilisees, les equipements sans variable et - sa case Qualite cochee -
//      les popups jamais ouvertes : Generer les dit deja, une fois suffit) ;
//    - LES DOSSIERS INCOHERENTS : des dossiers gardes pour une liste qui n'existe
//      pas, un nom de dossier illisible, un meme dossier ecrit de deux facons (un
//      joker de dossier, Ligne/* ou Process/*, qui vise un dossier vide ou absent :
//      Generer le dit deja).
//  Chaque constat : sa severite (erreur bloquante, avertissement, information),
//  sa categorie, un message en francais, l'endroit, et les corrections
//  PROPOSEES - jamais appliquees : le projet n'est pas modifie.
//
//  LES ADRESSES. ranges() : chaque case liee d'un equipement Modbus (une
//  structure en ses membres) et sa place ; overlaps() : les chevauchements ;
//  freeRanges() : les places libres d'une table. optimize() : un plan avant /
//  apres - les variables rangees sans trou ni chevauchement - jamais applique
//  (applyMoves() l'applique, quand l'utilisateur l'a accepte, dans sa commande).
// =============================================================================
#pragma once

#include "HmiCheck.hpp"   // NameExists
#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::audit {

// ---------------------------------------------------------------- le rapport ---
enum class Severity : std::uint8_t { Error /*bloquante*/, Warning, Info };
// "erreur bloquante", "avertissement", "information".
[[nodiscard]] std::string_view severityLabel(Severity) noexcept;

// UNE CORRECTION PROPOSEE (jamais appliquee par run).
struct Fix {
    std::string what;      // "adresse", "texte", "suppression", "dossier"
    std::string target;    // ce qui change : "variable Consigne_Debit", "Vue_Station . Btn_P2 . value"
    std::string before;    // la valeur d'aujourd'hui ("40013") ; vide : rien (une suppression : l'element)
    std::string after;     // la valeur proposee ("40016") ; vide : retirer
    std::string why;       // pourquoi, en une phrase
};

struct Finding {
    Severity    severity{Severity::Warning};
    // "Adresses", "Orphelin", "Dossiers", ou la categorie de Generer et Compiler ("Variable", "Script", "Alarme"...).
    std::string category;
    std::string message;
    // Lisible : "Vue_Station . Btn_P2 . value", "script Calcul_Niveau, ligne 3", "variable Consigne_Debit".
    std::string where;
    Id          view{kNoId}, object{kNoId}, item{kNoId};
    Id          script{kNoId};   // un script (general ou de vue) en cause
    int         line{0};         // ... a cette ligne (1 = la premiere)
    std::string property;        // la propriete de l'objet, le nom de l'element
    std::string source;          // d'ou vient le controle : "Generer", "Compiler", "Adresses", "Relations", "Dossiers"
    std::vector<Fix> fixes;
};

struct Report {
    // Les erreurs bloquantes d'abord, puis les avertissements, puis les informations (chacun dans l'ordre trouve).
    std::vector<Finding> findings;
    std::size_t          errors{0}, warnings{0}, infos{0};
    // Ce qui n'est pas un constat : le bilan de Compiler ("12 expression(s), 3 script(s)... compil\xC3\xA9(s)").
    std::vector<std::string> notes;
    // "2 erreurs bloquantes, 5 avertissements, 9 informations".
    [[nodiscard]] std::string summary() const;
};

// La verification globale. `plcHasName` : les noms de l'automate (vide : aucun automate - un nom inconnu de l'IHM est
// une erreur, comme l'Assistant le juge) ; `projectFolder` : ou chercher les fichiers externes relatifs.
[[nodiscard]] Report run(const Project&, const NameExists& plcHasName = {}, const std::string& projectFolder = {});

// --------------------------------------------------------------- les adresses ---
//  Les places sont des cases d'une table de l'equipement (MemTable) : des mots pour les registres (de maintien,
//  d'entree), des bits pour les bobines et les entrees TOR ; numerotees a partir de 0 (40001 : 0).
struct Range {
    std::string   equipment;           // "Station_Modbus"
    std::string   variable;            // la variable IHM (la racine) : "Cuve_1"
    std::string   cell;                // la case : "Cuve_1.Niveau" ; une variable simple : son nom
    std::string   address;             // telle qu'ecrite : "40011", "%MW10"
    std::string   type;                // "REAL"
    MemTable      table{MemTable::Holding};
    std::uint32_t first{0}, last{0};   // les cases occupees
    int           bit{-1};             // un BOOL range dans un mot : son bit ; -1 : la case entiere
    bool          written{false};      // ecrite par l'IHM (une commande, une action, un script, une recette)
    std::vector<std::string> writers;  // qui l'ecrit ("Sld_Consigne (Vue_Station, Curseur)")
    // "registres 40011 \xC3\xA0 40012", "registre 40001, bit 3", "bobine 00017".
    [[nodiscard]] std::string text() const;
};
// Les cases liees des equipements Modbus (`equipment` vide : tous), dans l'ordre des equipements et des variables.
[[nodiscard]] std::vector<Range> ranges(const Project&, std::string_view equipment = {});

struct Overlap {
    Range         a, b;                // les deux cases (a commence avant b)
    int           severity{1};         // 1 : deux lectures ; 2 : une ecriture et une lecture ; 3 : deux ecritures
    MemTable      table{MemTable::Holding};
    std::uint32_t first{0}, last{0};   // les cases communes
    std::string   text;                // "deux \xC3\xA9" "critures sur 40013 \xC3\xA0 40014 : ..." (zones::conflictText)
};
// Les chevauchements des cases (`equipment` vide : tous les equipements). Deux cases d'une meme variable aussi.
[[nodiscard]] std::vector<Overlap> overlaps(const Project&, std::string_view equipment = {});

// Les places libres d'une table de l'equipement entre `from` et `to` (bornes comprises) : les intervalles [debut, fin]
// qu'aucune case n'occupe (un bit pris prend son mot) - et, ses zones declarees, dans ses zones seulement.
[[nodiscard]] std::vector<std::pair<long, long>> freeRanges(const Project&, std::string_view equipment, MemTable table, long from,
                                                            long to);
// L'adresse libre la plus proche de la variable `variable` (toute son emprise : une structure entiere), ecrite comme son
// adresse d'aujourd'hui (40013 -> 40016 ; %MW12 -> %MW15) ; a egale distance, la plus haute. Les autres variables de
// l'equipement ne bougent pas. Vide et `why` : pas une variable liee, pas de place.
[[nodiscard]] std::string nearestFreeAddress(const Project&, std::string_view variable, std::string* why = nullptr);

// ------------------------------------------------------------ l'optimisation ---
struct Move {
    std::string variable;   // "Cuve_1"
    std::string equipment;  // "Station_Modbus"
    std::string before;     // "40011"
    std::string after;      // "40009"
    std::string reason;     // "rang\xC3\xA9" "e \xC3\xA0 la suite de Pompe_2 (registres 40009 \xC3\xA0 40013)"
};
// L'emprise d'une table d'un equipement : de la premiere a la derniere case occupee, combien sont occupees, combien de
// trous ; "registres 40001 \xC3\xA0 40022 : 18 occup\xC3\xA9s, 4 libres".
struct Footprint {
    std::string equipment;
    MemTable    table{MemTable::Holding};
    long        first{0}, last{-1};
    long        used{0}, holes{0};
    std::string text;
};
struct Optimization {
    std::vector<Move>        moves;      // dans l'ordre (equipement, table, variables)
    std::vector<std::string> notes;      // ce qui n'a pas bouge et pourquoi ; les precautions
    std::vector<Footprint>   before, after;
    std::size_t              overlapsBefore{0}, overlapsAfter{0};   // entre deux variables differentes, sur ces tables
    [[nodiscard]] bool empty() const noexcept { return moves.empty(); }
};
// Les variables donnees (vide : toutes celles qui ont une adresse sur un equipement Modbus ; un nom peut porter * :
// "Pompe_*", "Cuve*") rangees sans trou ni chevauchement, dans l'ordre donne (vide : l'ordre du projet), a partir de la
// premiere case occupee de leur equipement (par table) ; `frozen` : celles qui ne bougent pas (les autres variables de
// l'equipement non plus) - les autres se rangent autour, dans les zones declarees. Une variable dont un membre a une
// adresse corrigee (Variable::places), un bit d'un mot, ou des cases dans deux tables ne bouge pas (une note le dit).
// Une variable deja a sa place ne donne pas de Move. Deterministe ; JAMAIS APPLIQUE.
[[nodiscard]] Optimization optimize(const Project&, const std::vector<std::string>& variables = {},
                                    const std::vector<std::string>& frozen = {});
// Appliquer des deplacements (accepte par l'utilisateur, dans sa commande) : l'adresse de chaque variable devient
// `after` si elle vaut encore `before`. Rend le nombre de variables changees ; `why` : celles qui ne l'ont pas ete.
std::size_t applyMoves(Project&, const std::vector<Move>&, std::string* why = nullptr);

} // namespace hmi::audit
