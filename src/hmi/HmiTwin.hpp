// =============================================================================
//  hmi/HmiTwin.hpp - le jumeau simule d'un equipement : un esclave Modbus
//                     virtuel (lot 17)
// -----------------------------------------------------------------------------
//  SA MEMOIRE (TwinBank) : les quatre tables d'un vrai appareil - bobines,
//  entrees TOR, registres d'entree, registres de maintien -, chacune a elle.
//  Il sert les zones de l'equipement (non declarees : tout) ; au-dela, il
//  repond "adresse illegale" (exception 02), et "fonction non prise en charge"
//  (01) pour une table qu'il n'a pas. Une exception forcee (02, 04, 06...)
//  repond a tout, pour voir l'IHM reagir. Il retient qui a lu et ecrit chaque
//  case, et quand (la carte memoire le montre).
//
//  SES COMPORTEMENTS (Behavior) : ce qu'une plage fait toute seule - constante,
//  sinus, rampe, compteur, clignote, aleatoire, recopie d'une autre case (avec
//  un retard), suit une variable de l'automate simule, etapes (une liste de
//  valeurs rejouee). Les autres cases gardent ce qu'on y ecrit.
//
//  Le serveur (modbus::Server) et le fil sont a l'hote (EquipmentHost) ; rien
//  ici ne depend d'un ecran.
// =============================================================================
#pragma once

#include "HmiModbus.hpp"
#include "HmiModel.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::twin {

// L'horloge des acces (s, monotone).
[[nodiscard]] double now() noexcept;

class TwinBank final : public modbus::Bank {
public:
    TwinBank();

    // Ses zones ; non declarees : tout (0..65535 dans les quatre tables).
    void setZones(const MemZones&);
    [[nodiscard]] MemZones zones() const;
    void setForcedException(int code);           // 0 : aucune
    [[nodiscard]] int forcedException() const;
    void setIdentification(modbus::Identification id);

    // modbus::Bank : ce que servent les requetes (les zones, l'exception forcee, les acces retenus).
    int readBits(bool discrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) override;
    int readRegisters(bool input, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) override;
    int writeBits(std::uint16_t address, const std::vector<bool>& values) override;
    int writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) override;
    [[nodiscard]] modbus::Identification identification() override;

    // L'application (les comportements, la carte, Ecrire une valeur) : sans zones, sans compter d'acces.
    [[nodiscard]] std::uint16_t word(MemTable, std::uint32_t offset) const;
    void setWord(MemTable, std::uint32_t offset, std::uint16_t value);
    [[nodiscard]] bool bit(MemTable, std::uint32_t offset) const;
    void setBit(MemTable, std::uint32_t offset, bool value);
    [[nodiscard]] std::vector<std::uint16_t> words(MemTable, std::uint32_t first, std::uint32_t count) const;
    // Une ecriture de l'application, comptee comme celle de `who` ("Ecrire une valeur").
    // Lot 18 : 4 (refusee, comptee) si elle changeait une case forcee ; 0 : ecrite.
    int write(MemTable, std::uint32_t offset, const std::vector<std::uint16_t>& values, const std::string& who);
    void clear();

    // Qui a lu et ecrit la case, et quand (now() ; -1 : jamais).
    struct Access {
        double      readAt{-1}, writeAt{-1};
        std::string reader, writer;
    };
    [[nodiscard]] Access access(MemTable, std::uint32_t offset) const;
    struct Counters {
        std::uint64_t reads{0}, writes{0}, refused{0};
        double        lastWriteAt{-1};
        std::string   lastWrite;               // "40003 = 16968 (IHM)"
        // Lot 18 : les ecritures refusees parce qu'elles changeraient une case forcee.
        std::uint64_t forcedRefused{0};
        double        lastRefusedAt{-1};
        std::string   lastRefused;             // "43021 (IHM)"
    };
    [[nodiscard]] Counters counters() const;

    // Lot 18 : LES CASES FORCEES - chaque mot tient ses bits forces (un masque et
    // leur valeur ; une bobine, une entree TOR : le masque 1). Une ecriture Modbus
    // qui changerait un bit force est refusee toute entiere (exception 04, comptee) ;
    // les comportements, "Ecrire une valeur", la memoire au depart ne le changent pas.
    struct ForcedCell {
        MemTable      table{MemTable::Holding};
        std::uint32_t offset{0};
        std::uint16_t mask{0xFFFF};
        std::uint16_t value{0};
        bool operator==(const ForcedCell&) const = default;
    };
    void setForced(const std::vector<ForcedCell>&);
    [[nodiscard]] bool forced(MemTable, std::uint32_t first, std::uint32_t count = 1) const;
    [[nodiscard]] std::uint16_t forcedMask(MemTable, std::uint32_t offset) const;   // les bits forces du mot (0 : aucun)

    // La memoire gardee : les suites de valeurs non nulles ; et la recharger.
    [[nodiscard]] std::vector<SavedMemory> snapshot() const;
    void load(const std::vector<SavedMemory>&);

private:
    struct Cell {
        float         readAt{-1}, writeAt{-1};
        std::uint16_t reader{0}, writer{0};    // dans names_ (0 : personne)
    };
    [[nodiscard]] int check(MemTable, std::uint32_t first, std::uint32_t count) const;   // sous mutex_
    std::uint16_t nameIndex(const std::string& who);                                   // sous mutex_
    void touch(MemTable, std::uint32_t first, std::uint32_t count, bool write, const std::string& who);

    [[nodiscard]] std::uint16_t hold(MemTable, std::uint32_t offset, std::uint16_t value) const;   // sous mutex_ : les bits forces
    [[nodiscard]] bool changesForced(MemTable, std::uint32_t offset, std::uint16_t value) const;   // sous mutex_
    void refuseForced(MemTable, std::uint32_t offset, const std::string& who);                     // sous mutex_

    mutable std::mutex                          mutex_;
    std::array<std::vector<std::uint16_t>, 4>   mem_;       // les bits en 0/1
    std::array<std::map<std::uint32_t, std::pair<std::uint16_t, std::uint16_t>>, 4> force_;   // lot 18 : (masque, valeur)
    std::array<std::vector<Cell>, 4>            cells_;
    std::vector<std::string>                    names_{""};
    MemZones                                    zones_;
    int                                         forced_{0};
    modbus::Identification                      id_;
    Counters                                    counters_;
};

// Lot 18 : les cases d'un forcage (son adresse, son type, sa valeur brute) ;
// faux : une adresse illisible, un type impossible (why : pourquoi).
bool forcedCells(const Forcing&, bool lowWordFirst, std::vector<TwinBank::ForcedCell>& out, std::string* why = nullptr);
[[nodiscard]] std::vector<TwinBank::ForcedCell> forcedCells(const std::vector<Forcing>&, bool lowWordFirst);
// "5100 (INT)", "1 (bobine)" : un forcage en clair.
[[nodiscard]] std::string forcingText(const Forcing&);

// Les valeurs initiales des variables liees, dans la memoire (au demarrage) ;
// celles d'une seule variable (une variable nouvelle, ou changee).
void primeInitial(TwinBank&, const Project&, const Equipment&);
void primeVariable(TwinBank&, const Project&, const Equipment&, const Variable&);

// ------------------------------------------------------------ les comportements ---
// Une valeur de variable de l'automate simule (suit l'automate) ; vide : inconnue.
using PlcReader = std::function<std::optional<double>(const std::string& name)>;

class Behaviors {
public:
    // Fait avancer chaque comportement a l'heure `t` (s) ; ecrit dans la memoire.
    void tick(TwinBank&, const std::vector<Behavior>&, double t, bool lowWordFirst, const PlcReader& plc);
    void reset() { state_.clear(); }

private:
    struct State {
        std::uint64_t                               seed{0};
        double                                      nextAt{-1};
        double                                      value{0};
        std::deque<std::pair<double, double>>       history;    // recopie : (heure, valeur de la source)
    };
    std::map<std::string, State> state_;                        // cle : l'adresse et le genre
    std::mt19937_64              rng_{12345};
};

// Ce que vaut maintenant la plage d'un comportement (la carte, "Maintenant").
[[nodiscard]] std::optional<double> valueAt(const TwinBank&, const Behavior&, bool lowWordFirst);
// Le comportement se lit-il ? (l'adresse, le type, les reglages) ; why : pourquoi pas.
bool validBehavior(const Behavior&, std::string* why = nullptr);
// "sinus 20 -> 80, periode 60 s" : ses reglages en clair.
[[nodiscard]] std::string behaviorText(const Behavior&);

// ------------------------------------------------- lot 18 : les valeurs simulees ---
//  UNE LIGNE DE L'ONGLET "Valeurs simulees" (Configuration > Equipements) et de
//  l'onglet "Jumeaux" de la simulation : une variable IHM liee (ou une case d'une
//  structure) d'un equipement qui a un jumeau, ou un registre anime ou force sans
//  variable. Son comportement et son forcage se retrouvent par leur case (43003 et
//  %MW3002 sont la meme).
struct ValueRow {
    std::string   variable;               // la case ("Four1.Consigne") ; vide : un registre sans variable
    std::string   root;                   // la variable IHM
    std::string   address;                // "43003", telle qu'ecrite
    std::string   type{"INT"};            // le type du registre (brut)
    MemTable      table{MemTable::Holding};
    std::uint32_t offset{0};
    int           bit{-1};                // le bit d'un mot
    bool          boolean{false};         // bobine, entree TOR, bit, BOOL
    bool          scaled{false};
    double        rawMin{0}, rawMax{0}, engMin{0}, engMax{0};
    bool          written{false};         // l'IHM l'ecrit (un champ, une action, un script)
    std::string   writers;                // qui l'ecrit (les premiers)
    int           behavior{-1};           // son comportement (Equipment::behaviors) ; -1 : aucun
    int           forcing{-1};            // son forcage (Equipment::forcings) ; -1 : aucun
    // Brut <-> la valeur de la variable (la mise a l'echelle ; sans : la meme).
    [[nodiscard]] double eng(double raw) const noexcept;
    [[nodiscard]] double raw(double eng) const noexcept;
    [[nodiscard]] std::string key() const { return address; }
};
[[nodiscard]] std::vector<ValueRow> valueRows(const Project&, const Equipment&);
// La ligne d'une case sans variable ni mouvement (la carte : animer, forcer une case libre).
[[nodiscard]] std::optional<ValueRow> freeRow(const std::string& address, const std::string& type = "INT");
// Deux adresses d'un equipement designent-elles la meme case ?
[[nodiscard]] bool sameCell(const std::string& a, const std::string& b);
// Ce que vaut la ligne maintenant (brut).
[[nodiscard]] std::optional<double> rowValue(const TwinBank&, const ValueRow&, bool lowWordFirst);
// Un comportement pour animer la ligne : clignote (un BOOL), sinon un sinus
// dans une bande qui dit quelque chose (autour de sa valeur, ou au milieu de
// l'echelle), periode 30 s.
[[nodiscard]] Behavior defaultBehavior(const ValueRow&, std::optional<double> nowRaw);
// La barre (en valeur de la variable) : l'echelle si elle est mise a l'echelle ;
// sinon autour de la bande et de la valeur (des bornes rondes).
[[nodiscard]] std::pair<double, double> barRange(const ValueRow&, const Behavior*, std::optional<double> nowRaw);
// Le forcage d'une valeur brute tapee ("5100", "0x1F", "12.5", "TRUE") ; faux : illisible.
bool parseRaw(const ValueRow&, const std::string& text, double& out, std::string* why = nullptr);
// "5100 (51 Hz)" : un forcage en clair, et ce qu'il vaut pour la variable.
[[nodiscard]] std::string rowForcingText(const ValueRow&, double raw);
[[nodiscard]] std::string numberText(double v);

// ------------------------------------------------------------------ fichiers ---
// La memoire en CSV : "table;adresse;valeur" (les cases non nulles des zones).
[[nodiscard]] std::string exportCsv(const TwinBank&);
bool importCsv(TwinBank&, std::string_view csv, std::string* why = nullptr, std::size_t* count = nullptr);

} // namespace hmi::twin
