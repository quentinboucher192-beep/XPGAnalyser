// hmi/HmiNetInfo.cpp - le reseau du PC (lot 15) : les ports, le ping, l'adresse
// d'un port. Le seul fichier du lot 15 a parler au systeme pour le reseau.
#include "HmiNetInfo.hpp"

#include "HmiEquipment.hpp"
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
#  include <windows.h>
#  include <iphlpapi.h>
#  include <icmpapi.h>
#  include <shellapi.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "iphlpapi.lib")
#    pragma comment(lib, "ws2_32.lib")
#    pragma comment(lib, "shell32.lib")
#    pragma comment(lib, "advapi32.lib")
#  endif
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <dirent.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/ip.h>
#  include <netinet/ip_icmp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace hmi::netinfo {

namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#if defined(_WIN32)
std::string macText(const unsigned char* b, std::size_t n) {
    if (n == 0) return {};
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += '-';
        out += hex[(b[i] >> 4) & 15];
        out += hex[b[i] & 15];
    }
    return out;
}
#endif

bool resolveIpv4(const std::string& host, std::uint32_t& out) {
    if (equip::parseIpv4(host, out)) return true;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    const auto* sin = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
    out = ntohl(sin->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}


#if defined(_WIN32)
std::string narrow(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
    return out;
}
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    return out;
}
struct WsaInit {
    WsaInit() {
        WSADATA d;
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }
    ~WsaInit() {
        if (ok) WSACleanup();
    }
    bool ok{false};
};
#else
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}
// La sortie d'une commande (et son code de retour).
std::string run(const std::string& command, int* code = nullptr) {
    std::string out;
    FILE* p = ::popen((command + " 2>&1").c_str(), "r");
    if (!p) {
        if (code) *code = -1;
        return {};
    }
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    const int rc = ::pclose(p);
    if (code) *code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    return out;
}
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}
#endif

} // namespace

std::string Adapter::speedText() const {
    if (speedBps == 0) return {};
    if (speedBps >= 1000000000ULL) {
        const double g = static_cast<double>(speedBps) / 1e9;
        char b[32];
        std::snprintf(b, sizeof b, g == static_cast<double>(static_cast<long long>(g)) ? "%.0f Gb/s" : "%.1f Gb/s", g);
        return b;
    }
    return std::to_string(speedBps / 1000000ULL) + " Mb/s";
}

bool Adapter::automaticAddress() const noexcept {
    const std::uint32_t a = ip();
    return (a >> 16) == ((169u << 8) | 254u);
}

// ================================================================ les ports ===
#if defined(_WIN32)
std::vector<Adapter> adapters() {
    WsaInit wsa;
    std::vector<Adapter> out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != NO_ERROR) return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        Adapter ad;
        ad.name = narrow(a->FriendlyName);
        ad.index = std::to_string(a->IfIndex);
        ad.description = narrow(a->Description);
        ad.mac = macText(a->PhysicalAddress, a->PhysicalAddressLength);
        ad.kind = a->IfType == IF_TYPE_ETHERNET_CSMACD ? Adapter::Kind::Ethernet
                : a->IfType == IF_TYPE_IEEE80211       ? Adapter::Kind::WiFi
                                                       : Adapter::Kind::Other;
        ad.up = a->OperStatus == IfOperStatusUp;
        ad.speedBps = a->TransmitLinkSpeed == static_cast<ULONG64>(-1) ? 0 : a->TransmitLinkSpeed;
        ad.dhcp = (a->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;
        ad.dhcpKnown = true;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            ad.addresses.emplace_back(ntohl(sin->sin_addr.s_addr), static_cast<int>(u->OnLinkPrefixLength));
        }
        for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
            if (!g->Address.lpSockaddr || g->Address.lpSockaddr->sa_family != AF_INET) continue;
            ad.gateway = ntohl(reinterpret_cast<const sockaddr_in*>(g->Address.lpSockaddr)->sin_addr.s_addr);
            break;
        }
        for (auto* d = a->FirstDnsServerAddress; d; d = d->Next) {
            if (!d->Address.lpSockaddr || d->Address.lpSockaddr->sa_family != AF_INET) continue;
            ad.dns.push_back(ntohl(reinterpret_cast<const sockaddr_in*>(d->Address.lpSockaddr)->sin_addr.s_addr));
        }
        // Les pseudo-cartes (tunnels, Bluetooth, Hyper-V sans adresse) : seulement
        // l'Ethernet et le Wi-Fi, ou une carte qui a une adresse.
        if (ad.kind == Adapter::Kind::Other && ad.addresses.empty()) continue;
        out.push_back(std::move(ad));
    }
    std::stable_sort(out.begin(), out.end(), [](const Adapter& x, const Adapter& y) { return x.kind < y.kind; });
    return out;
}

std::string computerName() {
    wchar_t b[256];
    DWORD n = 256;
    if (!GetComputerNameW(b, &n)) return "ce PC";
    return narrow(b);
}

std::string systemName() {
    HKEY key = nullptr;
    std::string out = "Windows";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t b[128];
        DWORD size = sizeof b;
        DWORD type = 0;
        if (RegQueryValueExW(key, L"ProductName", nullptr, &type, reinterpret_cast<LPBYTE>(b), &size) == ERROR_SUCCESS && type == REG_SZ) {
            b[127] = 0;
            out = narrow(b);
            // Windows 11 se dit encore "Windows 10" dans ProductName : le numero de build tranche.
            wchar_t build[32];
            DWORD bs = sizeof build;
            if (RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, &type, reinterpret_cast<LPBYTE>(build), &bs) == ERROR_SUCCESS) {
                build[31] = 0;
                const int n = _wtoi(build);
                const auto at = out.find("Windows 10");
                if (n >= 22000 && at != std::string::npos) out.replace(at, 10, "Windows 11");
            }
        }
        RegCloseKey(key);
    }
    return out;
}

bool isAdministrator() {
    BOOL member = FALSE;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) {
        if (!CheckTokenMembership(nullptr, admins, &member)) member = FALSE;
        FreeSid(admins);
    }
    return member != FALSE;
}

PingResult ping(const std::string& host, int timeoutMs) {
    WsaInit wsa;
    PingResult r;
    r.method = "ICMP";
    std::uint32_t ip = 0;
    if (!resolveIpv4(host, ip)) {
        r.why = "adresse inconnue : " + host;
        return r;
    }
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) {
        r.why = "ICMP indisponible sur ce PC";
        return r;
    }
    char data[32] = "XpgAnalyzer ping";
    std::vector<unsigned char> reply(sizeof(ICMP_ECHO_REPLY) + sizeof data + 64);
    const double t0 = nowMs();
    const DWORD n = IcmpSendEcho(h, htonl(ip), data, sizeof data, nullptr, reply.data(), static_cast<DWORD>(reply.size()),
                                 static_cast<DWORD>(std::max(1, timeoutMs)));
    const double t1 = nowMs();
    if (n > 0) {
        const auto* e = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
        if (e->Status == IP_SUCCESS) {
            r.ok = true;
            r.ms = e->RoundTripTime > 0 ? static_cast<double>(e->RoundTripTime) : std::max(0.1, t1 - t0);
        } else if (e->Status == IP_DEST_HOST_UNREACHABLE || e->Status == IP_DEST_NET_UNREACHABLE) {
            r.why = "h\xC3\xB4te injoignable";
        } else {
            r.why = "pas de r\xC3\xA9ponse en " + std::to_string(timeoutMs) + " ms";
        }
    } else {
        r.why = "pas de r\xC3\xA9ponse en " + std::to_string(timeoutMs) + " ms";
    }
    IcmpCloseHandle(h);
    return r;
}

bool addressInUse(std::uint32_t ip, int timeoutMs, std::string* mac) {
    (void)timeoutMs;
    ULONG buf[2] = {0, 0};
    ULONG len = 6;
    if (SendARP(htonl(ip), 0, buf, &len) == NO_ERROR && len > 0) {
        if (mac) *mac = macText(reinterpret_cast<const unsigned char*>(buf), len);
        return true;
    }
    return false;
}

std::string arpLookup(std::uint32_t ip) {
    ULONG size = 0;
    GetIpNetTable(nullptr, &size, FALSE);
    if (size == 0) return {};
    std::vector<unsigned char> buf(size);
    auto* table = reinterpret_cast<MIB_IPNETTABLE*>(buf.data());
    if (GetIpNetTable(table, &size, FALSE) != NO_ERROR) return {};
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (ntohl(row.dwAddr) != ip || row.dwType == MIB_IPNET_TYPE_INVALID || row.dwPhysAddrLen == 0) continue;
        return macText(row.bPhysAddr, row.dwPhysAddrLen);
    }
    return {};
}

bool applyPortConfig(const Adapter& a, const PortConfig& c, std::string* log, std::string* why) {
    const auto commands = applyCommands(a, c, true);
    std::string params = "/c ";
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (i) params += " & ";
        params += commands[i];
    }
    if (log) *log = params.substr(3);
    const std::wstring wparams = widen(params);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";                 // l'autorisation administrateur
    info.lpFile = L"cmd.exe";
    info.lpParameters = wparams.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        const DWORD e = GetLastError();
        if (why) *why = e == ERROR_CANCELLED ? "autorisation administrateur refus\xC3\xA9" "e : rien n'a chang\xC3\xA9"
                                             : "netsh n'a pas pu \xC3\xAAtre lanc\xC3\xA9 (erreur " + std::to_string(e) + ")";
        return false;
    }
    const DWORD waited = WaitForSingleObject(info.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    if (waited != WAIT_OBJECT_0) {
        if (why) *why = "netsh ne r\xC3\xA9pond pas depuis 60 s";
        return false;
    }
    if (code != 0) {
        if (why) *why = "netsh a refus\xC3\xA9 le r\xC3\xA9glage (code " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

#else   // --------------------------------------------------------- Linux ---

std::vector<Adapter> adapters() {
    std::vector<Adapter> out;
    std::map<std::string, std::size_t> byName;
    // Les interfaces : /sys/class/net (y compris celles sans adresse, cable debranche).
    if (DIR* d = ::opendir("/sys/class/net")) {
        std::vector<std::string> names;
        while (const dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == ".." || n == "lo") continue;
            names.push_back(n);
        }
        ::closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) {
            const std::string base = "/sys/class/net/" + n;
            const std::string type = readFile(base + "/type");
            if (type == "772") continue;                          // la boucle locale
            Adapter a;
            a.name = n;
            a.index = n;
            a.mac = readFile(base + "/address");
            for (auto& c : a.mac) {
                if (c == ':') c = '-';
                else c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (a.mac == "00-00-00-00-00-00") a.mac.clear();
            a.kind = exists(base + "/wireless") ? Adapter::Kind::WiFi : type == "1" ? Adapter::Kind::Ethernet : Adapter::Kind::Other;
            const std::string carrier = readFile(base + "/carrier");
            const std::string oper = readFile(base + "/operstate");
            a.up = carrier == "1" || (carrier.empty() && oper == "up");
            const std::string speed = readFile(base + "/speed");
            if (!speed.empty() && speed[0] != '-') {
                try { a.speedBps = static_cast<std::uint64_t>(std::stoll(speed)) * 1000000ULL; } catch (...) { a.speedBps = 0; }
            }
            if (!a.up) a.speedBps = 0;
            // La carte : son pilote ; une interface sans materiel : virtuelle.
            char link[512] = {0};
            const std::string drv = base + "/device/driver";
            const auto len = ::readlink(drv.c_str(), link, sizeof link - 1);
            if (len > 0) {
                std::string p(link, static_cast<std::size_t>(len));
                a.description = "pilote " + p.substr(p.rfind('/') + 1);
            } else {
                a.description = "interface virtuelle";
            }
            byName[n] = out.size();
            out.push_back(std::move(a));
        }
    }
    // Les adresses IPv4 et leur masque.
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_name) continue;
            const auto it = byName.find(i->ifa_name);
            if (it == byName.end()) continue;
            const std::uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr);
            int prefix = 32;
            if (i->ifa_netmask) {
                const std::uint32_t m = ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_netmask)->sin_addr.s_addr);
                prefix = 0;
                while (prefix < 32 && (m & (0x80000000u >> prefix))) ++prefix;
            }
            out[it->second].addresses.emplace_back(ip, prefix);
        }
        ::freeifaddrs(list);
    }
    // La passerelle par defaut de chaque interface : /proc/net/route.
    {
        std::ifstream f("/proc/net/route");
        std::string line;
        std::getline(f, line);
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string iface, dest, gw, flags;
            ss >> iface >> dest >> gw >> flags;
            if (dest != "00000000") continue;
            const auto it = byName.find(iface);
            if (it == byName.end()) continue;
            unsigned long g = std::strtoul(gw.c_str(), nullptr, 16);
            out[it->second].gateway = ntohl(static_cast<std::uint32_t>(g));
        }
    }
    // Les DNS : /etc/resolv.conf (communs a toutes les cartes).
    std::vector<std::uint32_t> dns;
    {
        std::ifstream f("/etc/resolv.conf");
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string key, value;
            ss >> key >> value;
            std::uint32_t ip = 0;
            if (key == "nameserver" && equip::parseIpv4(value, ip)) dns.push_back(ip);
        }
    }
    for (auto& a : out)
        if (!a.addresses.empty()) a.dns = dns;
    std::stable_sort(out.begin(), out.end(), [](const Adapter& x, const Adapter& y) { return x.kind < y.kind; });
    return out;
}

std::string computerName() {
    char b[256] = {0};
    if (::gethostname(b, sizeof b - 1) != 0) return "ce PC";
    return b;
}

std::string systemName() {
    std::ifstream f("/etc/os-release");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        std::string v = line.substr(12);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return v;
    }
    return "Linux";
}

bool isAdministrator() { return ::geteuid() == 0; }

namespace {

std::uint16_t checksum(const unsigned char* data, std::size_t n) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i + 1 < n; i += 2) sum += static_cast<std::uint32_t>((data[i] << 8) | data[i + 1]);
    if (n & 1) sum += static_cast<std::uint32_t>(data[n - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<std::uint16_t>(~sum);
}

// Un echo ICMP par un socket (datagramme ou brut) ; -1 : le socket n'a pas pu s'ouvrir.
int icmpEcho(std::uint32_t ip, int timeoutMs, bool raw, PingResult& r) {
    const int fd = ::socket(AF_INET, raw ? SOCK_RAW : SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) return -1;
    static std::atomic<unsigned> seq{0};
    const std::uint16_t id = static_cast<std::uint16_t>(::getpid() & 0xFFFF);
    const std::uint16_t mySeq = static_cast<std::uint16_t>(++seq);
    std::array<unsigned char, 40> pkt{};
    pkt[0] = 8;                                  // echo
    pkt[4] = static_cast<unsigned char>(id >> 8);
    pkt[5] = static_cast<unsigned char>(id & 0xFF);
    pkt[6] = static_cast<unsigned char>(mySeq >> 8);
    pkt[7] = static_cast<unsigned char>(mySeq & 0xFF);
    std::memcpy(pkt.data() + 8, "XpgAnalyzer ping ...............", 32);
    const std::uint16_t cs = checksum(pkt.data(), pkt.size());
    pkt[2] = static_cast<unsigned char>(cs >> 8);
    pkt[3] = static_cast<unsigned char>(cs & 0xFF);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(ip);
    const double t0 = nowMs();
    if (::sendto(fd, pkt.data(), pkt.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to) < 0) {
        r.why = errno == ENETUNREACH || errno == EHOSTUNREACH ? "h\xC3\xB4te injoignable (aucun port du PC dans ce r\xC3\xA9seau)"
                                                               : "envoi impossible : " + net::lastErrorText();
        ::close(fd);
        return 0;
    }
    for (;;) {
        const double left = timeoutMs - (nowMs() - t0);
        if (left <= 0) break;
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, static_cast<int>(left) + 1) <= 0) break;
        std::array<unsigned char, 1500> in{};
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const auto n = ::recvfrom(fd, in.data(), in.size(), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        std::size_t off = 0;
        if (raw) {
            off = static_cast<std::size_t>((in[0] & 0x0F) * 4);          // l'en-tete IP
            if (from.sin_addr.s_addr != to.sin_addr.s_addr) continue;
        }
        if (static_cast<std::size_t>(n) < off + 8) continue;
        const unsigned char* icmp = in.data() + off;
        if (icmp[0] == 3) {                                                  // destination injoignable
            r.why = "h\xC3\xB4te injoignable";
            ::close(fd);
            return 0;
        }
        if (icmp[0] != 0) continue;                                          // pas un echo reply
        const std::uint16_t rseq = static_cast<std::uint16_t>((icmp[6] << 8) | icmp[7]);
        const std::uint16_t rid = static_cast<std::uint16_t>((icmp[4] << 8) | icmp[5]);
        if (rseq != mySeq || (raw && rid != id)) continue;
        r.ok = true;
        r.ms = std::max(0.01, nowMs() - t0);
        ::close(fd);
        return 1;
    }
    r.why = "pas de r\xC3\xA9ponse en " + std::to_string(timeoutMs) + " ms";
    ::close(fd);
    return 0;
}

} // namespace

PingResult ping(const std::string& host, int timeoutMs) {
    PingResult r;
    r.method = "ICMP";
    std::uint32_t ip = 0;
    if (!resolveIpv4(host, ip)) {
        r.why = "adresse inconnue : " + host;
        return r;
    }
    // Le socket ICMP datagramme (sans droits, si le systeme le permet), puis brut (root).
    if (icmpEcho(ip, timeoutMs, false, r) >= 0) return r;
    if (icmpEcho(ip, timeoutMs, true, r) >= 0) return r;
    // Sinon la commande ping.
    r.method = "ping";
    int code = 0;
    const int secs = std::max(1, (timeoutMs + 999) / 1000);
    const std::string outText = run("ping -c 1 -W " + std::to_string(secs) + " " + shellQuote(equip::ipv4Text(ip)), &code);
    const auto at = outText.find("time=");
    if (code == 0 && at != std::string::npos) {
        r.ok = true;
        r.ms = std::atof(outText.c_str() + at + 5);
    } else {
        r.why = "pas de r\xC3\xA9ponse en " + std::to_string(timeoutMs) + " ms";
    }
    return r;
}

std::string arpLookup(std::uint32_t ip) {
    std::ifstream f("/proc/net/arp");
    std::string line;
    std::getline(f, line);
    const std::string want = equip::ipv4Text(ip);
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string addr, hw, flags, mac;
        ss >> addr >> hw >> flags >> mac;
        if (addr != want) continue;
        if (flags == "0x0" || mac == "00:00:00:00:00:00") return {};
        for (auto& c : mac) {
            if (c == ':') c = '-';
            else c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return mac;
    }
    return {};
}

bool addressInUse(std::uint32_t ip, int timeoutMs, std::string* mac) {
    const auto p = ping(equip::ipv4Text(ip), timeoutMs);
    const std::string m = arpLookup(ip);
    if (mac) *mac = m;
    return p.ok || !m.empty();
}

bool applyPortConfig(const Adapter& a, const PortConfig& c, std::string* log, std::string* why) {
    const auto commands = applyCommands(a, c, false);
    std::string script;
    for (const auto& cmd : commands) script += (script.empty() ? "" : " && ") + cmd;
    const std::string line = isAdministrator() ? "sh -c " + shellQuote(script) : "pkexec sh -c " + shellQuote(script);
    int code = 0;
    const std::string output = run(line, &code);
    if (log) *log = script + (output.empty() ? std::string{} : "\n" + output);
    if (code != 0) {
        if (why) *why = isAdministrator() ? "ip a refus\xC3\xA9 le r\xC3\xA9glage : " + output
                                          : "autorisation refus\xC3\xA9" "e, ou ip a refus\xC3\xA9 le r\xC3\xA9glage : " + output;
        return false;
    }
    return true;
}
#endif

// ============================================================ commun ===
PingResult probeTcp(const std::string& host, int port, int timeoutMs) {
    PingResult r;
    r.method = "TCP " + std::to_string(port);
    net::Socket s;
    const double t0 = nowMs();
    std::string why;
    if (s.connect(host, port, timeoutMs, &why)) {
        r.ok = true;
        r.ms = std::max(0.01, nowMs() - t0);
        s.close();
    } else {
        r.why = why.empty() ? "port " + std::to_string(port) + " ferm\xC3\xA9" : why;
    }
    return r;
}

std::string reverseName(std::uint32_t ip) {
#if defined(_WIN32)
    WsaInit wsa;
#endif
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(ip);
    char host[1025] = {0};
#if defined(_WIN32)
    const DWORD hostLen = static_cast<DWORD>(sizeof host);
#else
    const socklen_t hostLen = static_cast<socklen_t>(sizeof host);
#endif
    if (getnameinfo(reinterpret_cast<const sockaddr*>(&sa), static_cast<socklen_t>(sizeof sa), host, hostLen, nullptr, 0, NI_NAMEREQD) != 0) return {};
    return host;
}

std::vector<std::string> applyCommands(const Adapter& a, const PortConfig& c, bool windows) {
    std::vector<std::string> out;
    if (windows) {
        const std::string name = "name=" + (a.index.empty() ? "\"" + a.name + "\"" : a.index);
        if (c.add) {
            out.push_back("netsh interface ipv4 add address " + name + " address=" + c.ip + " mask=" + c.mask);
            return out;
        }
        if (c.dhcp) {
            out.push_back("netsh interface ipv4 set address " + name + " source=dhcp");
            out.push_back("netsh interface ipv4 set dnsservers " + name + " source=dhcp");
            return out;
        }
        out.push_back("netsh interface ipv4 set address " + name + " source=static address=" + c.ip + " mask=" + c.mask
                      + " gateway=" + (c.gateway.empty() ? std::string("none") : c.gateway));
        if (!c.dns.empty())
            out.push_back("netsh interface ipv4 set dnsservers " + name + " source=static address=" + c.dns + " register=primary validate=no");
        return out;
    }
    const std::string dev = a.index.empty() ? a.name : a.index;
    if (c.add) {
        int extra = 24;
        (void)equip::parseMask(c.mask, extra);
        out.push_back("ip addr add " + c.ip + "/" + std::to_string(extra) + " dev " + dev);
        return out;
    }
    if (c.dhcp) {
        out.push_back("ip -4 addr flush dev " + dev);
        out.push_back("(dhclient -1 " + dev + " || dhcpcd -1 " + dev + ")");
        return out;
    }
    int prefix = 24;
    (void)equip::parseMask(c.mask, prefix);
    out.push_back("ip -4 addr flush dev " + dev);
    out.push_back("ip addr add " + c.ip + "/" + std::to_string(prefix) + " dev " + dev);
    out.push_back("ip link set " + dev + " up");
    if (!c.gateway.empty()) out.push_back("ip route replace default via " + c.gateway + " dev " + dev);
    return out;
}

PortConfig currentConfig(const Adapter& a) {
    PortConfig c;
    c.dhcp = a.dhcpKnown && a.dhcp;
    if (!a.addresses.empty()) {
        c.ip = equip::ipv4Text(a.ip());
        c.mask = equip::maskText(a.prefix());
    }
    if (a.gateway) c.gateway = equip::ipv4Text(a.gateway);
    if (!a.dns.empty()) c.dns = equip::ipv4Text(a.dns.front());
    return c;
}

} // namespace hmi::netinfo
