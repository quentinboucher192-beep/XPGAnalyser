// =============================================================================
//  hmi/HmiDiscovery.hpp - 1.21.0 : la decouverte des equipements, sans intrusion
// -----------------------------------------------------------------------------
//  CE QUI SE PASSE AUTOUR DU SCANNER IP (HmiIpScan) - sans ecran, sans reseau :
//
//    LA DEMANDE      ce que l'utilisateur choisit dans le dialogue de
//                    confirmation : l'interface du PC, l'adresse de debut, celle
//                    de fin, le masque, les protocoles, la cadence. plan() la
//                    verifie et la resume : l'interface, la plage, le masque, les
//                    ports, le nombre d'adresses, la duree estimee, la cadence -
//                    ce que montre la confirmation (et la carte de l'IA) avant
//                    que rien ne parte. Le scan ne demarre qu'avec l'accord
//                    (ipscan::grant) donne au clic Lancer.
//
//    LA FICHE        classify() : pour chaque appareil, chaque information et
//                    sa certitude - CONFIRMEE (il a repondu : le ping, la
//                    connexion, la reponse Modbus, la table ARP), ANNONCEE (par
//                    l'appareil lui-meme : l'identification Modbus ; par le DNS :
//                    le nom), SUPPOSEE (le fabricant d'apres le prefixe MAC,
//                    "automate probable" d'apres le port 502, un protocole
//                    d'apres son seul port) ou NON IDENTIFIEE. Un modele qui
//                    n'est pas annonce n'est jamais invente.
//
//    LA COMPARAISON  analyze() : les appareils trouves et les equipements du
//                    projet ouvert (et l'automate du projet) - deja declares,
//                    absents du projet, declares mais injoignables, a une autre
//                    adresse, en double, incoherents (le port declare ferme, un
//                    Ethernet TCP/IP qui repond en Modbus). Et des PROPOSITIONS :
//                    ajouter un equipement (les champs de creer_equipement),
//                    mettre a jour une adresse, associer un appareil a un
//                    equipement seulement simule. RIEN n'est change ici : la
//                    modification du projet est une operation a part (le volet
//                    Equipements ou l'Assistant IA, une commande, Ctrl+Z).
//
//    LE JOURNAL      une ligne par scan (lance, puis termine ou arrete) : qui,
//                    quand, d'ou (Scanner IP, Assistant IA), l'interface, la
//                    plage, les protocoles, la cadence, le nombre d'adresses,
//                    d'interrogations, d'appareils trouves - dans le projet :
//                    ihm/reseau/decouvertes.csv, ecrit tout de suite (comme
//                    l'audit), jamais reecrit.
// =============================================================================
#pragma once

#include "HmiIpScan.hpp"
#include "HmiModel.hpp"
#include "HmiNetInfo.hpp"
#include "../core/Result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::discovery {

// ------------------------------------------------------------- les protocoles ---
// Ceux que propose la confirmation : chacun ses ports TCP (une connexion
// ordinaire ; seul Modbus est interroge, en lecture seule).
struct Protocol {
    std::string      key;       // "modbus"
    std::string      label;     // "Modbus TCP"
    std::vector<int> ports;     // {502}
    bool             byDefault{true};
    std::string      detail;    // ce qui est fait
};
[[nodiscard]] const std::vector<Protocol>& protocols();
// Par sa cle, son libelle ou un alias ("Modbus TCP", "modbus_tcp", "siemens", "EtherNet/IP", "http"...), sans casse ;
// nul : inconnu.
[[nodiscard]] const Protocol* protocol(std::string_view key);
[[nodiscard]] std::vector<std::string> defaultProtocols();
// "Modbus TCP (502)", "Web (HTTP, HTTPS) (80, 443)"
[[nodiscard]] std::string protocolText(const Protocol&);

// ---------------------------------------------------------------- la demande ---
struct Request {
    std::string              adapter;        // l'interface du PC (vide : aucune, une plage libre)
    std::string              start, end;     // "192.168.1.1", "192.168.1.254"
    std::string              mask;           // "24", "/24", "255.255.255.0" ; vide : aucun
    std::vector<std::string> protocols{defaultProtocols()};
    std::string              extraPorts;     // d'autres ports TCP : "8080, 5020"
    double                   rate{ipscan::kDefaultRate};
    int                      threads{ipscan::kDefaultThreads};
    int                      timeoutMs{400};
    bool                     names{true};
    bool                     modbusId{true};
    std::string              origin{"Scanner IP"};   // qui le demande : "Scanner IP", "Assistant IA"
    bool operator==(const Request&) const = default;
};

// Les interfaces du PC telles que les liste la confirmation.
struct Interface {
    std::string   name;       // "eth1", "Ethernet 2"
    std::string   label;      // "Ethernet 2 - 192.168.1.20 / 24"
    std::uint32_t ip{0};
    int           prefix{0};
    bool          up{false};
    bool          usable{false};   // branchee, une adresse, pas une 169.254
};
[[nodiscard]] std::vector<Interface> interfaces(const std::vector<netinfo::Adapter>&);
// Celle proposee d'office : la premiere Ethernet branchee avec une adresse, sinon la premiere utilisable ; vide : aucune.
[[nodiscard]] std::string defaultInterface(const std::vector<netinfo::Adapter>&);
// Debut, fin et masque : le reseau de l'interface (sans le reseau ni la diffusion). Faux : inconnue, ou sans adresse.
bool useInterface(Request&, const std::vector<netinfo::Adapter>&, std::string_view name, std::string* why = nullptr);
// Le masque applique a l'adresse de debut : debut et fin deviennent tout ce reseau.
bool applyMask(Request&, std::string* why = nullptr);
// Une plage ecrite d'un trait (comme ipscan::parseRange : "192.168.1.0/24", "192.168.1.1-50", une adresse) ;
// un /n pose aussi le masque.
bool setRange(Request&, std::string_view text, std::string* why = nullptr);

// L'ASSISTANT IA (l'outil detecter_equipements) : une plage d'un trait ("192.168.1.0/24", "192.168.1.10-40",
// "192.168.1.30"), des protocoles (cles, libelles ou alias ; vide : ceux par defaut), une cadence (0 : celle par
// defaut ; au-dela de 100 : refusee). L'interface : celle du PC dont le reseau contient la plage (aucune sinon).
// origin = "Assistant IA". Faux et `why` : illisible. Rien n'est lance : la demande va a la confirmation.
bool requestFrom(std::string_view range, const std::vector<std::string>& protocolNames, double rate, Request& out,
                 const std::vector<netinfo::Adapter>& adapters = {}, std::string* why = nullptr);

// LE PLAN : la demande verifiee, ce qui partira, et son resume.
struct Plan {
    ipscan::Options          options;
    std::string              interfaceText;    // "eth1 (192.168.1.20 / 24)" ; "aucune (plage libre)"
    std::string              rangeText;        // "192.168.1.1 - 192.168.1.254 (254 adresses)"
    std::string              maskText;         // "/24 (255.255.255.0)" ; vide : aucun
    std::vector<std::string> protocolLabels;   // "Modbus TCP (502)"
    std::size_t              addresses{0};
    std::uint64_t            lowProbes{0}, highProbes{0};
    double                   lowSeconds{0}, highSeconds{0};
    std::vector<std::string> notes;            // ce qu'il faut savoir (hors des reseaux du PC, la plage deborde du masque...)
    std::string              error;            // non vide : refusee, rien ne part
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};
[[nodiscard]] Plan plan(const Request&, const std::vector<netinfo::Adapter>& adapters);

// L'estimation : combien d'interrogations (au moins : personne ne repond ; au plus : tout repond, chaque port ouvert)
// et combien de temps, la cadence et les delais compris.
struct Estimate {
    std::uint64_t lowProbes{0}, highProbes{0};
    double        lowSeconds{0}, highSeconds{0};
};
[[nodiscard]] Estimate estimate(const ipscan::Options&);
[[nodiscard]] std::string durationText(double seconds);         // "40 s", "2 min 05 s", "1 h 10 min"
// Le resume de la confirmation : (libelle, valeur), dans l'ordre - Interface, Plage, Masque, Protocoles, Ports TCP,
// Adresses, Duree estimee, Cadence, Envoye.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> summary(const Plan&);
[[nodiscard]] std::string summaryText(const Plan&);             // "Interface : eth1 ...\nPlage : ..."

// La demande en texte (la reponse du dialogue, la carte de l'IA) : "cle=valeur" par ligne.
[[nodiscard]] std::string encodeRequest(const Request&);
bool decodeRequest(std::string_view text, Request& out, std::string* why = nullptr);

// ------------------------------------------------------------------- la fiche ---
enum class Certainty : std::uint8_t { Confirmed, Announced, Supposed, Unknown };
// "confirm\xC3\xA9", "annonc\xC3\xA9", "suppos\xC3\xA9", "non identifi\xC3\xA9"
[[nodiscard]] std::string_view certaintyText(Certainty) noexcept;

struct Fact {
    std::string key;      // "ip", "acces", "temps", "mac", "nom", "constructeur", "modele", "version", "type",
                          // "protocole" (un par protocole), "ports", "services", "identification"
    std::string label;    // "Adresse IP"
    std::string value;
    Certainty   certainty{Certainty::Confirmed};
    std::string source;   // "ping", "table ARP", "identification Modbus (fonction 43)", "pr\xC3\xA9" "fixe MAC"...
};

struct Device {
    ipscan::Host      host;
    std::vector<Fact> facts;
    // Pour les colonnes : la valeur (vide : inconnue) et sa certitude.
    std::string       vendor, model, type;
    Certainty         vendorCertainty{Certainty::Unknown}, modelCertainty{Certainty::Unknown}, typeCertainty{Certainty::Unknown};
    bool              modbus{false};         // Modbus TCP confirme (une reponse) ou suppose (le 502 ouvert)
    bool              industrial{false};     // un port industriel ouvert : "automate ou equipement industriel probable"
    [[nodiscard]] const Fact* fact(std::string_view key) const;
    [[nodiscard]] std::vector<const Fact*> all(std::string_view key) const;
};
[[nodiscard]] Device classify(const ipscan::Host&);
// "Schneider Electric (suppos\xC3\xA9)" ; vide : la valeur seule (confirmee) ; "non identifi\xC3\xA9".
[[nodiscard]] std::string valueWithCertainty(const std::string& value, Certainty);
// La fiche en lignes : "Adresse IP : 192.168.1.10 (confirm\xC3\xA9, ping)".
[[nodiscard]] std::string deviceText(const Device&);

// -------------------------------------------------------------- la comparaison ---
struct Declared {
    hmi::Id       id{kNoId};          // kNoId : l'automate du projet
    std::string   name;
    std::string   host;
    std::uint32_t ip{0};              // 0 : pas une IPv4 (un nom de poste)
    int           port{0};
    int           unit{0};
    bool          modbus{false};
    bool          simulatedOnly{false};   // seulement simule : pas d'appareil (un esclave virtuel)
    bool          plc{false};
    std::string   description;
};
// Les equipements du projet ; `withPlc` : l'automate du projet en tete (une liaison Modbus TCP).
[[nodiscard]] std::vector<Declared> declared(const Project&, bool withPlc);

enum class Status : std::uint8_t { Known, New, Unreachable, Moved, Duplicate, Inconsistent, ThisPc, NotScanned };
// "d\xC3\xA9" "clar\xC3\xA9", "absent du projet", "d\xC3\xA9" "clar\xC3\xA9 mais injoignable", "adresse diff\xC3\xA9rente",
// "doublon", "incoh\xC3\xA9rence", "ce PC", "hors de la plage"
[[nodiscard]] std::string_view statusText(Status) noexcept;

struct Finding {
    Status      status{Status::Known};
    std::string equipment;          // l'equipement du projet (vide : aucun)
    std::string address;            // l'adresse trouvee (vide : aucune)
    std::string declaredAddress;    // l'adresse du projet (vide : aucune)
    std::string text;               // la phrase
    Certainty   certainty{Certainty::Confirmed};
};

struct Proposal {
    enum class Kind : std::uint8_t { Add, UpdateAddress, Associate };
    Kind        kind{Kind::Add};
    std::string equipment;          // Add : le nom propose (libre dans le projet) ; sinon l'equipement existant
    std::string address;            // l'adresse trouvee
    std::string before;             // UpdateAddress, Associate : son adresse d'avant
    // Add : les champs de l'operation creer_equipement - "type" (modbus_tcp, ethernet_tcp), "adresse", "port",
    // "esclave", "description" ; UpdateAddress, Associate : "hote" (et "esclave" s'il a repondu).
    std::vector<std::pair<std::string, std::string>> props;
    std::string text;               // "Ajouter Modbus 192.168.1.10 (Modbus TCP/IP 192.168.1.10:502, esclave 255)"
    std::string reason;             // pourquoi ; une supposition le dit
    Certainty   certainty{Certainty::Confirmed};
    [[nodiscard]] std::string prop(std::string_view key) const;
};
// "ajouter", "mettre \xC3\xA0 jour l'adresse", "associer"
[[nodiscard]] std::string_view proposalKindText(Proposal::Kind) noexcept;

struct Scope {
    std::uint32_t              first{0}, last{0};   // la plage scannee
    std::vector<int>           ports;               // les ports essayes
    std::vector<std::uint32_t> local;               // les adresses de ce PC
    bool                       withPlc{true};       // l'automate du projet compte (une liaison Modbus)
    bool                       complete{true};      // le scan est alle au bout (arrete : un injoignable n'est pas conclu)
};

struct Report {
    std::vector<Device>   devices;      // par adresse croissante
    std::vector<Finding>  findings;
    std::vector<Proposal> proposals;
    std::size_t known{0}, fresh{0}, unreachable{0}, moved{0}, duplicates{0}, inconsistent{0}, local{0};
    [[nodiscard]] std::vector<const Finding*> about(std::string_view address) const;
    [[nodiscard]] const Proposal* proposalFor(std::string_view address) const;
    [[nodiscard]] const Device*   device(std::string_view address) const;
    // La colonne "Dans le projet" d'une adresse trouvee : "Centrale_1", "absent du projet", "Centrale_1 ? (adresse
    // diff\xC3\xA9rente)", "ce PC (eth0)"...
    [[nodiscard]] std::string projectText(std::string_view address) const;
    // "3 d\xC3\xA9" "clar\xC3\xA9s, 2 absents du projet, 1 injoignable..." ; vide : rien a dire.
    [[nodiscard]] std::string countsText() const;
};
[[nodiscard]] Report analyze(const Project&, const std::vector<ipscan::Host>& found, const Scope&);
// Le compte rendu en texte (la carte de l'IA, le presse-papiers) : les comptes, chaque appareil (sa fiche), les
// constats, les propositions - `maxDevices` fiches au plus.
[[nodiscard]] std::string reportText(const Report&, std::size_t maxDevices = 50);

// ------------------------------------------------------------------ le journal ---
struct JournalEntry {
    std::string   id;             // l'identifiant du scan (le lancement et la fin portent le meme)
    std::string   stamp;          // "2026-10-31 14:05:12.350"
    std::string   who;            // "jdupont@PC-ATELIER" (l'accord : ipscan::Permit::who)
    std::string   origin;         // "Scanner IP", "Assistant IA"
    std::string   state;          // "lance", "termine", "arrete", "refuse"
    std::string   interfaceName;
    std::string   range;          // "192.168.1.1 - 192.168.1.254 (254 adresses)"
    std::string   protocols;      // "Modbus TCP (502), Web (80, 443)"
    std::string   ports;          // "502, 80, 443"
    double        rate{0};
    std::size_t   addresses{0}, tried{0}, found{0};
    std::uint64_t probes{0};
    double        seconds{0};
    std::string   note;
    bool operator==(const JournalEntry&) const = default;
};
// "compte@PC" de cette seance (le compte du systeme, le nom du PC).
[[nodiscard]] std::string currentUser();
// Un identifiant neuf : "20261031-140512-a3f901".
[[nodiscard]] std::string newScanId();
// L'entree d'un lancement (state "lance") d'apres le plan et l'accord.
[[nodiscard]] JournalEntry startEntry(const Plan&, const ipscan::Permit&, std::string id, std::string origin);
// L'entree de la fin, d'apres celle du lancement et l'avancement.
[[nodiscard]] JournalEntry endEntry(const JournalEntry& started, const ipscan::Scanner::Progress&);
[[nodiscard]] std::string journalHeader();
[[nodiscard]] std::string journalLine(const JournalEntry&);           // une ligne CSV (';'), avec son retour
bool parseJournal(std::string_view text, std::vector<JournalEntry>& out, std::string* why = nullptr);
[[nodiscard]] std::string journalPath(const std::string& projectFolder);   // <dossier>/ihm/reseau/decouvertes.csv
// Ajoute la ligne au fichier du projet (l'en-tete s'il est neuf) ; un dossier vide : rien, ok.
[[nodiscard]] core::Status appendJournal(const JournalEntry&, const std::string& projectFolder);
[[nodiscard]] std::vector<JournalEntry> readJournal(const std::string& projectFolder, std::string* why = nullptr);
// "31/10 14:05 - jdupont@PC : 192.168.1.1 - 192.168.1.254 (254 adresses), 3 appareils (termin\xC3\xA9)"
[[nodiscard]] std::string journalText(const JournalEntry&);

} // namespace hmi::discovery
