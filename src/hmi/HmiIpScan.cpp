// hmi/HmiIpScan.cpp - le scanner IP (lot 15) ; 1.21.0 : la confirmation, la cadence, la lecture seule.
#include "HmiIpScan.hpp"

#include "HmiEquipment.hpp"
#include "HmiModbus.hpp"
#include "HmiNetInfo.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace hmi::ipscan {

namespace {

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

} // namespace

bool Host::hasPort(int port) const { return std::find(open.begin(), open.end(), port) != open.end(); }

std::string Host::portsText() const {
    std::string out;
    for (const int p : open) {
        const std::string s = serviceName(p);
        out += (out.empty() ? "" : ", ") + std::to_string(p) + (s.empty() ? std::string{} : " " + s);
    }
    return out;
}

bool parseRange(std::string_view raw, std::uint32_t& first, std::uint32_t& last, std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    const std::string text = trimmed(raw);
    if (text.empty()) return fail("plage vide : un r\xC3\xA9seau (192.168.1.0/24) ou une plage (192.168.1.1-254)");
    std::uint32_t a = 0, b = 0;
    if (const auto slash = text.find('/'); slash != std::string::npos) {
        int prefix = 0;
        if (!equip::parseIpv4(trimmed(text.substr(0, slash)), a)) return fail("adresse illisible : " + text.substr(0, slash));
        if (!equip::parseMask(trimmed(text.substr(slash + 1)), prefix) || prefix < 8)
            return fail("masque illisible : " + text.substr(slash + 1) + " (/8 \xC3\xA0 /32)");
        const std::uint32_t net = equip::networkOf(a, prefix);
        const std::uint32_t broadcast = net | ~equip::maskOf(prefix);
        if (prefix >= 31) {
            a = net;
            b = broadcast;
        } else {
            a = net + 1;
            b = broadcast - 1;
        }
    } else if (const auto dash = text.find('-'); dash != std::string::npos) {
        if (!equip::parseIpv4(trimmed(text.substr(0, dash)), a)) return fail("adresse illisible : " + text.substr(0, dash));
        const std::string right = trimmed(text.substr(dash + 1));
        if (right.find('.') != std::string::npos) {
            if (!equip::parseIpv4(right, b)) return fail("adresse illisible : " + right);
        } else {
            char* end = nullptr;
            const long n = std::strtol(right.c_str(), &end, 10);
            if (right.empty() || (end && *end) || n < 0 || n > 255) return fail("fin de plage illisible : " + right + " (0 \xC3\xA0 255)");
            b = (a & 0xFFFFFF00u) | static_cast<std::uint32_t>(n);
        }
    } else {
        if (!equip::parseIpv4(text, a)) return fail("adresse illisible : " + text);
        b = a;
    }
    if (b < a) return fail("la plage finit avant de commencer");
    const std::uint64_t n = static_cast<std::uint64_t>(b) - a + 1;
    if (n > kMaxAddresses)
        return fail("trop d'adresses (" + std::to_string(n) + ") : " + std::to_string(kMaxAddresses) + " au plus (un /20)");
    first = a;
    last = b;
    return true;
}

std::string rangeText(std::uint32_t first, std::uint32_t last) {
    const std::uint64_t n = static_cast<std::uint64_t>(last) - first + 1;
    return equip::ipv4Text(first) + (last != first ? " - " + equip::ipv4Text(last) : std::string{}) + " (" + std::to_string(n) + " adresse"
           + (n > 1 ? "s" : "") + ")";
}

bool parsePorts(std::string_view raw, std::vector<int>& out, std::string* why) {
    std::vector<int> ports;
    std::string cur;
    const std::string text = std::string(raw) + ",";
    for (const char c : text) {
        if (c == ',' || c == ';' || c == ' ') {
            if (cur.empty()) continue;
            char* end = nullptr;
            const long n = std::strtol(cur.c_str(), &end, 10);
            if ((end && *end) || n < 1 || n > 65535) {
                if (why) *why = "port illisible : " + cur + " (1 \xC3\xA0 65535)";
                return false;
            }
            if (std::find(ports.begin(), ports.end(), static_cast<int>(n)) == ports.end()) ports.push_back(static_cast<int>(n));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (ports.size() > 32) {
        if (why) *why = "32 ports au plus";
        return false;
    }
    out = std::move(ports);
    return true;
}

std::string portsText(const std::vector<int>& ports) {
    std::string out;
    for (const int p : ports) out += (out.empty() ? "" : ", ") + std::to_string(p);
    return out;
}

std::string serviceName(int port) {
    switch (port) {
        case 21: return "FTP";
        case 22: return "SSH";
        case 23: return "Telnet";
        case 80: case 8080: return "web";
        case 102: return "S7 (Siemens)";
        case 443: return "web (https)";
        case 502: return "Modbus";
        case 554: return "RTSP (cam\xC3\xA9ra)";
        case 1883: return "MQTT";
        case 2404: return "IEC 104";
        case 3389: return "bureau \xC3\xA0 distance";
        case 4840: return "OPC UA";
        case 5020: return "Modbus (d\xC3\xA9mo)";
        case 5900: return "VNC";
        case 9100: return "imprimante";
        case 20000: return "DNP3";
        case 44818: return "EtherNet/IP";
        default: return {};
    }
}

std::string vendorOf(std::string_view mac) {
    std::string hex;
    for (const char c : mac)
        if (std::isxdigit(static_cast<unsigned char>(c))) hex += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (hex.size() < 6) return {};
    const std::string oui = hex.substr(0, 6);
    struct Entry {
        const char* oui;
        const char* vendor;
    };
    static const Entry kTable[] = {
        {"0080F4", "Schneider Electric (T\xC3\xA9l\xC3\xA9m\xC3\xA9" "canique)"},
        {"000054", "Schneider Electric (Modicon)"},
        {"001B1B", "Siemens"},
        {"000E8C", "Siemens"},
        {"080006", "Siemens"},
        {"001FF8", "Siemens"},
        {"0000BC", "Rockwell Automation (Allen-Bradley)"},
        {"001D9C", "Rockwell Automation"},
        {"00A045", "Phoenix Contact"},
        {"0030DE", "WAGO"},
        {"0090E8", "Moxa"},
        {"008063", "Hirschmann"},
        {"000105", "Beckhoff"},
        {"006065", "B&R"},
        {"00000A", "Omron"},
        {"00D0C9", "Advantech"},
        {"B827EB", "Raspberry Pi"},
        {"DCA632", "Raspberry Pi"},
        {"E45F01", "Raspberry Pi"},
        {"005056", "VMware"},
        {"000C29", "VMware"},
        {"00155D", "Microsoft (Hyper-V)"},
        {"525400", "QEMU / KVM"},
        {"080027", "VirtualBox"},
    };
    for (const auto& e : kTable)
        if (oui == e.oui) return e.vendor;
    // Le bit "administree localement" du premier octet.
    const int first = std::stoi(hex.substr(0, 2), nullptr, 16);
    if (first & 0x02) return "adresse locale (machine virtuelle, conteneur)";
    return {};
}


// ============================================================ lecture seule ===
modbus::Frame identificationRequest(std::uint16_t transaction, std::uint8_t unit, std::uint8_t idCode) {
    modbus::Request rq;
    rq.transaction = transaction;
    rq.unit = unit;
    rq.function = modbus::kReadDeviceIdentification;
    rq.idCode = std::clamp<std::uint8_t>(idCode, 1, 4);
    return modbus::encode(rq);
}

bool readOnlyFrame(const modbus::Frame& f, std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    // MBAP (7 octets) + 43, 14, code, objet : 11 octets, ni plus ni moins.
    if (f.size() < 8) return fail("trame trop courte (" + std::to_string(f.size()) + " octets)");
    if (f[2] != 0 || f[3] != 0) return fail("protocole non Modbus dans l'en-t\xC3\xAAte");
    const std::size_t len = static_cast<std::size_t>((f[4] << 8) | f[5]);
    if (len + 6 != f.size()) return fail("longueur de l'en-t\xC3\xAAte incoh\xC3\xA9rente");
    const std::uint8_t fc = f[7];
    if (fc != modbus::kReadDeviceIdentification)
        return fail(modbus::functionName(fc) + " : refus\xC3\xA9" "e, seule la lecture de l'identification (43 / 14) est permise");
    if (f.size() != 11) return fail("identification mal form\xC3\xA9" "e (" + std::to_string(f.size()) + " octets)");
    if (f[8] != 0x0E) return fail("fonction 43, MEI " + std::to_string(f[8]) + " : refus\xC3\xA9, seul le MEI 14 (identification) est permis");
    if (f[9] < 1 || f[9] > 4) return fail("code d'identification " + std::to_string(f[9]) + " hors de 1 \xC3\xA0 4");
    return true;
}

// =================================================================== le temps ===
namespace {

class SystemClock final : public Clock {
public:
    double now() override { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    void sleepUntil(double t) override {
        const double left = t - now();
        if (left > 0) std::this_thread::sleep_for(std::chrono::duration<double>(left));
    }
};

} // namespace

std::shared_ptr<Clock> systemClock() {
    static const std::shared_ptr<Clock> clock = std::make_shared<SystemClock>();
    return clock;
}

Pacer::Pacer(double perSecond, std::shared_ptr<Clock> clock)
    : perSecond_(std::isfinite(perSecond) ? std::clamp(perSecond, 0.1, kMaxRate) : kDefaultRate),
      interval_(1.0 / perSecond_),
      clock_(clock ? std::move(clock) : systemClock()) {}

bool Pacer::wait(const std::atomic<bool>& stop) {
    if (stop) return false;
    double slot = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const double now = clock_->now();
        slot = next_ < 0 ? now : std::max(next_, now);
        next_ = slot + interval_;
    }
    for (;;) {
        if (stop) return false;
        const double now = clock_->now();
        if (now >= slot) break;
        clock_->sleepUntil(std::min(slot, now + 0.05));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ++granted_;
    return true;
}

std::uint64_t Pacer::granted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return granted_;
}

// ================================================================== le reseau ===
namespace {

// La ligne Modbus du vrai reseau : un client, une connexion ; chaque trame est
// reverifiee (readOnlyFrame) avant de partir.
class SystemLine final : public Prober::Line {
public:
    explicit SystemLine(modbus::Client::Settings s) : client_(std::move(s)) {}
    bool open(std::string* why) {
        const auto o = client_.connect();
        if (!o.ok) {
            if (why) *why = o.why;
            return false;
        }
        modbus::labelClient(client_.localName(), "Scanner IP");
        return true;
    }
    modbus::Outcome exchange(const modbus::Frame& request, modbus::Frame& response) override {
        modbus::Outcome o;
        if (!readOnlyFrame(request, &o.why)) {
            response.clear();
            return o;           // jamais envoyee
        }
        return client_.raw(request, response);
    }

private:
    modbus::Client client_;
};

class SystemProber final : public Prober {
public:
    netinfo::PingResult ping(std::uint32_t ip, int timeoutMs) override { return netinfo::ping(equip::ipv4Text(ip), timeoutMs); }
    netinfo::PingResult connect(std::uint32_t ip, int port, int timeoutMs) override { return netinfo::probeTcp(equip::ipv4Text(ip), port, timeoutMs); }
    std::string arp(std::uint32_t ip) override { return netinfo::arpLookup(ip); }
    std::string name(std::uint32_t ip) override { return netinfo::reverseName(ip); }
    std::unique_ptr<Line> modbus(std::uint32_t ip, int port, int timeoutMs, std::string* why) override {
        auto line = std::make_unique<SystemLine>(modbus::Client::Settings{equip::ipv4Text(ip), port, 255, timeoutMs});
        if (!line->open(why)) return nullptr;
        return line;
    }
};

} // namespace

std::shared_ptr<Prober> systemProber() { return std::make_shared<SystemProber>(); }

// ============================================================ la confirmation ===
std::string signature(const Options& o) {
    char rate[32];
    std::snprintf(rate, sizeof rate, "%.3f", o.rate);
    return equip::ipv4Text(o.first) + "-" + equip::ipv4Text(o.last) + "|" + portsText(o.ports) + "|" + std::to_string(o.timeoutMs) + "|"
           + std::to_string(o.threads) + "|" + rate + "|" + (o.names ? "n" : "-") + (o.modbusId ? "m" : "-") + (o.onLink ? "a" : "-");
}

Permit grant(const Options& o, std::string who, std::string when) {
    Permit p;
    p.signature_ = signature(o);
    p.who_ = std::move(who);
    p.when_ = std::move(when);
    p.used_ = std::make_shared<std::atomic<bool>>(false);
    return p;
}

// ================================================================== Scanner ===
Scanner::Scanner(std::shared_ptr<Prober> prober, std::shared_ptr<Clock> clock)
    : prober_(prober ? std::move(prober) : systemProber()), clock_(clock ? std::move(clock) : systemClock()) {}

Scanner::~Scanner() { stop(); }

bool Scanner::start(const Options& options, const Permit& permit, std::string* why) {
    stop();
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    if (options.last < options.first) return fail("plage vide");
    const std::uint64_t n = static_cast<std::uint64_t>(options.last) - options.first + 1;
    if (n > kMaxAddresses) return fail("trop d'adresses (" + std::to_string(n) + ") : " + std::to_string(kMaxAddresses) + " au plus");
    if (!std::isfinite(options.rate) || options.rate < 1 || options.rate > kMaxRate)
        return fail("cadence hors de 1 \xC3\xA0 " + std::to_string(static_cast<int>(kMaxRate)) + " interrogations par seconde");
    if (options.threads < 1 || options.threads > kMaxThreads)
        return fail("de 1 \xC3\xA0 " + std::to_string(kMaxThreads) + " interrogations en parall\xC3\xA8le");
    // La confirmation : un accord, pour CES reglages, pas encore servi.
    if (!permit.valid()) return fail("confirmation requise : rien n'est envoy\xC3\xA9 avant Lancer");
    if (permit.signature() != signature(options)) return fail("les r\xC3\xA9glages ont chang\xC3\xA9 depuis la confirmation : confirmez \xC3\xA0 nouveau");
    if (permit.used_->exchange(true)) return fail("cette confirmation a d\xC3\xA9j\xC3\xA0 servi : un nouveau scan se confirme \xC3\xA0 nouveau");
    options_ = options;
    options_.timeoutMs = std::clamp(options_.timeoutMs, 50, 5000);
    pacer_ = std::make_unique<Pacer>(options_.rate, clock_);
    who_ = permit.who();
    when_ = permit.when();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        found_.clear();
        stopped_ = false;
        t0_ = clock_->now();
        t1_ = 0;
    }
    next_ = 0;
    done_ = 0;
    probes_ = 0;
    stop_ = false;
    running_ = true;
    const int workers = static_cast<int>(std::min<std::uint64_t>(n, static_cast<std::uint64_t>(options_.threads)));
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { work(); });
    watcher_ = std::thread([this] {
        for (auto& t : threads_) t.join();
        threads_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        t1_ = std::max(clock_->now(), t0_ + 1e-9);
        stopped_ = stop_.load();
        running_ = false;
    });
    return true;
}

void Scanner::stop() {
    stop_ = true;
    if (watcher_.joinable()) watcher_.join();
}

void Scanner::work() {
    const std::uint64_t span = static_cast<std::uint64_t>(options_.last) - options_.first + 1;
    while (!stop_) {
        const std::uint64_t i = next_++;
        if (i >= span) break;
        const auto ip = static_cast<std::uint32_t>(options_.first + i);
        Host h = probe(ip);
        if (!h.address.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            found_.push_back(std::move(h));
        }
        if (!stop_) ++done_;
    }
}

bool Scanner::pace() {
    if (!pacer_ || !pacer_->wait(stop_)) return false;
    ++probes_;
    return true;
}

Host Scanner::probe(std::uint32_t ip) {
    Host h;
    h.ip = ip;
    bool alive = false;
    if (!pace()) return Host{};
    const auto p = prober_->ping(ip, options_.timeoutMs);
    if (p.ok) {
        alive = true;
        h.ping = true;
        h.pingMs = p.ms;
        h.how = "ping";
    }
    // Sur le reseau d'un port : l'ARP a repondu meme si le ping est bloque (la table du systeme, lue).
    if (options_.onLink) {
        h.mac = prober_->arp(ip);
        if (!h.mac.empty() && !alive) {
            alive = true;
            h.how = "ARP";
        }
    }
    if (alive || !options_.onLink) {
        for (const int port : options_.ports) {
            if (!pace()) break;
            const auto t = prober_->connect(ip, port, options_.timeoutMs);
            if (t.ok) {
                h.open.push_back(port);
                if (h.tcpMs < 0 || t.ms < h.tcpMs) h.tcpMs = t.ms;
                if (!alive) h.how = "port " + std::to_string(port);
                alive = true;
            } else if (!alive && t.why.find("refus") != std::string::npos) {
                // Une connexion refusee : quelqu'un a repondu.
                alive = true;
                h.how = "TCP (refus)";
            }
        }
    }
    if (!alive) return Host{};
    h.address = equip::ipv4Text(ip);
    if (h.mac.empty()) h.mac = prober_->arp(ip);
    h.vendor = vendorOf(h.mac);
    if (options_.names && !stop_) h.name = prober_->name(ip);
    if (options_.modbusId && h.hasPort(502) && !stop_) identify(h);
    return h;
}

// L'identification Modbus (lecture seule) : l'esclave 255 puis 1 ; la courante (2), sinon la base (1).
void Scanner::identify(Host& h) {
    const int timeout = std::max(500, options_.timeoutMs);
    for (const int unit : {255, 1}) {
        if (!pace()) return;
        std::string why;
        const auto line = prober_->modbus(h.ip, 502, timeout, &why);
        if (!line) return;
        bool nextUnit = false;
        for (const std::uint8_t code : {std::uint8_t{2}, std::uint8_t{1}}) {
            modbus::Request asked;
            asked.transaction = static_cast<std::uint16_t>(++transaction_);
            if (asked.transaction == 0) asked.transaction = static_cast<std::uint16_t>(++transaction_);
            asked.unit = static_cast<std::uint8_t>(unit);
            asked.function = modbus::kReadDeviceIdentification;
            asked.idCode = code;
            const modbus::Frame request = identificationRequest(asked.transaction, asked.unit, code);
            if (!readOnlyFrame(request)) return;          // la liste blanche : jamais autre chose
            if (!pace()) return;
            modbus::Frame response;
            const auto o = line->exchange(request, response);
            modbus::Response r;
            const bool decoded = !response.empty() && modbus::decodeResponse(response.data(), response.size(), asked, r);
            if (!decoded) return;                          // pas de reponse, ou illisible : inutile d'insister
            h.modbusAnswered = true;
            h.modbusUnit = unit;
            if (!r.exception && o.ok) {
                h.identity = r.objects;
                std::string text;
                for (const auto& [object, value] : r.objects)
                    if (!value.empty() && object != 3) text += (text.empty() ? "" : " \xC2\xB7 ") + value;
                h.modbus = text.empty() ? std::string("r\xC3\xA9pond (identification vide)") : text;
                return;
            }
            h.modbus = "r\xC3\xA9pond (identification non servie)";
            if (r.exception == 3 && code == 2) continue;   // la courante n'est pas servie : la base
            nextUnit = true;
            break;
        }
        if (!nextUnit) return;
    }
}

Scanner::Progress Scanner::progress() const {
    Progress p;
    p.total = static_cast<std::size_t>(static_cast<std::uint64_t>(options_.last) - options_.first + 1);
    if (options_.last < options_.first) p.total = 0;
    p.done = std::min(done_.load(), p.total);
    p.probes = probes_.load();
    std::lock_guard<std::mutex> lock(mutex_);
    p.found = found_.size();
    p.seconds = t0_ > 0 ? ((t1_ > 0 ? t1_ : clock_->now()) - t0_) : 0;
    p.finished = !running_ && t1_ > 0 && !stopped_;
    p.stopped = !running_ && stopped_;
    return p;
}

std::vector<Host> Scanner::results() const {
    std::vector<Host> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out = found_;
    }
    std::sort(out.begin(), out.end(), [](const Host& a, const Host& b) { return a.ip < b.ip; });
    return out;
}

std::string Scanner::csv() const {
    std::string out = "adresse;mac;fabricant;nom;ping_ms;ports;modbus;vu_par\n";
    const auto quote = [](const std::string& s) {
        if (s.find_first_of(";\"\n") == std::string::npos) return s;
        std::string q = "\"";
        for (const char c : s) q += c == '"' ? std::string("\"\"") : std::string(1, c);
        return q + "\"";
    };
    for (const auto& h : results()) {
        char ms[32] = {0};
        if (h.ping) std::snprintf(ms, sizeof ms, "%.1f", h.pingMs);
        out += h.address + ";" + h.mac + ";" + quote(h.vendor) + ";" + quote(h.name) + ";" + ms + ";" + quote(h.portsText()) + ";" + quote(h.modbus)
               + ";" + h.how + "\n";
    }
    return out;
}

} // namespace hmi::ipscan
