// =============================================================================
//  deploy/Net.hpp - 1.28.0 : les sockets du deploiement reseau (TCP et UDP)
// -----------------------------------------------------------------------------
//  Windows (Winsock) et POSIX, sur le modele de hmi/HmiNet (que deploy/ ne lie
//  pas). Tout est non bloquant : chaque attente a son delai (wait), le TLS
//  (deploy/Tls) lit et ecrit par sendSome / receiveSome.
//
//    TcpSocket     connect (delai), listen (0 : port choisi par le systeme),
//                  accept (delai), sendSome / receiveSome, wait
//    UdpSocket     bind, diffusion, sendTo, receive (delai)
//    broadcastAddresses   l'adresse de diffusion de chaque interface IPv4
//    hostName             le nom de ce PC
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deploy::net {

// Winsock pret (une fois, sans danger entre fils). Faux : le reseau est indisponible.
bool startup();
// Le texte (francais) d'un code d'erreur du systeme ; lastError() : le dernier.
[[nodiscard]] std::string errorText(int code);
[[nodiscard]] int lastError();

class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Se connecter (adresse IPv4/IPv6 ou nom), en `timeoutMs` au plus.
    bool connect(const std::string& host, int port, int timeoutMs, std::string* why);
    // Ecouter sur `bindAddress` ("" ou "0.0.0.0" : toutes les interfaces). Le port pris par un
    // autre programme : faux, et `why` le dit (Windows : SO_EXCLUSIVEADDRUSE, personne ne le partage).
    bool listen(const std::string& bindAddress, int port, std::string* why);
    // Une connexion entrante (socket invalide : rien en `timeoutMs`). `peer` : "adresse:port".
    TcpSocket accept(int timeoutMs, std::string* peer = nullptr);

    // > 0 : octets envoyes ; 0 : il faudrait attendre ; -1 : erreur (lastError()).
    long sendSome(const void* data, std::size_t size);
    // > 0 : octets lus ; 0 : fin (l'autre bout a ferme) ; -2 : rien pour l'instant ; -1 : erreur.
    long receiveSome(void* data, std::size_t size);
    // 1 : pret ; 0 : delai ecoule ; -1 : erreur.
    int wait(bool forWrite, int timeoutMs) const;

    [[nodiscard]] bool valid() const noexcept { return fd_ != kInvalid; }
    [[nodiscard]] int localPort() const;
    // L'adresse de l'autre bout (sans le port), "" si inconnue.
    [[nodiscard]] std::string peerAddress() const;
    void close() noexcept;

private:
    static constexpr std::intptr_t kInvalid = -1;
    std::intptr_t fd_{kInvalid};
};

struct Datagram {
    std::string from;   // l'adresse IPv4 de l'envoyeur
    int fromPort{0};
    std::string data;
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // IPv4 ; port 0 : choisi par le systeme. Le port deja pris : faux, `why` le dit.
    bool bind(const std::string& bindAddress, int port, std::string* why);
    bool enableBroadcast();
    bool sendTo(const std::string& ipv4, int port, std::string_view data, std::string* why);
    // 1 : un datagramme ; 0 : rien en `timeoutMs` ; -1 : erreur.
    int receive(Datagram& out, int timeoutMs);
    [[nodiscard]] bool valid() const noexcept { return fd_ != kInvalid; }
    [[nodiscard]] int localPort() const;
    void close() noexcept;

private:
    static constexpr std::intptr_t kInvalid = -1;
    std::intptr_t fd_{kInvalid};
};

// L'adresse de diffusion de chaque interface IPv4 en service (sans la boucle locale), "192.168.1.255"...
[[nodiscard]] std::vector<std::string> broadcastAddresses();
// Le nom de ce PC ("PC-ATELIER-1") ; "PC" s'il est inconnu.
[[nodiscard]] std::string hostName();

} // namespace deploy::net
