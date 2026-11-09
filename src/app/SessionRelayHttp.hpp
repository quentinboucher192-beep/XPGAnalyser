// =============================================================================
//  app/SessionRelayHttp.hpp - 1.44.0 : le transport des sessions par Internet
//  (session::RelayTransport) : le HTTP du service des licences, signe
// -----------------------------------------------------------------------------
//  Chaque appel au relais (/v1/relais/*) porte les quatre en-tetes X-XPG-*
//  (licensing::service::signRequest, le schema des appels signes du service des
//  licences), signes par la cle donnee : celle de ce PC dans IHM Studio
//  (licensing::deviceSignServiceMessage), une cle d'essai dans les essais.
//  L'heure signee : l'heure du systeme corrigee de l'ecart lu sur GET /v1/etat (au
//  premier appel, puis apres un refus 401 << heure >> : l'appel est refait une fois).
//  Le transport du systeme (app::ai : WinHTTP, libcurl), un client par appel :
//  plusieurs fils appellent en meme temps (le lien lit en attente longue pendant
//  qu'il envoie) ; cancel() les arrete tous. Rien n'est ecrit dans un journal.
// =============================================================================
#pragma once

#include "../deploy/Crypto.hpp"
#include "../session/SessionRelay.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace app::sessionrelay {

struct HttpOptions {
    std::string baseUrl;    // l'adresse du service des licences ("https://...", http://127.0.0.1:N dans les essais)
    // La cle publique qui signe ; faux : `why`.
    std::function<bool(deploy::crypto::PublicKey&, std::string&)> publicKey;
    // Signer un message (service::requestMessage) ; faux : `why`.
    std::function<bool(std::string_view, deploy::crypto::Signature&, std::string&)> sign;
    std::string software;   // X-XPG-Logiciel ("IHM-Studio/1.44.0") ; vide : rien
};

// Un transport neuf (un lien : son propre transport, cancel() est definitif).
[[nodiscard]] session::RelayTransportPtr httpTransport(HttpOptions options);

} // namespace app::sessionrelay
