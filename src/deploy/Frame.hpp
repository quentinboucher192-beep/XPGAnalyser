// =============================================================================
//  deploy/Frame.hpp - 1.28.0 : les trames du protocole de deploiement
// -----------------------------------------------------------------------------
//  Une trame, sur la connexion TLS :
//      longueur totale   4 octets, gros-boutiste : ce qui SUIT ce champ
//                        (4 + longueur de l'en-tete + longueur des donnees),
//                        2 Mio au plus (kMaxFrame) en 1.28
//      longueur en-tete  4 octets, gros-boutiste
//      en-tete           du JSON (un objet) : voir deploy/Protocol
//      donnees           le reste (en 1.28 : du JSON, ou rien)
//  Une trame trop grosse n'est pas lue : FrameRead::TooBig, et la connexion se
//  ferme apres une reponse d'erreur (on ne sait plus ou commence la suivante).
// =============================================================================
#pragma once

#include "Tls.hpp"

#include "../core/Json.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace deploy {

inline constexpr std::uint32_t kMaxFrame = 2u * 1024u * 1024u;

struct Frame {
    core::json::Value header;   // un objet
    std::string data;           // les donnees (du JSON en 1.28, parfois vide)
};

enum class FrameRead { Ok, Closed, Idle, Timeout, Cancelled, TooBig, Malformed, Error };

// Les octets d'une trame. Faux (et `why`) si elle depasse kMaxFrame.
bool encodeFrame(const core::json::Value& header, std::string_view data, std::string& out, std::string* why);
// Une trame complete (avec sa longueur totale) en memoire. Faux : `why`.
bool decodeFrame(std::string_view bytes, Frame& out, std::string* why);

// Envoyer une trame en `timeoutMs`.
bool writeFrame(TlsStream& s, const core::json::Value& header, std::string_view data, int timeoutMs, std::string* why);
// Lire une trame : sa longueur arrive en `idleMs` au plus (sinon Idle), le reste en `timeoutMs`.
// `announced` (si donne) : la longueur annoncee, meme refusee (TooBig).
FrameRead readFrame(TlsStream& s, Frame& out, int idleMs, int timeoutMs, std::string* why, std::uint32_t* announced = nullptr);

} // namespace deploy
