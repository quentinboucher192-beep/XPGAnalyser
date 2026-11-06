// =============================================================================
//  deploy/Net.cpp - 1.28.0 : les sockets du deploiement (voir Net.hpp)
// =============================================================================
#include "Net.hpp"

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
#  include <windows.h>
#  include <iphlpapi.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "ws2_32.lib")
#    pragma comment(lib, "iphlpapi.lib")
#  endif
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <ifaddrs.h>
#  include <net/if.h>
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
#include <utility>

namespace deploy::net {

namespace {

#if defined(_WIN32)
using Native = SOCKET;
using SockLen = int;
Native nat(std::intptr_t fd) { return static_cast<SOCKET>(fd); }
bool invalidNative(Native s) { return s == INVALID_SOCKET; }
void closeNative(Native s) { ::closesocket(s); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEINTR; }
constexpr int kSendFlags = 0;
#else
using Native = int;
using SockLen = socklen_t;
Native nat(std::intptr_t fd) { return static_cast<int>(fd); }
bool invalidNative(Native s) { return s < 0; }
void closeNative(Native s) { ::close(s); }
bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS || e == EINTR; }
#  if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#  else
constexpr int kSendFlags = 0;
#  endif
#endif

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

// Les trames partent tout de suite (sans Nagle : une demande, une reponse).
void tune(Native s) {
    int one = 1;
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
#if defined(__APPLE__)
    (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

// Personne d'autre sur ce port : sous Windows, SO_EXCLUSIVEADDRUSE (sans lui, un autre programme
// pourrait s'y lier aussi) ; ailleurs, SO_REUSEADDR pour TCP seulement (relancer l'agent juste
// apres l'avoir arrete), qui ne permet pas d'ecouter sur un port deja ecoute.
void exclusive(Native s, bool tcp) {
#if defined(_WIN32)
    (void)tcp;
    BOOL on = TRUE;
    (void)setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof on);
#else
    if (tcp) {
        int one = 1;
        (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
#endif
}

int waitNative(Native s, bool forWrite, int timeoutMs) {
    const int left = std::max(0, timeoutMs);
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
    return WSAGetLastError() == WSAEINTR ? 0 : -1;
#else
    pollfd p{};
    p.fd = s;
    p.events = forWrite ? POLLOUT : POLLIN;
    const int rc = ::poll(&p, 1, left);
    if (rc > 0) return 1;
    if (rc == 0) return 0;
    return errno == EINTR ? 0 : -1;
#endif
}

int portOf(const sockaddr_storage& ss) {
    if (ss.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&ss)->sin_port);
    if (ss.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_port);
    return 0;
}

std::string hostOf(const sockaddr* sa, SockLen len) {
    char host[128] = {0};
    if (getnameinfo(sa, len, host, sizeof host, nullptr, 0, NI_NUMERICHOST) != 0) return {};
    std::string h(host);
    if (h.rfind("::ffff:", 0) == 0) h = h.substr(7);   // une adresse IPv4 vue en IPv6
    return h;
}

int localPortOf(Native s) {
    sockaddr_storage ss{};
    SockLen len = sizeof ss;
    if (getsockname(s, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return 0;
    return portOf(ss);
}

bool ipv4(const std::string& text, sockaddr_in& out, int port) {
    std::memset(&out, 0, sizeof out);
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<unsigned short>(port));
    if (text.empty() || text == "0.0.0.0") {
        out.sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }
    return inet_pton(AF_INET, text.c_str(), &out.sin_addr) == 1;
}

} // namespace

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

int lastError() {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
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
        case WSAEADDRINUSE:   return "port d\xC3\xA9j\xC3\xA0 pris par un autre programme";
        case WSAEACCES:       return "port d\xC3\xA9j\xC3\xA0 pris par un autre programme, ou r\xC3\xA9serv\xC3\xA9 par Windows (acc\xC3\xA8s refus\xC3\xA9)";
        case WSAENETDOWN:     return "r\xC3\xA9seau hors service";
        case WSAEADDRNOTAVAIL: return "adresse indisponible sur ce PC";
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
        case EADDRINUSE:   return "port d\xC3\xA9j\xC3\xA0 pris par un autre programme";
        case EACCES:       return "acc\xC3\xA8s refus\xC3\xA9 (un port sous 1024 demande des droits d'administrateur)";
        case ENETDOWN:     return "r\xC3\xA9seau hors service";
        case EADDRNOTAVAIL: return "adresse indisponible sur ce PC";
        case EPIPE:        return "connexion ferm\xC3\xA9" "e par l'autre bout";
        default:           return std::string(std::strerror(e));
    }
#endif
}

// ================================================================ TcpSocket ===
TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = kInvalid; }

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = kInvalid;
    }
    return *this;
}

void TcpSocket::close() noexcept {
    if (fd_ != kInvalid) closeNative(nat(fd_));
    fd_ = kInvalid;
}

bool TcpSocket::connect(const std::string& host, int port, int timeoutMs, std::string* why) {
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
            const int w = waitNative(s, true, timeoutMs);
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
    return fail(why, host + ":" + service + " : " + last);
}

bool TcpSocket::listen(const std::string& bindAddress, int port, std::string* why) {
    close();
    if (!startup()) return fail(why, "r\xC3\xA9seau indisponible (Winsock ne d\xC3\xA9marre pas)");
    if (port < 0 || port > 65535) return fail(why, "port " + std::to_string(port) + " hors de 0 \xC3\xA0 65535");
    sockaddr_in addr{};
    if (!ipv4(bindAddress, addr, port)) return fail(why, "adresse d'\xC3\xA9" "coute invalide : " + bindAddress);
    const Native s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (invalidNative(s)) return fail(why, errorText(lastError()));
    exclusive(s, true);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        const int e = lastError();
        closeNative(s);
        return fail(why, "port TCP " + std::to_string(port) + " : " + errorText(e));
    }
    if (::listen(s, 16) != 0) {
        const int e = lastError();
        closeNative(s);
        return fail(why, "port TCP " + std::to_string(port) + " : " + errorText(e));
    }
    setNonBlocking(s);
    fd_ = static_cast<std::intptr_t>(s);
    return true;
}

TcpSocket TcpSocket::accept(int timeoutMs, std::string* peer) {
    TcpSocket out;
    if (!valid() || waitNative(nat(fd_), false, timeoutMs) <= 0) return out;
    sockaddr_storage from{};
    SockLen len = sizeof from;
    const Native c = ::accept(nat(fd_), reinterpret_cast<sockaddr*>(&from), &len);
    if (invalidNative(c)) return out;
    setNonBlocking(c);
    tune(c);
    out.fd_ = static_cast<std::intptr_t>(c);
    if (peer) *peer = hostOf(reinterpret_cast<const sockaddr*>(&from), len) + ":" + std::to_string(portOf(from));
    return out;
}

long TcpSocket::sendSome(const void* data, std::size_t size) {
    if (!valid()) return -1;
    const int chunk = static_cast<int>(std::min<std::size_t>(size, 1u << 20));
    const auto n = ::send(nat(fd_), static_cast<const char*>(data), chunk, kSendFlags);
    if (n >= 0) return static_cast<long>(n);
    return wouldBlock(lastError()) ? 0 : -1;
}

long TcpSocket::receiveSome(void* data, std::size_t size) {
    if (!valid()) return -1;
    const int chunk = static_cast<int>(std::min<std::size_t>(size, 1u << 20));
    const auto n = ::recv(nat(fd_), static_cast<char*>(data), chunk, 0);
    if (n > 0) return static_cast<long>(n);
    if (n == 0) return 0;
    return wouldBlock(lastError()) ? -2 : -1;
}

int TcpSocket::wait(bool forWrite, int timeoutMs) const {
    if (!valid()) return -1;
    return waitNative(nat(fd_), forWrite, timeoutMs);
}

int TcpSocket::localPort() const { return valid() ? localPortOf(nat(fd_)) : 0; }

std::string TcpSocket::peerAddress() const {
    if (!valid()) return {};
    sockaddr_storage ss{};
    SockLen len = sizeof ss;
    if (getpeername(nat(fd_), reinterpret_cast<sockaddr*>(&ss), &len) != 0) return {};
    return hostOf(reinterpret_cast<const sockaddr*>(&ss), len);
}

// ================================================================ UdpSocket ===
UdpSocket::~UdpSocket() { close(); }

void UdpSocket::close() noexcept {
    if (fd_ != kInvalid) closeNative(nat(fd_));
    fd_ = kInvalid;
}

bool UdpSocket::bind(const std::string& bindAddress, int port, std::string* why) {
    close();
    if (!startup()) return fail(why, "r\xC3\xA9seau indisponible (Winsock ne d\xC3\xA9marre pas)");
    if (port < 0 || port > 65535) return fail(why, "port " + std::to_string(port) + " hors de 0 \xC3\xA0 65535");
    sockaddr_in addr{};
    if (!ipv4(bindAddress, addr, port)) return fail(why, "adresse invalide : " + bindAddress);
    const Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (invalidNative(s)) return fail(why, errorText(lastError()));
    exclusive(s, false);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        const int e = lastError();
        closeNative(s);
        return fail(why, "port UDP " + std::to_string(port) + " : " + errorText(e));
    }
    setNonBlocking(s);
    fd_ = static_cast<std::intptr_t>(s);
    return true;
}

bool UdpSocket::enableBroadcast() {
    if (!valid()) return false;
    int one = 1;
    return setsockopt(nat(fd_), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one) == 0;
}

bool UdpSocket::sendTo(const std::string& ip, int port, std::string_view data, std::string* why) {
    if (!valid()) return fail(why, "pas de socket");
    sockaddr_in to{};
    if (ip.empty() || !ipv4(ip, to, port)) return fail(why, "adresse IPv4 invalide : " + ip);
    const auto n = ::sendto(nat(fd_), data.data(), static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
    if (n < 0) return fail(why, ip + " : " + errorText(lastError()));
    return true;
}

int UdpSocket::receive(Datagram& out, int timeoutMs) {
    if (!valid()) return -1;
    const int w = waitNative(nat(fd_), false, timeoutMs);
    if (w <= 0) return w;
    char buf[2048];
    sockaddr_storage from{};
    SockLen len = sizeof from;
    const auto n = ::recvfrom(nat(fd_), buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &len);
    if (n < 0) {
        const int e = lastError();
#if defined(_WIN32)
        // Windows signale ici le << port injoignable >> d'un envoi precedent : sans importance.
        if (e == WSAECONNRESET || e == WSAEMSGSIZE) return 0;
#endif
        return wouldBlock(e) ? 0 : -1;
    }
    out.data.assign(buf, static_cast<std::size_t>(n));
    out.from = hostOf(reinterpret_cast<const sockaddr*>(&from), len);
    out.fromPort = portOf(from);
    return 1;
}

int UdpSocket::localPort() const { return valid() ? localPortOf(nat(fd_)) : 0; }

// ============================================================== interfaces ===
std::vector<std::string> broadcastAddresses() {
    std::vector<std::string> out;
    const auto add = [&out](std::uint32_t hostOrder) {
        in_addr a{};
        a.s_addr = htonl(hostOrder);
        char text[INET_ADDRSTRLEN] = {0};
        if (!inet_ntop(AF_INET, &a, text, sizeof text)) return;
        if (std::find(out.begin(), out.end(), text) == out.end()) out.emplace_back(text);
    };
#if defined(_WIN32)
    if (!startup()) return out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != NO_ERROR) return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->OperStatus != IfOperStatusUp) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const std::uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            const unsigned prefix = u->OnLinkPrefixLength;
            if (prefix == 0 || prefix >= 31) continue;
            const std::uint32_t mask = 0xFFFFFFFFu << (32 - prefix);
            add((ip & mask) | ~mask);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return out;
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if ((i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0) continue;
        if ((i->ifa_flags & IFF_BROADCAST) != 0 && i->ifa_broadaddr && i->ifa_broadaddr->sa_family == AF_INET) {
            add(ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_broadaddr)->sin_addr.s_addr));
        } else if (i->ifa_netmask) {
            const std::uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr);
            const std::uint32_t mask = ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_netmask)->sin_addr.s_addr);
            if (mask != 0 && mask < 0xFFFFFFFEu) add((ip & mask) | ~mask);
        }
    }
    ::freeifaddrs(list);
#endif
    return out;
}

std::string hostName() {
#if defined(_WIN32)
    wchar_t b[256];
    DWORD n = 256;
    if (GetComputerNameW(b, &n) && n > 0) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, b, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 0 ? len : 0), '\0');
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, b, static_cast<int>(n), s.data(), len, nullptr, nullptr);
        if (!s.empty()) return s;
    }
    return "PC";
#else
    char b[256] = {0};
    if (::gethostname(b, sizeof b - 1) == 0 && b[0]) return b;
    return "PC";
#endif
}

} // namespace deploy::net
