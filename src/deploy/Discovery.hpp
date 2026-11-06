// =============================================================================
//  deploy/Discovery.hpp - 1.28.0 : trouver les agents du reseau (UDP)
// -----------------------------------------------------------------------------
//  Le Studio envoie {"xpgd":1,"op":"DISCOVER","nonce":"..."} en diffusion
//  (255.255.255.255 et l'adresse de diffusion de chaque interface IPv4) et en
//  direct aux adresses donnees ; chaque agent repond a l'envoyeur seul :
//  {"xpgd":1,"op":"HERE","nonce":"...","machine":"PC-ATELIER-1","id":"M-7F21",
//   "version":"1.0.0","protos":[1],"port":47861,"fp":"<empreinte>"}.
//  Une reponse ne prouve rien : c'est une adresse, tant que la machine n'est pas
//  appairee (l'empreinte annoncee n'est jamais epinglee telle quelle).
//  L'agent limite son debit de reponse (RateLimiter).
// =============================================================================
#pragma once

#include "Protocol.hpp"

#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace deploy::discovery {

// Ce qu'un agent annonce.
struct Announce {
    std::string machine, id, version, fingerprint;
    std::vector<int> protos;
    int port{protocol::kAgentPort};
};

[[nodiscard]] std::string discoverMessage(const std::string& nonce);
bool parseDiscover(std::string_view text, std::string& nonce);
[[nodiscard]] std::string hereMessage(const std::string& nonce, const Announce& a);
bool parseHere(std::string_view text, std::string& nonce, Announce& a);

// Un agent trouve : l'adresse est celle d'ou vient la reponse.
struct Found {
    std::string address;
    int port{protocol::kAgentPort};   // son port TCP
    std::string machine, id, version, fingerprint;
    std::vector<int> protos;
};

struct Options {
    int port{protocol::kDiscoveryPort};   // le port UDP des agents
    int waitMs{1500};                     // le temps de collecte
    bool broadcast{true};                 // en diffusion (sinon : seulement `direct`)
    std::vector<std::string> direct;      // des adresses IPv4 interrogees en direct
};

// Bloquant (`waitMs`) : les agents qui ont repondu, sans doublon (une meme empreinte une seule fois).
// Faux dans `why` seulement si rien n'a pu partir ; une liste vide n'est pas une erreur.
std::vector<Found> discover(const Options& options, std::string* why = nullptr);

// Le debit de reponse de l'agent : 10 par seconde en tout (20 d'avance au plus), 5 par seconde
// pour un meme envoyeur, une seule reponse par (envoyeur, nonce).
class RateLimiter {
public:
    bool allow(const std::string& sender, const std::string& nonce, std::chrono::steady_clock::time_point now);

private:
    struct Seen {
        std::chrono::steady_clock::time_point windowStart{};
        int count{0};
    };
    double tokens_{20.0};
    std::chrono::steady_clock::time_point last_{};
    bool started_{false};
    std::map<std::string, Seen> senders_;
    std::map<std::string, std::chrono::steady_clock::time_point> nonces_;
};

} // namespace deploy::discovery
