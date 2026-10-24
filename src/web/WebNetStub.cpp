// =============================================================================
//  web/WebNetStub.cpp - 1.12.31 : le reseau, cote page web (il n'y en a pas)
// -----------------------------------------------------------------------------
//  Un navigateur ne peut pas ouvrir de socket TCP : hmi/HmiNet.cpp (sockets du
//  systeme) n'est pas compile pour la page. Ce fichier redonne la meme interface
//  (hmi::net), mais sans reseau : un Socket ne se connecte jamais (il le dit), et
//  les quelques fonctions libres gardent un sens (base64, l'adresse locale, une
//  attente). Un equipement n'est donc pas joint depuis la page : il le dit
//  ("pas de socket TCP dans un navigateur").
//
//  Compile seulement pour la page (XPG_WEB) ; l'application garde HmiNet.cpp.
// =============================================================================
#ifdef XPG_WEB

#include "../hmi/HmiNet.hpp"

#include <array>
#include <cstdint>

namespace hmi::net {

Socket::~Socket() {}
Socket::Socket(Socket&& o) noexcept : fd_(o.fd_), pending_(std::move(o.pending_)), timedOut_(o.timedOut_) { o.fd_ = kInvalid; }
Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) { fd_ = o.fd_; pending_ = std::move(o.pending_); timedOut_ = o.timedOut_; o.fd_ = kInvalid; }
    return *this;
}
void Socket::close() noexcept { fd_ = kInvalid; pending_.clear(); }

bool Socket::connect(const std::string&, int, int, std::string* why) {
    if (why) *why = "pas de socket TCP dans un navigateur";
    return false;
}
bool Socket::listen(const std::string&, int, std::string* why) {
    if (why) *why = "pas de serveur dans un navigateur";
    return false;
}
Socket Socket::accept(int, std::string*) { return Socket{}; }
bool Socket::sendAll(const void*, std::size_t, int, std::string* why) {
    if (why) *why = "pas de reseau";
    return false;
}
bool Socket::receiveExact(void*, std::size_t, int, std::string* why) {
    if (why) *why = "pas de reseau";
    return false;
}
long Socket::receiveSome(void*, std::size_t, int, std::string* why) {
    if (why) *why = "pas de reseau";
    return -1;
}
bool Socket::receiveLine(std::string&, int, std::size_t, std::string* why) {
    if (why) *why = "pas de reseau";
    return false;
}
bool Socket::readable(int) const { return false; }
int Socket::localPort() const { return 0; }
std::string Socket::peerName() const { return {}; }
std::string Socket::localName() const { return {}; }

std::vector<std::size_t> waitReadable(const std::vector<const Socket*>&, int) { return {}; }
std::string lastErrorText() { return "pas de reseau"; }
std::string localAddress() { return "127.0.0.1"; }

// La page n'attend pas sur le fil (elle rendrait la main au navigateur) : rien.
void preciseWait(int) {}

// Base64 (les en-tetes HTTP, l'authentification) - le meme que l'application.
std::string base64(std::string_view bytes) {
    static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t n = (std::uint8_t(bytes[i]) << 16) | (std::uint8_t(bytes[i + 1]) << 8) | std::uint8_t(bytes[i + 2]);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += tbl[n & 63];
    }
    if (i < bytes.size()) {
        std::uint32_t n = std::uint8_t(bytes[i]) << 16;
        if (i + 1 < bytes.size()) n |= std::uint8_t(bytes[i + 1]) << 8;
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += (i + 1 < bytes.size()) ? tbl[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

} // namespace hmi::net

#endif // XPG_WEB
