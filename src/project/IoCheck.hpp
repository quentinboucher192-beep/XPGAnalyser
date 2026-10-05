// =============================================================================
//  project/IoCheck.hpp - lot API 4 : les voies, les adresses et le plan memoire
// -----------------------------------------------------------------------------
//  « Voies et adresses » : chaque adresse topologique que le programme nomme
//  (%I0.5.3, %Q0.8.4, %IW0.10.1...) - dans son code ou comme adresse d'une
//  variable situee - face au module de son emplacement dans les racks du .XHW :
//
//    * pas de module a cet emplacement (le rack 0 n'a que 8 emplacements, le
//      programme ecrit %Q0.8.x) ;
//    * un module dans le mauvais sens (lire %I0.5.x sur un module de sorties) ;
//    * une voie au-dela de celles du module (%Q0.5.16 sur 16 sorties : 0 a 15).
//
//  Par module : les voies que le programme emploie (« 14/16 » dans les racks).
//
//  « Plan memoire » : les variables situees en %MW (et %MD, %MF, qui prennent
//  deux mots), leur place, les chevauchements, la plus grande place libre, et
//  les %MW que le code nomme en direct.
//
//  LA LECTURE DU CODE est celle du tableau de bord (ApiChecks) : commentaires
//  et chaines retires ; une ecriture est une adresse suivie de ':='.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace project::io {

enum class Direction : std::uint8_t { Input, Output };

struct Address {
    enum class Status : std::uint8_t { Ok, NoHardware, NoModule, WrongDirection, NoSuchChannel };

    std::string   text;                 // "%Q0.5.3", "%IW0.10.0" (forme normalisee, majuscules)
    Direction     direction{Direction::Input};
    bool          word{false};          // %IW / %QW / %ID / %QD
    int           rack{0}, slot{0}, channel{0};
    int           sub{-1};              // %IW0.3.1.2 -> 2 ; -1 : aucun
    std::vector<std::string> users;     // les entrees (sections, unites) qui la nomment, dans l'ordre
    std::size_t   reads{0}, writes{0};
    std::string   declaredAs;           // la variable situee a cette adresse, s'il y en a une
    Status        status{Status::Ok};
    std::string   module;               // la reference a cet emplacement ("" : aucun)
    std::string   detail;               // le verdict, en clair
    [[nodiscard]] bool faulty() const noexcept { return status != Status::Ok && status != Status::NoHardware; }
};

struct ModuleUse {
    int                      rack{0}, slot{0};
    std::string              reference;
    std::size_t              used{0}, total{0};   // voies employees / voies du module
    std::vector<std::string> addresses;           // celles que le programme nomme
};

struct Report {
    std::vector<Address>   addresses;   // %I, %IW, %Q, %QW ; puis rack, emplacement, voie
    std::vector<ModuleUse> modules;     // chaque module des racks (le processeur compris)
    std::size_t            faulty{0};
    bool                   hardware{false};   // des racks (un .XHW) : sinon rien a comparer
};

[[nodiscard]] Report check(const domain::Project&);
// Le verdict d'une adresse, pour une ligne du tableau : "ok", ou la raison.
[[nodiscard]] std::string verdict(const Address&);

// ------------------------------------------------------- le plan memoire ----
struct Located {
    std::string   name, type, address;
    std::uint32_t first{0}, words{1};
};

struct Overlap {
    std::string   a, b;
    std::uint32_t word{0};
};

struct Memory {
    std::vector<Located>       variables;         // par adresse
    std::vector<Overlap>       overlaps;
    std::uint32_t              low{0}, high{0};   // le premier et le dernier mot employes
    std::uint32_t              gapFrom{1}, gapTo{0};   // la plus grande place libre (gapTo < gapFrom : aucune)
    std::vector<std::uint32_t> directWords;       // les %MW nommes en direct dans le code (uniques, tries)
    std::size_t                directReferences{0};
    std::uint32_t              configured{0};     // %MW declares au .XHW (0 : inconnu)
    [[nodiscard]] std::uint32_t gapSize() const noexcept { return gapTo >= gapFrom ? gapTo - gapFrom + 1 : 0; }
};

[[nodiscard]] Memory memoryOf(const domain::Project&);

// ------------------------------------------ lot API 5 : les trois zones ----
//  Le plan memoire ne montrait que les %MW. Un automate en a trois zones, que
//  le .XHW dimensionne : les bits %M, les mots %MW (les %MD et %MF y prennent
//  deux mots), les constantes %KW. Pour chacune : sa TAILLE COMPLETE (celle du
//  .XHW ; sinon, faute de mieux, jusqu'a la derniere cellule employee), les
//  bornes de lecture (Project::memoryWindows : toute la zone par defaut), et
//  dans ces bornes les cellules employees - par une variable situee ou par le
//  code en direct : lue ou ECRITE (%MW100 := 50 prend de la memoire comme une
//  variable), un bit de mot (%MW10.3), une table (%MW20:5), une adresse
//  indexee (%MW0[index], voir Indexed) -, le pourcentage d'utilisation, la
//  plus grande place libre, les chevauchements. Et les variables situees AU-DELA de
//  la taille configuree : Control Expert les refuserait a la generation.
// Une adresse INDEXEE du code : %MW0[index], %MW100[i+1], %MF10[2]. L'indice
// s'evalue quand il le peut (des nombres, des constantes : %MW800[2*3+1] est
// %MW807) ; sinon il est DYNAMIQUE - une variable, lue a chaque cycle. Sa plage
// se deduit d'une boucle FOR de la meme section (FOR i := 0 TO 15 : %MW100[i]
// va de %MW100 a %MW115) ; a defaut, elle est inconnue et le plan le dit.
struct Indexed {
    std::string   text;                 // "%MW100[i]", tel qu'ecrit (blancs retires)
    std::string   section;
    std::uint32_t base{0};
    bool          constant{false};      // l'indice s'evalue : une seule cellule, fixe
    bool          dynamic{false};       // l'indice est une variable
    bool          bounded{false};       // dynamique, plage deduite d'une boucle FOR
    bool          estimated{false};     // dynamique, une variable inconnue prise a 0 : une estimation
    std::uint32_t lo{0}, hi{0};         // les cellules touchees (dynamique sans plage : la base seule)
    bool          write{false};         // « %MW100[i] := ... »
    std::string   why;                  // "i de 0 a 15 (FOR)", "plage inconnue : index change a l'execution"
};

struct ZoneMap {
    domain::MemoryZone         zone{domain::MemoryZone::Words};
    std::string                prefix;              // "%M", "%MW", "%KW"
    std::string                unit, units;         // "bit" / "bits", "mot" / "mots"
    std::uint32_t              size{0};             // la taille complete de la zone (cellules)
    bool                       configured{false};   // la taille vient du .XHW
    std::uint32_t              from{0}, to{0};      // les bornes de lecture (comprises)
    bool                       bounded{false};      // bornes posees (sinon : toute la zone)
    std::vector<Located>       variables;           // toutes les variables situees, par adresse
    std::vector<Overlap>       overlaps;
    std::vector<std::uint32_t> direct;              // cellules nommees en direct dans le code (uniques, triees)
    std::vector<std::uint32_t> written;             // ... dont celles que le code ECRIT (%MW100 := 50)
    std::size_t                directReferences{0};
    std::vector<Indexed>       indexed;             // les adresses indexees (%MW0[index]...)
    std::vector<std::uint32_t> dynamicCells;        // les cellules des plages dynamiques deduites (triees)
    std::size_t                dynamicUnknown{0};   // les indexees dynamiques sans plage connue
    std::uint32_t              used{0};             // cellules employees dans les bornes
    std::uint32_t              gapFrom{1}, gapTo{0};   // la plus grande place libre dans les bornes
    std::vector<std::string>   outside;             // variables au-dela de la taille configuree
    std::uint32_t              highest{0};          // la derniere cellule employee (+1 ; 0 : aucune)
    [[nodiscard]] std::uint32_t span() const noexcept { return to >= from ? to - from + 1 : 0; }
    [[nodiscard]] std::uint32_t gapSize() const noexcept { return gapTo >= gapFrom ? gapTo - gapFrom + 1 : 0; }
    // L'utilisation dans les bornes, en pour cent (0 a 100).
    [[nodiscard]] double percent() const noexcept { return span() ? 100.0 * static_cast<double>(used) / static_cast<double>(span()) : 0.0; }
    [[nodiscard]] std::string cell(std::uint32_t n) const { return prefix + std::to_string(n); }
};
// %M, %MW, %KW - dans cet ordre (l'indice est domain::MemoryZone).
[[nodiscard]] std::vector<ZoneMap> memoryZones(const domain::Project&);
// La variable qui occupe cette cellule (nullptr : aucune).
[[nodiscard]] const Located* variableAt(const ZoneMap&, std::uint32_t cell);
// « 12,5 % » ; « 0,4 % » ; « < 0,1 % » quand une cellule est employee sur 10 000.
[[nodiscard]] std::string percentText(double percent);

// ------------------------------------------------------------- le reseau ----
//  Les ports de communication des modules (le processeur, les BMX NOC...) : ce
//  que le .XHW en dit - le module, la voie, le protocole. Les adresses IP ne
//  sont pas dans le .XHW (elles se reglent dans Control Expert).
struct Port {
    int         rack{0}, slot{0}, channel{0};
    std::string module, role, protocol, task;
};
[[nodiscard]] std::vector<Port> networkOf(const domain::Project&);
// "EthernetCIP_Mirano_TR2" -> "Ethernet (EtherNet/IP, Modbus TCP)" ;
// "ModbusSerialPort" -> "Modbus s\xC3\xA9rie" ; "" si ce n'est pas un port.
[[nodiscard]] std::string protocolOf(std::string_view role);
// La variable qui occupe ce mot (nullptr : aucune).
[[nodiscard]] const Located* variableAt(const Memory&, std::uint32_t word);

} // namespace project::io
