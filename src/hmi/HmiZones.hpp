// =============================================================================
//  hmi/HmiZones.hpp - les zones memoire d'un equipement, et sa carte (lot 17)
// -----------------------------------------------------------------------------
//  LES ZONES : ce qu'un equipement a dans sa memoire, table par table (MemZones,
//  HmiModel.hpp). Elles s'ecrivent comme on les lit sur une documentation :
//
//      0-99; 1000-1049          les numeros, a partir de 0 (comme %MW)
//      40001-40100              a la Modicon (a partir de 1)
//      %MW0-%MW99               a la Schneider
//      aucune                   la table n'existe pas
//
//  LA DETECTION ne fait que LIRE (fonctions 1, 2, 3, 4), jamais ecrire : pour
//  chaque table, la taille d'un seul tenant depuis 0 (recherche par moities),
//  puis un balayage par blocs pour trouver les plages plus loin (un bloc
//  refuse se coupe en deux jusqu'a la case). Une fonction refusee (exception
//  01) : la table n'existe pas. Un appareil qui repond partout (des 0, sans
//  erreur) ne dit pas ou s'arrete sa memoire : c'est signale.
//
//  LA CARTE : les variables IHM liees a l'equipement, case par case (une
//  structure en ses membres), avec qui les ecrit vraiment (une action, un champ
//  de saisie, un script :=, une recette) ; les chevauchements jugees :
//
//      des lectures qui se chevauchent       normal
//      une ecriture et une lecture           attention (ca peut etre voulu)
//      deux ecritures                        erreur
//      une case hors des zones declarees     erreur (l'appareil la refusera)
// =============================================================================
#pragma once

#include "HmiComm.hpp"
#include "HmiModel.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hmi::zones {

// ------------------------------------------------------------------- tables ---
[[nodiscard]] std::string_view tableLabel(MemTable) noexcept;      // "Registres de maintien"
[[nodiscard]] std::string_view tableModicon(MemTable) noexcept;    // "4x"
[[nodiscard]] std::string_view tableSchneider(MemTable) noexcept;  // "%MW"
[[nodiscard]] std::string_view tableKey(MemTable) noexcept;        // "4x" (le disque)
[[nodiscard]] std::optional<MemTable> tableFrom(std::string_view) noexcept;   // "4x", "%MW", "maintien"...
[[nodiscard]] bool isBits(MemTable) noexcept;
[[nodiscard]] MemTable   tableOf(comm::Area) noexcept;
[[nodiscard]] comm::Area areaOf(MemTable) noexcept;
// La case `offset` a la Modicon ("40101", "400101" au-dela de 9999) et a la Schneider ("%MW100").
[[nodiscard]] std::string modicon(MemTable, std::uint32_t offset);
[[nodiscard]] std::string schneider(MemTable, std::uint32_t offset);

// ------------------------------------------------------------------- plages ---
// Triees, les voisines et les chevauchantes fusionnees.
void normalize(std::vector<MemRange>&);
// "0 - 99 ; 1000 - 1049" ; vide : "aucune".
[[nodiscard]] std::string rangesText(const std::vector<MemRange>&);
// "0-99; 1000-1049", "40001-40100", "%MW0-%MW99", "5", "aucune" (ou vide) ; faux : illisible (why).
bool parseRanges(MemTable, std::string_view text, std::vector<MemRange>& out, std::string* why = nullptr);
[[nodiscard]] bool contains(const std::vector<MemRange>&, std::uint32_t first, std::uint32_t count) noexcept;
[[nodiscard]] std::uint32_t totalSize(const std::vector<MemRange>&) noexcept;
// Une ligne de resume : "bobines 0-1999, registres de maintien 0-99 et 1000-1049" ; "non declarees".
[[nodiscard]] std::string summary(const MemZones&);

// ------------------------------------------------------------------ modeles ---
//  Pour aller vite : "variables" (les plages qu'occupent ses variables liees,
//  arrondies a la centaine), "automate" (la configuration de l'automate du
//  projet : %M et %MW), "comme:<equipement>" (les zones d'un autre), "tout"
//  (0-65535 partout), "registres" (maintien et entree 0-9999), "aucune".
struct Preset {
    std::string key, label;
};
// `plcBits`, `plcWords` : %M et %MW de la configuration de l'automate (0 : inconnus).
[[nodiscard]] std::vector<Preset> presets(const Project&, const Equipment&, std::uint32_t plcBits, std::uint32_t plcWords);
bool applyPreset(const Project&, const Equipment&, std::string_view key, std::uint32_t plcBits, std::uint32_t plcWords,
                 MemZones& out, std::string* why = nullptr);

// ---------------------------------------------------------------- detection ---
// Lire `count` cases de `table` a partir de `first` : 0 (lu), le code
// d'exception, ou -1 (pas de reponse, liaison perdue).
using Reader = std::function<int(MemTable table, std::uint32_t first, std::uint32_t count)>;
struct DetectTable {
    MemTable              table{MemTable::Holding};
    std::vector<MemRange> ranges;
    bool                  absent{false};      // fonction refusee (exception 01)
    bool                  everywhere{false};  // toutes les adresses repondent
    bool                  failed{false};      // plus de reponse
    bool                  cut{false};         // trop de requetes : arrete
    std::string           comment;            // "d'un seul tenant depuis 0", "deux plages"...
    int                   requests{0};
};
struct DetectReport {
    std::vector<DetectTable> tables;
    int                      requests{0};
    double                   seconds{0};
    bool                     stopped{false};
    bool                     failed{false};
    std::string              why;
    // Les zones trouvees (declarees), et une ligne pour l'origine.
    [[nodiscard]] MemZones zones(const std::string& origin) const;
};
// Une table ; `stop` (peut etre nul) arrete ; `progress` (0..1) pour la barre.
[[nodiscard]] DetectTable detectTable(MemTable, const Reader&, const std::atomic<bool>* stop = nullptr,
                                      const std::function<void(double)>& progress = {}, int maxRequests = 20000);

// La detection d'un equipement, sur son fil, par un client Modbus TCP.
class Detector {
public:
    struct Target {
        std::string host{"127.0.0.1"};
        int         port{502};
        int         unit{1};
        int         timeoutMs{1000};
    };
    struct Progress {
        bool        running{false};
        bool        done{false};
        int         table{0};          // 0..3 : la table en cours
        double      fraction{0};       // 0..1 : le tout
        int         requests{0};
        double      seconds{0};
        std::string line;              // "registres de maintien : 40001-40100..."
    };
    Detector() = default;
    ~Detector();
    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;

    void start(Target target);
    void stop();                       // Arreter : le rapport dit ou il en etait
    [[nodiscard]] bool running() const;
    [[nodiscard]] Progress progress() const;
    [[nodiscard]] std::optional<DetectReport> report() const;   // fini (ou arrete)
    [[nodiscard]] const Target& target() const noexcept { return target_; }

private:
    void run();
    Target                        target_;
    std::thread                   thread_;
    std::atomic<bool>             stop_{false};
    mutable std::mutex            mutex_;
    Progress                      progress_;
    std::optional<DetectReport>   report_;
};

// ----------------------------------------------------------------- le scanner ---
//  SCANNER L'EQUIPEMENT : ses zones lues en boucle (lecture seule), une passe
//  toutes les `periodMs` ; pour chaque case, sa valeur, combien de fois elle a
//  change, son minimum et son maximum. Une case "active" : non nulle, ou qui a
//  change. Zones non declarees : les plages de `fallback` (celles des variables).
struct CellStat {
    std::uint16_t value{0};
    std::uint16_t min{0}, max{0};
    std::uint32_t changes{0};
    bool          seen{false};
    [[nodiscard]] bool active() const noexcept { return seen && (value != 0 || changes > 0); }
};
class Scanner {
public:
    struct Target {
        std::string host{"127.0.0.1"};
        int         port{502};
        int         unit{1};
        int         timeoutMs{1000};
        int         periodMs{1000};
    };
    struct State {
        bool        running{false};
        int         passes{0};
        int         requestsPerPass{0};
        double      passMs{0};
        std::string why;                    // la derniere erreur
        double      startedAt{0}, seconds{0};
    };
    Scanner() = default;
    ~Scanner();
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;
    // `zones` : ce qui est lu (non declarees : `fallback`).
    void start(Target target, const MemZones& zones, const MemZones& fallback = {});
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] std::optional<CellStat> stat(MemTable, std::uint32_t offset) const;
    [[nodiscard]] std::size_t activeCells() const;
    [[nodiscard]] const Target& target() const noexcept { return target_; }
    [[nodiscard]] MemZones scanned() const;
    void clear();

private:
    void run();
    Target                                        target_;
    MemZones                                      zones_;
    std::thread                                   thread_;
    std::atomic<bool>                             stop_{false};
    mutable std::mutex                            mutex_;
    State                                         state_;
    std::array<std::vector<CellStat>, 4>          cells_;
};

// ------------------------------------------------------------------- la carte ---
// Qui ecrit vraiment une variable (un chemin : "Four1.Consigne", "Fours[2].Vannes[1].Position") :
// "Champ_Consigne (Vue_Four, champ de saisie)", "script Regulation_Four (ligne 12)",
// "Btn_Plus (Vue_Four, action Affecter)", "la recette Recettes_Four". Une
// ecriture de la racine (Four1 := Four2 ; Fours[i].x := ...) compte pour chaque case.
[[nodiscard]] std::vector<std::string> writersOf(const Project&, std::string_view path);

// L'index des ecritures du projet (1.11.2, BLK) : bati en UNE passe (chaque script
// analyse une fois), range par racine du chemin (une ecriture ne touche qu'une case
// de sa racine). writersOf(path) donne exactement writersOf(projet, path), dans le
// meme ordre ; la carte d'un equipement ne relit plus le projet pour chaque variable
// (36,5 s sur Tunnel_Paris_CDG pour un clic sur IHM > Configuration > Equipements).
class WriteIndex {
public:
    explicit WriteIndex(const Project&);
    [[nodiscard]] std::vector<std::string> writersOf(std::string_view path) const;
    [[nodiscard]] std::size_t entries() const noexcept { return entries_; }

private:
    struct Site {                    // un endroit qui ecrit, dans l'ordre du projet
        std::string label, tail;     // code : label + ligne + tail
        bool        code{false};
    };
    struct Entry {
        std::string   key;           // le chemin ecrit (keyOf)
        std::uint32_t site{0};
        int           line{0};
    };
    std::vector<Site>                                    sites_;
    std::map<std::string, std::vector<Entry>>            roots_;   // par racine du chemin
    std::size_t                                          entries_{0};
};
// L'index du projet, garde tant que ce qu'il lit ne change pas (une empreinte des
// vues, actions, scripts, fonctions et recettes) : refait seulement au changement.
[[nodiscard]] std::shared_ptr<const WriteIndex> writeIndexOf(const Project&);
// Combien de fois l'index a ete bati (pour les essais).
[[nodiscard]] std::size_t writeIndexBuilds() noexcept;

struct MapVar {
    std::string   name;               // la case : "Four1.Consigne"
    std::string   root;               // la variable IHM : "Four1"
    std::string   address;            // "43003"
    std::string   type;               // "REAL"
    MemTable      table{MemTable::Holding};
    std::uint32_t first{0}, last{0};  // les cases occupees (des bits pour 0x, 1x)
    int           bit{-1};            // le bit d'un mot (BOOL range par 16)
    bool          writable{true};     // l'acces permet d'ecrire
    bool          corrected{false};   // une adresse corrigee (une case d'une structure placee ailleurs)
    bool          outOfZone{false};
    std::vector<std::string> writers;
    int           verdict{0};         // 0 seule, 1 partagee en lecture, 2 attention, 3 erreur (chevauchement)
    [[nodiscard]] bool written() const noexcept { return writable && !writers.empty(); }
};
struct MapConflict {
    std::size_t   a{0}, b{0};         // les deux variables (dans vars)
    int           severity{1};        // 1 partage en lecture, 2 attention, 3 erreur
    MemTable      table{MemTable::Holding};
    std::uint32_t first{0}, last{0};  // les cases communes
};
struct MemoryMap {
    std::string              equipment;
    MemZones                 zones;
    std::vector<MapVar>      vars;
    std::vector<MapConflict> conflicts;
    bool                     lowWordFirst{true};
    [[nodiscard]] std::size_t count(int severity) const noexcept;
    [[nodiscard]] std::size_t outOfZone() const noexcept;
    // Les variables sur la case (dans l'ordre de vars).
    [[nodiscard]] std::vector<std::size_t> at(MemTable, std::uint32_t offset) const;
};
[[nodiscard]] MemoryMap buildMap(const Project&, const Equipment&);
// L'automate du projet : les variables du plan (programme et table des
// adresses) ; ses zones, celles de sa configuration (0 : inconnues).
[[nodiscard]] MemoryMap buildPlcMap(const Project&, const comm::Plan&, std::uint32_t plcBits, std::uint32_t plcWords);
// "attention : Vitesse (ecrite par ...) et Vannes[1].Position (lue) sur 40007" ; le texte d'un conflit.
[[nodiscard]] std::string conflictText(const MemoryMap&, const MapConflict&);

// Une valeur vue de plusieurs facons : (libelle, texte). `next` : le mot suivant
// (s'il existe) pour les types de deux mots.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> interpretations(std::uint16_t word, std::optional<std::uint16_t> next,
                                                                                bool lowWordFirst);

} // namespace hmi::zones
