// =============================================================================
//  hmi/HmiSimData.hpp - 1.11.15 : LA REMANENCE DE SIMULATION
// -----------------------------------------------------------------------------
//  « Conserver les donnees de simulation entre les demarrages » : a l'arret de
//  la simulation, ses donnees sont prises (les variables IHM, case par case ; la
//  memoire des esclaves simules) ; au demarrage suivant, elles sont rendues
//  apres les valeurs initiales, avant les scripts de Demarrage (comme la reprise
//  a chaud d'un automate).
//
//  Chaque case est reconnue par l'IDENTIFIANT STABLE de sa variable et par son
//  chemin dans la variable ("", ".Temperature", "[3]", "[2].Position") : un
//  renommage ou un changement d'ordre ne la fait pas changer de variable. Un
//  esclave simule, par l'identifiant de son equipement.
//
//  Au retour :
//    - une variable supprimee : ignoree (comptee) ;
//    - une nouvelle variable, un nouveau membre : leur valeur initiale ;
//    - un type change : la valeur convertie si les types sont compatibles (un
//      entier dans un entier plus large, ou plus etroit si elle y tient ; un
//      entier dans un REAL), sinon la valeur initiale et un avertissement ;
//    - une variable liee a un equipement n'est pas prise : sa valeur est dans
//      la memoire de l'esclave simule, rendue avec lui.
//
//  Le fichier : <projet>/.xpg/simulation/remanence.txt, ecrit d'un bloc
//  (core::writeFileAtomic, une copie .bak) - l'espace du developpement, jamais
//  celui du poste d'exploitation (la remanence par variable du lot 4 a le sien).
//  Une ligne "fin" compte ce qui precede : un fichier coupe est refuse.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../sim/Value.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::simdata {

inline constexpr std::string_view kHeader = "xpg-simulation-remanence 1";

struct Cell {
    Id          variable{kNoId};
    std::string name;          // le nom de la variable a la prise (pour le dire)
    std::string declared;      // son type declare ("REAL", "T_Four", "ARRAY[1..4] OF INT")
    std::string path;          // le chemin de la case dans la variable ("" : une variable simple)
    sim::Value  value;
};
struct TwinMemory {
    Id                       equipment{kNoId};
    std::string              name;
    std::vector<SavedMemory> memory;
};
struct Snapshot {
    std::string             date;          // "2026-10-08 21:12:30"
    int                     session{0};
    std::vector<Cell>       cells;
    std::vector<TwinMemory> twins;
    [[nodiscard]] bool empty() const noexcept { return cells.empty() && twins.empty(); }
};

[[nodiscard]] std::string serialize(const Snapshot&);
// Faux : pas un instantane, ou coupe (la ligne "fin" manque ou ne compte pas juste).
[[nodiscard]] bool        parse(std::string_view text, Snapshot& out, std::string* why = nullptr);

// Les cases a prendre : les variables IHM non liees, depliees ; `read` donne la
// valeur du moment d'une case par son chemin complet ("Four1.Vannes[2].Position").
using Reader = std::function<const sim::Value*(const std::string& path)>;
// `keep` (1.11.16) : les seules variables a prendre (les remanentes du poste) ; vide : toutes.
[[nodiscard]] std::vector<Cell> captureVariables(const Project&, const Reader& read, const std::function<bool(const Variable&)>& keep = {});

// Ce que le retour a fait.
struct Report {
    int restored{0};           // cases rendues (converties comprises)
    int converted{0};          // ... dont converties (INT -> DINT)
    int removed{0};            // variables de l'instantane qui n'existent plus : ignorees
    int dropped{0};            // cases d'un membre qui n'existe plus
    int incompatible{0};       // cases refusees (type incompatible) : valeur initiale
    int fresh{0};              // variables nouvelles (pas dans l'instantane) : valeur initiale
    int twins{0};              // esclaves simules rendus
    std::vector<std::string> warnings;   // "Temperature : REAL devient BOOL - incompatible, valeur initiale"
    std::vector<std::string> notes;      // "Compteur : INT devient DINT - valeur convertie"
    [[nodiscard]] std::string summary() const;   // "12 valeurs rendues (1 convertie) ; 2 variables supprimees ignorees ; ..."
};
using Slot = std::function<sim::Value*(const std::string& path)>;
Report restoreVariables(const Project&, const std::vector<Cell>&, const Slot& slot);

// Une valeur dans une case de type `to` : convertie, ou rien (types incompatibles,
// ou valeur hors de la plage du nouveau type).
[[nodiscard]] std::optional<sim::Value> convert(const sim::Value& from, sim::Type to);

// "2026-10-08 21:12:30" (l'heure du poste).
[[nodiscard]] std::string nowStamp();

// Une valeur en texte (TRUE, -42, 21.537, 90500 pour un TIME en ms, le texte d'une STRING),
// et dans l'autre sens pour un type donne (rien : illisible). 1.11.16 : la remanence
// d'exploitation (hmi::retain) ecrit ses valeurs de la meme facon.
[[nodiscard]] std::string               valueText(const sim::Value&);
[[nodiscard]] std::optional<sim::Value> valueFrom(sim::Type, const std::string& text);

} // namespace hmi::simdata
