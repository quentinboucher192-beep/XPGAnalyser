// hmi/HmiDiscovery.cpp - 1.21.0 : la decouverte des equipements, sans intrusion.
#include "HmiDiscovery.hpp"

#include "HmiEditHistory.hpp"   // currentSignature : le compte, le PC
#include "HmiEquipment.hpp"
#include "HmiHistory.hpp"       // wallStamp, csvField, csvSplit

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>

namespace hmi::discovery {

namespace fs = std::filesystem;
namespace eq = hmi::equip;

namespace {

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool iequal(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

// Un nom compare sans casse ni separateurs : Centrale_1 == centrale-1 == CENTRALE 1.
std::string folded(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string join(const std::vector<std::string>& list, std::string_view sep) {
    std::string out;
    for (const auto& s : list) {
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

std::string msText(double ms) {
    char b[32];
    std::snprintf(b, sizeof b, ms < 10 ? "%.1f ms" : "%.0f ms", ms);
    return b;
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// L'objet `id` de l'identification annoncee ; vide : absent.
std::string objectOf(const modbus::Identification& id, std::uint8_t object) {
    for (const auto& [k, v] : id)
        if (k == object && !trimmed(v).empty()) return trimmed(v);
    return {};
}

// Le protocole que designe un port ouvert (une supposition : seul le port est vu).
std::string protocolOfPort(int port) {
    switch (port) {
        case 102: return "S7 (ISO-TSAP)";
        case 44818: return "EtherNet/IP";
        case 4840: return "OPC UA";
        case 80: case 8080: return "HTTP";
        case 443: return "HTTPS";
        case 9100: return "impression (RAW)";
        case 2404: return "IEC 60870-5-104";
        case 20000: return "DNP3";
        case 1883: return "MQTT";
        default: {
            const std::string s = ipscan::serviceName(port);
            return s.empty() ? "TCP " + std::to_string(port) : s;
        }
    }
}

bool industrialPort(int port) { return port == 502 || port == 102 || port == 44818 || port == 2404 || port == 20000; }

constexpr char kForbidden[] = "\"';|$\n\t";   // ce qu'un nom d'equipement ne porte pas (creer_equipement)

} // namespace

// =========================================================== les protocoles ===
const std::vector<Protocol>& protocols() {
    static const std::vector<Protocol> list = {
        {"modbus", "Modbus TCP", {502}, true,
         "automates, variateurs, centrales : la connexion, puis l'identification en lecture seule (fonction 43)"},
        {"s7", "S7 (Siemens)", {102}, true, "le port seul : aucune requ\xC3\xAAte S7"},
        {"enip", "EtherNet/IP", {44818}, true, "le port seul : aucune requ\xC3\xAAte CIP"},
        {"opcua", "OPC UA", {4840}, true, "le port seul : aucune session OPC UA"},
        {"web", "Web (HTTP, HTTPS)", {80, 443}, true, "le port seul : aucune page lue"},
        {"imprimante", "Imprimantes", {9100}, true, "le port seul : rien n'est imprim\xC3\xA9"},
        {"iec104", "IEC 60870-5-104", {2404}, false, "le port seul"},
        {"dnp3", "DNP3", {20000}, false, "le port seul"},
        {"mqtt", "MQTT", {1883}, false, "le port seul"},
    };
    return list;
}

const Protocol* protocol(std::string_view key) {
    const std::string k = folded(key);
    if (k.empty()) return nullptr;
    // Les noms que l'on rencontre (l'IA, un utilisateur) : sans casse, sans espaces ni ponctuation.
    static const std::pair<const char*, const char*> kAliases[] = {
        {"modbustcp", "modbus"},   {"modbustcpip", "modbus"},       {"mbtcp", "modbus"},       {"siemens", "s7"},
        {"s7comm", "s7"},          {"isotsap", "s7"},               {"ethernetip", "enip"},    {"cip", "enip"},
        {"http", "web"},           {"https", "web"},                {"printer", "imprimante"}, {"imprimantes", "imprimante"},
        {"iec608705104", "iec104"},
    };
    std::string target = k;
    for (const auto& [alias, canonical] : kAliases)
        if (k == alias) target = canonical;
    for (const auto& p : protocols())
        if (folded(p.key) == target || folded(p.label) == k) return &p;
    return nullptr;
}

std::vector<std::string> defaultProtocols() {
    std::vector<std::string> out;
    for (const auto& p : protocols())
        if (p.byDefault) out.push_back(p.key);
    return out;
}

std::string protocolText(const Protocol& p) { return p.label + " (" + ipscan::portsText(p.ports) + ")"; }

// ========================================================== les interfaces ===
std::vector<Interface> interfaces(const std::vector<netinfo::Adapter>& adapters) {
    std::vector<Interface> out;
    for (const auto& a : adapters) {
        Interface i;
        i.name = a.name;
        i.ip = a.ip();
        i.prefix = a.prefix();
        i.up = a.up;
        i.usable = a.up && a.ip() != 0 && !a.automaticAddress();
        i.label = a.name + (a.ip() ? " - " + eq::ipv4Text(a.ip()) + " / " + std::to_string(a.prefix()) : std::string{});
        if (!a.up) i.label += " (d\xC3\xA9" "branch\xC3\xA9" "e)";
        else if (!a.ip()) i.label += " (sans adresse IPv4)";
        else if (a.automaticAddress()) i.label += " (adresse automatique)";
        out.push_back(std::move(i));
    }
    return out;
}

std::string defaultInterface(const std::vector<netinfo::Adapter>& adapters) {
    for (const auto& a : adapters)
        if (a.kind == netinfo::Adapter::Kind::Ethernet && a.up && a.ip() && !a.automaticAddress()) return a.name;
    for (const auto& a : adapters)
        if (a.up && a.ip() && !a.automaticAddress()) return a.name;
    return {};
}

bool useInterface(Request& r, const std::vector<netinfo::Adapter>& adapters, std::string_view name, std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    const auto it = std::find_if(adapters.begin(), adapters.end(), [&](const netinfo::Adapter& a) { return a.name == name; });
    if (it == adapters.end()) return fail("interface inconnue : " + std::string(name));
    if (!it->ip()) return fail(std::string(name) + " n'a pas d'adresse IPv4");
    r.adapter = it->name;
    // Un reseau de plus de 4096 adresses : le /24 du PC (la plage se change a la main).
    const int prefix = it->prefix() < 20 ? 24 : it->prefix();
    const std::uint32_t net = eq::networkOf(it->ip(), prefix);
    const std::uint32_t broadcast = net | ~eq::maskOf(prefix);
    r.start = eq::ipv4Text(prefix >= 31 ? net : net + 1);
    r.end = eq::ipv4Text(prefix >= 31 ? broadcast : broadcast - 1);
    r.mask = std::to_string(prefix);
    return true;
}

bool applyMask(Request& r, std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    std::uint32_t a = 0;
    if (!eq::parseIpv4(trimmed(r.start), a)) return fail("adresse de d\xC3\xA9" "but illisible : " + r.start);
    int prefix = 0;
    if (!eq::parseMask(r.mask, prefix) || prefix < 8) return fail("masque illisible : " + r.mask + " (/8 \xC3\xA0 /32, ou 255.255.255.0)");
    const std::uint32_t net = eq::networkOf(a, prefix);
    const std::uint32_t broadcast = net | ~eq::maskOf(prefix);
    r.start = eq::ipv4Text(prefix >= 31 ? net : net + 1);
    r.end = eq::ipv4Text(prefix >= 31 ? broadcast : broadcast - 1);
    return true;
}

bool setRange(Request& r, std::string_view text, std::string* why) {
    std::uint32_t a = 0, b = 0;
    if (!ipscan::parseRange(text, a, b, why)) return false;
    r.start = eq::ipv4Text(a);
    r.end = eq::ipv4Text(b);
    const std::string t = trimmed(text);
    if (const auto slash = t.find('/'); slash != std::string::npos) {
        int prefix = 0;
        if (eq::parseMask(t.substr(slash + 1), prefix)) r.mask = std::to_string(prefix);
    } else {
        r.mask.clear();
    }
    return true;
}

bool requestFrom(std::string_view range, const std::vector<std::string>& protocolNames, double rate, Request& out,
                 const std::vector<netinfo::Adapter>& adapters, std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    Request r;
    r.origin = "Assistant IA";
    if (!setRange(r, range, why)) return false;
    if (!protocolNames.empty()) {
        std::vector<std::string> keys;
        for (const auto& name : protocolNames) {
            const Protocol* p = protocol(name);
            if (!p) return fail("protocole inconnu : \xC2\xAB " + name + " \xC2\xBB (modbus, s7, enip, opcua, web, imprimante, iec104, dnp3, mqtt)");
            keys.push_back(p->key);
        }
        // Dans l'ordre du tableau : le resume et les ports restent stables.
        r.protocols.clear();
        for (const auto& p : protocols())
            if (std::find(keys.begin(), keys.end(), p.key) != keys.end()) r.protocols.push_back(p.key);
    }
    if (!std::isfinite(rate) || rate < 0 || rate > ipscan::kMaxRate)
        return fail("cadence : de 1 \xC3\xA0 " + std::to_string(static_cast<int>(ipscan::kMaxRate)) + " interrogations par seconde (0 : 20 par d\xC3\xA9" "faut)");
    r.rate = rate > 0 ? std::max(1.0, rate) : ipscan::kDefaultRate;
    std::uint32_t a = 0, b = 0;
    (void)eq::parseIpv4(r.start, a);
    (void)eq::parseIpv4(r.end, b);
    for (const auto& ad : adapters)
        for (const auto& [ip, pre] : ad.addresses)
            if (r.adapter.empty() && eq::sameNetwork(ip, a, pre) && eq::sameNetwork(ip, b, pre)) r.adapter = ad.name;
    out = std::move(r);
    return true;
}

// ================================================================== le plan ===
Estimate estimate(const ipscan::Options& o) {
    Estimate e;
    if (o.last < o.first) return e;
    const std::uint64_t n = static_cast<std::uint64_t>(o.last) - o.first + 1;
    const std::uint64_t ports = o.ports.size();
    const std::uint64_t modbus = o.modbusId && contains(o.ports, 502) ? 6 : 0;   // 2 esclaves : une connexion, 2 requetes
    e.lowProbes = o.onLink ? n : n * (1 + ports);
    e.highProbes = n * (1 + ports + modbus);
    const double rate = std::max(0.1, o.rate);
    const double wait = std::max(50, o.timeoutMs) / 1000.0;
    const int threads = std::max(1, o.threads);
    // Personne ne repond : chaque interrogation attend son delai, `threads` a la fois ; la cadence borne aussi.
    e.lowSeconds = std::max(static_cast<double>(e.lowProbes) / rate, static_cast<double>(e.lowProbes) * wait / threads);
    // Tout repond, chaque port ouvert : la cadence.
    e.highSeconds = std::max(e.lowSeconds, static_cast<double>(e.highProbes) / rate);
    return e;
}

std::string durationText(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) return "inconnue";
    if (seconds < 1) return "moins d'une seconde";
    const auto s = static_cast<long long>(std::llround(seconds));
    char b[48];
    if (s < 60) std::snprintf(b, sizeof b, "%lld s", s);
    else if (s < 3600) std::snprintf(b, sizeof b, "%lld min %02lld s", s / 60, s % 60);
    else std::snprintf(b, sizeof b, "%lld h %02lld min", s / 3600, (s % 3600) / 60);
    return b;
}

Plan plan(const Request& r, const std::vector<netinfo::Adapter>& adapters) {
    Plan p;
    const auto fail = [&](std::string m) {
        p.error = std::move(m);
        return p;
    };
    std::uint32_t a = 0, b = 0;
    if (!eq::parseIpv4(trimmed(r.start), a)) return fail("adresse de d\xC3\xA9" "but illisible : \xC2\xAB " + r.start + " \xC2\xBB");
    if (!eq::parseIpv4(trimmed(r.end), b)) return fail("adresse de fin illisible : \xC2\xAB " + r.end + " \xC2\xBB");
    if (b < a) return fail("la plage finit avant de commencer (" + r.start + " - " + r.end + ")");
    const std::uint64_t n = static_cast<std::uint64_t>(b) - a + 1;
    if (n > ipscan::kMaxAddresses)
        return fail("trop d'adresses (" + std::to_string(n) + ") : " + std::to_string(ipscan::kMaxAddresses) + " au plus (un /20) - r\xC3\xA9" "duisez la plage");
    if ((a >> 24) == 0 || (b >> 24) >= 224) return fail("adresse r\xC3\xA9serv\xC3\xA9" "e ou de multidiffusion : seules les adresses d'appareils (1.x.x.x \xC3\xA0 223.x.x.x) se scannent");
    // Le masque (facultatif).
    if (!trimmed(r.mask).empty()) {
        int prefix = 0;
        if (!eq::parseMask(r.mask, prefix) || prefix < 8) return fail("masque illisible : \xC2\xAB " + r.mask + " \xC2\xBB (/8 \xC3\xA0 /32, ou 255.255.255.0)");
        p.maskText = "/" + std::to_string(prefix) + " (" + eq::maskText(prefix) + ")";
        if (!eq::sameNetwork(a, b, prefix)) p.notes.push_back("la plage d\xC3\xA9" "borde du r\xC3\xA9seau /" + std::to_string(prefix) + " de l'adresse de d\xC3\xA9" "but");
    }
    // Les protocoles, puis les ports en plus.
    std::vector<int> ports;
    const auto addPort = [&](int port) {
        if (!contains(ports, port)) ports.push_back(port);
    };
    for (const auto& key : r.protocols) {
        const Protocol* pr = protocol(key);
        if (!pr) return fail("protocole inconnu : \xC2\xAB " + key + " \xC2\xBB");
        if (std::any_of(p.protocolLabels.begin(), p.protocolLabels.end(), [&](const std::string& s) { return s == protocolText(*pr); })) continue;
        p.protocolLabels.push_back(protocolText(*pr));
        for (const int port : pr->ports) addPort(port);
    }
    if (!trimmed(r.extraPorts).empty()) {
        std::vector<int> extra;
        std::string why;
        if (!ipscan::parsePorts(r.extraPorts, extra, &why)) return fail("ports en plus : " + why);
        for (const int port : extra) addPort(port);
        p.protocolLabels.push_back("autres ports (" + ipscan::portsText(extra) + ")");
    }
    if (ports.size() > 32) return fail("trop de ports (" + std::to_string(ports.size()) + ") : 32 au plus");
    if (!std::isfinite(r.rate) || r.rate < 1 || r.rate > ipscan::kMaxRate)
        return fail("cadence : de 1 \xC3\xA0 " + std::to_string(static_cast<int>(ipscan::kMaxRate)) + " interrogations par seconde");
    if (r.threads < 1 || r.threads > ipscan::kMaxThreads)
        return fail("interrogations en parall\xC3\xA8le : de 1 \xC3\xA0 " + std::to_string(ipscan::kMaxThreads));
    if (r.timeoutMs < 50 || r.timeoutMs > 5000) return fail("d\xC3\xA9lai : de 50 \xC3\xA0 5000 ms");
    // L'interface : celle qui a donne la plage ; les paquets suivent la table de routage du systeme.
    if (!trimmed(r.adapter).empty()) {
        const auto it = std::find_if(adapters.begin(), adapters.end(), [&](const netinfo::Adapter& x) { return x.name == trimmed(r.adapter); });
        if (it == adapters.end()) return fail("interface inconnue : \xC2\xAB " + r.adapter + " \xC2\xBB");
        p.interfaceText = it->name + (it->ip() ? " (" + eq::ipv4Text(it->ip()) + " / " + std::to_string(it->prefix()) + ")" : std::string(" (sans adresse)"));
        bool inside = false;
        for (const auto& [ip, pre] : it->addresses)
            if (eq::sameNetwork(ip, a, pre) && eq::sameNetwork(ip, b, pre)) inside = true;
        if (!inside)
            p.notes.push_back("la plage n'est pas dans le r\xC3\xA9seau de " + it->name + " : les interrogations suivent la table de routage du syst\xC3\xA8me");
    } else {
        p.interfaceText = "aucune (plage libre)";
    }
    p.options.first = a;
    p.options.last = b;
    p.options.ports = ports;
    p.options.timeoutMs = r.timeoutMs;
    p.options.threads = r.threads;
    p.options.names = r.names;
    p.options.modbusId = r.modbusId && contains(ports, 502);
    p.options.rate = r.rate;
    p.options.onLink = false;
    for (const auto& ad : adapters)
        for (const auto& [ip, pre] : ad.addresses)
            if (eq::sameNetwork(ip, a, pre) && eq::sameNetwork(ip, b, pre)) p.options.onLink = true;
    if (!p.options.onLink)
        p.notes.push_back("hors des r\xC3\xA9seaux du PC : pas d'ARP, chaque port est essay\xC3\xA9 \xC3\xA0 chaque adresse (plus long)");
    if (ports.empty()) p.notes.push_back("aucun port : le ping seul (et la table ARP)");
    p.addresses = static_cast<std::size_t>(n);
    p.rangeText = ipscan::rangeText(a, b);
    const Estimate e = estimate(p.options);
    p.lowProbes = e.lowProbes;
    p.highProbes = e.highProbes;
    p.lowSeconds = e.lowSeconds;
    p.highSeconds = e.highSeconds;
    return p;
}

std::vector<std::pair<std::string, std::string>> summary(const Plan& p) {
    std::vector<std::pair<std::string, std::string>> out;
    out.emplace_back("Interface", p.interfaceText);
    out.emplace_back("Plage", p.rangeText);
    out.emplace_back("Masque", p.maskText.empty() ? std::string("aucun") : p.maskText);
    out.emplace_back("Protocoles", p.protocolLabels.empty() ? std::string("aucun (le ping seul)") : join(p.protocolLabels, ", "));
    out.emplace_back("Ports TCP", p.options.ports.empty() ? std::string("aucun") : ipscan::portsText(p.options.ports));
    out.emplace_back("Adresses", std::to_string(p.addresses));
    const std::string low = durationText(p.lowSeconds), high = durationText(p.highSeconds);
    out.emplace_back("Dur\xC3\xA9" "e estim\xC3\xA9" "e", low == high ? low : "de " + low + " \xC3\xA0 " + high);
    char rate[32];
    std::snprintf(rate, sizeof rate, "%g", p.options.rate);
    out.emplace_back("Cadence", std::string(rate) + " interrogations par seconde au plus, " + std::to_string(p.options.threads) + " en parall\xC3\xA8le, d\xC3\xA9lai "
                                    + std::to_string(p.options.timeoutMs) + " ms");
    out.emplace_back("Envoy\xC3\xA9", std::string("ping, connexion TCP referm\xC3\xA9" "e aussit\xC3\xB4t")
                                         + (p.options.modbusId ? std::string(", lecture de l'identification Modbus (fonction 43)") : std::string{})
                                         + (p.options.names ? std::string(", nom (DNS inverse)") : std::string{})
                                         + " ; aucune \xC3\xA9" "criture, aucune commande");
    return out;
}

std::string summaryText(const Plan& p) {
    std::string out;
    for (const auto& [k, v] : summary(p)) out += k + " : " + v + "\n";
    for (const auto& n : p.notes) out += "Note : " + n + "\n";
    if (!p.ok()) out += "Refus\xC3\xA9 : " + p.error + "\n";
    return out;
}

// ------------------------------------------------------------- la demande en texte ---
std::string encodeRequest(const Request& r) {
    const auto clean = [](std::string s) {
        for (auto& c : s)
            if (c == '\n' || c == '\r') c = ' ';
        return s;
    };
    char rate[32];
    std::snprintf(rate, sizeof rate, "%g", r.rate);
    std::string out;
    out += "interface=" + clean(r.adapter) + "\n";
    out += "debut=" + clean(r.start) + "\n";
    out += "fin=" + clean(r.end) + "\n";
    out += "masque=" + clean(r.mask) + "\n";
    out += "protocoles=" + clean(join(r.protocols, ",")) + "\n";
    out += "ports=" + clean(r.extraPorts) + "\n";
    out += std::string("cadence=") + rate + "\n";
    out += "fils=" + std::to_string(r.threads) + "\n";
    out += "delai=" + std::to_string(r.timeoutMs) + "\n";
    out += std::string("noms=") + (r.names ? "oui" : "non") + "\n";
    out += std::string("modbus=") + (r.modbusId ? "oui" : "non") + "\n";
    out += "origine=" + clean(r.origin) + "\n";
    return out;
}

bool decodeRequest(std::string_view text, Request& out, std::string* why) {
    Request r;
    bool any = false;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eqAt = line.find('=');
        if (eqAt == std::string::npos) continue;
        const std::string k = trimmed(line.substr(0, eqAt));
        const std::string v = trimmed(line.substr(eqAt + 1));
        const auto number = [&](double& x) {
            char* end = nullptr;
            const double d = std::strtod(v.c_str(), &end);
            if (v.empty() || (end && *end)) return false;
            x = d;
            return true;
        };
        any = true;
        if (k == "interface") r.adapter = v;
        else if (k == "debut") r.start = v;
        else if (k == "fin") r.end = v;
        else if (k == "masque") r.mask = v;
        else if (k == "protocoles") {
            r.protocols.clear();
            std::string cur;
            for (const char c : v + ",") {
                if (c == ',') {
                    if (!trimmed(cur).empty()) r.protocols.push_back(lower(trimmed(cur)));
                    cur.clear();
                } else {
                    cur += c;
                }
            }
        } else if (k == "ports") r.extraPorts = v;
        else if (k == "cadence") {
            if (!number(r.rate)) {
                if (why) *why = "cadence illisible : " + v;
                return false;
            }
        } else if (k == "fils" || k == "delai") {
            double d = 0;
            if (!number(d)) {
                if (why) *why = k + " illisible : " + v;
                return false;
            }
            (k == "fils" ? r.threads : r.timeoutMs) = static_cast<int>(d);
        } else if (k == "noms") r.names = v == "oui" || v == "1" || v == "vrai";
        else if (k == "modbus") r.modbusId = v == "oui" || v == "1" || v == "vrai";
        else if (k == "origine") r.origin = v;
    }
    if (!any) {
        if (why) *why = "demande vide";
        return false;
    }
    out = std::move(r);
    return true;
}

// ================================================================= la fiche ===
std::string_view certaintyText(Certainty c) noexcept {
    switch (c) {
        case Certainty::Confirmed: return "confirm\xC3\xA9";
        case Certainty::Announced: return "annonc\xC3\xA9";
        case Certainty::Supposed: return "suppos\xC3\xA9";
        case Certainty::Unknown: return "non identifi\xC3\xA9";
    }
    return {};
}

const Fact* Device::fact(std::string_view key) const {
    for (const auto& f : facts)
        if (f.key == key) return &f;
    return nullptr;
}

std::vector<const Fact*> Device::all(std::string_view key) const {
    std::vector<const Fact*> out;
    for (const auto& f : facts)
        if (f.key == key) out.push_back(&f);
    return out;
}

std::string valueWithCertainty(const std::string& value, Certainty c) {
    switch (c) {
        case Certainty::Confirmed: return value;
        case Certainty::Announced: return value + " (annonc\xC3\xA9)";
        case Certainty::Supposed: return value + " (suppos\xC3\xA9)";
        case Certainty::Unknown: return value.empty() ? std::string("non identifi\xC3\xA9") : value;
    }
    return value;
}

Device classify(const ipscan::Host& h) {
    Device d;
    d.host = h;
    const auto add = [&](std::string key, std::string label, std::string value, Certainty c, std::string source) {
        d.facts.push_back(Fact{std::move(key), std::move(label), std::move(value), c, std::move(source)});
    };
    // L'adresse et l'accessibilite : il a repondu (observe).
    std::string seen = h.how;
    if (h.how == "ping") seen = "r\xC3\xA9pond au ping";
    else if (h.how == "ARP") seen = "table ARP (le ping ne r\xC3\xA9pond pas)";
    else if (h.how.rfind("port ", 0) == 0) seen = "connexion au " + h.how;
    else if (h.how == "TCP (refus)") seen = "connexion refus\xC3\xA9" "e : il r\xC3\xA9pond";
    add("ip", "Adresse IP", h.address, Certainty::Confirmed, seen);
    add("acces", "Accessibilit\xC3\xA9", std::string("confirm\xC3\xA9" "e") + (h.ping ? "" : " (sans r\xC3\xA9ponse au ping)"), Certainty::Confirmed, seen);
    if (h.ping && h.pingMs >= 0) add("temps", "Temps de r\xC3\xA9ponse", msText(h.pingMs), Certainty::Confirmed, "ping");
    else if (h.tcpMs >= 0) add("temps", "Temps de r\xC3\xA9ponse", msText(h.tcpMs), Certainty::Confirmed, "connexion TCP");
    if (!h.mac.empty()) add("mac", "Adresse MAC", h.mac, Certainty::Confirmed, "table ARP");
    else add("mac", "Adresse MAC", "inconnue", Certainty::Unknown, "hors du r\xC3\xA9seau local, ou absente de la table ARP");
    if (!h.name.empty()) add("nom", "Nom d'h\xC3\xB4te", h.name, Certainty::Announced, "DNS inverse");
    // Le constructeur : annonce par l'appareil, sinon suppose d'apres la MAC.
    if (const std::string v = objectOf(h.identity, 0); !v.empty()) {
        d.vendor = v;
        d.vendorCertainty = Certainty::Announced;
        add("constructeur", "Constructeur", v, Certainty::Announced, "identification Modbus (fonction 43)");
    } else if (!h.vendor.empty()) {
        d.vendor = h.vendor;
        d.vendorCertainty = Certainty::Supposed;
        add("constructeur", "Constructeur", h.vendor, Certainty::Supposed, "pr\xC3\xA9" "fixe de l'adresse MAC (OUI)");
    } else {
        add("constructeur", "Constructeur", "non identifi\xC3\xA9", Certainty::Unknown, "rien d'annonc\xC3\xA9, pr\xC3\xA9" "fixe MAC inconnu");
    }
    // Le modele : seulement s'il est annonce - jamais devine.
    std::string model = objectOf(h.identity, 4);
    if (model.empty()) model = objectOf(h.identity, 5);
    if (model.empty()) model = objectOf(h.identity, 1);
    if (!model.empty()) {
        d.model = model;
        d.modelCertainty = Certainty::Announced;
        add("modele", "Mod\xC3\xA8le", model, Certainty::Announced, "identification Modbus (fonction 43)");
    } else {
        add("modele", "Mod\xC3\xA8le", "non identifi\xC3\xA9", Certainty::Unknown, "aucun mod\xC3\xA8le annonc\xC3\xA9 : il n'est pas devin\xC3\xA9");
    }
    if (const std::string v = objectOf(h.identity, 2); !v.empty()) add("version", "Version", v, Certainty::Announced, "identification Modbus (fonction 43)");
    // Les protocoles : Modbus confirme par une reponse ; les autres d'apres leur seul port.
    d.modbus = h.modbusAnswered || h.hasPort(502);
    if (h.ping) add("protocole", "Protocole", "ICMP (ping)", Certainty::Confirmed, "r\xC3\xA9ponse au ping");
    if (h.modbusAnswered) add("protocole", "Protocole", "Modbus TCP", Certainty::Confirmed, "r\xC3\xA9ponse Modbus (fonction 43)");
    else if (h.hasPort(502)) add("protocole", "Protocole", "Modbus TCP", Certainty::Supposed, "port 502 ouvert, aucune r\xC3\xA9ponse Modbus lue");
    for (const int port : h.open) {
        if (port == 502) continue;
        add("protocole", "Protocole", protocolOfPort(port), Certainty::Supposed, "port " + std::to_string(port) + " ouvert (le protocole n'est pas v\xC3\xA9rifi\xC3\xA9)");
    }
    add("ports", "Ports ouverts", h.open.empty() ? std::string("aucun des ports essay\xC3\xA9s") : ipscan::portsText(h.open), Certainty::Confirmed, "connexion TCP");
    if (!h.open.empty()) add("services", "Services", h.portsText(), Certainty::Supposed, "d'apr\xC3\xA8s les num\xC3\xA9ros de port");
    // Le type : toujours une supposition.
    d.industrial = h.modbusAnswered || std::any_of(h.open.begin(), h.open.end(), industrialPort);
    std::string type;
    std::string why;
    if (d.industrial) {
        type = "automate ou \xC3\xA9quipement industriel probable";
        why = h.modbusAnswered ? "r\xC3\xA9pond en Modbus TCP" : "port industriel ouvert (" + ipscan::portsText(h.open) + ")";
    } else if (h.hasPort(4840)) {
        type = "serveur OPC UA probable";
        why = "port 4840 ouvert";
    } else if (h.hasPort(9100)) {
        type = "imprimante probable";
        why = "port 9100 ouvert";
    } else if (h.hasPort(80) || h.hasPort(443) || h.hasPort(8080)) {
        type = "\xC3\xA9quipement avec une interface web probable";
        why = "port web ouvert";
    } else if (h.hasPort(1883)) {
        type = "serveur MQTT probable";
        why = "port 1883 ouvert";
    }
    if (!type.empty()) {
        d.type = type;
        d.typeCertainty = Certainty::Supposed;
        add("type", "Type", type, Certainty::Supposed, why);
    } else {
        add("type", "Type", "non identifi\xC3\xA9", Certainty::Unknown, "aucun port r\xC3\xA9v\xC3\xA9lateur");
    }
    if (!h.modbus.empty()) {
        const bool announced = !h.identity.empty();
        add("identification", "Identification annonc\xC3\xA9" "e", h.modbus + (h.modbusUnit >= 0 ? " (esclave " + std::to_string(h.modbusUnit) + ")" : std::string{}),
            announced ? Certainty::Announced : Certainty::Confirmed, "fonction 43, en lecture seule");
    }
    return d;
}

std::string deviceText(const Device& d) {
    std::string out;
    for (const auto& f : d.facts) {
        out += f.label + " : " + f.value;
        if (f.certainty != Certainty::Unknown || f.value != "non identifi\xC3\xA9") {
            out += " (" + std::string(certaintyText(f.certainty));
            if (!f.source.empty()) out += ", " + f.source;
            out += ")";
        }
        out += "\n";
    }
    return out;
}

// ============================================================ la comparaison ===
std::vector<Declared> declared(const Project& p, bool withPlc) {
    std::vector<Declared> out;
    if (withPlc && p.comm.modbus()) {
        Declared d;
        d.plc = true;
        d.name = "automate du projet";
        d.host = trimmed(p.comm.host);
        (void)eq::parseIpv4(d.host, d.ip);
        d.port = p.comm.port;
        d.unit = p.comm.unit;
        d.modbus = true;
        out.push_back(std::move(d));
    }
    for (const auto& e : p.equipments) {
        Declared d;
        d.id = e.id;
        d.name = e.name;
        d.host = trimmed(e.host);
        if (!eq::parseIpv4(d.host, d.ip)) d.ip = 0;
        d.port = e.port;
        d.unit = e.unit;
        d.modbus = e.modbus();
        d.simulatedOnly = e.simulated;
        d.description = e.description;
        out.push_back(std::move(d));
    }
    return out;
}

std::string_view statusText(Status s) noexcept {
    switch (s) {
        case Status::Known: return "d\xC3\xA9" "clar\xC3\xA9";
        case Status::New: return "absent du projet";
        case Status::Unreachable: return "d\xC3\xA9" "clar\xC3\xA9 mais injoignable";
        case Status::Moved: return "adresse diff\xC3\xA9rente";
        case Status::Duplicate: return "doublon";
        case Status::Inconsistent: return "incoh\xC3\xA9rence";
        case Status::ThisPc: return "ce PC";
        case Status::NotScanned: return "hors de la plage";
    }
    return {};
}

std::string_view proposalKindText(Proposal::Kind k) noexcept {
    switch (k) {
        case Proposal::Kind::Add: return "ajouter";
        case Proposal::Kind::UpdateAddress: return "mettre \xC3\xA0 jour l'adresse";
        case Proposal::Kind::Associate: return "associer";
    }
    return {};
}

std::string Proposal::prop(std::string_view key) const {
    for (const auto& [k, v] : props)
        if (k == key) return v;
    return {};
}

namespace {

// L'appareil trouve ressemble-t-il a cet equipement du projet ? Le meme nom (DNS), son identification ou sa MAC dans
// la description de l'equipement (le scanner l'y ecrit a l'ajout). Une supposition : `why` dit laquelle.
bool looksLike(const Device& dev, const Declared& d, std::string& why) {
    const auto& h = dev.host;
    if (!h.name.empty()) {
        const std::string shortName = h.name.substr(0, h.name.find('.'));
        if (!folded(shortName).empty() && folded(shortName) == folded(d.name)) {
            why = "m\xC3\xAAme nom (DNS : " + h.name + ")";
            return true;
        }
    }
    const std::string desc = lower(d.description);
    if (desc.empty()) return false;
    if (!h.identity.empty() && h.modbus.size() >= 4 && desc.find(lower(h.modbus)) != std::string::npos) {
        why = "son identification \xC2\xAB " + h.modbus + " \xC2\xBB est dans la description de " + d.name;
        return true;
    }
    if (!h.mac.empty()) {
        const auto hex = [](std::string_view s) {
            std::string out;
            for (const char c : s)
                if (std::isxdigit(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        };
        const std::string mac = hex(h.mac);
        if (mac.size() == 12 && hex(desc).find(mac) != std::string::npos) {
            why = "sa MAC " + h.mac + " est dans la description de " + d.name;
            return true;
        }
    }
    return false;
}

std::string suggestName(const Project& p, const Device& dev, std::set<std::string>& used) {
    const auto& h = dev.host;
    const std::string octet = h.address.substr(h.address.rfind('.') + 1);
    std::string base;
    if (!h.name.empty()) base = h.name.substr(0, h.name.find('.'));
    else if (!dev.vendor.empty() && dev.vendor.rfind("adresse locale", 0) != 0) base = dev.vendor.substr(0, dev.vendor.find(" (")) + " " + octet;
    else base = (dev.modbus ? "Modbus " : "Appareil ") + h.address;
    std::string clean;
    for (const char c : base)
        if (std::string_view(kForbidden).find(c) == std::string_view::npos) clean += c;
    clean = trimmed(clean);
    if (clean.size() > 56) clean = trimmed(clean.substr(0, 56));
    if (clean.empty()) clean = "Appareil " + h.address;
    std::string name = clean;
    for (int i = 2; i < 1000 && (p.equipmentByName(name) || used.count(lower(name))); ++i) name = clean + " " + std::to_string(i);
    used.insert(lower(name));
    return name;
}

} // namespace

std::vector<const Finding*> Report::about(std::string_view address) const {
    std::vector<const Finding*> out;
    for (const auto& f : findings)
        if (!address.empty() && f.address == address) out.push_back(&f);
    return out;
}

const Proposal* Report::proposalFor(std::string_view address) const {
    for (const auto& p : proposals)
        if (p.address == address) return &p;
    return nullptr;
}

const Device* Report::device(std::string_view address) const {
    for (const auto& d : devices)
        if (d.host.address == address) return &d;
    return nullptr;
}

std::string Report::projectText(std::string_view address) const {
    std::string knownAs, movedFrom, flag;
    bool isNew = false, pc = false;
    for (const auto* f : about(address)) {
        switch (f->status) {
            case Status::Known: knownAs = f->equipment; break;
            case Status::Moved: movedFrom = f->equipment; break;
            case Status::New: isNew = true; break;
            case Status::ThisPc: pc = true; break;
            case Status::Inconsistent: flag = " (incoh\xC3\xA9rence)"; break;
            case Status::Duplicate: if (flag.empty()) flag = " (doublon)"; break;
            default: break;
        }
    }
    if (pc) return "ce PC";
    if (!knownAs.empty()) return knownAs + flag;
    if (!movedFrom.empty()) return movedFrom + " ? (adresse diff\xC3\xA9rente)";
    if (const Proposal* p = proposalFor(address); p && p->kind == Proposal::Kind::Associate) return p->equipment + " ? (esclave simul\xC3\xA9)";
    if (isNew) return "absent du projet" + flag;
    return {};
}

std::string Report::countsText() const {
    std::vector<std::string> parts;
    if (known) parts.push_back(plural(known, "d\xC3\xA9" "clar\xC3\xA9 trouv\xC3\xA9", "d\xC3\xA9" "clar\xC3\xA9s trouv\xC3\xA9s"));
    if (fresh) parts.push_back(plural(fresh, "absent du projet", "absents du projet"));
    if (unreachable) parts.push_back(plural(unreachable, "injoignable", "injoignables"));
    if (moved) parts.push_back(plural(moved, "\xC3\xA0 une autre adresse (suppos\xC3\xA9)", "\xC3\xA0 une autre adresse (suppos\xC3\xA9)"));
    if (duplicates) parts.push_back(plural(duplicates, "doublon", "doublons"));
    if (inconsistent) parts.push_back(plural(inconsistent, "incoh\xC3\xA9rence", "incoh\xC3\xA9rences"));
    return join(parts, ", ");
}

Report analyze(const Project& project, const std::vector<ipscan::Host>& found, const Scope& scope) {
    Report r;
    std::vector<ipscan::Host> sorted = found;
    std::sort(sorted.begin(), sorted.end(), [](const ipscan::Host& a, const ipscan::Host& b) { return a.ip < b.ip; });
    std::map<std::uint32_t, std::size_t> byIp;
    for (const auto& h : sorted) {
        if (byIp.count(h.ip)) continue;
        byIp[h.ip] = r.devices.size();
        r.devices.push_back(classify(h));
    }
    const auto decl = declared(project, scope.withPlc);
    const auto inRange = [&](std::uint32_t ip) { return ip >= scope.first && ip <= scope.last; };
    const auto isLocal = [&](std::uint32_t ip) { return std::find(scope.local.begin(), scope.local.end(), ip) != scope.local.end(); };
    const auto finding = [&](Status s, std::string equipment, std::string address, std::string declaredAddress, std::string text, Certainty c) {
        r.findings.push_back(Finding{s, std::move(equipment), std::move(address), std::move(declaredAddress), std::move(text), c});
    };

    // 1. Les doublons du projet : la meme adresse, le meme port (et le meme esclave en Modbus).
    for (std::size_t i = 0; i < decl.size(); ++i)
        for (std::size_t j = i + 1; j < decl.size(); ++j) {
            const auto& a = decl[i];
            const auto& b = decl[j];
            if (a.simulatedOnly || b.simulatedOnly || a.host.empty() || !iequal(a.host, b.host) || a.port != b.port) continue;
            if (a.modbus && b.modbus && a.unit != b.unit) continue;   // une passerelle : plusieurs esclaves derriere une adresse
            ++r.duplicates;
            finding(Status::Duplicate, a.name, b.ip ? eq::ipv4Text(b.ip) : std::string{}, a.host,
                    a.name + " et " + b.name + " : la m\xC3\xAAme adresse " + a.host + ":" + std::to_string(a.port)
                        + (a.modbus && b.modbus ? ", le m\xC3\xAAme esclave " + std::to_string(a.unit) : std::string{}),
                    Certainty::Confirmed);
        }

    // 2. Chaque equipement reel du projet : trouve, injoignable, ou hors de la plage.
    std::set<std::uint32_t> matched;
    std::vector<const Declared*> missing;
    for (const auto& d : decl) {
        if (d.simulatedOnly) continue;
        if (!d.ip) {
            if (!d.host.empty())
                finding(Status::NotScanned, d.name, {}, d.host, d.name + " (" + d.host + ") : un nom de poste, pas une adresse IPv4 - non compar\xC3\xA9", Certainty::Unknown);
            continue;
        }
        const auto it = byIp.find(d.ip);
        if (it != byIp.end()) {
            const Device& dev = r.devices[it->second];
            const auto& h = dev.host;
            matched.insert(d.ip);
            ++r.known;
            std::string text = d.name + " (" + d.host + ") r\xC3\xA9pond";
            if (d.port > 0 && !contains(scope.ports, d.port)) text += " ; son port " + std::to_string(d.port) + " n'a pas \xC3\xA9t\xC3\xA9 essay\xC3\xA9";
            finding(Status::Known, d.name, h.address, d.host, text, Certainty::Confirmed);
            if (d.port > 0 && contains(scope.ports, d.port) && !h.hasPort(d.port)) {
                ++r.inconsistent;
                finding(Status::Inconsistent, d.name, h.address, d.host,
                        d.name + " : le port " + std::to_string(d.port) + " d\xC3\xA9" "clar\xC3\xA9 ne r\xC3\xA9pond pas (l'appareil, lui, r\xC3\xA9pond : "
                            + (h.how.empty() ? std::string("vu") : h.how) + ")",
                        Certainty::Confirmed);
            }
            if (!d.modbus && h.modbusAnswered) {
                ++r.inconsistent;
                finding(Status::Inconsistent, d.name, h.address, d.host,
                        d.name + " est d\xC3\xA9" "clar\xC3\xA9 en Ethernet TCP/IP mais r\xC3\xA9pond en Modbus TCP (identification lue)", Certainty::Confirmed);
            }
        } else if (inRange(d.ip)) {
            if (!scope.complete) {
                finding(Status::NotScanned, d.name, {}, d.host, d.name + " (" + d.host + ") : scan arr\xC3\xAAt\xC3\xA9 avant la fin - non conclu", Certainty::Unknown);
                continue;
            }
            ++r.unreachable;
            missing.push_back(&d);
            finding(Status::Unreachable, d.name, {}, d.host,
                    d.name + " (" + d.host + ") : aucune r\xC3\xA9ponse pendant ce scan (ping, ARP, ports essay\xC3\xA9s)", Certainty::Confirmed);
        } else {
            finding(Status::NotScanned, d.name, {}, d.host, d.name + " (" + d.host + ") : hors de la plage scann\xC3\xA9" "e, non v\xC3\xA9rifi\xC3\xA9", Certainty::Unknown);
        }
    }

    // 3. Les doublons sur le reseau : une meme MAC a plusieurs adresses.
    std::map<std::string, std::vector<std::string>> byMac;
    for (const auto& dev : r.devices)
        if (!dev.host.mac.empty() && !isLocal(dev.host.ip)) byMac[dev.host.mac].push_back(dev.host.address);
    for (const auto& [mac, list] : byMac) {
        if (list.size() < 2) continue;
        ++r.duplicates;
        for (const auto& address : list)
            finding(Status::Duplicate, {}, address, {},
                    "la m\xC3\xAAme MAC " + mac + " r\xC3\xA9pond \xC3\xA0 " + join(list, ", ") + " : un appareil \xC3\xA0 plusieurs adresses, ou un conflit d'adresses",
                    Certainty::Confirmed);
    }

    // 4. Chaque appareil trouve hors du projet : une autre adresse d'un injoignable, un esclave simule, ou nouveau.
    std::set<std::string> used;
    for (const auto& dev : r.devices) {
        const auto& h = dev.host;
        if (isLocal(h.ip)) {
            ++r.local;
            finding(Status::ThisPc, {}, h.address, {}, h.address + " : ce PC", Certainty::Confirmed);
            continue;
        }
        if (matched.count(h.ip)) continue;
        ++r.fresh;
        finding(Status::New, {}, h.address, {}, h.address + " r\xC3\xA9pond et n'est pas dans le projet", Certainty::Confirmed);
        std::string why;
        auto at = std::find_if(missing.begin(), missing.end(), [&](const Declared* d) { return looksLike(dev, *d, why); });
        if (at != missing.end()) {
            const Declared& d = **at;
            missing.erase(at);
            ++r.moved;
            finding(Status::Moved, d.name, h.address, d.host,
                    d.name + " (d\xC3\xA9" "clar\xC3\xA9 \xC3\xA0 " + d.host + ") ne r\xC3\xA9pond pas ; " + h.address + " lui ressemble : " + why, Certainty::Supposed);
            if (d.plc) continue;   // l'automate du projet : son adresse se regle dans Communication, pas ici
            Proposal p;
            p.kind = Proposal::Kind::UpdateAddress;
            p.equipment = d.name;
            p.address = h.address;
            p.before = d.host;
            p.props.emplace_back("hote", h.address);
            if (d.modbus && h.modbusAnswered && h.modbusUnit >= 0 && h.modbusUnit != d.unit) p.props.emplace_back("esclave", std::to_string(h.modbusUnit));
            p.text = "Mettre \xC3\xA0 jour l'adresse de \xC2\xAB " + d.name + " \xC2\xBB : " + d.host + " \xE2\x86\x92 " + h.address;
            p.reason = "suppos\xC3\xA9 : " + why;
            p.certainty = Certainty::Supposed;
            r.proposals.push_back(std::move(p));
            continue;
        }
        const Declared* sim = nullptr;
        for (const auto& d : decl)
            if (d.simulatedOnly && !d.plc && looksLike(dev, d, why)) {
                sim = &d;
                break;
            }
        if (sim) {
            Proposal p;
            p.kind = Proposal::Kind::Associate;
            p.equipment = sim->name;
            p.address = h.address;
            p.before = sim->host;
            p.props.emplace_back("hote", h.address);
            if (sim->modbus && h.modbusAnswered && h.modbusUnit >= 0) p.props.emplace_back("esclave", std::to_string(h.modbusUnit));
            p.text = "Associer " + h.address + " \xC3\xA0 l'esclave simul\xC3\xA9 \xC2\xAB " + sim->name + " \xC2\xBB (il reste simul\xC3\xA9 tant que la case n'est pas d\xC3\xA9" "coch\xC3\xA9" "e)";
            p.reason = "suppos\xC3\xA9 : " + why;
            p.certainty = Certainty::Supposed;
            r.proposals.push_back(std::move(p));
            continue;
        }
        // Nouveau : les champs de creer_equipement, ce qui est connu seulement (aucune variable inventee).
        Proposal p;
        p.kind = Proposal::Kind::Add;
        p.equipment = suggestName(project, dev, used);
        p.address = h.address;
        const bool modbus = dev.modbus;
        const int port = modbus ? 502 : (h.open.empty() ? 0 : h.open.front());
        p.props.emplace_back("type", modbus ? "modbus_tcp" : "ethernet_tcp");
        p.props.emplace_back("adresse", h.address);
        p.props.emplace_back("port", std::to_string(port));
        if (modbus && h.modbusAnswered && h.modbusUnit >= 0) p.props.emplace_back("esclave", std::to_string(h.modbusUnit));
        std::string desc = "trouv\xC3\xA9 par le scanner";
        if (!h.identity.empty()) desc += " : " + h.modbus;
        else if (!dev.vendor.empty()) desc += " : " + valueWithCertainty(dev.vendor, dev.vendorCertainty);
        if (!h.mac.empty()) desc += " (MAC " + h.mac + ")";
        p.props.emplace_back("description", desc);
        p.text = "Ajouter \xC2\xAB " + p.equipment + " \xC2\xBB : " + (modbus ? std::string("Modbus TCP/IP ") : std::string("Ethernet TCP/IP ")) + h.address
                 + (port > 0 ? ":" + std::to_string(port) : std::string{})
                 + (modbus && h.modbusAnswered && h.modbusUnit >= 0 ? ", esclave " + std::to_string(h.modbusUnit) : std::string{});
        p.certainty = modbus && !h.modbusAnswered ? Certainty::Supposed : Certainty::Confirmed;
        p.reason = "il r\xC3\xA9pond et n'est pas dans le projet"
                   + std::string(modbus && !h.modbusAnswered ? " ; Modbus TCP suppos\xC3\xA9 (port 502 ouvert, aucune r\xC3\xA9ponse lue)" : "")
                   + " ; aucune variable n'est cr\xC3\xA9\xC3\xA9" "e (ses adresses ne sont pas connues)";
        r.proposals.push_back(std::move(p));
    }
    return r;
}

std::string reportText(const Report& r, std::size_t maxDevices) {
    std::string out = plural(r.devices.size(), "appareil trouv\xC3\xA9", "appareils trouv\xC3\xA9s");
    const std::string counts = r.countsText();
    if (!counts.empty()) out += " : " + counts;
    out += ".\n";
    std::size_t shown = 0;
    for (const auto& d : r.devices) {
        if (shown++ >= maxDevices) {
            out += "... et " + std::to_string(r.devices.size() - maxDevices) + " autres.\n";
            break;
        }
        out += "\n" + d.host.address + " - " + r.projectText(d.host.address) + "\n" + deviceText(d);
    }
    bool title = false;
    for (const auto& f : r.findings) {
        if (f.status == Status::Known || f.status == Status::New || f.status == Status::ThisPc) continue;
        if (!title) {
            out += "\nConstats :\n";
            title = true;
        }
        out += "- " + std::string(statusText(f.status)) + " : " + f.text + " (" + std::string(certaintyText(f.certainty)) + ")\n";
    }
    if (!r.proposals.empty()) {
        out += "\nPropositions (rien n'est appliqu\xC3\xA9 sans votre accord) :\n";
        for (const auto& p : r.proposals) out += "- " + p.text + " - " + p.reason + "\n";
    }
    return out;
}

// ================================================================ le journal ===
namespace {

constexpr std::size_t kJournalFields = 16;

std::string numberText(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

} // namespace

std::string currentUser() {
    const auto s = edits::currentSignature();
    if (s.account.empty()) return s.pc.empty() ? std::string("inconnu") : s.pc;
    return s.pc.empty() ? s.account : s.account + "@" + s.pc;
}

std::string newScanId() {
    std::string stamp = wallStamp();   // "2026-10-31 14:05:12.350"
    std::string digits;
    for (const char c : stamp.substr(0, 19))
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    if (digits.size() >= 14) digits = digits.substr(0, 8) + "-" + digits.substr(8, 6);
    // Un tirage, et un compteur de la seance : deux scans de la meme seconde ne se confondent pas.
    static std::mt19937 rng{std::random_device{}()};
    static std::mutex mutex;
    static unsigned counter = 0;
    unsigned v = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        v = (static_cast<unsigned>(rng() & 0xFFFFu) << 8) | (++counter & 0xFFu);
    }
    char tail[16];
    std::snprintf(tail, sizeof tail, "%06x", v & 0xFFFFFFu);
    return digits + "-" + tail;
}

JournalEntry startEntry(const Plan& p, const ipscan::Permit& permit, std::string id, std::string origin) {
    JournalEntry e;
    e.id = std::move(id);
    e.stamp = permit.when().empty() ? wallStamp() : permit.when();
    e.who = permit.who().empty() ? currentUser() : permit.who();
    e.origin = std::move(origin);
    e.state = "lance";
    e.interfaceName = p.interfaceText;
    e.range = p.rangeText;
    e.protocols = join(p.protocolLabels, ", ");
    e.ports = ipscan::portsText(p.options.ports);
    e.rate = p.options.rate;
    e.addresses = p.addresses;
    return e;
}

JournalEntry endEntry(const JournalEntry& started, const ipscan::Scanner::Progress& pr) {
    JournalEntry e = started;
    e.stamp = wallStamp();
    e.state = pr.stopped ? "arrete" : "termine";
    e.tried = pr.done;
    e.found = pr.found;
    e.probes = pr.probes;
    e.seconds = pr.seconds;
    return e;
}

std::string journalHeader() {
    return "id;horodatage;qui;origine;etat;interface;plage;protocoles;ports;cadence;adresses;essayees;trouves;interrogations;secondes;note";
}

std::string journalLine(const JournalEntry& e) {
    const std::vector<std::string> fields = {e.id, e.stamp, e.who, e.origin, e.state, e.interfaceName, e.range, e.protocols, e.ports,
                                             numberText(e.rate), std::to_string(e.addresses), std::to_string(e.tried), std::to_string(e.found),
                                             std::to_string(e.probes), numberText(e.seconds), e.note};
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        std::string f = fields[i];
        for (auto& c : f)
            if (c == '\n' || c == '\r') c = ' ';
        out += (i ? ";" : "") + csvField(f);
    }
    return out + "\n";
}

bool parseJournal(std::string_view text, std::vector<JournalEntry>& out, std::string* why) {
    std::vector<JournalEntry> list;
    std::size_t bad = 0;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trimmed(line).empty() || line.rfind("id;", 0) == 0) continue;
        const auto f = csvSplit(line, ';', false);
        if (f.size() < kJournalFields) {
            ++bad;
            continue;
        }
        JournalEntry e;
        e.id = f[0];
        e.stamp = f[1];
        e.who = f[2];
        e.origin = f[3];
        e.state = f[4];
        e.interfaceName = f[5];
        e.range = f[6];
        e.protocols = f[7];
        e.ports = f[8];
        e.rate = std::strtod(f[9].c_str(), nullptr);
        e.addresses = static_cast<std::size_t>(std::strtoull(f[10].c_str(), nullptr, 10));
        e.tried = static_cast<std::size_t>(std::strtoull(f[11].c_str(), nullptr, 10));
        e.found = static_cast<std::size_t>(std::strtoull(f[12].c_str(), nullptr, 10));
        e.probes = std::strtoull(f[13].c_str(), nullptr, 10);
        e.seconds = std::strtod(f[14].c_str(), nullptr);
        e.note = f[15];
        list.push_back(std::move(e));
    }
    if (why) *why = bad ? std::to_string(bad) + " ligne(s) illisible(s), ignor\xC3\xA9" "e(s)" : std::string{};
    out = std::move(list);
    return true;
}

std::string journalPath(const std::string& projectFolder) {
    if (projectFolder.empty()) return {};
    return (fs::path(projectFolder) / "ihm" / "reseau" / "decouvertes.csv").string();
}

core::Status appendJournal(const JournalEntry& e, const std::string& projectFolder) {
    if (projectFolder.empty()) return core::ok();
    const fs::path file = journalPath(projectFolder);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, "dossier " + file.parent_path().string() + " : " + ec.message());
    const bool fresh = !fs::exists(file, ec) || fs::file_size(file, ec) == 0;
    std::ofstream out(file, std::ios::binary | std::ios::app);
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + file.string());
    if (fresh) out << journalHeader() << "\n";
    out << journalLine(e);
    out.flush();
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + file.string());
    return core::ok();
}

std::vector<JournalEntry> readJournal(const std::string& projectFolder, std::string* why) {
    std::vector<JournalEntry> out;
    const std::string path = journalPath(projectFolder);
    if (path.empty()) return out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    std::ostringstream ss;
    ss << in.rdbuf();
    (void)parseJournal(ss.str(), out, why);
    return out;
}

std::string journalText(const JournalEntry& e) {
    std::string when = e.stamp;
    if (e.stamp.size() >= 16) when = e.stamp.substr(8, 2) + "/" + e.stamp.substr(5, 2) + " " + e.stamp.substr(11, 5);
    std::string state = e.state;
    if (state == "lance") state = "lanc\xC3\xA9";
    else if (state == "termine") state = "termin\xC3\xA9";
    else if (state == "arrete") state = "arr\xC3\xAAt\xC3\xA9";
    else if (state == "refuse") state = "refus\xC3\xA9";
    std::string out = when + " - " + e.who + " : " + e.range;
    if (e.state != "lance" && e.state != "refuse") out += ", " + plural(e.found, "appareil", "appareils");
    return out + " (" + state + (e.origin.empty() ? std::string{} : ", " + e.origin) + ")";
}

} // namespace hmi::discovery
