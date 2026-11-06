// =============================================================================
//  deploy/Tls.hpp - 1.28.0 : une connexion TLS 1.3 (mbedTLS), certificats des deux cotes
// -----------------------------------------------------------------------------
//  TLS 1.3 seulement (min = max = 1.3), echange ephemere (ECDHE), le client et le
//  serveur presentent chacun leur certificat (deploy/TlsIdentity). La verification
//  par une autorite est remplacee par l'EPINGLAGE : la poignee de main accepte un
//  certificat auto-signe (la possession de sa cle est prouvee par TLS 1.3), puis
//  peerFingerprint() rend l'empreinte du pair, que l'appelant compare a celle qu'il
//  a gardee (deploy/Client, deploy/Agent).
//
//  FILS. La configuration de mbedTLS est celle livree (sans MBEDTLS_THREADING_C) :
//  PSA garde un etat global (cles, generateur). Tout appel a mbedTLS passe donc sous
//  un verrou unique (libraryLock) ; les attentes sur la socket se font HORS du
//  verrou (les entrees-sorties sont non bloquantes), si bien que plusieurs
//  connexions avancent ensemble. psa_crypto_init est appele une fois (ready()).
//
//  Chaque lecture et chaque ecriture a son delai ; setCancel() interrompt une
//  attente (arret de l'agent) en 100 ms au plus.
// =============================================================================
#pragma once

#include "Crypto.hpp"
#include "Net.hpp"
#include "TlsIdentity.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace deploy::tls {

// Le verrou de toute la bibliotheque mbedTLS (voir l'en-tete).
std::mutex& libraryLock();
// PSA initialise (une fois, sans danger entre fils) et libsodium pret. Faux : `why`.
bool ready(std::string* why);
// Le hasard pour mbedTLS : celui du systeme, par libsodium.
int randomCallback(void* context, unsigned char* out, std::size_t size);
// Le texte d'un code d'erreur de mbedTLS ("-0x7780 SSL - A fatal alert message was received").
[[nodiscard]] std::string errorText(int code);

} // namespace deploy::tls

namespace deploy {

class TlsStream {
public:
    enum class Io { Ok, Closed, Timeout, Cancelled, Error };

    TlsStream();
    ~TlsStream();
    TlsStream(TlsStream&&) noexcept;
    TlsStream& operator=(TlsStream&&) noexcept;
    TlsStream(const TlsStream&) = delete;
    TlsStream& operator=(const TlsStream&) = delete;

    // Le cote client : TCP puis la poignee de main, le tout en `timeoutMs`.
    bool connect(const std::string& host, int port, const TlsIdentity& me, int timeoutMs, std::string* why);
    // Le cote serveur, sur une connexion acceptee.
    bool accept(net::TcpSocket&& socket, const TlsIdentity& me, int timeoutMs, std::string* why);

    // Ecrire tout / lire exactement `size` octets, en `timeoutMs` au plus.
    Io write(const void* data, std::size_t size, int timeoutMs, std::string* why);
    Io read(void* data, std::size_t size, int timeoutMs, std::string* why);

    // Un drapeau qui, mis a vrai, interrompt les attentes (Io::Cancelled).
    void setCancel(const std::atomic<bool>* flag) noexcept;
    // Fermer (close_notify, puis la socket).
    void close() noexcept;

    [[nodiscard]] bool open() const noexcept;
    // L'empreinte SHA-256 du certificat du pair (apres la poignee de main).
    [[nodiscard]] const crypto::Digest& peerFingerprint() const noexcept;
    [[nodiscard]] std::string peerFingerprintHex() const;
    [[nodiscard]] std::string peerAddress() const;
    // "TLSv1.3" et la suite ("TLS1-3-AES-128-GCM-SHA256"), pour le journal.
    [[nodiscard]] std::string protocolName() const;
    [[nodiscard]] std::string cipherName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace deploy
