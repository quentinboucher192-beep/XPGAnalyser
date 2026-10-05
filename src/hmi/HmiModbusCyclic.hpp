// =============================================================================
//  hmi/HmiModbusCyclic.hpp - la lecture cyclique a plusieurs requetes (1.9)
// -----------------------------------------------------------------------------
//  L'OUTIL MODBUS LIT UNE LISTE DE REQUETES. Chacune a sa cible (un equipement,
//  l'automate, une adresse tapee, l'esclave simule d'un equipement), sa
//  fonction (1 a 4), son adresse, son nombre, ses valeurs et sa periode (0 : la
//  periode commune). Une case la coupe sans la retirer.
//
//  UNE CONNEXION PAR CIBLE (MultiPoller) : un fil par cible lit ses requetes
//  l'une apres l'autre, la plus en retard d'abord ; deux cibles sont lues en
//  meme temps. Une requete qui echoue n'arrete pas les autres ; apres N echecs
//  de suite elle se met en pause (0 : jamais), avec l'erreur en clair, jusqu'a
//  Reprendre. "L'une apres l'autre" (perTarget faux) : un seul fil pour toutes.
//
//  LES VALEURS, PAS LES REGISTRES. Une requete se lit en valeurs (un DINT est
//  une valeur) : celles de ses variables (chacune son type), ou celles de son
//  format (un mot, deux mots, un texte, des bits). Chaque valeur a ses
//  chiffres - minimum, maximum, moyenne, changements, l'heure du dernier
//  changement - et son historique (le dernier quart d'heure) pour le
//  graphique et l'export.
//
//  LE REGROUPEMENT DES VARIABLES (planReads) : des voisines d'une meme table
//  tiennent dans une requete (125 mots, 2000 bits au plus), avec un trou
//  tolere ; les mots du trou sont lus au passage et ne sont pas montres.
//
//  L'EXPORT, POUR EXCEL (point-virgule, virgule decimale, BOM) : toutes les
//  requetes dans un fichier - une ligne par tour de la periode commune, une
//  colonne par valeur, une requete plus lente garde sa derniere valeur - ou
//  un fichier par requete. L'ENREGISTREMENT CONTINU a son propre fil (jamais
//  l'ecran) : une ligne par tour, un fichier par jour.
// =============================================================================
#pragma once

#include "HmiModbusTool.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hmi::mbtool {

// ------------------------------------------------------------------ valeurs ---
// Ce qu'est une valeur d'une requete : un nombre (un ou deux mots, lus dans un
// Format), un texte (N mots), un bit (une bobine, ou le bit d'un mot), une
// bande de bits sans nom ("0010 1100...").
enum class ValueKind : std::uint8_t { Number, Text, Bit, Bits };

struct ValueSpec {
    std::string name;                   // la variable qui porte cette adresse (vide : aucune)
    std::string type;                   // son type ("DINT", "REAL"...) ; vide : celui du format
    ValueKind   kind{ValueKind::Number};
    Format      format{Format::Unsigned};
    int         offset{0};              // depuis l'adresse de la requete : mots (3, 4) ou bits (1, 2)
    int         words{1};               // Number : 1 ou 2 ; Text : N mots ; Bits : N bits ; Bit : 1
    int         bit{-1};                // Bit dans un mot (%MW10.3) : 0 a 15 ; -1 : une bobine
    // Une variable IHM mise a l'echelle : la valeur montree (brut -> echelle).
    bool        scaled{false};
    double      rawMin{0}, rawMax{0}, scaleMin{0}, scaleMax{0};
    std::string unit;                   // "V", "bar" ; vide : aucune
    bool operator==(const ValueSpec&) const = default;
};

// Le type d'une variable -> sa valeur, a `offset` dans la requete ("INT",
// "UDINT", "REAL", "BOOL" (bit < 0 : une bobine), "STRING" (`words` mots)...).
[[nodiscard]] ValueSpec specForType(std::string name, std::string_view type, int offset, int bit = -1, int words = 0);
// Les valeurs d'une requete sans variables : son format, mot a mot ("Decimal",
// 32 bits : deux mots par valeur ; ASCII : un texte de `count` mots) ; des bits :
// un par bit (un bit nomme seul, les autres en bandes de 16 au plus).
// `names(offset)` : la variable qui porte cette place (vide : aucune).
[[nodiscard]] std::vector<ValueSpec> valuesForFormat(int function, int count, Format format,
                                                     const std::function<std::string(int offset)>& names = {});

// Une valeur lue : le nombre (NaN pour un texte, une bande), le texte (texte, bande).
struct Decoded {
    bool                       ok{false};
    double                     number{std::numeric_limits<double>::quiet_NaN()};
    std::string                text;
    std::vector<std::uint16_t> raw;      // les mots (ou les bits, 0 / 1) tels que lus
};
[[nodiscard]] Decoded decodeValue(const ValueSpec&, const std::vector<std::uint16_t>& registers, const std::vector<bool>& bits,
                                  bool lowFirst);

// ---- les nombres en francais (l'ecran) et pour Excel (le CSV) ----
// "184 811" (espace fine insecable), "400,8", "-12" ; decimals < 0 : au plus
// sept chiffres significatifs, sans zeros inutiles.
[[nodiscard]] std::string frenchNumber(double v, int decimals = -1, bool group = true);
// La valeur dans son format : "184 811", "0x05A2", "0000 0101 1010 0010",
// "400,8 V", "\xC2\xAB ARMOIRE \xC2\xBB" ; nombre : sa mise a l'echelle comprise.
[[nodiscard]] std::string valueText(const ValueSpec&, double number, const std::string& text, bool unit = true);
// Le meme pour le CSV : sans separateur de milliers, virgule decimale, texte brut.
[[nodiscard]] std::string csvValue(const ValueSpec&, double number, const std::string& text);

// ----------------------------------------------------------------- requetes ---
struct CyclicRequest {
    int                    id{0};             // R1 -> 1 (stable : il ne se renumerote pas)
    std::string            name;
    bool                   active{true};
    Target                 target;
    std::string            targetLabel;       // "API", "Centrale PM5560", "Variateur ATV320 . esclave simule"
    int                    function{3};       // 1 a 4
    int                    address{0};        // a partir de 0
    int                    count{1};
    bool                   lowFirst{true};
    int                    periodMs{0};       // 0 : la periode commune
    std::vector<ValueSpec> values;
    bool operator==(const CyclicRequest&) const = default;
};
// "3 mots, a partir de 0" -> faux si impossible (why).
bool validRequest(const CyclicRequest&, std::string* why = nullptr);
// La cle d'une cible : ses requetes partagent une connexion ("127.0.0.1:502:1").
[[nodiscard]] std::string targetKey(const Target&);

struct CyclicOptions {
    int  periodMs{500};       // la periode commune
    int  maxFailures{10};     // en pause apres N echecs de suite (0 : jamais)
    int  pauseMs{0};          // entre deux requetes d'une meme connexion
    bool perTarget{true};     // une connexion par cible, lues en meme temps (faux : toutes l'une apres l'autre)
    bool operator==(const CyclicOptions&) const = default;
};

// L'erreur en clair : "exception 02 : adresse illegale - l'appareil n'a pas ces
// registres", "delai depasse : pas de reponse en 1000 ms", "connexion impossible : ...".
[[nodiscard]] std::string failureText(const Reply&, int timeoutMs = 0);
// La meme, courte (le tableau) : "exception 02", "pas de reponse", "connexion impossible".
[[nodiscard]] std::string failureShort(const Reply&);

// ------------------------------------------------------- la lecture cyclique ---
class MultiPoller {
public:
    MultiPoller();
    ~MultiPoller();
    MultiPoller(const MultiPoller&) = delete;
    MultiPoller& operator=(const MultiPoller&) = delete;

    // Les requetes et les reglages. En marche aussi : une requete inchangee
    // garde ses chiffres et son historique ; une requete changee repart de zero ;
    // les fils suivent les cibles (une nouvelle : un fil de plus).
    void configure(std::vector<CyclicRequest> requests, CyclicOptions options);
    [[nodiscard]] std::vector<CyclicRequest> requests() const;
    [[nodiscard]] CyclicOptions options() const;
    void start();
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    // Couper (ou remettre) une requete sans la retirer ; reprendre une requete en pause.
    bool setActive(int id, bool on);
    bool resume(int id);
    // Les chiffres, l'historique et le journal repartent de zero.
    void clear();

    // ---- ce que l'ecran montre ----
    struct ValueState {
        bool                       has{false};       // lue au moins une fois
        double                     number{std::numeric_limits<double>::quiet_NaN()};
        std::string                text;
        std::vector<std::uint16_t> raw;
        double                     min{0}, max{0}, sum{0};
        std::uint64_t              count{0};         // les lectures (numeriques) qui comptent dans la moyenne
        std::uint64_t              changes{0};
        double                     changedAt{0};     // heure murale du dernier changement (la premiere lecture compte)
        [[nodiscard]] double mean() const noexcept { return count ? sum / static_cast<double>(count) : std::numeric_limits<double>::quiet_NaN(); }
    };
    struct RequestState {
        int                     id{0};
        bool                    active{true};
        bool                    paused{false};
        std::uint64_t           reads{0}, errors{0};
        double                  lastMs{0}, avgMs{0}, maxMs{0};
        int                     failuresInRow{0};
        bool                    lastOk{false};
        bool                    everOk{false};
        int                     exception{0};
        bool                    timeout{false};
        std::string             lastError;          // en clair (failureText)
        std::string             lastErrorShort;     // "exception 02"
        double                  lastAt{0};          // heure murale de la derniere lecture
        std::vector<ValueState> values;
    };
    [[nodiscard]] std::vector<RequestState> states() const;
    [[nodiscard]] std::optional<RequestState> state(int id) const;

    struct JournalEntry {
        double      t{0};
        int         id{0};
        bool        ok{false};
        double      ms{0};
        std::string text;        // les valeurs lues, ou l'erreur
    };
    // Le plus recent en tete ; `id` > 0 : cette requete seulement.
    [[nodiscard]] std::vector<JournalEntry> journal(std::size_t max = 500, int id = 0) const;
    [[nodiscard]] std::size_t journalSize() const;

    // Les points (heure murale, nombre) d'une valeur, les `seconds` dernieres
    // secondes avant `until` (0 : maintenant).
    [[nodiscard]] std::vector<std::pair<double, double>> history(int id, std::size_t value, double seconds, double until = 0) const;
    // Combien de lectures gardees (toutes requetes) ; la plus ancienne.
    [[nodiscard]] std::size_t samples() const;

    // ---- le CSV ----
    // Toutes les requetes dans un fichier : une ligne par tour de `stepMs` (la
    // periode commune), de la premiere lecture a la derniere ; une colonne par
    // valeur (brutes : une par registre). `maxLines` > 0 : les dernieres seulement.
    [[nodiscard]] std::string csvAll(bool raw, int stepMs = 0, std::size_t maxLines = 0, std::size_t* lines = nullptr) const;
    // Un fichier par requete : l'heure, la duree, l'etat, puis ses valeurs.
    [[nodiscard]] std::vector<std::pair<int, std::string>> csvEach(bool raw, std::size_t maxLines = 0) const;
    // Le titre de la colonne d'une valeur : "R2 DINT_1058", "R1 42740", "R5 U12 (V)".
    [[nodiscard]] static std::string columnTitle(const CyclicRequest&, const ValueSpec&);

    // ---- l'enregistrement continu (MB-7) ----
    // Tant que la lecture tourne : une ligne par tour de la periode commune dans
    // `folder`/AAAA-MM-JJ_<jeu>.csv (un fichier par jour), sur son propre fil.
    bool startRecording(const std::string& folder, const std::string& setName, std::string* why = nullptr);
    void stopRecording();
    [[nodiscard]] bool recording() const noexcept { return recording_.load(); }
    [[nodiscard]] std::string recordFile() const;          // le fichier du jour (vide : aucun)
    [[nodiscard]] std::uint64_t recordedLines() const noexcept { return recorded_.load(); }
    [[nodiscard]] std::string recordError() const;

private:
    struct Sample {
        double                     t{0};
        bool                       ok{false};
        double                     ms{0};
        std::vector<std::uint16_t> registers;
        std::vector<bool>          bits;
    };
    struct Entry {
        CyclicRequest              request;
        std::uint64_t              generation{0};
        RequestState               state;
        std::deque<Sample>         samples;
        double                     sumMs{0};
        double                     nextDue{0};          // horloge monotone (s)
        bool                       busy{false};         // un fil la lit en ce moment
    };
    struct Worker {
        std::string                         key;         // la cible ("" : toutes, l'une apres l'autre)
        std::thread                         thread;
        std::shared_ptr<std::atomic<bool>>  stop;
        std::shared_ptr<std::atomic<bool>>  done;        // le fil a fini (il se joint sans attendre)
    };
    void syncWorkers();                                  // sous mutex_
    void work(std::string key, std::shared_ptr<std::atomic<bool>> stop);
    void record(int id, std::uint64_t generation, const Reply& r, double wall);
    void recorderLoop(std::shared_ptr<std::atomic<bool>> stop);
    [[nodiscard]] std::string recordLine(double wall, bool header) const;   // sous mutex_
    [[nodiscard]] Entry*       entry(int id);
    [[nodiscard]] const Entry* entry(int id) const;
    void reapRetired();

    mutable std::mutex                  mutex_;
    std::vector<Entry>                  entries_;
    CyclicOptions                       options_;
    std::uint64_t                       generations_{0};
    std::deque<JournalEntry>            journal_;
    std::vector<Worker>                 workers_;
    std::vector<Worker>                 retired_;        // arretes, joints plus tard (jamais l'ecran n'attend)
    std::atomic<bool>                   running_{false};
    std::condition_variable             wake_;
    // L'enregistrement.
    std::atomic<bool>                   recording_{false};
    std::atomic<std::uint64_t>          recorded_{0};
    std::thread                         recorder_;
    std::shared_ptr<std::atomic<bool>>  recorderStop_;
    std::string                         recordFolder_, recordSet_, recordFile_, recordError_, recordHeader_;
};

// ------------------------------------------------- regrouper des variables ---
// Une variable a lire : sa table (1 bobines, 2 entrees TOR, 3 registres de
// maintien, 4 registres d'entree : la fonction de lecture), sa place, sa taille.
struct ReadItem {
    std::string name;
    std::string type;                  // "INT", "DINT", "BOOL", "STRING"
    int         function{3};
    int         offset{0};             // le premier mot (ou bit)
    int         words{1};              // les mots occupes (1 pour un bit, un bit de mot)
    int         bit{-1};               // le bit d'un mot (%MW10.3) ; -1 : aucun
    // Une variable IHM mise a l'echelle, son unite (reprises dans sa valeur).
    bool        scaled{false};
    double      rawMin{0}, rawMax{0}, scaleMin{0}, scaleMax{0};
    std::string unit;
};
struct PlannedRead {
    int                      function{3};
    int                      address{0};
    int                      count{0};
    std::vector<std::size_t> items;    // les rangs dans la liste, par adresse
    int                      hidden{0}; // mots (bits) lus au passage, pas montres
    // Les valeurs de la requete (une par variable), a leur place.
    [[nodiscard]] std::vector<ValueSpec> values(const std::vector<ReadItem>& list) const;
};
// Les requetes d'une liste de variables : par table, par adresse ; `merge` :
// deux voisines a moins de `gap` mots (16 x gap bits) l'une de l'autre dans la
// meme requete (sinon seulement les contigues) ; `maxWords` mots, `maxBits` bits au plus.
[[nodiscard]] std::vector<PlannedRead> planReads(const std::vector<ReadItem>& items, bool merge, int gap,
                                                 int maxWords = 125, int maxBits = 2000);

// "1 \xC2\xB7 bits", "3 \xC2\xB7 mots" ; "1 - lire des bits (bobines)"...
[[nodiscard]] std::string functionShort(int function);
[[nodiscard]] std::string functionLong(int function);
// La place Modicon d'une adresse (a partir de 0) : "42740", "00001", "30005", "10017" ; "48503".
[[nodiscard]] std::string modiconOf(int function, int address);
// La place Schneider : "%MW2739", "%M0", "%IW4", "%I7" ; un bit de mot : "%MW10.3".
[[nodiscard]] std::string schneiderOf(int function, int address, int bit = -1);

// Les formats d'une requete : leurs cles (le disque) et leurs libelles courts (le tableau).
[[nodiscard]] std::string_view formatKey(Format) noexcept;
[[nodiscard]] std::optional<Format> formatFromKey(std::string_view key) noexcept;
[[nodiscard]] std::string formatShort(Format);

// Le texte CSV d'une case (guillemets si besoin).
[[nodiscard]] std::string csvCell(std::string_view text);
// "14:32:10,250" ; "01/10/2026".
[[nodiscard]] std::string clockText(double wall, bool millis = true);
[[nodiscard]] std::string dateText(double wall, bool iso = false);

} // namespace hmi::mbtool
