// hmi/HmiIpScan.cpp - le scanner IP (lot 15).
#include "HmiIpScan.hpp"

#include "HmiEquipment.hpp"
#include "HmiModbus.hpp"
#include "HmiNetInfo.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace hmi::ipscan {

namespace {

double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

constexpr std::uint64_t kMaxAddresses = 4096;

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

// ================================================================== Scanner ===
Scanner::~Scanner() { stop(); }

bool Scanner::start(const Options& options, std::string* why) {
    stop();
    if (options.last < options.first) {
        if (why) *why = "plage vide";
        return false;
    }
    const std::uint64_t n = static_cast<std::uint64_t>(options.last) - options.first + 1;
    if (n > kMaxAddresses) {
        if (why) *why = "trop d'adresses (" + std::to_string(n) + ") : " + std::to_string(kMaxAddresses) + " au plus";
        return false;
    }
    options_ = options;
    options_.threads = std::clamp(options_.threads, 1, 128);
    options_.timeoutMs = std::clamp(options_.timeoutMs, 50, 5000);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        found_.clear();
        stopped_ = false;
        t0_ = nowS();
        t1_ = 0;
    }
    next_ = 0;
    done_ = 0;
    stop_ = false;
    running_ = true;
    const int workers = static_cast<int>(std::min<std::uint64_t>(n, static_cast<std::uint64_t>(options_.threads)));
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { work(); });
    watcher_ = std::thread([this] {
        for (auto& t : threads_) t.join();
        threads_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        t1_ = nowS();
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
        ++done_;
    }
}

Host Scanner::probe(std::uint32_t ip) const {
    Host h;
    h.ip = ip;
    const std::string address = equip::ipv4Text(ip);
    const auto p = netinfo::ping(address, options_.timeoutMs);
    bool alive = p.ok;
    if (p.ok) {
        h.ping = true;
        h.pingMs = p.ms;
        h.how = "ping";
    }
    // Sur le reseau d'un port : l'ARP a repondu meme si le ping est bloque.
    if (options_.onLink) {
        h.mac = netinfo::arpLookup(ip);
        if (!h.mac.empty() && !alive) {
            alive = true;
            h.how = "ARP";
        }
    }
    if (alive || !options_.onLink) {
        for (const int port : options_.ports) {
            if (stop_) break;
            const auto t = netinfo::probeTcp(address, port, options_.timeoutMs);
            if (t.ok) {
                h.open.push_back(port);
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
    h.address = address;
    if (h.mac.empty()) h.mac = netinfo::arpLookup(ip);
    h.vendor = vendorOf(h.mac);
    if (options_.names && !stop_) h.name = netinfo::reverseName(ip);
    if (options_.modbusId && h.hasPort(502) && !stop_) {
        for (const int unit : {255, 1}) {
            modbus::Client c({address, 502, unit, std::max(500, options_.timeoutMs)});
            if (!c.connect().ok) break;
            modbus::Identification id;
            const auto o = c.readIdentification(id);
            if (o.ok) {
                std::string text;
                for (const auto& [object, value] : id)
                    if (!value.empty() && object != 3) text += (text.empty() ? "" : " \xC2\xB7 ") + value;
                h.modbus = text.empty() ? std::string("r\xC3\xA9pond (identification vide)") : text;
                h.modbusUnit = unit;
                break;
            }
            if (!o.exception) break;           // coupe : inutile d'essayer l'esclave 1
            h.modbus = "r\xC3\xA9pond (identification non servie)";
            h.modbusUnit = unit;
        }
    }
    return h;
}

Scanner::Progress Scanner::progress() const {
    Progress p;
    p.total = static_cast<std::size_t>(static_cast<std::uint64_t>(options_.last) - options_.first + 1);
    if (options_.last < options_.first) p.total = 0;
    p.done = std::min(done_.load(), p.total);
    std::lock_guard<std::mutex> lock(mutex_);
    p.found = found_.size();
    p.seconds = t0_ > 0 ? ((t1_ > 0 ? t1_ : nowS()) - t0_) : 0;
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
