// =============================================================================
//  hmi/HmiModbus.hpp - Modbus TCP : les trames, le client, le serveur
// -----------------------------------------------------------------------------
//  LOT 14. Le protocole de l'automate reel, tel que le parlent les M340, M580,
//  Premium et la plupart des equipements : l'en-tete MBAP (transaction,
//  protocole 0, longueur, esclave) puis la fonction et ses donnees, en
//  grand-boutiste.
//
//    1  lire des bits (bobines)           %M
//    2  lire des bits d'entree
//    3  lire des mots (registres)         %MW, %MD, %MF
//    4  lire des mots d'entree
//    5  ecrire un bit        15  ecrire des bits
//    6  ecrire un mot        16  ecrire des mots
//    43 lire l'identification de l'equipement (fabricant, produit, version)
//
//  LE CLIENT est synchrone, un echange a la fois, avec un delai : c'est la
//  tache de communication (HmiComm) qui l'appelle, hors de l'ecran. Une reponse
//  dont la transaction n'est pas la bonne (celle d'une requete abandonnee a son
//  delai) est ecartee, pas prise pour la reponse attendue.
//
//  LE SERVEUR sert une memoire (Bank) : une memoire simple pour les essais, ou
//  le simulateur de l'automate (le serveur de demonstration). Il tourne sur son
//  propre fil ; plusieurs clients a la fois.
// =============================================================================
#pragma once

#include "HmiNet.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hmi::modbus {

enum Function : std::uint8_t {
    kReadCoils = 1, kReadDiscreteInputs = 2, kReadHoldingRegisters = 3, kReadInputRegisters = 4,
    kWriteSingleCoil = 5, kWriteSingleRegister = 6, kWriteMultipleCoils = 15, kWriteMultipleRegisters = 16,
    kReadDeviceIdentification = 43,
};

// Les limites du protocole : une trame fait 260 octets au plus.
inline constexpr int kMaxReadRegisters = 125;
inline constexpr int kMaxWriteRegisters = 123;
inline constexpr int kMaxReadBits = 2000;
inline constexpr int kMaxWriteBits = 1968;

// "2 : adresse illegale" ; "fonction 3 (lire des mots)".
[[nodiscard]] std::string exceptionText(int code);
[[nodiscard]] std::string functionName(int function);

using Frame = std::vector<std::uint8_t>;
using Identification = std::vector<std::pair<std::uint8_t, std::string>>;   // (objet, texte)

// Une requete : le client l'ecrit, le serveur la lit.
struct Request {
    std::uint16_t              transaction{0};
    std::uint8_t               unit{255};
    std::uint8_t               function{0};
    std::uint16_t              address{0};
    std::uint16_t              count{0};       // lectures, ecritures multiples
    std::vector<std::uint16_t> registers;      // 6 (un), 16
    std::vector<bool>          bits;           // 5 (un), 15
    std::uint8_t               idCode{1};      // 43 : 1 de base, 2 courante
};

struct Response {
    std::uint16_t              transaction{0};
    std::uint8_t               unit{255};
    std::uint8_t               function{0};
    std::uint8_t               exception{0};   // non nul : une reponse d'exception
    std::uint16_t              address{0};     // l'echo des ecritures
    std::uint16_t              value{0};       // 5, 6 : la valeur ; 15, 16 : la quantite
    std::vector<std::uint16_t> registers;      // 3, 4
    std::vector<bool>          bits;           // 1, 2
    Identification             objects;        // 43
};

[[nodiscard]] Frame encode(const Request&);
[[nodiscard]] Frame encode(const Response&);
// Une requete lue. Faux : la trame est illisible (why) - le serveur ferme alors
// la connexion. Vrai avec `exception` non nul : une requete bien formee mais
// refusee (1 : fonction non prise en charge ; 3 : quantite hors limites).
bool decodeRequest(const std::uint8_t* frame, std::size_t size, Request& out, int& exception, std::string* why = nullptr);
// La reponse a `asked`. Faux : illisible, ou pas la reponse a cette requete.
bool decodeResponse(const std::uint8_t* frame, std::size_t size, const Request& asked, Response& out, std::string* why = nullptr);

// ----------------------------------------------------------- lot 15 : l'espion ---
// Chaque trame qu'un client envoie ou recoit (les liaisons de l'IHM, l'outil
// Modbus) passe ici quand un espion ecoute : `sent` vrai pour une requete,
// `local` et `peer` : "192.168.1.5:50712", "192.168.1.30:502". Appele sur le
// fil du client : l'espion copie et rend la main.
using Tap = std::function<void(bool sent, const std::string& local, const std::string& peer, const std::vector<std::uint8_t>& adu)>;
void setTap(Tap tap);        // vide : plus d'espion
[[nodiscard]] bool tapped() noexcept;

// ----------------------------------------------------- lot 17 : qui parle ---
// Un nom pour une connexion locale d'un client de l'application (sa localName,
// "127.0.0.1:50712") : "IHM", "Outil Modbus", "Detection des zones".
void labelClient(const std::string& localName, std::string label);
// Le nom d'un pair : son libelle s'il en a un, sinon lui-meme.
[[nodiscard]] std::string clientLabel(const std::string& peer);
// Sur le fil d'un serveur, pendant qu'il sert une requete : qui la demande
// (clientLabel du pair) ; vide ailleurs. La memoire d'un jumeau le retient.
[[nodiscard]] std::string servingClient();

// ------------------------------------------------------------------- client ---
struct Outcome {
    bool        ok{false};
    int         exception{0};    // l'equipement a repondu par une exception
    bool        timeout{false};  // pas de reponse dans le delai (la connexion reste)
    bool        lost{false};     // la connexion est perdue (fermee, coupee, trame illisible)
    std::string why;
    double      ms{0};           // la duree de l'echange
};

class Client {
public:
    struct Settings {
        std::string host{"127.0.0.1"};
        int         port{502};
        int         unit{255};
        int         timeoutMs{1000};
    };
    Client() = default;
    explicit Client(Settings s) : settings_(std::move(s)) {}

    void setSettings(Settings s) { settings_ = std::move(s); }
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }

    Outcome connect();
    void    disconnect() { socket_.close(); }
    [[nodiscard]] bool connected() const noexcept { return socket_.valid(); }

    Outcome readBits(bool discrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out);
    Outcome readRegisters(bool input, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out);
    Outcome writeCoil(std::uint16_t address, bool value);
    Outcome writeRegister(std::uint16_t address, std::uint16_t value);
    Outcome writeCoils(std::uint16_t address, const std::vector<bool>& values);
    Outcome writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values);
    // Fabricant, produit, version... (fonction 43) : l'identification courante,
    // sinon celle de base.
    Outcome readIdentification(Identification& out);

    // Un echange : la requete (sa transaction et son esclave sont poses ici) et sa reponse.
    Outcome exchange(Request& request, Response& response);
    // Lot 15 : une trame telle quelle (l'outil Modbus, Trames) - en-tete MBAP
    // compris, rien n'est corrige - et la premiere trame qui revient (selon la
    // longueur de son en-tete), dans le delai.
    Outcome raw(const Frame& request, Frame& response);
    // Le nom de la connexion : "192.168.1.5:50712" (vide : pas connecte).
    [[nodiscard]] std::string localName() const { return socket_.localName(); }

private:
    Settings      settings_;
    net::Socket   socket_;
    std::uint16_t next_{1};
};

// ------------------------------------------------------------------ serveur ---
// Ce que sert un serveur. Chaque methode rend 0, ou le code d'exception
// (2 : adresse illegale ; 4 : defaut de l'equipement).
class Bank {
public:
    virtual ~Bank() = default;
    virtual int readBits(bool discrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) = 0;
    virtual int readRegisters(bool input, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) = 0;
    virtual int writeBits(std::uint16_t address, const std::vector<bool>& values) = 0;
    virtual int writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) = 0;
    [[nodiscard]] virtual Identification identification() { return {}; }
};

// Une memoire simple : `bits` bits et `words` mots ; au-dela, l'adresse est
// illegale. Les bits d'entree (2) et les mots d'entree (4) lisent les memes.
class MemoryBank : public Bank {
public:
    explicit MemoryBank(std::size_t bits = 65536, std::size_t words = 65536);
    int readBits(bool discrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) override;
    int readRegisters(bool input, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) override;
    int writeBits(std::uint16_t address, const std::vector<bool>& values) override;
    int writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) override;
    [[nodiscard]] Identification identification() override;

    void setIdentification(Identification id);
    [[nodiscard]] std::uint16_t word(std::size_t address) const;
    void setWord(std::size_t address, std::uint16_t value);
    [[nodiscard]] bool bit(std::size_t address) const;
    void setBit(std::size_t address, bool value);

protected:
    mutable std::mutex          mutex_;
    std::vector<std::uint16_t>  words_;
    std::vector<std::uint8_t>   bits_;
    Identification              id_;
};

// Ce que la memoire repond a une requete (le serveur ; les essais sans reseau).
[[nodiscard]] Response serve(Bank& bank, const Request& request);

class Server {
public:
    Server() = default;
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Ecoute sur bindAddress:port (port 0 : un port libre, port() le dit) et sert
    // `bank` sur son propre fil.
    bool start(const std::string& bindAddress, int port, std::shared_ptr<Bank> bank, std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] int  port() const noexcept { return port_; }
    // Muet : il accepte et lit, mais ne repond plus (un cable debranche cote
    // automate) - pour les essais des delais, et pour la demonstration.
    void setMute(bool on) noexcept { mute_.store(on); }
    [[nodiscard]] bool mute() const noexcept { return mute_.load(); }
    // Lot 17 : le temps de reponse d'un appareil simule (plus ou moins jitterMs) ;
    // et l'espion du cote serveur (un jumeau visible sur le vrai reseau : ses
    // clients ne sont pas ceux de l'application).
    void setDelay(int ms, int jitterMs = 0) noexcept;
    void setServerTap(bool on) noexcept { tapServer_.store(on); }

    struct Stats {
        std::uint64_t requests{0};
        std::uint64_t exceptions{0};
        std::size_t   clients{0};       // connectes maintenant
        std::string   lastClient;       // "192.168.1.20:50712"
    };
    [[nodiscard]] Stats stats() const;

private:
    void loop();

    net::Socket           listener_;
    std::shared_ptr<Bank> bank_;
    std::thread           thread_;
    std::atomic<bool>     stop_{false};
    std::atomic<bool>     running_{false};
    std::atomic<bool>     mute_{false};
    std::atomic<int>      delayMs_{0}, jitterMs_{0};
    std::atomic<bool>     tapServer_{false};
    int                   port_{0};
    mutable std::mutex    statsMutex_;
    Stats                 stats_;
};

} // namespace hmi::modbus
