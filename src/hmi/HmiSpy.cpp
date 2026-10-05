// hmi/HmiSpy.cpp - l'espion Modbus (lot 15) : le journal, la trace, le relais,
// le decodage des paquets, la capture (Npcap / libpcap / AF_PACKET).
#include "HmiSpy.hpp"

#include "HmiModbus.hpp"
#include "HmiModbusTool.hpp"
#include "HmiNet.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "ws2_32.lib")
#  endif
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <dirent.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#  if !defined(XPG_SANS_NPCAP)
#    include <dlfcn.h>
#    if defined(__linux__)
#      include <linux/if_packet.h>
#      include <net/ethernet.h>
#    endif
#  endif
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>

namespace hmi::spy {

namespace {

double wall() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string clockText(double t) {
    const auto secs = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char b[40];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec, static_cast<int>((t - static_cast<double>(secs)) * 1000.0));
    return b;
}

std::string csvCell(std::string s) {
    for (auto& c : s)
        if (c == ';' || c == '\n' || c == '\r') c = ',';
    return s;
}

std::string ipText(const std::uint8_t* p) {
    return std::to_string(p[0]) + "." + std::to_string(p[1]) + "." + std::to_string(p[2]) + "." + std::to_string(p[3]);
}

} // namespace

// ================================================================= journal ===
void Journal::add(FrameEvent e) {
    if (e.adu.size() >= 8) {
        e.transaction = (e.adu[0] << 8) | e.adu[1];
        e.unit = e.adu[6];
        e.function = e.adu[7] & 0x7F;
        e.exception = (e.adu[7] & 0x80) && e.adu.size() > 8 ? e.adu[8] : 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // La connexion, vue du maitre : "maitre>equipement".
    const std::string conn = e.request ? e.from + ">" + e.to : e.to + ">" + e.from;
    const std::string pk = conn + "#" + std::to_string(e.transaction);
    if (e.request) {
        pending_[pk] = {e.t, e.function};
        ++stats_.requests;
        e.text = mbtool::describe(e.adu, true);
    } else {
        int asked = 0;
        if (const auto it = pending_.find(pk); it != pending_.end()) {
            e.replyMs = std::max(0.0, (e.t - it->second.first) * 1000.0);
            asked = it->second.second;
            pending_.erase(it);
            ++replies_;
            sumReply_ += e.replyMs;
            stats_.avgReplyMs = sumReply_ / static_cast<double>(replies_);
            stats_.maxReplyMs = std::max(stats_.maxReplyMs, e.replyMs);
        }
        ++stats_.responses;
        if (e.exception) ++stats_.exceptions;
        e.text = mbtool::describe(e.adu, false, asked);
    }
    ++stats_.frames;
    e.seq = next_++;
    events_.push_back(std::move(e));
    while (events_.size() > capacity_) events_.pop_front();
    // Les requetes restees sans reponse : oubliees au bout de 10 s.
    if (pending_.size() > 64)
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (events_.back().t - it->second.first > 10.0) {
                ++stats_.unanswered;
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
}

std::vector<FrameEvent> Journal::since(std::uint64_t after, std::size_t max) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FrameEvent> out;
    auto it = std::lower_bound(events_.begin(), events_.end(), after + 1,
                               [](const FrameEvent& e, std::uint64_t s) { return e.seq < s; });
    for (; it != events_.end() && out.size() < max; ++it) out.push_back(*it);
    return out;
}

std::uint64_t Journal::lastSeq() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_ - 1;
}

void Journal::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    pending_.clear();
    stats_ = Stats{};
    sumReply_ = 0;
    replies_ = 0;
}

Journal::Stats Journal::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s = stats_;
    const double now = events_.empty() ? wall() : std::max(wall(), events_.back().t);
    for (const auto& [key, p] : pending_)
        if (now - p.first > 5.0) ++s.unanswered;
    return s;
}

std::string Journal::csv() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out = "rang;heure;source;de;vers;sens;trame;hexa;reponse (ms)\n";
    for (const auto& e : events_) {
        char ms[32] = "";
        if (e.replyMs >= 0) std::snprintf(ms, sizeof ms, "%.2f", e.replyMs);
        out += std::to_string(e.seq) + ";" + clockText(e.t) + ";" + e.source + ";" + e.from + ";" + e.to + ";"
             + (e.request ? "requ\xC3\xAAte" : "r\xC3\xA9ponse") + ";" + csvCell(e.text) + ";" + mbtool::hex(e.adu) + ";" + ms + "\n";
    }
    return out;
}

// ============================================================= reassemblage ===
void Reassembler::feed(const std::uint8_t* data, std::size_t n) {
    buffer_.insert(buffer_.end(), data, data + n);
    if (buffer_.size() > 64 * 1024) buffer_.clear();       // pas du Modbus : on repart
}

bool Reassembler::next(std::vector<std::uint8_t>& adu) {
    while (buffer_.size() >= 7) {
        const int proto = (buffer_[2] << 8) | buffer_[3];
        const int len = (buffer_[4] << 8) | buffer_[5];
        if (proto != 0 || len < 2 || len > 254) {
            buffer_.erase(buffer_.begin());                 // desaligne : on cherche la suite
            continue;
        }
        const std::size_t total = 6 + static_cast<std::size_t>(len);
        if (buffer_.size() < total) return false;
        adu.assign(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
        return true;
    }
    return false;
}

// ================================================================== la trace ===
namespace {
std::atomic<bool>& traceOn() {
    static std::atomic<bool> on{false};
    return on;
}
} // namespace

void traceTo(std::shared_ptr<Journal> journal) {
    if (!journal) {
        traceOn() = false;
        modbus::setTap({});
        return;
    }
    traceOn() = true;
    modbus::setTap([journal](bool sent, const std::string& local, const std::string& peer, const std::vector<std::uint8_t>& adu) {
        FrameEvent e;
        e.t = wall();
        e.source = "trace";
        e.request = sent;
        e.from = sent ? local : peer;
        e.to = sent ? peer : local;
        e.adu = adu;
        journal->add(std::move(e));
    });
}

bool tracing() noexcept { return traceOn().load(); }

// ================================================================= le relais ===
struct Relay::Impl {
    struct Pair {
        net::Socket  client, upstream;
        std::string  clientName, upstreamName;
        Reassembler  up, down;
    };
    net::Socket                        listener;
    std::string                        targetHost;
    int                                targetPort{502};
    std::shared_ptr<Journal>           journal;
    std::vector<std::unique_ptr<Pair>> pairs;
};

Relay::Relay() = default;
Relay::~Relay() { stop(); }

bool Relay::start(const std::string& bind, int listenPort, const std::string& targetHost, int targetPort,
                  std::shared_ptr<Journal> journal, std::string* why) {
    stop();
    impl_ = std::make_unique<Impl>();
    if (!impl_->listener.listen(bind, listenPort, why)) {
        impl_.reset();
        return false;
    }
    impl_->targetHost = targetHost;
    impl_->targetPort = targetPort;
    impl_->journal = std::move(journal);
    port_ = impl_->listener.localPort();
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = Stats{};
    }
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void Relay::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
    impl_.reset();
    port_ = 0;
}

Relay::Stats Relay::stats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void Relay::loop() {
    auto& I = *impl_;
    std::vector<std::uint8_t> buf(4096);
    const auto note = [&](Impl::Pair& p, bool request, const std::vector<std::uint8_t>& adu) {
        FrameEvent e;
        e.t = wall();
        e.source = "relais";
        e.request = request;
        e.from = request ? p.clientName : p.upstreamName;
        e.to = request ? p.upstreamName : p.clientName;
        e.adu = adu;
        if (I.journal) I.journal->add(std::move(e));
        std::lock_guard<std::mutex> lock(statsMutex_);
        ++stats_.frames;
    };
    while (!stop_) {
        std::vector<const net::Socket*> watch{&I.listener};
        for (const auto& p : I.pairs) {
            watch.push_back(&p->client);
            watch.push_back(&p->upstream);
        }
        const auto ready = net::waitReadable(watch, 100);
        for (const auto r : ready) {
            if (r == 0) {
                std::string peer;
                net::Socket c = I.listener.accept(0, &peer);
                if (!c.valid()) continue;
                auto p = std::make_unique<Impl::Pair>();
                std::string why;
                if (!p->upstream.connect(I.targetHost, I.targetPort, 3000, &why)) {
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.lastError = I.targetHost + ":" + std::to_string(I.targetPort) + " : " + why;
                    continue;                                  // le client est ferme en sortant
                }
                p->client = std::move(c);
                p->clientName = peer;
                p->upstreamName = I.targetHost + ":" + std::to_string(I.targetPort);
                {
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.lastClient = peer;
                }
                I.pairs.push_back(std::move(p));
                continue;
            }
            const std::size_t k = (r - 1) / 2;
            if (k >= I.pairs.size()) continue;
            auto& p = *I.pairs[k];
            const bool fromClient = (r - 1) % 2 == 0;
            net::Socket& in = fromClient ? p.client : p.upstream;
            net::Socket& out = fromClient ? p.upstream : p.client;
            const long n = in.receiveSome(buf.data(), buf.size(), 0);
            if (n < 0) {
                p.client.close();
                p.upstream.close();
                continue;
            }
            if (n == 0) continue;
            if (!out.sendAll(buf.data(), static_cast<std::size_t>(n), 2000)) {
                p.client.close();
                p.upstream.close();
                continue;
            }
            auto& re = fromClient ? p.up : p.down;
            re.feed(buf.data(), static_cast<std::size_t>(n));
            std::vector<std::uint8_t> adu;
            while (re.next(adu)) note(p, fromClient, adu);
        }
        I.pairs.erase(std::remove_if(I.pairs.begin(), I.pairs.end(),
                                     [](const std::unique_ptr<Impl::Pair>& p) { return !p->client.valid() || !p->upstream.valid(); }),
                      I.pairs.end());
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.clients = I.pairs.size();
    }
    for (auto& p : I.pairs) {
        p->client.close();
        p->upstream.close();
    }
    I.pairs.clear();
    I.listener.close();
}

// ======================================================= decodage des paquets ===
std::vector<FrameEvent> decodePacket(const std::uint8_t* d, std::size_t n, int linkType, int port, double t, Flows& flows) {
    std::vector<FrameEvent> out;
    std::size_t ip = 0;
    switch (linkType) {
        case kLinkEthernet: {
            if (n < 14) return out;
            std::size_t off = 12;
            int type = (d[off] << 8) | d[off + 1];
            if (type == 0x8100 && n >= 18) {                   // VLAN
                off = 16;
                type = (d[off] << 8) | d[off + 1];
            }
            if (type != 0x0800) return out;
            ip = off + 2;
            break;
        }
        case kLinkNull: {
            if (n < 4) return out;
            const std::uint32_t fam = static_cast<std::uint32_t>(d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24));
            if (fam != 2 && fam != 0x02000000u) return out;   // AF_INET, dans l'un ou l'autre ordre
            ip = 4;
            break;
        }
        case kLinkLinuxSll: {
            if (n < 16) return out;
            if (((d[14] << 8) | d[15]) != 0x0800) return out;
            ip = 16;
            break;
        }
        case kLinkRaw: case kLinkRaw2: ip = 0; break;
        default: return out;
    }
    if (n < ip + 20 || (d[ip] >> 4) != 4) return out;
    const std::size_t ihl = static_cast<std::size_t>(d[ip] & 0x0F) * 4;
    const std::size_t total = static_cast<std::size_t>((d[ip + 2] << 8) | d[ip + 3]);
    if (d[ip + 9] != 6 || ihl < 20) return out;                // TCP seulement
    if ((((d[ip + 6] & 0x1F) << 8) | d[ip + 7]) != 0) return out;   // un fragment
    const std::size_t end = std::min(n, ip + total);
    const std::size_t tcp = ip + ihl;
    if (end < tcp + 20) return out;
    const int sport = (d[tcp] << 8) | d[tcp + 1];
    const int dport = (d[tcp + 2] << 8) | d[tcp + 3];
    if (sport != port && dport != port) return out;
    const std::size_t tcpLen = static_cast<std::size_t>(d[tcp + 12] >> 4) * 4;
    const std::uint8_t flags = d[tcp + 13];
    const std::string src = ipText(d + ip + 12) + ":" + std::to_string(sport);
    const std::string dst = ipText(d + ip + 16) + ":" + std::to_string(dport);
    auto& re = flows.byFlow[src + ">" + dst];
    if (flags & 0x02) re.reset();                              // SYN : une connexion neuve
    const std::size_t payload = tcp + tcpLen;
    if (payload < end) re.feed(d + payload, end - payload);
    std::vector<std::uint8_t> adu;
    while (re.next(adu)) {
        FrameEvent e;
        e.t = t;
        e.source = "capture";
        e.request = dport == port;
        e.from = src;
        e.to = dst;
        e.adu = adu;
        out.push_back(std::move(e));
    }
    if (flags & 0x05) flows.byFlow.erase(src + ">" + dst);     // FIN, RST : fini
    return out;
}

// ================================================================ la capture ===
#if !defined(XPG_SANS_NPCAP)
namespace {

// Les structures de libpcap / Npcap (leur disposition est fixee par l'ABI).
struct PcapAddr {
    PcapAddr* next;
    sockaddr* addr;
    sockaddr* netmask;
    sockaddr* broadaddr;
    sockaddr* dstaddr;
};
struct PcapIf {
    PcapIf*       next;
    char*         name;
    char*         description;
    PcapAddr*     addresses;
    std::uint32_t flags;
};
struct PcapPkthdr {
    timeval       ts;
    std::uint32_t caplen;
    std::uint32_t len;
};
struct BpfProgram {
    unsigned int bf_len;
    void*        bf_insns;
};

struct Pcap {
    using FindAll = int (*)(PcapIf**, char*);
    using FreeAll = void (*)(PcapIf*);
    using OpenLive = void* (*)(const char*, int, int, int, char*);
    using NextEx = int (*)(void*, PcapPkthdr**, const unsigned char**);
    using Datalink = int (*)(void*);
    using Close = void (*)(void*);
    using GetErr = char* (*)(void*);
    using Compile = int (*)(void*, BpfProgram*, const char*, int, std::uint32_t);
    using SetFilter = int (*)(void*, BpfProgram*);
    using FreeCode = void (*)(BpfProgram*);
    using BreakLoop = void (*)(void*);
    using SetNonBlock = int (*)(void*, int, char*);
    using SelectableFd = int (*)(void*);
    FindAll   findalldevs{nullptr};
    FreeAll   freealldevs{nullptr};
    OpenLive  openLive{nullptr};
    NextEx    nextEx{nullptr};
    Datalink  datalink{nullptr};
    Close     close{nullptr};
    GetErr    geterr{nullptr};
    Compile   compile{nullptr};
    SetFilter setfilter{nullptr};
    FreeCode  freecode{nullptr};
    BreakLoop    breakloop{nullptr};
    SetNonBlock  setnonblock{nullptr};
    SelectableFd selectableFd{nullptr};     // Linux, macOS (pas Npcap)
    std::string name;       // "Npcap", "libpcap"
    std::string why;
    [[nodiscard]] bool ok() const noexcept { return findalldevs && freealldevs && openLive && nextEx && datalink && close && geterr; }
};

// Charge une fois, jamais decharge.
const Pcap& pcap() {
    static const Pcap lib = [] {
        Pcap p;
#if defined(_WIN32)
        HMODULE h = nullptr;
        wchar_t sys[MAX_PATH] = {0};
        const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
        if (n > 0 && n < MAX_PATH - 32) {
            const std::wstring path = std::wstring(sys) + L"\\Npcap\\wpcap.dll";
            h = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        if (!h) h = LoadLibraryW(L"wpcap.dll");                // WinPcap, ou Npcap en mode compatible
        if (!h) {
            p.why = "Npcap n'est pas install\xC3\xA9 sur ce PC (https://npcap.com)";
            return p;
        }
        const auto sym = [&](const char* s) { return reinterpret_cast<void*>(GetProcAddress(h, s)); };
        p.name = "Npcap";
#else
        void* h = nullptr;
        for (const char* so : {"libpcap.so.1", "libpcap.so.0.8", "libpcap.so"}) {
            h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
            if (h) break;
        }
        if (!h) {
            p.why = "libpcap absente (paquet libpcap0.8)";
            return p;
        }
        const auto sym = [&](const char* s) { return dlsym(h, s); };
        p.name = "libpcap";
#endif
        p.findalldevs = reinterpret_cast<Pcap::FindAll>(sym("pcap_findalldevs"));
        p.freealldevs = reinterpret_cast<Pcap::FreeAll>(sym("pcap_freealldevs"));
        p.openLive = reinterpret_cast<Pcap::OpenLive>(sym("pcap_open_live"));
        p.nextEx = reinterpret_cast<Pcap::NextEx>(sym("pcap_next_ex"));
        p.datalink = reinterpret_cast<Pcap::Datalink>(sym("pcap_datalink"));
        p.close = reinterpret_cast<Pcap::Close>(sym("pcap_close"));
        p.geterr = reinterpret_cast<Pcap::GetErr>(sym("pcap_geterr"));
        p.compile = reinterpret_cast<Pcap::Compile>(sym("pcap_compile"));
        p.setfilter = reinterpret_cast<Pcap::SetFilter>(sym("pcap_setfilter"));
        p.freecode = reinterpret_cast<Pcap::FreeCode>(sym("pcap_freecode"));
        p.breakloop = reinterpret_cast<Pcap::BreakLoop>(sym("pcap_breakloop"));
        p.setnonblock = reinterpret_cast<Pcap::SetNonBlock>(sym("pcap_setnonblock"));
#if !defined(_WIN32)
        p.selectableFd = reinterpret_cast<Pcap::SelectableFd>(sym("pcap_get_selectable_fd"));
#endif
        if (!p.ok()) p.why = p.name + " incomplet (une fonction manque)";
        return p;
    }();
    return lib;
}

#if defined(__linux__)
bool packetSocketUsable() {
    const int fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) return false;
    ::close(fd);
    return true;
}
#endif

std::string addressOf(const sockaddr* a) {
    if (!a || a->sa_family != AF_INET) return {};
    const auto* sin = reinterpret_cast<const sockaddr_in*>(a);
    const auto* b = reinterpret_cast<const std::uint8_t*>(&sin->sin_addr);
    return ipText(b);
}

} // namespace
#endif

CaptureSupport captureSupport() {
    CaptureSupport s;
#if defined(XPG_SANS_NPCAP)
    s.why = "construit sans la capture r\xC3\xA9seau (option XPG_SANS_NPCAP) : la trace et le relais restent";
    return s;
#else
    s.compiled = true;
    const auto& p = pcap();
    if (p.ok()) {
        s.available = true;
        s.backend = p.name;
        return s;
    }
#  if defined(__linux__)
    if (packetSocketUsable()) {
        s.available = true;
        s.backend = "AF_PACKET";
        return s;
    }
    s.why = p.why + " ; AF_PACKET demande les droits root";
#  else
    s.why = p.why;
#  endif
    return s;
#endif
}

std::vector<CaptureInterface> captureInterfaces(std::string* why) {
    std::vector<CaptureInterface> out;
#if defined(XPG_SANS_NPCAP)
    if (why) *why = captureSupport().why;
    return out;
#else
    const auto& p = pcap();
    if (p.ok()) {
        PcapIf* all = nullptr;
        char err[512] = {0};
        if (p.findalldevs(&all, err) != 0) {
            if (why) *why = err;
            return out;
        }
        for (PcapIf* d = all; d; d = d->next) {
            CaptureInterface c;
            c.name = d->name ? d->name : "";
            c.description = d->description ? d->description : c.name;
            for (PcapAddr* a = d->addresses; a; a = a->next) {
                const std::string ip = addressOf(a->addr);
                if (!ip.empty()) c.addresses.push_back(ip);
            }
            out.push_back(std::move(c));
        }
        p.freealldevs(all);
        return out;
    }
#  if defined(__linux__)
    if (packetSocketUsable()) {
        if (DIR* dir = ::opendir("/sys/class/net")) {
            while (const dirent* e = ::readdir(dir)) {
                const std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                CaptureInterface c;
                c.name = n;
                c.description = n == "lo" ? "boucle locale (ce PC)" : n;
                out.push_back(std::move(c));
            }
            ::closedir(dir);
        }
        ifaddrs* list = nullptr;
        if (::getifaddrs(&list) == 0) {
            for (ifaddrs* i = list; i; i = i->ifa_next) {
                if (!i->ifa_name) continue;
                const std::string ip = addressOf(i->ifa_addr);
                if (ip.empty()) continue;
                for (auto& c : out)
                    if (c.name == i->ifa_name) c.addresses.push_back(ip);
            }
            ::freeifaddrs(list);
        }
        std::sort(out.begin(), out.end(), [](const CaptureInterface& a, const CaptureInterface& b) { return a.name < b.name; });
        return out;
    }
#  endif
    if (why) *why = captureSupport().why;
    return out;
#endif
}

struct Capture::Impl {
    std::shared_ptr<Journal> journal;
    int                      port{502};
    std::string              name;
    Flows                    flows;
#if !defined(XPG_SANS_NPCAP)
    void*                    handle{nullptr};     // pcap_t*
    int                      pollFd{-1};          // libpcap non bloquant : le fd a surveiller
    int                      linkType{kLinkEthernet};
    int                      fd{-1};              // AF_PACKET
    bool                     loopback{false};
#endif
};

Capture::Capture() = default;
Capture::~Capture() { stop(); }

bool Capture::start(const std::string& interfaceName, int port, bool promiscuous, std::shared_ptr<Journal> journal, std::string* why) {
    stop();
#if defined(XPG_SANS_NPCAP)
    (void)interfaceName;
    (void)port;
    (void)promiscuous;
    (void)journal;
    if (why) *why = captureSupport().why;
    return false;
#else
    auto impl = std::make_unique<Impl>();
    impl->journal = std::move(journal);
    impl->port = port;
    impl->name = interfaceName;
    Stats st;
    st.interfaceName = interfaceName;
    const auto& p = pcap();
    if (p.ok()) {
        char err[512] = {0};
        impl->handle = p.openLive(interfaceName.c_str(), 65535, promiscuous ? 1 : 0, 100, err);
        if (!impl->handle) {
            if (why) *why = std::string("ouverture de l'interface impossible : ") + err;
            return false;
        }
        impl->linkType = p.datalink(impl->handle);
        // Le filtre du noyau (le tri se refait ensuite de toute facon).
        if (p.compile && p.setfilter && p.freecode) {
            BpfProgram prog{};
            const std::string filter = "tcp port " + std::to_string(port);
            if (p.compile(impl->handle, &prog, filter.c_str(), 1, 0xFFFFFFFFu) == 0) {
                (void)p.setfilter(impl->handle, &prog);
                p.freecode(&prog);
            }
        }
        // Linux : libpcap (TPACKET_V3) ne rend la main qu'a l'arrivee d'un
        // paquet, meme avec un delai ; sans trafic, l'arret attendrait. Mode non
        // bloquant et poll() avec un delai a la place.
#  if !defined(_WIN32)
        if (p.selectableFd && p.setnonblock) {
            char nerr[512] = {0};
            const int fd = p.selectableFd(impl->handle);
            if (fd >= 0 && p.setnonblock(impl->handle, 1, nerr) == 0) impl->pollFd = fd;
        }
#  endif
        st.backend = p.name;
    } else {
#  if defined(__linux__)
        impl->fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (impl->fd < 0) {
            if (why) *why = "capture impossible : " + p.why + " ; AF_PACKET demande les droits root";
            return false;
        }
        const unsigned idx = ::if_nametoindex(interfaceName.c_str());
        if (idx == 0) {
            ::close(impl->fd);
            if (why) *why = "interface inconnue : " + interfaceName;
            return false;
        }
        sockaddr_ll sll{};
        sll.sll_family = AF_PACKET;
        sll.sll_protocol = htons(ETH_P_ALL);
        sll.sll_ifindex = static_cast<int>(idx);
        if (::bind(impl->fd, reinterpret_cast<sockaddr*>(&sll), sizeof sll) != 0) {
            ::close(impl->fd);
            if (why) *why = "interface " + interfaceName + " : " + net::lastErrorText();
            return false;
        }
        if (promiscuous) {
            packet_mreq mr{};
            mr.mr_ifindex = static_cast<int>(idx);
            mr.mr_type = PACKET_MR_PROMISC;
            (void)::setsockopt(impl->fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof mr);
        }
        impl->loopback = interfaceName == "lo";
        impl->linkType = kLinkEthernet;
        st.backend = "AF_PACKET";
#  else
        if (why) *why = p.why;
        return false;
#  endif
    }
    impl_ = std::move(impl);
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = st;
    }
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    return true;
#endif
}

void Capture::stop() {
    stop_ = true;
#if !defined(XPG_SANS_NPCAP)
    // Reveille pcap_next_ex s'il attend (Npcap le fait aussi a la fin de son delai).
    if (thread_.joinable() && impl_ && impl_->handle && impl_->pollFd < 0 && pcap().breakloop) pcap().breakloop(impl_->handle);
#endif
    if (thread_.joinable()) thread_.join();
    running_ = false;
#if !defined(XPG_SANS_NPCAP)
    if (impl_) {
        if (impl_->handle && pcap().close) pcap().close(impl_->handle);
#  if defined(__linux__)
        if (impl_->fd >= 0) ::close(impl_->fd);
#  endif
    }
#endif
    impl_.reset();
}

Capture::Stats Capture::stats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void Capture::loop() {
#if !defined(XPG_SANS_NPCAP)
    auto& I = *impl_;
    const auto deliver = [&](const std::uint8_t* data, std::size_t n, double t) {
        auto events = decodePacket(data, n, I.linkType, I.port, t, I.flows);
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            ++stats_.packets;
            stats_.modbus += events.size();
        }
        if (I.journal)
            for (auto& e : events) I.journal->add(std::move(e));
    };
    if (I.handle) {
        const auto& p = pcap();
        while (!stop_) {
#  if !defined(_WIN32)
            if (I.pollFd >= 0) {
                pollfd pf{I.pollFd, POLLIN, 0};
                if (::poll(&pf, 1, 100) <= 0) continue;
            }
#  endif
            PcapPkthdr* h = nullptr;
            const unsigned char* data = nullptr;
            const int rc = p.nextEx(I.handle, &h, &data);
            if (rc == 1 && h && data) {
                const double t = static_cast<double>(h->ts.tv_sec) + static_cast<double>(h->ts.tv_usec) / 1e6;
                deliver(data, h->caplen, t);
            } else if (rc == -2) {
                break;      // pcap_breakloop : l'arret
            } else if (rc < 0) {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.lastError = p.geterr(I.handle) ? p.geterr(I.handle) : "erreur de capture";
                break;
            }
        }
        return;
    }
#  if defined(__linux__)
    std::vector<std::uint8_t> buf(65536);
    while (!stop_) {
        pollfd pf{I.fd, POLLIN, 0};
        if (::poll(&pf, 1, 100) <= 0) continue;
        sockaddr_ll from{};
        socklen_t fl = sizeof from;
        const auto n = ::recvfrom(I.fd, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        // La boucle locale montre chaque paquet deux fois (sortant, puis entrant) : un seul.
        if (I.loopback && from.sll_pkttype == PACKET_OUTGOING) continue;
        deliver(buf.data(), static_cast<std::size_t>(n), wall());
    }
#  endif
#endif
}

} // namespace hmi::spy
