// sim/ValueText.cpp - 1.12.19 : la lecture d'une valeur tapee, partout la meme.
#include "ValueText.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace sim {
namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(uc(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(uc(s.back()))) s.remove_suffix(1);
    return s;
}

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(uc(a[i])) != std::toupper(uc(b[i]))) return false;
    return true;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(uc(c)));
    return s;
}

// Sans les separateurs qu'on tape ou qu'on copie : _, espaces, espaces insecables et fines.
std::string compact(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = uc(s[i]);
        if (c == '_' || std::isspace(c)) continue;
        if (c == 0xE2 && i + 2 < s.size() && uc(s[i + 1]) == 0x80 && uc(s[i + 2]) == 0xAF) { i += 2; continue; }
        if (c == 0xC2 && i + 1 < s.size() && uc(s[i + 1]) == 0xA0) { ++i; continue; }
        out += s[i];
    }
    return out;
}

std::string quote(std::string_view raw) { return "\xC2\xAB " + std::string(raw) + " \xC2\xBB"; }

// Une chaine ST entre apostrophes ($' $$ $N $T, '' aussi), ou entre guillemets : son texte.
std::string unquoted(std::string_view raw) {
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') return std::string(raw.substr(1, raw.size() - 2));
    if (raw.size() < 2 || raw.front() != '\'' || raw.back() != '\'') return std::string(raw);
    const std::string_view in = raw.substr(1, raw.size() - 2);
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '$' && i + 1 < in.size()) {
            const char n = in[++i];
            if (n == 'N' || n == 'n' || n == 'L' || n == 'l') out += '\n';
            else if (n == 'T' || n == 't') out += '\t';
            else if (n == 'R' || n == 'r') out += '\r';
            else out += n;                                  // $' $$
            continue;
        }
        if (c == '\'' && i + 1 < in.size() && in[i + 1] == '\'') { out += '\''; ++i; continue; }
        out += c;
    }
    return out;
}

bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }

// Le prefixe d'une duree (T#, TIME#, LTIME#), en majuscules ; sa longueur, 0 sans.
std::size_t durationPrefix(std::string_view up) {
    for (const std::string_view p : {"LTIME#", "TIME#", "T#"})
        if (startsWith(up, p)) return p.size();
    return 0;
}

// Le type d'une date d'apres son prefixe (D#, DATE#, TOD#, TIME_OF_DAY#, DT#, DATE_AND_TIME#) ; Unknown sans.
Type datePrefix(std::string_view up, std::size_t& length) {
    struct P { std::string_view text; Type type; };
    for (const P p : {P{"DATE_AND_TIME#", Type::Dt}, P{"TIME_OF_DAY#", Type::Tod}, P{"DATE#", Type::Date}, P{"TOD#", Type::Tod},
                      P{"DT#", Type::Dt}, P{"D#", Type::Date}})
        if (startsWith(up, p.text)) {
            length = p.text.size();
            return p.type;
        }
    length = 0;
    return Type::Unknown;
}

bool isIdentifier(std::string_view s) {
    if (s.empty() || !(std::isalpha(uc(s.front())) || s.front() == '_')) return false;
    for (const char c : s)
        if (!(std::isalnum(uc(c)) || c == '_')) return false;
    return true;
}

std::string grouped(std::string_view digits) {
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += digits[i];
    }
    return out;
}

std::string typeName(Type t) { return std::string(toString(t)); }

} // namespace

bool trueWord(std::string_view text) noexcept {
    const auto t = trimmed(text);
    return iequal(t, "TRUE") || iequal(t, "VRAI") || iequal(t, "OUI") || iequal(t, "ON") || iequal(t, "YES") || t == "1";
}

bool falseWord(std::string_view text) noexcept {
    const auto t = trimmed(text);
    return iequal(t, "FALSE") || iequal(t, "FAUX") || iequal(t, "NON") || iequal(t, "OFF") || iequal(t, "NO") || t == "0";
}

bool readDecimal(std::string_view text, double& out) noexcept {
    std::string n = compact(trimmed(text));
    if (n.empty()) return false;
    for (auto& c : n) {
        if (c == ',') c = '.';
        // strtod lit aussi 0x1A, inf, nan : pas un nombre tape ici.
        if (!(std::isdigit(uc(c)) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) return false;
    }
    char* end = nullptr;
    const double v = std::strtod(n.c_str(), &end);
    if (end == n.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool readInteger(std::string_view text, long long& out) noexcept {
    double d = 0;
    if (!readDecimal(text, d) || d != std::floor(d) || d < -9.2e18 || d > 9.2e18) return false;
    out = static_cast<long long>(d);
    return true;
}

bool readDuration(std::string_view text, std::int64_t& ms, double bareMs) noexcept {
    std::string t = upper(compact(trimmed(text)));
    if (const std::size_t p = durationPrefix(t); p > 0) {
        t.erase(0, p);
        bareMs = 1.0;                                      // T#500 : des millisecondes, comme le ST
    }
    bool negative = false;
    if (!t.empty() && (t.front() == '-' || t.front() == '+')) {
        negative = t.front() == '-';
        t.erase(0, 1);
    }
    if (t.empty()) return false;
    for (auto& c : t)
        if (c == ',') c = '.';
    const auto number = [](const std::string& piece, double& out) {
        if (piece.empty() || piece.find_first_not_of("0123456789.") != std::string::npos) return false;
        char* end = nullptr;
        out = std::strtod(piece.c_str(), &end);
        return end && *end == '\0';
    };
    double total = 0.0;
    // 00:05 (mm:ss), 01:30:00 (hh:mm:ss)
    if (t.find(':') != std::string::npos) {
        double parts[3] = {0, 0, 0};
        std::size_t count = 0, from = 0;
        while (true) {
            const auto colon = t.find(':', from);
            if (count == 3 || !number(t.substr(from, colon == std::string::npos ? std::string::npos : colon - from), parts[count])) return false;
            ++count;
            if (colon == std::string::npos) break;
            from = colon + 1;
        }
        if (count < 2) return false;
        total = (count == 3 ? parts[0] * 3600.0 + parts[1] * 60.0 + parts[2] : parts[0] * 60.0 + parts[1]) * 1000.0;
    } else {
        // Des nombres suivis d'une unite : 1h2m3s4ms, 1,5 s, 250 ms, 2 min, 2d4h ; un nombre seul apres
        // h : des minutes (1 h 30), apres min : des secondes (2 min 30) ; seul : dans l'unite `bareMs`.
        bool any = false;
        std::string last;
        std::size_t i = 0;
        while (i < t.size()) {
            std::size_t j = i;
            while (j < t.size() && (std::isdigit(uc(t[j])) || t[j] == '.')) ++j;
            double n = 0;
            if (!number(t.substr(i, j - i), n)) return false;
            std::size_t k = j;
            while (k < t.size() && std::isalpha(uc(t[k]))) ++k;
            std::string unit = t.substr(j, k - j);
            if (unit.empty()) {
                if (k != t.size()) return false;
                unit = last == "H" ? "MIN" : (last == "MIN" || last == "M" || last == "MN") ? "S" : any ? std::string("?") : std::string{};
            }
            double factor = 0.0;
            if (unit.empty()) factor = bareMs;
            else if (unit == "D" || unit == "J") factor = 86400000.0;
            else if (unit == "H") factor = 3600000.0;
            else if (unit == "M" || unit == "MIN" || unit == "MN") factor = 60000.0;
            else if (unit == "S" || unit == "SEC") factor = 1000.0;
            else if (unit == "MS") factor = 1.0;
            else return false;
            total += n * factor;
            last = unit;
            any = true;
            i = k;
        }
        if (!any) return false;
    }
    if (!std::isfinite(total) || total > 9.0e18) return false;
    ms = static_cast<std::int64_t>(std::llround(negative ? -total : total));
    return true;
}

bool integerBounds(Type t, long long& lo, long long& hi) noexcept {
    switch (t) {
        case Type::Byte:  lo = 0; hi = 255; return true;
        case Type::Word:
        case Type::UInt:  lo = 0; hi = 65535; return true;
        case Type::Int:   lo = -32768; hi = 32767; return true;
        case Type::DInt:  lo = -2147483648LL; hi = 2147483647LL; return true;
        case Type::DWord:
        case Type::UDInt: lo = 0; hi = 4294967295LL; return true;
        case Type::LInt:  lo = std::numeric_limits<long long>::min(); hi = std::numeric_limits<long long>::max(); return true;
        case Type::ULInt:
        case Type::LWord: lo = 0; hi = std::numeric_limits<long long>::max(); return true;
        default: return false;
    }
}

std::string thousands(long long n) {
    const bool negative = n < 0;
    const unsigned long long a = negative ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
    return (negative ? std::string("-") : std::string{}) + grouped(std::to_string(a));
}

std::string boundsText(Type type) {
    long long lo = 0, hi = 0;
    if (!integerBounds(type, lo, hi)) return {};
    return thousands(lo) + " \xC3\xA0 " + thousands(hi);
}

bool readTyped(std::string_view text, Type type, Value& out, std::string& why) {
    const std::string raw(trimmed(text));
    if (raw.empty()) {
        why = "rien n'est tap\xC3\xA9";
        return false;
    }
    // Un texte : tel quel, ses apostrophes (ou guillemets) retirees.
    if (type == Type::String || (type == Type::Unknown && raw.size() >= 2 && raw.front() == '\'' && raw.back() == '\'')) {
        out = Value::text(unquoted(raw));
        return true;
    }
    std::string t = compact(raw);
    std::string up = upper(t);
    // Un prefixe de type (INT#5, REAL#1.5) n'apprend rien ; une base (16#FF), une duree (T#5S)
    // ou une date (D#2026-10-09), si.
    if (const auto hash = up.find('#'); hash != std::string::npos) {
        const std::string head = up.substr(0, hash);
        std::size_t dateLength = 0;
        const bool kept = durationPrefix(up) != 0 || datePrefix(up, dateLength) != Type::Unknown || !isIdentifier(head);
        if (!kept) {
            t = t.substr(hash + 1);
            up = up.substr(hash + 1);
        }
    }
    if (type == Type::Bool) {
        if (trueWord(up)) { out = Value::boolean(true); return true; }
        if (falseWord(up)) { out = Value::boolean(false); return true; }
        why = quote(raw) + " n'est pas un bool\xC3\xA9" "en : TRUE ou FALSE (VRAI, OUI, ON, 1 aussi)";
        return false;
    }
    if (type == Type::Unknown && up != "1" && up != "0") {
        if (trueWord(up)) { out = Value::boolean(true); return true; }
        if (falseWord(up)) { out = Value::boolean(false); return true; }
    }
    // Une date, une heure, une date et heure.
    {
        std::size_t length = 0;
        const Type prefixed = datePrefix(upper(raw), length);
        if (isDateType(type) || (type == Type::Unknown && prefixed != Type::Unknown)) {
            const Type target = isDateType(type) ? type : prefixed;
            std::string body = raw.substr(length);
            std::int64_t ms = 0;
            bool ok = parseDateText(target, body, ms);
            if (!ok && body.find(' ') != std::string::npos) {   // 2026-10-09 14:30 : l'espace pour le tiret
                std::string dashed = body;
                for (auto& c : dashed) if (c == ' ') c = '-';
                ok = parseDateText(target, dashed, ms);
            }
            if (!ok) {
                why = quote(raw) + (target == Type::Tod ? " n'est pas une heure : 14:30 ou 14:30:15"
                                    : target == Type::Dt ? " n'est pas une date et heure : 2026-10-09-14:30"
                                                         : " n'est pas une date : 2026-10-09");
                return false;
            }
            out = Value::integer(target, ms);
            return true;
        }
    }
    // Une duree (un TIME, ou T#... dans une case qui l'accepte comme un nombre de ms).
    if (type == Type::Time || durationPrefix(up) != 0) {
        std::int64_t ms = 0;
        if (!readDuration(t, ms)) {
            why = quote(raw) + " n'est pas une dur\xC3\xA9" "e : T#5s, 1m30s, 250 ms";
            return false;
        }
        if (type == Type::Time && (ms < 0 || ms > 4294967295LL)) {
            why = quote(raw) + " est hors des bornes d'un TIME";
            return false;
        }
        if (type == Type::Time || type == Type::Unknown) out = Value::time(ms);
        else if (type == Type::Real) out = Value::real(static_cast<double>(ms));
        else if (isInteger(type)) out = Value::integer(type, ms);
        else {
            why = "une dur\xC3\xA9" "e ne va pas dans un " + typeName(type);
            return false;
        }
        return true;
    }
    // Une base : 16#FF, 2#1010, 8#17 - le motif de bits, a la largeur du type.
    {
        std::string digits = up;
        bool negative = false;
        if (!digits.empty() && (digits.front() == '-' || digits.front() == '+')) {
            negative = digits.front() == '-';
            digits.erase(0, 1);
        }
        if (const auto hash = digits.find('#'); hash != std::string::npos) {
            const int base = std::atoi(digits.substr(0, hash).c_str());
            digits = digits.substr(hash + 1);
            char* end = nullptr;
            errno = 0;
            const unsigned long long n = std::strtoull(digits.c_str(), &end, base);
            if ((base != 2 && base != 8 && base != 16) || digits.empty() || !end || *end != '\0' || errno == ERANGE) {
                why = quote(raw) + " ne se lit pas : 16#FF, 2#1010, 8#17";
                return false;
            }
            if (type == Type::Real) {
                out = Value::real(negative ? -static_cast<double>(n) : static_cast<double>(n));
                return true;
            }
            const Type target = type == Type::Unknown ? Type::DInt : type;
            const int bits = bitWidth(target);
            const unsigned long long max = bits >= 64 ? ~0ULL : (1ULL << bits) - 1ULL;
            if (!isInteger(target) || n > max) {
                why = quote(raw) + " est trop grand pour un " + typeName(target);
                return false;
            }
            const auto value = static_cast<long long>(n);
            out = Value::integer(target, negative ? -value : value);
            return true;
        }
    }
    // Un nombre : la virgule francaise comme le point.
    double d = 0;
    if (!readDecimal(t, d)) {
        why = quote(raw) + " n'est pas un nombre" + (type == Type::Unknown ? std::string{} : " (" + typeName(type) + ")");
        return false;
    }
    std::string n = t;
    for (auto& c : n) if (c == ',') c = '.';
    if (type == Type::Real || (type == Type::Unknown && n.find_first_of(".eE") != std::string::npos)) {
        out = Value::real(d);
        return true;
    }
    const Type target = type == Type::Unknown ? Type::DInt : type;
    if (!isInteger(target)) {
        why = "une valeur " + typeName(target) + " ne s'\xC3\xA9" "crit pas ainsi";
        return false;
    }
    if (std::floor(d) != d) {
        why = quote(raw) + " n'est pas un nombre entier (" + typeName(target) + ")";
        return false;
    }
    long long lo = 0, hi = 0;
    (void)integerBounds(target, lo, hi);
    // Exact au-dela de 2^53 quand le texte n'a que des chiffres.
    long long exact = 0;
    bool exactRead = false;
    if (n.find_first_of(".eE") == std::string::npos) {
        errno = 0;
        char* end = nullptr;
        const long long v = std::strtoll(n.c_str(), &end, 10);
        if (end && *end == '\0' && errno != ERANGE) {
            exact = v;
            exactRead = true;
        } else if (end && *end == '\0' && errno == ERANGE && n.front() != '-') {
            errno = 0;
            const unsigned long long u = std::strtoull(n.c_str(), &end, 10);
            if (errno != ERANGE && (target == Type::ULInt || target == Type::LWord)) {
                out = Value::integer(target, static_cast<std::int64_t>(u));
                return true;
            }
            why = raw + " est hors des bornes d'un " + typeName(target) + " (" + boundsText(target) + ")";
            return false;
        }
    }
    const bool outside = exactRead ? (exact < lo || exact > hi) : (d < static_cast<double>(lo) || d > static_cast<double>(hi));
    if (outside) {
        why = raw + " est hors des bornes d'un " + typeName(target) + " (" + boundsText(target) + ")";
        return false;
    }
    out = Value::integer(target, exactRead ? exact : static_cast<std::int64_t>(d));
    return true;
}

std::string fixedText(const Value& value) {
    char buf[64];
    switch (value.type()) {
        case Type::Bool: return value.isTruthy() ? "TRUE" : "FALSE";
        case Type::Real: {
            const double v = value.asReal();
            if (!std::isfinite(v)) return std::isnan(v) ? "NaN" : (v < 0 ? "-INF" : "INF");
            // Le plus court qui se relit pareil (la precision d'un REAL de l'automate d'abord).
            const bool single = static_cast<double>(static_cast<float>(v)) == v;
            for (int p = 1; p <= 17; ++p) {
                std::snprintf(buf, sizeof buf, "%.*g", p, v);
                const double back = std::strtod(buf, nullptr);
                if (single ? static_cast<float>(back) == static_cast<float>(v) : back == v) break;
            }
            return buf;
        }
        case Type::Time: {
            std::int64_t ms = value.asInteger();
            std::string out = "T#";
            if (ms < 0) {
                out += '-';
                ms = -ms;
            }
            if (ms == 0) return out + "0ms";
            const struct { std::int64_t size; const char* unit; } parts[] = {
                {86400000, "d"}, {3600000, "h"}, {60000, "m"}, {1000, "s"}, {1, "ms"}};
            for (const auto& part : parts)
                if (ms >= part.size) {
                    out += std::to_string(ms / part.size) + part.unit;
                    ms %= part.size;
                }
            return out;
        }
        case Type::String: return value.asString();
        case Type::Date:
        case Type::Tod:
        case Type::Dt: return dateText(value.type(), value.asInteger());
        case Type::Unknown: return {};
        case Type::ULInt:
        case Type::LWord:
            std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(value.asInteger()));
            return buf;
        default:
            std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(value.asInteger()));
            return buf;
    }
}

} // namespace sim
