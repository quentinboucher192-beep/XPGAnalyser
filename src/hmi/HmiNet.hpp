// =============================================================================
//  hmi/HmiNet.hpp - le reseau, sans bibliotheque : un socket TCP
// -----------------------------------------------------------------------------
//  LOT 14. Ce qu'il faut a l'exploitation reelle et rien de plus : Modbus TCP
//  vers l'automate (client), le serveur de demonstration qui expose le
//  simulateur, la messagerie (SMTP), la passerelle SMS (HTTP) et le serveur
//  web du poste. Windows (Winsock) et POSIX, derriere la meme classe.
//
//  TOUT A UN DELAI. Un automate debranche ne doit jamais geler l'ecran : se
//  connecter, envoyer, recevoir prennent chacun un delai en millisecondes, et
//  rendent la main a son terme avec la raison (" pas de reponse en 1000 ms ").
//  Le socket est non bloquant ; l'attente passe par poll (POSIX) ou select
//  (Windows).
//
//  AUCUN TYPE DU SYSTEME DANS CE FICHIER : Winsock ne se mele pas au reste du
//  programme (windows.h et ses macros restent dans HmiNet.cpp).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::net {

class Socket {
public:
    Socket() noexcept = default;
    ~Socket();
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    [[nodiscard]] bool valid() const noexcept { return fd_ != kInvalid; }
    void close() noexcept;

    // Client : une adresse IP ("192.168.1.10") ou un nom ; au plus timeoutMs
    // pour que la connexion s'etablisse. Faux : `why` dit pourquoi.
    bool connect(const std::string& host, int port, int timeoutMs, std::string* why = nullptr);

    // Serveur : ecoute sur bindAddress ("0.0.0.0" : toutes les cartes reseau ;
    // "127.0.0.1" : ce poste seulement). Port 0 : un port libre, que localPort()
    // donne ensuite.
    bool listen(const std::string& bindAddress, int port, std::string* why = nullptr);
    // Le client qui se presente dans les timeoutMs (0 : sans attendre) ; un
    // socket invalide sinon. `peer` : "192.168.1.20:50712".
    [[nodiscard]] Socket accept(int timeoutMs, std::string* peer = nullptr);

    // Tout envoyer, en timeoutMs au plus.
    bool sendAll(const void* data, std::size_t size, int timeoutMs, std::string* why = nullptr);
    bool sendAll(std::string_view text, int timeoutMs, std::string* why = nullptr) {
        return sendAll(text.data(), text.size(), timeoutMs, why);
    }
    // Exactement `size` octets, en timeoutMs au plus au total.
    bool receiveExact(void* data, std::size_t size, int timeoutMs, std::string* why = nullptr);
    // Ce qui arrive (au plus `size` octets), en attendant timeoutMs au plus : le
    // nombre d'octets lus ; 0 : rien dans le delai ; -1 : ferme par l'autre bout,
    // ou une erreur (why).
    long receiveSome(void* data, std::size_t size, int timeoutMs, std::string* why = nullptr);
    // Une ligne, sans sa fin (\n ou \r\n), au plus maxBytes : SMTP, HTTP.
    bool receiveLine(std::string& line, int timeoutMs, std::size_t maxBytes = 8192, std::string* why = nullptr);
    // Quelque chose a lire (ou la fermeture) dans les timeoutMs.
    [[nodiscard]] bool readable(int timeoutMs) const;
    // Le dernier echec d'envoi ou de reception etait-il un delai depasse sans
    // rien recevoir (la connexion reste utilisable), plutot qu'une coupure ou
    // une reponse tronquee (elle ne l'est plus) ?
    [[nodiscard]] bool timedOut() const noexcept { return timedOut_; }

    [[nodiscard]] int         localPort() const;
    [[nodiscard]] std::string peerName() const;   // "192.168.1.20:50712"
    [[nodiscard]] std::string localName() const;  // lot 15 : "192.168.1.5:50712" (l'espion)
    // Pour attendre plusieurs sockets a la fois (le serveur) : son descripteur.
    [[nodiscard]] std::intptr_t handle() const noexcept { return fd_; }

private:
    static constexpr std::intptr_t kInvalid = -1;
    std::intptr_t fd_{kInvalid};
    std::string   pending_;   // lu au-dela d'une ligne (receiveLine), rendu d'abord
    bool          timedOut_{false};
};

// Les sockets de la liste qui ont quelque chose a lire (ou qui se ferment),
// par leur rang, en attendant timeoutMs au plus. Les invalides sont ignores.
[[nodiscard]] std::vector<std::size_t> waitReadable(const std::vector<const Socket*>& sockets, int timeoutMs);

// Le texte de la derniere erreur du systeme (errno, WSAGetLastError) : en
// francais pour les plus courantes (connexion refusee, hote injoignable...).
[[nodiscard]] std::string lastErrorText();

// L'adresse d'une carte reseau de ce poste, pour dire ou joindre un serveur
// ("192.168.1.30") ; "127.0.0.1" a defaut.
[[nodiscard]] std::string localAddress();

// Base64 (l'authentification SMTP, les en-tetes HTTP).
[[nodiscard]] std::string base64(std::string_view bytes);

} // namespace hmi::net
