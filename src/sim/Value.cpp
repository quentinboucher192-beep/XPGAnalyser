#include "Value.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

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
    return Type::Unknown;
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
        case Type::Unknown: return "?";
        default:
            std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(integer_));
            return buf;
    }
}

} // namespace sim
