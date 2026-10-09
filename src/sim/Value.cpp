#include "Value.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace sim {

std::string_view toString(Type t) noexcept {
    switch (t) {
        case Type::Bool:   return "BOOL";
        case Type::Byte:   return "BYTE";
        case Type::Word:   return "WORD";
        case Type::DWord:  return "DWORD";
        case Type::Int:    return "INT";
        case Type::DInt:   return "DINT";
        case Type::UInt:   return "UINT";
        case Type::UDInt:  return "UDINT";
        case Type::Real:   return "REAL";
        case Type::Time:   return "TIME";
        case Type::String: return "STRING";
        case Type::Date:   return "DATE";
        case Type::Tod:    return "TIME_OF_DAY";
        case Type::Dt:     return "DATE_AND_TIME";
        case Type::Unknown: break;
    }
    return "?";
}

Type typeFromName(std::string_view name) noexcept {
    std::string upper(name);
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    // EBOOL behaves as BOOL for everything the interpreter does; its history and
    // forcing bits are a PLC concern, not an expression-evaluation one.
    if (upper == "BOOL" || upper == "EBOOL") return Type::Bool;
    if (upper == "BYTE")   return Type::Byte;
    if (upper == "WORD")   return Type::Word;
    if (upper == "DWORD")  return Type::DWord;
    if (upper == "INT")    return Type::Int;
    if (upper == "DINT")   return Type::DInt;
    if (upper == "UINT")   return Type::UInt;
    if (upper == "UDINT")  return Type::UDInt;
    if (upper == "REAL")   return Type::Real;
    if (upper == "TIME")   return Type::Time;
    if (upper.rfind("STRING", 0) == 0) return Type::String;
    // 1.12.1 : CHAR et WSTRING sont des textes ; les dates, des millisecondes.
    if (upper == "CHAR" || upper.rfind("WSTRING", 0) == 0) return Type::String;
    if (upper == "DATE") return Type::Date;
    if (upper == "TIME_OF_DAY" || upper == "TOD") return Type::Tod;
    if (upper == "DATE_AND_TIME" || upper == "DT") return Type::Dt;
    return Type::Unknown;
}

bool isDateType(Type t) noexcept { return t == Type::Date || t == Type::Tod || t == Type::Dt; }

// Le calendrier (H. Hinnant, "chrono-Compatible Low-Level Date Algorithms") : sans
// gmtime ni timegm, le meme sous Windows.
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

namespace {
constexpr std::int64_t kDayMs = 86400000;
std::int64_t floorDiv(std::int64_t a, std::int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

std::string clockText(std::int64_t ms) {
    char buf[32];
    const auto h = ms / 3600000, mi = ms / 60000 % 60, s = ms / 1000 % 60, f = ms % 1000;
    if (f) std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld.%03lld", static_cast<long long>(h), static_cast<long long>(mi),
                         static_cast<long long>(s), static_cast<long long>(f));
    else std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld", static_cast<long long>(h), static_cast<long long>(mi), static_cast<long long>(s));
    return buf;
}
std::string dayText(std::int64_t days) {
    std::int64_t y = 0;
    unsigned m = 0, d = 0;
    civilFromDays(days, y, m, d);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return buf;
}
// "14:30", "14:30:15", "14:30:15.25" : des millisecondes depuis minuit.
bool parseClock(std::string_view t, std::int64_t& ms) noexcept {
    std::int64_t part[3] = {0, 0, 0};
    int n = 0;
    std::size_t i = 0;
    double seconds = 0;
    while (n < 3) {
        const std::size_t start = i;
        while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || (n == 2 && t[i] == '.'))) ++i;
        if (i == start) return false;
        const std::string piece(t.substr(start, i - start));
        if (n == 2) seconds = std::atof(piece.c_str());
        else part[n] = std::atoll(piece.c_str());
        ++n;
        if (i >= t.size()) break;
        if (t[i] != ':') return false;
        ++i;
    }
    if (i != t.size() || n < 2) return false;
    if (part[0] > 23 || part[1] > 59 || seconds >= 60.0) return false;
    ms = part[0] * 3600000 + part[1] * 60000 + static_cast<std::int64_t>(std::llround(seconds * 1000.0));
    return true;
}
// "2026-10-09" : des jours depuis le 1970-01-01.
bool parseDay(std::string_view t, std::int64_t& days) noexcept {
    int y = 0;
    unsigned m = 0, d = 0;
    char tail = 0;
    const std::string s(t);
    if (std::sscanf(s.c_str(), "%d-%u-%u%c", &y, &m, &d, &tail) != 3) return false;
    if (m < 1 || m > 12 || d < 1) return false;
    static const unsigned kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (d > kDays[m - 1] + (m == 2 && leap ? 1u : 0u)) return false;
    days = daysFromCivil(y, m, d);
    return true;
}
} // namespace

std::string dateText(Type t, std::int64_t ms) {
    if (t == Type::Tod) return clockText(((ms % kDayMs) + kDayMs) % kDayMs);
    const std::int64_t days = floorDiv(ms, kDayMs);
    if (t == Type::Date) return dayText(days);
    return dayText(days) + "-" + clockText(ms - days * kDayMs);
}

bool parseDateText(Type t, std::string_view text, std::int64_t& ms) noexcept {
    std::int64_t days = 0, clock = 0;
    if (t == Type::Tod) return parseClock(text, ms);
    if (t == Type::Date) {
        if (!parseDay(text, days)) return false;
        ms = days * kDayMs;
        return true;
    }
    if (t != Type::Dt) return false;
    // 2026-10-09-14:30:00 : la date, un tiret, l'heure.
    std::size_t dash = 0;
    for (int k = 0; k < 3 && dash != std::string_view::npos; ++k) dash = text.find('-', k == 0 ? 1 : dash + 1);
    if (dash == std::string_view::npos) return false;
    if (!parseDay(text.substr(0, dash), days) || !parseClock(text.substr(dash + 1), clock)) return false;
    ms = days * kDayMs + clock;
    return true;
}

bool isInteger(Type t) noexcept {
    switch (t) {
        case Type::Byte: case Type::Word: case Type::DWord:
        case Type::Int:  case Type::DInt: case Type::UInt: case Type::UDInt:
        case Type::Time: return true;
        default: return false;
    }
}

bool isNumeric(Type t) noexcept { return isInteger(t) || t == Type::Real; }

int bitWidth(Type t) noexcept {
    switch (t) {
        case Type::Bool:  return 1;
        case Type::Byte:  return 8;
        case Type::Word:  case Type::Int: case Type::UInt: return 16;
        case Type::DWord: case Type::DInt: case Type::UDInt: case Type::Time: return 32;
        default: return 64;
    }
}

std::int64_t truncateTo(Type t, std::int64_t v) {
    // The PLC wraps; it does not saturate and it does not promote. A counter
    // declared INT that reaches 32767 goes to -32768, and a simulator that
    // quietly kept counting would hide exactly the bug worth finding.
    switch (t) {
        case Type::Bool:  return v != 0 ? 1 : 0;
        case Type::Byte:  return static_cast<std::uint8_t>(v);
        case Type::Word:  case Type::UInt: return static_cast<std::uint16_t>(v);
        case Type::Int:   return static_cast<std::int16_t>(v);
        case Type::DWord: case Type::UDInt: case Type::Time:
                          return static_cast<std::uint32_t>(v);
        case Type::DInt:  return static_cast<std::int32_t>(v);
        default:          return v;
    }
}

Value Value::boolean(bool v) {
    Value out;
    out.type_ = Type::Bool;
    out.integer_ = v ? 1 : 0;
    return out;
}

Value Value::integer(Type t, std::int64_t v) {
    Value out;
    out.type_ = t;
    out.integer_ = truncateTo(t, v);
    return out;
}

Value Value::real(double v) {
    Value out;
    out.type_ = Type::Real;
    out.real_ = v;
    return out;
}

Value Value::time(std::int64_t ms) { return integer(Type::Time, ms); }

Value Value::text(std::string v) {
    Value out;
    out.type_ = Type::String;
    out.text_ = std::move(v);
    return out;
}

Value Value::defaultOf(Type t) {
    if (t == Type::Real)   return real(0.0);
    if (t == Type::String) return text({});
    Value out;
    out.type_ = t == Type::Unknown ? Type::Int : t;
    return out;
}

bool Value::isTruthy() const noexcept {
    if (type_ == Type::Real) return real_ != 0.0;
    return integer_ != 0;
}

std::int64_t Value::asInteger() const noexcept {
    if (type_ == Type::Real) return static_cast<std::int64_t>(real_);
    return integer_;
}

double Value::asReal() const noexcept {
    if (type_ == Type::Real) return real_;
    return static_cast<double>(integer_);
}

void Value::assignFrom(const Value& v) {
    // The slot keeps its declared type; the value is converted into it. This is
    // what makes `intVar := realExpr` truncate rather than turn the slot REAL.
    switch (type_) {
        case Type::Real:   real_ = v.asReal(); break;
        case Type::String: text_ = v.type() == Type::String ? v.asString() : v.display(); break;
        case Type::Unknown:
            *this = v;
            break;
        default:
            integer_ = truncateTo(type_, v.type() == Type::Real
                                             ? static_cast<std::int64_t>(std::llround(v.asReal()))
                                             : v.asInteger());
            break;
    }
}

bool Value::equals(const Value& other) const { return compare(other) == 0; }

int Value::compare(const Value& other) const {
    if (type_ == Type::String || other.type_ == Type::String) {
        const auto a = type_ == Type::String ? text_ : display();
        const auto b = other.type_ == Type::String ? other.text_ : other.display();
        return a < b ? -1 : (a == b ? 0 : 1);
    }
    if (type_ == Type::Real || other.type_ == Type::Real) {
        const double a = asReal(), b = other.asReal();
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    const auto a = asInteger(), b = other.asInteger();
    return a < b ? -1 : (a > b ? 1 : 0);
}

std::string Value::display() const {
    char buf[64];
    switch (type_) {
        case Type::Bool:   return integer_ ? "TRUE" : "FALSE";
        case Type::Real:
            std::snprintf(buf, sizeof buf, "%g", real_);
            return buf;
        case Type::Time:
            // Shown the way it is written: T#1s500ms rather than 1500.
            std::snprintf(buf, sizeof buf, "T#%lldms", static_cast<long long>(integer_));
            return buf;
        case Type::String: return "'" + text_ + "'";
        case Type::Date:   return "D#" + dateText(type_, integer_);       // 1.12.1
        case Type::Tod:    return "TOD#" + dateText(type_, integer_);
        case Type::Dt:     return "DT#" + dateText(type_, integer_);
        case Type::Unknown: return "?";
        default:
            std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(integer_));
            return buf;
    }
}

} // namespace sim
