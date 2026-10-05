// hmi/HmiComm.cpp - plan d'adressage, liaison Modbus, serveur de demonstration (lot 14).
#include "HmiComm.hpp"
#include "HmiActionKinds.hpp"
#include "HmiObjectAlarms.hpp"

#include "HmiHistory.hpp"
#include "../domain/ProjectModel.hpp"
#include "../sim/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <unordered_set>

// 1.8.0 : la version vient de core/Version.hpp (la meme partout).
#include "../core/Version.hpp"
#ifndef XPG_ANALYZER_VERSION
#define XPG_ANALYZER_VERSION "1.0.0"
#endif

namespace hmi::comm {

namespace {

double monoNow() {
    static const auto origin = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count();
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::string seconds(double s) {
    if (s < 60) return std::to_string(static_cast<long long>(std::floor(s))) + " s";
    if (s < 3600) return std::to_string(static_cast<long long>(s / 60)) + " min " + std::to_string(static_cast<long long>(s) % 60) + " s";
    return std::to_string(static_cast<long long>(s / 3600)) + " h " + std::to_string(static_cast<long long>(s / 60) % 60) + " min";
}

// "%MW100", "%MW10.3", "%M5", "%MD20", "%I0.3.5", "%MW0[57]" : les lettres et les nombres.
struct Direct {
    std::string            letters;
    std::vector<long long> numbers;
};

bool parseDirect(std::string_view text, Direct& d) {
    const std::string s = sim::Runtime::normaliseAddress(trim(text));
    if (s.size() < 3 || s[0] != '%') return false;
    std::size_t i = 1;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) d.letters += static_cast<char>(std::toupper(static_cast<unsigned char>(s[i++])));
    if (d.letters.empty() || i >= s.size()) return false;
    while (i < s.size()) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        long long v = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            v = v * 10 + (s[i] - '0');
            if (v > 10000000) return false;
            ++i;
        }
        d.numbers.push_back(v);
        if (i < s.size()) {
            if (s[i] != '.' || i + 1 >= s.size()) return false;
            ++i;
        }
    }
    return !d.numbers.empty();
}

std::string directText(const Direct& d) {
    std::string out = "%" + d.letters;
    for (std::size_t k = 0; k < d.numbers.size(); ++k) out += (k ? "." : "") + std::to_string(d.numbers[k]);
    return out;
}

// "STRING[20]" -> 20 ; "STRING" -> 16 (la longueur par defaut de Control Expert).
std::uint16_t stringChars(std::string_view typeName) {
    const auto open = typeName.find('[');
    if (open == std::string_view::npos) return 16;
    const auto n = std::atoi(std::string(typeName.substr(open + 1)).c_str());
    return static_cast<std::uint16_t>(std::clamp(n, 1, 254));
}

// Sept chiffres significatifs : ce que porte un REAL de 32 bits (12.3, pas 12.300000190734863).
double roundFloat(float f) {
    if (!std::isfinite(f)) return static_cast<double>(f);
    char b[48];
    std::snprintf(b, sizeof b, "%.7g", static_cast<double>(f));
    return std::strtod(b, nullptr);
}

std::uint32_t join32(const std::uint16_t* w, bool low) {
    return low ? (static_cast<std::uint32_t>(w[1]) << 16) | w[0] : (static_cast<std::uint32_t>(w[0]) << 16) | w[1];
}

void split32(std::uint32_t v, bool low, std::vector<std::uint16_t>& out) {
    const auto lo = static_cast<std::uint16_t>(v & 0xFFFF), hi = static_cast<std::uint16_t>(v >> 16);
    if (low) out = {lo, hi};
    else out = {hi, lo};
}

long long integerOf(const sim::Value& v) {
    if (v.type() == sim::Type::Real) {
        const double r = v.asReal();
        if (!std::isfinite(r)) return 0;
        return static_cast<long long>(std::llround(std::clamp(r, -9.2e18, 9.2e18)));
    }
    if (v.type() == sim::Type::String) return std::atoll(v.asString().c_str());
    return v.asInteger();
}

// La case `index` d'un tableau du plan.
Point element(const Point& p, long long index) {
    Point e = p;
    e.array = false;
    e.name = p.name + "[" + std::to_string(index) + "]";
    const auto k = static_cast<std::size_t>(index - p.low);
    e.offset = static_cast<std::uint16_t>(p.offset + (p.bits() ? k : k * p.size));
    Direct d;
    if (parseDirect(p.address, d) && !d.numbers.empty()) {
        d.numbers.assign(1, static_cast<long long>(e.offset));
        e.address = directText(d);
    }
    e.typeName = std::string(sim::toString(p.type));
    return e;
}

} // namespace

// ------------------------------------------------------------------- le plan ---
std::size_t Point::span() const noexcept {
    const std::size_t n = array && high >= low ? static_cast<std::size_t>(high - low + 1) : 1;
    return bits() ? n : n * size;
}

std::string Point::placeText() const {
    const std::size_t n = span();
    const std::size_t last = offset + n - 1;
    switch (area) {
        case Area::Coils:
            return n == 1 ? "bit " + std::to_string(offset) : "bits " + std::to_string(offset) + " \xC3\xA0 " + std::to_string(last);
        case Area::DiscreteInputs:
            return n == 1 ? "bit d'entr\xC3\xA9" "e " + std::to_string(offset)
                          : "bits d'entr\xC3\xA9" "e " + std::to_string(offset) + " \xC3\xA0 " + std::to_string(last);
        case Area::Holding: {
            std::string t = n == 1 ? "mot " + std::to_string(offset) : "mots " + std::to_string(offset) + " \xC3\xA0 " + std::to_string(last);
            if (encoding == Encoding::BitOfWord) t += ", bit " + std::to_string(bit);
            return t;
        }
        case Area::InputRegisters:
            return n == 1 ? "mot d'entr\xC3\xA9" "e " + std::to_string(offset)
                          : "mots d'entr\xC3\xA9" "e " + std::to_string(offset) + " \xC3\xA0 " + std::to_string(last);
    }
    return {};
}

std::string Point::functionsText() const {
    switch (area) {
        case Area::Coils:          return writable ? "1, 5, 15" : "1";
        case Area::DiscreteInputs: return "2";
        case Area::Holding:        return writable ? "3, 6, 16" : "3";
        case Area::InputRegisters: return "4";
    }
    return {};
}

std::string Point::modbusText() const {
    const std::string f = functionsText();
    return placeText() + (f.find(',') != std::string::npos ? " (fonctions " : " (fonction ") + f + ")";
}

std::string keyOf(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

sim::Type typeOfAddress(std::string_view address) {
    Direct d;
    if (!parseDirect(address, d)) return sim::Type::Unknown;
    const auto& l = d.letters;
    if (l == "M" || l == "I" || l == "Q" || l == "S") return sim::Type::Bool;
    if (l == "MW" || l == "IW" || l == "QW" || l == "SW" || l == "KW") return d.numbers.size() >= 2 ? sim::Type::Bool : sim::Type::Int;
    if (l == "MD" || l == "ID" || l == "SD" || l == "KD") return sim::Type::DInt;
    if (l == "MF" || l == "IF" || l == "KF") return sim::Type::Real;
    return sim::Type::Unknown;
}

bool placeAddress(std::string_view address, sim::Type type, std::uint16_t chars, Point& out, std::string* why) {
    Direct d;
    if (!parseDirect(address, d))
        return fail(why, "adresse illisible \xC2\xAB " + trim(address) + " \xC2\xBB (%MW100, %MF20, %MD20, %M5, %MW10.3, %IW4, %I7)");
    const std::string& l = d.letters;
    if (l == "S" || l == "SW" || l == "SD")
        return fail(why, "bits et mots syst\xC3\xA8me : pas lisibles par Modbus (recopie-les dans des %MW du programme)");
    if (l == "K" || l == "KW" || l == "KD" || l == "KF") return fail(why, "constantes : pas lisibles par Modbus");
    if (l == "Q" || l == "QW" || l == "QD")
        return fail(why, "sorties : pas lisibles par Modbus (recopie-les dans des %M ou des %MW du programme)");
    if (l == "CH") return fail(why, "voie topologique : pas lisible par Modbus");
    if ((l == "I" || l == "IW" || l == "ID") && d.numbers.size() >= 3)
        return fail(why, "adresse topologique (rack.module.voie) : pas lisible par Modbus (recopie-la dans des %M ou des %MW)");
    const long long offset = d.numbers.front();
    if (offset > 65535) return fail(why, "au-del\xC3\xA0 de l'adresse 65535");
    out.offset = static_cast<std::uint16_t>(offset);
    out.type = type;
    out.bit = 0;
    if (l == "M" || l == "I") {
        if (d.numbers.size() > 1) return fail(why, "adresse de bit illisible : " + trim(address));
        if (type != sim::Type::Bool) return fail(why, "une adresse de bit pour une variable " + std::string(sim::toString(type)));
        out.area = l == "M" ? Area::Coils : Area::DiscreteInputs;
        out.writable = l == "M";
        out.encoding = Encoding::Bit;
        out.size = 1;
        out.address = directText(d);
        return true;
    }
    if (l != "MW" && l != "MD" && l != "MF" && l != "IW" && l != "ID" && l != "IF")
        return fail(why, "adresse " + trim(address) + " : pas lisible par Modbus (%M, %MW, %MD, %MF, %I, %IW)");
    out.area = l[0] == 'I' ? Area::InputRegisters : Area::Holding;
    out.writable = l[0] == 'M';
    out.address = directText(d);
    if (d.numbers.size() >= 2) {
        if (d.numbers.size() > 2 || d.numbers[1] > 15) return fail(why, "bit de mot illisible : " + trim(address) + " (un mot a 16 bits, 0 \xC3\xA0 15)");
        if (type != sim::Type::Bool) return fail(why, "un bit de mot pour une variable " + std::string(sim::toString(type)));
        out.encoding = Encoding::BitOfWord;
        out.bit = static_cast<std::uint8_t>(d.numbers[1]);
        out.size = 1;
        return true;
    }
    switch (type) {
        case sim::Type::Bool:   out.encoding = Encoding::BoolWord; out.size = 1; break;
        case sim::Type::Byte:   out.encoding = Encoding::Byte; out.size = 1; break;
        case sim::Type::Int:    out.encoding = Encoding::Int16; out.size = 1; break;
        case sim::Type::UInt:
        case sim::Type::Word:   out.encoding = Encoding::UInt16; out.size = 1; break;
        case sim::Type::DInt:
        case sim::Type::Time:   out.encoding = Encoding::Int32; out.size = 2; break;
        case sim::Type::UDInt:
        case sim::Type::DWord:  out.encoding = Encoding::UInt32; out.size = 2; break;
        case sim::Type::Real:   out.encoding = Encoding::Real32; out.size = 2; break;
        case sim::Type::String:
            out.encoding = Encoding::Text;
            out.size = static_cast<std::uint16_t>(std::max<int>(1, (chars + 1) / 2));
            break;
        default:
            return fail(why, "type " + std::string(sim::toString(type)) + " : pas lisible par Modbus");
    }
    if (offset + out.size > 65536) return fail(why, "au-del\xC3\xA0 de l'adresse 65535");
    return true;
}

void Plan::add(Point p) {
    const auto key = keyOf(p.name);
    if (key.empty()) return;
    if (const auto it = index_.find(key); it != index_.end()) {
        points_[it->second] = std::move(p);
        return;
    }
    index_.emplace(key, points_.size());
    points_.push_back(std::move(p));
}

void Plan::refuse(std::string name, std::string why) {
    const auto key = keyOf(name);
    if (key.empty() || refusedIndex_.count(key)) return;
    refusedIndex_.emplace(key, refused_.size());
    refused_.emplace_back(std::move(name), std::move(why));
}

std::optional<Point> Plan::resolve(std::string_view name) const {
    const std::string key = keyOf(name);
    if (key.empty()) return std::nullopt;
    if (const auto it = index_.find(key); it != index_.end()) {
        if (points_[it->second].array) return std::nullopt;   // un tableau entier : pas une valeur
        return points_[it->second];
    }
    // Une case d'un tableau : "tab[3]".
    if (key.back() == ']') {
        const auto open = key.rfind('[');
        if (open != std::string::npos && open > 0) {
            if (const auto it = index_.find(key.substr(0, open)); it != index_.end() && points_[it->second].array) {
                const std::string digits = key.substr(open + 1, key.size() - open - 2);
                if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '-'; })) {
                    const long long i = std::atoll(digits.c_str());
                    const auto& p = points_[it->second];
                    if (i >= p.low && i <= p.high) {
                        Point e = element(p, i);
                        e.name = std::string(trim(name));
                        return e;
                    }
                }
            }
        }
    }
    // Une adresse directe.
    if (key.front() == '%') {
        Point p;
        const auto t = typeOfAddress(key);
        if (t != sim::Type::Unknown && placeAddress(key, t, 16, p)) {
            p.name = std::string(trim(name));
            p.origin = "adresse";
            p.typeName = std::string(sim::toString(t));
            return p;
        }
    }
    return std::nullopt;
}

std::string Plan::whyNot(std::string_view name) const {
    std::string key = keyOf(name);
    if (key.empty()) return {};
    if (const auto it = refusedIndex_.find(key); it != refusedIndex_.end()) return refused_[it->second].second;
    // La racine : "armoires[0].ana.pt1.mes" -> "armoires".
    const auto cut = key.find_first_of(".[");
    if (cut != std::string::npos) {
        if (const auto it = refusedIndex_.find(key.substr(0, cut)); it != refusedIndex_.end()) return refused_[it->second].second;
        if (const auto it = index_.find(key.substr(0, cut)); it != index_.end()) {
            const auto& p = points_[it->second];
            if (p.array) return "hors du tableau " + p.name + "[" + std::to_string(p.low) + ".." + std::to_string(p.high) + "]";
        }
    }
    if (key.front() == '%') {
        Point p;
        std::string why;
        const auto t = typeOfAddress(key);
        if (t == sim::Type::Unknown || !placeAddress(key, t, 16, p, &why)) return why.empty() ? "adresse illisible" : why;
    }
    if (const auto it = index_.find(key); it != index_.end() && points_[it->second].array)
        return "un tableau entier : lis ses cases (" + points_[it->second].name + "[" + std::to_string(points_[it->second].low) + "])";
    return {};
}

std::string Plan::signature() const {
    std::string s;
    s.reserve(points_.size() * 24);
    for (const auto& p : points_) {
        s += p.name;
        s += '=';
        s += p.address;
        s += ':';
        s += std::string(sim::toString(p.type));
        s += p.writable ? '+' : '-';
        s += std::to_string(p.low) + ".." + std::to_string(p.high);
        s += ';';
    }
    s += '|';
    s += std::to_string(refused_.size());
    return s;
}

Plan buildPlan(const domain::Project* plc, const Communication& comm, const TypeOracle& types) {
    Plan plan;
    std::unordered_set<std::string> manual;
    for (const auto& a : comm.addresses) manual.insert(keyOf(a.variable));
    if (plc) {
        const auto& pr = *plc;
        for (const auto& v : pr.variables) {
            if (v.scope != domain::VariableScope::Global) continue;
            const std::string name(pr.strings.text(v.name));
            if (name.empty() || manual.count(keyOf(name))) continue;   // la table decide
            const std::string typeText(pr.strings.text(v.type.name));
            if (!v.located || !v.address.valid()) {
                // 1.11.1 (decision 134, R1111-12) : le tutoiement, comme le remede qui suit
                // cette raison dans la marque et l'infobulle de la colonne Accessible.
                plan.refuse(name, "non localis\xC3\xA9" "e : donne-lui une adresse (Configuration > Communication, table des adresses)");
                continue;
            }
            Point pt;
            pt.name = name;
            pt.origin = "programme";
            pt.typeName = typeText;
            pt.description = std::string(pr.strings.text(v.comment));
            std::string why;
            const std::string raw = v.address.raw.empty() ? std::string("?") : v.address.raw;
            if (v.type.klass == domain::TypeClass::Array) {
                const std::string elemName(pr.strings.text(v.type.elementType));
                const auto elem = sim::typeFromName(elemName);
                if (elem == sim::Type::Unknown) {
                    plan.refuse(name, "tableau de " + elemName + " : seuls les tableaux de types simples se lisent par Modbus");
                    continue;
                }
                if (!placeAddress(raw, elem, stringChars(elemName), pt, &why)) {
                    plan.refuse(name, raw + " : " + why);
                    continue;
                }
                if (pt.encoding == Encoding::BitOfWord) {
                    plan.refuse(name, raw + " : un tableau sur des bits de mot n'est pas pris en charge");
                    continue;
                }
                pt.array = true;
                pt.low = v.type.arrayLow;
                pt.high = v.type.arrayHigh;
                if (pt.high < pt.low || pt.offset + pt.span() > 65536) {
                    plan.refuse(name, raw + " : le tableau d\xC3\xA9" "borde la m\xC3\xA9moire (65536)");
                    continue;
                }
                plan.add(std::move(pt));
                continue;
            }
            if (v.type.klass == domain::TypeClass::Derived || v.type.klass == domain::TypeClass::FunctionBlock) {
                plan.refuse(name, "structure localis\xC3\xA9" "e (" + typeText + ") : donne une adresse \xC3\xA0 chaque membre utile (table des adresses)");
                continue;
            }
            const auto t = sim::typeFromName(typeText);
            if (t == sim::Type::Unknown) {
                plan.refuse(name, "type " + typeText + " : pas lisible par Modbus");
                continue;
            }
            if (!placeAddress(raw, t, v.type.stringLength ? static_cast<std::uint16_t>(v.type.stringLength) : stringChars(typeText), pt, &why)) {
                plan.refuse(name, raw + " : " + why);
                continue;
            }
            plan.add(std::move(pt));
        }
    }
    for (const auto& a : comm.addresses) {
        const std::string name = trim(a.variable);
        if (name.empty()) continue;
        Point pt;
        pt.name = name;
        pt.origin = "table";
        pt.description = a.description;
        std::string typeText = upper(trim(a.type));
        sim::Type t = typeText.empty() ? sim::Type::Unknown : sim::typeFromName(typeText);
        if (!typeText.empty() && t == sim::Type::Unknown) {
            plan.refuse(name, "type " + typeText + " : pas lisible par Modbus (BOOL, INT, UINT, WORD, DINT, UDINT, DWORD, REAL, TIME, STRING)");
            continue;
        }
        if (t == sim::Type::Unknown && types) {
            sim::Type known = sim::Type::Unknown;
            if (types(name, known)) t = known;
        }
        if (t == sim::Type::Unknown) t = typeOfAddress(a.address);
        if (t == sim::Type::Unknown) {
            plan.refuse(name, "type inconnu : pr\xC3\xA9" "cise-le (INT, REAL, BOOL...)");
            continue;
        }
        std::string why;
        if (!placeAddress(a.address, t, stringChars(typeText), pt, &why)) {
            plan.refuse(name, trim(a.address) + " : " + why);
            continue;
        }
        pt.typeName = typeText.empty() ? std::string(sim::toString(t)) : typeText;
        if (a.readOnly) pt.writable = false;
        plan.add(std::move(pt));
    }
    return plan;
}

// ------------------------------------------------------------ les valeurs ---
sim::Value decodeWords(const Point& p, const std::uint16_t* w, bool low) {
    switch (p.encoding) {
        case Encoding::Bit:       return sim::Value::boolean(w[0] != 0);
        case Encoding::BitOfWord: return sim::Value::boolean(((w[0] >> p.bit) & 1u) != 0);
        case Encoding::BoolWord:  return sim::Value::boolean(w[0] != 0);
        case Encoding::Byte:      return sim::Value::integer(sim::Type::Byte, w[0] & 0xFF);
        case Encoding::Int16:     return sim::Value::integer(p.type, static_cast<std::int16_t>(w[0]));
        case Encoding::UInt16:    return sim::Value::integer(p.type, w[0]);
        case Encoding::Int32: {
            const auto v = static_cast<std::int32_t>(join32(w, low));
            return p.type == sim::Type::Time ? sim::Value::time(v) : sim::Value::integer(p.type, v);
        }
        case Encoding::UInt32:    return sim::Value::integer(p.type, join32(w, low));
        case Encoding::Real32: {
            const std::uint32_t bitsOf = join32(w, low);
            float f = 0.f;
            std::memcpy(&f, &bitsOf, sizeof f);
            return sim::Value::real(roundFloat(f));
        }
        case Encoding::Text: {
            std::string s;
            for (std::size_t i = 0; i < p.size; ++i) {
                const char a = static_cast<char>(w[i] & 0xFF), b = static_cast<char>(w[i] >> 8);
                if (a == 0) break;
                s += a;
                if (b == 0) break;
                s += b;
            }
            return sim::Value::text(std::move(s));
        }
    }
    return {};
}

sim::Value decodeBit(const Point& p, bool bit) {
    (void)p;
    return sim::Value::boolean(bit);
}

std::vector<std::uint16_t> encodeWords(const Point& p, const sim::Value& v, bool low, std::uint16_t current) {
    std::vector<std::uint16_t> out;
    switch (p.encoding) {
        case Encoding::Bit:
        case Encoding::BoolWord:
            out = {static_cast<std::uint16_t>(v.isTruthy() ? 1 : 0)};
            break;
        case Encoding::BitOfWord: {
            const auto mask = static_cast<std::uint16_t>(1u << p.bit);
            out = {static_cast<std::uint16_t>(v.isTruthy() ? (current | mask) : (current & ~mask))};
            break;
        }
        case Encoding::Byte:
            out = {static_cast<std::uint16_t>(std::clamp<long long>(integerOf(v), 0, 255))};
            break;
        case Encoding::Int16:
            out = {static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp<long long>(integerOf(v), -32768, 32767)))};
            break;
        case Encoding::UInt16:
            out = {static_cast<std::uint16_t>(std::clamp<long long>(integerOf(v), 0, 65535))};
            break;
        case Encoding::Int32:
            split32(static_cast<std::uint32_t>(static_cast<std::int32_t>(std::clamp<long long>(integerOf(v), std::numeric_limits<std::int32_t>::min(),
                                                                                                std::numeric_limits<std::int32_t>::max()))),
                    low, out);
            break;
        case Encoding::UInt32:
            split32(static_cast<std::uint32_t>(std::clamp<long long>(integerOf(v), 0, 4294967295LL)), low, out);
            break;
        case Encoding::Real32: {
            const double d = v.type() == sim::Type::String ? std::atof(v.asString().c_str()) : v.asReal();
            const float f = static_cast<float>(d);
            std::uint32_t u = 0;
            std::memcpy(&u, &f, sizeof u);
            split32(u, low, out);
            break;
        }
        case Encoding::Text: {
            const std::string s = v.type() == sim::Type::String ? v.asString() : v.display();
            out.assign(p.size, 0);
            for (std::size_t i = 0; i < s.size() && i / 2 < p.size; ++i) {
                const auto c = static_cast<std::uint8_t>(s[i]);
                out[i / 2] = static_cast<std::uint16_t>(out[i / 2] | (i % 2 ? c << 8 : c));
            }
            break;
        }
    }
    return out;
}

std::vector<Block> planBlocks(const std::vector<Point>& points, int maxWords, int maxBits, int gap) {
    std::vector<Block> out;
    for (const Area area : {Area::Coils, Area::DiscreteInputs, Area::Holding, Area::InputRegisters}) {
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (points[i].area == area && !points[i].array) order.push_back(i);
        if (order.empty()) continue;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return points[a].offset != points[b].offset ? points[a].offset < points[b].offset : points[a].size > points[b].size;
        });
        const bool bits = area == Area::Coils || area == Area::DiscreteInputs;
        const std::size_t limit = static_cast<std::size_t>(bits ? std::clamp(maxBits, 1, modbus::kMaxReadBits)
                                                                : std::clamp(maxWords, 1, modbus::kMaxReadRegisters));
        const long long tolerance = std::max(0, gap) * (bits ? 16LL : 1LL);
        Block cur;
        bool open = false;
        for (const auto i : order) {
            const auto& p = points[i];
            const std::size_t first = p.offset;
            const std::size_t end = first + (bits ? 1u : p.size);
            if (open) {
                const std::size_t curEnd = static_cast<std::size_t>(cur.start) + cur.count;
                const long long hole = static_cast<long long>(first) - static_cast<long long>(curEnd);
                const std::size_t newEnd = std::max(curEnd, end);
                if (hole <= tolerance && newEnd - cur.start <= limit) {
                    cur.count = static_cast<std::uint16_t>(newEnd - cur.start);
                    cur.items.push_back(i);
                    continue;
                }
                out.push_back(std::move(cur));
                cur = Block{};
            }
            cur.area = area;
            cur.start = p.offset;
            cur.count = static_cast<std::uint16_t>(std::min(end - first, limit));
            cur.items = {i};
            open = true;
        }
        if (open) out.push_back(std::move(cur));
    }
    return out;
}

// ----------------------------------------------------------------- la liaison ---
Settings settingsOf(const Communication& c) {
    Settings s;
    s.host = trim(c.host);
    s.port = std::clamp(c.port, 1, 65535);
    s.unit = std::clamp(c.unit, 0, 255);
    s.timeoutMs = std::clamp(c.timeoutMs, 50, 60000);
    s.periodMs = std::clamp(c.periodMs, 50, 600000);
    s.retryS = std::clamp(c.retryS, 1, 3600);
    s.lowWordFirst = c.wordOrder != "fort";
    s.maxWords = std::clamp(c.maxWords, 1, modbus::kMaxReadRegisters);
    s.maxBits = std::clamp(c.maxBits, 1, modbus::kMaxReadBits);
    s.gap = std::clamp(c.gap, 0, 100);
    s.writes = c.writes;
    s.badAfterS = std::clamp(c.badAfterS, 1, 86400);
    return s;
}

std::string_view qualityName(Quality q) noexcept {
    switch (q) {
        case Quality::Good:    return "bonne";
        case Quality::Pending: return "en attente";
        case Quality::Stale:   return "ancienne";
        case Quality::Bad:     return "mauvaise";
        case Quality::None:    break;
    }
    return "";
}

namespace {
// Les fonctions standard (ABS, MAX, INT_TO_REAL...) d'une expression de vue :
// celles du simulateur, sans son programme - elles ne lisent rien de l'automate.
sim::Runtime& functionLibrary() {
    static sim::Runtime library(std::make_shared<domain::Project>());
    return library;
}
} // namespace

Link::Link(Plan plan, Settings settings) : plan_(std::move(plan)), settings_(std::move(settings)) {
    client_.setSettings({settings_.host, settings_.port, settings_.unit, settings_.timeoutMs});
    counters_.host = settings_.host;
    counters_.port = settings_.port;
    counters_.unit = settings_.unit;
    stateAt_ = monoNow();
    stateSince_ = wallStamp().substr(0, 19);
}

Link::~Link() { stop(); }

void Link::start() {
    if (thread_.joinable()) return;
    stop_.store(false);
    finished_.store(false);
    thread_ = std::thread([this] {
        loop();
        finished_.store(true);
    });
}

void Link::requestStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_.store(true);
    }
    wake_.notify_all();
}

void Link::stop() {
    requestStop();
    if (thread_.joinable()) thread_.join();
    client_.disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
}

void Link::loop() {
    while (!stop_.load()) {
        const double t0 = monoNow();
        cycle();
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_.load()) break;
        const double period = settings_.periodMs / 1000.0;
        double wait = connected_ ? period - (monoNow() - t0) : std::min(0.25, std::max(0.0, nextTry_ - monoNow()));
        wait = std::clamp(wait, 0.005, 60.0);
        wake_.wait_for(lock, std::chrono::duration<double>(wait), [this] {
            return stop_.load() || reconnectAsked_ || (connected_ && !writes_.empty());
        });
    }
}

void Link::event(std::string text) {
    events_.push_back(std::move(text));
    if (events_.size() > 200) events_.erase(events_.begin(), events_.begin() + 100);
}

void Link::lose(const std::string& why, double now) {
    counters_.lastError = why;
    counters_.lastErrorAt = wallStamp().substr(0, 19);
    if (connected_) {
        connected_ = false;
        stateAt_ = now;
        stateSince_ = counters_.lastErrorAt;
        event("Liaison perdue avec " + settings_.peer + " " + settings_.host + ":" + std::to_string(settings_.port) + " : " + why);
        nextTry_ = now + std::min(1.0, static_cast<double>(settings_.retryS));
    } else {
        nextTry_ = now + settings_.retryS;
    }
    timeoutsInRow_ = 0;
}

void Link::note(const modbus::Outcome& o, bool isWrite) {
    ++counters_.requests;
    if (isWrite) ++counters_.writes;
    if (o.ok || o.exception) {
        counters_.lastMs = o.ms;
        sumMs_ += o.ms;
        ++timedExchanges_;
        counters_.avgMs = sumMs_ / static_cast<double>(timedExchanges_);
        counters_.maxMs = std::max(counters_.maxMs, o.ms);
    }
    if (o.ok) return;
    ++counters_.errors;
    if (isWrite) ++counters_.writeErrors;
    if (o.timeout) ++counters_.timeouts;
    if (o.exception) ++counters_.exceptions;
    counters_.lastError = o.why;
    counters_.lastErrorAt = wallStamp().substr(0, 19);
}

bool Link::ensureConnected(double now) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (reconnectAsked_) {
            reconnectAsked_ = false;
            if (client_.connected()) {
                client_.disconnect();
                if (connected_) {
                    connected_ = false;
                    stateAt_ = now;
                    stateSince_ = wallStamp().substr(0, 19);
                    event("Liaison refaite \xC3\xA0 la demande (" + settings_.host + ":" + std::to_string(settings_.port) + ")");
                }
            }
            nextTry_ = 0;
        }
        if (client_.connected()) return true;
        if (now < nextTry_) return false;
    }
    const std::string where = settings_.host + ":" + std::to_string(settings_.port);
    const auto refused = [&](const modbus::Outcome& o, const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool first = counters_.lastError.empty() || connected_ || !cycled_;
        ++counters_.errors;
        if (o.timeout) ++counters_.timeouts;
        const std::string why = "connexion \xC3\xA0 " + where + " : " + text;
        if (first || counters_.lastError != why) event("Automate injoignable (" + where + ") : " + text);
        counters_.lastError = why;
        counters_.lastErrorAt = wallStamp().substr(0, 19);
        if (connected_) {
            connected_ = false;
            stateAt_ = now;
            stateSince_ = counters_.lastErrorAt;
        }
        nextTry_ = monoNow() + settings_.retryS;
        cycled_ = true;
        return false;
    };
    const auto o = client_.connect();
    if (!o.ok) return refused(o, o.why);
    modbus::labelClient(client_.localName(), "IHM");      // lot 17 : un jumeau sait que c'est l'IHM
    // La connexion TCP ne dit pas que l'automate repond (un serveur fige
    // accepte encore) : il faut une reponse Modbus - l'identification (une
    // exception compte : il a repondu), sinon un mot lu.
    modbus::Identification id;
    auto probe = client_.readIdentification(id);
    if (!probe.ok && !probe.exception && client_.connected()) {
        std::vector<std::uint16_t> w;
        probe = client_.readRegisters(false, 0, 1, w);
    }
    if (!probe.ok && !probe.exception) {
        client_.disconnect();
        return refused(probe, "connexion accept\xC3\xA9" "e mais aucune r\xC3\xA9ponse Modbus (" + probe.why + ")");
    }
    std::string device;
    for (const auto& [objectId, value] : id) {
        if (objectId > 6 || value.empty() || objectId == 3) continue;
        if (!device.empty()) device += " ";
        device += value;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    cycled_ = true;
    if (everConnected_) ++counters_.reconnects;
    event(std::string(everConnected_ ? "Liaison retrouv\xC3\xA9" "e" : "Liaison \xC3\xA9tablie") + " avec " + settings_.peer + " " + where + " ("
          + std::to_string(static_cast<long long>(std::lround(o.ms + probe.ms))) + " ms)");
    everConnected_ = true;
    connected_ = true;
    timeoutsInRow_ = 0;
    stateAt_ = monoNow();
    stateSince_ = wallStamp().substr(0, 19);
    counters_.device = !id.empty() ? (device.empty() ? std::string("(vide)") : device) : std::string("(identification non servie)");
    return true;
}

Link::Entry* Link::subscribe(std::string_view name, double now) {
    const std::string key = keyOf(name);
    if (const auto it = entries_.find(key); it != entries_.end()) return &it->second;
    auto p = plan_.resolve(name);
    if (!p) return nullptr;
    Entry e;
    e.point = std::move(*p);
    e.subscribedAt = now;
    e.usedAt = now;
    e.value = sim::Value::defaultOf(e.point.type);
    auto& stored = entries_.emplace(key, std::move(e)).first->second;
    wake_.notify_all();
    return &stored;
}

void Link::cycle() {
    const double now = monoNow();
    if (!ensureConnected(now) || stop_.load()) return;
    doWrites();
    if (stop_.load()) return;                                    // 1.11.7 : arretee, elle sort sans lire
    doReads(monoNow());
}

// 1.11.10 : les ecritures abandonnees (la liaison coupee) ne sont plus en attente.
void Link::dropWrites() {
    for (const auto& lost : writes_) {
        event("\xC3\x89" "criture de " + lost.point.name + " perdue : la liaison est coup\xC3\xA9" "e");
        if (const auto it = entries_.find(lost.key); it != entries_.end()) it->second.pendingWrites = 0;
    }
    writes_.clear();
}

void Link::doWrites() {
    // 1.11.10 (defaut du 05/10 soir : « des mauvaises donnees sur un esclave simule ») :
    // seulement les ecritures deja en attente au debut du cycle. Avant, la boucle
    // videait la file - et un script qui ecrit plus vite que la liaison n'envoie
    // la remplissait sans fin : les lectures ne repassaient plus (« pas relue
    // depuis 2 min 49 s »). Les suivantes partent au cycle d'apres, apres les lectures.
    std::size_t budget = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        budget = writes_.size();
    }
    for (; budget > 0; --budget) {
        PendingWrite w;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_.load()) return;                                // 1.11.7 : arretee, entre deux requetes
            if (writes_.empty() || !client_.connected()) {
                if (!writes_.empty()) dropWrites();
                return;
            }
            w = std::move(writes_.front());
            writes_.pop_front();
            if (const auto it = entries_.find(w.key); it != entries_.end() && it->second.pendingWrites > 0) --it->second.pendingWrites;
        }
        const auto& p = w.point;
        modbus::Outcome o;
        if (p.bits()) {
            o = client_.writeCoil(p.offset, w.value.isTruthy());
        } else if (p.encoding == Encoding::BitOfWord) {
            // Le bit d'un mot : le mot relu, son bit change, le mot reecrit.
            std::vector<std::uint16_t> cur;
            o = client_.readRegisters(false, p.offset, 1, cur);
            if (o.ok) o = client_.writeRegister(p.offset, encodeWords(p, w.value, settings_.lowWordFirst, cur.empty() ? 0 : cur.front()).front());
        } else {
            o = client_.writeRegisters(p.offset, encodeWords(p, w.value, settings_.lowWordFirst));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        note(o, true);
        if (o.ok) {
            if (const auto it = entries_.find(w.key); it != entries_.end()) {
                it->second.readAt = monoNow();
                it->second.hasValue = true;
            }
            continue;
        }
        event("\xC3\x89" "criture de " + p.name + " (" + p.address + ") refus\xC3\xA9" "e : " + o.why);
        if (o.lost || o.timeout) {
            // Jamais rejouees apres une coupure : une commande d'il y a dix secondes
            // n'est plus celle de l'operateur.
            dropWrites();
            if (o.lost) lose(o.why, monoNow());
            else if (++timeoutsInRow_ >= 2) {
                lose(o.why, monoNow());
                client_.disconnect();
            }
            return;
        }
    }
}

void Link::doReads(double now) {
    struct Item {
        std::string key;
        Point       point;
    };
    std::vector<Item> items, retries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();) {
            auto& e = it->second;
            if (!e.followed && now - e.usedAt > keep_) {     // 1.9 : une suivie reste
                it = entries_.erase(it);
                continue;
            }
            if (e.excluded) {
                if (now >= e.retryAt) retries.push_back({it->first, e.point});
            } else {
                items.push_back({it->first, e.point});
            }
            ++it;
        }
    }
    const bool low = settings_.lowWordFirst;
    const auto store = [&](const std::string& key, sim::Value v) {
        if (const auto it = entries_.find(key); it != entries_.end()) {
            // 1.11.10 : une ecriture encore en attente garde sa valeur (l'appareil
            // a encore l'ancienne) ; la place est lue quand meme : elle est bonne.
            if (it->second.pendingWrites == 0) it->second.value = std::move(v);
            it->second.hasValue = true;
            it->second.readAt = monoNow();
            it->second.excluded = false;
            it->second.why.clear();
        }
    };
    const auto exclude = [&](const std::string& key, const std::string& why) {
        if (const auto it = entries_.find(key); it != entries_.end()) {
            it->second.excluded = true;
            it->second.why = "refus\xC3\xA9" "e par l'automate (" + it->second.point.address + ") : " + why;
            it->second.retryAt = monoNow() + 30.0;
        }
    };
    // Lire une place seule : vrai si la liaison tient toujours.
    const auto readOne = [&](const Item& item) -> bool {
        const auto& p = item.point;
        modbus::Outcome o;
        sim::Value v;
        if (p.bits()) {
            std::vector<bool> b;
            o = client_.readBits(p.area == Area::DiscreteInputs, p.offset, 1, b);
            if (o.ok && !b.empty()) v = decodeBit(p, b.front());
        } else {
            std::vector<std::uint16_t> w;
            o = client_.readRegisters(p.area == Area::InputRegisters, p.offset, p.size, w);
            if (o.ok && w.size() >= p.size) v = decodeWords(p, w.data(), low);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        note(o, false);
        if (o.ok) {
            store(item.key, std::move(v));
            timeoutsInRow_ = 0;
            return true;
        }
        if (o.exception) {
            exclude(item.key, "exception " + modbus::exceptionText(o.exception));
            return true;
        }
        if (o.timeout && ++timeoutsInRow_ < 2) return true;
        lose(o.why, monoNow());
        return false;
    };

    std::vector<Point> pts;
    pts.reserve(items.size());
    for (const auto& i : items) pts.push_back(i.point);
    const auto blocks = planBlocks(pts, settings_.maxWords, settings_.maxBits, settings_.gap);
    const double t0 = monoNow();
    std::size_t requests = 0;
    bool alive = true;
    for (const auto& b : blocks) {
        if (!alive || !client_.connected() || stop_.load()) break;   // 1.11.7 : arretee, entre deux requetes
        ++requests;
        modbus::Outcome o;
        std::vector<bool> bitsRead;
        std::vector<std::uint16_t> wordsRead;
        const bool isBits = b.area == Area::Coils || b.area == Area::DiscreteInputs;
        if (isBits) o = client_.readBits(b.area == Area::DiscreteInputs, b.start, b.count, bitsRead);
        else o = client_.readRegisters(b.area == Area::InputRegisters, b.start, b.count, wordsRead);
        std::unique_lock<std::mutex> lock(mutex_);
        note(o, false);
        if (o.ok) {
            timeoutsInRow_ = 0;
            for (const auto k : b.items) {
                const auto& p = pts[k];
                const std::size_t at = p.offset - b.start;
                if (isBits) {
                    if (at < bitsRead.size()) store(items[k].key, decodeBit(p, bitsRead[at]));
                } else if (at + p.size <= wordsRead.size()) {
                    store(items[k].key, decodeWords(p, wordsRead.data() + at, low));
                }
            }
            continue;
        }
        if (o.exception) {
            if (b.items.size() == 1) {
                exclude(items[b.items.front()].key, "exception " + modbus::exceptionText(o.exception));
                continue;
            }
            // Un bloc refuse : chaque place seule, pour trouver celle qui gene.
            lock.unlock();
            for (const auto k : b.items) {
                if (stop_.load()) break;
                ++requests;
                if (!(alive = readOne(items[k]))) break;
            }
            continue;
        }
        if (o.timeout && ++timeoutsInRow_ < 2) continue;
        lose(o.why, monoNow());
        alive = false;
    }
    for (const auto& r : retries) {
        if (!alive || !client_.connected() || stop_.load()) break;
        ++requests;
        alive = readOne(r);
    }
    if (!alive) client_.disconnect();
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.cycleMs = (monoNow() - t0) * 1000.0;
    counters_.requestsPerCycle = requests;
}

bool Link::read(std::string_view name, sim::Value& out) {
    const double now = monoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    Entry* e = subscribe(name, now);
    if (!e) {
        // Une variable de l'automate sans place : le diagnostic la montre.
        const std::string key = keyOf(name);
        if (auto it = missing_.find(key); it != missing_.end()) it->second.usedAt = now;
        else if (missing_.size() < 500)
            if (const auto why = plan_.whyNot(name); !why.empty()) missing_.emplace(key, Missing{std::string(name), "sans adresse Modbus : " + why, now});
        return false;
    }
    e->usedAt = now;
    out = e->value;
    return true;
}

bool Link::write(std::string_view name, const sim::Value& value) {
    const double now = monoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    Entry* e = subscribe(name, now);
    if (!e) return false;
    const auto& p = e->point;
    if (!settings_.writes) {
        event("\xC3\x89" "criture de " + p.name + " refus\xC3\xA9" "e : la liaison est en lecture seule (Configuration > Communication)");
        return false;
    }
    if (!p.writable) {
        event("\xC3\x89" "criture de " + p.name + " refus\xC3\xA9" "e : " + p.address + " se lit seulement");
        return false;
    }
    if (!connected_) {
        event("\xC3\x89" "criture de " + p.name + " refus\xC3\xA9" "e : " + settings_.peer + " n'est pas joint");
        return false;
    }
    // La valeur dans le type de la place (un REAL ecrit dans un INT : arrondi).
    sim::Value shown = value;
    if (!p.bits()) {
        const auto words = encodeWords(p, value, settings_.lowWordFirst, 0);
        if (p.encoding != Encoding::BitOfWord) shown = decodeWords(p, words.data(), settings_.lowWordFirst);
        else shown = sim::Value::boolean(value.isTruthy());
    } else {
        shown = sim::Value::boolean(value.isTruthy());
    }
    // 1.11.10 : les ecritures d'une meme place en attente se regroupent. La meme
    // valeur que la derniere en attente : rien de plus ; deux deja en attente : la
    // seconde prend la nouvelle valeur. La file reste bornee (deux par place) ; un
    // 1 puis un 0 (une impulsion) partent tous les deux, et la derniere valeur
    // demandee part toujours.
    const std::string key = keyOf(name);
    PendingWrite* last = nullptr;
    int queued = 0;
    for (auto it = writes_.rbegin(); it != writes_.rend(); ++it) {
        if (it->key != key) continue;
        if (!last) last = &*it;
        ++queued;
    }
    if (last && last->value.equals(value)) {
        // deja demandee
    } else if (last && queued >= 2) {
        last->point = p;
        last->value = value;
    } else {
        writes_.push_back({key, p, value});
        ++e->pendingWrites;
    }
    e->value = shown;
    e->hasValue = true;
    e->usedAt = now;
    e->writtenAt = now;
    wake_.notify_all();
    return true;
}

Link::Activity Link::activity(std::string_view name) const {
    Activity a;
    a.now = monoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(keyOf(name));
    if (it == entries_.end()) return a;
    a.readAt = it->second.readAt;
    a.writtenAt = it->second.writtenAt;
    return a;
}

bool Link::exists(std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.count(keyOf(name))) return true;
    return plan_.resolve(name).has_value();
}

bool Link::call(std::string_view name, std::string_view instance, const std::vector<std::pair<std::string, sim::Value>>& arguments,
                sim::Value& result) {
    // L'interpreteur passe le nom en instance aussi : un appel de fonction et un
    // appel d'instance se ressemblent. La bibliotheque n'a pas d'instance - un
    // bloc de l'automate n'est pas appelable par Modbus.
    return functionLibrary().call(name, instance, arguments, result);
}

Quality Link::quality(std::string_view name, std::string* why) const {
    const double now = monoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(keyOf(name));
    if (it == entries_.end()) {
        if (plan_.resolve(name)) {
            if (why) *why = "pas encore lue";
            return Quality::Pending;
        }
        const auto reason = plan_.whyNot(name);
        if (reason.empty()) return Quality::None;
        if (why) *why = "sans adresse Modbus : " + reason;
        return Quality::Bad;
    }
    const Entry& e = it->second;
    if (e.excluded) {
        if (why) *why = e.why;
        return Quality::Bad;
    }
    const double staleAfter = std::max(2.0, (3.0 * settings_.periodMs + settings_.timeoutMs) / 1000.0);
    if (e.readAt >= 0) {
        const double age = now - e.readAt;
        if (connected_ && age <= staleAfter) return Quality::Good;
        if (age <= settings_.badAfterS) {
            if (why) *why = "ancienne : lue il y a " + seconds(age) + (connected_ ? std::string{} : " (liaison perdue)");
            return Quality::Stale;
        }
        if (why) *why = connected_ ? "pas relue depuis " + seconds(age) : "liaison perdue depuis " + seconds(now - stateAt_);
        return Quality::Bad;
    }
    if (!cycled_ || (connected_ && now - e.subscribedAt <= staleAfter)) {
        if (why) *why = "en attente de la premi\xC3\xA8re lecture";
        return Quality::Pending;
    }
    if (why) *why = connected_ ? std::string("jamais lue") : "jamais lue : " + (counters_.lastError.empty() ? std::string("automate pas joint") : counters_.lastError);
    return Quality::Bad;
}

Diagnostics Link::diagnostics() const {
    const double now = monoNow();
    Diagnostics d;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        d = counters_;
        d.connected = connected_;
        d.state = connected_ ? "Connect\xC3\xA9" "e" : !cycled_ ? (thread_.joinable() ? "Connexion..." : "Arr\xC3\xAAt\xC3\xA9" "e") : "D\xC3\xA9" "connect\xC3\xA9" "e";
        d.since = stateSince_;
        d.stateSeconds = now - stateAt_;
        d.retryIn = connected_ ? 0.0 : std::max(0.0, nextTry_ - now);
        d.subscribed = entries_.size();
        for (const auto& [key, e] : entries_) d.followed += e.followed ? 1 : 0;
    }
    // Les qualites (quality() reprend le verrou : hors du bloc ci-dessus).
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        names.reserve(entries_.size());
        for (const auto& [key, e] : entries_) names.push_back(e.point.name);
    }
    std::sort(names.begin(), names.end());
    {
        // Les demandees sans place (depuis moins de `keep_` s) : mauvaises.
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<const Missing*> gone;
        for (const auto& [key, m] : missing_) {
            if (now - m.usedAt > keep_) continue;
            ++d.subscribed;
            ++d.bad;
            gone.push_back(&m);
        }
        std::sort(gone.begin(), gone.end(), [](const Missing* a, const Missing* b) { return a->name < b->name; });
        for (const auto* m : gone)
            if (d.badPoints.size() < 50) d.badPoints.emplace_back(m->name, m->why);
    }
    for (const auto& n : names) {
        std::string why;
        switch (quality(n, &why)) {
            case Quality::Good:    ++d.good; break;
            case Quality::Pending: ++d.pending; break;
            case Quality::Stale:
                ++d.stale;
                if (d.badPoints.size() < 50) d.badPoints.emplace_back(n, why);
                break;
            case Quality::Bad:
                ++d.bad;
                if (d.badPoints.size() < 50) d.badPoints.emplace_back(n, why);
                break;
            case Quality::None:    break;
        }
    }
    return d;
}

bool Link::connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

void Link::reconnect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reconnectAsked_ = true;
        nextTry_ = 0;
    }
    wake_.notify_all();
}

void Link::resetCounters() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string device = counters_.device;
    counters_ = Diagnostics{};
    counters_.host = settings_.host;
    counters_.port = settings_.port;
    counters_.unit = settings_.unit;
    counters_.device = device;
    sumMs_ = 0;
    timedExchanges_ = 0;
}

std::vector<std::string> Link::takeEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

void Link::setKeepSeconds(double s) {
    std::lock_guard<std::mutex> lock(mutex_);
    keep_ = std::max(0.5, s);
}

std::size_t Link::follow(std::string_view owner, const std::vector<std::string>& names) {
    const double now = monoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string who(owner);
    const auto mine = follows_.find(who);
    if (mine == follows_.end() ? names.empty() : mine->second == names) return 0;   // inchangee
    if (names.empty()) follows_.erase(mine);
    else follows_[who] = names;
    // Abonnees tout de suite : la tache les lit des son prochain cycle.
    std::size_t added = 0;
    for (const auto& name : names) {
        const bool known = entries_.count(keyOf(name)) != 0;
        if (subscribe(name, now) && !known) ++added;
    }
    // Les marques, pour toutes les listes : ce qui n'est plus suivi par personne
    // redevient ordinaire (oublie dans `keep_` s si rien ne le lit).
    std::set<std::string> keys;
    for (const auto& [o, list] : follows_)
        for (const auto& name : list) keys.insert(keyOf(name));
    for (auto& [key, e] : entries_) {
        const bool f = keys.count(key) != 0;
        if (e.followed && !f) e.usedAt = now;
        e.followed = f;
    }
    return added;
}

std::vector<std::string> Link::followed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    for (const auto& [key, e] : entries_)
        if (e.followed) out.push_back(e.point.name);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ------------------------------------------------ le serveur de demonstration ---
SimBank::SimBank(Plan plan, bool lowWordFirst, std::size_t bits, std::size_t words, modbus::Identification id)
    : plan_(std::move(plan)), low_(lowWordFirst), words_(std::clamp<std::size_t>(words, 1, 65536), 0),
      bits_(std::clamp<std::size_t>(bits, 1, 65536), 0), id_(std::move(id)) {}

void SimBank::exchange(sim::Environment& plc) {
    std::deque<Write> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending.swap(writes_);
    }
    for (const auto& w : pending) (void)plc.write(w.name, w.value);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto publish = [&](const Point& p, const std::string& name) {
        sim::Value v;
        if (!plc.read(name, v)) return;
        if (p.bits()) {
            if (p.offset < bits_.size()) bits_[p.offset] = v.isTruthy() ? 1 : 0;
            return;
        }
        if (static_cast<std::size_t>(p.offset) + p.size > words_.size()) return;
        const auto w = encodeWords(p, v, low_, words_[p.offset]);
        for (std::size_t k = 0; k < w.size() && k < p.size; ++k) words_[p.offset + k] = w[k];
    };
    for (const auto& p : plan_.points()) {
        if (!p.array) {
            publish(p, p.name);
            continue;
        }
        for (long long i = p.low; i <= p.high; ++i) publish(element(p, i), p.name + "[" + std::to_string(i) + "]");
    }
}

int SimBank::readBits(bool, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + count > bits_.size()) return 2;
    out.assign(count, false);
    for (std::size_t i = 0; i < count; ++i) out[i] = bits_[address + i] != 0;
    return 0;
}

int SimBank::readRegisters(bool, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + count > words_.size()) return 2;
    out.assign(words_.begin() + address, words_.begin() + address + count);
    return 0;
}

int SimBank::writeBits(std::uint16_t address, const std::vector<bool>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + values.size() > bits_.size()) return 2;
    for (std::size_t i = 0; i < values.size(); ++i) bits_[address + i] = values[i] ? 1 : 0;
    ++clientWrites_;
    queueTouched(Area::Coils, address, values.size());
    return 0;
}

int SimBank::writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + values.size() > words_.size()) return 2;
    std::copy(values.begin(), values.end(), words_.begin() + address);
    ++clientWrites_;
    queueTouched(Area::Holding, address, values.size());
    return 0;
}

void SimBank::queueTouched(Area area, std::size_t first, std::size_t n) {
    const std::size_t last = first + n;   // exclu
    const auto touch = [&](const Point& p, const std::string& name) {
        if (!p.writable) return;
        const std::size_t a = p.offset, b = p.offset + (p.bits() ? 1u : p.size);
        if (b <= first || a >= last) return;
        sim::Value v = p.bits() ? decodeBit(p, bits_[p.offset] != 0) : decodeWords(p, words_.data() + p.offset, low_);
        writes_.push_back({name, std::move(v)});
    };
    for (const auto& p : plan_.points()) {
        if (p.area != area) continue;
        const std::size_t a = p.offset, b = p.offset + p.span();
        if (b <= first || a >= last) continue;
        if (!p.array) {
            touch(p, p.name);
            continue;
        }
        for (long long i = p.low; i <= p.high; ++i) touch(element(p, i), p.name + "[" + std::to_string(i) + "]");
    }
}

modbus::Identification SimBank::identification() {
    std::lock_guard<std::mutex> lock(mutex_);
    return id_;
}

std::uint64_t SimBank::clientWrites() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clientWrites_;
}

std::shared_ptr<SimBank> makeSimBank(const domain::Project* plc, const Communication& comm, const TypeOracle& types) {
    std::size_t bits = 65536, words = 65536;
    modbus::Identification id{{0, "XpgAnalyzer"}, {1, "SIMULATEUR"}, {2, XPG_ANALYZER_VERSION}, {4, "Automate simul\xC3\xA9"}};
    if (plc) {
        const auto& hw = plc->hardware;
        if (hw.memory.declared) {
            if (hw.memory.internalBits) bits = hw.memory.internalBits;
            if (hw.memory.internalWords) words = hw.memory.internalWords;
        }
        if (!hw.cpuReference.empty()) id[1].second = hw.cpuReference;
        if (!hw.family.empty()) id.emplace_back(5, hw.family);
        if (!plc->header.projectName.empty()) id.emplace_back(6, plc->header.projectName);
    }
    Plan plan = buildPlan(plc, comm, types);
    // La memoire couvre au moins ce que le plan place.
    for (const auto& p : plan.points()) {
        if (p.bits()) bits = std::max(bits, static_cast<std::size_t>(p.offset) + p.span());
        else words = std::max(words, static_cast<std::size_t>(p.offset) + p.span());
    }
    return std::make_shared<SimBank>(std::move(plan), comm.wordOrder != "fort", bits, words, std::move(id));
}


// ---------------------------------------------------------------- les objets ---
namespace {

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '%'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool keyword(std::string_view w) {
    static const char* const kWords[] = {"TRUE", "FALSE", "AND", "OR", "XOR", "NOT", "MOD", "IF", "THEN", "ELSE", "ELSIF", "END_IF",
                                         "VAR", "END_VAR", "FOR", "TO", "BY", "DO", "END_FOR", "WHILE", "END_WHILE", "REPEAT",
                                         "UNTIL", "END_REPEAT", "CASE", "OF", "END_CASE", "RETURN", "EXIT"};
    const std::string u = upper(w);
    for (const char* k : kWords)
        if (u == k) return true;
    return false;
}

// Les chemins d'un morceau d'expression (sans accolades).
void scanPaths(std::string_view s, std::vector<std::string>& out) {
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {   // une chaine : sautee
            const char q = c;
            ++i;
            while (i < s.size() && s[i] != q) i += s[i] == '$' ? 2 : 1;
            ++i;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {   // un nombre, 16#FF, 1.5E3
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '#' || s[i] == '.' || s[i] == '_')) ++i;
            continue;
        }
        if (!identStart(c) || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '#' || s[i - 1] == '.'))) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        ++i;
        while (i < s.size() && (identChar(s[i]) || (s[start] == '%' && s[i] == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))))) ++i;
        std::string path(s.substr(start, i - start));
        bool dynamic = false;
        for (;;) {
            if (i < s.size() && s[i] == '.' && i + 1 < s.size() && identStart(s[i + 1]) && s[i + 1] != '%') {
                std::size_t j = i + 1;
                while (j < s.size() && identChar(s[j])) ++j;
                path += s.substr(i, j - i);
                i = j;
                continue;
            }
            if (i < s.size() && s[i] == '[') {
                const auto close = s.find(']', i);
                if (close == std::string_view::npos) break;
                std::string inside = trim(s.substr(i + 1, close - i - 1));
                if (inside.empty() || !std::all_of(inside.begin(), inside.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) || ch == '-'; }))
                    dynamic = true;
                path += "[" + inside + "]";
                i = close + 1;
                continue;
            }
            break;
        }
        std::size_t k = i;
        while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
        const bool call = k < s.size() && s[k] == '(';
        const bool typed = i < s.size() && s[i] == '#';   // T#5s, INT#3
        if (dynamic || call || typed || keyword(path)) continue;
        if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(std::move(path));
    }
}

std::string grouped(std::uint64_t v) {
    std::string d = std::to_string(v), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += ' ';
        out += d[i];
    }
    return out;
}

std::string ms(double v) {
    if (v < 10) {
        char b[32];
        std::snprintf(b, sizeof b, "%.1f", v);
        std::string t = b;
        for (auto& ch : t)
            if (ch == '.') ch = ',';
        return t + " ms";
    }
    return std::to_string(static_cast<long long>(std::lround(v))) + " ms";
}

std::string hourOf(const std::string& stamp) { return stamp.size() >= 19 ? stamp.substr(11, 8) : stamp; }

} // namespace

std::vector<std::string> plcPaths(std::string_view expression) {
    std::vector<std::string> out;
    const auto open = expression.find('{');
    if (open == std::string_view::npos) {
        scanPaths(expression, out);
        return out;
    }
    // Un texte a trous : ce qui est entre accolades, sans son format ({P:0.0}).
    std::size_t i = open;
    while (i < expression.size()) {
        const auto a = expression.find('{', i);
        if (a == std::string_view::npos) break;
        const auto b = expression.find('}', a + 1);
        if (b == std::string_view::npos) break;
        std::string_view hole = expression.substr(a + 1, b - a - 1);
        // Le format suit le dernier ':' hors d'une chaine.
        bool quoted = false;
        std::size_t colon = std::string_view::npos;
        for (std::size_t k = 0; k < hole.size(); ++k) {
            if (hole[k] == '\'') quoted = !quoted;
            else if (hole[k] == ':' && !quoted && (k + 1 >= hole.size() || hole[k + 1] != '=')) colon = k;
        }
        if (colon != std::string_view::npos) hole = hole.substr(0, colon);
        scanPaths(hole, out);
        i = b + 1;
    }
    return out;
}

std::vector<std::string> projectPlcPaths(const Project& p) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    const auto isHmiName = [&](std::string_view root) {
        if (root.empty()) return true;
        const std::string r = upper(root);
        if (r == "SYS") return true;
        if (p.variable(root) || p.viewByName(root) || p.functionByName(root)) return true;
        for (const auto& v : p.views)
            for (const auto& prm : v.params)
                if (upper(prm.name) == r) return true;
        return false;
    };
    const auto take = [&](std::string_view source) {
        if (source.empty()) return;
        for (auto& path : plcPaths(source)) {
            const auto root = std::string_view(path).substr(0, path.find_first_of(".["));
            if (isHmiName(root)) continue;
            if (seen.insert(keyOf(path)).second) out.push_back(std::move(path));
        }
    };
    const auto takeActions = [&](const std::vector<Action>& actions) {
        for (const auto& a : actions) {
            take(a.watch);
            take(a.guard);
            take(a.target);
            take(a.value);
            for (const auto& kv : actionkinds::params(a)) take(kv.second);      // 1.11.6
        }
    };
    for (const auto& v : p.views) {
        for (const auto& o : v.objects) {
            for (const auto& pr : o.props) {
                if (!pr.expr.empty()) take(pr.expr);
                else if (pr.value.find('{') != std::string::npos) take(pr.value);
                else if ((pr.key == "variable" || pr.key == "xVariable" || pr.key == "feedback") && !pr.value.empty()) take(pr.value);
            }
            takeActions(o.actions);
        }
        takeActions(v.actions);
        for (const auto& sc : v.scripts) take(sc.body);
    }
    for (const auto& sc : p.programs.scripts) take(sc.body);
    for (const auto& a : p.alarms) {
        take(a.condition);
        take(a.message);
    }
    // 1.9 : les alarmes des objets (synoptique, symboles), cochees, comme celles du projet.
    for (const auto& oa : objectAlarms(p)) {
        if (!oa.active) continue;
        take(oa.def.condition);
        take(oa.def.message);
    }
    for (const auto& r : p.recipes)
        for (const auto& f : r.fields) take(f.variable);
    for (const auto& e : p.history.archived) take(e);
    return out;
}

StatusLine commStatusLine(const Communication& comm, const Link* link, bool simulatorAttached, bool showAddress, bool showTime,
                          bool compact) {
    StatusLine line;
    const std::string where = comm.host + ":" + std::to_string(comm.port);
    if (!link) {
        line.tone = 4;
        if (compact) line.text = "Simulateur";
        else if (comm.modbus()) line.text = "Modbus TCP \xC3\xA0 l'arr\xC3\xAAt" + (showAddress ? " \xE2\x80\x94 " + where : std::string{});
        else line.text = simulatorAttached ? "Simulateur \xE2\x80\x94 automate simul\xC3\xA9 reli\xC3\xA9" : "Simulateur \xE2\x80\x94 aucun automate";
        if (comm.modbus()) line.tone = 3;
        return line;
    }
    const auto d = link->diagnostics();
    const std::string addr = d.host + ":" + std::to_string(d.port);
    if (d.connected) {
        line.tone = d.bad || d.stale ? 2 : 1;
        if (compact) {
            line.text = "Connect\xC3\xA9";
            return line;
        }
        line.text = "Automate connect\xC3\xA9";
        if (showAddress) line.text += " \xE2\x80\x94 " + addr;
        if (showTime && (d.lastMs > 0 || d.requests)) line.text += " \xE2\x80\x94 " + ms(d.lastMs);
        if (d.bad || d.stale) line.text += " \xE2\x80\x94 " + std::to_string(d.bad + d.stale) + " variable" + (d.bad + d.stale > 1 ? "s" : "") + " en d\xC3\xA9" "faut";
        return line;
    }
    if (d.state.rfind("Connexion", 0) == 0 || d.state.rfind("Arr", 0) == 0) {
        line.tone = 2;
        line.text = compact ? std::string("Connexion...") : "Connexion \xC3\xA0 " + addr + "...";
        return line;
    }
    line.tone = 3;
    if (compact) {
        line.text = "D\xC3\xA9" "connect\xC3\xA9";
        return line;
    }
    line.text = "Automate injoignable";
    if (showAddress) line.text += " \xE2\x80\x94 " + addr;
    if (d.retryIn > 0.05) line.text += " \xE2\x80\x94 nouvel essai dans " + std::to_string(static_cast<long long>(std::ceil(d.retryIn))) + " s";
    return line;
}

std::vector<DiagnosticRow> diagnosticRows(const Communication& comm, const Link* link, bool simulatorAttached, bool demoRunning, int demoPort) {
    std::vector<DiagnosticRow> rows;
    const std::string demo = demoRunning ? "en marche, port " + std::to_string(demoPort) : std::string("arr\xC3\xAAt\xC3\xA9");
    if (!link) {
        rows.push_back({"Liaison", comm.modbus() ? std::string("Modbus TCP \xC3\xA0 l'arr\xC3\xAAt") : std::string("Simulateur (dans l'application)"),
                        comm.modbus() ? 3 : 4});
        rows.push_back({"Automate simul\xC3\xA9", simulatorAttached ? "reli\xC3\xA9" : "absent", simulatorAttached ? 1 : 2});
        rows.push_back({"Serveur de d\xC3\xA9monstration", demo, demoRunning ? 1 : 0});
        rows.push_back({"Automate r\xC3\xA9" "el", "Configuration > Communication : Modbus TCP, son adresse IP", 0});
        return rows;
    }
    const auto d = link->diagnostics();
    rows.push_back({"Liaison", "Modbus TCP", 0});
    rows.push_back({"Adresse", d.host + ":" + std::to_string(d.port) + " \xC2\xB7 esclave " + std::to_string(d.unit), 0});
    if (d.connected) {
        rows.push_back({"\xC3\x89tat", "Connect\xC3\xA9" "e depuis " + hourOf(d.since), 1});
    } else if (d.state.rfind("Connexion", 0) == 0 || d.state.rfind("Arr", 0) == 0) {
        rows.push_back({"\xC3\x89tat", "Connexion...", 2});
    } else {
        std::string t = "D\xC3\xA9" "connect\xC3\xA9" "e depuis " + hourOf(d.since);
        if (d.retryIn > 0.05) t += " \xE2\x80\x94 nouvel essai dans " + std::to_string(static_cast<long long>(std::ceil(d.retryIn))) + " s";
        rows.push_back({"\xC3\x89tat", t, 3});
    }
    rows.push_back({"Identification", d.device.empty() ? std::string("\xE2\x80\x94") : d.device, 0});
    rows.push_back({"Temps de r\xC3\xA9ponse", d.requests ? ms(d.lastMs) + " (moyen " + ms(d.avgMs) + ", max " + ms(d.maxMs) + ")" : std::string("\xE2\x80\x94"),
                    d.maxMs > comm.timeoutMs * 0.5 ? 2 : 0});
    rows.push_back({"Cycle de lecture", ms(d.cycleMs) + ", " + std::to_string(d.requestsPerCycle) + " requ\xC3\xAAte" + (d.requestsPerCycle > 1 ? "s" : "")
                        + ", toutes les " + std::to_string(comm.periodMs) + " ms",
                    d.cycleMs > comm.periodMs ? 2 : 0});
    rows.push_back({"Requ\xC3\xAAtes", grouped(d.requests) + " (erreurs " + grouped(d.errors) + ", d\xC3\xA9lais " + grouped(d.timeouts) + ", exceptions "
                        + grouped(d.exceptions) + ")",
                    d.errors ? 2 : 0});
    rows.push_back({"Reconnexions", grouped(d.reconnects), d.reconnects ? 2 : 0});
    std::string vars = grouped(d.subscribed) + " suivie" + (d.subscribed > 1 ? "s" : "") + " : " + grouped(d.good) + " bonne" + (d.good > 1 ? "s" : "");
    if (d.pending) vars += ", " + grouped(d.pending) + " en attente";
    if (d.stale) vars += ", " + grouped(d.stale) + " ancienne" + (d.stale > 1 ? "s" : "");
    if (d.bad) vars += ", " + grouped(d.bad) + " mauvaise" + (d.bad > 1 ? "s" : "");
    rows.push_back({"Variables", vars, d.bad ? 3 : d.stale ? 2 : d.subscribed ? 1 : 0});
    rows.push_back({"\xC3\x89" "critures", grouped(d.writes) + (d.writeErrors ? " (refus\xC3\xA9" "es " + grouped(d.writeErrors) + ")" : std::string{})
                        + (comm.writes ? std::string{} : " \xE2\x80\x94 lecture seule"),
                    d.writeErrors ? 2 : 0});
    rows.push_back({"Derni\xC3\xA8re erreur", d.lastError.empty() ? std::string("aucune") : hourOf(d.lastErrorAt) + " \xE2\x80\x94 " + d.lastError,
                    d.lastError.empty() ? 0 : d.connected ? 2 : 3});
    rows.push_back({"Serveur de d\xC3\xA9monstration", demo, demoRunning ? 1 : 0});
    return rows;
}

DiagnosticLayout diagnosticLayout(const Object& o, double w, double h, std::size_t rows) {
    DiagnosticLayout l;
    l.fontSize = std::clamp(o.number("fontSize", 13), 8.0, 40.0);
    const double pad = 10;
    const double titleH = std::round(l.fontSize * 2.3);
    l.title = {0, 0, w, std::min(h, titleH)};
    const bool buttons = o.flag("showButtons", true);
    const double bh = buttons ? std::max(28.0, std::round(l.fontSize * 2.4)) : 0.0;
    if (buttons) {
        const double by = h - pad - bh;
        const double bw = (w - 3 * pad) / 2;
        l.reconnect = {pad, by, std::max(0.0, bw), bh};
        l.reset = {2 * pad + bw, by, std::max(0.0, bw), bh};
    }
    const double top = l.title.h + 6;
    const double bottom = buttons ? h - 2 * pad - bh : h - pad;
    l.rowH = std::round(l.fontSize * 1.75);
    l.labelW = std::clamp(w * 0.34, 90.0, 240.0);
    const double need = static_cast<double>(rows) * l.rowH;
    const double avail = std::max(0.0, bottom - top);
    l.rows = {pad, top, std::max(0.0, w - 2 * pad), std::min(need, avail)};
    if (o.flag("showBad", true) && avail - need > l.rowH * 2)
        l.list = {pad, top + need + 6, std::max(0.0, w - 2 * pad), avail - need - 6};
    return l;
}

std::string diagnosticHit(const Object& o, double w, double h, double x, double y, std::size_t rows) {
    const auto l = diagnosticLayout(o, w, h, rows);
    if (l.reconnect.w > 0 && l.reconnect.contains(x, y)) return "bouton:reconnecter";
    if (l.reset.w > 0 && l.reset.contains(x, y)) return "bouton:compteurs";
    return {};
}

} // namespace hmi::comm
