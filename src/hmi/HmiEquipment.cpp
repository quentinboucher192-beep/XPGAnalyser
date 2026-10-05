// hmi/HmiEquipment.cpp - les equipements du reseau (lot 15) : les adresses, le
// plan d'un equipement, la mise a l'echelle, les reseaux IPv4.
#include "HmiEquipment.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace hmi::equip {

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string upperOf(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool allDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

bool sameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// La zone d'une adresse Modicon, par son chiffre de tete (4, 3, 0, 1).
enum class Zone { Holding, Input, Coil, Discrete };

std::string schneider(Zone z, long long offset, int bit) {
    std::string out;
    switch (z) {
        case Zone::Holding: out = "%MW"; break;
        case Zone::Input: out = "%IW"; break;
        case Zone::Coil: out = "%M"; break;
        case Zone::Discrete: out = "%I"; break;
    }
    out += std::to_string(offset);
    if (bit >= 0) out += "." + std::to_string(bit);
    return out;
}

// Le numero et le bit eventuel : "0101", "0101.3".
bool numberAndBit(std::string_view s, long long& number, int& bit) {
    bit = -1;
    const auto dot = s.find('.');
    const std::string_view n = s.substr(0, dot);
    if (!allDigits(n) || n.size() > 6) return false;
    number = std::stoll(std::string(n));
    if (dot != std::string_view::npos) {
        const std::string_view b = s.substr(dot + 1);
        if (!allDigits(b) || b.size() > 2) return false;
        bit = std::stoi(std::string(b));
        if (bit > 15) return false;
    }
    return true;
}

} // namespace

// ------------------------------------------------------------------ adresses ---
std::string canonicalAddress(std::string_view address, std::string* why) {
    const std::string raw = upperOf(trimmed(address));
    const auto bad = [&](std::string text) {
        if (why) *why = std::move(text);
        return std::string{};
    };
    if (raw.empty()) return bad("adresse vide");
    if (raw.front() == '%') return raw;
    const std::string forms = " (%MW100, 40101, 4x0101, 30001, 00017, 10005, HR100, 40101.3)";
    // HR100, IR100, CO100, DI100 : a partir de 0.
    const std::pair<const char*, Zone> zero[] = {{"HR", Zone::Holding}, {"IR", Zone::Input}, {"CO", Zone::Coil}, {"DI", Zone::Discrete}};
    for (const auto& [prefix, zone] : zero) {
        if (raw.rfind(prefix, 0) != 0) continue;
        long long n = 0;
        int bit = -1;
        if (!numberAndBit(std::string_view(raw).substr(2), n, bit) || n > 65535) return bad("adresse illisible \xC2\xAB " + raw + " \xC2\xBB" + forms);
        if (bit >= 0 && zone != Zone::Holding && zone != Zone::Input) return bad("un bit de bit : " + raw);
        return schneider(zone, n, bit);
    }
    // 4x0101, 3x0001, 0x0017, 1x0005 : a partir de 1.
    if (raw.size() > 2 && raw[1] == 'X' && (raw[0] == '4' || raw[0] == '3' || raw[0] == '0' || raw[0] == '1')) {
        long long n = 0;
        int bit = -1;
        if (!numberAndBit(std::string_view(raw).substr(2), n, bit) || n < 1 || n > 65536)
            return bad("adresse illisible \xC2\xAB " + raw + " \xC2\xBB" + forms);
        const Zone zone = raw[0] == '4' ? Zone::Holding : raw[0] == '3' ? Zone::Input : raw[0] == '0' ? Zone::Coil : Zone::Discrete;
        if (bit >= 0 && (zone == Zone::Coil || zone == Zone::Discrete)) return bad("un bit de bit : " + raw);
        return schneider(zone, n - 1, bit);
    }
    // 40101, 400101 (et 30001, 00017, 10005) : a partir de 1.
    {
        const auto dot = raw.find('.');
        const std::string digits = raw.substr(0, dot);
        if (allDigits(digits) && (digits.size() == 5 || digits.size() == 6)) {
            const char lead = digits[0];
            if (lead != '4' && lead != '3' && lead != '0' && lead != '1')
                return bad("adresse \xC2\xAB " + raw + " \xC2\xBB : un registre commence par 4 (maintien) ou 3 (entr\xC3\xA9" "e), "
                           "un bit par 0 (bobine) ou 1 (entr\xC3\xA9" "e TOR)");
            long long n = 0;
            int bit = -1;
            if (!numberAndBit(std::string_view(raw).substr(1), n, bit) || n < 1 || n > 65536)
                return bad("adresse illisible \xC2\xAB " + raw + " \xC2\xBB" + forms + " : la num\xC3\xA9rotation Modicon part de 1");
            const Zone zone = lead == '4' ? Zone::Holding : lead == '3' ? Zone::Input : lead == '0' ? Zone::Coil : Zone::Discrete;
            if (bit >= 0 && (zone == Zone::Coil || zone == Zone::Discrete)) return bad("un bit de bit : " + raw);
            return schneider(zone, n - 1, bit);
        }
    }
    return bad("adresse illisible \xC2\xAB " + raw + " \xC2\xBB" + forms);
}

bool placeEquipmentAddress(std::string_view address, sim::Type type, comm::Point& out, std::string* why) {
    const std::string canon = canonicalAddress(address, why);
    if (canon.empty()) return false;
    // Une chaine : 32 caracteres (16 registres) - la place d'un nom, d'un code.
    return comm::placeAddress(canon, type, 32, out, why);
}

std::string modiconText(const comm::Point& p) {
    const auto num = [](char lead, long long n) {
        char b[16];
        if (n <= 9999) std::snprintf(b, sizeof b, "%c%04lld", lead, n);
        else std::snprintf(b, sizeof b, "%c%05lld", lead, n);
        return std::string(b);
    };
    const long long first = static_cast<long long>(p.offset) + 1;
    switch (p.area) {
        case comm::Area::Coils: return "bobine " + num('0', first);
        case comm::Area::DiscreteInputs: return "entr\xC3\xA9" "e TOR " + num('1', first);
        case comm::Area::Holding:
        case comm::Area::InputRegisters: {
            const char lead = p.area == comm::Area::Holding ? '4' : '3';
            const std::string what = p.area == comm::Area::Holding ? "registre" : "registre d'entr\xC3\xA9" "e";
            if (p.encoding == comm::Encoding::BitOfWord) return what + " " + num(lead, first) + ", bit " + std::to_string(p.bit);
            if (p.size > 1) return what + "s " + num(lead, first) + " \xC3\xA0 " + num(lead, first + p.size - 1);
            return what + " " + num(lead, first);
        }
    }
    return {};
}

sim::Type typeOfName(std::string_view typeName) noexcept {
    if (sameText(typeName, "LREAL")) return sim::Type::Real;
    const auto t = sim::typeFromName(typeName);
    return t == sim::Type::Unknown ? sim::Type::Int : t;
}

sim::Type registerType(const Variable& v) noexcept {
    if (v.scaled()) return v.rawType.empty() ? sim::Type::Int : typeOfName(v.rawType);
    return typeOfName(v.type);
}

std::vector<const Variable*> boundVariables(const Project& p, const Equipment& e) {
    std::vector<const Variable*> out;
    for (const auto& v : p.programs.variables)
        if (v.bound() && sameText(v.equipment, e.name)) out.push_back(&v);
    return out;
}

comm::Plan buildPlan(const Project& p, const Equipment& e) {
    comm::Plan plan;
    if (!e.modbus()) return plan;
    for (const auto* top : boundVariables(p, e)) {
        // Lot 16 : une structure ou un tableau - une place par case, sous son chemin.
        std::vector<std::string> whys;
        std::string error;
        const auto leaves = types::leafVariables(p, *top, nullptr, &whys, &error);
        if (!error.empty()) {
            plan.refuse(top->name, error);
            continue;
        }
        for (std::size_t i = 0; i < leaves.size(); ++i) {
            const Variable* v = &leaves[i];
            comm::Point pt;
            std::string why;
            if (v->address.empty()) {
                const std::string reason = i < whys.size() ? whys[i] : std::string{};
                plan.refuse(v->name, reason.empty() ? "sans adresse sur " + e.name : reason);
                continue;
            }
            if (!placeEquipmentAddress(v->address, registerType(*v), pt, &why)) {
                plan.refuse(v->name, why);
                continue;
            }
            pt.name = v->name;
            pt.origin = "variable IHM";
            pt.typeName = v->scaled() ? std::string(sim::toString(registerType(*v))) : v->type;
            pt.writable = pt.writable && !v->readOnly && e.writes;
            pt.description = v->description;
            plan.add(std::move(pt));
        }
    }
    return plan;
}

comm::Settings settingsOf(const Equipment& e, int twinPort) {
    comm::Settings s;
    // Lot 17 : le jumeau (un port local donne) ; sinon le vrai appareil.
    s.host = twinPort > 0 || e.simulated ? std::string("127.0.0.1") : e.host;
    s.port = twinPort > 0 || e.simulated ? twinPort : e.port;
    s.unit = e.unit;
    s.timeoutMs = e.timeoutMs;
    s.periodMs = e.periodMs;
    s.retryS = e.retryS;
    s.lowWordFirst = e.wordOrder != "fort";
    s.maxWords = e.maxWords;
    s.maxBits = e.maxBits;
    s.gap = e.gap;
    s.writes = e.writes;
    s.badAfterS = e.badAfterS;
    s.peer = "l'\xC3\xA9quipement " + e.name;
    return s;
}

std::string nextFreeAddress(const Project& p, const Equipment& e, sim::Type type) {
    long long nextWord = 0, nextBit = 0;
    bool modicon = false;
    for (const auto* top : boundVariables(p, e)) {
        const std::string a = trimmed(top->address);
        if (!a.empty() && a.front() != '%') modicon = true;
        // Lot 16 : une structure ou un tableau occupe toutes ses cases.
        for (const auto& leaf : types::leafVariables(p, *top)) {
            comm::Point pt;
            if (!placeEquipmentAddress(leaf.address, registerType(leaf), pt)) continue;
            if (pt.area == comm::Area::Holding)
                nextWord = std::max<long long>(nextWord, static_cast<long long>(pt.offset) + pt.size);
            else if (pt.area == comm::Area::Coils)
                nextBit = std::max<long long>(nextBit, static_cast<long long>(pt.offset) + 1);
        }
    }
    char b[24];
    if (type == sim::Type::Bool) {
        if (modicon) std::snprintf(b, sizeof b, "0%04lld", nextBit + 1);
        else std::snprintf(b, sizeof b, "%%M%lld", nextBit);
        return b;
    }
    if (modicon) {
        if (nextWord + 1 <= 9999) std::snprintf(b, sizeof b, "4%04lld", nextWord + 1);
        else std::snprintf(b, sizeof b, "4%05lld", nextWord + 1);
    } else {
        std::snprintf(b, sizeof b, "%%MW%lld", nextWord);
    }
    return b;
}

// ------------------------------------------------------------ mise a l'echelle ---
double scaleIn(const Variable& v, double raw) noexcept {
    if (!v.scaled()) return raw;
    return v.engMin + (raw - v.rawMin) * (v.engMax - v.engMin) / (v.rawMax - v.rawMin);
}

double scaleOut(const Variable& v, double value) noexcept {
    if (!v.scaled() || v.engMax == v.engMin) return value;
    return v.rawMin + (value - v.engMin) * (v.rawMax - v.rawMin) / (v.engMax - v.engMin);
}

sim::Value fromRegister(const Variable& v, const sim::Value& raw) {
    sim::Value out = sim::Value::defaultOf(typeOfName(v.type));
    if (!v.scaled()) {
        out.assignFrom(raw);
        return out;
    }
    const double value = scaleIn(v, raw.asReal());
    if (sim::isInteger(out.type())) out.assignFrom(sim::Value::integer(out.type(), static_cast<std::int64_t>(std::llround(value))));
    else out.assignFrom(sim::Value::real(value));
    return out;
}

sim::Value toRegister(const Variable& v, const sim::Value& value) {
    const sim::Type t = registerType(v);
    sim::Value out = sim::Value::defaultOf(t);
    if (!v.scaled()) {
        out.assignFrom(value);
        return out;
    }
    const double raw = scaleOut(v, value.asReal());
    if (!sim::isInteger(t)) {
        out.assignFrom(sim::Value::real(raw));
        return out;
    }
    // Un registre entier : arrondi, borne a sa largeur.
    double lo = -32768.0, hi = 32767.0;
    switch (t) {
        case sim::Type::UInt: case sim::Type::Word: lo = 0; hi = 65535; break;
        case sim::Type::DInt: case sim::Type::Time: lo = -2147483648.0; hi = 2147483647.0; break;
        case sim::Type::UDInt: case sim::Type::DWord: lo = 0; hi = 4294967295.0; break;
        case sim::Type::Byte: lo = 0; hi = 255; break;
        default: break;
    }
    const double r = std::clamp(std::round(raw), lo, hi);
    out.assignFrom(sim::Value::integer(t, static_cast<std::int64_t>(r)));
    return out;
}

// ------------------------------------------------------------------- IPv4 ---
bool parseIpv4(std::string_view text, std::uint32_t& out) noexcept {
    const std::string s = trimmed(text);
    std::uint32_t value = 0;
    int parts = 0;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = i;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        if (j == i || j - i > 3) return false;
        const int n = std::stoi(s.substr(i, j - i));
        if (n > 255) return false;
        value = (value << 8) | static_cast<std::uint32_t>(n);
        ++parts;
        if (j == s.size()) break;
        if (s[j] != '.' || parts == 4) return false;
        i = j + 1;
    }
    if (parts != 4) return false;
    out = value;
    return true;
}

std::string ipv4Text(std::uint32_t ip) {
    return std::to_string((ip >> 24) & 255) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "."
         + std::to_string(ip & 255);
}

std::uint32_t maskOf(int prefix) noexcept {
    if (prefix <= 0) return 0;
    if (prefix >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - prefix);
}

bool parseMask(std::string_view text, int& prefix) noexcept {
    std::string s = trimmed(text);
    if (!s.empty() && s.front() == '/') s.erase(s.begin());
    s = trimmed(s);
    if (allDigits(s) && s.size() <= 2) {
        const int n = std::stoi(s);
        if (n < 0 || n > 32) return false;
        prefix = n;
        return true;
    }
    std::uint32_t m = 0;
    if (!parseIpv4(s, m)) return false;
    int n = 0;
    while (n < 32 && (m & (0x80000000u >> n)) != 0) ++n;
    if (maskOf(n) != m) return false;       // des 1 puis des 0
    prefix = n;
    return true;
}

std::string maskText(int prefix) { return ipv4Text(maskOf(prefix)); }

std::uint32_t networkOf(std::uint32_t ip, int prefix) noexcept { return ip & maskOf(prefix); }

bool sameNetwork(std::uint32_t a, std::uint32_t b, int prefix) noexcept { return networkOf(a, prefix) == networkOf(b, prefix); }

std::string networkText(std::uint32_t ip, int prefix) { return ipv4Text(networkOf(ip, prefix)) + " / " + std::to_string(prefix); }

bool validHost(std::uint32_t ip, int prefix, std::string* why) {
    if (ip == 0) return fail(why, "0.0.0.0 n'est pas une adresse de poste");
    if ((ip >> 24) == 127) return fail(why, "127.x.x.x : la boucle locale de ce PC, pas une adresse de port");
    if ((ip >> 28) == 0xE) return fail(why, "une adresse de multidiffusion (224 \xC3\xA0 239)");
    if ((ip >> 28) == 0xF) return fail(why, "une adresse r\xC3\xA9serv\xC3\xA9" "e (240 et au-del\xC3\xA0)");
    if (prefix < 1 || prefix > 32) return fail(why, "masque impossible");
    if (prefix <= 30) {
        const std::uint32_t host = ip & ~maskOf(prefix);
        if (host == 0) return fail(why, ipv4Text(ip) + " est l'adresse du r\xC3\xA9seau lui-m\xC3\xAAme, pas celle d'un poste");
        if (host == (~maskOf(prefix))) return fail(why, ipv4Text(ip) + " est l'adresse de diffusion du r\xC3\xA9seau");
    }
    return true;
}

bool validPortSettings(std::string_view ip, std::string_view mask, std::string_view gateway, std::string* why) {
    std::uint32_t a = 0;
    if (!parseIpv4(ip, a)) return fail(why, "adresse IP illisible \xC2\xAB " + trimmed(ip) + " \xC2\xBB (quatre nombres de 0 \xC3\xA0 255 : 192.168.1.20)");
    int prefix = 0;
    if (!parseMask(mask, prefix) || prefix < 1 || prefix > 30)
        return fail(why, "masque illisible \xC2\xAB " + trimmed(mask) + " \xC2\xBB (255.255.255.0, ou /24)");
    if (!validHost(a, prefix, why)) return false;
    const std::string g = trimmed(gateway);
    if (!g.empty()) {
        std::uint32_t gw = 0;
        if (!parseIpv4(g, gw)) return fail(why, "passerelle illisible \xC2\xAB " + g + " \xC2\xBB");
        if (gw == a) return fail(why, "la passerelle ne peut pas \xC3\xAAtre l'adresse du port lui-m\xC3\xAAme");
        if (!sameNetwork(gw, a, prefix))
            return fail(why, "la passerelle " + g + " n'est pas dans le r\xC3\xA9seau " + networkText(a, prefix));
        if (!validHost(gw, prefix, why)) return false;
    }
    return true;
}

std::uint32_t suggestAddress(std::uint32_t anyIp, int prefix, const std::vector<std::uint32_t>& taken) {
    const std::uint32_t net = networkOf(anyIp, prefix);
    const std::uint32_t hostMask = ~maskOf(prefix);
    const std::uint32_t size = hostMask;   // les hotes : 1 .. size - 1
    if (size < 2) return 0;
    const auto free = [&](std::uint32_t host) {
        const std::uint32_t ip = net | host;
        return std::find(taken.begin(), taken.end(), ip) == taken.end();
    };
    for (std::uint32_t h = 100; h < size && h < 100000; ++h)
        if (free(h)) return net | h;
    for (std::uint32_t h = 1; h < std::min<std::uint32_t>(size, 100); ++h)
        if (free(h)) return net | h;
    return 0;
}

int networkFor(std::uint32_t host, const std::vector<Network>& networks) noexcept {
    // Le plus precis d'abord : un /24 dans un /16, c'est le /24.
    int best = -1, bestPrefix = -1;
    for (std::size_t i = 0; i < networks.size(); ++i) {
        const auto& n = networks[i];
        if (n.ip == 0) continue;
        if (sameNetwork(host, n.ip, n.prefix) && n.prefix > bestPrefix) {
            best = static_cast<int>(i);
            bestPrefix = n.prefix;
        }
    }
    return best;
}

} // namespace hmi::equip
