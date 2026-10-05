// hmi/HmiNet.cpp - un socket TCP, Windows (Winsock) et POSIX (lot 14).
#include "HmiNet.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#    undef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "ws2_32.lib")
#  endif
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace hmi::net {

namespace {

#if defined(_WIN32)
using Native = SOCKET;
using SockLen = int;
Native nat(std::intptr_t fd) { return static_cast<SOCKET>(fd); }
bool invalidNative(Native s) { return s == INVALID_SOCKET; }
int  lastError() { return WSAGetLastError(); }
void closeNative(Native s) { ::closesocket(s); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEINTR; }
constexpr int kSendFlags = 0;
#else
using Native = int;
using SockLen = socklen_t;
Native nat(std::intptr_t fd) { return static_cast<int>(fd); }
bool invalidNative(Native s) { return s < 0; }
int  lastError() { return errno; }
void closeNative(Native s) { ::close(s); }
bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS || e == EINTR; }
#  if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#  else
constexpr int kSendFlags = 0;
#  endif
#endif

bool startup() {
#if defined(_WIN32)
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

std::string errorText(int e) {
#if defined(_WIN32)
    switch (e) {
        case WSAECONNREFUSED: return "connexion refus\xC3\xA9" "e (rien n'\xC3\xA9" "coute sur ce port)";
        case WSAETIMEDOUT:    return "pas de r\xC3\xA9ponse (d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9)";
        case WSAEHOSTUNREACH: return "h\xC3\xB4te injoignable";
        case WSAENETUNREACH:  return "r\xC3\xA9seau injoignable";
        case WSAECONNRESET:   return "connexion coup\xC3\xA9" "e par l'autre bout";
        case WSAECONNABORTED: return "connexion interrompue";
        case WSAEADDRINUSE:   return "port d\xC3\xA9j\xC3\xA0 utilis\xC3\xA9";
        case WSAEACCES:       return "acc\xC3\xA8s refus\xC3\xA9 (port r\xC3\xA9serv\xC3\xA9 ?)";
        case WSAENETDOWN:     return "r\xC3\xA9seau hors service";
        case WSAEADDRNOTAVAIL: return "adresse indisponible sur ce poste";
        default:              return "erreur r\xC3\xA9seau " + std::to_string(e);
    }
#else
    switch (e) {
        case ECONNREFUSED: return "connexion refus\xC3\xA9" "e (rien n'\xC3\xA9" "coute sur ce port)";
        case ETIMEDOUT:    return "pas de r\xC3\xA9ponse (d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9)";
        case EHOSTUNREACH: return "h\xC3\xB4te injoignable";
        case ENETUNREACH:  return "r\xC3\xA9seau injoignable";
        case ECONNRESET:   return "connexion coup\xC3\xA9" "e par l'autre bout";
        case ECONNABORTED: return "connexion interrompue";
        case EADDRINUSE:   return "port d\xC3\xA9j\xC3\xA0 utilis\xC3\xA9";
        case EACCES:       return "acc\xC3\xA8s refus\xC3\xA9 (un port sous 1024 demande des droits d'administrateur)";
        case ENETDOWN:     return "r\xC3\xA9seau hors service";
        case EADDRNOTAVAIL: return "adresse indisponible sur ce poste";
        case EPIPE:        return "connexion ferm\xC3\xA9" "e par l'autre bout";
        default:           return std::string(std::strerror(e));
    }
#endif
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

void setNonBlocking(Native s) {
#if defined(_WIN32)
    u_long on = 1;
    (void)ioctlsocket(s, FIONBIO, &on);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

// Les petites trames (Modbus, une requete, une reponse) partent tout de suite :
// sans cela, Nagle et l'acquittement differe ajoutent jusqu'a 200 ms par echange.
void tune(Native s) {
    int one = 1;
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
#if defined(__APPLE__)
    (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

using Clock = std::chrono::steady_clock;

int remainingMs(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<int>(std::min<long long>(left, INT_MAX));
}

// 1 : pret ; 0 : delai ecoule ; -1 : erreur.
int waitFor(Native s, bool forWrite, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
    for (;;) {
        const int left = remainingMs(deadline);
#if defined(_WIN32)
        fd_set set, fails;
        FD_ZERO(&set);
        FD_ZERO(&fails);
        FD_SET(s, &set);
        FD_SET(s, &fails);
        timeval tv{left / 1000, (left % 1000) * 1000};
        const int rc = ::select(0, forWrite ? nullptr : &set, forWrite ? &set : nullptr, &fails, &tv);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        if (WSAGetLastError() == WSAEINTR && left > 0) continue;
        return -1;
#else
        pollfd p{};
        p.fd = s;
        p.events = forWrite ? POLLOUT : POLLIN;
        const int rc = ::poll(&p, 1, left);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        if (errno == EINTR && left > 0) continue;
        return -1;
#endif
    }
}

std::string nameOf(const sockaddr* sa, SockLen len) {
    char host[128] = {0};
    char port[16] = {0};
    if (getnameinfo(sa, len, host, sizeof host, port, sizeof port, NI_NUMERICHOST | NI_NUMERICSERV) != 0) return "?";
    return std::string(host) + ":" + port;
}

} // namespace

// ----------------------------------------------------------------- Socket ----
Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_), pending_(std::move(other.pending_)), timedOut_(other.timedOut_) {
    other.fd_ = kInvalid;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        pending_ = std::move(other.pending_);
        timedOut_ = other.timedOut_;
        other.fd_ = kInvalid;
    }
    return *this;
}

void Socket::close() noexcept {
    if (fd_ != kInvalid) closeNative(nat(fd_));
    fd_ = kInvalid;
    pending_.clear();
}

bool Socket::connect(const std::string& host, int port, int timeoutMs, std::string* why) {
    close();
    if (!startup()) return fail(why, "r\xC3\xA9seau indisponible (Winsock ne d\xC3\xA9marre pas)");
    if (host.empty()) return fail(why, "adresse vide");
    if (port <= 0 || port > 65535) return fail(why, "port " + std::to_string(port) + " hors de 1 \xC3\xA0 65535");
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || !found)
        return fail(why, "adresse inconnue : " + host);
    std::string last = "aucune adresse utilisable pour " + host;
    for (addrinfo* ai = found; ai; ai = ai->ai_next) {
        const Native s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (invalidNative(s)) {
            last = errorText(lastError());
            continue;
        }
        setNonBlocking(s);
        tune(s);
        if (::connect(s, ai->ai_addr, static_cast<SockLen>(ai->ai_addrlen)) != 0) {
            const int e = lastError();
            if (!wouldBlock(e)) {
                last = errorText(e);
                closeNative(s);
                continue;
            }
            const int w = waitFor(s, true, timeoutMs);
            if (w <= 0) {
                last = w == 0 ? "pas de r\xC3\xA9ponse en " + std::to_string(timeoutMs) + " ms" : errorText(lastError());
                closeNative(s);
                continue;
            }
            int err = 0;
            SockLen len = sizeof err;
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0) err = lastError();
            if (err != 0) {
                last = errorText(err);
                closeNative(s);
                continue;
            }
        }
        freeaddrinfo(found);
        fd_ = static_cast<std::intptr_t>(s);
        return true;
    }
    freeaddrinfo(found);
    return fail(why, last);
}

bool Socket::listen(const std::string& bindAddress, int port, std::string* why) {
    close();
    if (!startup()) return fail(why, "r\xC3\xA9seau indisponible (Winsock ne d\xC3\xA9marre pas)");
    if (port < 0 || port > 65535) return fail(why, "port " + std::to_string(port) + " hors de 0 \xC3\xA0 65535");
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    const char* node = bindAddress.empty() || bindAddress == "0.0.0.0" ? nullptr : bindAddress.c_str();
    if (getaddrinfo(node, service.c_str(), &hints, &found) != 0 || !found)
        return fail(why, "adresse d'\xC3\xA9" "coute inconnue : " + bindAddress);
    const Native s = ::socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (invalidNative(s)) {
        freeaddrinfo(found);
        return fail(why, errorText(lastError()));
    }
#if defined(_WIN32)
    BOOL exclusive = TRUE;
    (void)setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
#else
    int one = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    if (::bind(s, found->ai_addr, static_cast<SockLen>(found->ai_addrlen)) != 0) {
        const int e = lastError();
        freeaddrinfo(found);
        closeNative(s);
        return fail(why, "port " + std::to_string(port) + " : " + errorText(e));
    }
    freeaddrinfo(found);
    if (::listen(s, 16) != 0) {
        const int e = lastError();
        closeNative(s);
        return fail(why, errorText(e));
    }
    setNonBlocking(s);
    fd_ = static_cast<std::intptr_t>(s);
    return true;
}

Socket Socket::accept(int timeoutMs, std::string* peer) {
    Socket out;
    if (!valid() || waitFor(nat(fd_), false, timeoutMs) <= 0) return out;
    sockaddr_storage from{};
    SockLen len = sizeof from;
    const Native c = ::accept(nat(fd_), reinterpret_cast<sockaddr*>(&from), &len);
    if (invalidNative(c)) return out;
    setNonBlocking(c);
    tune(c);
    out.fd_ = static_cast<std::intptr_t>(c);
    if (peer) *peer = nameOf(reinterpret_cast<const sockaddr*>(&from), len);
    return out;
}

bool Socket::sendAll(const void* data, std::size_t size, int timeoutMs, std::string* why) {
    timedOut_ = false;
    if (!valid()) return fail(why, "pas de connexion");
    const char* p = static_cast<const char*>(data);
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
    while (size > 0) {
        const int w = waitFor(nat(fd_), true, remainingMs(deadline));
        if (w == 0) {
            timedOut_ = true;
            return fail(why, "envoi impossible en " + std::to_string(timeoutMs) + " ms");
        }
        if (w < 0) return fail(why, errorText(lastError()));
        const int chunk = static_cast<int>(std::min<std::size_t>(size, 1u << 20));
        const auto n = ::send(nat(fd_), p, chunk, kSendFlags);
        if (n < 0) {
            const int e = lastError();
            if (wouldBlock(e)) continue;
            return fail(why, errorText(e));
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

long Socket::receiveSome(void* data, std::size_t size, int timeoutMs, std::string* why) {
    if (size == 0) return 0;
    if (!pending_.empty()) {
        const std::size_t n = std::min(size, pending_.size());
        std::memcpy(data, pending_.data(), n);
        pending_.erase(0, n);
        return static_cast<long>(n);
    }
    if (!valid()) {
        (void)fail(why, "pas de connexion");
        return -1;
    }
    const int w = waitFor(nat(fd_), false, timeoutMs);
    if (w == 0) return 0;
    if (w < 0) {
        (void)fail(why, errorText(lastError()));
        return -1;
    }
    const int chunk = static_cast<int>(std::min<std::size_t>(size, 1u << 20));
    const auto n = ::recv(nat(fd_), static_cast<char*>(data), chunk, 0);
    if (n == 0) {
        (void)fail(why, "connexion ferm\xC3\xA9" "e par l'autre bout");
        return -1;
    }
    if (n < 0) {
        const int e = lastError();
        if (wouldBlock(e)) return 0;
        (void)fail(why, errorText(e));
        return -1;
    }
    return static_cast<long>(n);
}

bool Socket::receiveExact(void* data, std::size_t size, int timeoutMs, std::string* why) {
    timedOut_ = false;
    char* p = static_cast<char*>(data);
    std::size_t got = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
    while (got < size) {
        const int left = remainingMs(deadline);
        const long n = receiveSome(p + got, size - got, left, why);
        if (n < 0) return false;
        if (n == 0 && left <= 0) {
            timedOut_ = got == 0;
            return fail(why, got == 0 ? "pas de r\xC3\xA9ponse en " + std::to_string(timeoutMs) + " ms"
                                      : "r\xC3\xA9ponse incompl\xC3\xA8te en " + std::to_string(timeoutMs) + " ms");
        }
        got += static_cast<std::size_t>(n);
    }
    return true;
}

bool Socket::receiveLine(std::string& line, int timeoutMs, std::size_t maxBytes, std::string* why) {
    timedOut_ = false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
    for (;;) {
        if (const auto at = pending_.find('\n'); at != std::string::npos) {
            line = pending_.substr(0, at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending_.erase(0, at + 1);
            return true;
        }
        if (pending_.size() > maxBytes) return fail(why, "ligne trop longue");
        if (!valid()) return fail(why, "pas de connexion");
        const int left = remainingMs(deadline);
        const int w = waitFor(nat(fd_), false, left);
        if (w == 0) {
            timedOut_ = pending_.empty();
            return fail(why, "pas de r\xC3\xA9ponse en " + std::to_string(timeoutMs) + " ms");
        }
        if (w < 0) return fail(why, errorText(lastError()));
        char buffer[2048];
        const auto n = ::recv(nat(fd_), buffer, static_cast<int>(sizeof buffer), 0);
        if (n == 0) return fail(why, "connexion ferm\xC3\xA9" "e par l'autre bout");
        if (n < 0) {
            const int e = lastError();
            if (wouldBlock(e)) continue;
            return fail(why, errorText(e));
        }
        pending_.append(buffer, static_cast<std::size_t>(n));
    }
}

bool Socket::readable(int timeoutMs) const {
    if (!pending_.empty()) return true;
    return valid() && waitFor(nat(fd_), false, timeoutMs) > 0;
}

int Socket::localPort() const {
    if (!valid()) return 0;
    sockaddr_storage me{};
    SockLen len = sizeof me;
    if (getsockname(nat(fd_), reinterpret_cast<sockaddr*>(&me), &len) != 0) return 0;
    if (me.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&me)->sin_port);
    if (me.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&me)->sin6_port);
    return 0;
}

std::string Socket::peerName() const {
    if (!valid()) return {};
    sockaddr_storage peer{};
    SockLen len = sizeof peer;
    if (getpeername(nat(fd_), reinterpret_cast<sockaddr*>(&peer), &len) != 0) return {};
    return nameOf(reinterpret_cast<const sockaddr*>(&peer), len);
}

std::string Socket::localName() const {
    if (!valid()) return {};
    sockaddr_storage me{};
    SockLen len = sizeof me;
    if (getsockname(nat(fd_), reinterpret_cast<sockaddr*>(&me), &len) != 0) return {};
    return nameOf(reinterpret_cast<const sockaddr*>(&me), len);
}

// --------------------------------------------------------------- fonctions ---
std::vector<std::size_t> waitReadable(const std::vector<const Socket*>& sockets, int timeoutMs) {
    std::vector<std::size_t> ready;
    std::vector<std::size_t> rank;
#if defined(_WIN32)
    fd_set set;
    FD_ZERO(&set);
    for (std::size_t i = 0; i < sockets.size(); ++i)
        if (sockets[i] && sockets[i]->valid() && rank.size() < FD_SETSIZE) {
            FD_SET(nat(sockets[i]->handle()), &set);
            rank.push_back(i);
        }
    if (rank.empty()) return ready;
    const int ms = std::max(0, timeoutMs);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    if (::select(0, &set, nullptr, nullptr, &tv) <= 0) return ready;
    for (const auto i : rank)
        if (FD_ISSET(nat(sockets[i]->handle()), &set)) ready.push_back(i);
#else
    std::vector<pollfd> fds;
    for (std::size_t i = 0; i < sockets.size(); ++i)
        if (sockets[i] && sockets[i]->valid()) {
            pollfd p{};
            p.fd = nat(sockets[i]->handle());
            p.events = POLLIN;
            fds.push_back(p);
            rank.push_back(i);
        }
    if (fds.empty()) return ready;
    if (::poll(fds.data(), static_cast<nfds_t>(fds.size()), std::max(0, timeoutMs)) <= 0) return ready;
    for (std::size_t k = 0; k < fds.size(); ++k)
        if (fds[k].revents & (POLLIN | POLLHUP | POLLERR)) ready.push_back(rank[k]);
#endif
    return ready;
}

std::string lastErrorText() { return errorText(lastError()); }

std::string localAddress() {
    if (!startup()) return "127.0.0.1";
    // Une "connexion" UDP n'envoie rien : elle choisit seulement la carte de la
    // route par defaut, dont on lit l'adresse.
    const Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (invalidNative(s)) return "127.0.0.1";
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    (void)inet_pton(AF_INET, "192.0.2.1", &to.sin_addr);   // TEST-NET-1 : jamais joint
    std::string out = "127.0.0.1";
    if (::connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof to) == 0) {
        sockaddr_in me{};
        SockLen len = sizeof me;
        char text[64] = {0};
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0 && inet_ntop(AF_INET, &me.sin_addr, text, sizeof text))
            out = text;
        if (out == "0.0.0.0") out = "127.0.0.1";
    }
    closeNative(s);
    return out;
}

std::string base64(std::string_view bytes) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < bytes.size()) {
        const unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8)
                           | static_cast<unsigned char>(bytes[i + 2]);
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += kTable[(v >> 6) & 63];
        out += kTable[v & 63];
        i += 3;
    }
    if (i + 1 == bytes.size()) {
        const unsigned v = static_cast<unsigned char>(bytes[i]) << 16;
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += kTable[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

} // namespace hmi::net
