// =============================================================================
//  hmi/HmiComm.hpp - la communication avec l'automate reel (lot 14)
// -----------------------------------------------------------------------------
//  LE PLAN D'ADRESSAGE (Plan). Chaque variable de l'automate que l'IHM peut
//  lire par Modbus, avec sa place : les variables localisees du programme
//  (Vitesse AT %MW100 : INT ; un tableau AT %MW2050 : ses cases a la suite) et
//  la table des adresses du projet IHM (Configuration > Communication). Une
//  adresse directe (%MW100, %MF20, %M5, %MW10.3) se lit telle quelle.
//
//      %M i        un bit (bobine i : fonctions 1, 5, 15)
//      %MW i       un mot (registre i : fonctions 3, 6, 16) - INT, UINT, WORD
//      %MW i.j     le bit j du mot i (ecrit : le mot relu, le bit change, le mot reecrit)
//      %MD i, %MF i  deux mots, i et i+1 (DINT, UDINT, DWORD, TIME ; REAL)
//      %IW i, %I i mots et bits d'entree (fonctions 4 et 2), en lecture seule
//
//  Les variables d'un autre genre (une entree topologique %I0.3.5, un bit
//  systeme %S, une structure localisee, une variable non localisee) sont dans
//  la liste des refusees, avec la raison : l'IHM le dit au lieu d'afficher 0.
//
//  LA LIAISON (Link). Un sim::Environment, comme le simulateur : le moteur de
//  l'IHM lit et ecrit a travers elle sans savoir qu'un reseau est derriere.
//  Une lecture ne touche jamais le reseau : elle rend la derniere valeur lue et
//  ABONNE la variable ; la tache de communication (son propre fil) lit, a
//  chaque periode, tout ce qui est abonne, regroupe en requetes de mots
//  contigus (une variable qu'on ne regarde plus depuis 10 s n'est plus lue).
//  1.9 : ce que l'IHM surveille sans le montrer (les conditions d'alarme) est
//  SUIVI EN PERMANENCE (follow) : abonne tout de suite, jamais oublie.
//  Une ecriture part en tete du cycle suivant, tout de suite.
//
//  LA QUALITE d'une valeur : bonne (lue au dernier cycle), ancienne (la liaison
//  est perdue, ou la valeur n'a pas ete relue depuis plusieurs periodes : on
//  garde la derniere), mauvaise (jamais lue, refusee par l'automate - adresse
//  illegale -, sans adresse, ou la liaison perdue depuis trop longtemps). Les
//  objets de la vue la montrent.
//
//  LE SERVEUR DE DEMONSTRATION (SimBank). La memoire d'un automate dont les
//  valeurs sont celles du simulateur, au meme plan d'adressage : l'IHM (ou une
//  supervision) s'y connecte en Modbus TCP comme a un vrai automate.
// =============================================================================
#pragma once

#include "HmiModbus.hpp"
#include "HmiModel.hpp"
#include "../sim/Interpreter.hpp"
#include "../sim/Value.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace domain { class Project; }

namespace hmi::comm {

// ------------------------------------------------------------------- le plan ---
enum class Area : std::uint8_t { Coils, DiscreteInputs, Holding, InputRegisters };
enum class Encoding : std::uint8_t { Bit, BitOfWord, BoolWord, Byte, Int16, UInt16, Int32, UInt32, Real32, Text };

struct Point {
    std::string   name;              // tel que le projet l'ecrit
    std::string   address;           // "%MW100", "%MW10.3", "%M5", "%MF20"
    Area          area{Area::Holding};
    std::uint16_t offset{0};         // le premier mot (ou bit)
    std::uint8_t  bit{0};            // BitOfWord : le bit dans le mot
    std::uint16_t size{1};           // les mots occupes (1 pour un bit)
    Encoding      encoding{Encoding::Int16};
    sim::Type     type{sim::Type::Int};
    bool          writable{true};
    std::string   origin;            // "programme", "table", "adresse"
    std::string   typeName;          // "INT", "ARRAY[0..15] OF WORD"
    std::string   description;
    // Un tableau : ses cases [low..high], chacune `size` mots (ou un bit), a la suite.
    bool          array{false};
    long long     low{0}, high{-1};

    [[nodiscard]] bool bits() const noexcept { return area == Area::Coils || area == Area::DiscreteInputs; }
    // Ce que le tableau occupe en tout (mots, ou bits).
    [[nodiscard]] std::size_t span() const noexcept;
    // "mots 100 a 101 (fonctions 3, 6, 16)", "bit 12 (fonction 1)" ; la place
    // seule ("mots 100 a 101", "mot 40, bit 3") ; les fonctions ("3, 6, 16").
    [[nodiscard]] std::string modbusText() const;
    [[nodiscard]] std::string placeText() const;
    [[nodiscard]] std::string functionsText() const;
};

// La place d'une valeur de ce type a cette adresse ("%MW100", "%MF20"...) :
// faux si l'adresse n'est pas lisible par Modbus (why dit pourquoi).
bool placeAddress(std::string_view address, sim::Type type, std::uint16_t chars, Point& out, std::string* why = nullptr);
// Le type que dit une adresse directe : %MW -> INT, %MD -> DINT, %MF -> REAL, %M -> BOOL.
[[nodiscard]] sim::Type typeOfAddress(std::string_view address);
// "Vitesse_Pompe", " tab [ 3 ] " -> "tab[3]" : la cle d'un nom (sans casse ni espaces).
[[nodiscard]] std::string keyOf(std::string_view name);

class Plan {
public:
    void add(Point p);
    void refuse(std::string name, std::string why);

    // La place d'un nom : une variable du plan, une case d'un tableau du plan
    // ("Tab[3]"), une adresse directe ("%MW100", "%MW0[57]").
    [[nodiscard]] std::optional<Point> resolve(std::string_view name) const;
    // Pourquoi un nom n'a pas de place ; vide : ce n'est pas une variable de
    // l'automate que le plan connaisse.
    [[nodiscard]] std::string whyNot(std::string_view name) const;

    [[nodiscard]] const std::vector<Point>& points() const noexcept { return points_; }
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& refused() const noexcept { return refused_; }
    // Ce qui identifie le plan : il change, la liaison se refait.
    [[nodiscard]] std::string signature() const;

private:
    std::vector<Point>                                points_;
    std::unordered_map<std::string, std::size_t>      index_;
    std::vector<std::pair<std::string, std::string>>  refused_;
    std::unordered_map<std::string, std::size_t>      refusedIndex_;
};

// Le type d'une variable par son chemin (le simulateur le sait) ; faux : inconnue.
using TypeOracle = std::function<bool(std::string_view path, sim::Type& type)>;

// Le plan : les variables globales localisees du programme (plc, nul : aucun
// programme) puis la table des adresses (elle l'emporte sur une localisation).
// Les variables globales NON localisees sont dans les refusees : l'IHM dit
// alors "sans adresse" au lieu de "inconnue".
[[nodiscard]] Plan buildPlan(const domain::Project* plc, const Communication& comm, const TypeOracle& types = {});

// ------------------------------------------------------------ les valeurs ---
[[nodiscard]] sim::Value decodeWords(const Point&, const std::uint16_t* words, bool lowWordFirst);
[[nodiscard]] sim::Value decodeBit(const Point&, bool bit);
// Les mots d'une valeur (BitOfWord : `current` est le mot tel qu'il est, son bit change).
[[nodiscard]] std::vector<std::uint16_t> encodeWords(const Point&, const sim::Value&, bool lowWordFirst, std::uint16_t current = 0);

// Les requetes d'un cycle : des blocs contigus d'une meme zone, au plus
// maxWords mots (maxBits bits), deux places a moins de `gap` mots reunies.
struct Block {
    Area                     area{Area::Holding};
    std::uint16_t            start{0};
    std::uint16_t            count{0};
    std::vector<std::size_t> items;     // les rangs, dans `points`
};
[[nodiscard]] std::vector<Block> planBlocks(const std::vector<Point>& points, int maxWords, int maxBits, int gap);

// ----------------------------------------------------------------- la liaison ---
struct Settings {
    std::string host{"127.0.0.1"};
    int         port{502};
    int         unit{255};
    int         timeoutMs{1000};
    int         periodMs{500};
    int         retryS{5};
    bool        lowWordFirst{true};
    int         maxWords{120};
    int         maxBits{1968};
    int         gap{8};
    bool        writes{true};
    int         badAfterS{30};
    // Lot 15 : qui est au bout, pour le journal ("l'automate", "l'equipement Centrale").
    std::string peer{"l'automate"};
    bool operator==(const Settings&) const = default;
};
[[nodiscard]] Settings settingsOf(const Communication&);

enum class Quality : std::uint8_t { Good, Pending, Stale, Bad, None };
// "bonne", "en attente", "ancienne", "mauvaise", "" (pas une variable de l'automate)
[[nodiscard]] std::string_view qualityName(Quality) noexcept;

struct Diagnostics {
    std::string   state;                 // "Connectee", "Connexion...", "Deconnectee", "Arretee"
    bool          connected{false};
    std::string   host;
    int           port{0};
    int           unit{0};
    std::string   since;                 // "2026-09-25 15:40:12" : l'etat depuis
    double        stateSeconds{0};       // ... depuis N s
    double        retryIn{0};            // deconnectee : le prochain essai dans N s
    std::uint64_t requests{0}, errors{0}, timeouts{0}, exceptions{0}, reconnects{0};
    std::uint64_t writes{0}, writeErrors{0};
    double        lastMs{0}, avgMs{0}, maxMs{0};   // le temps de reponse
    double        cycleMs{0};                      // la duree du dernier cycle de lecture
    std::size_t   subscribed{0}, good{0}, pending{0}, stale{0}, bad{0};
    std::size_t   followed{0};           // 1.9 : dont suivies en permanence (follow)
    std::size_t   requestsPerCycle{0};
    std::string   lastError;
    std::string   lastErrorAt;
    std::string   device;                // l'identification (fonction 43) ; vide : pas servie
    std::vector<std::pair<std::string, std::string>> badPoints;   // (variable, pourquoi) : anciennes et mauvaises, 50 au plus
};

// 1.9 : pas `final` - un essai peut deriver une liaison qui compte ce qu'on lui
// fait suivre (follow est virtuelle) ; le moteur ne connait que celle-ci.
class Link : public sim::Environment {
public:
    Link(Plan plan, Settings settings);
    ~Link() override;
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    // La tache de communication, sur son fil.
    void start();
    void stop();
    [[nodiscard]] bool started() const noexcept { return thread_.joinable(); }
    // 1.11.7 (le blocage du 05/10 : « Delier V » en simulation - la boucle principale attendait
    // dans stop() la fin d'un cycle entier, chaque ecriture payant le temps de reponse de
    // l'esclave) : arreter SANS ATTENDRE. Le fil finit la requete en cours et sort (les
    // ecritures pas encore parties sont abandonnees) ; finished() le dit, et stop() ne bloque
    // plus. stop() lui-meme n'attend plus qu'une requete, plus un cycle.
    void requestStop();
    [[nodiscard]] bool finished() const noexcept { return finished_.load(); }
    // Un cycle sur le fil de l'appelant (les essais ; l'outil Tester) : la
    // connexion s'il le faut, les ecritures, les lectures.
    void cycle();

    // sim::Environment
    bool read(std::string_view name, sim::Value& out) override;
    bool write(std::string_view name, const sim::Value& value) override;
    [[nodiscard]] bool exists(std::string_view name) override;
    bool call(std::string_view name, std::string_view instance,
              const std::vector<std::pair<std::string, sim::Value>>& arguments, sim::Value& result) override;
    void report(sim::Diagnostic) override {}

    [[nodiscard]] Quality     quality(std::string_view name, std::string* why = nullptr) const;
    // Lot 17 : quand l'IHM a lu (avec succes) et ecrit la variable pour la
    // derniere fois, et l'heure de maintenant (la meme horloge ; -1 : jamais) -
    // les pastilles L et E de la carte memoire.
    struct Activity {
        double readAt{-1}, writtenAt{-1}, now{0};
    };
    [[nodiscard]] Activity    activity(std::string_view name) const;
    [[nodiscard]] Diagnostics diagnostics() const;
    [[nodiscard]] bool        connected() const;
    // Refermer et se reconnecter tout de suite (le bouton Reconnecter).
    void reconnect();
    void resetCounters();
    // Ce qui merite une ligne du journal, depuis le dernier appel : liaison
    // etablie, perdue, retrouvee ; une ecriture refusee.
    [[nodiscard]] std::vector<std::string> takeEvents();

    [[nodiscard]] const Plan&     plan() const noexcept { return plan_; }
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    // Une variable que plus personne ne lit est oubliee au bout de N s (10).
    void setKeepSeconds(double s);
    // 1.9 : les variables SUIVIES EN PERMANENCE pour `owner` ("alarmes") :
    // abonnees tout de suite (lues des le cycle suivant, meme si rien ne les
    // lit), jamais oubliees tant qu'elles sont dans une liste. Chaque appel
    // remplace la liste de `owner` (vide : plus rien pour lui ; ce qui n'est plus
    // suivi par personne redevient une variable ordinaire, oubliee au bout de N s).
    // Les noms sans place dans le plan sont ignores. Rend le nombre de variables
    // nouvellement abonnees ; une liste inchangee ne coute qu'une comparaison.
    virtual std::size_t follow(std::string_view owner, const std::vector<std::string>& names);
    // Les noms suivis en permanence (tous proprietaires, tries, sans doublon).
    [[nodiscard]] std::vector<std::string> followed() const;

private:
    struct Entry {
        Point       point;
        sim::Value  value;
        bool        hasValue{false};
        double      readAt{-1};       // la derniere lecture reussie
        double      writtenAt{-1};    // lot 17 : la derniere ecriture demandee par l'IHM
        double      usedAt{0};        // la derniere lecture par l'IHM
        double      subscribedAt{0};
        std::string why;              // mauvaise : pourquoi
        bool        excluded{false};  // refusee par l'automate : relue seule, toutes les 30 s
        double      retryAt{0};
        bool        followed{false};  // 1.9 : suivie en permanence (follow) : jamais oubliee
        int         pendingWrites{0}; // 1.11.10 : ses ecritures pas encore parties (une lecture ne les defait pas)
    };
    struct PendingWrite {
        std::string key;
        Point       point;
        sim::Value  value;
    };

    void loop();
    bool ensureConnected(double now);
    void doWrites();
    void dropWrites();                 // sous mutex_
    void doReads(double now);
    void note(const modbus::Outcome& o, bool write);
    void lose(const std::string& why, double now);
    void event(std::string text);
    Entry* subscribe(std::string_view name, double now);

    Plan                     plan_;
    Settings                 settings_;
    modbus::Client           client_;
    mutable std::mutex       mutex_;
    std::condition_variable  wake_;
    std::unordered_map<std::string, Entry> entries_;   // cle : keyOf(nom)
    std::deque<PendingWrite> writes_;
    std::thread              thread_;
    std::atomic<bool>        stop_{false};
    std::atomic<bool>        finished_{false};    // 1.11.7 : le fil est sorti de loop()
    // L'etat et les compteurs (sous mutex_).
    bool                     connected_{false};
    bool                     everConnected_{false};
    bool                     reconnectAsked_{false};
    bool                     cycled_{false};
    double                   stateAt_{0};
    std::string              stateSince_;
    double                   nextTry_{0};
    int                      timeoutsInRow_{0};
    double                   keep_{10.0};
    double                   sumMs_{0};
    std::uint64_t            timedExchanges_{0};
    Diagnostics              counters_;
    std::vector<std::string> events_;
    // Les variables de l'automate que l'IHM a demandees et qui n'ont pas de place
    // (sans adresse) : cle -> (nom, pourquoi, derniere demande).
    struct Missing {
        std::string name, why;
        double      usedAt{0};
    };
    std::unordered_map<std::string, Missing> missing_;
    // 1.9 : les listes suivies en permanence, par proprietaire (follow).
    std::unordered_map<std::string, std::vector<std::string>> follows_;
};

// ------------------------------------------------ le serveur de demonstration ---
class SimBank final : public modbus::Bank {
public:
    // `bits`, `words` : la memoire servie (au-dela : adresse illegale).
    SimBank(Plan plan, bool lowWordFirst, std::size_t bits, std::size_t words, modbus::Identification id);

    // Sur le fil de l'ecran : les ecritures des clients passent dans le
    // simulateur, puis les valeurs du simulateur dans la memoire servie.
    void exchange(sim::Environment& plc);

    int readBits(bool discrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) override;
    int readRegisters(bool input, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) override;
    int writeBits(std::uint16_t address, const std::vector<bool>& values) override;
    int writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) override;
    [[nodiscard]] modbus::Identification identification() override;

    [[nodiscard]] const Plan& plan() const noexcept { return plan_; }
    [[nodiscard]] std::uint64_t clientWrites() const;

private:
    struct Write {
        std::string name;
        sim::Value  value;
    };
    // Les variables (et cases) touchees par une ecriture de [first, first + n).
    void queueTouched(Area area, std::size_t first, std::size_t n);

    Plan                        plan_;
    bool                        low_{true};
    mutable std::mutex          mutex_;
    std::vector<std::uint16_t>  words_;
    std::vector<std::uint8_t>   bits_;
    std::deque<Write>           writes_;
    modbus::Identification      id_;
    std::uint64_t               clientWrites_{0};
};

// ---------------------------------------------------------------- les objets ---
// Les chemins de l'automate qu'une expression (ou un texte a trous) lit, ecrits
// en entier : "Pression", "Tab[3]", "Armoires[0].ana.PT1.mes", "%MW100". Un
// index calcule (Tab[i]) n'en donne pas : sa place n'est connue qu'en marche.
[[nodiscard]] std::vector<std::string> plcPaths(std::string_view expression);

// Les chemins que le projet IHM lit ou ecrit - proprietes pilotees, textes a
// trous, variables des commandes, actions, alarmes, recettes, mesures
// archivees, scripts - sans doublon, dans l'ordre de rencontre. Les variables
// IHM, SYS., les vues et les parametres de vue n'y sont pas : le reste est
// peut-etre une variable de l'automate (le plan d'adressage le dit).
[[nodiscard]] std::vector<std::string> projectPlcPaths(const Project& p);

// L'etat de la communication en une ligne, et son ton : 0 neutre, 1 bon,
// 2 attention, 3 mauvais, 4 simulateur.
struct StatusLine {
    std::string text;
    int         tone{0};
};
[[nodiscard]] StatusLine commStatusLine(const Communication& comm, const Link* link, bool simulatorAttached,
                                        bool showAddress, bool showTime, bool compact);

// Les lignes du diagnostic automate : libelle, valeur, ton (comme ci-dessus).
struct DiagnosticRow {
    std::string label, value;
    int         tone{0};
};
[[nodiscard]] std::vector<DiagnosticRow> diagnosticRows(const Communication& comm, const Link* link,
                                                        bool simulatorAttached, bool demoRunning, int demoPort);

// La geometrie du diagnostic automate (celle du dessin et du clic) : le
// bandeau de titre, les lignes, la liste des variables en defaut, les deux
// boutons ("bouton:reconnecter", "bouton:compteurs").
struct DiagnosticLayout {
    Box    title, rows, list, reconnect, reset;
    double fontSize{13}, rowH{22}, labelW{200};
};
[[nodiscard]] DiagnosticLayout diagnosticLayout(const Object& o, double w, double h, std::size_t rows);
[[nodiscard]] std::string      diagnosticHit(const Object& o, double w, double h, double x, double y, std::size_t rows);

// La memoire d'un automate simule pour ce projet : sa taille (les %M et %MW
// configures, sinon 65 536), l'identification ("XpgAnalyzer", le processeur).
[[nodiscard]] std::shared_ptr<SimBank> makeSimBank(const domain::Project* plc, const Communication& comm,
                                                   const TypeOracle& types = {});

} // namespace hmi::comm
